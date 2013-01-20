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

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

static gboolean
pk_socket_example_accept_connection_cb (GSocket *socket, GIOCondition condition, gpointer user_data)
{
	GError *error = NULL;
	gsize len;
	gchar buffer[1024];
	gboolean ret = TRUE;
	GMainLoop *loop = (GMainLoop *) user_data;

	/* the helper process exited */
	if ((condition & G_IO_HUP) > 0) {
		g_warning ("socket was disconnected");
		g_main_loop_quit (loop);
		ret = FALSE;
		goto out;
	}

	/* there is data */
	if ((condition & G_IO_IN) > 0) {
		len = g_socket_receive (socket, buffer, 1024, NULL, &error);
		if (error != NULL) {
			g_warning ("failed to get data: %s", error->message);
			g_error_free (error);
			ret = FALSE;
			goto out;
		}
		if (len == 0)
			goto out;
		g_debug ("got data: %s : %" G_GSIZE_FORMAT, buffer, len);
	}
out:
	return ret;
}

gint
main (void)
{
	gboolean ret;
	GSocket *socket = NULL;
	GSocketAddress *address = NULL;
	GError *error = NULL;
	gsize wrote;
	const gchar *buffer = "ping\n";
	const gchar *socket_filename = "/tmp/pk-self-test.socket";
	GSource *source;
	GMainLoop *loop;

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init();
#endif

	loop = g_main_loop_new (NULL, FALSE);

	/* create socket */
	socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
	if (socket == NULL) {
		g_warning ("failed to create socket: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_socket_set_blocking (socket, FALSE);
	g_socket_set_keepalive (socket, TRUE);

	/* connect to it */
	address = g_unix_socket_address_new (socket_filename);
	ret = g_socket_connect (socket, address, NULL, &error);
	if (!ret) {
		g_warning ("failed to connect to socket: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* socket has data */
	source = g_socket_create_source (socket, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, NULL);
	g_source_set_callback (source, (GSourceFunc) pk_socket_example_accept_connection_cb, loop, NULL);
	g_source_attach (source, NULL);

	/* send some data */
	wrote = g_socket_send (socket, buffer, 5, NULL, &error);
	if (wrote != 5) {
		g_warning ("failed to write 5 bytes");
		goto out;
	}

	/* run main loop */
	g_debug ("running main loop");
	g_main_loop_run (loop);
out:
	if (loop != NULL)
		g_main_loop_unref (loop);
	if (socket != NULL)
		g_object_unref (socket);
	if (address != NULL)
		g_object_unref (address);
	return 0;
}
