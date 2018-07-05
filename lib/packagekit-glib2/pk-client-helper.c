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
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-client-helper
 * @short_description: helper object to run a helper session process for the lifetime of a transaction
 *
 * This GObject can be used to run a session helper program out of band
 * with the normal PackageKit transaction. This allows an external program
 * such as debconf to be used that needs direct console access.
 *
 *
 *   client ----> packagekit-glib ---> dbus ---> packagekitd ---> apt
 *          .------------^                                         ^
 *   debconf ___________________               (SetHints)          |
 *    | \___|  PkClientHelper   \__.____.____.______/_.____.____.__/
 *    ^-----|___________________/         (socket in tmp)
 *   (stdin & stdout )
 *
 *  \------------.------------------/          \------------.---------/
 *               |                                          |
 *          user session                              system context
 */

#include "config.h"

#include <signal.h>
#include <errno.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include <packagekit-glib2/pk-client-helper.h>

static void     pk_client_helper_finalize	(GObject     *object);

#define PK_CLIENT_HELPER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT_HELPER, PkClientHelperPrivate))

/**
 * PkClientHelperPrivate:
 *
 * Private #PkClientHelper data
 **/
struct _PkClientHelperPrivate
{
	gchar				**argv;
	gchar				**envp;
	GFile				*socket_file;
	GSocket				*socket;
	GIOChannel			*socket_channel;
	GSource				*socket_channel_source;
	GPtrArray			*children;
	GPid				 kde_helper_pid;
};

typedef struct
{
	PkClientHelper			*helper;
	GSocket				*socket;
	GIOChannel			*socket_channel;
	GSource				*socket_channel_source;
	GPid				 pid;
	GIOChannel			*stdin_channel;
	GIOChannel			*stdout_channel;
	GIOChannel			*stderr_channel;
	GSource				*stdout_channel_source;
	GSource				*stderr_channel_source;
} PkClientHelperChild;

G_DEFINE_TYPE (PkClientHelper, pk_client_helper, G_TYPE_OBJECT)

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
		kill (child->pid, SIGQUIT);
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
 * Stops the helper process, by killing the helper process and deleting
 * the socket.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_helper_stop (PkClientHelper *client_helper, GError **error)
{
	PkClientHelperPrivate *priv = NULL;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	priv = client_helper->priv;

	/* make sure started */
	g_return_val_if_fail (priv->socket_file != NULL, FALSE);

	/* close the socket */
	if (priv->socket_channel_source != NULL)
		g_source_destroy (priv->socket_channel_source);
	if (priv->socket != NULL) {
		if (!g_socket_close (priv->socket, error))
			return FALSE;
	}

	/* kill any children */
	for (guint i = 0; i < priv->children->len; i++) {
		PkClientHelperChild *child = g_ptr_array_index (priv->children, i);
		int retval;

		g_debug ("sending SIGQUIT %ld", (long)child->pid);
		retval = kill (child->pid, SIGQUIT);
		if (retval == EINVAL) {
			g_set_error (error, 1, 0, "failed to kill, signum argument is invalid");
			return FALSE;
		}
		if (retval == EPERM) {
			g_set_error (error, 1, 0, "failed to kill, no permission");
			return FALSE;
		}
	}

	/* remove any socket file */
	if (g_file_query_exists (priv->socket_file, NULL)) {
		if (!g_file_delete (priv->socket_file, NULL, error))
			return FALSE;
	}

	return TRUE;
}

/*
 * pk_client_helper_copy_stdout_cb:
 **/
static gboolean
pk_client_helper_copy_stdout_cb (GIOChannel *source, GIOCondition condition, PkClientHelperChild *child)
{
	gchar data[1024];
	gsize len = 0;
	gsize written = 0;
	GIOStatus status;
	g_autoptr(GError) error = NULL;

	/* read data */
	status = g_io_channel_read_chars (source, data, sizeof (data) - 1, &len, &error);
	if (status == G_IO_STATUS_EOF) {
		g_debug ("helper process exited");
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
		g_warning ("failed to write to socket: %s", error->message);
		return G_SOURCE_REMOVE;
	}
	if (written != len) {
		g_warning ("failed to write %" G_GSIZE_FORMAT " bytes, "
			   "only wrote %" G_GSIZE_FORMAT " bytes", len, written);
		return G_SOURCE_REMOVE;
	}
	g_debug ("wrote %" G_GSIZE_FORMAT " bytes to socket", written);

	return G_SOURCE_CONTINUE;
}

/*
 * pk_client_helper_echo_stderr_cb:
 **/
