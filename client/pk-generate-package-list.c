/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <stdio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <egg-debug.h>
#include <pk-client.h>
#include <pk-common.h>
#include <pk-package-id.h>

#define PK_PACKAGE_LIST_LOCATION	"/var/lib/PackageKit/package-list.txt"

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkClient *client;
	GOptionContext *context;
	gboolean verbose = FALSE;
	gboolean quiet = FALSE;
	gboolean ret;
	GError *error = NULL;
	PkPackageList *list = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
			"Do not show any output to the console", NULL },
		{ NULL}
	};

	g_type_init ();

	context = g_option_context_new ("pk-generate-package-list");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	/* get the package list with no filter */
	ret = pk_client_get_packages (client, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		if (!quiet)
			g_print ("Failed to get package lists: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* check that we only matched one package */
	list = pk_client_get_package_list (client);
	ret = pk_package_list_to_file (list, PK_PACKAGE_LIST_LOCATION);
	if (!ret) {
		if (!quiet)
			g_print ("Failed to write to disk\n");
		goto out;
	}

out:
	if (list != NULL) {
		g_object_unref (list);
	}
	g_object_unref (client);
	return 0;
}
