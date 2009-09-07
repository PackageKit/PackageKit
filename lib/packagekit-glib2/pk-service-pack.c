/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>

#ifdef HAVE_ARCHIVE_H
#include <archive.h>
#include <archive_entry.h>
#endif /* HAVE_ARCHIVE_H */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-service-pack.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-package-ids.h>

#include "egg-debug.h"
#include "egg-string.h"

#define PK_SERVICE_PACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SERVICE_PACK, PkServicePackPrivate))

typedef enum {
	PK_SERVICE_PACK_TYPE_UPDATE,
	PK_SERVICE_PACK_TYPE_INSTALL,
	PK_SERVICE_PACK_TYPE_UNKNOWN
} PkServicePackType;

/**
 * PkServicePackState:
 *
 * For use in the async methods
 **/
typedef struct {
	gboolean			 ret;
	gchar				*filename;
	gchar				**package_ids;
	gchar				**package_ids_exclude;
	gpointer			 progress_user_data;
	guint				 request;
	GCancellable			*cancellable;
	GSimpleAsyncResult		*res;
	PkProgressCallback		 progress_callback;
	PkServicePack			*pack;
	PkServicePackType		 type;
} PkServicePackState;

struct PkServicePackPrivate
{
	gchar			*directory;
	PkClient		*client;
};

G_DEFINE_TYPE (PkServicePack, pk_service_pack, G_TYPE_OBJECT)

/**
 * pk_service_pack_error_quark:
 *
 * Return value: Our personal error quark.
 **/
GQuark
pk_service_pack_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_service_pack_error");
	return quark;
}

/**
 * pk_service_pack_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_service_pack_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_SETUP, "FailedSetup"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_DOWNLOAD, "FailedDownload"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION, "FailedExtraction"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_CREATE, "FailedCreate"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_NOTHING_TO_DO, "NothingToDo"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_NOT_COMPATIBLE, "NotCompatible"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkServicePackError", values);
	}
	return etype;
}

/**
 * pk_service_pack_check_metadata_file:
 **/
static gboolean
pk_service_pack_check_metadata_file (const gchar *full_path, GError **error)
{
	GKeyFile *file;
	gboolean ret;
	GError *error_local = NULL;
	gchar *type = NULL;
	gchar *distro_id = NULL;
	gchar *distro_id_us = NULL;

	/* load the file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, full_path, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to load file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* read the value */
	distro_id = g_key_file_get_string (file, PK_SERVICE_PACK_GROUP_NAME, "distro_id", &error_local);
	if (distro_id == NULL) {
		*error = g_error_new (1, 0, "failed to get value: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* read the value */
	type = g_key_file_get_string (file, PK_SERVICE_PACK_GROUP_NAME, "type", &error_local);
	if (type == NULL) {
		*error = g_error_new (1, 0, "failed to get type: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* check the types we support */
	if (g_strcmp0 (type, "update") != 0 && g_strcmp0 (type, "install") != 0) {
		*error = g_error_new (1, 0, "does not have correct type key: %s", type);
		ret = FALSE;
		goto out;
	}

	/* get this system id */
	distro_id_us = pk_get_distro_id ();

	/* do we match? */
	ret = (g_strcmp0 (distro_id_us, distro_id) == 0);
	if (!ret)
		*error = g_error_new (1, 0, "distro id did not match %s == %s", distro_id_us, distro_id);

out:
	g_key_file_free (file);
	g_free (type);
	g_free (distro_id);
	g_free (distro_id_us);
	return ret;
}

/**
 * pk_service_pack_extract:
 * @directory: the directory to unpack into
 * @error: a valid %GError
 *
 * Decompress a tar file
 *
 * Return value: %TRUE if the file was decompressed
 **/
#ifdef HAVE_ARCHIVE_H
static gboolean
pk_service_pack_extract (const gchar *filename, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	struct archive *arch = NULL;
	struct archive_entry *entry;
	int r;
	int retval;
	gchar *retcwd;
	gchar buf[PATH_MAX];

	/* save the PWD as we chdir to extract */
	retcwd = getcwd (buf, PATH_MAX);
	if (retcwd == NULL) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to get cwd");
		goto out;
	}

	/* we can only read tar achives */
	arch = archive_read_new ();
	archive_read_support_format_tar (arch);

	/* open the tar file */
	r = archive_read_open_file (arch, filename, 10240);
	if (r) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
				      "cannot open: %s", archive_error_string (arch));
		goto out;
	}

	/* switch to our destination directory */
	retval = chdir (directory);
	if (retval != 0) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed chdir to %s", directory);
		goto out;
	}

	/* decompress each file */
	for (;;) {
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
					      "cannot read header: %s", archive_error_string (arch));
			goto out;
		}
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
					      "cannot extract: %s", archive_error_string (arch));
			goto out;
		}
	}

	/* completed all okay */
	ret = TRUE;