static gboolean
pk_client_helper_echo_stderr_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	gchar data[1024];
	gsize len = 0;
	GIOStatus status;

	/* read data */
	status = g_io_channel_read_chars (source, data, sizeof (data) - 1, &len, NULL);
	if (status == G_IO_STATUS_EOF) {
		return G_SOURCE_REMOVE;
	}
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
pk_client_helper_copy_conn_cb (GIOChannel *source, GIOCondition condition, PkClientHelperChild *child)
{
	gchar data[1024];
	gsize len = 0;
	GIOStatus status;
	gsize written = 0;
	g_autoptr(GError) error = NULL;

	status = g_io_channel_read_chars (source, data, sizeof (data) - 1, &len, &error);
	if (status == G_IO_STATUS_EOF) {
		g_debug ("socket hung up");

		/* Shutdown helper */
		if (!g_io_channel_shutdown (child->stdin_channel, TRUE, &error))
			g_warning ("failed to close connection to child: %s", error->message);
		return G_SOURCE_REMOVE;
	}
	if (error != NULL) {
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
		g_warning ("failed to write to stdin: %s", error->message);
		return G_SOURCE_REMOVE;
	}
	if (written != len) {
		g_warning ("failed to write %" G_GSIZE_FORMAT " bytes, "
			   "only wrote %" G_GSIZE_FORMAT " bytes", len, written);
		return G_SOURCE_REMOVE;
	}
	g_debug ("wrote %" G_GSIZE_FORMAT " bytes to stdin of %s", written, child->helper->priv->argv[0]);

	return G_SOURCE_CONTINUE;
}

static GSource *
make_input_source (GIOChannel *channel, GSourceFunc func, gpointer user_data)
{
	GSource *source;
	GMainContext *context;

	source = g_io_create_watch (channel, G_IO_IN);
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
pk_client_helper_accept_connection_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = client_helper->priv;
	g_autoptr(GSocket) socket = NULL;
	GPid pid;
	gint standard_input = 0;
	gint standard_output = 0;
	gint standard_error = 0;
	gint fd;
	GIOStatus status;
	PkClientHelperChild *child;
	g_autoptr(GError) error = NULL;

	/* accept the connection request */
	socket = g_socket_accept (priv->socket, NULL, &error);
	if (socket == NULL) {
		g_warning ("failed to accept socket: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_debug ("accepting connection %i for socket %i",
		 g_socket_get_fd (socket),
		 g_socket_get_fd (priv->socket));

	/* spawn helper executable */
	if (!g_spawn_async_with_pipes (NULL, priv->argv, priv->envp, 0, NULL, NULL, &pid,
				       &standard_input, &standard_output, &standard_error, &error)) {
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
	child->socket_channel_source =
		make_input_source (child->socket_channel, (GSourceFunc) pk_client_helper_copy_conn_cb, child);
	/* binary data */
	status = g_io_channel_set_encoding (child->socket_channel, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return G_SOURCE_CONTINUE;
	}
	g_io_channel_set_buffered (child->socket_channel, FALSE);

	/* frontend has data */
	child->stdout_channel_source =
		make_input_source (child->stdout_channel, (GSourceFunc) pk_client_helper_copy_stdout_cb, child);
	child->stderr_channel_source =
		make_input_source (child->stderr_channel, (GSourceFunc) pk_client_helper_echo_stderr_cb, child);
	return G_SOURCE_CONTINUE;
}

/**
 * pk_client_helper_start:
 * @client_helper: a valid #PkClientHelper instance
 * @socket_filename: a socket filename that does not already exist
 * @argv: the executable, along with any arguments
 * @envp: the environment
 * @error: A #GError or %NULL
 *
 * Starts the helper process, by running the helper process and setting
 * up the socket for use.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_helper_start (PkClientHelper *client_helper,
			const gchar *socket_filename,
			gchar **argv, gchar **envp,
			GError **error)
{
	guint i;
	gboolean use_kde_helper = FALSE;
	PkClientHelperPrivate *priv = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GSocketAddress) address = NULL;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (socket_filename != NULL, FALSE);
	g_return_val_if_fail (argv != NULL, FALSE);
	g_return_val_if_fail (socket_filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	priv = client_helper->priv;

	/* make sure not been started before */
	g_return_val_if_fail (priv->argv == NULL, FALSE);
	g_return_val_if_fail (priv->socket_file == NULL, FALSE);

	/* already exists? */
	if (g_file_test (socket_filename, G_FILE_TEST_EXISTS)) {
		g_set_error (error, 1, 0, "socket %s already exists", socket_filename);
		return FALSE;
	}

	g_debug ("using socket in %s", socket_filename);
	priv->socket_file = g_file_new_for_path (socket_filename);

	/* preconfigure KDE frontend, if requested */
	if (envp != NULL) {
		for (i = 0; envp[i] != NULL; i++) {
			if (g_strcmp0 (envp[i], "DEBIAN_FRONTEND=kde") == 0) {
				if (g_file_test ("/usr/bin/debconf-kde-helper",
						 G_FILE_TEST_EXISTS)) {
					use_kde_helper = TRUE;
				}
			}
		}
	}

	/* create socket */
	priv->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, error);
	if (priv->socket == NULL)
		return FALSE;

	/* bind to the socket */
	address = g_unix_socket_address_new (socket_filename);
	if (!g_socket_bind (priv->socket, address, TRUE, error))
		return FALSE;

	/* spawn KDE debconf communicator */
	if (use_kde_helper) {
		priv->envp = g_strdupv (envp);
		priv->argv = g_new0 (gchar *, 2);
		priv->argv[0] = g_strdup ("/usr/bin/debconf-kde-helper");
		priv->argv[1] = g_strconcat ("--socket-path", "=", socket_filename, NULL);

		if (!g_spawn_async (NULL, priv->argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL,
			NULL, NULL, &priv->kde_helper_pid, &error_local)) {
			g_warning ("failed to spawn: %s", error_local->message);
			return FALSE;
		}
		g_debug ("started process %s with pid %i", priv->argv[0], priv->kde_helper_pid);
		return TRUE;
	}

	/* listen to the socket */
	if (!g_socket_listen (priv->socket, error))
		return FALSE;

	return pk_client_helper_start_with_socket (client_helper, priv->socket, argv, envp, error);
}

