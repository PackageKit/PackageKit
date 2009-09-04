/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-package-sack
 * @short_description: TODO
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include <packagekit-glib2/pk-package-sack.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>

#include "egg-debug.h"

static void     pk_package_sack_finalize	(GObject     *object);

#define PK_PACKAGE_SACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE_SACK, PkPackageSackPrivate))

/**
 * PkPackageSackPrivate:
 *
 * Private #PkPackageSack data
 **/
struct _PkPackageSackPrivate
{
	GPtrArray		*array;
	PkClient		*client;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

G_DEFINE_TYPE (PkPackageSack, pk_package_sack, G_TYPE_OBJECT)

/**
 * pk_package_sack_get_size:
 * @sack: a valid #PkPackageSack instance
 *
 * Gets the number of packages in the sack
 *
 * Return value: the number of packages in the sack
 **/
guint
pk_package_sack_get_size (PkPackageSack *sack)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);

	return sack->priv->array->len;
}

/**
 * pk_package_sack_get_index:
 * @sack: a valid #PkPackageSack instance
 * @i: the instance to get
 *
 * Gets a packages from the sack
 *
 * Return value: a %PkPackage instance
 **/
PkPackage *
pk_package_sack_get_index (PkPackageSack *sack, guint i)
{
	PkPackage *package = NULL;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);

	/* index invalid */
	if (i >= sack->priv->array->len)
		goto out;

	/* get object */
	package = g_object_ref (g_ptr_array_index (sack->priv->array, i));
out:
	return package;
}

/**
 * pk_package_sack_add_package:
 * @sack: a valid #PkPackageSack instance
 * @package: a valid #PkPackage instance
 *
 * Adds a package to the sack.
 *
 * Return value: %TRUE if the package was added to the sack
 **/
gboolean
pk_package_sack_add_package (PkPackageSack *sack, PkPackage *package)
{
	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);

	/* add to array */
	g_ptr_array_add (sack->priv->array, g_object_ref (package));

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
	g_ptr_array_add (sack->priv->array, g_object_ref (package));
out:
	g_object_unref (package);
	return ret;
}

/**
 * pk_package_sack_remove_package:
 * @sack: a valid #PkPackageSack instance
 * @package: a valid #PkPackage instance
 * @package_id: a package_id descriptor
 *
 * Removes a package reference from the sack. The pointers have to match exactly.
 *
 * Return value: %TRUE if the package was removed from the sack
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
 * Return value: %TRUE if the package was removed to the sack
 **/
gboolean
pk_package_sack_remove_package_by_id (PkPackageSack *sack, const gchar *package_id)
{
	PkPackage *package;
	const gchar *id;
	gboolean ret = FALSE;
	guint i;
	guint len;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	len = sack->priv->array->len;
	for (i=0; i<len; i++) {
		package = g_ptr_array_index (sack->priv->array, i);
		id = pk_package_get_id (package);
		if (g_strcmp0 (package_id, id) == 0) {
			g_ptr_array_remove_index (sack->priv->array, i);
			ret = TRUE;
			break;
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
 * Return value: the #PkPackage object, or %NULL if unfound. Free with g_object_unref()
 **/
PkPackage *
pk_package_sack_find_by_id (PkPackageSack *sack, const gchar *package_id)
{
	PkPackage *package_tmp;
	const gchar *id;
	PkPackage *package = NULL;
	guint i;
	guint len;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (sack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	len = sack->priv->array->len;
	for (i=0; i<len; i++) {
		package_tmp = g_ptr_array_index (sack->priv->array, i);
		id = pk_package_get_id (package_tmp);
		if (g_strcmp0 (package_id, id) == 0) {
			package = g_object_ref (package_tmp);
			break;
		}
	}

	return package;
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
	gint retval;
	gchar *summary1;
	gchar *summary2;

	g_object_get (*a, "summary", &summary1, NULL);
	g_object_get (*b, "summary", &summary2, NULL);
	retval = g_strcmp0 (summary1, summary2);

	g_free (summary1);
	g_free (summary2);
	return retval;
}

/**
 * pk_package_sack_sort_compare_info_func:
 **/
static gint
pk_package_sack_sort_compare_info_func (PkPackage **a, PkPackage **b)
{
	PkInfoEnum *info1;
	PkInfoEnum *info2;

	g_object_get (*a, "info", &info1, NULL);
	g_object_get (*b, "info", &info2, NULL);

	if (info1 == info2)
		return 0;
	else if (info1 > info2)
		return -1;
	return 1;
}

/**
 * pk_package_sack_sort_package_id:
 *
 * Sorts by Package ID
 **/
void
pk_package_sack_sort_package_id (PkPackageSack *sack)
{
	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_package_id_func);
}

/**
 * pk_package_sack_sort_summary:
 *
 * Sorts by summary
 **/
void
pk_package_sack_sort_summary (PkPackageSack *sack)
{
	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_summary_func);
}

/**
 * pk_package_sack_sort_info:
 *
 * Sorts by PkInfoEnum
 **/
void
pk_package_sack_sort_info (PkPackageSack *sack)
{
	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_ptr_array_sort (sack->priv->array, (GCompareFunc) pk_package_sack_sort_compare_info_func);
}

/**
 * pk_package_sack_get_total_bytes:
 * @sack: a valid #PkPackageSack instance
 *
 * Gets the total size of the package sack in bytes.
 *
 * Return value: the size in bytes
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
	/* remove weak ref */
	if (state->sack != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->sack), (gpointer) &state->sack);

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
	g_object_unref (state->res);
	g_slice_free (PkPackageSackState, state);
}