out:
	/* close the archive */
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_finish (arch);
	}

	/* switch back to PWD */
	retval = chdir (buf);
	if (retval != 0)
		egg_warning ("cannot chdir back!");

	return ret;
}
#else /* HAVE_ARCHIVE_H */
static gboolean
pk_service_pack_extract (const gchar *filename, const gchar *directory, GError **error)
{
	*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
			      "The service pack %s cannot be extracted as PackageKit was not built with libarchive support", filename);
	return FALSE;
}
#endif /* HAVE_ARCHIVE_H */

/**
 * pk_service_pack_get_random:
 **/
static gchar *
pk_service_pack_get_random (const gchar *prefix, guint length)
{
	guint32 n;
	gchar *str;
	guint i;
	guint prefix_len;

	/* make a string to hold both parts */
	prefix_len = egg_strlen (prefix, 28);
	str = g_strnfill (length + prefix_len, 'X');

	/* copy over prefix */
	for (i=0; i<prefix_len; i++)
		str[i] = prefix[i];

	/* use random string */
	for (i=prefix_len; i<length+prefix_len; i++) {
		n = g_random_int_range (97, 122);
		str[i] = (gchar) n;
	}
	return str;
}

/**
 * pk_service_pack_create_temporary_directory:
 **/
static gchar *
pk_service_pack_create_temporary_directory (const gchar *prefix)
{
	gboolean ret;
	gchar *random;
	gchar *directory = NULL;

	/* ensure path does not already exist */
	do {
		/* last iter results, or NULL */
		g_free (directory);

		/* get a random path */
		random = pk_service_pack_get_random (prefix, 8);

		/* ITS4: ignore, the user has no control over the daemon envp  */
		directory = g_build_filename (g_get_tmp_dir (), random, NULL);
		g_free (random);
		ret = g_file_test (directory, G_FILE_TEST_IS_DIR);
	} while (ret);

	/* create so only user (root) has rwx access */
	g_mkdir (directory, 0700);

	return directory;
}

/**
 * pk_service_pack_check_valid:
 * @pack: a valid #PkServicePack instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Checks to see if a service pack file is valid, and usable with this system.
 *
 * Return value: %TRUE if the service pack is valid
 **/
gboolean
pk_service_pack_check_valid (PkServicePack *pack, const gchar *filename, GError **error)
{
	gboolean ret = TRUE;
	gchar *directory = NULL;
	gchar *metafile = NULL;
	GDir *dir = NULL;
	const gchar *filename_entry;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* create a random directory */
	directory = pk_service_pack_create_temporary_directory ("PackageKit-");
	ret = pk_service_pack_extract (filename, directory, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
				      "failed to check %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the files */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to get directory for %s", directory);
		ret = FALSE;
		goto out;
	}

	/* find the file, and check the metadata */
	while ((filename_entry = g_dir_read_name (dir))) {
		metafile = g_build_filename (directory, filename_entry, NULL);
		if (g_strcmp0 (filename_entry, "metadata.conf") == 0) {
			ret = pk_service_pack_check_metadata_file (metafile, &error_local);
			if (!ret) {
				*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_NOT_COMPATIBLE,
						      "Service Pack %s not compatible with your distro: %s", filename, error_local->message);
				g_error_free (error_local);
				ret = FALSE;
				goto out;
			}
		}
		g_free (metafile);
	}
out:
	g_rmdir (directory);
	g_free (directory);
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * pk_service_pack_set_temp_directory:
 * @pack: a valid #PkServicePack instance
 * @directory: the directory to use, or %NULL to use the default
 *
 * Sets the directory to use when decompressing the service pack
 *
 * Return value: %TRUE if the directory was set
 **/
gboolean
pk_service_pack_set_temp_directory (PkServicePack *pack, const gchar *directory)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_free (pack->priv->directory);

	/* use default */
	if (directory == NULL)
		directory = pk_service_pack_create_temporary_directory ("PackageKit-");

	pack->priv->directory = g_strdup (directory);
	return TRUE;
}

