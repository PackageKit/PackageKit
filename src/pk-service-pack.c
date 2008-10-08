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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_ARCHIVE_H
#include <archive.h>
#include <archive_entry.h>
#endif /* HAVE_ARCHIVE_H */

#include <glib.h>
#include <glib/gstdio.h>

#include "egg-debug.h"
#include "egg-string.h"

#include <pk-common.h>

#include "pk-service-pack.h"

#define PK_SERVICE_PACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SERVICE_PACK, PkServicePackPrivate))

struct PkServicePackPrivate
{
	PkPackageList		*exclude_list;
	gchar			*filename;
	gchar			*directory;
};

G_DEFINE_TYPE (PkServicePack, pk_service_pack, G_TYPE_OBJECT)


/**
 * pk_service_pack_check_metadata_file:
 **/
static gboolean
pk_service_pack_check_metadata_file (const gchar *full_path)
{
	GKeyFile *file;
	gboolean ret;
	GError *error = NULL;
	gchar *distro_id = NULL;
	gchar *distro_id_us = NULL;

	/* load the file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, full_path, G_KEY_FILE_NONE, &error);
	if (!ret) {
		egg_warning ("failed to load file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* read the value */
	distro_id = g_key_file_get_string (file, PK_SERVICE_PACK_GROUP_NAME, "distro_id", &error);
	if (!ret) {
		egg_warning ("failed to get value: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get this system id */
	distro_id_us = pk_get_distro_id ();

	/* do we match? */
	ret = egg_strequal (distro_id_us, distro_id);

out:
	g_key_file_free (file);
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
		*error = g_error_new (1, 0, "failed to get cwd");
		goto out;
	}

	/* we can only read tar achives */
	arch = archive_read_new ();
	archive_read_support_format_tar (arch);

	/* open the tar file */
	r = archive_read_open_file (arch, filename, 10240);
	if (r) {
		*error = g_error_new (1, 0, "cannot open: %s", archive_error_string (arch));
		goto out;
	}

	/* switch to our destination directory */
	retval = chdir (directory);
	if (retval != 0) {
		*error = g_error_new (1, 0, "failed chdir to %s", directory);
		goto out;
	}

	/* decompress each file */
	for (;;) {
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			*error = g_error_new (1, 0, "cannot read header: %s", archive_error_string (arch));
			goto out;
		}
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			*error = g_error_new (1, 0, "cannot extract: %s", archive_error_string (arch));
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
gboolean
pk_service_pack_extract (const gchar *filename, const gchar *directory, GError **error)
{
	*error = g_error_new (1, 0, "Cannot check PackageKit as not built with libarchive support");
	return FALSE;
}
#endif /* HAVE_ARCHIVE_H */

/**
 * pk_service_pack_check_valid:
 **/
gboolean
pk_service_pack_check_valid (PkServicePack *pack, GError **error)
{
	gboolean ret = TRUE;
	gchar *directory = NULL;
	gchar *metafile = NULL;
	GDir *dir = NULL;
	const gchar *filename_entry;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);

	/* ITS4: ignore, the user has no control over the daemon envp  */
	directory = g_build_filename (g_get_tmp_dir (), "meta", NULL);
	ret = pk_service_pack_extract (pack->priv->filename, directory, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to check %s: %s", pack->priv->filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the files */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		*error = g_error_new (1, 0, "failed to get directory for %s", directory);
		ret = FALSE;
		goto out;
	}

	/* find the file, and check the metadata */
	while ((filename_entry = g_dir_read_name (dir))) {
		metafile = g_build_filename (directory, filename_entry, NULL);
		if (egg_strequal (filename_entry, "metadata.conf")) {
			ret = pk_service_pack_check_metadata_file (metafile);
			if (!ret) {
				*error = g_error_new (1, 0, "Service Pack %s not compatible with your distro", pack->priv->filename);
				ret = FALSE;
				goto out;
			}
		}
		g_free (metafile);
	}
out:
	g_rmdir (directory);
	g_free (directory);
	g_dir_close (dir);
	return ret;
}

/**
 * pk_service_pack_set_filename:
 **/
gboolean
pk_service_pack_set_filename (PkServicePack *pack, const gchar *filename)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_free (pack->priv->filename);
	pack->priv->filename = g_strdup (filename);
	return TRUE;
}

/**
 * pk_service_pack_set_temp_directory:
 **/
gboolean
pk_service_pack_set_temp_directory (PkServicePack *pack, const gchar *directory)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_free (pack->priv->directory);
	pack->priv->directory = g_strdup (directory);
	return TRUE;
}

/**
 * pk_service_pack_set_exclude_list:
 **/
gboolean
pk_service_pack_set_exclude_list (PkServicePack *pack, PkPackageList *list)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (list != NULL, FALSE);
	if (pack->priv->exclude_list != NULL)
		g_object_unref (pack->priv->exclude_list);
	pack->priv->exclude_list = g_object_ref (list);
	return TRUE;
}

/**
 * pk_service_pack_create_for_package:
 **/
gboolean
pk_service_pack_create_for_package (PkServicePack *pack, const gchar *package, GError **error)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);
	return TRUE;
}

/**
 * pk_service_pack_create_for_updates:
 **/
gboolean
pk_service_pack_create_for_updates (PkServicePack *pack, GError **error)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);
	return TRUE;
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

	if (pack->priv->exclude_list != NULL)
		g_object_unref (pack->priv->exclude_list);
	g_free (pack->priv->directory);
	g_free (pack->priv->filename);

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
	pack->priv->exclude_list = NULL;
	pack->priv->filename = NULL;
	pack->priv->directory = NULL;
}

/**
 * pk_service_pack_new:
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

void
egg_test_service_pack (EggTest *test)
{
	PkServicePack *pack;

	if (!egg_test_start (test, "PkServicePack"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	pack = pk_service_pack_new ();
	egg_test_assert (test, pack != NULL);

	g_object_unref (pack);

	egg_test_end (test);
}
#endif

