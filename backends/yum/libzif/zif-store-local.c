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
 * SECTION:zif-store-local
 * @short_description: A local store is a store that can operate on installed packages
 *
 * A #ZifStoreLocal is a subclassed #ZifStore and operates on installed objects.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>

#include <glib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <fcntl.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-groups.h"
#include "zif-package-local.h"
#include "zif-monitor.h"
#include "zif-string.h"
#include "zif-depend.h"
#include "zif-lock.h"

#include "egg-debug.h"

#define ZIF_STORE_LOCAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_LOCAL, ZifStoreLocalPrivate))

struct _ZifStoreLocalPrivate
{
	gboolean		 loaded;
	gchar			*prefix;
	GPtrArray		*packages;
	ZifGroups		*groups;
	ZifMonitor		*monitor;
	ZifLock			*lock;
};


G_DEFINE_TYPE (ZifStoreLocal, zif_store_local, ZIF_TYPE_STORE)
static gpointer zif_store_local_object = NULL;

/**
 * zif_store_local_set_prefix:
 * @store: the #ZifStoreLocal object
 * @prefix: the install root, e.g. "/"
 * @error: a #GError which is used on failure, or %NULL
 *
 * Sets the prefix to use for the install root.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_local_set_prefix (ZifStoreLocal *store, const gchar *prefix, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *filename = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (prefix != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check file exists */
	ret = g_file_test (prefix, G_FILE_TEST_IS_DIR);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "prefix %s does not exist", prefix);
		goto out;
	}

	/* is the same */
	if (g_strcmp0 (prefix, store->priv->prefix) == 0)
		goto out;

	/* empty cache */
	if (store->priv->loaded) {
		egg_debug ("abandoning cache");
		g_ptr_array_set_size (store->priv->packages, 0);
		store->priv->loaded = FALSE;
	}

	/* setup watch */
	filename = g_build_filename (prefix, "var", "lib", "rpm", "Packages", NULL);
	ret = zif_monitor_add_watch (store->priv->monitor, filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* save new value */
	g_free (store->priv->prefix);
	store->priv->prefix = g_strdup (prefix);
out:
	g_free (filename);
	return ret;
}

/**
 * zif_store_local_load:
 **/