#if 0
/**
 * pk_service_pack_download_package_ids:
 **/
static gboolean
pk_service_pack_download_package_ids (PkServicePack *pack, gchar **package_ids, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);

	egg_debug ("download+ %s", package_ids[0]);
	ret = pk_client_download_packages (pack->priv->client, package_ids, pack->priv->directory, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_DOWNLOAD,
				      "failed to download: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}
#endif

#if 0
/**
 * pk_service_pack_exclude_packages:
 **/
static gboolean
pk_service_pack_exclude_packages (PkServicePack *pack, gchar **package_ids)
{
	guint i;
	guint length;
	gboolean found;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* do not just download everything, uselessly */
	length = pk_package_list_get_size (package_ids);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (package_ids, i);
		/* will just ignore if the obj is not there */
		found = pk_obj_list_remove (PK_OBJ_LIST(list), obj);
		if (found)
			egg_debug ("removed %s", obj->id->name);
	}
	return TRUE;
}
#endif

#ifdef HAVE_ARCHIVE_H
/**
 * pk_service_pack_create_metadata_file:
 **/
static gboolean
pk_service_pack_create_metadata_file (PkServicePackState *state)
{
	gboolean ret = FALSE;
	gchar *distro_id = NULL;
	gchar *iso_time = NULL;
	GError *error = NULL;
	GKeyFile *file = NULL;
	gchar *data = NULL;

	g_return_val_if_fail (state->filename != NULL, FALSE);
	g_return_val_if_fail (state->type != PK_SERVICE_PACK_TYPE_UNKNOWN, FALSE);

	file = g_key_file_new ();

	/* get needed data */
	distro_id = pk_get_distro_id ();
	if (distro_id == NULL)
		goto out;
	iso_time = pk_iso8601_present ();
	if (iso_time == NULL)
		goto out;

	g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "distro_id", distro_id);
	g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "created", iso_time);

	if (state->type == PK_SERVICE_PACK_TYPE_INSTALL)
		g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "type", "install");
	else if (state->type == PK_SERVICE_PACK_TYPE_UPDATE)
		g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "type", "update");

	/* convert to text */
	data = g_key_file_to_data (file, NULL, &error);
	if (data == NULL) {
		egg_warning ("failed to convert to text: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save contents */
	ret = g_file_set_contents (state->filename, data, -1, &error);
	if (!ret) {
		egg_warning ("failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}

out:
	g_key_file_free (file);
	g_free (data);
	g_free (distro_id);
	g_free (iso_time);
	return ret;
}

/**
 * pk_service_pack_archive_add_file:
 **/
static gboolean
pk_service_pack_archive_add_file (struct archive *arch, const gchar *filename, GError **error)
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
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "file not found %s", filename);
		goto out;
	}
	egg_debug ("stat(%s), size=%lu bytes\n", filename, (glong) st.st_size);

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
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "failed to write header: %s\n", archive_error_string (arch));
		goto out;
	}

	/* open file to copy */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "failed to get fd for %s", filename);
		goto out;
	}

	/* ITS4: ignore, buffer statically preallocated  */
	len = read (fd, buff, sizeof (buff));
	/* write data to archive -- how come no convenience function? */
	while (len > 0) {
		wrote = archive_write_data (arch, buff, len);
		if (wrote != len)
			egg_warning("wrote %i instead of %i\n", wrote, len);
		/* ITS4: ignore, buffer statically preallocated  */
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
 * pk_service_pack_create_from_files:
 **/
static gboolean
pk_service_pack_create_from_files (PkServicePackState *state, gchar **file_array, GError **error)
{
	struct archive *arch = NULL;
	gboolean ret = FALSE;
	guint i;
	gchar *filename;
	gchar **files_and_metadata = NULL;

	g_return_val_if_fail (file_array != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);

	/* create a file with metadata in it */
	filename = g_build_filename (g_get_tmp_dir (), "metadata.conf", NULL);
	ret = pk_service_pack_create_metadata_file (state);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "failed to generate metadata file %s", filename);
		goto out;
	}
	files_and_metadata = pk_package_ids_add_id (file_array, filename);

	/* we can only write tar achives */
	arch = archive_write_new ();
	archive_write_set_compression_none (arch);
	archive_write_set_format_ustar (arch);
	archive_write_open_filename (arch, state->filename);

	/* for each filename */
	for (i=0; files_and_metadata[i] != NULL; i++) {
		/* try to add to archive */
		ret = pk_service_pack_archive_add_file (arch, files_and_metadata[i], error);
		if (!ret)
			goto out;
	}

	/* completed all okay */
	ret = TRUE;
