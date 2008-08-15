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


/**
 * pk_generate_pack_perhaps_resolve:
 **/
gchar *
pk_generate_pack_perhaps_resolve (PkClient *client, PkFilterEnum filter, const gchar *package, GError **error)
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
		pk_warning ("Cannot resolve the package: invalid package");
		return NULL;
	}

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
gboolean
pk_generate_pack_download_only (PkClient *client, gchar **package_ids, const gchar *directory)
{
	gboolean ret;
	GError *error = NULL;

	/* check for NULL values */
	if (package_ids == NULL || directory == NULL) {
		pk_warning (_("failed to download: invalid package_id and/or directory"));
		ret = FALSE;
		goto out;
	}

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
gboolean
pk_generate_pack_exclude_packages (PkPackageList *list, const gchar *package_list)
{
	guint i;
	guint length;
	gboolean found;
	PkPackageList *list_packages;
	const PkPackageObj *obj;
	gboolean ret;

	list_packages = pk_package_list_new ();

	/* check for NULL values */
	if (package_list == NULL) {
		pk_warning ("Cannot find the list of packages to be excluded");
		ret = FALSE;
		goto out;
	}

	/* load a list of packages already found on the users system */
	ret = pk_package_list_add_file (list_packages, package_list);
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
 * pk_generate_pack_set_metadata:
 **/
gboolean
pk_generate_pack_set_metadata (const gchar *full_path)
{
	gboolean ret = FALSE;
	gchar *distro_id = NULL;
	gchar *datetime = NULL;
	GError *error = NULL;
	GKeyFile *file = NULL;
	gchar *data = NULL;

	file = g_key_file_new ();

	/* check for NULL values */
	if (full_path == NULL) {
		pk_warning (_("Could not find a valid metadata file"));
		goto out;
	}

	/* get needed data */
	distro_id = pk_get_distro_id ();
	if (distro_id == NULL)
		goto out;
	datetime = pk_iso8601_present ();
	if (datetime == NULL)
		goto out;

	g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "distro_id", distro_id);
	g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "created", datetime);

	/* convert to text */
	data = g_key_file_to_data (file, NULL, &error);
	if (data == NULL) {
		pk_warning ("failed to convert to text: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save contents */
	ret = g_file_set_contents (full_path, data, -1, &error);
	if (!ret) {
		pk_warning ("failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}

out:
	g_key_file_free (file);
	g_free (data);
	g_free (distro_id);
	g_free (datetime);
	return ret;
}

/**
 * pk_generate_pack_create:
 **/
gboolean
pk_generate_pack_create (const gchar *tarfilename, GPtrArray *file_array, GError **error)
{
	gboolean ret = TRUE;
	guint retval;
	TAR *t;
	FILE *file;
	guint i;
	gchar *src;
	gchar *dest;
	gchar *meta_src;
	gchar *meta_dest = NULL;

	/* create a file with metadata in it */
	meta_src = g_build_filename (g_get_tmp_dir (), "metadata.conf", NULL);
	ret = pk_generate_pack_set_metadata (meta_src);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to generate metadata file %s", meta_src);
		ret = FALSE;
		goto out;
	}

	/* create the tar file */
	file = g_fopen (tarfilename, "a+");
	retval = tar_open (&t, (gchar *)tarfilename, NULL, O_WRONLY, 0, TAR_GNU);
	if (retval != 0) {
		*error = g_error_new (1, 0, "failed to open tar file: %s", tarfilename);
		ret = FALSE;
		goto out;
	}

	/* add the metadata first */
	meta_dest = g_path_get_basename (meta_src);
	retval = tar_append_file(t, (gchar *)meta_src, meta_dest);
	if (retval != 0) {
		*error = g_error_new (1, 0, "failed to copy %s into %s", meta_src, meta_dest);
		ret = FALSE;
		goto out;
	}

	/* check for NULL values */
	if (file_array == NULL) {
		g_remove ((gchar *) tarfilename);
		ret = FALSE;
		goto out;
	}

	/* add each of the files */
	for (i=0; i<file_array->len; i++) {
		src = (gchar *) g_ptr_array_index (file_array, i);
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
		g_free (src);

		/* free the stripped filename */
		g_free (dest);

		/* abort */
		if (!ret)
			break;
	}
	tar_append_eof (t);
	tar_close (t);
	fclose (file);
out:
	/* delete metadata file */
	g_remove (meta_src);
	g_free (meta_src);
	g_free (meta_dest);
	return ret;
}

/**
 * pk_generate_pack_scan_dir:
 **/
GPtrArray *
pk_generate_pack_scan_dir (const gchar *directory)
{
	gchar *src;
	GPtrArray *file_array = NULL;
	GDir *dir;
	const gchar *filename;

	/* check for NULL values */
	if (directory == NULL) {
		pk_warning ("failed to get directory");
		goto out;
	}

	/* try and open the directory */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		pk_warning ("failed to get directory for %s", directory);
		goto out;
	}

	/* add each file to an array */
	file_array = g_ptr_array_new ();
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (directory, filename, NULL);
		g_ptr_array_add (file_array, src);
	}
	g_dir_close (dir);
out:
	return file_array;
}

/**
 * pk_generate_pack_main:
 **/
gboolean
pk_generate_pack_main (const gchar *pack_filename, const gchar *directory, const gchar *package, const gchar *package_list, GError **error)
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
	GError *error_local = NULL;
	gboolean ret = FALSE;
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
	ret = pk_generate_pack_exclude_packages (list, package_list);
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
	}

	/* failed to get deps */
	if (!ret) {
		pk_warning ("failed to download deps of package: %s", package_id);
		goto out;
	}

	/* find packages that were downloaded */
	file_array = pk_generate_pack_scan_dir (directory);
	if (file_array == NULL) {
		pk_warning ("failed to scan directory: %s", directory);
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_generate_pack (LibSelfTest *test)
{
	PkClient *client = NULL;
	gboolean ret;
	gboolean retval;
	GError *error = NULL;
	gchar *file;
	PkPackageList *list = NULL;
	GPtrArray *file_array = NULL;
	gchar *src;
	gchar **package_ids;

	if (libst_start (test, "PkGeneratePack", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get client");
	client = pk_client_new ();
	if (client != NULL)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "test perhaps resolve NULL");
	retval = pk_client_reset (client, &error);
	file = pk_generate_pack_perhaps_resolve (client, PK_FILTER_ENUM_NONE, NULL, &error);
	if (file == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to resolve %s", error->message);
		g_error_free (error);
	}
	g_free (file);

	/************************************************************/
	libst_title (test, "test perhaps resolve gitk");
	retval = pk_client_reset(client, &error);
	file = pk_generate_pack_perhaps_resolve (client, PK_FILTER_ENUM_NONE, "gitk;1.5.5.1-1.fc9;i386;installed", &error);
	if (file != NULL && pk_strequal (file, "gitk;1.5.5.1-1.fc9;i386;installed"))
		libst_success (test, NULL);
	else
		libst_failed (test, "got: %s", file);
	g_free (file);

	/************************************************************/
	libst_title (test, "download only NULL");
	ret = pk_generate_pack_download_only (client, NULL, NULL);
	if (!ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "download only gitk");
	package_ids = pk_package_ids_from_id ("gitk;1.5.5.1-1.fc9;i386;installed");
	ret = pk_generate_pack_download_only (client, package_ids, "/tmp");
	if (ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);
	g_strfreev (package_ids);

	/************************************************************/
	libst_title (test, "exclude NULL");
	list = pk_package_list_new ();
	ret = pk_generate_pack_exclude_packages (list, NULL);
	if (!ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "exclude /var/lib/PackageKit/package-list.txt");
	list = pk_package_list_new ();
	ret = pk_generate_pack_exclude_packages (list, "/var/lib/PackageKit/package-list.txt");
	if (ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "exclude false.txt");
	list = pk_package_list_new ();
	ret = pk_generate_pack_exclude_packages (list, "/media/USB/false.txt");
	if (!ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "metadata NULL");
	ret = pk_generate_pack_set_metadata (NULL);
	if (!ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "metadata /tmp/metadata.conf");
	ret = pk_generate_pack_set_metadata ("/tmp/metadata.conf");
	if (ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);
	g_remove ("/tmp/metadata.conf");

	/************************************************************/
	libst_title (test, "scandir NULL");
	file_array = pk_generate_pack_scan_dir (NULL);
	if (file_array == NULL)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "scandir /tmp");
	file_array = pk_generate_pack_scan_dir ("/tmp");
	if (file_array != NULL)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);

	/************************************************************/
	libst_title (test, "generate pack NULL NULL");
	ret = pk_generate_pack_create (NULL, NULL, &error);
	if (!ret) {
		if (error != NULL)
			libst_success (test, "failed to create pack %s" , error->message);
		else
			libst_failed (test, "could not set error");
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "generate pack /tmp/test.pack NULL");
	ret = pk_generate_pack_create ("/tmp/test.pack", NULL, &error);
	if (!ret) {
		if (error != NULL)
			libst_success (test, "failed to create pack %s" , error->message);
		else
			libst_failed (test, "could not set error");
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "generate pack /tmp/test NULL");
	ret = pk_generate_pack_create ("/tmp/test", NULL, &error);
	if (!ret) {
		if (error != NULL)
			libst_success (test, "failed to create pack %s" , error->message);
		else
			libst_failed (test, "could not set error");
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "generate pack /tmp/test.tar NULL");
	ret = pk_generate_pack_create ("test.tar", NULL, &error);
	if (!ret) {
		if (error != NULL)
			libst_success (test, "failed to create pack %s" , error->message);
		else
			libst_failed (test, "could not set error");
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "generate pack NULL gitk");
	file_array = g_ptr_array_new ();
	g_ptr_array_add (file_array, NULL);
	ret = pk_generate_pack_create (NULL, file_array, &error);
	if (!ret) {
		if (error != NULL)
			libst_success (test, "failed to create pack %s" , error->message);
		else
			libst_failed (test, "could not set error");
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "generate pack /tmp/gitk.pack gitk");
	file_array = g_ptr_array_new ();
	src = g_build_filename ("/tmp", "gitk-1.5.5.1-1.fc9.i386.rpm", NULL);
	g_ptr_array_add (file_array, src);
	ret = pk_generate_pack_create ("/tmp/gitk.pack",file_array, &error);
	if (!ret) {
		if (error != NULL)
			libst_failed (test, "failed to create pack %s" , error->message);
		else
			libst_failed (test, "could not set error");
	} else {
		libst_success (test, NULL);
	}
	/************************************************************/
}
#endif
