/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Loosely based on apt-daemon/debconf.py, which is:
 *   Copyright (C) 2009 Sebastian Heinlein <devel@glatzor.de>
 *   Copyright (C) 2009 Michael Vogt <michael.vogt@ubuntu.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * PkClientHelper:
 *
 * helper object to run a helper session process for the lifetime of a transaction
 *
 * This GObject can be used to run a session helper program out of band
 * with the normal PackageKit transaction. This allows an external program
 * such as debconf to be used that needs direct console access.
 *
 *
 *   client ----> libpackagekit ---> dbus ---> packagekitd ---> apt
 *          .------------^                                         ^
 *   debconf ___________________          (GetFrontendSocket)      |
 *    | \___|  PkClientHelper   \__.____.____.______/_.____.____.__/
 *    ^-----|___________________/      (socket in /run/packagekit)
 *   (stdin & stdout )
 *
 *  \------------.------------------/          \------------.---------/
 *               |                                          |
 *          user session                              system context
 */

#include "config.h"

#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <glib-object.h>
#include <gio/gunixconnection.h>
#include <gio/gunixsocketaddress.h>

#include "pk-client-helper-private.h"
#include "pk-debug.h"

static void pk_client_helper_finalize (GObject *object);

/**
 * PkClientHelperPrivate:
 *
 * Private #PkClientHelper data
 **/
struct _PkClientHelperPrivate
{
	gchar **argv;
	gchar **envp;
	GSocket *socket;
	GIOChannel *socket_channel;
	GSource *socket_channel_source;
	GPtrArray *children;
};

typedef struct
{
	PkClientHelper *helper;
	GSocket *socket;
	GIOChannel *socket_channel;
	GSource *socket_channel_source;
	GPid pid;
	GIOChannel *stdin_channel;
	GIOChannel *stdout_channel;
	GIOChannel *stderr_channel;
	GSource *stdout_channel_source;
	GSource *stderr_channel_source;
} PkClientHelperChild;

G_DEFINE_TYPE_WITH_PRIVATE (PkClientHelper, pk_client_helper, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (pk_client_helper_get_instance_private (o))

/* how long to wait for a helper to exit on SIGTERM before escalating to SIGKILL */
#define PK_CLIENT_HELPER_SIGKILL_TIMEOUT 5  /* seconds */
#define PK_CLIENT_HELPER_POLL_DELAY	 50 /* ms */

/*
 * pk_client_helper_kill_child:
 *
 * Terminates a helper child gracefully: sends SIGTERM and, if the process is
 * still alive after PK_CLIENT_HELPER_SIGKILL_TIMEOUT, escalates to SIGKILL.
 * The child is reaped so it does not linger as a zombie.
 *
 * The children must be spawned with %G_SPAWN_DO_NOT_REAP_CHILD.
 **/
static void
pk_client_helper_kill_child (GPid pid)
{
	guint elapsed_ms = 0;

	if (pid <= 0)
		return;

	g_debug ("sending SIGTERM %ld", (long) pid);
	if (kill (pid, SIGTERM) < 0 && errno == ESRCH) {
		/* already gone, just reap it */
		waitpid (pid, NULL, WNOHANG);
		return;
	}

	/* wait for the child to exit, polling so we can escalate if needed */
	while (elapsed_ms < PK_CLIENT_HELPER_SIGKILL_TIMEOUT * 1000) {
		if (waitpid (pid, NULL, WNOHANG) == pid)
			return;
		g_usleep (PK_CLIENT_HELPER_POLL_DELAY * 1000);
		elapsed_ms += PK_CLIENT_HELPER_POLL_DELAY;
	}

	/* still running, murder it */
	g_debug ("helper %ld did not exit on SIGTERM, sending SIGKILL", (long) pid);
	kill (pid, SIGKILL);
	waitpid (pid, NULL, 0);
}

static void
pk_client_helper_child_free (PkClientHelperChild *child)
{
	if (child->socket != NULL)
		g_object_unref (child->socket);
	if (child->socket_channel != NULL)
		g_io_channel_unref (child->socket_channel);
	if (child->socket_channel_source != NULL) {
		g_source_destroy (child->socket_channel_source);
		g_source_unref (child->socket_channel_source);
	}
	if (child->pid > 0)
		pk_client_helper_kill_child (child->pid);
	if (child->stdin_channel != NULL)
		g_io_channel_unref (child->stdin_channel);
	if (child->stdout_channel != NULL)
		g_io_channel_unref (child->stdout_channel);
	if (child->stderr_channel != NULL)
		g_io_channel_unref (child->stderr_channel);
	if (child->stdout_channel_source != NULL) {
		g_source_destroy (child->stdout_channel_source);
		g_source_unref (child->stdout_channel_source);
	}
	if (child->stderr_channel_source != NULL) {
		g_source_destroy (child->stderr_channel_source);
		g_source_unref (child->stderr_channel_source);
	}
}

/**
 * pk_client_helper_stop:
 * @client_helper: a valid #PkClientHelper instance
 * @error: A #GError or %NULL
 *
 * Stops the helper process, by killing it and closing the socket.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_helper_stop (PkClientHelper *client_helper, GError **error)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* close the socket - there is none if the session's agent took over.
	 * Shutting it down affects every copy, so the backend's connects are
	 * refused instead of waiting forever for us to accept them */
	if (priv->socket_channel_source != NULL)
		g_source_destroy (priv->socket_channel_source);
	if (priv->socket != NULL) {
		g_socket_shutdown (priv->socket, TRUE, TRUE, NULL);
		if (!g_socket_close (priv->socket, error))
			return FALSE;
	}

	/* kill any children */
	for (guint i = 0; i < priv->children->len; i++) {
		PkClientHelperChild *child = g_ptr_array_index (priv->children, i);
		pk_client_helper_kill_child (child->pid);
	}

	return TRUE;
}

