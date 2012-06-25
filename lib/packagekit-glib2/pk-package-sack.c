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
 * SECTION:pk-package-sack
 * @short_description: A sack of packages that can be manipulated
 *
 * A package sack is a set of packages that can have operations done on them
 * in parallel. This might be adding summary text for bare package ID's, or
 * to add package or update details.
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-package-sack.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-package-id.h>

static void     pk_package_sack_finalize	(GObject     *object);

#define PK_PACKAGE_SACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE_SACK, PkPackageSackPrivate))

/**
 * PkPackageSackPrivate:
 *
 * Private #PkPackageSack data
 **/
struct _PkPackageSackPrivate
{
	GHashTable		*table;
	GPtrArray		*array;
	PkClient		*client;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

G_DEFINE_TYPE (PkPackageSack, pk_package_sack, G_TYPE_OBJECT)

/**
 * pk_package_sack_clear:
 * @sack: a valid #PkPackageSack instance
 *
 * Empty all the packages from the sack
 *
 * Since: 0.5.2
 **/
void
pk_package_sack_clear (PkPackageSack *sack)
{
	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));

	g_ptr_array_set_size (sack->priv->array, 0);
	g_hash_table_remove_all (sack->priv->table);
}

/**
 * pk_package_sack_get_size:
 * @sack: a valid #PkPackageSack instance
 *
 * Gets the number of packages in the sack
 *
 * Return value: the number of packages in the sack
 *
 * Since: 0.5.2
 **/
guint
pk_package_sack_get_size (PkPackageSack *sack)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), 0);

	return sack->priv->array->len;
}

/**
 * pk_package_sack_get_ids:
 * @sack: a valid #PkPackageSack instance
 *
 * Returns all the Package IDs in the sack
 *
 * Return value: (transfer full): the number of packages in the sack, free with g_strfreev()
 *
 * Since: 0.5.3
 **/
gchar **
pk_package_sack_get_ids (PkPackageSack *sack)
{
	gchar **package_ids;
	GPtrArray *array;
	guint i;
	PkPackage *package;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), NULL);

	array = sack->priv->array;
	package_ids = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_ids[i] = g_strdup (pk_package_get_id (package));
	}
	return package_ids;
}

/**
 * pk_package_sack_get_array:
 * @sack: a valid #PkPackageSack instance
 *
 * Gets the package array from the sack
 *
 * Return value: (element-type PkPackage) (transfer container): A #GPtrArray, free with g_ptr_array_unref().
 *
 * Since: 0.6.1
 **/
GPtrArray *
pk_package_sack_get_array (PkPackageSack *sack)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), NULL);
	return g_ptr_array_ref (sack->priv->array);
}

/**
 * pk_package_sack_filter_by_info:
 * @sack: a valid #PkPackageSack instance
 * @info: a %PkInfoEnum value to match
 *
 * Returns a new package sack which only matches packages that match the
 * specified info enum value.
 *
 * Return value: (transfer full): a new #PkPackageSack, free with g_object_unref()
 *
 * Since: 0.6.2
 **/
PkPackageSack *
pk_package_sack_filter_by_info (PkPackageSack *sack, PkInfoEnum info)
{
	PkPackageSack *results;
	PkPackage *package;
	PkInfoEnum info_tmp;
	guint i;
	PkPackageSackPrivate *priv = sack->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), NULL);

	/* create new sack */
	results = pk_package_sack_new ();

	/* add each that matches the info enum */
	for (i = 0; i < priv->array->len; i++) {
		package = g_ptr_array_index (priv->array, i);
		info_tmp = pk_package_get_info (package);
		if (info_tmp == info)
			pk_package_sack_add_package (results, package);
	}

	return results;
}

/**
 * pk_package_sack_filter:
 * @sack: a valid #PkPackageSack instance
 * @filter_cb: (scope call): a #PkPackageSackFilterFunc, which returns %TRUE for the #PkPackage's to add
 * @user_data: user data to pass to @filter_cb
 *
 * Returns a new package sack which only matches packages that return %TRUE
 * from the filter function.
 *
 * Return value: (transfer full): a new #PkPackageSack, free with g_object_unref()
 *
 * Since: 0.6.3
 **/
