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

/**
 * SECTION:zif-sack
 * @short_description: A sack is a container that holds one or more stores
 *
 * A #ZifSack is a container that #ZifStore's are kept. Global operations can
 * be done on the sack and not the indervidual stores.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-config.h"
#include "zif-completion.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-sack.h"
#include "zif-package.h"
#include "zif-utils.h"
#include "zif-repos.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_SACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_SACK, ZifSackPrivate))

struct _ZifSackPrivate
{
	GPtrArray		*array;
};

/* in PackageKit we split categories from groups using a special @ prefix (bodge) */
#define PK_ROLE_ENUM_SEARCH_CATEGORY	PK_ROLE_ENUM_UNKNOWN + 1

G_DEFINE_TYPE (ZifSack, zif_sack, G_TYPE_OBJECT)

/**
 * zif_sack_add_store:
 * @sack: the #ZifSack object
 * @store: the #ZifStore to add
 *
 * Add a single #ZifStore to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_store (ZifSack *sack, ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);
	g_return_val_if_fail (store != NULL, FALSE);

	g_ptr_array_add (sack->priv->array, g_object_ref (store));
	return TRUE;
}

/**
 * zif_sack_add_stores:
 * @sack: the #ZifSack object
 * @stores: the array of #ZifStore's to add
 *
 * Add an array of #ZifStore's to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_stores (ZifSack *sack, GPtrArray *stores)
{
	guint i;
	ZifStore *store;
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);
	g_return_val_if_fail (stores != NULL, FALSE);

	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);
		ret = zif_sack_add_store (sack, store);
		if (!ret)
			break;
	}
	return ret;
}

/**
 * zif_sack_add_local:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add local store to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_local (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreLocal *store;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	store = zif_store_local_new ();
	zif_sack_add_store (sack, ZIF_STORE (store));
	g_object_unref (store);

	return TRUE;
}

/**
 * zif_sack_add_remote:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add remote stores to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_remote (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* get stores */
	repos = zif_repos_new ();
	array = zif_repos_get_stores (repos, cancellable, completion, &error_local);
	if (array == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get enabled stores: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add */
	zif_sack_add_stores (ZIF_SACK (sack), array);

	/* free */
	g_ptr_array_unref (array);
out:
	g_object_unref (repos);
	return ret;
}

/**
 * zif_sack_add_remote_enabled:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Convenience function to add enabled remote stores to the #ZifSack.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_add_remote_enabled (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifRepos *repos;
	GError *error_local = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* get stores */
	repos = zif_repos_new ();
	array = zif_repos_get_stores_enabled (repos, cancellable, completion, &error_local);
	if (array == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get enabled stores: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* add */
	zif_sack_add_stores (ZIF_SACK (sack), array);

	/* free */
	g_ptr_array_unref (array);
out:
	g_object_unref (repos);
	return ret;
}

/**
 * zif_sack_repos_search:
 **/
static GPtrArray *
zif_sack_repos_search (ZifSack *sack, PkRoleEnum role, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	GPtrArray *stores;
	GPtrArray *part;
	ZifStore *store;
	ZifPackage *package;
	GError *error_local = NULL;
	ZifCompletion *completion_local = NULL;

	/* find results in each store */
	stores = sack->priv->array;

	/* nothing to do */
	if (stores->len == 0) {
		egg_warning ("nothing to do");
		if (error != NULL)
			*error = g_error_new (1, 0, "nothing to do as no stores in sack");
		goto out;
	}

	/* set number of stores */
	zif_completion_set_number_steps (completion, stores->len);

	/* do each one */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		/* create a chain of completions */
		completion_local = zif_completion_get_child (completion);

		/* get results for this store */
		if (role == PK_ROLE_ENUM_RESOLVE)
			part = zif_store_resolve (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_NAME)
			part = zif_store_search_name (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
			part = zif_store_search_details (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_GROUP)
			part = zif_store_search_group (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_CATEGORY)
			part = zif_store_search_category (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_FILE)
			part = zif_store_search_file (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_GET_PACKAGES)
			part = zif_store_get_packages (store, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_GET_UPDATES)
			part = zif_store_get_updates (store, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_WHAT_PROVIDES)
			part = zif_store_what_provides (store, search, cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_GET_CATEGORIES)
			part = zif_store_get_categories (store, cancellable, completion_local, &error_local);
		else
			egg_error ("internal error: %s", pk_role_enum_to_text (role));
		if (part == NULL) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to %s in %s: %s", pk_role_enum_to_text (role), zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		for (j=0; j<part->len; j++) {
			package = g_ptr_array_index (part, j);
			g_ptr_array_add (array, g_object_ref (package));
		}
		g_ptr_array_unref (part);

		/* this section done */
		zif_completion_done (completion);
	}
