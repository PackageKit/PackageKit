/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-task-common.h>
#include <pk-task-list.h>
#include <pk-connection.h>

/**
 * pk_monitor_task_list_changed_cb:
 **/
static void
pk_monitor_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	pk_task_list_print (tlist);
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, gpointer data)
{
	pk_debug ("connected=%i", connected);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkTaskList *tlist;
	gboolean ret;
	GPtrArray *task_list;
	GMainLoop *loop;
	PkConnection *pconnection;
	gboolean connected;

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();
	pk_debug_init (TRUE);

	if (!g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();

	loop = g_main_loop_new (NULL, FALSE);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), loop);
	connected = pk_connection_valid (pconnection);
	pk_debug ("connected=%i", connected);

	tlist = pk_task_list_new ();
	g_signal_connect (tlist, "task-list-changed",
			  G_CALLBACK (pk_monitor_task_list_changed_cb), NULL);

	ret = pk_task_list_refresh (tlist);
	if (ret == FALSE) {
		g_error ("cannot refresh job list");
	}
	task_list = pk_task_list_get_latest (tlist);
	pk_task_list_print (tlist);

	g_main_loop_run (loop);

	g_object_unref (tlist);
	g_object_unref (pconnection);

	return 0;
}