PkPackageSack *
pk_package_sack_filter (PkPackageSack *sack, PkPackageSackFilterFunc filter_cb, gpointer user_data)
{
	PkPackageSack *results;
	PkPackage *package;
	guint i;
	PkPackageSackPrivate *priv = sack->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), NULL);
	g_return_val_if_fail (filter_cb != NULL, NULL);

	/* create new sack */
	results = pk_package_sack_new ();

	/* add each that matches the info enum */
	for (i = 0; i < priv->array->len; i++) {
		package = g_ptr_array_index (priv->array, i);
		if (filter_cb (package, user_data))
			pk_package_sack_add_package (results, package);
	}
	return results;
}

/**
 * pk_package_sack_add_package:
 * @sack: a valid #PkPackageSack instance
 * @package: a valid #PkPackage instance
 *
 * Adds a package to the sack.
 *
 * Return value: %TRUE if the package was added to the sack
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_sack_add_package (PkPackageSack *sack, PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);

	/* add to array */
	g_ptr_array_add (sack->priv->array, g_object_ref (package));
	g_hash_table_insert (sack->priv->table, g_strdup (pk_package_get_id (package)), g_object_ref (package));

	return TRUE;
}

/**
 * pk_package_sack_add_package_by_id:
 * @sack: a valid #PkPackageSack instance
 * @package_id: a package_id descriptor
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Adds a package reference to the sack.
 *
 * Return value: %TRUE if the package was added to the sack
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_sack_add_package_by_id (PkPackageSack *sack, const gchar *package_id, GError **error)
{
	PkPackage *package;
	gboolean ret;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create new object */
	package = pk_package_new ();
	ret = pk_package_set_id (package, package_id, error);
	if (!ret)
		goto out;

	/* add to array, array will own object */
	pk_package_sack_add_package (sack, package);
out:
	g_object_unref (package);
	return ret;
}

/**
 * pk_package_sack_add_packages_from_line:
 **/
static void
pk_package_sack_add_packages_from_line (PkPackageSack *sack, const gchar *package_str, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;
	PkPackage *package;
	gchar **pdata;
	PkInfoEnum info;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));

	package = pk_package_new ();

	pdata = g_strsplit (package_str, "\t", -1);
	if (g_strv_length (pdata) != 3) {
		g_set_error (error, 1, 0, "invalid package-info line: %s", package_str);
		goto out;
	}

	info = pk_info_enum_from_string (pdata[0]);
	g_object_set (package,
		      "info", info,
		      "summary", pdata[2],
		      NULL);
	ret = pk_package_set_id (package, pdata[1], &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "invalid package-id in package-info line: %s", pdata[1]);
		goto out;
	}
	ret = pk_package_sack_add_package (sack, package);
	if (!ret)
		g_set_error (error, 1, 0, "could not add package '%s' to package-sack!", pdata[1]);

out:
	g_strfreev (pdata);
	g_object_unref (package);
}

/**
 * pk_package_sack_add_packages_from_file
 * @sack: a valid #PkPackageSack instance
 * @file: a valid package-list file
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Adds packages from package-list file to a PkPackageSack.
 *
 * Return value: %TRUE if there were no errors.
 *
 **/
gboolean
pk_package_sack_add_packages_from_file (PkPackageSack *sack, GFile *file, GError **error)
{
	GError *error_local = NULL;
	gboolean ret = TRUE;
	GFileInputStream *is;
	GDataInputStream *input;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);

	is = g_file_read (file, NULL, &error_local);

	if (is == NULL) {
		g_propagate_error (error, error_local);
		return FALSE;
	}

	input = g_data_input_stream_new (G_INPUT_STREAM (is));

	/* read package info file line by line */
	while (TRUE) {
		gchar *line;

		line = g_data_input_stream_read_line (input, NULL, NULL, &error_local);

		if (line == NULL)
			break;
		g_strstrip (line);

		pk_package_sack_add_packages_from_line (sack, line, &error_local);
		if (error_local != NULL) {
			g_propagate_error (error, error_local);
			ret = FALSE;
			break;
		}
	}

	g_object_unref (input);
	g_object_unref (is);

	return ret;
}

/**
 * pk_package_sack_remove_package:
 * @sack: a valid #PkPackageSack instance
 * @package: a valid #PkPackage instance
 *
 * Removes a package reference from the sack. The pointers have to match exactly.
 *
 * Return value: %TRUE if the package was removed from the sack
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_sack_remove_package (PkPackageSack *sack, PkPackage *package)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);

	/* remove from array */
	ret = g_ptr_array_remove (sack->priv->array, package);

	return ret;
}