/*
 * pk_client_helper_copy_stdout_cb:
 **/
static gboolean
pk_client_helper_copy_stdout_cb (GIOChannel *source,
				 GIOCondition condition,
				 PkClientHelperChild *child)
{
	gchar data[1024];
	gsize len = 0;
	gsize written = 0;
	GIOStatus status;
	g_autoptr(GError) error = NULL;

	/* read data */
	status = g_io_channel_read_chars (source, data, sizeof (data) - 1, &len, &error);
	if (status == G_IO_STATUS_ERROR)
		g_warning ("failed to read from helper stdout: %s", error->message);
	if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR) {
		g_debug ("helper process exited");
		g_clear_error (&error);

		/* reap it now if the process is gone; we own reaping since it was spawned with G_SPAWN_DO_NOT_REAP_CHILD.
		 * If it has not exited yet (stdout closed but still running), leave it for kill_child(). */
		if (child->pid > 0 && waitpid (child->pid, NULL, WNOHANG) == child->pid)
			child->pid = -1;

		/* the socket watch is polling the fd we are about to close: leaving it
		 * attached would spin the main loop on POLLNVAL, and the fd number may
		 * later be reused by an unrelated descriptor */
		if (child->socket_channel_source != NULL)
			g_source_destroy (child->socket_channel_source);
		if (!g_socket_close (child->socket, &error))
			g_warning ("failed to close socket");
		return G_SOURCE_REMOVE;
	}
	if (len == 0)
		return G_SOURCE_CONTINUE;

	/* write to socket */
	data[len] = '\0';
	g_debug ("child has input to push to the socket: %s", data);
	status = g_io_channel_write_chars (child->socket_channel, data, len, &written, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to write to socket: %s",
			   error ? error->message : "Unknown error");
		return G_SOURCE_REMOVE;
	}
	if (written != len) {
		g_warning ("failed to write %" G_GSIZE_FORMAT " bytes, "
			   "only wrote %" G_GSIZE_FORMAT " bytes",
			   len,
			   written);
		return G_SOURCE_REMOVE;
	}
	g_debug ("wrote %" G_GSIZE_FORMAT " bytes to socket", written);

	return G_SOURCE_CONTINUE;
}

/*
 * pk_client_helper_echo_stderr_cb:
 **/
static gboolean
pk_client_helper_echo_stderr_cb (GIOChannel *source,
				 GIOCondition condition,
				 PkClientHelperChild *child)
{
	gchar data[1024];
	gsize len = 0;
	GIOStatus status;

	/* read data */
	status = g_io_channel_read_chars (source, data, sizeof (data) - 1, &len, NULL);
	if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_ERROR)
		return G_SOURCE_REMOVE;
	if (len == 0)
		return G_SOURCE_CONTINUE;

	/* write to socket */
	data[len] = '\0';
	g_debug ("child has error: %s", data);

	return G_SOURCE_CONTINUE;
}

