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
pk_console_package_cb (PkTaskClient *tclient, const gchar *package, const gchar *summary, gpointer data)
{
	gchar *padding;
	gint size;
	size = (25 - strlen(package));
	if (size < 0) {
		size = 0;
	}
	padding = g_strnfill (size, ' ');
	g_print ("%s%s %s\n", package, padding, summary);
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
 * main:
 **/
int
main (int argc, char *argv[])
{
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
	pk_debug_init (verbose);

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	pk_debug ("mode = %s", mode);
	pk_debug ("value = %s", value);
	pk_debug ("async = %i", async);

	if (mode == NULL) {
		pk_debug ("invalid mode");
		return 1;
	}

	tclient = pk_task_client_new ();
	g_signal_connect (tclient, "package",
			  G_CALLBACK (pk_console_package_cb), NULL);
	g_signal_connect (tclient, "percentage-changed",
			  G_CALLBACK (pk_console_percentage_changed_cb), NULL);

	pk_task_client_set_sync (tclient, !async);
	if (strcmp (mode, "search") == 0) {
		pk_task_client_find_packages (tclient, value);
	} else if (strcmp (mode, "install") == 0) {
		pk_task_client_install_package (tclient, value);
	} else if (strcmp (mode, "remove") == 0) {
		pk_task_client_remove_package_with_deps (tclient, value);
	} else if (strcmp (mode, "update") == 0) {
		pk_task_client_update_system (tclient);
	} else if (strcmp (mode, "checkupdate") == 0) {
		pk_task_client_get_updates (tclient);
	} else {
		pk_debug ("not yet supported");
	}
	g_object_unref (tclient);

	return 0;
}
