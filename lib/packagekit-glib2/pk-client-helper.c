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
	GFile				*socket_file;
	GIOChannel			*io_channel_temp;
	GIOChannel			*io_channel_socket;
	GIOChannel			*io_channel_child_stdin;
	GIOChannel			*io_channel_child_stdout;
	GIOChannel			*io_channel_child_stderr;
	guint				 io_channel_socket_listen_id;
	guint				 io_channel_child_stdin_listen_id;
	guint				 io_channel_child_stdout_listen_id;
	guint				 io_channel_child_stderr_listen_id;
	gchar				**argv;
	gchar				**envp;
	GSocket				*socket;
	GSocket				*active_conn;
	GPid				 child_pid;
};

G_DEFINE_TYPE (PkClientHelper, pk_client_helper, G_TYPE_OBJECT)

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
	PkClientHelperPrivate *priv = client_helper->priv;
	gboolean ret = FALSE;
	gint retval;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* make sure started */
	g_return_val_if_fail (priv->socket_file != NULL, FALSE);

	/* close the socket */
	if (priv->socket != NULL) {
		ret = g_socket_close (priv->socket, error);
		if (!ret)
			goto out;

		/* stop watching for events */
		if (priv->io_channel_socket_listen_id > 0)
			g_source_remove (priv->io_channel_socket_listen_id);
		if (priv->io_channel_child_stdout_listen_id > 0)
			g_source_remove (priv->io_channel_child_stdout_listen_id);
		if (priv->io_channel_child_stderr_listen_id > 0)
			g_source_remove (priv->io_channel_child_stderr_listen_id);
		if (priv->io_channel_child_stdin_listen_id > 0)
			g_source_remove (priv->io_channel_child_stdin_listen_id);
	}

	/* kill process */
	if (priv->child_pid > 0) {
		g_debug ("sending SIGQUIT %ld", (long)priv->child_pid);
		retval = kill (priv->child_pid, SIGQUIT);
		if (retval == EINVAL) {
			g_set_error (error, 1, 0, "failed to kill, signum argument is invalid");
			goto out;
		}
		if (retval == EPERM) {
			g_set_error (error, 1, 0, "failed to kill, no permission");
			goto out;
		}
	}

	/* when we're here, everything worked fine */
	ret = TRUE;

	/* remove any socket file */
	if (g_file_query_exists (priv->socket_file, NULL)) {
		ret = g_file_delete (priv->socket_file, NULL, error);
		if (!ret)
		    goto out;
	}
out:
	return ret;
}

/*
 * pk_client_helper_copy_stdout_cb:
 **/
static gboolean
pk_client_helper_copy_stdout_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	gchar data[1024];
	gsize len = 0;
	gsize written = 0;
	GIOStatus status;
	gboolean ret = TRUE;
	PkClientHelperPrivate *priv = client_helper->priv;
	g_autoptr(GError) error = NULL;

	/* the helper process exited */
	if ((condition & G_IO_HUP) > 0) {
		g_debug ("helper process exited");
		status = g_io_channel_shutdown (priv->io_channel_child_stdout, FALSE, &error);
		if (status != G_IO_STATUS_NORMAL) {
			g_warning ("failed to shutdown channel: %s", error->message);
			return FALSE;
		}
		status = g_io_channel_shutdown (priv->io_channel_child_stderr, FALSE, &error);
		if (status != G_IO_STATUS_NORMAL) {
			g_warning ("failed to shutdown channel: %s", error->message);
			return FALSE;
		}
		if (priv->active_conn != NULL) {
			ret = g_socket_close (priv->active_conn, &error);
			if (!ret) {
				g_warning ("failed to close socket");
			}
			g_object_unref (priv->active_conn);
			priv->active_conn = NULL;
		}
		return FALSE;
	}

	/* there is data to read */
	if ((condition & G_IO_IN) > 0) {

		/* read data */
		status = g_io_channel_read_chars (source, data, 1024, &len, &error);
		if (status == G_IO_STATUS_EOF) {
			g_warning ("child closed unexpectedly");
			return FALSE;
		}
		if (len == 0)
			return TRUE;

		/* write to socket */
		data[len] = '\0';
		g_debug ("child has input to push to the socket: %s", data);
		status = g_io_channel_write_chars (priv->io_channel_socket, data, len, &written, &error);
		if (status != G_IO_STATUS_NORMAL) {
			g_warning ("failed to write to socket: %s", error->message);
			return FALSE;
		}
		if (written != len) {
			g_warning ("failed to write %" G_GSIZE_FORMAT " bytes, "
				   "only wrote %" G_GSIZE_FORMAT " bytes", len, written);
			return FALSE;
		}
		g_debug ("wrote %" G_GSIZE_FORMAT " bytes to socket", written);
	}
	return TRUE;
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
	gboolean ret = TRUE;

	/* there is data to read */
	if ((condition & G_IO_IN) == 0)
		goto out;

	/* read data */
	status = g_io_channel_read_chars (source, data, 1024, &len, NULL);
	if (status == G_IO_STATUS_EOF) {
		ret = FALSE;
		goto out;
	}
	if (len == 0)
		goto out;

	/* write to socket */
	data[len] = '\0';
	g_debug ("child has error: %s", data);