/*
 * pk_client_helper_copy_conn_cb:
 **/
static gboolean
pk_client_helper_copy_conn_cb (GIOChannel *source,
			       GIOCondition condition,
			       PkClientHelperChild *child)
{
	PkClientHelperPrivate *helper_priv = pk_client_helper_get_instance_private (child->helper);
	gchar data[1024];
	gsize len = 0;
	GIOStatus status;
	gsize written = 0;
	g_autoptr(GError) error = NULL;

	status = g_io_channel_read_chars (source, data, sizeof (data) - 1, &len, &error);
	if (status == G_IO_STATUS_EOF) {
		g_debug ("socket hung up");

		/* Shutdown helper */
		if (g_io_channel_shutdown (child->stdin_channel, TRUE, &error) !=
		    G_IO_STATUS_NORMAL)
			g_warning ("failed to close connection to child: %s", error->message);
		return G_SOURCE_REMOVE;
	}
	if (status == G_IO_STATUS_ERROR) {
		g_warning ("failed to read: %s", error->message);
		return G_SOURCE_REMOVE;
	}
	if (len == 0)
		return G_SOURCE_CONTINUE;

	/* write to child */
	data[len] = '\0';
	g_debug ("socket has data to push to child: '%s'", data);
	status = g_io_channel_write_chars (child->stdin_channel, data, len, &written, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to write to stdin: %s",
			   error ? error->message : "Unknown error");
		return G_SOURCE_REMOVE;
	}
	if (written != len) {
		g_warning ("failed to write %" G_GSIZE_FORMAT " bytes, "
			   "only wrote %" G_GSIZE_FORMAT " bytes",
			   len,
			   written);
		return G_SOURCE_REMOVE;
	}
	g_debug ("wrote %" G_GSIZE_FORMAT " bytes to stdin of %s", written, helper_priv->argv[0]);

	return G_SOURCE_CONTINUE;
}

static GSource *
make_input_source (GIOChannel *channel,
		   GIOCondition condition,
		   GSourceFunc func,
		   gpointer user_data)
{
	GSource *source;
	GMainContext *context;

	/* G_IO_IN alone is not enough for the child pipes and sockets: a drained pipe
	 * whose writer has exited reports only G_IO_HUP, and GIOChannel's check()
	 * masks revents against the requested condition - so the callback would never
	 * be dispatched while poll() kept returning instantly, spinning the main loop
	 * at 100% CPU. Callers therefore also watch for hang-up and error, so the
	 * callback runs, reads the EOF and removes the source. */
	source = g_io_create_watch (channel, condition);
	g_source_set_callback (source, func, user_data, NULL);

	context = g_main_context_get_thread_default ();
	if (context == NULL)
		context = g_main_context_default ();
	g_source_attach (source, context);

	return source;
}

/*
 * pk_client_helper_accept_connection_cb:
 **/