out:
	g_strfreev (files_and_metadata);
	g_free (filename);
	/* delete each filename */
	for (i=0; file_array[i] != NULL; i++)
		g_remove (file_array[i]);

	/* close the archive */
	if (arch != NULL) {
		archive_write_close (arch);
		archive_write_finish (arch);
	}
	return ret;
}
#else
/**
 * pk_service_pack_create_from_files:
 **/
static gboolean
pk_service_pack_create_from_files (PkServicePackState *state, GPtrArray *file_array, GError **error)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
			      "The service pack %s cannot be created as PackageKit was not built with libarchive support", pack->priv->filename);
	return FALSE;
}
#endif

#if 0
/**
 * pk_service_pack_scan_files_in_directory:
 **/
static GPtrArray *
pk_service_pack_scan_files_in_directory (PkServicePack *pack)
{
	gchar *src;
	GPtrArray *file_array = NULL;
	GDir *dir;
	const gchar *filename;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), NULL);
	g_return_val_if_fail (pack->priv->directory != NULL, NULL);

	/* try and open the directory */
	dir = g_dir_open (pack->priv->directory, 0, NULL);
	if (dir == NULL) {
		egg_warning ("failed to get directory for %s", pack->priv->directory);
		goto out;
	}

	/* add each file to an array */
	file_array = g_ptr_array_new ();
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (pack->priv->directory, filename, NULL);
		g_ptr_array_add (file_array, src);
	}
	g_dir_close (dir);
out:
	return file_array;
}
#endif

#if 0
/**
 * pk_service_pack_package_cb:
 **/
static void
pk_service_pack_package_cb (PkClient *client, const PkPackageObj *obj, PkServicePack *pack)
{
	g_return_if_fail (PK_IS_SERVICE_PACK (pack));

	/* only shown downloading */
	if (obj->info != PK_INFO_ENUM_DOWNLOADING)
		return;
}

/**
 * pk_service_pack_create_for_package_ids_internal:
 **/