static gboolean
zif_store_local_load (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gint retval;
	gboolean ret = TRUE;
	rpmdbMatchIterator mi;
	Header header;
	ZifPackageLocal *package;
	rpmdb db;
	GError *error_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), FALSE);
	g_return_val_if_fail (local->priv->prefix != NULL, FALSE);
	g_return_val_if_fail (local->priv->packages != NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* already loaded */
	if (local->priv->loaded)
		goto out;

	/* setup completion with the correct number of steps */
	zif_completion_set_number_steps (completion, 2);

	retval = rpmdbOpen (local->priv->prefix, &db, O_RDONLY, 0777);
	if (retval != 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to open rpmdb");
		ret = FALSE;
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* get list */
	mi = rpmdbInitIterator (db, RPMDBI_PACKAGES, NULL, 0);
	if (mi == NULL)
		egg_warning ("failed to get iterator");
	do {
		header = rpmdbNextIterator (mi);
		if (header == NULL)
			break;
		package = zif_package_local_new ();
		ret = zif_package_local_set_from_header (package, header, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to set from header: %s", error_local->message);
			g_error_free (error_local);
			g_object_unref (package);
			break;
		}
		g_ptr_array_add (local->priv->packages, package);
	} while (TRUE);
	rpmdbFreeIterator (mi);
	rpmdbClose (db);

	/* this section done */
	zif_completion_done (completion);

	/* okay */
	local->priv->loaded = TRUE;
out:
	return ret;
}

/**
 * zif_store_local_search_name:
 **/
static GPtrArray *
zif_store_local_search_name (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	gchar **split;
	GError *error_local = NULL;
	gboolean ret;
	ZifCompletion *completion_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		package_id = zif_package_get_id (package);
		split = pk_package_id_split (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split[PK_PACKAGE_ID_NAME], search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_strfreev (split);

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_search_category:
 **/
static GPtrArray *
zif_store_local_search_category (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *category;
	GError *error_local = NULL;
	gboolean ret;
	ZifCompletion *completion_local = NULL;
	ZifCompletion *completion_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		completion_loop = zif_completion_get_child (completion_local);
		category = zif_package_get_category (package, cancellable, completion_loop, NULL);
		for (j=0; search[j] != NULL; j++) {
			if (g_strcmp0 (category, search[j]) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_earch_details:
 **/
static GPtrArray *
zif_store_local_search_details (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	const gchar *description;
	gchar **split;
	GError *error_local = NULL;
	gboolean ret;
	ZifCompletion *completion_local = NULL;
	ZifCompletion *completion_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		package_id = zif_package_get_id (package);
		completion_loop = zif_completion_get_child (completion_local);
		description = zif_package_get_description (package, cancellable, completion_loop, NULL);
		split = pk_package_id_split (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcasestr (split[PK_PACKAGE_ID_NAME], search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			} else if (strcasestr (description, search[j]) != NULL) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_strfreev (split);

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_search_group:
 **/
static GPtrArray *
zif_store_local_search_group (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	PkGroupEnum group_tmp;
	GError *error_local = NULL;
	gboolean ret;
	PkGroupEnum group;
	ZifCompletion *completion_local = NULL;
	ZifCompletion *completion_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		for (j=0; search[j] != NULL; j++) {
			group = pk_group_enum_from_text (search[j]);
			completion_loop = zif_completion_get_child (completion_local);
			group_tmp = zif_package_get_group (package, cancellable, completion_loop, NULL);
			if (group == group_tmp) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_search_file:
 **/
static GPtrArray *
zif_store_local_search_file (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j, l;
	GPtrArray *array = NULL;
	ZifPackage *package;
	GPtrArray *files;
	GError *error_local = NULL;
	const gchar *filename;
	gboolean ret;
	ZifCompletion *completion_local = NULL;
	ZifCompletion *completion_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		completion_loop = zif_completion_get_child (completion_local);
		files = zif_package_get_files (package, cancellable, completion_loop, &error_local);
		if (files == NULL) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to get file lists: %s", error_local->message);
			g_error_free (error_local);
			g_ptr_array_unref (array);
			array = NULL;
			break;
		}
		for (j=0; j<files->len; j++) {
			filename = g_ptr_array_index (files, j);
			for (l=0; search[l] != NULL; l++) {
				if (g_strcmp0 (search[l], filename) == 0) {
					g_ptr_array_add (array, g_object_ref (package));
					break;
				}
			}
		}
		g_ptr_array_unref (files);

		/* this section done */
		zif_completion_done (completion_local);
	}
out:
	return array;
}

/**
 * zif_store_local_resolve:
 **/
static GPtrArray *
zif_store_local_resolve (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	ZifPackage *package;
	const gchar *package_id;
	GError *error_local = NULL;
	gboolean ret;
	gchar **split;
	ZifCompletion *completion_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		package_id = zif_package_get_id (package);
		split = pk_package_id_split (package_id);
		for (j=0; search[j] != NULL; j++) {
			if (strcmp (split[PK_PACKAGE_ID_NAME], search[j]) == 0) {
				g_ptr_array_add (array, g_object_ref (package));
				break;
			}
		}
		g_strfreev (split);

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_what_provides:
 **/
static GPtrArray *
zif_store_local_what_provides (ZifStore *store, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	guint j, k;
	GPtrArray *array = NULL;
	ZifPackage *package;
	GPtrArray *provides;
	GError *error_local = NULL;
	gboolean ret;
	const ZifDepend *provide;
	ZifCompletion *completion_local = NULL;
	ZifCompletion *completion_loop = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (search != NULL, NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		completion_loop = zif_completion_get_child (completion_local);
		provides = zif_package_get_provides (package, cancellable, completion_loop, NULL);
		for (j=0; j<provides->len; j++) {
			provide = g_ptr_array_index (provides, j);
			for (k=0; search[k] != NULL; k++) {
				if (g_strcmp0 (provide->name, search[k]) == 0) {
					g_ptr_array_add (array, g_object_ref (package));
					break;
				}
			}
		}

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_get_packages:
 **/
static GPtrArray *
zif_store_local_get_packages (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package;
	GError *error_local = NULL;
	gboolean ret;
	ZifCompletion *completion_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		g_ptr_array_add (array, g_object_ref (package));

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_local_find_package:
 **/
static ZifPackage *
zif_store_local_find_package (ZifStore *store, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	ZifPackage *package_tmp = NULL;
	GError *error_local = NULL;
	gboolean ret;
	const gchar *package_id_tmp;
	ZifCompletion *completion_local = NULL;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	g_return_val_if_fail (local->priv->prefix != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (local->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED, "not locked");
		goto out;
	}

	/* we have a different number of steps depending if we are loaded or not */
	if (local->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!local->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_local_load (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load package store: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* check we have packages */
	if (local->priv->packages->len == 0) {
		egg_warning ("no packages in sack, so nothing to do!");
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
				     "no packages in local sack");
		goto out;
	}

	/* setup completion with the correct number of steps */
	completion_local = zif_completion_get_child (completion);

	/* setup completion */
	zif_completion_set_number_steps (completion_local, local->priv->packages->len);

	/* iterate list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0;i<local->priv->packages->len;i++) {
		package_tmp = g_ptr_array_index (local->priv->packages, i);
		package_id_tmp = zif_package_get_id (package_tmp);
		if (g_strcmp0 (package_id_tmp, package_id) == 0)
			g_ptr_array_add (array, g_object_ref (package_tmp));

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* nothing */
	if (array->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to find package");
		goto out;
	}

	/* more than one match */
	if (array->len > 1) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_MULTIPLE_MATCHES,
				     "more than one match");
		goto out;
	}

	/* return ref to package */
	package = g_object_ref (g_ptr_array_index (array, 0));

	/* this section done */
	zif_completion_done (completion);
out:
	g_ptr_array_unref (array);
	return package;
}

/**
 * zif_store_local_get_id:
 **/
static const gchar *
zif_store_local_get_id (ZifStore *store)
{
	g_return_val_if_fail (ZIF_IS_STORE_LOCAL (store), NULL);
	return "installed";
}

/**
 * zif_store_local_print:
 **/
static void
zif_store_local_print (ZifStore *store)
{
	guint i;
	ZifPackage *package;
	ZifStoreLocal *local = ZIF_STORE_LOCAL (store);

	g_return_if_fail (ZIF_IS_STORE_LOCAL (store));
	g_return_if_fail (local->priv->prefix != NULL);
	g_return_if_fail (local->priv->packages->len != 0);

	for (i=0;i<local->priv->packages->len;i++) {
		package = g_ptr_array_index (local->priv->packages, i);
		zif_package_print (package);
	}
}

/**
 * zif_store_local_file_monitor_cb:
 **/
static void
zif_store_local_file_monitor_cb (ZifMonitor *monitor, ZifStoreLocal *store)
{
	store->priv->loaded = FALSE;

	g_ptr_array_set_size (store->priv->packages, 0);

	egg_debug ("rpmdb changed");
}

/**
 * zif_store_local_finalize:
 **/
static void
zif_store_local_finalize (GObject *object)
{
	ZifStoreLocal *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_LOCAL (object));
	store = ZIF_STORE_LOCAL (object);

	g_ptr_array_unref (store->priv->packages);
	g_object_unref (store->priv->groups);
	g_object_unref (store->priv->monitor);
	g_object_unref (store->priv->lock);
	g_free (store->priv->prefix);

	G_OBJECT_CLASS (zif_store_local_parent_class)->finalize (object);
}

/**
 * zif_store_local_class_init:
 **/
static void
zif_store_local_class_init (ZifStoreLocalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_local_finalize;

	/* map */
	store_class->load = zif_store_local_load;
	store_class->search_name = zif_store_local_search_name;
	store_class->search_category = zif_store_local_search_category;
	store_class->search_details = zif_store_local_search_details;
	store_class->search_group = zif_store_local_search_group;
	store_class->search_file = zif_store_local_search_file;
	store_class->resolve = zif_store_local_resolve;
	store_class->what_provides = zif_store_local_what_provides;
	store_class->get_packages = zif_store_local_get_packages;
	store_class->find_package = zif_store_local_find_package;
	store_class->get_id = zif_store_local_get_id;
	store_class->print = zif_store_local_print;

	g_type_class_add_private (klass, sizeof (ZifStoreLocalPrivate));
}

/**
 * zif_store_local_init:
 **/
static void
zif_store_local_init (ZifStoreLocal *store)
{
	store->priv = ZIF_STORE_LOCAL_GET_PRIVATE (store);
	store->priv->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	store->priv->groups = zif_groups_new ();
	store->priv->monitor = zif_monitor_new ();
	store->priv->lock = zif_lock_new ();
	store->priv->prefix = NULL;
	store->priv->loaded = FALSE;
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (zif_store_local_file_monitor_cb), store);
}

/**
 * zif_store_local_new:
 *
 * Return value: A new #ZifStoreLocal class instance.
 *
 * Since: 0.0.1
 **/
ZifStoreLocal *
zif_store_local_new (void)
{
	if (zif_store_local_object != NULL) {
		g_object_ref (zif_store_local_object);
	} else {
		zif_store_local_object = g_object_new (ZIF_TYPE_STORE_LOCAL, NULL);
		g_object_add_weak_pointer (zif_store_local_object, &zif_store_local_object);
	}
	return ZIF_STORE_LOCAL (zif_store_local_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

#include "zif-config.h"

void
zif_store_local_test (EggTest *test)
{
	ZifStoreLocal *store;
	gboolean ret;
	GPtrArray *array;
	ZifPackage *package;
	ZifGroups *groups;
	ZifLock *lock;
	ZifConfig *config;
	ZifCompletion *completion;
	GError *error = NULL;
	guint elapsed;
	const gchar *text;
	const gchar *string;
	const gchar *package_id;
	gchar **split;
	const gchar *to_array[] = { NULL, NULL };

	if (!egg_test_start (test, "ZifStoreLocal"))
		return;

	/* set this up as dummy */
	config = zif_config_new ();
	zif_config_set_filename (config, "../test/etc/yum.conf", NULL);

	/* use completion object */
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get groups");
	groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get store");
	store = zif_store_local_new ();
	egg_test_assert (test, store != NULL);

	/************************************************************/
	egg_test_title (test, "get lock");
	lock = zif_lock_new ();
	egg_test_assert (test, lock != NULL);

	/************************************************************/
	egg_test_title (test, "lock");
	ret = zif_lock_set_locked (lock, NULL, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "set prefix");
	ret = zif_store_local_set_prefix (store, "/", &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set prefix '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_store_local_load (ZIF_STORE (store), NULL, completion, &error);
	elapsed = egg_test_elapsed (test);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "check time < 1000ms");
	if (elapsed < 1000)
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "load (again)");
	zif_completion_reset (completion);
	ret = zif_store_local_load (ZIF_STORE (store), NULL, completion, &error);
	elapsed = egg_test_elapsed (test);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "check time < 10ms");
	if (elapsed < 10)
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "resolve");
	zif_completion_reset (completion);
	to_array[0] = "kernel";
	array = zif_store_local_resolve (ZIF_STORE (store), (gchar**)to_array, NULL, completion, NULL);
	elapsed = egg_test_elapsed (test);
	if (array->len >= 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "check time < 10ms");
	if (elapsed < 1000) /* TODO: reduce back down to 10ms */
		egg_test_success (test, "time to load = %ims", elapsed);
	else
		egg_test_failed (test, "time to load = %ims", elapsed);

	/************************************************************/
	egg_test_title (test, "search name");
	zif_completion_reset (completion);
	to_array[0] = "gnome-p";
	array = zif_store_local_search_name (ZIF_STORE (store), (gchar**)to_array, NULL, completion, NULL);
	if (array->len > 10)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "search details");
	zif_completion_reset (completion);
	to_array[0] = "manage packages";
	array = zif_store_local_search_details (ZIF_STORE (store), (gchar**)to_array, NULL, completion, NULL);
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);
	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "what-provides");
	zif_completion_reset (completion);
	to_array[0] = "config(PackageKit)";
	array = zif_store_local_what_provides (ZIF_STORE (store), (gchar**)to_array, NULL, completion, NULL);
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	/* get this package */
	package = g_ptr_array_index (array, 0);

	/************************************************************/
	egg_test_title (test, "get id");
	package_id = zif_package_get_id (package);
	split = pk_package_id_split (package_id);
	if (g_strcmp0 (split[PK_PACKAGE_ID_NAME], "PackageKit") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect name: %s", split[PK_PACKAGE_ID_NAME]);
	g_strfreev (split);

	/************************************************************/
	egg_test_title (test, "get package id");
	text = zif_package_get_package_id (package);
	if (g_str_has_suffix (text, ";installed"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect package_id: %s", text);

	/************************************************************/
	egg_test_title (test, "get summary");
	zif_completion_reset (completion);
	string = zif_package_get_summary (package, NULL, completion, NULL);
	if (g_strcmp0 (string, "Package management service") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect summary: %s", string);

	/************************************************************/
	egg_test_title (test, "get license");
	zif_completion_reset (completion);
	string = zif_package_get_license (package, NULL, completion, NULL);
	if (g_strcmp0 (string, "GPLv2+") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect license: %s", string);

	/************************************************************/
	egg_test_title (test, "get category");
	zif_completion_reset (completion);
	string = zif_package_get_category (package, NULL, completion, NULL);
	if (g_strcmp0 (string, "System Environment/Libraries") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect category: %s", string);

	/************************************************************/
	egg_test_title (test, "is devel");
	ret = zif_package_is_devel (package);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is gui");
	ret = zif_package_is_gui (package);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is installed");
	ret = zif_package_is_installed (package);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "is free");
	ret = zif_package_is_free (package);
	egg_test_assert (test, ret);

	g_ptr_array_unref (array);

	g_object_unref (store);
	g_object_unref (groups);
	g_object_unref (config);
	g_object_unref (lock);
	g_object_unref (completion);

	egg_test_end (test);
}
#endif

