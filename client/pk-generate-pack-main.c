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

#include "egg-debug.h"

#include <pk-client.h>
#include <pk-control.h>
#include <pk-common.h>
#include <pk-package-list.h>

#include "pk-tools-common.h"
#include "pk-generate-pack.h"
#include "pk-service-pack.h"

/**
 * pk_generate_pack_get_filename:
 **/
static gchar *
pk_generate_pack_get_filename (const gchar *name, const gchar *directory)
{
	gchar *filename = NULL;
	gchar *distro_id;
	gchar *iso_time = NULL;

	distro_id = pk_get_distro_id ();
	if (name != NULL) {
		filename = g_strdup_printf ("%s/%s-%s.servicepack", directory, name, distro_id);
	} else {
		iso_time = pk_iso8601_present ();
		filename = g_strdup_printf ("%s/updates-%s-%s.servicepack", directory, iso_time, distro_id);
	}
	g_free (distro_id);
	g_free (iso_time);
	return filename;
}

int
main (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;
	gchar *options_help;
	gboolean ret;
	guint retval;
	gchar *filename = NULL;
	PkControl *control = NULL;
	PkBitfield roles;
	gchar *tempdir = NULL;
	gboolean exists;
	gboolean overwrite;
	PkServicePack *pack = NULL;
	PkPackageList *list = NULL;

	gboolean verbose = FALSE;
	gchar *directory = NULL;
	gchar *package_list = NULL;
	gchar *package = NULL;
	gboolean updates = FALSE;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "with-package-list", 'l', 0, G_OPTION_ARG_STRING, &package_list,
			_("Set the path of the file with the list of packages/dependencies to be excluded"), NULL},
		{ "output", 'o', 0, G_OPTION_ARG_STRING, &directory,
			_("The directory to put the pack file, or the current directory if ommitted"), NULL},
		{ "package", 'i', 0, G_OPTION_ARG_STRING, &package,
			_("The package to be put into the ServicePack"), NULL},
		{ "updates", 'u', 0, G_OPTION_ARG_NONE, &updates,
			_("Put all updates available in the ServicePack"), NULL},
		{ NULL}
	};

	if (! g_thread_supported ())
		g_thread_init (NULL);

	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new ("PackageKit Pack Generator");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);
	egg_debug_init (verbose);

	/* neither options selected */
	if (package == NULL && !updates) {
		g_print ("%s\n", _("Neither option selected"));
		g_print ("%s", options_help);
		return 1;
	}

	/* both options selected */
	if (package != NULL && updates) {
		g_print ("%s\n", _("Both optiosn selected"));
		g_print ("%s", options_help);
		return 1;
	}

	/* fall back to the system copy */
	if (package_list == NULL)
		package_list = g_strdup ("/var/lib/PackageKit/package-list.txt");

	/* fall back to CWD */
	if (directory == NULL)
		directory = g_get_current_dir ();

	/* are we dumb and can't check for depends? */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		g_print ("Please use a backend that supports GetDepends!\n");
		goto out;
	}

	/* get fn */
	filename = pk_generate_pack_get_filename (package, directory);

	/* download packages to a temporary directory */
	tempdir = g_build_filename (g_get_tmp_dir (), "pack", NULL);

	/* check if file exists before we overwrite it */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);

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

	/* not yet */
	if (updates) {
		g_print ("Not working yet...\n");
		return 1;
	}

	/* get the exclude list */
	list = pk_package_list_new ();
	ret = pk_package_list_add_file (list, package_list);
	if (!ret) {
		g_print ("%s: %s\n", _("Failed to open package list"), package_list);
		goto out;
	}

	/* create pack and set initial values */
	pack = pk_service_pack_new ();
	pk_service_pack_set_filename (pack, filename);
	pk_service_pack_set_temp_directory (pack, tempdir);
	pk_service_pack_set_exclude_list (pack, list);

	/* generate the pack */
	g_print (_("Creating service pack: %s\n"), filename);
	ret = pk_generate_pack_main (filename, tempdir, package, package_list, &error);
	if (!ret) {
		g_print ("%s: %s\n", _("Failed to create pack"), error->message);
		g_error_free (error);
		goto out;
	}
	g_print ("%s\n", _("Done!"));

out:
	/* get rid of temp directory */
	g_rmdir (tempdir);

	if (pack != NULL)
		g_object_unref (pack);
	if (list != NULL)
		g_object_unref (list);
	g_free (tempdir);
	g_free (filename);
	g_free (directory);
	g_free (package_list);
	g_free (options_help);
	g_object_unref (control);
	return 0;
}
