/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
 * SECTION:pk-catalog
 * @short_description: Functionality for installing catalogs
 *
 * Clients can use this GObject for installing catalog files.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-catalog.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>
#include <packagekit-glib2/pk-control-sync.h>

#define PK_CATALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CATALOG, PkCatalogPrivate))

typedef enum {
	PK_CATALOG_MODE_PACKAGES,
	PK_CATALOG_MODE_FILES,
	PK_CATALOG_MODE_PROVIDES,
	PK_CATALOG_MODE_UNKNOWN
} PkCatalogMode;

/**
 * PkCatalogState:
 *
 * For use in the async methods
 **/
typedef struct {
	GKeyFile			*file;
	gpointer			 progress_user_data;
	GCancellable			*cancellable;
	GSimpleAsyncResult		*res;
	PkProgressCallback		 progress_callback;
	PkCatalog			*catalog;
	GPtrArray			*array_packages;
	GPtrArray			*array_files;
	GPtrArray			*array_provides;
	GPtrArray			*array;
} PkCatalogState;

/**
 * PkCatalogPrivate:
 *
 * Private #PkCatalog data
 **/
struct _PkCatalogPrivate
{
	gchar			*distro_id;
	PkClient		*client;
};

G_DEFINE_TYPE (PkCatalog, pk_catalog, G_TYPE_OBJECT)

/**
 * pk_catalog_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.5.3
 **/
GQuark
pk_catalog_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_catalog_error");
	return quark;
}

/**
 * pk_catalog_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_catalog_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_CATALOG_ERROR_FAILED, "Failed"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkCatalogError", values);
	}
	return etype;
}

/**
 * pk_catalog_mode_to_string:
 **/
static const gchar *
pk_catalog_mode_to_string (PkCatalogMode mode)
{
	if (mode == PK_CATALOG_MODE_PACKAGES)
		return "InstallPackages";
	if (mode == PK_CATALOG_MODE_FILES)
		return "InstallFiles";
	if (mode == PK_CATALOG_MODE_PROVIDES)
		return "InstallProvides";
	return NULL;
}

/**
 * pk_catalog_process_type_part:
 **/
static void
pk_catalog_process_type_part (PkCatalogState *state, PkCatalogMode mode, const gchar *distro_id_part)
{
	gchar *data = NULL;
	gchar **list = NULL;
	gchar *key = NULL;
	guint i;
	const gchar *type;
	GPtrArray *array = NULL;

	/* make key */
	type = pk_catalog_mode_to_string (mode);
	if (distro_id_part == NULL)
		key = g_strdup (type);
	else
		key = g_strdup_printf ("%s(%s)", type, distro_id_part);
	data = g_key_file_get_string (state->file, PK_CATALOG_FILE_HEADER, key, NULL);

	/* we have no key of this name */
	if (data == NULL)
		goto out;

	/* get array */
	if (mode == PK_CATALOG_MODE_PACKAGES)
		array = state->array_packages;
	if (mode == PK_CATALOG_MODE_FILES)
		array = state->array_files;
	if (mode == PK_CATALOG_MODE_PROVIDES)
		array = state->array_provides;

	/* split using any of the delimiters and add to correct array*/
	list = g_strsplit_set (data, ";, ", 0);
	for (i=0; list[i] != NULL; i++)
		g_ptr_array_add (array, g_strdup (list[i]));
out:
	g_strfreev (list);
	g_free (key);
	g_free (data);
}

/**
 * pk_catalog_process_type:
 **/
static void
pk_catalog_process_type (PkCatalogState *state, PkCatalogMode mode)
{
	gchar **parts;
	gchar *distro_id_part;

	/* split distro id */
	parts = g_strsplit (state->catalog->priv->distro_id, ";", 0);

	/* no specifier */
	pk_catalog_process_type_part (state, mode, NULL);

	/* distro */
	pk_catalog_process_type_part (state, mode, parts[0]);

	/* distro-ver */
	distro_id_part = g_strjoin (";", parts[0], parts[1], NULL);
	pk_catalog_process_type_part (state, mode, distro_id_part);
	g_free (distro_id_part);

	/* distro-ver-arch */
	pk_catalog_process_type_part (state, mode, state->catalog->priv->distro_id);

	g_strfreev (parts);
}

/**
 * pk_catalog_lookup_state_finish:
 **/
