/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * PkFiles:
 *
 * Files object
 *
 * This GObject represents a files from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>
#include <glib/gstdio.h>
#include <sys/stat.h>

#include "pk-files.h"

static void pk_files_finalize (GObject *object);

/**
 * PkFile:
 *
 * One entry of a #PkFiles list: an absolute path and what it is.
 *
 * Since: 2.0.0
 */

struct _PkFile
{
	gchar *path;
	PkFileKindEnum kind;
};

G_DEFINE_BOXED_TYPE (PkFile, pk_file, pk_file_copy, pk_file_free)

/**
 * pk_file_new:
 * @path: the absolute path
 * @kind: what the entry is
 *
 * Returns: (transfer full): a new #PkFile
 *
 * Since: 2.0.0
 */
PkFile *
pk_file_new (const gchar *path, PkFileKindEnum kind)
{
	PkFile *file;

	g_return_val_if_fail (path != NULL, NULL);

	file = g_new0 (PkFile, 1);
	file->path = g_strdup (path);
	file->kind = kind;
	return file;
}

/**
 * pk_file_new_from_disk:
 * @path: the absolute path of an entry that is on the filesystem
 *
 * Creates an entry for a file of an installed package, taking its kind from
 * the filesystem, as package metadata often does not record it. The kind is
 * unknown if @path does not exist.
 *
 * Returns: (transfer full): a new #PkFile
 *
 * Since: 2.0.0
 */
PkFile *
pk_file_new_from_disk (const gchar *path)
{
	PkFileKindEnum kind = PK_FILE_KIND_ENUM_UNKNOWN;
	GStatBuf st;

	g_return_val_if_fail (path != NULL, NULL);

	if (g_lstat (path, &st) == 0) {
		if (S_ISDIR (st.st_mode))
			kind = PK_FILE_KIND_ENUM_DIRECTORY;
		else if (S_ISLNK (st.st_mode))
			kind = PK_FILE_KIND_ENUM_SYMLINK;
		else if (S_ISREG (st.st_mode))
			kind = PK_FILE_KIND_ENUM_REGULAR;
	}
	return pk_file_new (path, kind);
}

/**
 * pk_file_copy:
 * @file: a #PkFile
 *
 * Returns: (transfer full): a copy of @file
 *
 * Since: 2.0.0
 */
PkFile *
pk_file_copy (PkFile *file)
{
	g_return_val_if_fail (file != NULL, NULL);
	return pk_file_new (file->path, file->kind);
}

/**
 * pk_file_free:
 * @file: a #PkFile
 *
 * Since: 2.0.0
 */
void
pk_file_free (PkFile *file)
{
	if (file == NULL)
		return;
	g_free (file->path);
	g_free (file);
}

/**
 * pk_file_get_path:
 * @file: a #PkFile
 *
 * Returns: the absolute path, without a trailing slash
 *
 * Since: 2.0.0
 */
const gchar *
pk_file_get_path (PkFile *file)
{
	g_return_val_if_fail (file != NULL, NULL);
	return file->path;
}

/**
 * pk_file_get_kind:
 * @file: a #PkFile
 *
 * Returns: what the entry is
 *
 * Since: 2.0.0
 */
PkFileKindEnum
pk_file_get_kind (PkFile *file)
{
	g_return_val_if_fail (file != NULL, PK_FILE_KIND_ENUM_UNKNOWN);
	return file->kind;
}

/**
 * PkFilesPrivate:
 *
 * Private #PkFiles data
 **/
struct _PkFilesPrivate
{
	gchar *package_id;
	GPtrArray *files;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_FILES,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (PkFiles, pk_files, PK_TYPE_SOURCE)
#define GET_PRIVATE(o) (pk_files_get_instance_private (o))

/**
 * pk_files_get_package_id:
 * @files: a valid #PkFiles instance
 *
 * Gets the package-id
 *
 * Return value: Gets the package_id for the files object
 *
 * Since: 0.9.1
 **/
const gchar *
pk_files_get_package_id (PkFiles *files)
{
	PkFilesPrivate *priv = GET_PRIVATE(files);

	g_return_val_if_fail (PK_IS_FILES (files), NULL);

	return priv->package_id;
}

/**
 * pk_files_get_files:
 * @files: a valid #PkFiles instance
 *
 * Gets the file list
 *
 * Return value: (transfer none) (element-type PkFile): the entries of the file list
 *
 * Since: 0.9.1
 **/
GPtrArray *
pk_files_get_files (PkFiles *files)
{
	PkFilesPrivate *priv = GET_PRIVATE(files);

	g_return_val_if_fail (PK_IS_FILES (files), NULL);

	return priv->files;
}

/*
 * pk_files_get_property:
 **/
static void
pk_files_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkFiles *files = PK_FILES (object);
	PkFilesPrivate *priv = GET_PRIVATE(files);

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_FILES:
		g_value_set_boxed (value, priv->files);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_files_set_property:
 **/
static void
pk_files_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkFiles *files = PK_FILES (object);
	PkFilesPrivate *priv = GET_PRIVATE(files);

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_value_dup_string (value);
		break;
	case PROP_FILES:
		g_clear_pointer (&priv->files, g_ptr_array_unref);
		priv->files = g_value_dup_boxed (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_files_class_init:
 **/
static void
pk_files_class_init (PkFilesClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_files_finalize;
	object_class->get_property = pk_files_get_property;
	object_class->set_property = pk_files_set_property;

	/**
	 * PkFiles:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkFiles:files:
	 *
	 * A #GPtrArray of #PkFile. The array is referenced, not copied, so it
	 * must own its elements via pk_file_free().
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boxed ("files",
				    NULL,
				    NULL,
				    G_TYPE_PTR_ARRAY,
				    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_FILES, pspec);
}

/*
 * pk_files_init:
 **/
static void
pk_files_init (PkFiles *files)
{}

/*
 * pk_files_finalize:
 **/
static void
pk_files_finalize (GObject *object)
{
	PkFiles *files = PK_FILES (object);
	PkFilesPrivate *priv = GET_PRIVATE(files);

	g_clear_pointer (&priv->package_id, g_free);
	g_clear_pointer (&priv->files, g_ptr_array_unref);

	G_OBJECT_CLASS (pk_files_parent_class)->finalize (object);
}

/**
 * pk_files_new:
 *
 * Return value: a new #PkFiles object.
 *
 * Since: 0.5.4
 **/
PkFiles *
pk_files_new (void)
{
	PkFiles *files;
	files = g_object_new (PK_TYPE_FILES, NULL);
	return PK_FILES (files);
}
