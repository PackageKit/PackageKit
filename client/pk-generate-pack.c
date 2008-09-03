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

#include <pk-package-ids.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-package-id.h>
#include <pk-common.h>
#ifdef HAVE_ARCHIVE_H
#include <archive.h>
#include <archive_entry.h>
#endif /* HAVE_ARCHIVE_H */
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-tools-common.h"


/**
 * pk_generate_pack_perhaps_resolve:
 **/
gchar *
pk_generate_pack_perhaps_resolve (PkClient *client, PkBitfield filter, const gchar *package, GError **error)
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
	if (valid) {
		return g_strdup (package);
	}

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
 * pk_generate_pack_download_only:
 **/
gboolean
pk_generate_pack_download_only (PkClient *client, gchar **package_ids, const gchar *directory)
{
	gboolean ret;
	GError *error = NULL;

	/* check for NULL values */
	if (package_ids == NULL || directory == NULL) {
		egg_warning (_("failed to download: invalid package_id and/or directory"));
		ret = FALSE;
		goto out;
	}

	egg_debug ("download+ %s %s", package_ids[0], directory);
	ret = pk_client_reset (client, &error);
	if (!ret) {
		egg_warning ("failed to download: %s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = pk_client_download_packages (client, package_ids, directory, &error);
	if (!ret) {
		egg_warning ("failed to download: %s", error->message);
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
		egg_warning ("Cannot find the list of packages to be excluded");
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
			egg_debug ("removed %s", obj->id->name);
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
		egg_warning (_("Could not find a valid metadata file"));
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
		egg_warning ("failed to convert to text: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save contents */
	ret = g_file_set_contents (full_path, data, -1, &error);
	if (!ret) {
		egg_warning ("failed to save file: %s", error->message);
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

#ifdef HAVE_ARCHIVE_H
/**
 * pk_generate_pack_archive_add_file:
 **/
static gboolean
pk_generate_pack_archive_add_file (struct archive *arch, const gchar *filename, GError **error)
{
	int retval;
	int len;
	int fd = -1;
	int wrote;
	gboolean ret = FALSE;
	gchar *filename_basename = NULL;
	struct archive_entry *entry = NULL;
	struct stat st;
	gchar buff[8192];

	/* stat file */
	retval = stat (filename, &st);
	if (retval != 0) {
		*error = g_error_new (1, 0, "file not found %s", filename);
		goto out;
	}
	egg_debug ("stat(%s), size=%lu bytes\n", filename, st.st_size);

	/* create new entry */
	entry = archive_entry_new ();
	archive_entry_copy_stat (entry, &st);
	filename_basename = g_path_get_basename (filename);
	archive_entry_set_pathname (entry, filename_basename);

	/* ._BIG FAT BUG_. We should not have to do this, as it should be
	 * set from archive_entry_copy_stat() */
	archive_entry_set_size (entry, st.st_size);

	/* write header */
	retval = archive_write_header (arch, entry);
	if (retval != ARCHIVE_OK) {
		*error = g_error_new (1, 0, "failed to write header: %s\n", archive_error_string (arch));
		goto out;
	}

	/* open file to copy */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		*error = g_error_new (1, 0, "failed to get fd for %s", filename);
		goto out;
	}

	/* write data to archive -- how come no convenience function? */
	len = read (fd, buff, sizeof (buff));
	while (len > 0) {
		wrote = archive_write_data (arch, buff, len);
		if (wrote != len)
			egg_warning("wrote %i instead of %i\n", wrote, len);
		len = read (fd, buff, sizeof (buff));
	}
	ret = TRUE;
out:
	if (fd >= 0)
		close (fd);
	if (entry != NULL)
		archive_entry_free (entry);
	g_free (filename_basename);
	return ret;
}

/**
 * pk_generate_pack_create:
 **/
gboolean
pk_generate_pack_create (const gchar *tarfilename, GPtrArray *file_array, GError **error)
{
	struct archive *arch = NULL;
	gboolean ret = FALSE;
	const gchar *src;
	guint i;
	gchar *metadata_filename;

	g_return_val_if_fail (tarfilename != NULL, FALSE);
	g_return_val_if_fail (file_array != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);

	/* create a file with metadata in it */
	metadata_filename = g_build_filename (g_get_tmp_dir (), "metadata.conf", NULL);
	ret = pk_generate_pack_set_metadata (metadata_filename);
	if (!ret) {
	        *error = g_error_new (1, 0, "failed to generate metadata file %s", metadata_filename);
	        goto out;
	}
	g_ptr_array_add (file_array, g_strdup (metadata_filename));

	/* we can only write tar achives */
	arch = archive_write_new ();
	archive_write_set_compression_none (arch);
	archive_write_set_format_ustar (arch);
	archive_write_open_filename (arch, tarfilename);

	/* for each filename */
	for (i=0; i<file_array->len; i++) {
		src = (const gchar *) g_ptr_array_index (file_array, i);
		/* try to add to archive */
		ret = pk_generate_pack_archive_add_file (arch, src, error);
		if (!ret)
			goto out;
	}

	/* completed all okay */
	ret = TRUE;
out:
	/* delete each filename */
	for (i=0; i<file_array->len; i++) {
		src = (const gchar *) g_ptr_array_index (file_array, i);
		g_remove (src);
	}

	/* close the archive */
	if (arch != NULL) {
		archive_write_close (arch);
		archive_write_finish (arch);
	}
	return ret;
}
#else
/**
 * pk_generate_pack_create:
 **/
gboolean
pk_generate_pack_create (const gchar *tarfilename, GPtrArray *file_array, GError **error)
{
	*error = g_error_new (1, 0, "Cannot create pack as PackageKit as not built with libtar support");
	return FALSE;
}
#endif

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
		egg_warning ("failed to get directory");
		goto out;
	}

	/* try and open the directory */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		egg_warning ("failed to get directory for %s", directory);
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
		egg_warning ("failed to resolve: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* download this package */
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_generate_pack_download_only (client, package_ids, directory);
	if (!ret) {
		egg_warning ("failed to download main package: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get depends */
	ret = pk_client_reset (client, &error_local);
	if (!ret) {
		egg_warning ("failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	egg_debug ("Getting depends for %s", package_id);
	ret = pk_client_get_depends (client, PK_FILTER_ENUM_NONE, package_ids, TRUE, &error_local);
	if (!ret) {
		egg_warning ("failed to get depends: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
	g_strfreev (package_ids);

	/* get the deps */
	list = pk_client_get_package_list (client);

	/* remove some deps */
	ret = pk_generate_pack_exclude_packages (list, package_list);
	if (!ret) {
		egg_warning ("failed to exclude packages");
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
		egg_warning ("failed to download deps of package: %s", package_id);
		goto out;
	}

	/* find packages that were downloaded */
	file_array = pk_generate_pack_scan_dir (directory);
	if (file_array == NULL) {
		egg_warning ("failed to scan directory: %s", directory);
		goto out;
	}

	/* generate pack file */
	ret = pk_generate_pack_create (pack_filename, file_array, &error_local);
	if (!ret) {
		egg_warning ("failed to create archive: %s", error_local->message);
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

	if (!libst_start (test, "PkGeneratePack"))
		return;

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
	if (file == NULL)
		libst_success (test, NULL);
	else {
		libst_failed (test, "failed to resolve %s", error->message);
		g_error_free (error);
	}
	g_free (file);

	/************************************************************/
	libst_title (test, "test perhaps resolve gitk");
	retval = pk_client_reset(client, &error);
	file = pk_generate_pack_perhaps_resolve (client, PK_FILTER_ENUM_NONE, "gitk;1.5.5.1-1.fc9;i386;installed", &error);
	if (file != NULL && egg_strequal (file, "gitk;1.5.5.1-1.fc9;i386;installed"))
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
	g_object_unref (client);

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
	g_object_unref (list);

	/************************************************************/
	libst_title (test, "exclude false.txt");
	list = pk_package_list_new ();
	ret = pk_generate_pack_exclude_packages (list, "/media/USB/false.txt");
	if (!ret)
		libst_success (test, NULL);
	else
		libst_failed (test, NULL);
	g_object_unref (list);

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
	libst_title (test, "generate pack /tmp/gitk.servicepack gitk");
	file_array = g_ptr_array_new ();
	src = g_build_filename ("/tmp", "gitk-1.5.5.1-1.fc9.i386.rpm", NULL);
	g_ptr_array_add (file_array, src);
	ret = pk_generate_pack_create ("/tmp/gitk.servicepack",file_array, &error);
	if (!ret) {
		if (error != NULL) {
			libst_failed (test, "failed to create pack %s" , error->message);
			g_error_free (error);
		} else {
			libst_failed (test, "could not set error");
		}
	} else {
		libst_success (test, NULL);
	}
	if (file_array != NULL)
		g_ptr_array_free (file_array, TRUE);
	g_remove ("/tmp/gitk.servicepack");

	/************************************************************/
	libst_end (test);
}
#endif
