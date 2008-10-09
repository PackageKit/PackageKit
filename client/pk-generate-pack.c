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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "egg-debug.h"

#include <pk-client.h>
#include <pk-control.h>
#include <pk-common.h>
#include <pk-package-list.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-client.h>
#include <pk-service-pack.h>

#include "pk-tools-common.h"

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
		/* don't include the time, just use the date prefix */
		iso_time[10] = '\0';
		filename = g_strdup_printf ("%s/updates-%s-%s.servicepack", directory, iso_time, distro_id);
	}
	g_free (distro_id);
	g_free (iso_time);
	return filename;
}

/**
 * pk_generate_pack_package_resolve:
 **/
static gchar *
pk_generate_pack_package_resolve (PkClient *client, PkBitfield filter, const gchar *package, GError **error)
{
	gboolean ret;
	gboolean valid;
	guint i;
	guint length;
	const PkPackageObj *obj;
	PkPackageList *list;
	gchar **packages;

	/* check for NULL values */
	if (package == NULL) {
		egg_warning ("Cannot resolve the package: invalid package");
		return NULL;
	}

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid)
		return g_strdup (package);

	ret = pk_client_reset (client, error);
	if (ret == FALSE) {
		egg_warning ("failed to reset client task");
		return NULL;
	}

	/* we need to resolve it */
	packages = pk_package_ids_from_id (package);
	ret = pk_client_resolve (client, filter, packages, error);
	g_strfreev (packages);
	if (ret == FALSE) {
		egg_warning ("Resolve failed");
		return NULL;
	}

	/* get length of items found */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	g_object_unref (list);

	/* didn't resolve to anything, try to get a provide */
	if (length == 0) {
		ret = pk_client_reset (client, error);
		if (ret == FALSE) {
			egg_warning ("failed to reset client task");
			return NULL;
		}
		ret = pk_client_what_provides (client, filter, PK_PROVIDES_ENUM_ANY, package, error);
		if (ret == FALSE) {
			egg_warning ("WhatProvides is not supported in this backend");
			return NULL;
		}
	}

	/* get length of items found again (we might have had success) */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	if (length == 0) {
		egg_warning (_("Could not find a package match"));
		return NULL;
	}

	/* only found one, great! */
	if (length == 1) {
		obj = pk_package_list_get_obj (list, 0);
		return pk_package_id_to_string (obj->id);
	}
	g_print ("%s\n", _("There are multiple package matches"));
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		g_print ("%i. %s-%s.%s\n", i+1, obj->id->name, obj->id->version, obj->id->arch);
	}

	/* find out what package the user wants to use */
	i = pk_console_get_number (_("Please enter the package number: "), length);
	obj = pk_package_list_get_obj (list, i-1);
	g_object_unref (list);

	return pk_package_id_to_string (obj->id);
}

/**
 * pk_generate_pack_package_cb:
 **/
static void
pk_generate_pack_package_cb (PkServicePack *pack, const PkPackageObj *obj, gpointer data)
{
	g_return_if_fail (obj != NULL);
	g_print ("%s %s-%s.%s\n", _("Downloading"), obj->id->name, obj->id->version, obj->id->arch);
}

/**
 * main:
 **/
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
	PkClient *client = NULL;
	gchar *package_id = NULL;

	gboolean verbose = FALSE;
	gchar *directory = NULL;
	gchar *package_list = NULL;
	gchar *package = NULL;
	gboolean updates = FALSE;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "with-package-list", 'l', 0, G_OPTION_ARG_STRING, &package_list,
			_("Set the filename of dependencies to be excluded"), NULL},
		{ "output", 'o', 0, G_OPTION_ARG_STRING, &directory,
			_("The directory to put the pack file, or the current directory if ommitted"), NULL},
		{ "package", 'p', 0, G_OPTION_ARG_STRING, &package,
			_("The package to be put into the ServicePack"), NULL},
		{ "updates", 'u', 0, G_OPTION_ARG_NONE, &updates,
			_("Put all updates available in the ServicePack"), NULL},
		{ NULL}
	};

	if (! g_thread_supported ())
		g_thread_init (NULL);

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
		package_list = g_strdup (PK_SYSTEM_PACKAGE_LIST_FILENAME);

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

	/* get the exclude list */
	list = pk_package_list_new ();
	ret = pk_package_list_add_file (list, package_list);
	if (!ret) {
		g_print ("%s: %s\n", _("Failed to open package list"), package_list);
		goto out;
	}

	/* resolve package name to package_id */
	if (!updates) {
		client = pk_client_new ();
		pk_client_set_use_buffer (client, TRUE, NULL);
		pk_client_set_synchronous (client, TRUE, NULL);
		g_print ("%s\n", _("Resolving package name to remote object"));
		package_id = pk_generate_pack_package_resolve (client, PK_FILTER_ENUM_NONE, package, &error);
		if (package_id == NULL) {
			g_print (_("Failed to find package '%s': %s"), package, error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* create pack and set initial values */
	pack = pk_service_pack_new ();
	g_signal_connect (pack, "package",
			  G_CALLBACK (pk_generate_pack_package_cb), pack);
	pk_service_pack_set_filename (pack, filename);
	pk_service_pack_set_temp_directory (pack, tempdir);
	pk_service_pack_set_exclude_list (pack, list);

	/* generate the pack */
	g_print (_("Service pack to create: %s\n"), filename);
	if (updates)
		ret = pk_service_pack_create_for_updates (pack, &error);
	else
		ret = pk_service_pack_create_for_package_id (pack, package_id, &error);
	if (ret)
		g_print ("%s\n", _("Done!"));
	else {
		g_print ("%s: %s\n", _("Failed"), error->message);
		g_error_free (error);
	}

out:
	/* get rid of temp directory */
	g_rmdir (tempdir);

	if (pack != NULL)
		g_object_unref (pack);
	if (client != NULL)
		g_object_unref (client);
	if (list != NULL)
		g_object_unref (list);
	g_free (tempdir);
	g_free (filename);
	g_free (package_id);
	g_free (directory);
	g_free (package_list);
	g_free (options_help);
	g_object_unref (control);
	return 0;
}