static gboolean
pk_client_helper_accept_connection_cb (GIOChannel *source,
				       GIOCondition condition,
				       PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);
	g_autoptr(GSocket) socket = NULL;
	GPid pid;
	gint standard_input = 0;
	gint standard_output = 0;
	gint standard_error = 0;
	gint fd;
	GIOStatus status;
	PkClientHelperChild *child;
	g_autoptr(GError) error = NULL;

	/* the daemon shut the socket down, its transaction is gone; accepting
	 * would only fail over and over */
	if ((condition & (G_IO_HUP | G_IO_ERR)) > 0) {
		g_debug ("frontend socket was shut down");
		g_clear_pointer (&priv->socket_channel_source, g_source_unref);
		return G_SOURCE_REMOVE;
	}

	/* accept the connection request */
	socket = g_socket_accept (priv->socket, NULL, &error);
	if (socket == NULL) {
		g_warning ("failed to accept socket: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_debug ("accepting connection %i for socket %i",
		 g_socket_get_fd (socket),
		 g_socket_get_fd (priv->socket));

	/* spawn helper executable; we reap the child ourselves (see
	 * pk_client_helper_kill_child()) so GLib must not reap it for us */
	if (!g_spawn_async_with_pipes (NULL, /* working dir */
				       priv->argv,
				       priv->envp,
				       G_SPAWN_DO_NOT_REAP_CHILD,
				       NULL, /* child_setup */
				       NULL, /* child_setup user data */
				       &pid,
				       &standard_input,
				       &standard_output,
				       &standard_error,
				       &error)) {
		g_warning ("failed to spawn: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_debug ("started process %s with pid %i", priv->argv[0], pid);

	child = g_slice_new0 (PkClientHelperChild);
	g_ptr_array_add (priv->children, child);
	child->helper = client_helper;
	child->socket = g_steal_pointer (&socket);
	child->pid = pid;
	child->stdin_channel = g_io_channel_unix_new (standard_input);
	child->stdout_channel = g_io_channel_unix_new (standard_output);
	child->stderr_channel = g_io_channel_unix_new (standard_error);

	/* binary data */
	status = g_io_channel_set_encoding (child->stdin_channel, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_io_channel_set_buffered (child->stdin_channel, FALSE);

	/* binary data */
	status = g_io_channel_set_encoding (child->stdout_channel, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_io_channel_set_buffered (child->stdout_channel, FALSE);

	/* binary data */
	status = g_io_channel_set_encoding (child->stderr_channel, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_io_channel_set_buffered (child->stderr_channel, FALSE);

	/* socket has data */
	fd = g_socket_get_fd (child->socket);
	child->socket_channel = g_io_channel_unix_new (fd);
	child->socket_channel_source = make_input_source (
	    child->socket_channel,
	    G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
	    G_SOURCE_FUNC (pk_client_helper_copy_conn_cb),
	    child);
	/* binary data */
	status = g_io_channel_set_encoding (child->socket_channel, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_io_channel_set_buffered (child->socket_channel, FALSE);

	/* frontend has data */
	child->stdout_channel_source = make_input_source (
	    child->stdout_channel,
	    G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
	    G_SOURCE_FUNC (pk_client_helper_copy_stdout_cb),
	    child);
	child->stderr_channel_source = make_input_source (
	    child->stderr_channel,
	    G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
	    G_SOURCE_FUNC (pk_client_helper_echo_stderr_cb),
	    child);
	return G_SOURCE_CONTINUE;
}

static void
pk_client_helper_listen (PkClientHelper *client_helper, GSocket *socket)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);

	g_set_object (&priv->socket, socket);
	priv->socket_channel = g_io_channel_unix_new (g_socket_get_fd (socket));
	priv->socket_channel_source = make_input_source (
	    priv->socket_channel,
	    G_IO_IN | G_IO_HUP | G_IO_ERR,
	    G_SOURCE_FUNC (pk_client_helper_accept_connection_cb),
	    client_helper);
}

/*
 * pk_client_helper_session_agent:
 *
 * The socket of a helper the session runs itself, such as the
 * socket-activated pk-debconf-agent, or %NULL to spawn one per connection.
 **/
static gchar *
pk_client_helper_session_agent (gchar **envp)
{
	g_autofree gchar *path = NULL;

	/* only we can reach the current terminal, and the self tests need
	 * their own frontend */
	if (envp != NULL && g_strv_contains ((const gchar *const *) envp, "DEBIAN_FRONTEND=dialog"))
		return NULL;
	if (g_getenv ("PK_SELF_TEST") != NULL)
		return NULL;
	path = g_build_filename (g_get_user_runtime_dir (), "pk-debconf-agent.socket", NULL);
	if (!g_file_test (path, G_FILE_TEST_EXISTS))
		return NULL;

	return g_steal_pointer (&path);
}

/*
 * pk_client_helper_hand_over:
 *
 * Gives the listening socket to the helper of the session, so that questions
 * are answered even after this process is gone.
 **/
static gboolean
pk_client_helper_hand_over (GSocket *socket, const gchar *path, GError **error)
{
	g_autoptr(GSocketClient) client = g_socket_client_new ();
	g_autoptr(GSocketAddress) address = g_unix_socket_address_new (path);
	g_autoptr(GSocketConnection) conn = NULL;

	conn = g_socket_client_connect (client, G_SOCKET_CONNECTABLE (address), NULL, error);
	if (conn == NULL)
		return FALSE;
	return g_unix_connection_send_fd (G_UNIX_CONNECTION (conn),
					  g_socket_get_fd (socket),
					  NULL,
					  error);
}

/**
 * pk_client_helper_start:
 * @client_helper: a valid #PkClientHelper instance
 * @socket: the (bound and listening) #GSocket instance to use
 * @argv: (nullable): the frontend to run for every connection, with the
 *   connection on its stdin and stdout, or %NULL if the caller does not
 *   handle the questions itself
 * @envp: (nullable): the environment of @argv
 * @error: A #GError or %NULL
 *
 * Gets the questions asked on @socket answered. A caller that passes a
 * frontend handles them itself. Otherwise they go to the agent of the
 * session if it runs one, which outlives this process, and failing that
 * to the default frontend spawned for each connection.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_helper_start (PkClientHelper *client_helper,
			GSocket *socket,
			gchar **argv,
			gchar **envp,
			GError **error)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);
	g_auto(GStrv) default_argv = NULL;
	g_auto(GStrv) default_envp = NULL;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (socket != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* make sure not been started before */
	g_return_val_if_fail (priv->socket == NULL, FALSE);

	if (argv == NULL) {
		g_autofree gchar *agent = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!pk_client_helper_create_argv_envp (&default_argv, &default_envp)) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "no frontend available");
			return FALSE;
		}
		argv = default_argv;
		envp = default_envp;

		/* the agent cannot use our terminal, everything else it does better */
		agent = pk_client_helper_session_agent (envp);
		if (agent != NULL) {
			if (pk_client_helper_hand_over (socket, agent, &error_local))
				return TRUE;
			g_warning ("failed to hand over to %s: %s", agent, error_local->message);
		}
	}

	/* cache for actual start */
	priv->argv = g_strdupv (argv);
	priv->envp = g_strdupv (envp);
	pk_client_helper_listen (client_helper, socket);
	return TRUE;
}

