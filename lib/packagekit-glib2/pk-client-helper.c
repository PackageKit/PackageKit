/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 */

#include "config.h"

#include <signal.h>
#include <errno.h>
#include <glib-object.h>
#include <gio/gio.h>

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
	GIOChannel			*io_channel_socket;
	GIOChannel			*io_channel_child_stdin;
	GIOChannel			*io_channel_child_stdout;
	guint				 io_channel_socket_listen_id;
	guint				 io_channel_child_stdout_listen_id;
	gchar				**argv;
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

	/* stop watching for events */
	if (priv->io_channel_socket_listen_id > 0)
		g_source_remove (priv->io_channel_socket_listen_id);
	if (priv->io_channel_child_stdout_listen_id > 0)
		g_source_remove (priv->io_channel_child_stdout_listen_id);

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

	/* remove any socket file */
	ret = g_file_delete (priv->socket_file, NULL, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_client_helper_child_has_output_cb:
 **/
static gboolean
pk_client_helper_child_has_output_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	gchar data[1024];
	GError *error = NULL;
	gsize len = 0;
	gsize written = 0;
	GIOStatus status;
	gboolean ret = TRUE;
	PkClientHelperPrivate *priv = client_helper->priv;

	/* read data */
	status = g_io_channel_read_chars (source, data, 1024, &len, &error);
	if (status == G_IO_STATUS_EOF) {
		g_warning ("child closed unexpectedly");
		ret = FALSE;
		goto out;
	}
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to read: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (len == 0)
		goto out;
	data[len] = '\0';
	g_debug ("child has input to push to the socket: %s", data);

	/* write to socket */
	data[len] = '\0';
	g_debug ("socket has data to push to child: '%s'", data);
	status = g_io_channel_write_chars (priv->io_channel_socket, data, len, &written, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to write to socket: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (written != len) {
		g_warning ("failed to write %i bytes, only wrote %i bytes", len, written);
		goto out;
	}
	g_debug ("wrote %i bytes to socket", written);
out:
	return ret;
}

/**
 * pk_client_helper_socket_has_input_cb:
 **/
static gboolean
pk_client_helper_socket_has_input_cb (GIOChannel *source, GIOCondition condition, PkClientHelper *client_helper)
{
	PkClientHelperPrivate *priv = client_helper->priv;
	gchar data[1024];
	GError *error = NULL;
	gsize len = 0;
	GIOStatus status;
	gsize written = 0;
	gboolean ret = TRUE;

	/* read data */
	status = g_io_channel_read_chars (source, data, 1024, &len, &error);
	if (status == G_IO_STATUS_EOF)
		goto out;
	if (status != G_IO_STATUS_NORMAL)
		g_error ("status = %i (%i,%i,%i,%i)", status, G_IO_STATUS_NORMAL, G_IO_STATUS_AGAIN, G_IO_STATUS_EOF, G_IO_STATUS_ERROR);
	if (error != NULL) {
		g_warning ("failed to read: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (len == 0)
		goto out;

	/* write to child */
	data[len] = '\0';
	g_debug ("socket has data to push to child: '%s'", data);
	status = g_io_channel_write_chars (priv->io_channel_child_stdin, data, len, &written, &error);
	if (status != G_IO_STATUS_NORMAL) {
		g_warning ("failed to write to stdin: %s", error->message);
		g_error_free (error);
		goto out;
	}
	if (written != len) {
		g_warning ("failed to write %i bytes, only wrote %i bytes", len, written);
		goto out;
	}
	g_debug ("wrote %i bytes to stdin of %s", written, priv->argv[0]);
out:
	return ret;
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
	PkClientHelperPrivate *priv = client_helper->priv;
	GIOStatus status;
	gboolean ret = FALSE;
	gint standard_input = 0;
	gint standard_output = 0;

	g_return_val_if_fail (PK_IS_CLIENT_HELPER (client_helper), FALSE);
	g_return_val_if_fail (socket_filename != NULL, FALSE);
	g_return_val_if_fail (argv != NULL, FALSE);
	g_return_val_if_fail (envp != NULL, FALSE);
	g_return_val_if_fail (socket_filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* make sure not been started before */
	g_return_val_if_fail (priv->argv == NULL, FALSE);
	g_return_val_if_fail (priv->socket_file == NULL, FALSE);

	/* already exists? */
	if (g_file_test (socket_filename, G_FILE_TEST_EXISTS)) {
		g_set_error (error, 1, 0, "socket %s already exists", socket_filename);
		goto out;
	}

	g_debug ("using socket in %s", socket_filename);

	/* cache for actual start */
	priv->argv = g_strdupv (argv);

	/* create file */
	priv->socket_file = g_file_new_for_path (socket_filename);
	priv->io_channel_socket = g_io_channel_new_file (socket_filename, "w+", error);
	if (priv->io_channel_socket == NULL)
		goto out;

	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_socket, NULL, error);
	if (status != G_IO_STATUS_NORMAL)
		goto out;
	g_io_channel_set_buffered (priv->io_channel_socket, FALSE);

	/* spawn helper executable */
	ret = g_spawn_async_with_pipes (NULL, argv, envp, 0, NULL, NULL, &priv->child_pid,
					&standard_input, &standard_output, NULL, error);
	if (!ret)
		goto out;
	g_debug ("started process %s with pid %i", priv->argv[0], priv->child_pid);

	/* connect up */
	priv->io_channel_child_stdin = g_io_channel_unix_new (standard_input);
	priv->io_channel_child_stdout = g_io_channel_unix_new (standard_output);

	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_child_stdin, NULL, error);
	if (status != G_IO_STATUS_NORMAL)
		goto out;
	g_io_channel_set_buffered (priv->io_channel_child_stdin, FALSE);

	/* binary data */
	status = g_io_channel_set_encoding (priv->io_channel_child_stdout, NULL, error);
	if (status != G_IO_STATUS_NORMAL)
		goto out;
	g_io_channel_set_buffered (priv->io_channel_child_stdout, FALSE);

	/* socket has data */
	priv->io_channel_socket_listen_id =
		g_io_add_watch_full (priv->io_channel_socket, G_PRIORITY_HIGH_IDLE,
				     G_IO_IN,
				     (GIOFunc) pk_client_helper_socket_has_input_cb, client_helper, NULL);

	/* frontend has data */
	priv->io_channel_child_stdout_listen_id =
		g_io_add_watch_full (priv->io_channel_child_stdout, G_PRIORITY_HIGH_IDLE,
				     G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
				     (GIOFunc) pk_client_helper_child_has_output_cb, client_helper, NULL);

	/* success */
	ret = TRUE;
out:
	return ret;
}

/**
 * pk_client_helper_class_init:
 **/
static void
pk_client_helper_class_init (PkClientHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_client_helper_finalize;
	g_type_class_add_private (klass, sizeof (PkClientHelperPrivate));
}

/**
 * pk_client_helper_init:
 **/
static void
pk_client_helper_init (PkClientHelper *client_helper)
{
	client_helper->priv = PK_CLIENT_HELPER_GET_PRIVATE (client_helper);
}

/**
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
	if (priv->io_channel_child_stdin != NULL)
		g_io_channel_unref (priv->io_channel_child_stdin);
	if (priv->io_channel_child_stdout != NULL)
		g_io_channel_unref (priv->io_channel_child_stdout);
	g_strfreev (priv->argv);

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
