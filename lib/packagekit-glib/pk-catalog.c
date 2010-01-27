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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>

#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-client.h>
#include <packagekit-glib/pk-package-list.h>
#include <packagekit-glib/pk-package-ids.h>
#include <packagekit-glib/pk-marshal.h>
#include <packagekit-glib/pk-catalog.h>

#include "egg-debug.h"
#include "egg-string.h"

static void     pk_catalog_finalize	(GObject	*object);

#define PK_CATALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CATALOG, PkCatalogPrivate))

struct PkCatalogPrivate
{
	GKeyFile		*file;
	gchar			*distro_id;
	const gchar		*type;
	PkClient		*client;
	PkPackageList		*list;
	gboolean		 is_cancelled;
};

enum {
	PK_CATALOG_PROGRESS,
	PK_CATALOG_LAST_SIGNAL
};

static guint signals [PK_CATALOG_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkCatalog, pk_catalog, G_TYPE_OBJECT)
#define PK_CATALOG_FILE_HEADER	"PackageKit Catalog"

/**
 * pk_catalog_process_type_part:
 **/
static gboolean
pk_catalog_process_type_part (PkCatalog *catalog, const gchar *type, GPtrArray *array, const gchar *distro_id_part)
{
	gchar *data;
	gchar **list;
	gchar *key;
	guint len;
	guint i;

	/* cancelled? */
	if (catalog->priv->is_cancelled) {
		egg_debug ("escaping as cancelled!");
		return FALSE;
	}

	/* make key */
	if (distro_id_part == NULL)
		key = g_strdup (type);
	else
		key = g_strdup_printf ("%s(%s)", type, distro_id_part);
	data = g_key_file_get_string (catalog->priv->file, PK_CATALOG_FILE_HEADER, key, NULL);
	g_free (key);

	/* we have no key of this name */
	if (data == NULL)
		return FALSE;

	/* split using the three delimiters */
	list = g_strsplit_set (data, ";, ", 0);

	/* add to array */
	len = g_strv_length (list);
	for (i=0; i<len; i++) {
		g_ptr_array_add (array, g_strdup (list[i]));
	}
	g_strfreev (list);
	g_free (data);
	return TRUE;
}

/**
 * pk_catalog_process_type:
 **/
static gboolean
pk_catalog_process_type (PkCatalog *catalog, const gchar *type)
{
	PkCatalogProgress mode = 0;
	PkPackageList *list;
	GPtrArray *array;
	GError *error = NULL;
	gchar **parts = NULL;
	gchar *distro_id_part;
	gchar **packages;
	const gchar *package;
	gboolean ret = TRUE;
	guint i;

	/* cancelled? */
	if (catalog->priv->is_cancelled) {
		egg_debug ("escaping as cancelled!");
		return FALSE;
	}

	parts = g_strsplit (catalog->priv->distro_id, "-", 0);
	array = g_ptr_array_new ();

	/* no specifier */
	pk_catalog_process_type_part (catalog, type, array, NULL);

	/* distro */
	pk_catalog_process_type_part (catalog, type, array, parts[0]);

	/* distro-ver */
	distro_id_part = g_strjoin ("-", parts[0], parts[1], NULL);
	pk_catalog_process_type_part (catalog, type, array, distro_id_part);
	g_free (distro_id_part);

	/* distro-ver-arch */
	pk_catalog_process_type_part (catalog, type, array, catalog->priv->distro_id);

	/* find mode */
	if (g_strcmp0 (type, "InstallPackages") == 0) {
		mode = PK_CATALOG_PROGRESS_PACKAGES;
	} else if (g_strcmp0 (type, "InstallFiles") == 0) {
		mode = PK_CATALOG_PROGRESS_FILES;
	} else if (g_strcmp0 (type, "InstallProvides") == 0) {
		mode = PK_CATALOG_PROGRESS_PROVIDES;
	}

	/* do each entry */
	for (i=0; i<array->len; i++) {
		if (catalog->priv->is_cancelled) {
			egg_debug ("escaping as cancelled!");
			break;
		}

		/* reset */
		ret = pk_client_reset (catalog->priv->client, &error);
		if (!ret) {
			egg_warning ("reset failed: %s", error->message);
			g_error_free (error);
			break;
		}

		/* get data */
		package = (const gchar *) g_ptr_array_index (array, i);

		/* tell the client what we are doing */
		g_signal_emit (catalog, signals [PK_CATALOG_PROGRESS], 0, mode, package);

		/* do the actions */
		if (mode == PK_CATALOG_PROGRESS_PACKAGES) {
			packages = pk_package_ids_from_id (package);
			ret = pk_client_resolve (catalog->priv->client,
						 pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_NEWEST, -1),
						 packages, &error);
			g_strfreev (packages);
		} else if (mode == PK_CATALOG_PROGRESS_FILES) {
			ret = pk_client_search_file (catalog->priv->client,
						     pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_NEWEST, -1),
						     package, &error);
		} else if (mode == PK_CATALOG_PROGRESS_PROVIDES) {
			ret = pk_client_what_provides (catalog->priv->client,
						      pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, PK_FILTER_ENUM_NEWEST, -1),
						      PK_PROVIDES_ENUM_ANY, package, &error);
		}
		if (!ret) {
			egg_warning ("method failed: %s", error->message);
			g_error_free (error);
			break;
		}

		/* add to list any results */
		list = pk_client_get_package_list (catalog->priv->client);
		pk_obj_list_add_list (PK_OBJ_LIST(catalog->priv->list), PK_OBJ_LIST(list));
		g_object_unref (list);
	}

	g_strfreev (parts);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	return ret;
}