/**
 * pk_client_helper_start_with_socket:
 * @client_helper: a valid #PkClientHelper instance
 * @socket: the (bound and listening) #GSocket instance to use
 * @argv: the executable, along with any arguments
 * @envp: the environment
 * @error: A #GError or %NULL
 *
 * Starts the helper process, by running the helper process and setting
 * up the socket for use.
 *
 * Return value: %TRUE for success
 *
 * Since: 1.1.13
 **/
gboolean
pk_client_helper_start_with_socket (PkClientHelper *client_helper,
				    GSocket *socket,
				    gchar **argv, gchar **envp,
				    GError **error)
{
	gint fd;
	PkClientHelperPrivate *priv = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GSocketAddress) address = NULL;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (socket != NULL, FALSE);
	g_return_val_if_fail (argv != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	priv = client_helper->priv;

	/* make sure not been started before */
	g_return_val_if_fail (priv->argv == NULL, FALSE);

	/* cache for actual start */
	priv->argv = g_strdupv (argv);
	priv->envp = g_strdupv (envp);

	/* Set the socket */
	priv->socket = socket;

	/* socket has data */
	fd = g_socket_get_fd (priv->socket);
	priv->socket_channel = g_io_channel_unix_new (fd);
	priv->socket_channel_source =
		make_input_source (priv->socket_channel, (GSourceFunc) pk_client_helper_accept_connection_cb, client_helper);
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
	PkClientHelperPrivate *priv;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);

	priv = client_helper->priv;

	for (guint i = 0; i < priv->children->len; i++) {
		PkClientHelperChild *child = g_ptr_array_index (priv->children, i);
		if (!g_source_is_destroyed (child->socket_channel_source) &&
		    !g_source_is_destroyed (child->stdout_channel_source))
			return TRUE;
	}

	return FALSE;
}

/*
 * pk_client_helper_class_init:
 **/
static void
pk_client_helper_class_init (PkClientHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_client_helper_finalize;
	g_type_class_add_private (klass, sizeof (PkClientHelperPrivate));
}

/*
 * pk_client_helper_init:
 **/
static void
pk_client_helper_init (PkClientHelper *client_helper)
{
	client_helper->priv = PK_CLIENT_HELPER_GET_PRIVATE (client_helper);
	client_helper->priv->children = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_client_helper_child_free);
}

/*
 * pk_client_helper_finalize:
 **/
static void
pk_client_helper_finalize (GObject *object)
{
	PkClientHelper *client_helper = PK_CLIENT_HELPER (object);
	PkClientHelperPrivate *priv = client_helper->priv;

	if (priv->socket_channel_source != NULL) {
		g_source_destroy (priv->socket_channel_source);
		g_source_unref (priv->socket_channel_source);
	}

	g_strfreev (priv->argv);
	g_strfreev (priv->envp);
	if (priv->socket_file != NULL) {
		g_file_delete (priv->socket_file, NULL, NULL);
		g_object_unref (priv->socket_file);
	}
	if (priv->socket != NULL) {
		g_socket_close (priv->socket, NULL);
		g_object_unref (priv->socket);
	}
	if (priv->socket_channel != NULL)
		g_io_channel_unref (priv->socket_channel);
	g_ptr_array_unref (priv->children);
	if (priv->kde_helper_pid > 0)
		kill (priv->kde_helper_pid, SIGQUIT);

	G_OBJECT_CLASS (pk_client_helper_parent_class)->finalize (object);
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