static gboolean
pk_service_pack_create_for_package_ids_internal (PkServicePack *pack, gchar **package_ids, GError **error)
{
	gchar **package_ids_deps = NULL;
	PkPackageList *list = NULL;
	guint length;
	GPtrArray *file_array = NULL;
	GError *error_local = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (state->filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);

	/* remove some deps */
	pk_package_list_set_fuzzy_arch (list, TRUE);
	pk_service_pack_exclude_packages (pack, list);

	/* get the deps */
	length = pk_package_list_get_size (list);
	if (length != 0) {
		/* download additional package_ids */
		package_ids_deps = pk_package_list_to_strv (list);
		pk_service_pack_status_changed (pack, PK_SERVICE_PACK_STATUS_DOWNLOAD_DEPENDENCIES);
		ret = pk_service_pack_download_package_ids (pack, package_ids_deps, &error_local);
		g_strfreev (package_ids_deps);

		/* failed to get deps */
		if (!ret) {
			*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
					      "failed to download deps of package: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* find packages that were downloaded */
	file_array = pk_service_pack_scan_files_in_directory (pack);
	if (file_array == NULL) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to scan directory: %s", pack->priv->directory);
		goto out;
	}

	/* generate pack file */
	ret = pk_service_pack_create_from_files (pack, file_array, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
				      "failed to create archive: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

out:
	if (list != NULL)
		g_object_unref (list);
	if (file_array != NULL) {
		g_ptr_array_foreach (file_array, (GFunc) g_free, NULL);
		g_ptr_array_free (file_array, TRUE);
	}
	return ret;
}
#endif

/**
 * pk_service_pack_generic_state_finish:
 **/
static void
pk_service_pack_generic_state_finish (PkServicePackState *state, const GError *error)
{
	/* remove weak ref */
	if (state->pack != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->pack), (gpointer) &state->pack);

	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		/* FIXME: change g_simple_async_result_set_from_error() to accept const GError */
		g_simple_async_result_set_from_error (state->res, (GError*) error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);
	g_strfreev (state->package_ids);
	g_strfreev (state->package_ids_exclude);
	g_free (state->filename);
	g_object_unref (state->res);
	g_slice_free (PkServicePackState, state);
}

/**
 * pk_service_pack_download_ready_cb:
 **/
static void
pk_service_pack_download_ready_cb (GObject *source_object, GAsyncResult *res, PkServicePackState *state)
{
	PkServicePack *pack = PK_SERVICE_PACK (source_object);
	GError *error = NULL;
	PkResults *results;
	PkExitEnum exit_enum;
	gboolean ret;
	gchar **files = NULL;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(pack), res, &error);
	if (results == NULL) {
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get exit code */
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		error = g_error_new (1, 0, "failed to download");
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* now create pack */
	egg_error ("todo");
	ret = pk_service_pack_create_from_files (state, files, &error);
	if (!ret) {
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we can't handle this, just finish the async method */
	state->ret = TRUE;

	/* we're done */
	pk_service_pack_generic_state_finish (state, error);
out:
	g_strfreev (files);
	if (results != NULL)
		g_object_unref (results);
	return;
}

/**
 * pk_service_pack_get_depends_ready_cb:
 **/
static void
pk_service_pack_get_depends_ready_cb (GObject *source_object, GAsyncResult *res, PkServicePackState *state)
{
	PkServicePack *pack = PK_SERVICE_PACK (source_object);
	GError *error = NULL;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *array = NULL;
	guint i;
	const PkResultItemPackage *package;
	gchar **package_ids = NULL;
	gchar **package_ids_to_download = NULL;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(pack), res, &error);
	if (results == NULL) {
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get exit code */
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		error = g_error_new (1, 0, "failed to download");
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* add all the results to the existing list */
	array = pk_results_get_package_array (results);
	package_ids = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_ids[i] = g_strdup (package->package_id);
	}
	package_ids_to_download = pk_package_ids_add_ids (state->package_ids, package_ids);

	/* now download */
	pk_client_download_packages_async (PK_CLIENT(state->pack), package_ids_to_download, pack->priv->directory,
					   state->cancellable, state->progress_callback, state->progress_user_data,
					   (GAsyncReadyCallback) pk_service_pack_download_ready_cb, state);
out:
	g_strfreev (package_ids);
	g_strfreev (package_ids_to_download);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return;
}

/**
 * pk_service_pack_create_for_package_ids_async:
 * @pack: a valid #PkServicePack instance
 * @filename: the filename of the service pack
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @package_ids_exclude: An array of packages to exclude
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @user_data: the data to pass to @callback
 *
 * Create a service pack for the specified Package IDs
 **/
void
pk_service_pack_create_for_package_ids_async (PkServicePack *pack, const gchar *filename, gchar **package_ids,
					      gchar **package_ids_exclude, GCancellable *cancellable,
					      PkProgressCallback progress_callback, gpointer progress_user_data,
					      GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkServicePackState *state;

	g_return_if_fail (PK_IS_SERVICE_PACK (pack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (pack), callback, user_data, pk_service_pack_create_for_package_ids_async);

	/* save state */
	state = g_slice_new0 (PkServicePackState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->pack = pack;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->filename = g_strdup (filename);
	state->package_ids = g_strdupv (package_ids);
	state->package_ids_exclude = g_strdupv (package_ids_exclude);
	state->type = PK_SERVICE_PACK_TYPE_INSTALL;
	g_object_add_weak_pointer (G_OBJECT (state->pack), (gpointer) &state->pack);

	/* get deps, TODO: use NEWEST? */
	pk_client_get_depends_async (PK_CLIENT(state->pack), PK_FILTER_ENUM_NONE, state->package_ids, TRUE,
				     state->cancellable, state->progress_callback, state->progress_user_data,
				     (GAsyncReadyCallback) pk_service_pack_get_depends_ready_cb, state);

	g_object_unref (res);
}

/**
 * pk_service_pack_get_updates_ready_cb:
 **/
static void
pk_service_pack_get_updates_ready_cb (GObject *source_object, GAsyncResult *res, PkServicePackState *state)
{
	PkServicePack *pack = PK_SERVICE_PACK (source_object);
	GError *error = NULL;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *array = NULL;
	guint i;
	const PkResultItemPackage *package;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(pack), res, &error);
	if (results == NULL) {
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get exit code */
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		error = g_error_new (1, 0, "failed to get updates");
		pk_service_pack_generic_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* add all the results to the existing list */
	array = pk_results_get_package_array (results);
	state->package_ids = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		state->package_ids[i] = g_strdup (package->package_id);
	}

	/* get deps, TODO: use NEWEST? */
	pk_client_get_depends_async (PK_CLIENT(state->pack), PK_FILTER_ENUM_NONE, state->package_ids, TRUE,
				     state->cancellable, state->progress_callback, state->progress_user_data,
				     (GAsyncReadyCallback) pk_service_pack_get_depends_ready_cb, state);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	return;
}

/**
 * pk_service_pack_create_for_updates_async:
 * @pack: a valid #PkServicePack instance
 * @filename: the filename of the service pack
 * @package_ids_exclude: An array of packages to exclude
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @user_data: the data to pass to @callback
 *
 * Create a service pack for the specified Package IDs
 **/
void
pk_service_pack_create_for_updates_async (PkServicePack *pack, const gchar *filename,
					  gchar **package_ids_exclude, GCancellable *cancellable,
					  PkProgressCallback progress_callback, gpointer progress_user_data,
					  GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkServicePackState *state;

	g_return_if_fail (PK_IS_SERVICE_PACK (pack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (pack), callback, user_data, pk_service_pack_create_for_updates_async);

	/* save state */
	state = g_slice_new0 (PkServicePackState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->pack = pack;
	state->type = PK_SERVICE_PACK_TYPE_UPDATE;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->filename = g_strdup (filename);
	state->package_ids_exclude = g_strdupv (package_ids_exclude);
	g_object_add_weak_pointer (G_OBJECT (state->pack), (gpointer) &state->pack);

	/* get deps, TODO: use NEWEST? */
	pk_client_get_updates_async (PK_CLIENT(state->pack), PK_FILTER_ENUM_NONE,
				     state->cancellable, state->progress_callback, state->progress_user_data,
				     (GAsyncReadyCallback) pk_service_pack_get_updates_ready_cb, state);

	g_object_unref (res);
}

/**
 * pk_service_pack_generic_finish:
 * @pack: a valid #PkServicePack instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE for success
 **/
gboolean
pk_service_pack_generic_finish (PkServicePack *pack, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/**
 * pk_service_pack_finalize:
 **/
static void
pk_service_pack_finalize (GObject *object)
{
	PkServicePack *pack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_SERVICE_PACK (object));
	pack = PK_SERVICE_PACK (object);

	g_object_unref (pack->priv->client);
	g_free (pack->priv->directory);

	G_OBJECT_CLASS (pk_service_pack_parent_class)->finalize (object);
}

/**
 * pk_service_pack_class_init:
 **/
static void
pk_service_pack_class_init (PkServicePackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_service_pack_finalize;

	g_type_class_add_private (klass, sizeof (PkServicePackPrivate));
}

/**
 * pk_service_pack_init:
 **/
static void
pk_service_pack_init (PkServicePack *pack)
{
	pack->priv = PK_SERVICE_PACK_GET_PRIVATE (pack);
	pack->priv->client = pk_client_new ();
	pack->priv->directory = NULL;
}

/**
 * pk_service_pack_new:
 *
 * Return value: A new service_pack class instance.
 **/
PkServicePack *
pk_service_pack_new (void)
{
	PkServicePack *pack;
	pack = g_object_new (PK_TYPE_SERVICE_PACK, NULL);
	return PK_SERVICE_PACK (pack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_service_pack_test_create_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkServicePack *pack = PK_SERVICE_PACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the results */
	ret = pk_service_pack_generic_finish (pack, res, &error);
	if (!ret) {
		egg_test_failed (test, "failed to create pack: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	egg_test_loop_quit (test);
}

static void
pk_service_pack_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		egg_debug ("now %s", pk_status_enum_to_text (status));
	}
}

void
pk_service_pack_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkServicePack *pack;
	gchar **package_ids;

	if (!egg_test_start (test, "PkServicePack"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	pack = pk_service_pack_new ();
	egg_test_assert (test, pack != NULL);

	/************************************************************/
	egg_test_title (test, "get service_pack");
	pack = pk_service_pack_new ();
	egg_test_assert (test, pack != NULL);

	/************************************************************/
	egg_test_title (test, "install package");
	package_ids = g_strsplit ("glib2;2.14.0;i386;fedora", ",", -1);
	pk_service_pack_create_for_package_ids_async (pack, "dave.servicepack", package_ids, NULL, NULL,
				        (PkProgressCallback) pk_service_pack_test_progress_cb, test,
				        (GAsyncReadyCallback) pk_service_pack_test_create_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 150000);
	egg_test_success (test, "installed in %i", egg_test_elapsed (test));

	g_object_unref (pack);

	egg_test_end (test);
}
#endif