out:
	return array;
}

/**
 * zif_sack_find_package:
 * @sack: the #ZifSack object
 * @package_id: the PackageId which defines the package
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find a single package in the #ZifSack.
 *
 * Return value: A single #ZifPackage or %NULL
 **/
ZifPackage *
zif_sack_find_package (ZifSack *sack, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *stores;
	ZifStore *store;
	ZifPackage *package = NULL;
	ZifCompletion *completion_local = NULL;

	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);

	/* find results in each store */
	stores = sack->priv->array;

	/* nothing to do */
	if (stores->len == 0) {
		egg_debug ("nothing to do");
		goto out;
	}

	/* create a chain of completions */
	zif_completion_set_number_steps (completion, stores->len);

	/* do each one */
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		completion_local = zif_completion_get_child (completion);
		package = zif_store_find_package (store, package_id, cancellable, completion_local, NULL);
		if (package != NULL)
			break;

		/* this section done */
		zif_completion_done (completion);
	}
out:
	return package;
}

/**
 * zif_sack_clean:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Cleans the #ZifStoreRemote objects by deleting cache.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_clean (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *stores;
	ZifStore *store;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	ZifCompletion *completion_local = NULL;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* clean each store */
	stores = sack->priv->array;

	/* nothing to do */
	if (stores->len == 0) {
		egg_debug ("nothing to do");
		goto out;
	}

	/* set number of stores */
	zif_completion_set_number_steps (completion, stores->len);

	/* do each one */
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		/* clean this one */
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_clean (store, cancellable, completion_local, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to clean %s: %s", zif_store_get_id (store), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}
out:
	return ret;
}

/**
 * zif_sack_refresh:
 * @sack: the #ZifSack object
 * @force: if the data should be re-downloaded if it's still valid
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Refreshs the #ZifStoreRemote objects by downloading new data
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_sack_refresh (ZifSack *sack, gboolean force, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *stores;
	ZifStore *store;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	ZifCompletion *completion_local = NULL;

	g_return_val_if_fail (ZIF_IS_SACK (sack), FALSE);

	/* refresh each store */
	stores = sack->priv->array;

	/* nothing to do */
	if (stores->len == 0) {
		egg_debug ("nothing to do");
		goto out;
	}

	/* create a chain of completions */
	zif_completion_set_number_steps (completion, stores->len);

	/* do each one */
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		egg_warning ("refreshing %s", zif_store_get_id (store));

		/* refresh this one */
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_refresh (store, force, cancellable, completion_local, &error_local);
		if (!ret) {
			//if (error != NULL)
			//	*error = g_error_new (1, 0, "failed to refresh %s: %s", zif_store_get_id (store), error_local->message);
			//g_error_free (error_local);
			//goto out;
			/* non-fatal */
			g_print ("failed to refresh %s: %s\n", zif_store_get_id (store), error_local->message);
			g_clear_error (&error_local);
			ret = TRUE;
		}

		/* this section done */
		zif_completion_done (completion);
	}
out:
	return ret;
}

/**
 * zif_sack_resolve:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "gnome-power-manager"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_resolve (ZifSack *sack, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_RESOLVE, search, cancellable, completion, error);
}

/**
 * zif_sack_search_name:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "power"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match the package name in some part.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_name (ZifSack *sack, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_NAME, search, cancellable, completion, error);
}

/**
 * zif_sack_search_details:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "trouble"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match some detail about the package.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_details (ZifSack *sack, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_DETAILS, search, cancellable, completion, error);
}

/**
 * zif_sack_search_group:
 * @sack: the #ZifSack object
 * @group_enum: the group enumerated value, e.g. "games"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that belong in a specific group.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_group (ZifSack *sack, const gchar *group_enum, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_GROUP, group_enum, cancellable, completion, error);
}

/**
 * zif_sack_search_category:
 * @sack: the #ZifSack object
 * @group_id: the group id, e.g. "gnome-system-tools"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that belong in a specific category.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_category (ZifSack *sack, const gchar *group_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array;
	ZifPackage *package;
	const gchar *package_id;
	const gchar *package_id_tmp;
	gchar **split;

	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);

	/* get all results from all repos */
	array = zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_CATEGORY, group_id, cancellable, completion, error);
	if (array == NULL)
		goto out;

	/* remove duplicate package_ids */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_id = zif_package_get_id (package);
		for (j=0; j<array->len; j++) {
			if (i == j)
				continue;
			package = g_ptr_array_index (array, j);
			package_id_tmp = zif_package_get_id (package);
			if (g_strcmp0 (package_id, package_id_tmp) == 0) {
				split = pk_package_id_split (package_id);
				egg_warning ("duplicate %s-%s", split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_VERSION]);
				g_ptr_array_remove_index (array, j);
				g_strfreev (split);
			}
		}
	}