/**
 * pk_catalog_process_file:
 **/
static gboolean
pk_catalog_process_file (PkCatalog *catalog, const gchar *filename)
{
	gboolean ret;
	GError *error = NULL;

	/* cancelled? */
	if (catalog->priv->is_cancelled) {
		egg_debug ("escaping as cancelled!");
		return FALSE;
	}

	/* load all data */
	ret = g_key_file_load_from_file (catalog->priv->file, filename, G_KEY_FILE_NONE, &error);
	if (!ret) {
		egg_warning ("cannot open file %s, %s", filename, error->message);
		g_error_free (error);
		return FALSE;
	}

	/* InstallPackages */
	pk_catalog_process_type (catalog, "InstallPackages");

	/* InstallFiles */
	pk_catalog_process_type (catalog, "InstallFiles");

	/* InstallProvides */
	pk_catalog_process_type (catalog, "InstallProvides");

	return TRUE;
}

/**
 * pk_catalog_cancel:
 **/
gboolean
pk_catalog_cancel (PkCatalog *catalog)
{
	gboolean ret;
	GError *error = NULL;

	if (catalog->priv->is_cancelled) {
		egg_warning ("already cancelled");
		return FALSE;
	}
	catalog->priv->is_cancelled = TRUE;

	/* cancel whatever is in progress */
	ret = pk_client_cancel (catalog->priv->client, &error);
	if (!ret) {
		egg_warning ("cancel failed: %s", error->message);
		g_error_free (error);
	}
	return TRUE;
}

/**
 * pk_catalog_process_files:
 **/
PkPackageList *
pk_catalog_process_files (PkCatalog *catalog, gchar **filenames)
{
	guint len;
	guint i;

	/* process each file */
	len = g_strv_length (filenames);
	for (i=0; i<len; i++) {
		if (catalog->priv->is_cancelled) {
			egg_debug ("escaping as cancelled!");
			break;
		}
		egg_debug ("filenames[%i]=%s", i, filenames[i]);
		pk_catalog_process_file (catalog, filenames[i]);
	}

	g_object_ref (catalog->priv->list);
	return catalog->priv->list;
}

/**
 * pk_catalog_class_init:
 * @klass: The PkCatalogClass
 **/
static void
pk_catalog_class_init (PkCatalogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_catalog_finalize;
	g_type_class_add_private (klass, sizeof (PkCatalogPrivate));
	signals [PK_CATALOG_PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
}

/**
 * pk_catalog_init:
 * @catalog: This class instance
 **/
static void
pk_catalog_init (PkCatalog *catalog)
{
	catalog->priv = PK_CATALOG_GET_PRIVATE (catalog);
	catalog->priv->is_cancelled = FALSE;
	catalog->priv->file = g_key_file_new ();
	catalog->priv->list = pk_package_list_new ();

	/* name-version-arch */
	catalog->priv->distro_id = pk_get_distro_id ();
	if (catalog->priv->distro_id == NULL) {
		egg_error ("no distro_id, your distro needs to implement this in pk-common.c!");
	}

	catalog->priv->client = pk_client_new ();
	pk_client_set_use_buffer (catalog->priv->client, TRUE, NULL);
	pk_client_set_synchronous (catalog->priv->client, TRUE, NULL);
}

/**
 * pk_catalog_finalize:
 * @object: The object to finalize
 **/
static void
pk_catalog_finalize (GObject *object)
{
	PkCatalog *catalog;

	g_return_if_fail (PK_IS_CATALOG (object));

	catalog = PK_CATALOG (object);

	g_return_if_fail (catalog->priv != NULL);
	g_key_file_free (catalog->priv->file);
	g_free (catalog->priv->distro_id);
	g_object_unref (catalog->priv->list);
	g_object_unref (catalog->priv->client);

	G_OBJECT_CLASS (pk_catalog_parent_class)->finalize (object);
}

/**
 * pk_catalog_new:
 *
 * Return value: a new PkCatalog object.
 **/
PkCatalog *
pk_catalog_new (void)
{
	PkCatalog *catalog;
	catalog = g_object_new (PK_TYPE_CATALOG, NULL);
	return PK_CATALOG (catalog);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include <packagekit-glib/pk-connection.h>

void
pk_catalog_test (EggTest *test)
{
	PkConnection *connection;
	PkCatalog *catalog;
	PkPackageList *list;
	gboolean ret;
	gchar **filenames;
	gchar *path;
	guint size;

	if (!egg_test_start (test, "PkCatalog"))
		return;

	/* check to see if there is a daemon running */
	connection = pk_connection_new ();
	ret = pk_connection_valid (connection);
	g_object_unref (connection);
	if (!ret) {
		egg_warning ("daemon is not running, skipping tests");
		goto out;
	}

	/************************************************************/
	egg_test_title (test, "get catalog");
	catalog = pk_catalog_new ();
	egg_test_assert (test, catalog != NULL);

	/************************************************************/
	egg_test_title (test, "get test file");
	path = egg_test_get_data_file ("test.catalog");
	egg_test_assert (test, path != NULL);

	/************************************************************/
	egg_test_title (test, "process the files getting non-null");
	filenames = g_strsplit (path, " ", 0);
	list = pk_catalog_process_files (catalog, filenames);
	g_strfreev (filenames);
	if (list != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	g_free (path);

	/************************************************************/
	egg_test_title (test, "have we got packages?");
	size = pk_package_list_get_size (list);
	if (size > 0)
		egg_test_success (test, "%i packages", size);
	else
		egg_test_failed (test, NULL);
	g_object_unref (list);
	g_object_unref (catalog);
out:
	egg_test_end (test);
}
#endif