/**
 * pk_package_sack_remove_package_by_id:
 * @sack: a valid #PkPackageSack instance
 * @package_id: a package_id descriptor
 *
 * Removes a package reference from the sack. As soon as one package is removed
 * the search is stopped.
 *
 * Return value: %TRUE if the package was removed from the sack
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_sack_remove_package_by_id (PkPackageSack *sack, const gchar *package_id)
{
	PkPackage *package;
	const gchar *id;
	gboolean ret = FALSE;
	guint i;
	GPtrArray *array;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	array = sack->priv->array;
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		id = pk_package_get_id (package);
		if (g_strcmp0 (package_id, id) == 0) {
			g_ptr_array_remove_index (array, i);
			ret = TRUE;
			break;
		}
	}

	return ret;
}

/**
 * pk_package_sack_remove_by_filter:
 * @sack: a valid #PkPackageSack instance
 * @filter_cb: (scope call): a #PkPackageSackFilterFunc, which returns %TRUE for the #PkPackage's to retain
 * @user_data: user data to pass to @filter_cb
 *
 * Removes from the package sack any packages that return %FALSE from the filter
 * function.
 *
 * Return value: %TRUE if a package was removed from the sack
 *
 * Since: 0.6.3
 **/
gboolean
pk_package_sack_remove_by_filter (PkPackageSack *sack, PkPackageSackFilterFunc filter_cb, gpointer user_data)
{
	gboolean ret = FALSE;
	PkPackage *package;
	gint i;
	PkPackageSackPrivate *priv = sack->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (filter_cb != NULL, FALSE);

	/* add each that matches the info enum */
	for (i = 0; i < (gint) priv->array->len; i++) {
		package = g_ptr_array_index (priv->array, i);
		if (!filter_cb (package, user_data)) {
			ret = TRUE;
			pk_package_sack_remove_package (sack, package);

			/* ensure we pick up subsequent matches */
			i--;
		}
	}
	return ret;
}

/**
 * pk_package_sack_find_by_id:
 * @sack: a valid #PkPackageSack instance
 * @package_id: a package_id descriptor
 *
 * Finds a package in a sack from reference. As soon as one package is found
 * the search is stopped.
 *
 * Return value: (transfer full): the #PkPackage object, or %NULL if unfound. Free with g_object_unref()
 *
 * Since: 0.5.2
 **/
PkPackage *
pk_package_sack_find_by_id (PkPackageSack *sack, const gchar *package_id)
{
	PkPackage *package = NULL;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), NULL);
	g_return_val_if_fail (package_id != NULL, NULL);

	package = g_hash_table_lookup (sack->priv->table, package_id);
	if (package != NULL)
		g_object_ref (package);

	return package;
}

/**
 * pk_package_sack_sort_compare_name_func:
 **/
static gint
pk_package_sack_sort_compare_name_func (PkPackage **a, PkPackage **b)
{
	const gchar *package_id1;
	const gchar *package_id2;
	gchar **split1;
	gchar **split2;
	gint retval;

	package_id1 = pk_package_get_id (*a);
	package_id2 = pk_package_get_id (*b);
	split1 = pk_package_id_split (package_id1);
	split2 = pk_package_id_split (package_id2);
	retval = g_strcmp0 (split1[PK_PACKAGE_ID_NAME], split2[PK_PACKAGE_ID_NAME]);
	g_strfreev (split1);
	g_strfreev (split2);
	return retval;
}

/**
 * pk_package_sack_sort_compare_package_id_func:
 **/
static gint
pk_package_sack_sort_compare_package_id_func (PkPackage **a, PkPackage **b)
{
	const gchar *package_id1;
	const gchar *package_id2;
	package_id1 = pk_package_get_id (*a);
	package_id2 = pk_package_get_id (*b);
	return g_strcmp0 (package_id1, package_id2);
}

/**
 * pk_package_sack_sort_compare_summary_func:
 **/
static gint
pk_package_sack_sort_compare_summary_func (PkPackage **a, PkPackage **b)
{
	const gchar *summary1;
	const gchar *summary2;
	summary1 = pk_package_get_summary (*a);
	summary2 = pk_package_get_summary (*b);
	return g_strcmp0 (summary1, summary2);
}

/**
 * pk_package_sack_sort_compare_info_func:
 **/