out:
	return array;
}

/**
 * zif_sack_search_file:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "/usr/bin/gnome-power-manager"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide the specified file.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_search_file (ZifSack *sack, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, cancellable, completion, error);
}

/**
 * zif_sack_get_packages:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return all packages in the #ZifSack's.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_get_packages (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_GET_PACKAGES, NULL, cancellable, completion, error);
}

/**
 * zif_sack_get_updates:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of packages that are updatable.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_get_updates (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_GET_UPDATES, NULL, cancellable, completion, error);
}

/**
 * zif_sack_what_provides:
 * @sack: the #ZifSack object
 * @search: the search term, e.g. "gstreamer(codec-mp3)"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: an array of #ZifPackage's
 **/
GPtrArray *
zif_sack_what_provides (ZifSack *sack, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);

	/* if this is a path, then we use the file list and treat like a SearchFile */
	if (g_str_has_prefix (search, "/"))
		return zif_sack_repos_search (sack, PK_ROLE_ENUM_SEARCH_FILE, search, cancellable, completion, error);
	return zif_sack_repos_search (sack, PK_ROLE_ENUM_WHAT_PROVIDES, search, cancellable, completion, error);
}

/**
 * zif_sack_get_categories:
 * @sack: the #ZifSack object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of custom categories from all repos.
 *
 * Return value: an array of #PkCategory's
 **/
GPtrArray *
zif_sack_get_categories (ZifSack *sack, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array;
	PkCategory *obj;
	PkCategory *obj_tmp;
	gchar *parent_id;
	gchar *parent_id_tmp;
	gchar *cat_id;
	gchar *cat_id_tmp;

	g_return_val_if_fail (ZIF_IS_SACK (sack), NULL);

	/* get all results from all repos */
	array = zif_sack_repos_search (sack, PK_ROLE_ENUM_GET_CATEGORIES, NULL, cancellable, completion, error);
	if (array == NULL)
		goto out;

	/* remove duplicate parents and groups */
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		g_object_get (obj,
			      "parent-id", &parent_id,
			      "cat-id", &cat_id,
			      NULL);
		for (j=0; j<array->len; j++) {
			if (i == j)
				continue;
			obj_tmp = g_ptr_array_index (array, j);
			g_object_get (obj_tmp,
				      "parent-id", &parent_id_tmp,
				      "cat-id", &cat_id_tmp,
				      NULL);
			if (g_strcmp0 (parent_id_tmp, parent_id) == 0 &&
			    g_strcmp0 (cat_id_tmp, cat_id) == 0) {
				egg_warning ("duplicate %s-%s", parent_id, cat_id);
				g_object_unref (obj_tmp);
				g_ptr_array_remove_index (array, j);
			}
			g_free (parent_id_tmp);
			g_free (cat_id_tmp);
		}
		g_free (parent_id);
		g_free (cat_id);
	}
out:
	return array;
}

/**
 * zif_sack_finalize:
 **/
static void
zif_sack_finalize (GObject *object)
{
	ZifSack *sack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_SACK (object));
	sack = ZIF_SACK (object);

	g_ptr_array_unref (sack->priv->array);

	G_OBJECT_CLASS (zif_sack_parent_class)->finalize (object);
}

/**
 * zif_sack_class_init:
 **/
static void
zif_sack_class_init (ZifSackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_sack_finalize;
	g_type_class_add_private (klass, sizeof (ZifSackPrivate));
}

/**
 * zif_sack_init:
 **/
static void
zif_sack_init (ZifSack *sack)
{
	sack->priv = ZIF_SACK_GET_PRIVATE (sack);
	sack->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_sack_new:
 *
 * Return value: A new #ZifSack class instance.
 **/
ZifSack *
zif_sack_new (void)
{
	ZifSack *sack;
	sack = g_object_new (ZIF_TYPE_SACK, NULL);
	return ZIF_SACK (sack);
}