out:
	return ret;
}

/*
 * pk_client_helper_copy_conn_cb:
 **/
static gboolean
pk_client_helper_copy_conn_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = client_helper->priv;
	gchar data[1024];
	gsize len = 0;
	GIOStatus status;
	gsize written = 0;
	gboolean ret = TRUE;
	g_autoptr(GError) error = NULL;

	/* package manager is done processing a package */
	if ((condition & G_IO_HUP) > 0) {
		g_debug ("socket hung up");
		ret = g_socket_close (priv->active_conn, &error);
		if (!ret)
			g_warning ("failed to close socket");
		g_object_unref (priv->active_conn);
		priv->active_conn = NULL;
		return FALSE;
	}

	/* there is data to read */
	if ((condition & G_IO_IN) > 0) {
		status = g_io_channel_read_chars (source, data, 1024, &len, &error);
		if (status == G_IO_STATUS_EOF)
			return FALSE;
		if (error != NULL) {
			g_warning ("failed to read: %s", error->message);
			return FALSE;
		}
		if (len == 0)
			return TRUE;

		/* write to child */
		data[len] = '\0';
		g_debug ("socket has data to push to child: '%s'", data);
		status = g_io_channel_write_chars (priv->io_channel_child_stdin, data, len, &written, &error);
		if (status != G_IO_STATUS_NORMAL) {
			g_warning ("failed to write to stdin: %s", error->message);
			return FALSE;
		}
		if (written != len) {
			g_warning ("failed to write %" G_GSIZE_FORMAT " bytes, "
				   "only wrote %" G_GSIZE_FORMAT " bytes", len, written);
			return FALSE;
		}
		g_debug ("wrote %" G_GSIZE_FORMAT " bytes to stdin of %s", written, priv->argv[0]);
	}
	return TRUE;
}

/*
 * pk_client_helper_accept_connection_cb:
 **/
