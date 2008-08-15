/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Shishir Goel <crazyontheedge@gmail.com>
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
#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-control.h>


#include "pk-tools-common.h"
#include "pk-generate-pack.h"

int
main (int argc, char *argv[])
{
	GError *error = NULL;
	gboolean verbose = FALSE;
	gchar *with_package_list = NULL;
	GOptionContext *context;
	gchar *options_help;
	gboolean ret;
	guint retval;
	const gchar *package = NULL;
	gchar *pack_filename = NULL;
	gchar *packname = NULL;
	PkControl *control = NULL;
	PkRoleEnum roles;
	const gchar *package_list = NULL;
	gchar *tempdir = NULL;
	gboolean exists;
	gboolean overwrite;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "with-package-list", '\0', 0, G_OPTION_ARG_STRING, &with_package_list,
			_("Set the path of the file with the list of packages/dependencies to be excluded"), NULL},
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new ("PackageKit Pack Generator");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);

	if (with_package_list != NULL) {
		package_list = with_package_list;
	} else {
		package_list = "/var/lib/PackageKit/package-list.txt";
	}

	if (argc < 2) {
		g_print ("%s", options_help);
		return 1;
	}

	/* are we dumb and can't check for depends? */
	control = pk_control_new ();
	roles = pk_control_get_actions (control);
	if (!pk_enums_contain (roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		g_print ("Please use a backend that supports GetDepends!\n");
		goto out;
	}

	/* get the arguments */
	pack_filename = argv[1];
	if (argc > 2) {
		package = argv[2];
	}

	/* have we specified the right things */
	if (pack_filename == NULL || package == NULL) {
		g_print (_("You need to specify the pack name and packages to be packed\n"));
		goto out;
	}

	/* check the suffix */
	if (!g_str_has_suffix (pack_filename,".pack")) {
		g_print(_("Invalid name for the service pack, Specify a name with .pack extension\n"));
		goto out;
	}

	/* download packages to a temporary directory */
	tempdir = g_build_filename (g_get_tmp_dir (), "pack", NULL);

	/* check if file exists before we overwrite it */
	exists = g_file_test (pack_filename, G_FILE_TEST_EXISTS);

	/*ask user input*/
	if (exists) {
		overwrite = pk_console_get_prompt (_("A pack with the same name already exists, do you want to overwrite it?"), FALSE);
		if (!overwrite) {
			g_print ("%s\n", _("Cancelled!"));
			goto out;
		}
	}

	/* get rid of temp directory if it already exists */
	g_rmdir (tempdir);

	/* make the temporary directory */
	retval = g_mkdir_with_parents (tempdir, 0777);
	if (retval != 0) {
		g_print ("%s: %s\n", _("Failed to create directory"), tempdir);
		goto out;
	}

	/* generate the pack */
	ret = pk_generate_pack_main (pack_filename, tempdir, package, package_list, &error);
	if (!ret) {
		g_print ("%s: %s\n", _("Failed to create pack"), error->message);
		g_error_free (error);
		goto out;
	}

out:
	/* get rid of temp directory */
	g_rmdir (tempdir);
	g_free (tempdir);
	g_free (packname);
	g_free (with_package_list);
	g_free (options_help);
	g_object_unref (control);
	return 0;
}
