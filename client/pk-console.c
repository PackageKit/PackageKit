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

#include "pk-debug.h"
#include "pk-task-client.h"

/**
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (PkTaskClient *tclient, guint value, const gchar *package, const gchar *summary, gpointer data)
{
	gchar *padding;
	const gchar *installed;
	gint size;
	size = (25 - strlen(package));
	if (size < 0) {
		size = 0;
	}
	if (value == 0) {
		installed = "no ";
	} else {
		installed = "yes";
	}
	padding = g_strnfill (size, ' ');
	g_print ("%s %s%s %s\n", installed, package, padding, summary);
	g_free (padding);
}

/**
 * pk_console_percentage_changed_cb:
 **/
static void
pk_console_percentage_changed_cb (PkTaskClient *tclient, guint percentage, gpointer data)
{
	g_print ("%i%%\n", percentage);
}

/**
 * pk_console_usage:
 **/
static void
pk_console_usage (const gchar *error)
{
	if (error != NULL) {
		g_print ("Error: %s\n", error);
	}
	g_print ("usage:\n");
	g_print ("  pkcon search power\n");
	g_print ("  pkcon async install gtk2-devel\n");
	g_print ("  pkcon install gimp update totem\n");
	g_print ("  pkcon sync update\n");
	g_print ("  pkcon refresh\n");
	g_print ("  pkcon force-refresh\n");
	g_print ("  pkcon debug checkupdate\n");
}

/**
 * pk_console_parse_multiple_commands:
 **/
static void
pk_console_parse_multiple_commands (PkTaskClient *tclient, GPtrArray *array)
{
	const gchar *mode;
	const gchar *value = NULL;
	gboolean remove_two;
	mode = g_ptr_array_index (array, 0);
	if (array->len > 1) {
		value = g_ptr_array_index (array, 1);
	}
	remove_two = FALSE;

	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a search term");
		} else {
			pk_task_client_set_sync (tclient, TRUE);
			pk_task_client_find_packages (tclient, value, 0, TRUE, TRUE);
			remove_two = TRUE;
		}
	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a package to install");
		} else {
			pk_task_client_install_package (tclient, value);
			remove_two = TRUE;
		}
	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			pk_console_usage ("you need to specify a package to remove");
		} else {
			pk_task_client_remove_package (tclient, value, FALSE);
			remove_two = TRUE;
		}
	} else if (strcmp (mode, "debug") == 0) {
		pk_debug_init (TRUE);
	} else if (strcmp (mode, "verbose") == 0) {
		pk_debug_init (TRUE);
	} else if (strcmp (mode, "update") == 0) {
		pk_task_client_update_system (tclient);
	} else if (strcmp (mode, "refresh") == 0) {
		pk_task_client_refresh_cache (tclient, FALSE);
	} else if (strcmp (mode, "force-refresh") == 0) {
		pk_task_client_refresh_cache (tclient, TRUE);
	} else if (strcmp (mode, "sync") == 0) {
		pk_task_client_set_sync (tclient, TRUE);
	} else if (strcmp (mode, "async") == 0) {
		pk_task_client_set_sync (tclient, FALSE);
	} else if (strcmp (mode, "checkupdate") == 0) {
		pk_task_client_set_sync (tclient, TRUE);
		pk_task_client_get_updates (tclient);
	} else {
		pk_console_usage ("option not yet supported");
	}

	/* remove the right number of items from the pointer index */
	g_ptr_array_remove_index (array, 0);
	if (remove_two == TRUE) {
		g_ptr_array_remove_index (array, 0);
	}
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	DBusGConnection *system_connection;
	GError *error = NULL;
	PkTaskClient *tclient;
	GPtrArray *array;
	guint i;

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	if (!g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	if (argc < 2) {
		pk_console_usage (NULL);
		return 1;
	}

	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "package",
			  G_CALLBACK (pk_console_package_cb), NULL);
	g_signal_connect (tclient, "percentage-changed",
			  G_CALLBACK (pk_console_percentage_changed_cb), NULL);

	/* add argv to a pointer array */
	array = g_ptr_array_new ();
	for (i=1; i<argc; i++) {
		g_ptr_array_add (array, (gpointer) argv[i]);
	}
	/* process all the commands */
	while (array->len > 0) {
		pk_console_parse_multiple_commands (tclient, array);
	}
	g_ptr_array_free (array, TRUE);
	g_object_unref (tclient);

	return 0;
}