static gint
pk_package_sack_sort_compare_info_func (PkPackage **a, PkPackage **b)
{
	PkInfoEnum info1;
	PkInfoEnum info2;
	info1 = pk_package_get_info (*a);
	info2 = pk_package_get_info (*b);
	if (info1 == info2)
		return 0;
	else if (info1 > info2)
		return -1;
	return 1;
}

/**
 * pk_package_sack_sort_package_id:
 * @sack: a valid #PkPackageSack instance
 * @type: the type of sorting, e.g. #PK_PACKAGE_SACK_SORT_TYPE_NAME
 *
 * Sorts the package sack
 *
 * Since: 0.6.1
 **/
void
pk_package_sack_sort (PkPackageSack *sack, PkPackageSackSortType type)
{
	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	if (type == PK_PACKAGE_SACK_SORT_TYPE_NAME)
		g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_name_func);
	else if (type == PK_PACKAGE_SACK_SORT_TYPE_PACKAGE_ID)
		g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_package_id_func);
	else if (type == PK_PACKAGE_SACK_SORT_TYPE_SUMMARY)
		g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_summary_func);
	else if (type == PK_PACKAGE_SACK_SORT_TYPE_INFO)
		g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_info_func);
}

/**
 * pk_package_sack_get_total_bytes:
 * @sack: a valid #PkPackageSack instance
 *
 * Gets the total size of the package sack in bytes.
 *
 * Return value: the size in bytes
 *
 * Since: 0.5.2
 **/
guint64
pk_package_sack_get_total_bytes (PkPackageSack *sack)
{
	PkPackage *package = NULL;
	guint i;
	GPtrArray *array;
	guint64 bytes = 0;
	guint64 bytes_tmp = 0;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);

	array = sack->priv->array;
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_object_get (package,
			      "size", &bytes_tmp,
			      NULL);
		bytes += bytes_tmp;
	}

	return bytes;
}

/**
 * pk_package_sack_get_package_ids:
 **/
static gchar **
pk_package_sack_get_package_ids (PkPackageSack *sack)
{
	const gchar *id;
	gchar **package_ids;
	const GPtrArray *array;
	PkPackage *package;
	guint i;

	/* create array of package_ids */
	array = sack->priv->array;
	package_ids = g_new0 (gchar *, array->len+1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		id = pk_package_get_id (package);
		package_ids[i] = g_strdup (id);
	}

	return package_ids;
}

typedef struct {
	PkPackageSack		*sack;
	GCancellable		*cancellable;
	gboolean		 ret;
	GSimpleAsyncResult	*res;
} PkPackageSackState;

/***************************************************************************************************/

/**
 * pk_package_sack_merge_bool_state_finish:
 **/
static void
pk_package_sack_merge_bool_state_finish (PkPackageSackState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);
	g_object_unref (state->res);
	g_object_unref (state->sack);
	g_slice_free (PkPackageSackState, state);
}

/**
 * pk_package_sack_resolve_cb:
 **/
static void
pk_package_sack_resolve_cb (GObject *source_object, GAsyncResult *res, PkPackageSackState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *packages = NULL;
	PkPackage *item;
	guint i;
	PkPackage *package;
	PkInfoEnum info;
	gchar *summary;
	gchar *package_id;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to resolve: %s", error->message);
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get the packages */
	packages = pk_results_get_package_array (results);
	if (packages->len == 0) {
		g_warning ("%i", state->ret);
		error = g_error_new (1, 0, "no packages found!");
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* set data on each item */
	for (i=0; i<packages->len; i++) {
		item = g_ptr_array_index (packages, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);

		/* get package, and set data */
		package = pk_package_sack_find_by_id (state->sack, package_id);
		if (package == NULL) {
			g_warning ("failed to find %s", package_id);
			goto skip;
		}

		/* set data */
		g_object_set (package,
			      "info", info,
			      "summary", summary,
			      NULL);
		g_object_unref (package);
skip:
		g_free (summary);
		g_free (package_id);
	}

	/* all okay */
	state->ret = TRUE;

	/* we're done */
	pk_package_sack_merge_bool_state_finish (state, error);
out:
	if (results != NULL)
		g_object_unref (results);
	if (packages != NULL)
		g_ptr_array_unref (packages);
}

/**
 * pk_package_sack_resolve_async:
 * @sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages using resolve.
 *
 * Since: 0.5.2
 **/
void
pk_package_sack_resolve_async (PkPackageSack *sack, GCancellable *cancellable,
				     PkProgressCallback progress_callback, gpointer progress_user_data,
				     GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkPackageSackState *state;
	gchar **package_ids;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (sack), callback, user_data, pk_package_sack_resolve_async);

	/* save state */
	state = g_slice_new0 (PkPackageSackState);
	state->res = g_object_ref (res);
	state->sack = g_object_ref (sack);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
	}
	state->ret = FALSE;

	/* start resolve async */
	package_ids = pk_package_sack_get_package_ids (sack);
	pk_client_resolve_async (sack->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids,
				 cancellable, progress_callback, progress_user_data,
				 (GAsyncReadyCallback) pk_package_sack_resolve_cb, state);
	g_strfreev (package_ids);
	g_object_unref (res);
}

