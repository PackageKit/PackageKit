/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-files
 * @short_description: Files object
 *
 * This GObject represents a files from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-files.h>

static void     pk_files_finalize	(GObject     *object);

#define PK_FILES_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_FILES, PkFilesPrivate))

/**
 * PkFilesPrivate:
 *
 * Private #PkFiles data
 **/
struct _PkFilesPrivate
{
	gchar				*package_id;
	gchar				**files;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_FILES,
	PROP_LAST
};

G_DEFINE_TYPE (PkFiles, pk_files, PK_TYPE_SOURCE)

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
	g_return_val_if_fail (PK_IS_FILES (files), NULL);
	return files->priv->package_id;
}

/**
 * pk_files_get_files:
 * @files: a valid #PkFiles instance
 *
 * Gets the file list
 *
 * Return value: (transfer none): Gets the file list for the files object
 *
 * Since: 0.9.1
 **/
gchar **
pk_files_get_files (PkFiles *files)
{
	g_return_val_if_fail (PK_IS_FILES (files), NULL);
	return files->priv->files;
}

/**
 * pk_files_get_property:
 **/
static void
pk_files_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkFiles *files = PK_FILES (object);
	PkFilesPrivate *priv = files->priv;

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

/**
 * pk_files_set_property:
 **/
static void
pk_files_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkFiles *files = PK_FILES (object);
	PkFilesPrivate *priv = files->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_FILES:
		g_strfreev (priv->files);
		priv->files = g_strdupv (g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
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
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkFiles:files:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boxed ("files", NULL, NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILES, pspec);

	g_type_class_add_private (klass, sizeof (PkFilesPrivate));
}

/**
 * pk_files_init:
 **/
static void
pk_files_init (PkFiles *files)
{
	files->priv = PK_FILES_GET_PRIVATE (files);
	files->priv->package_id = NULL;
	files->priv->files = NULL;
}

/**
 * pk_files_finalize:
 **/
static void
pk_files_finalize (GObject *object)
{
	PkFiles *files = PK_FILES (object);
	PkFilesPrivate *priv = files->priv;

	g_free (priv->package_id);
	g_strfreev (priv->files);

	G_OBJECT_CLASS (pk_files_parent_class)->finalize (object);
}

/**
 * pk_files_new:
 *
 * Return value: a new PkFiles object.
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