static gboolean
pk_client_helper_accept_connection_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = client_helper->priv;
	gboolean ret;
	gint standard_input = 0;
	gint standard_output = 0;
	gint standard_error = 0;
	gint fd;
	GIOStatus status;
	g_autoptr(GError) error = NULL;

	/* delaying connection */
	if (priv->active_conn != NULL)
		return TRUE;

	/* accept the connection request */
	priv->active_conn = g_socket_accept (priv->socket, NULL, &error);
	if (priv->active_conn == NULL) {
		g_warning ("failed to accept socket: %s", error->message);
		return TRUE;
	}
	g_debug ("accepting connection %i for socket %i",
		 g_socket_get_fd (priv->active_conn),
		 g_socket_get_fd (priv->socket));

	/* spawn helper executable */
	ret = g_spawn_async_with_pipes (NULL, priv->argv, priv->envp, 0, NULL, NULL, &priv->child_pid,
					&standard_input, &standard_output, &standard_error, &error);
	if (!ret) {
		g_warning ("failed to spawn: %s", error->message);
		return TRUE;
	}
	g_debug ("started process %s with pid %i", priv->argv[0], priv->child_pid);

	/* connect up */
	priv->io_channel_child_stdin = g_io_channel_unix_new (standard_input);
	priv->io_channel_child_stdout = g_io_channel_unix_new (standard_output);
	priv->io_channel_child_stderr = g_io_channel_unix_new (standard_error);

	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_child_stdin, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return TRUE;
	}
	g_io_channel_set_buffered (priv->io_channel_child_stdin, FALSE);

	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_child_stdout, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return TRUE;
	}
	g_io_channel_set_buffered (priv->io_channel_child_stdout, FALSE);

	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_child_stderr, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return TRUE;
	}
	g_io_channel_set_buffered (priv->io_channel_child_stderr, FALSE);

	/* socket has data */
	fd = g_socket_get_fd (priv->active_conn);
	priv->io_channel_socket = g_io_channel_unix_new (fd);
	priv->io_channel_child_stdin_listen_id =
		g_io_add_watch_full (priv->io_channel_socket, G_PRIORITY_HIGH_IDLE,
				     G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
				     (GIOFunc) pk_client_helper_copy_conn_cb, client_helper, NULL);
	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_socket, NULL, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to set encoding: %s", error->message);
		return TRUE;
	}
	g_io_channel_set_buffered (priv->io_channel_socket, FALSE);

	/* frontend has data */
	priv->io_channel_child_stdout_listen_id =
		g_io_add_watch_full (priv->io_channel_child_stdout, G_PRIORITY_HIGH_IDLE,
				     G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
				     (GIOFunc) pk_client_helper_copy_stdout_cb, client_helper, NULL);
	priv->io_channel_child_stderr_listen_id =
		g_io_add_watch_full (priv->io_channel_child_stderr, G_PRIORITY_HIGH_IDLE, G_IO_IN,
				     (GIOFunc) pk_client_helper_echo_stderr_cb, client_helper, NULL);
	return TRUE;
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
	gboolean ret = FALSE;
	gboolean use_kde_helper = FALSE;
	gint fd;
	PkClientHelperPrivate *priv = client_helper->priv;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GSocketAddress) address = NULL;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (socket_filename != NULL, FALSE);
	g_return_val_if_fail (argv != NULL, FALSE);
	g_return_val_if_fail (socket_filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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

	/* cache for actual start */
	priv->argv = g_strdupv (argv);
	priv->envp = g_strdupv (envp);

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
		priv->argv = g_new0 (gchar *, 2);
		priv->argv[0] = g_strdup ("/usr/bin/debconf-kde-helper");
		priv->argv[1] = g_strconcat ("--socket-path", "=", socket_filename, NULL);

		ret = g_spawn_async (NULL, priv->argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL,
			NULL, NULL, &priv->child_pid, &error_local);
		if (!ret) {
			g_warning ("failed to spawn: %s", error_local->message);
			return FALSE;
		}
		g_debug ("started process %s with pid %i", priv->argv[0], priv->child_pid);
		return TRUE;
	}

	/* listen to the socket */
	if (!g_socket_listen (priv->socket, error))
		return FALSE;

	/* socket has data */
	fd = g_socket_get_fd (priv->socket);
	priv->io_channel_temp = g_io_channel_unix_new (fd);
	priv->io_channel_socket_listen_id =
		g_io_add_watch_full (priv->io_channel_temp,
				     G_PRIORITY_DEFAULT_IDLE, G_IO_IN,
				     (GIOFunc) pk_client_helper_accept_connection_cb, client_helper, NULL);
	return TRUE;
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
}

/*
 * pk_client_helper_finalize:
 **/
static void
pk_client_helper_finalize (GObject *object)
{
	PkClientHelper *client_helper = PK_CLIENT_HELPER (object);
	PkClientHelperPrivate *priv = client_helper->priv;

	if (priv->socket_file != NULL)
		g_object_unref (priv->socket_file);
	if (priv->io_channel_socket != NULL)
		g_io_channel_unref (priv->io_channel_socket);
	if (priv->io_channel_temp != NULL)
		g_io_channel_unref (priv->io_channel_temp);
	if (priv->io_channel_child_stdin != NULL)
		g_io_channel_unref (priv->io_channel_child_stdin);
	if (priv->io_channel_child_stdout != NULL)
		g_io_channel_unref (priv->io_channel_child_stdout);
	if (priv->io_channel_child_stderr != NULL)
		g_io_channel_unref (priv->io_channel_child_stderr);
	g_strfreev (priv->argv);
	g_strfreev (priv->envp);

	G_OBJECT_CLASS (pk_client_helper_parent_class)->finalize (object);
}

/**
 * pk_client_helper_new:
 *
 * Return value: a new PkClientHelper object.
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