/**
 * pk_package_sack_merge_resolve_cb:
 **/
static void
pk_package_sack_merge_resolve_cb (GObject *source_object, GAsyncResult *res, PkPackageSackState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *packages = NULL;
	const PkResultItemPackage *item;
	guint i;
	PkPackage *package;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to resolve: %s", error->message);
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* get the packages */
	packages = pk_results_get_package_array (results);
	if (packages->len == 0) {
		egg_warning ("%i", state->ret);
		error = g_error_new (1, 0, "no packages found!");
		pk_package_sack_merge_bool_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* set data on each item */
	for (i=0; i<packages->len; i++) {
		item = g_ptr_array_index (packages, i);

		egg_debug ("%s\t%s\t%s", pk_info_enum_to_text (item->info_enum), item->package_id, item->summary);

		/* get package, and set data */
		package = pk_package_sack_find_by_id (state->sack, item->package_id);
		if (package == NULL) {
			egg_warning ("failed to find %s", item->package_id);
			continue;
		}

		/* set data */
		g_object_set (package,
			      "info", item->info_enum,
			      "summary", item->summary,
			      NULL);
		g_object_unref (package);
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
 * pk_package_sack_merge_resolve_async:
 * @package_sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages using resolve.
 **/
void
pk_package_sack_merge_resolve_async (PkPackageSack *sack, GCancellable *cancellable,
				     PkProgressCallback progress_callback, gpointer progress_user_data,
				     GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkPackageSackState *state;
	gchar **package_ids;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (sack), callback, user_data, pk_package_sack_merge_resolve_async);

	/* save state */
	state = g_slice_new0 (PkPackageSackState);
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
	}
	state->sack = sack;
	state->ret = FALSE;
	g_object_add_weak_pointer (G_OBJECT (state->sack), (gpointer) &state->sack);

	/* start resolve async */
	package_ids = pk_package_sack_get_package_ids (sack);
	pk_client_resolve_async (sack->priv->client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids,
				 cancellable, progress_callback, progress_user_data,
				 (GAsyncReadyCallback) pk_package_sack_merge_resolve_cb, state);
	g_strfreev (package_ids);
	g_object_unref (res);
}

/**
 * pk_package_sack_merge_generic_finish:
 * @package_sack: a valid #PkPackageSack instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE for success
 **/
gboolean
pk_package_sack_merge_generic_finish (PkPackageSack *package_sack, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (package_sack), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/***************************************************************************************************/

/**
 * pk_package_sack_merge_details_cb:
 **/
static void
pk_package_sack_merge_details_cb (GObject *source_object, GAsyncResult *res, PkPackageSackState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *details = NULL;
	const PkResultItemDetails *item;
	guint i;
	PkPackage *package;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to details: %s", error->message);
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

		egg_debug ("%s\t%s\t%s", item->package_id, item->url, item->license);

		/* get package, and set data */
		package = pk_package_sack_find_by_id (state->sack, item->package_id);
		if (package == NULL) {
			egg_warning ("failed to find %s", item->package_id);
			continue;
		}

		/* set data */
		g_object_set (package,
			      "license", item->license,
			      "group", item->group_enum,
			      "description", item->description,
			      "url", item->url,
			      "size", item->size,
			      NULL);
		g_object_unref (package);
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
 * pk_package_sack_merge_details_async:
 * @package_sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in details about packages.
 **/
void
pk_package_sack_merge_details_async (PkPackageSack *sack, GCancellable *cancellable,
				     PkProgressCallback progress_callback, gpointer progress_user_data,
				     GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkPackageSackState *state;
	gchar **package_ids;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (sack), callback, user_data, pk_package_sack_merge_details_async);

	/* save state */
	state = g_slice_new0 (PkPackageSackState);
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
	}
	state->sack = sack;
	state->ret = FALSE;
	g_object_add_weak_pointer (G_OBJECT (state->sack), (gpointer) &state->sack);

	/* start details async */
	package_ids = pk_package_sack_get_package_ids (sack);
	pk_client_get_details_async (sack->priv->client, package_ids,
				     cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_package_sack_merge_details_cb, state);

	g_strfreev (package_ids);
	g_object_unref (res);
}

/***************************************************************************************************/

/**
 * pk_package_sack_merge_update_detail_cb:
 **/
static void
pk_package_sack_merge_update_detail_cb (GObject *source_object, GAsyncResult *res, PkPackageSackState *state)
{
	PkClient *client = PK_CLIENT (source_object);
	GError *error = NULL;
	PkResults *results;
	GPtrArray *update_details = NULL;
	const PkResultItemUpdateDetail *item;
	guint i;
	PkPackage *package;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to update_detail: %s", error->message);
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

		egg_debug ("%s\t%s\t%s", item->package_id, item->updates, item->changelog);

		/* get package, and set data */
		package = pk_package_sack_find_by_id (state->sack, item->package_id);
		if (package == NULL) {
			egg_warning ("failed to find %s", item->package_id);
			continue;
		}

		/* set data */
		g_object_set (package,
			      "update-updates", item->updates,
			      "update-obsoletes", item->obsoletes,
			      "update-vendor-url", item->vendor_url,
			      "update-bugzilla-url", item->bugzilla_url,
			      "update-cve-url", item->cve_url,
			      "update-restart", item->restart_enum,
			      "update-text", item->update_text,
			      "update-changelog", item->changelog,
			      "update-state", item->state_enum,
			      "update-issued", item->issued,
			      "update-updated", item->updated,
			      NULL);
		g_object_unref (package);
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
 * pk_package_sack_merge_update_detail_async:
 * @package_sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Merges in update details about packages.
 **/
void
pk_package_sack_merge_update_detail_async (PkPackageSack *sack, GCancellable *cancellable,
					   PkProgressCallback progress_callback, gpointer progress_user_data,
					   GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkPackageSackState *state;
	gchar **package_ids;

	g_return_if_fail (PK_IS_PACKAGE_SACK (sack));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (sack), callback, user_data, pk_package_sack_merge_update_detail_async);

	/* save state */
	state = g_slice_new0 (PkPackageSackState);
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
	}
	state->sack = sack;
	state->ret = FALSE;
	g_object_add_weak_pointer (G_OBJECT (state->sack), (gpointer) &state->sack);

	/* start update_detail async */
	package_ids = pk_package_sack_get_package_ids (sack);
	pk_client_get_update_detail_async (sack->priv->client, package_ids,
					   cancellable, progress_callback, progress_user_data,
					   (GAsyncReadyCallback) pk_package_sack_merge_update_detail_cb, state);

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
	g_object_unref (priv->client);

	G_OBJECT_CLASS (pk_package_sack_parent_class)->finalize (object);
}

