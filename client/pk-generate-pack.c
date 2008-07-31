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
#include <pk-package-ids.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-package-id.h>
#include <pk-common.h>
#include <libtar.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include "pk-tools-common.h"

//FIXME: This should be generated using g_get_tmp_dir and random chars
#define PK_GENPACK_TEMP_DIR "/tmp/pack"

/**
 * pk_generate_pack_perhaps_resolve:
 **/
static gchar *
pk_generate_pack_perhaps_resolve (PkClient *client, PkFilterEnum filter, const gchar *package, GError **error)
{
	gboolean ret;
	gboolean valid;
	guint i;
	guint length;
	const PkPackageObj *obj;
	PkPackageList *list;
	gchar **packages;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid) {
		return g_strdup (package);
	}

	ret = pk_client_reset (client, error);
	if (ret == FALSE) {
		pk_warning ("failed to reset client task");
		return NULL;
	}

	/* we need to resolve it */
	packages = pk_package_ids_from_id (package);
	ret = pk_client_resolve (client, filter, packages, error);
	g_strfreev (packages);
	if (ret == FALSE) {
		pk_warning ("Resolve failed");
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
			pk_warning ("failed to reset client task");
			return NULL;
		}
		ret = pk_client_what_provides (client, filter, PK_PROVIDES_ENUM_ANY, package, error);
		if (ret == FALSE) {
			pk_warning ("WhatProvides is not supported in this backend");
			return NULL;
		}
	}

	/* get length of items found again (we might have had success) */
	list = pk_client_get_package_list (client);
	length = pk_package_list_get_size (list);
	if (length == 0) {
		pk_warning (_("Could not find a package match"));
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
 * pk_generate_pack_download_only:
 **/
static gboolean
pk_generate_pack_download_only (PkClient *client, gchar **package_ids, const gchar *directory)
{
	gboolean ret;
	GError *error = NULL;

	pk_debug ("download+ %s %s", package_ids[0], directory);
	ret = pk_client_reset (client, &error);
	if (!ret) {
		pk_warning ("failed to download: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_download_packages (client, package_ids, directory, &error);
	if (!ret) {
		pk_warning ("failed to download: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_generate_pack_exclude_packages:
 **/
static gboolean
pk_generate_pack_exclude_packages (PkPackageList *list)
{
	guint i;
	guint length;
	gboolean found;
	PkPackageList *list_packages;
	const PkPackageObj *obj;
	gboolean ret;

	list_packages = pk_package_list_new ();

	/* load a list of packages already found on the users system */
	ret = pk_package_list_add_file (list_packages, "/var/lib/PackageKit/package-list.txt");
	if (!ret)
		goto out;

	/* do not just download everything, uselessly */
	length = pk_package_list_get_size (list_packages);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list_packages, i);
		/* will just ignore if the obj is not there */
		found = pk_package_list_remove_obj (list, obj);
		if (found)
			pk_debug ("removed %s", obj->id->name);
	}

out:
	g_object_unref (list_packages);
	return ret;
}

/**
 * pk_generate_pack_create:
 **/
static gboolean
pk_generate_pack_create (const gchar *tarfilename, GPtrArray *file_array, GError **error)
{
	gboolean ret = TRUE;
	guint retval;
	TAR *t;
	FILE *file;
	guint i;
	const gchar *src;
	gchar *dest;

	/* FIXME: do we have to close this? */
	/* FIXME: check if file exists before we overwrite it */
	file = g_fopen (tarfilename, "a+");
	retval = tar_open (&t, (gchar *)tarfilename, NULL, O_WRONLY, 0, TAR_GNU);
	if (retval != 0) {
		*error = g_error_new (1, 0, "failed to open tar file: %s", tarfilename);
		ret = FALSE;
		goto out;
	}

	/* add each of the files */
	for (i=0; i<file_array->len; i++) {
		src = (const gchar *) g_ptr_array_index (file_array, i);
		dest =  g_path_get_basename (src);

		/* add file to archive */
		pk_debug ("adding %s", src);
		retval = tar_append_file (t, (gchar *)src, dest);
		if (retval != 0) {
			*error = g_error_new (1, 0, "failed to copy %s into %s", src, dest);
			ret = FALSE;
		}

		/* delete file */
		g_remove (src);

		/* free the stripped filename */
		g_free (dest);

		/* abort */
		if (!ret)
			break;
	}
	tar_append_eof (t);
	tar_close (t);

out:
	return ret;
}

/**
 * pk_generate_pack_scan_dir:
 **/
static GPtrArray *
pk_generate_pack_scan_dir (const gchar *directory)
{
	guint i;
	guint n;
	gchar *src;
	GPtrArray *file_array;
	struct dirent **namelist;

	/* FIXME: this has to be converted to GDir */
	n = scandir ("/tmp/pack", &namelist, 0, alphasort);

	file_array = g_ptr_array_new ();
	/* add each of the files */
	for (i=2; i<n; i++) {
		src = g_strjoin (G_DIR_SEPARATOR_S, directory, namelist[i]->d_name, NULL);
		g_ptr_array_add (file_array, src);
	}
	free (namelist);
	return file_array;
}

/**
 * pk_generate_pack_main:
 **/
static gboolean
pk_generate_pack_main (const gchar *pack_filename, const gchar *directory, const gchar *package, GError **error)
{

	gchar *package_id;
	gchar **package_ids;
	PkPackageList *list = NULL;
	guint length;
	gboolean download;
	guint i;
	const PkPackageObj *obj;
	GPtrArray *file_array = NULL;
	PkClient *client;
	GError *error_local;
	gboolean ret;
	gchar *text;

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, TRUE, NULL);

	/* resolve package */
	package_id = pk_generate_pack_perhaps_resolve (client, PK_FILTER_ENUM_NONE, package, &error_local);
	if (package_id == NULL) {
		pk_warning ("failed to resolve: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* download this package */
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_generate_pack_download_only (client, package_ids, directory);
	if (!ret) {
		pk_warning ("failed to download main package: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get depends */
	ret = pk_client_reset (client, &error_local);
	if (!ret) {
		pk_warning ("failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	pk_debug ("Getting depends for %s", package_id);
	ret = pk_client_get_depends (client, PK_FILTER_ENUM_NONE, package_ids, TRUE, &error_local);
	if (!ret) {
		pk_warning ("failed to get depends: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
	g_strfreev (package_ids);

	/* get the deps */
	list = pk_client_get_package_list (client);

	/* remove some deps */
	ret = pk_generate_pack_exclude_packages (list);
	if (!ret) {
		pk_warning ("failed to exclude packages");
		goto out;
	}

	/* list deps */
	length = pk_package_list_get_size (list);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		text = pk_package_obj_to_string (obj);
		g_print ("%s\n", text);
		g_free (text);
	}

	/* confirm we want the deps */
	if (length != 0) {
		/* get user input */
		download = pk_console_get_prompt (_("Okay to download the additional packages"), TRUE);

		/* we chickened out */
		if (download == FALSE) {
			g_print ("%s\n", _("Cancelled!"));
			ret = FALSE;
			goto out;
		}

		/* convert to list of package_ids */
		package_ids = pk_package_list_to_argv (list);

		ret = pk_generate_pack_download_only (client, package_ids, directory);
		g_strfreev (package_ids);
		if (!ret) {
			pk_warning ("failed to download deps of package: %s", package_id);
			goto out;
		}
	}

	/* find packages that were downloaded */
	file_array = pk_generate_pack_scan_dir (directory);
	if (!ret) {
		pk_warning ("failed to download packages");
		goto out;
	}

	/* generate pack file */
	ret = pk_generate_pack_create (pack_filename, file_array, &error_local);
	if (!ret) {
		pk_warning ("failed to create archive: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

out:
	g_object_unref (client);
	if (list != NULL)
		g_object_unref (list);
	g_free (package_id);
	if (file_array != NULL)
		g_ptr_array_free (file_array, TRUE);
	return ret;
}

int
main (int argc, char *argv[])
{
	GError *error = NULL;
	gboolean verbose = FALSE;
	GOptionContext *context;
	gchar *options_help;
	gboolean ret;
	guint retval;
	const gchar *package = NULL;
	gchar *pack_filename = NULL;
	gchar *packname = NULL;
	PkControl *control = NULL;
	PkRoleEnum roles;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
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

	/* get rid of temp directory if it already exists */
	g_rmdir (PK_GENPACK_TEMP_DIR);

	/* make the temporary directory */
	retval = g_mkdir_with_parents (PK_GENPACK_TEMP_DIR, 0777);
	if (retval != 0) {
		g_print ("%s: %s\n", _("Failed to create directory"), PK_GENPACK_TEMP_DIR);
		goto out;
	}

	/* generate the pack */
	ret = pk_generate_pack_main (pack_filename, PK_GENPACK_TEMP_DIR, package, &error);
	if (!ret) {
		g_print ("%s: %s\n", _("Failed to create pack"), error->message);
		g_error_free (error);
		goto out;
	}

out:
	/* get rid of temp directory */
	g_rmdir (PK_GENPACK_TEMP_DIR);

	g_free (packname);
	g_free (options_help);
	g_object_unref (control);
	return 0;
}