/**
 * pk_package_sack_merge_generic_finish:
 * @sack: a valid #PkPackageSack instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.2
 **/
gboolean
pk_package_sack_merge_generic_finish (PkPackageSack *sack, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/***************************************************************************************************/

/**
 * pk_package_sack_get_details_cb:
 **/
static void
pk_package_sack_get_details_cb (GObject *source_object, GAsyncResult *res, PkPackageSackState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *details = NULL;
	PkDetails *item;
	guint i;
	PkPackage *package;
	PkGroupEnum group;
	gchar *license;
	gchar *url;
	gchar *description;
	gchar *package_id;
	guint64 size;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to details: %s", error->message);
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get the details */
	details = pk_results_get_details_array (results);
	if (details->len == 0) {
		error = g_error_new (1, 0, "no details found!");
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* set data on each item */
	for (i=0; i<details->len; i++) {
		item = g_ptr_array_index (details, i);

		g_object_get (item,
			      "package-id", &package_id,
			      "group", &group,
			      "license", &license,
			      "url", &url,
			      "description", &description,
			      "size", &size,
			      NULL);

		/* get package, and set data */
		package = pk_package_sack_find_by_id (state->sack, package_id);
		if (package == NULL) {
			g_warning ("failed to find %s", package_id);
			goto skip;
		}

		/* set data */
		g_object_set (package,
			      "license", license,
			      "group", group,
			      "description", description,
			      "url", url,
			      "size", size,
			      NULL);
		g_object_unref (package);
skip:
		g_free (package_id);
		g_free (license);
		g_free (url);
		g_free (description);
	}

	/* all okay */
	state->ret = TRUE;

	/* we're done */
	pk_package_sack_merge_bool_state_finish (state, error);
out:
	if (results != NULL)
		g_object_unref (results);
	if (details != NULL)
		g_ptr_array_unref (details);
}

/**
 * pk_package_sack_get_details_async:
 * @sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages.
 **/
void
pk_package_sack_get_details_async (PkPackageSack *sack, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkPackageSackState *state;
	gchar **package_ids;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (sack), callback, user_data, pk_package_sack_get_details_async);

	/* save state */
	state = g_slice_new0 (PkPackageSackState);
	state->res = g_object_ref (res);
	state->sack = g_object_ref (sack);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
	}
	state->ret = FALSE;

	/* start details async */
	package_ids = pk_package_sack_get_package_ids (sack);
	pk_client_get_details_async (sack->priv->client, package_ids,
				     cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_package_sack_get_details_cb, state);

	g_strfreev (package_ids);
	g_object_unref (res);
}

/***************************************************************************************************/

/**
 * pk_package_sack_get_update_detail_cb:
 **/
