/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#include <pk-common.h>
#include <pk-control.h>
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
 * pk_monitor_repo_list_changed_cb:
 **/
static void
pk_monitor_repo_list_changed_cb (PkControl *control, gpointer data)
{
	g_print ("repo-list-changed\n");
}

/**
 * pk_monitor_updates_changed_cb:
 **/
static void
pk_monitor_updates_changed_cb (PkControl *control, gpointer data)
{
	g_print ("updates-changed\n");
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
 * pk_monitor_locked_cb:
 **/
static void
pk_monitor_locked_cb (PkControl *control, gboolean is_locked, gpointer data)
{
	if (is_locked) {
		g_print ("locked\n");
	} else {
		g_print ("unlocked\n");
	}
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkTaskList *tlist;
	PkControl *control;
	gboolean ret;
	GMainLoop *loop;
	PkConnection *pconnection;
	gboolean connected;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			_("Show the program version and exit"), NULL},
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("PackageKit Monitor"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);

	loop = g_main_loop_new (NULL, FALSE);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), loop);
	connected = pk_connection_valid (pconnection);
	pk_debug ("connected=%i", connected);

	control = pk_control_new ();
	g_signal_connect (control, "locked",
			  G_CALLBACK (pk_monitor_locked_cb), NULL);
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (pk_monitor_repo_list_changed_cb), NULL);
	g_signal_connect (control, "updates-changed",
			  G_CALLBACK (pk_monitor_updates_changed_cb), NULL);

	tlist = pk_task_list_new ();
	g_signal_connect (tlist, "changed",
			  G_CALLBACK (pk_monitor_task_list_changed_cb), NULL);
	g_signal_connect (tlist, "status-changed",
			  G_CALLBACK (pk_monitor_task_list_changed_cb), NULL);

	pk_debug ("refreshing task list");
	ret = pk_task_list_refresh (tlist);
	if (ret == FALSE) {
		g_error ("cannot refresh transaction list");
	}
	pk_task_list_print (tlist);

	g_main_loop_run (loop);

	g_object_unref (control);
	g_object_unref (tlist);
	g_object_unref (pconnection);

	return 0;
}
