/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "hif-db.h"
#include "hif-package.h"
#include "hif-utils.h"

#define HIF_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), HIF_TYPE_DB, HifDbPrivate))

G_DEFINE_TYPE (HifDb, hif_db, G_TYPE_OBJECT)

/**
 * hif_db_create_dir:
 **/
static gboolean
hif_db_create_dir (const gchar *dir, GError **error)
{
	GFile *file = NULL;
	gboolean ret = TRUE;

	/* already exists */
	ret = g_file_test (dir, G_FILE_TEST_IS_DIR);
	if (ret)
		goto out;

	/* need to create */
	g_debug ("creating %s", dir);
	file = g_file_new_for_path (dir);
	ret = g_file_make_directory_with_parents (file, NULL, error);
out:
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * hif_db_get_dir_for_package:
 **/
static gchar *
hif_db_get_dir_for_package (HyPackage package)
{
	const gchar *pkgid;
	gchar *dir = NULL;

	pkgid = hif_package_get_pkgid (package);
	if (pkgid == NULL)
		goto out;
	dir = g_strdup_printf ("/var/lib/yum/yumdb/%c/%s-%s-%s-%s",
			       hy_package_get_name (package)[0],
			       pkgid,
			       hy_package_get_name (package),
			       hy_package_get_version (package),
			       hy_package_get_arch (package));
out:
	return dir;
}

/**
 * hif_db_get_string:
 **/
gchar *
hif_db_get_string (HifDb *db, HyPackage package, const gchar *key, GError **error)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *index_dir = NULL;
	gchar *value = NULL;

	g_return_val_if_fail (HIF_IS_DB (db), NULL);
	g_return_val_if_fail (package != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get file contents */
	index_dir = hif_db_get_dir_for_package (package);
	if (index_dir == NULL) {
		ret = FALSE;
		index_dir = hif_package_get_id (package);
		g_set_error (error,
			     HIF_ERROR,
			     HIF_ERROR_FAILED,
			     "cannot create index for %s", index_dir);
		goto out;
	}

	filename = g_build_filename (index_dir, key, NULL);

	/* check it exists */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     HIF_ERROR_FAILED,
			     "%s key not found",
			     filename);
		goto out;
	}

	/* get value */
	ret = g_file_get_contents (filename, &value, NULL, error);
	if (!ret)
		goto out;
out:
	g_free (index_dir);
	g_free (filename);
	return value;
}

/**
 * hif_db_set_string:
 **/
gboolean
hif_db_set_string (HifDb *db,
		   HyPackage package,
		   const gchar *key,
		   const gchar *value,
		   GError **error)
{
	gboolean ret = TRUE;
	gchar *index_dir = NULL;
	gchar *index_file = NULL;

	g_return_val_if_fail (HIF_IS_DB (db), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create the index directory */
	index_dir = hif_db_get_dir_for_package (package);
	if (index_dir == NULL) {
		ret = FALSE;
		index_dir = hif_package_get_id (package);
		g_set_error (error,
			     HIF_ERROR,
			     HIF_ERROR_FAILED,
			     "cannot create index for %s", index_dir);
		goto out;
	}
	ret = hif_db_create_dir (index_dir, error);
	if (!ret)
		goto out;

	/* write the value */
	index_file = g_build_filename (index_dir, key, NULL);
	g_debug ("writing %s to %s", value, index_file);
	ret = g_file_set_contents (index_file, value, -1, error);
	if (!ret)
		goto out;
out:
	g_free (index_dir);
	g_free (index_file);
	return ret;
}

/**
 * hif_db_remove:
 **/
gboolean
hif_db_remove (HifDb *db, HyPackage package,
	       const gchar *key, GError **error)
{
	gboolean ret = TRUE;
	gchar *index_dir = NULL;
	gchar *index_file = NULL;
	GFile *file = NULL;

	g_return_val_if_fail (HIF_IS_DB (db), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create the index directory */
	index_dir = hif_db_get_dir_for_package (package);
	if (index_dir == NULL) {
		ret = FALSE;
		index_dir = hif_package_get_id (package);
		g_set_error (error,
			     HIF_ERROR,
			     HIF_ERROR_FAILED,
			     "cannot create index for %s", index_dir);
		goto out;
	}

	/* delete the value */
	g_debug ("deleting %s from %s", key, index_dir);
	index_file = g_build_filename (index_dir, key, NULL);
	file = g_file_new_for_path (index_file);
	ret = g_file_delete (file, NULL, error);
	if (!ret)
		goto out;
out:
	if (file != NULL)
		g_object_unref (file);
	g_free (index_dir);
	return ret;
}

/**
 * hif_db_remove_all:
 **/
gboolean
hif_db_remove_all (HifDb *db, HyPackage package, GError **error)
{
	gboolean ret = TRUE;
	gchar *index_dir = NULL;
	gchar *index_file = NULL;
	GFile *file_tmp;
	GFile *file_directory = NULL;
	GDir *dir = NULL;
	const gchar *filename;

	g_return_val_if_fail (HIF_IS_DB (db), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get the folder */
	index_dir = hif_db_get_dir_for_package (package);
	if (index_dir == NULL) {
		ret = FALSE;
		index_dir = hif_package_get_id (package);
		g_set_error (error,
			     HIF_ERROR,
			     HIF_ERROR_FAILED,
			     "cannot create index for %s", index_dir);
		goto out;
	}
	ret = g_file_test (index_dir, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_debug ("Nothing to delete in %s", index_dir);
		ret = TRUE;
		goto out;
	}

	/* open */
	dir = g_dir_open (index_dir, 0, error);
	if (dir == NULL)
		goto out;

	/* delete each one */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		index_file = g_build_filename (index_dir, filename, NULL);
		file_tmp = g_file_new_for_path (index_file);

		/* delete, ignoring error */
		g_debug ("deleting %s from %s", filename, index_dir);
		ret = g_file_delete (file_tmp, NULL, NULL);
		if (!ret)
			g_debug ("failed to delete %s", filename);
		g_object_unref (file_tmp);
		g_free (index_file);
		filename = g_dir_read_name (dir);
	}

	/* now delete the directory */
	file_directory = g_file_new_for_path (index_dir);
	ret = g_file_delete (file_directory, NULL, error);
	if (!ret)
		goto out;
out:
	if (file_directory != NULL)
		g_object_unref (file_directory);
	g_free (index_dir);
	return ret;
}

/**
 * hif_db_finalize:
 **/
static void
hif_db_finalize (GObject *object)
{
	g_return_if_fail (HIF_IS_DB (object));
	G_OBJECT_CLASS (hif_db_parent_class)->finalize (object);
}

/**
 * hif_db_class_init:
 **/
static void
hif_db_class_init (HifDbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hif_db_finalize;
}

/**
 * hif_db_init:
 **/
static void
hif_db_init (HifDb *db)
{
}

/**
 * hif_db_new:
 **/
HifDb *
hif_db_new (void)
{
	HifDb *db;
	db = g_object_new (HIF_TYPE_DB, NULL);
	return HIF_DB (db);
}