static void
pk_catalog_lookup_state_finish (PkCatalogState *state, const GError *error)
{
	/* get result */
	if (error == NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_ptr_array_ref (state->array), (GDestroyNotify) g_ptr_array_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);
	g_ptr_array_unref (state->array_packages);
	g_ptr_array_unref (state->array_files);
	g_ptr_array_unref (state->array_provides);
	g_key_file_free (state->file);
	g_object_unref (state->res);
	g_object_unref (state->catalog);
	g_ptr_array_unref (state->array);
	g_slice_free (PkCatalogState, state);
}

/**
 * pk_catalog_what_provides_ready_cb:
 **/
static void
pk_catalog_what_provides_ready_cb (GObject *source_object, GAsyncResult *res, PkCatalogState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *array = NULL;
	guint i;
	PkPackage *package;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error = g_error_new (1, 0, "failed to search file: %s", pk_error_get_details (error_code));
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* add all the results to the existing list */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("adding %s", pk_package_get_id (package));
		g_ptr_array_add (state->array, g_object_ref (package));
	}

	/* there's nothing left to do */
	pk_catalog_lookup_state_finish (state, NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * pk_catalog_do_what_provides:
 **/
static void
pk_catalog_do_what_provides (PkCatalogState *state)
{
	gchar **data;
	gchar *dbg;
	data = pk_ptr_array_to_strv (state->array_files);
	dbg = g_strjoinv ("&", data);
	g_debug ("searching for %s", dbg);
	pk_client_what_provides_async (state->catalog->priv->client, pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1),
				       PK_PROVIDES_ENUM_ANY, data,
				       state->cancellable, state->progress_callback, state->progress_user_data,
				       (GAsyncReadyCallback) pk_catalog_what_provides_ready_cb, state);
	g_strfreev (data);
	g_free (dbg);
}

/**
 * pk_catalog_search_file_ready_cb:
 **/
static void
pk_catalog_search_file_ready_cb (GObject *source_object, GAsyncResult *res, PkCatalogState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *array = NULL;
	guint i;
	PkPackage *package;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error = g_error_new (1, 0, "failed to search file: %s", pk_error_get_details (error_code));
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* add all the results to the existing list */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("adding %s", pk_package_get_id (package));
		g_ptr_array_add (state->array, g_object_ref (package));
	}

	/* what-provides */
	if (state->array_provides->len > 0) {
		pk_catalog_do_what_provides (state);
		goto out;
	}

	/* just exit without any error as there's nothing to do */
	pk_catalog_lookup_state_finish (state, NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * pk_catalog_do_search_files:
 **/
static void
pk_catalog_do_search_files (PkCatalogState *state)
{
	gchar **data;
	gchar *dbg;
	data = pk_ptr_array_to_strv (state->array_files);
	dbg = g_strjoinv ("&", data);
	g_debug ("searching for %s", dbg);
	pk_client_search_files_async (state->catalog->priv->client, pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH, PK_FILTER_ENUM_NEWEST, -1),
				     data,
				     state->cancellable, state->progress_callback, state->progress_user_data,
				     (GAsyncReadyCallback) pk_catalog_search_file_ready_cb, state);
	g_strfreev (data);
	g_free (dbg);
}

/**
 * pk_catalog_resolve_ready_cb:
 **/
