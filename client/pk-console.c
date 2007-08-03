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

#include "pk-task-client.h"

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	DBusGConnection *system_connection;
	gboolean verbose = FALSE;
	gboolean async = FALSE;
	gchar *mode = NULL;
	gchar *value = NULL;
	GError *error = NULL;
	GOptionContext *context;
	PkTaskClient *tclient;

	const GOptionEntry options[] = {
		{ "mode", '\0', 0, G_OPTION_ARG_STRING, &mode,
		  "The mode of operation, [search|install|remove|update|checkupdate]", NULL },
		{ "value", '\0', 0, G_OPTION_ARG_STRING, &value,
		  "The package to use", NULL },
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ "async", '\0', 0, G_OPTION_ARG_NONE, &async,
		  "Do not wait for command to complete", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("PackageKit console client"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (!g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		g_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	g_debug ("mode = %s", mode);
	g_debug ("value = %s", value);
	g_debug ("async = %i", async);

	if (mode == NULL || value == NULL) {
		g_debug ("invalid command line parameters");
		return 1;
	}

	tclient = pk_task_client_new ();
	if (strcmp (mode, "search") == 0) {
		/* do search */
		pk_task_client_find_packages (tclient, value);
	} else {
		g_debug ("not yet supported");
	}
	g_object_unref (tclient);

	if (0 && async == FALSE) {
		loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (loop);
		g_main_loop_unref (loop);
	}

	return 0;
}