/**
 * pk_package_sack_new:
 *
 * Return value: a new PkPackageSack object.
 **/
PkPackageSack *
pk_package_sack_new (void)
{
	PkPackageSack *sack;
	sack = g_object_new (PK_TYPE_PACKAGE_SACK, NULL);
	return PK_PACKAGE_SACK (sack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_package_sack_test_resolve_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	if (!ret) {
		egg_test_failed (test, "failed to merge resolve: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_test_loop_quit (test);
}

static void
pk_package_sack_test_details_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	if (!ret) {
		egg_test_failed (test, "failed to merge details: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_test_loop_quit (test);
}

static void
pk_package_sack_test_update_detail_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	if (!ret) {
		egg_test_failed (test, "failed to merge update detail: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_test_loop_quit (test);
}

void
pk_package_sack_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	gboolean ret;
	PkPackageSack *sack;
	PkPackage *package;
	gchar *text;
	guint size;
	PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
	guint64 bytes;

	if (!egg_test_start (test, "PkPackageSack"))
		return;

	/************************************************************/
	egg_test_title (test, "get package_sack");
	sack = pk_package_sack_new ();
	egg_test_assert (test, sack != NULL);

	/************************************************************/
	egg_test_title (test, "get size of unused package sack");
	size = pk_package_sack_get_size (sack);
	egg_test_assert (test, (size == 0));

	/************************************************************/
	egg_test_title (test, "remove package not present");
	ret = pk_package_sack_remove_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "find package not present");
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, (package == NULL));

	/************************************************************/
	egg_test_title (test, "add package");
	ret = pk_package_sack_add_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get size of package sack");
	size = pk_package_sack_get_size (sack);
	egg_test_assert (test, (size == 1));

	/************************************************************/
	egg_test_title (test, "merge resolve results");
	pk_package_sack_merge_resolve_async (sack, NULL, NULL, NULL, (GAsyncReadyCallback) pk_package_sack_test_resolve_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "find package which is present");
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, (package != NULL));

	/************************************************************/
	egg_test_title (test, "check new summary");
	g_object_get (package,
		      "info", &info,
		      "summary", &text,
		      NULL);
	egg_test_assert (test, (g_strcmp0 (text, "Power consumption monitor") == 0));

	/************************************************************/
	egg_test_title (test, "check new info");
	egg_test_assert (test, (info == PK_INFO_ENUM_INSTALLED));

	g_free (text);
	g_object_unref (package);

	/************************************************************/
	egg_test_title (test, "merge details results");
	pk_package_sack_merge_details_async (sack, NULL, NULL, NULL, (GAsyncReadyCallback) pk_package_sack_test_details_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got details in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "find package which is present");
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, (package != NULL));

	/************************************************************/
	egg_test_title (test, "check new url");
	g_object_get (package,
		      "url", &text,
		      NULL);
	egg_test_assert (test, (g_strcmp0 (text, "http://live.gnome.org/powertop") == 0));
	g_object_unref (package);
	g_free (text);

	/************************************************************/
	egg_test_title (test, "merge update detail results");
	pk_package_sack_merge_update_detail_async (sack, NULL, NULL, NULL, (GAsyncReadyCallback) pk_package_sack_test_update_detail_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got update detail in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "find package which is present");
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, (package != NULL));

	/************************************************************/
	egg_test_title (test, "check new vendor url");
	g_object_get (package,
		      "update-vendor-url", &text,
		      NULL);
	egg_test_assert (test, (g_strcmp0 (text, "http://www.distro-update.org/page?moo;Bugfix release for powertop") == 0));

	g_free (text);
	g_object_unref (package);

	/************************************************************/
	egg_test_title (test, "chck size in bytes");
	bytes = pk_package_sack_get_total_bytes (sack);
	egg_test_assert (test, (bytes == 103424));

	/************************************************************/
	egg_test_title (test, "remove package");
	ret = pk_package_sack_remove_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get size of package sack");
	size = pk_package_sack_get_size (sack);
	egg_test_assert (test, (size == 0));

	/************************************************************/
	egg_test_title (test, "remove already removed package");
	ret = pk_package_sack_remove_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	egg_test_assert (test, !ret);

	g_object_unref (sack);
	egg_test_end (test);
}
#endif