/**
 * pk_client_helper_is_active:
 * @client_helper: a valid #PkClientHelper instance
 *
 * Return value: TRUE if there is an accepted connection, FALSE
 *               otherwise.
 *
 * Since: 1.1.13
 */
gboolean
pk_client_helper_is_active (PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);

	for (guint i = 0; i < priv->children->len; i++) {
		PkClientHelperChild *child = g_ptr_array_index (priv->children, i);

		/* the sources are NULL if child setup failed halfway through */
		if (child->socket_channel_source == NULL || child->stdout_channel_source == NULL)
			continue;
		if (!g_source_is_destroyed (child->socket_channel_source) &&
		    !g_source_is_destroyed (child->stdout_channel_source))
			return TRUE;
	}

	return FALSE;
}

/**
 * pk_client_helper_is_listening:
 * @client_helper: a valid #PkClientHelper instance
 *
 * Return value: TRUE if new connections can still arrive, FALSE once the
 *               socket was shut down or the helper stopped.
 */
gboolean
pk_client_helper_is_listening (PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);

	return priv->socket_channel_source != NULL &&
	       !g_source_is_destroyed (priv->socket_channel_source);
}

/*
 * pk_client_helper_class_init:
 **/
static void
pk_client_helper_class_init (PkClientHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_client_helper_finalize;
}

/*
 * pk_client_helper_init:
 **/
static void
pk_client_helper_init (PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);

	client_helper->priv = priv;
	priv->children = g_ptr_array_new_with_free_func (
	    (GDestroyNotify) pk_client_helper_child_free);
}

/*
 * pk_client_helper_finalize:
 **/
static void
pk_client_helper_finalize (GObject *object)
{
	PkClientHelper *client_helper = PK_CLIENT_HELPER (object);
	PkClientHelperPrivate *priv = GET_PRIVATE(client_helper);

	if (priv->socket_channel_source != NULL) {
		g_source_destroy (priv->socket_channel_source);
		g_clear_pointer (&priv->socket_channel_source, g_source_unref);
	}

	g_clear_pointer (&priv->argv, g_strfreev);
	g_clear_pointer (&priv->envp, g_strfreev);
	if (priv->socket != NULL) {
		g_socket_shutdown (priv->socket, TRUE, TRUE, NULL);
		g_socket_close (priv->socket, NULL);
		g_clear_object (&priv->socket);
	}
	g_clear_pointer (&priv->socket_channel, g_io_channel_unref);
	g_clear_pointer (&priv->children, g_ptr_array_unref);

	G_OBJECT_CLASS (pk_client_helper_parent_class)->finalize (object);
}

/*
 * pk_client_helper_create_argv_envp_test:
 *
 * The self tests use a tiny script instead of a debconf frontend.
 **/