static void
pk_package_sack_get_update_detail_cb (GObject *source_object, GAsyncResult *res, PkPackageSackState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *update_details = NULL;
	PkUpdateDetail *item;
	guint i;
	PkPackage *package;
	gchar *package_id;
	gchar *updates;
	gchar *obsoletes;
	gchar *vendor_url;
	gchar *bugzilla_url;
	gchar *cve_url;
	PkRestartEnum restart;
	gchar *update_text;
	gchar *changelog;
	PkUpdateStateEnum state_enum;
	gchar *issued;
	gchar *updated;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to update_detail: %s", error->message);
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get the update_details */
	update_details = pk_results_get_update_detail_array (results);
	if (update_details->len == 0) {
		error = g_error_new (1, 0, "no update details found!");
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* set data on each item */
	for (i=0; i<update_details->len; i++) {
		item = g_ptr_array_index (update_details, i);
		g_object_get (item,
			      "package-id", &package_id,
			      "updates", &updates,
			      "obsoletes", &obsoletes,
			      "vendor-url", &vendor_url,
			      "bugzilla-url", &bugzilla_url,
			      "cve-url", &cve_url,
			      "restart", &restart,
			      "update-text", &update_text,
			      "changelog", &changelog,
			      "state", &state_enum,
			      "issued", &issued,
			      "updated", &updated,
			      NULL);

		/* get package, and set data */
		package = pk_package_sack_find_by_id (state->sack, package_id);
		if (package == NULL) {
			g_warning ("failed to find %s", package_id);
			goto skip;
		}

		/* set data */
		g_object_set (package,
			      "update-updates", updates,
			      "update-obsoletes", obsoletes,
			      "update-vendor-url", vendor_url,
			      "update-bugzilla-url", bugzilla_url,
			      "update-cve-url", cve_url,
			      "update-restart", restart,
			      "update-text", update_text,
			      "update-changelog", changelog,
			      "update-state", state_enum,
			      "update-issued", issued,
			      "update-updated", updated,
			      NULL);
		g_object_unref (package);
skip:
		g_free (package_id);
		g_free (updates);
		g_free (obsoletes);
		g_free (vendor_url);
		g_free (bugzilla_url);
		g_free (cve_url);
		g_free (update_text);
		g_free (changelog);
		g_free (issued);
		g_free (updated);
	}

	/* all okay */
	state->ret = TRUE;

	/* we're done */
	pk_package_sack_merge_bool_state_finish (state, error);
out:
	if (results != NULL)
		g_object_unref (results);
	if (update_details != NULL)
		g_ptr_array_unref (update_details);
}

/**
 * pk_package_sack_get_update_detail_async:
 * @sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in update details about packages.
 *
 * Since: 0.5.2
 **/
void
pk_package_sack_get_update_detail_async (PkPackageSack *sack, GCancellable *cancellable,
					 PkProgressCallback progress_callback, gpointer progress_user_data,
					 GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkPackageSackState *state;
	gchar **package_ids;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (sack), callback, user_data, pk_package_sack_get_update_detail_async);

	/* save state */
	state = g_slice_new0 (PkPackageSackState);
	state->res = g_object_ref (res);
	state->sack = g_object_ref (sack);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
	}
	state->ret = FALSE;

	/* start update_detail async */
	package_ids = pk_package_sack_get_package_ids (sack);
	pk_client_get_update_detail_async (sack->priv->client, package_ids,
					   cancellable, progress_callback, progress_user_data,
					   (GAsyncReadyCallback) pk_package_sack_get_update_detail_cb, state);

	g_strfreev (package_ids);
	g_object_unref (res);
}

/***************************************************************************************************/

/**
 * pk_package_sack_class_init:
 **/
static void
pk_package_sack_class_init (PkPackageSackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_package_sack_finalize;

#if 0
	/**
	 * PkPackageSack::changed:
	 * @sack: the #PkPackageSack instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the sack data may have changed.
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSACK (PkPackageSackClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
#endif

	g_type_class_add_private (klass, sizeof (PkPackageSackPrivate));
}

/**
 * pk_package_sack_init:
 **/
static void
pk_package_sack_init (PkPackageSack *sack)
{
	PkPackageSackPrivate *priv;
	sack->priv = PK_PACKAGE_SACK_GET_PRIVATE (sack);
	priv = sack->priv;

	priv->table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	priv->array = g_ptr_array_new_with_free_func (g_object_unref);
	priv->client = pk_client_new ();
}

/**
 * pk_package_sack_finalize:
 **/
static void
pk_package_sack_finalize (GObject *object)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	PkPackageSackPrivate *priv = sack->priv;

	g_ptr_array_unref (priv->array);
	g_hash_table_unref (priv->table);
	g_object_unref (priv->client);

	G_OBJECT_CLASS (pk_package_sack_parent_class)->finalize (object);
}

/**
 * pk_package_sack_new:
 *
 * Return value: a new PkPackageSack object.
 *
 * Since: 0.5.2
 **/
PkPackageSack *
pk_package_sack_new (void)
{
	PkPackageSack *sack;
	sack = g_object_new (PK_TYPE_PACKAGE_SACK, NULL);
	return PK_PACKAGE_SACK (sack);
}
