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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>

#include <pk-debug.h>
#include "pk-application.h"

/**
 * pk_application_help_cb
 * @application: This application class instance
 *
 * What to do when help is requested
 **/
static void
pk_application_help_cb (PkApplication *application)
{
	g_warning ("help application");
}

/**
 * pk_application_close_cb
 * @application: This application class instance
 *
 * What to do when we are asked to close for whatever reason
 **/
static void
pk_application_close_cb (PkApplication *application)
{
	g_object_unref (application);
	exit (0);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	PkApplication *application = NULL;
	GOptionContext *context;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("PackageKit Manager"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	/* create a new application object */
	application = pk_application_new ();
	g_signal_connect (application, "action-help",
			  G_CALLBACK (pk_application_help_cb), NULL);
	g_signal_connect (application, "action-close",
			  G_CALLBACK (pk_application_close_cb), NULL);

	loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);
	g_main_loop_unref (loop);
	g_object_unref (application);

	return 0;
}