static gboolean
pk_client_helper_create_argv_envp_test (gchar ***argv, gchar ***envp)
{
	const gchar *datadir;
	g_autofree gchar *helper = NULL;

	/* the test data directory is provided at runtime by the test harness */
	datadir = g_getenv ("PK_TEST_DATA_DIR");
	if (datadir == NULL)
		return FALSE;

	/* check we have the right file */
	helper = g_build_filename (datadir, "pk-client-helper-test.py", NULL);
	if (!g_file_test (helper, G_FILE_TEST_EXISTS)) {
		g_warning ("could not find the socket helper!");
		return FALSE;
	}

	/* setup simple test socket */
	*argv = g_new0 (gchar *, 2);
	(*argv)[0] = g_steal_pointer (&helper);
	return TRUE;
}

/**
 * pk_client_helper_create_argv_envp:
 * @argv: (out) (transfer full): the program to run for every connection
 * @envp_out: (out) (transfer full): its environment
 *
 * Picks the debconf frontend to spawn for the session: the KDE helper or
 * debconf-communicate, with the environment that lets it reach the
 * display or terminal.
 *
 * Return value: %FALSE if no frontend is available
 *
 * Since: 2.0.0
 **/
gboolean
pk_client_helper_create_argv_envp (gchar ***argv, gchar ***envp_out)
{
	const gchar *dialog = NULL;
	const gchar *desktop;
	gchar **envp;

	/* use the test helper */
	if (g_getenv ("PK_SELF_TEST") != NULL &&
	    pk_client_helper_create_argv_envp_test (argv, envp_out))
		return TRUE;

	/* check we have the right file */
	if (!g_file_test ("/usr/bin/debconf-communicate", G_FILE_TEST_EXISTS))
		return FALSE;

	/* the frontend runs in our session with everything it needs to look
	 * and behave like one of its programs; only the debconf setup is ours,
	 * so a passthrough setup of a script that started us does not leak in */
	envp = g_get_environ ();
	envp = g_environ_setenv (envp, "DEBCONF_DB_REPLACE", "configdb", TRUE);
	envp = g_environ_setenv (envp, "DEBCONF_DB_OVERRIDE", "Pipe{infd:none outfd:none}", TRUE);
	envp = g_environ_unsetenv (envp, "DEBCONF_PIPE");
	envp = g_environ_unsetenv (envp, "DEBCONF_READFD");
	envp = g_environ_unsetenv (envp, "DEBCONF_WRITEFD");
	if (pk_debug_is_verbose ())
		envp = g_environ_setenv (envp, "DEBCONF_DEBUG", ".", TRUE);

	/* do we have an available terminal to use */
	if (g_getenv ("TERM") != NULL)
		dialog = "dialog";

	/* do we have access to the display */
	desktop = g_getenv ("XDG_SESSION_DESKTOP");
	if (g_getenv ("DISPLAY") != NULL || g_getenv ("WAYLAND_DISPLAY") != NULL) {
		if (g_strcmp0 (desktop, "KDE") == 0 ||
		    g_strcmp0 (g_getenv ("KDE_FULL_SESSION"), "true") == 0)
			dialog = "kde";
		else
			dialog = "gnome";
	}

	/* indicate a prefered frontend */
	if (dialog != NULL) {
		envp = g_environ_setenv (envp, "DEBIAN_FRONTEND", dialog, TRUE);
		g_debug ("using frontend %s", dialog);
	}
	*envp_out = envp;

	/* the KDE helper talks debconf itself, on the fds we give it */
	*argv = g_new0 (gchar *, 3);
	if (g_strcmp0 (dialog, "kde") == 0) {
		const gchar *kde_helpers[] = { "/usr/libexec/debconf-kde-helper",
					       "/usr/bin/debconf-kde-helper" };
		for (guint i = 0; i < G_N_ELEMENTS (kde_helpers); i++) {
			if (g_file_test (kde_helpers[i], G_FILE_TEST_EXISTS)) {
				(*argv)[0] = g_strdup (kde_helpers[i]);
				(*argv)[1] = g_strdup ("--fifo-fds=0,1");
				break;
			}
		}
	}
	if ((*argv)[0] == NULL)
		(*argv)[0] = g_strdup ("/usr/bin/debconf-communicate");
	return TRUE;
}

/**
 * pk_client_helper_new:
 *
 * Return value: a new #PkClientHelper object.
 *
 * Since: 0.6.10
 **/
PkClientHelper *
pk_client_helper_new (void)
{
	PkClientHelper *client_helper;
	client_helper = g_object_new (PK_TYPE_CLIENT_HELPER, NULL);
	return PK_CLIENT_HELPER (client_helper);
}
