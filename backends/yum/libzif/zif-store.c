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
 * SECTION:zif-store
 * @short_description: A store is an abstract collection of packages
 *
 * #ZifStoreLocal and #ZifStoreRemote both implement #ZifStore.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-store.h"
#include "zif-package.h"

#include "egg-debug.h"
#include "egg-string.h"

G_DEFINE_TYPE (ZifStore, zif_store, G_TYPE_OBJECT)

/**
 * zif_store_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.0.1
 **/
GQuark
zif_store_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("zif_store_error");
	return quark;
}

/**
 * zif_store_load:
 * @store: the #ZifStore object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Loads the #ZifStore object.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_load (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no support */
	if (klass->load == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->load (store, cancellable, completion, error);
}

/**
 * zif_store_clean:
 * @store: the #ZifStore object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Cleans the #ZifStore objects by deleting cache.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_clean (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no support */
	if (klass->clean == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->clean (store, cancellable, completion, error);
}

/**
 * zif_store_refresh:
 * @store: the #ZifStore object
 * @force: if the data should be re-downloaded if it's still valid
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * refresh the #ZifStore objects by downloading new data if required.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_refresh (ZifStore *store, gboolean force, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no support */
	if (klass->refresh == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return FALSE;
	}

	return klass->refresh (store, force, cancellable, completion, error);
}

/**
 * zif_store_search_name:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "power"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match the package name in some part.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_search_name (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_name == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->search_name (store, search, cancellable, completion, error);
}

/**
 * zif_store_search_category:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "gnome/games"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return packages in a specific category.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_search_category (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_category == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->search_category (store, search, cancellable, completion, error);
}

/**
 * zif_store_search_details:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "trouble"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that match some detail about the package.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_search_details (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);

	/* no support */
	if (klass->search_details == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->search_details (store, search, cancellable, completion, error);
}

/**
 * zif_store_search_group:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "games"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that belong in a specific group.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_search_group (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_group == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->search_group (store, search, cancellable, completion, error);
}

/**
 * zif_store_search_file:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "/usr/bin/gnome-power-manager"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide the specified file.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_search_file (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_file == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->search_file (store, search, cancellable, completion, error);
}

/**
 * zif_store_resolve:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "gnome-power-manager"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds packages matching the package name exactly.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_resolve (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->resolve == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->resolve (store, search, cancellable, completion, error);
}

/**
 * zif_store_what_provides:
 * @store: the #ZifStore object
 * @search: the search term, e.g. "gstreamer(codec-mp3)"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find packages that provide a specific string.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_what_provides (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->search_name == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->what_provides (store, search, cancellable, completion, error);
}

/**
 * zif_store_get_packages:
 * @store: the #ZifStore object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return all packages in the #ZifSack's.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_get_packages (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_packages == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->get_packages (store, cancellable, completion, error);
}

/**
 * zif_store_get_updates:
 * @store: the #ZifStore object
 * @packages: an array of #ZifPackage's
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of packages that are updatable.
 *
 * Return value: an array of #ZifPackage's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_get_updates (ZifStore *store, GPtrArray *packages,
		       GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (packages != NULL, NULL);

	/* no support */
	if (klass->get_updates == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->get_updates (store, packages, cancellable, completion, error);
}

/**
 * zif_store_find_package:
 * @store: the #ZifStore object
 * @package_id: the package ID which defines the package
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find a single package in the #ZifSack.
 *
 * Return value: A single #ZifPackage or %NULL
 *
 * Since: 0.0.1
 **/
ZifPackage *
zif_store_find_package (ZifStore *store, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (package_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->find_package == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->find_package (store, package_id, cancellable, completion, error);
}

/**
 * zif_store_get_categories:
 * @store: the #ZifStore object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Return a list of custom categories.
 *
 * Return value: an array of #PkCategory's
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_store_get_categories (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no support */
	if (klass->get_categories == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NO_SUPPORT,
				     "operation cannot be performed on this store");
		return NULL;
	}

	return klass->get_categories (store, cancellable, completion, error);
}

/**
 * zif_store_get_id:
 * @store: the #ZifStore object
 *
 * Gets the id for the object.
 *
 * Return value: A text ID, or %NULL
 *
 * Since: 0.0.1
 **/
const gchar *
zif_store_get_id (ZifStore *store)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_val_if_fail (ZIF_IS_STORE (store), NULL);

	/* no support */
	if (klass->get_id == NULL)
		return NULL;

	return klass->get_id (store);
}

/**
 * zif_store_print:
 * @store: the #ZifStore object
 *
 * Prints all the objects in the store.
 *
 * Since: 0.0.1
 **/
void
zif_store_print (ZifStore *store)
{
	ZifStoreClass *klass = ZIF_STORE_GET_CLASS (store);

	g_return_if_fail (ZIF_IS_STORE (store));

	/* no support */
	if (klass->print == NULL)
		return;

	klass->print (store);
}

/**
 * zif_store_finalize:
 **/
static void
zif_store_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE (object));

	G_OBJECT_CLASS (zif_store_parent_class)->finalize (object);
}

/**
 * zif_store_class_init:
 **/
static void
zif_store_class_init (ZifStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_store_finalize;
}

/**
 * zif_store_init:
 **/
static void
zif_store_init (ZifStore *store)
{
}

/**
 * zif_store_new:
 *
 * Return value: A new #ZifStore class instance.
 *
 * Since: 0.0.1
 **/
ZifStore *
zif_store_new (void)
{
	ZifStore *store;
	store = g_object_new (ZIF_TYPE_STORE, NULL);
	return ZIF_STORE (store);
}