static void
pk_catalog_resolve_ready_cb (GObject *source_object, GAsyncResult *res, PkCatalogState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *array = NULL;
	guint i;
	PkPackage *package;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		error = g_error_new (1, 0, "failed to resolve: %s", pk_error_get_details (error_code));
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* add all the results to the existing list */
	array = pk_results_get_package_array (results);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("adding %s", pk_package_get_id (package));
		g_ptr_array_add (state->array, g_object_ref (package));
	}

	/* search-file then what-provides */
	if (state->array_files->len > 0) {
		pk_catalog_do_search_files (state);
		goto out;
	}
	if (state->array_provides->len > 0) {
		pk_catalog_do_what_provides (state);
		goto out;
	}

	/* just exit without any error as there's nothing to do */
	pk_catalog_lookup_state_finish (state, NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

/**
 * pk_catalog_do_resolve:
 **/
static void
pk_catalog_do_resolve (PkCatalogState *state)
{
	gchar **data;
	gchar *dbg;
	data = pk_ptr_array_to_strv (state->array_packages);
	dbg = g_strjoinv ("&", data);
	g_debug ("searching for %s", dbg);
	pk_client_resolve_async (state->catalog->priv->client,
				 pk_bitfield_from_enums (PK_FILTER_ENUM_ARCH,
							 PK_FILTER_ENUM_NOT_INSTALLED,
							 PK_FILTER_ENUM_NEWEST, -1), data,
				 state->cancellable, state->progress_callback, state->progress_user_data,
				 (GAsyncReadyCallback) pk_catalog_resolve_ready_cb, state);
	g_free (dbg);
	g_strfreev (data);
}

/**
 * pk_catalog_lookup_async:
 * @catalog: a valid #PkCatalog instance
 * @filename: the filename of the catalog to install
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @user_data: the data to pass to @callback
 *
 * Simulate the install of a catalog file.
 *
 * Since: 0.5.3
 **/
void
pk_catalog_lookup_async (PkCatalog *catalog, const gchar *filename, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data,
			 GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkCatalogState *state;
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CATALOG (catalog));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (catalog), callback, user_data, pk_catalog_lookup_async);

	/* save state */
	state = g_slice_new0 (PkCatalogState);
	state->res = g_object_ref (res);
	state->catalog = g_object_ref (catalog);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->file = g_key_file_new ();
	state->array_packages = g_ptr_array_new_with_free_func (g_free);
	state->array_files = g_ptr_array_new_with_free_func (g_free);
	state->array_provides = g_ptr_array_new_with_free_func (g_free);;
	state->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;

	/* load all data */
	g_debug ("loading from %s", filename);
	ret = g_key_file_load_from_file (state->file, filename, G_KEY_FILE_NONE, &error);
	if (!ret) {
		pk_catalog_lookup_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* parse InstallPackages */
	g_debug ("processing InstallPackages");
	pk_catalog_process_type (state, PK_CATALOG_MODE_PACKAGES);

	/* parse InstallFiles */
	g_debug ("processing InstallFiles");
	pk_catalog_process_type (state, PK_CATALOG_MODE_FILES);

	/* parse InstallProvides */
	g_debug ("processing InstallProvides");
	pk_catalog_process_type (state, PK_CATALOG_MODE_PROVIDES);

	/* resolve, search-file then what-provides */
	if (state->array_packages->len > 0) {
		pk_catalog_do_resolve (state);
		goto out;
	} else if (state->array_files->len > 0) {
		pk_catalog_do_search_files (state);
		goto out;
	} else if (state->array_provides->len > 0) {
		pk_catalog_do_what_provides (state);
		goto out;
	}

	/* just exit without any error as there's nothing to do */
	pk_catalog_lookup_state_finish (state, NULL);
out:
	g_object_unref (res);
}

/**
 * pk_catalog_lookup_finish:
 * @catalog: a valid #PkCatalog instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: (transfer full): the #GPtrArray of #PkPackage's, or %NULL. Free with g_ptr_array_unref()
 *
 * Since: 0.5.3
 **/
GPtrArray *
pk_catalog_lookup_finish (PkCatalog *catalog, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_CATALOG (catalog), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_ptr_array_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

/**
 * pk_catalog_finalize:
 **/
static void
pk_catalog_finalize (GObject *object)
{
	PkCatalog *catalog;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CATALOG (object));
	catalog = PK_CATALOG (object);

	g_object_unref (catalog->priv->client);
	g_free (catalog->priv->distro_id);

	G_OBJECT_CLASS (pk_catalog_parent_class)->finalize (object);
}

/**
 * pk_catalog_class_init:
 **/
static void
pk_catalog_class_init (PkCatalogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_catalog_finalize;

	g_type_class_add_private (klass, sizeof (PkCatalogPrivate));
}

/**
 * pk_catalog_init:
 **/
static void
pk_catalog_init (PkCatalog *catalog)
{
	PkControl *control;
	gboolean ret;
	GError *error = NULL;

	catalog->priv = PK_CATALOG_GET_PRIVATE (catalog);
	catalog->priv->client = pk_client_new ();
	control = pk_control_new ();
	ret = pk_control_get_properties (control, NULL, &error);
	if (!ret) {
		g_warning ("Failed to contact PackageKit: %s", error->message);
		g_error_free (error);
		return;
	}

	/* get data */
	g_object_get (control,
		      "distro-id", &catalog->priv->distro_id,
		      NULL);

	if (catalog->priv->distro_id == NULL)
		g_warning ("no distro_id, your distro needs to implement this in pk-engine.c!");
}

/**
 * pk_catalog_new:
 *
 * Return value: A new catalog class instance.
 *
 * Since: 0.5.3
 **/
PkCatalog *
pk_catalog_new (void)
{
	PkCatalog *catalog;
	catalog = g_object_new (PK_TYPE_CATALOG, NULL);
	return PK_CATALOG (catalog);
}
