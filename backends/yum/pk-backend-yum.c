/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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

#include <config.h>
#include <gio/gio.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>
#include <string.h>
#include <packagekit-glib2/pk-debug.h>

#ifdef HAVE_ZIF
#include <zif.h>
#endif

#define PREUPGRADE_BINARY			"/usr/bin/preupgrade"
#define YUM_REPOS_DIRECTORY			"/etc/yum.repos.d"
#define YUM_BACKEND_LOCKING_RETRIES		10
#define YUM_BACKEND_LOCKING_DELAY		2 /* seconds */
#define PACKAGE_MEDIA_REPO_FILENAME		"/etc/yum.repos.d/packagekit-media.repo"

typedef struct {
	PkBackendSpawn	*spawn;
	GFileMonitor	*monitor;
	GCancellable	*cancellable;
	PkBitfield	 use_zif;
	guint		 signal_finished;
	guint		 signal_status;
#ifdef HAVE_ZIF
	ZifConfig	*config;
	ZifStore	*store_local;
	ZifRepos	*repos;
	ZifGroups	*groups;
	ZifState	*state;
	ZifLock		*lock;
	ZifRelease	*release;
#endif
	GTimer		*timer;
	GVolumeMonitor	*volume_monitor;
} PkBackendYumPrivate;

static PkBackendYumPrivate *priv;

/**
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("YUM (and optionally ZIF)");
}

/**
 * pk_backend_get_author:
 */
gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Tim Lauridsen <timlau@fedoraproject.org>, "
			 "Richard Hughes <richard@hughsie.com>");
}

/**
 * pk_backend_stderr_cb:
 */
static gboolean
pk_backend_stderr_cb (PkBackend *backend, const gchar *output)
{
	/* unsigned rpm, this will be picked up by yum and and exception will be thrown */
	if (strstr (output, "NOKEY") != NULL)
		return FALSE;
	if (strstr (output, "GPG") != NULL)
		return FALSE;
	if (strstr (output, "DeprecationWarning") != NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_backend_stdout_cb:
 */
static gboolean
pk_backend_stdout_cb (PkBackend *backend, const gchar *output)
{
	return TRUE;
}

/**
 * pk_backend_yum_repos_changed_cb:
 **/
static void
pk_backend_yum_repos_changed_cb (GFileMonitor *monitor_, GFile *file, GFile *other_file, GFileMonitorEvent event_type, PkBackend *backend)
{
	gchar *filename;

	/* ignore the packagekit-media.repo file */
	filename = g_file_get_path (file);
	if (g_str_has_prefix (filename, PACKAGE_MEDIA_REPO_FILENAME))
		goto out;

	/* emit signal */
	pk_backend_repo_list_changed (backend);
out:
	g_free (filename);
}

#ifdef HAVE_ZIF

static void
pk_backend_state_percentage_changed_cb (ZifState *state, guint percentage, PkBackend *backend)
{
	pk_backend_set_percentage (backend, percentage);
}

static void
pk_backend_state_subpercentage_changed_cb (ZifState *state, guint subpercentage, PkBackend *backend)
{
	pk_backend_set_sub_percentage (backend, subpercentage);
}

/**
 * pk_backend_profile:
 */
static void
pk_backend_profile (const gchar *title)
{
	gdouble elapsed;

	/* just reset?  */
	if (title == NULL)
		goto out;
	elapsed = g_timer_elapsed (priv->timer, NULL);
	g_debug ("PROFILE: %ims\t%s", (guint) (elapsed * 1000.0f), title);
out:
	g_timer_reset (priv->timer);
}

/**
 * pk_backend_is_all_installed:
 */
static gboolean
pk_backend_is_all_installed (gchar **package_ids)
{
	guint i;
	gboolean ret = TRUE;

	/* check if we can use zif */
	for (i=0; package_ids[i] != NULL; i++) {
		if (!g_str_has_suffix (package_ids[i], ";installed")) {
			ret = FALSE;
			break;
		}
	}
	return ret;
}
#endif

/**
 * pk_backend_transaction_start:
 */
void
pk_backend_transaction_start (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret = FALSE;
	GError *error = NULL;
	const gchar *root;
	guint i;
	guint pid;
	guint cache_age;
	gchar *http_proxy = NULL;

	/* quit the spawned backend rather than waiting for it to time out */
	pk_backend_spawn_exit (priv->spawn);

	/* only try a finite number of times */
	for (i=0; i<YUM_BACKEND_LOCKING_RETRIES; i++) {

		/* try to lock */
		ret = zif_lock_set_locked (priv->lock, &pid, &error);
		if (ret)
			break;

		/* we're now waiting */
		pk_backend_set_status (backend, PK_STATUS_ENUM_WAITING_FOR_LOCK);

		/* now wait */
		g_debug ("Failed to lock on try %i of %i, already locked by PID %i (sleeping for %i seconds): %s\n",
			   i+1, YUM_BACKEND_LOCKING_RETRIES, pid, YUM_BACKEND_LOCKING_DELAY, error->message);
		g_clear_error (&error);
		g_usleep (YUM_BACKEND_LOCKING_DELAY * G_USEC_PER_SEC);
	}

	/* we failed to lock */
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_GET_LOCK, "failed to get lock, held by PID: %i", pid);
		goto out;
	}

	/* this backend does not support a relocatable root... yet */
	root = pk_backend_get_root (backend);
	if (g_strcmp0 (root, "/") != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INSTALL_ROOT_INVALID, "backend does not support this root: '%s'", root);
		goto out;
	}

	/* try to set, or re-set install root */
	ret = zif_store_local_set_prefix (ZIF_STORE_LOCAL (priv->store_local), root, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to set prefix: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get network state */
	ret = pk_backend_is_online (backend);
	if (!ret) {
		zif_config_set_boolean (priv->config, "network", FALSE, NULL);
		goto out;
	}

	/* tell ZifConfig it's okay to contact the network */
	zif_config_set_boolean (priv->config, "network", TRUE, NULL);

	/* set cache age */
	cache_age = pk_backend_get_cache_age (backend);
	if (cache_age > 0)
		zif_config_set_uint (priv->config, "max-age", cache_age, NULL);

	/* set the proxy */
	http_proxy = pk_backend_get_proxy_http (backend);
	zif_config_set_string (priv->config, "http_proxy", http_proxy, NULL);

	/* setup state */
	zif_state_reset (priv->state);
out:
	g_free (http_proxy);
#endif
	return;
}

/**
 * pk_backend_transaction_stop:
 */
void
pk_backend_transaction_stop (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	GError *error = NULL;

	/* try to unlock */
	ret = zif_lock_set_unlocked (priv->lock, &error);
	if (!ret) {
		g_warning ("failed to unlock: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
#endif
	return;
}

#ifdef HAVE_ZIF
/**
 * pk_backend_filter_package_array_newest:
 *
 * This function needs to scale well, and be fast to process 50,000 packages in
 * less than one second. If it looks overcomplicated, it's because it needs to
 * be O(n) not O(n*n).
 **/
static gboolean
pk_backend_filter_package_array_newest (GPtrArray *array)
{
	gchar **split;
	const gchar *package_id;
	gboolean installed;
	gchar *key;
	GHashTable *hash;
	gint retval;
	guint i;
	ZifPackage *found;
	ZifPackage *package;

	/* as an indexed hash table for speed */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	for (i=0; i<array->len; i++) {

		/* get the current package */
		package = g_ptr_array_index (array, i);
		package_id = zif_package_get_id (package);
		installed = zif_package_is_installed (package);

		/* generate enough data to be specific */
		split = pk_package_id_split (package_id);
		key = g_strdup_printf ("%s-%s-%i", split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_ARCH], installed);
		g_strfreev (split);

		/* we've not already come across this package */
		found = g_hash_table_lookup (hash, key);
		if (found == NULL) {
			g_hash_table_insert (hash, key, g_object_ref (package));
			continue;
		}

		/* compare one package vs the other package */
		retval = zif_package_compare (package, found);

		/* the package is older than the one we have stored */
		if (retval <= 0) {
			g_free (key);
			g_ptr_array_remove (array, package);
			continue;
		}

		/* the package is newer than what we have stored, delete the old store, and add this one */
		g_hash_table_remove (hash, found);
		g_hash_table_insert (hash, key, g_object_ref (package));
	}

	g_hash_table_unref (hash);
	return TRUE;
}

/**
 * pk_backend_filter_package_array:
 **/
static GPtrArray *
pk_backend_filter_package_array (GPtrArray *array, PkBitfield filters)
{
	guint i;
	ZifPackage *package;
	GPtrArray *result = NULL;

	result = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* pre-result */
	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);

		/* installed */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			if (!zif_package_is_installed (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			if (zif_package_is_installed (package))
				continue;
		}

		/* development */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
			if (!zif_package_is_devel (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			if (zif_package_is_devel (package))
				continue;
		}

		/* gui */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
			if (!zif_package_is_gui (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
			if (zif_package_is_gui (package))
				continue;
		}

		/* free */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE)) {
			if (!zif_package_is_free (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE)) {
			if (zif_package_is_free (package))
				continue;
		}

		/* arch */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH)) {
			if (!zif_package_is_native (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH)) {
			if (zif_package_is_native (package))
				continue;
		}

		/* add to array so we can post process */
		g_ptr_array_add (result, g_object_ref (package));
	}

	/* do newest filtering */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		pk_backend_filter_package_array_newest (result);

	return result;
}

/**
 * pk_backend_emit_package_array:
 **/
static gboolean
pk_backend_emit_package_array (PkBackend *backend, GPtrArray *array, ZifState *state)
{
	guint i;
	gboolean installed;
	PkInfoEnum info;
	const gchar *info_hint;
	const gchar *package_id;
	const gchar *summary;
	ZifPackage *package;

	g_return_val_if_fail (array != NULL, FALSE);

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		installed = zif_package_is_installed (package);
		package_id = zif_package_get_package_id (package);

		/* FIXME: should be okay as shouldn't be doing any action */
		zif_state_reset (state);
		summary = zif_package_get_summary (package, state, NULL);

		/* if we set a hint, use that, otherwise just get the installed status correct */
		info_hint = (const gchar *)g_object_get_data (G_OBJECT(package), "kind");
		if (info_hint == NULL) {
			info = installed ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
		} else {
			info = pk_info_enum_from_string (info_hint);
		}

		pk_backend_package (backend, info, package_id, summary);
	}
	return TRUE;
}

/**
 * pk_backend_error_handler_cb:
 */
static gboolean
pk_backend_error_handler_cb (const GError *error, PkBackend *backend)
{
	/* if we try to do a comps search on a local store */
	if (error->domain == ZIF_STORE_ERROR &&
	    error->code == ZIF_STORE_ERROR_NO_SUPPORT) {
		g_debug ("ignoring operation on PkStoreLocal: %s", error->message);
		return TRUE;
	}
	/* emit a warning, this isn't fatal */
	pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "%s", error->message);
	return TRUE;
}

/**
 * pk_backend_get_default_store_array_for_filter:
 */
static GPtrArray *
pk_backend_get_default_store_array_for_filter (PkBackend *backend, PkBitfield filters, ZifState *state, GError **error)
{
	GPtrArray *store_array;
	ZifStore *store;
	GPtrArray *array;
	GError *error_local = NULL;

	store_array = zif_store_array_new ();

	/* add local packages to the store_array */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		store = zif_store_local_new ();
		zif_store_array_add_store (store_array, store);
		g_object_unref (store);
	}

	/* add remote packages to the store_array */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		array = zif_repos_get_stores_enabled (priv->repos, state, &error_local);
		if (array == NULL) {
			g_set_error (error, 1, 0, "failed to get enabled stores: %s", error_local->message);
			g_error_free (error_local);
			g_ptr_array_unref (store_array);
			store_array = NULL;
			goto out;
		}
		zif_store_array_add_stores (store_array, array);
		g_ptr_array_unref (array);
	}
out:
	return store_array;
}

/**
 * pk_backend_search_newest:
 */
static GPtrArray *
pk_backend_search_newest (GPtrArray *store_array, ZifState *state, guint recent, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	GTimeVal timeval_now;
	guint diff_secs = recent * 24 * 60 * 60;
	guint i;
	ZifPackage *package;

	/* get all the packages */
	array_tmp = zif_store_array_get_packages (store_array, state, error);
	if (array_tmp == NULL)
		goto out;

	/* only add elements to the array that are new enough */
	g_get_current_time (&timeval_now);
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array_tmp->len; i++) {
		package = g_ptr_array_index (array_tmp, i);
		if (timeval_now.tv_sec - zif_package_get_time_file (package) < diff_secs)
			g_ptr_array_add (array, g_object_ref (package));
	}
	g_debug ("added %i newest packages", array->len);
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * pk_backend_create_meta_package_for_category:
 */
static ZifPackage *
pk_backend_create_meta_package_for_category (GPtrArray *store_array, ZifCategory *cat, ZifState *state, GError **error)
{
	ZifPackage *package = NULL;
	ZifPackage *package_tmp;
	GPtrArray *array_packages;
	gchar *package_id = NULL;
	gboolean ret;
	ZifString *string;
	guint j;
	const gchar *to_array[] = { NULL, NULL };
	PkInfoEnum info = PK_INFO_ENUM_COLLECTION_INSTALLED;

	/* are all the packages in this group installed? */
	to_array[0] = zif_category_get_id (cat);
	array_packages = zif_store_array_search_category (store_array, (gchar**)to_array, state, error);
	if (array_packages == NULL)
		goto out;

	/* if any are not installed, then this is not installed */
	for (j=0; j<array_packages->len; j++) {
		package_tmp = g_ptr_array_index (array_packages, j);
		if (!zif_package_is_installed (package_tmp)) {
			info = PK_INFO_ENUM_COLLECTION_AVAILABLE;
			g_debug ("%s is not installed, so marking as not installed %s collection",
				 zif_package_get_id (package_tmp),
				 zif_category_get_id (cat));
			break;
		}
	}

	/* fake something */
	package_id = g_strdup_printf ("%s;;;meta",
				      zif_category_get_id (cat));
	package = zif_package_new ();
	ret = zif_package_set_id (package, package_id, NULL);
	if (!ret) {
		g_object_unref (package);
		package = NULL;
		goto out;
	}

	/* set summary */
	string = zif_string_new (zif_category_get_name (cat));
	zif_package_set_summary (package, string);
	zif_string_unref (string);

	/* map to simple binary installed value */
	zif_package_set_installed (package, (info == PK_INFO_ENUM_COLLECTION_INSTALLED));

	/* add to results */
	/* TODO: make a proper property */
	g_object_set_data (G_OBJECT(package), "kind", (gpointer)pk_info_enum_to_string (info));
out:
	if (array_packages != NULL)
		g_ptr_array_unref (array_packages);
	g_free (package_id);
	return package;
}

/**
 * pk_backend_search_collections:
 */
static GPtrArray *
pk_backend_search_collections (GPtrArray *store_array, ZifState *state, GError **error)
{
	gboolean ret;
	gchar *package_id;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	GError *error_local = NULL;
	ZifCategory *cat;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	/* set steps */
	zif_state_set_number_steps (state, 2);

	/* get sorted list of unique categories */
	state_local = zif_state_get_child (state);
	array_tmp = zif_store_array_get_categories (store_array, state_local, error);
	if (array_tmp == NULL)
		goto out;

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* set steps */
	state_local = zif_state_get_child (state);
	zif_state_set_number_steps (state_local, array_tmp->len);

	/* generate fake packages */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array_tmp->len; i++) {
		cat = g_ptr_array_index (array_tmp, i);

		/* ignore top level categories */
		if (zif_category_get_parent_id (cat) == NULL)
			continue;

		/* fake something here */
		state_loop = zif_state_get_child (state_local);
		package = pk_backend_create_meta_package_for_category (store_array, cat, state_loop, &error_local);
		if (package != NULL) {
			g_ptr_array_add (array, g_object_ref (package));
		} else {
			g_warning ("failed to add id %s: %s", package_id, error_local->message);
			g_clear_error (&error_local);
		}

		/* done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;

		g_object_unref (package);
	}

	/* done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	return array;
}

/**
 * pk_backend_get_cat_for_id:
 */
static ZifCategory *
pk_backend_get_cat_for_id (GPtrArray *store_array, const gchar *id, ZifState *state, GError **error)
{
	GPtrArray *array = NULL;
	ZifCategory *cat = NULL;
	ZifCategory *cat_tmp;
	guint i;

	/* get all cats */
	array = zif_store_array_get_categories (store_array, state, error);
	if (array == NULL)
		goto out;

	/* find one that matches */
	for (i=0; i<array->len; i++) {
		cat_tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (zif_category_get_id (cat_tmp), id) == 0) {
			cat = g_object_ref (cat_tmp);
			break;
		}
	}

	/* nothing found, so set error */
	if (cat == NULL)
		g_set_error (error, 1, 0, "no category %s found", id);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return cat;
}

/**
 * pk_backend_resolve_groups:
 */
static GPtrArray *
pk_backend_resolve_groups (GPtrArray *store_array, gchar **search, ZifState *state, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_retval = NULL;
	guint i;
	ZifCategory *cat;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	/* set steps */
	zif_state_set_number_steps (state, g_strv_length (search));

	/* resolve all the groups */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; search[i] != NULL; i++) {
		state_local = zif_state_get_child (state);

		/* set steps */
		zif_state_set_number_steps (state_local, 2);

		/* get the category */
		state_loop = zif_state_get_child (state_local);
		cat = pk_backend_get_cat_for_id (store_array, search[i]+1, state_loop, &error_local);
		if (cat == NULL) {
			g_debug ("group %s not found: %s", search[i], error_local->message);
			g_clear_error (&error_local);

			/* this part done */
			ret = zif_state_finished (state_loop, error);
			if (!ret)
				goto out;
			ret = zif_state_done (state_local, error);
			if (!ret)
				goto out;
		} else {
			ret = zif_state_done (state_local, error);
			if (!ret)
				goto out;

			/* fake something here */
			state_loop = zif_state_get_child (state_local);
			package = pk_backend_create_meta_package_for_category (store_array, cat, state_loop, &error_local);
			if (package != NULL) {
				g_ptr_array_add (array, package);
			} else {
				g_warning ("failed to add id %s: %s",
					   zif_category_get_id (cat),
					   error_local->message);
				g_clear_error (&error_local);
				ret = zif_state_finished (state_loop, error);
			}
		}

		/* this part done */
		ret = zif_state_done (state_local, error);
		if (!ret)
			goto out;
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array_retval = g_ptr_array_ref (array);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return array_retval;
}

/**
 * pk_backend_what_provides_helper:
 */
static GPtrArray *
pk_backend_what_provides_helper (GPtrArray *store_array, gchar **search, ZifState *state, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	GPtrArray *array_retval = NULL;
	GPtrArray *array_tmp;
	guint i, j;
	ZifDepend *depend;
	ZifPackage *package;
	ZifState *state_local;

	/* set steps */
	zif_state_set_number_steps (state, g_strv_length (search));

	/* resolve all the groups */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; search[i] != NULL; i++) {
		state_local = zif_state_get_child (state);

		/* parse this depend */
		depend = zif_depend_new ();
		ret = zif_depend_parse_description (depend, search[i], error);
		if (!ret)
			goto out;

		/* find what provides this depend */
		array_tmp = zif_store_array_what_provides (store_array, depend, state_local, error);
		g_object_unref (depend);
		if (array_tmp == NULL)
			goto out;

		/* add each result */
		for (j=0; j<array_tmp->len; j++) {
			package = g_ptr_array_index (array_tmp, i);
			g_ptr_array_add (array, g_object_ref (package));
		}
		g_ptr_array_unref (array_tmp);

		/* set steps */
		zif_state_set_number_steps (state_local, 2);

		/* this part done */
		ret = zif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	array_retval = g_ptr_array_ref (array);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return array_retval;
}
#endif

/**
 * pk_backend_search_thread:
 */
static gboolean
pk_backend_search_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	GPtrArray *store_array = NULL;
	GPtrArray *array = NULL;
	GPtrArray *result;
	PkBitfield filters;
	PkRoleEnum role;
	ZifState *state_local;
	GError *error = NULL;
	gchar **search;
	guint recent;
	guint i;

	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	role = pk_backend_get_role (backend);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	zif_state_set_number_steps (priv->state, 4);

	/* get default store_array */
	state_local = zif_state_get_child (priv->state);
	store_array = pk_backend_get_default_store_array_for_filter (backend, filters, state_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}
	zif_state_set_error_handler (priv->state, (ZifStateErrorHandlerCb) pk_backend_error_handler_cb, backend);

	/* do get action */
	if (role == PK_ROLE_ENUM_GET_PACKAGES) {
		state_local = zif_state_get_child (priv->state);
		array = zif_store_array_get_packages (store_array, state_local, &error);
		if (array == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get packages: %s", error->message);
			g_error_free (error);
			goto out;
		}
	} else {
		/* treat these all the same */
		search = pk_backend_get_strv (backend, "search");
		if (search == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "failed to get 'search' for %s", pk_role_enum_to_string (role));
			goto out;
		}
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

		/* do OR search */
		state_local = zif_state_get_child (priv->state);
		if (role == PK_ROLE_ENUM_SEARCH_NAME) {
			array = zif_store_array_search_name (store_array, search, state_local, &error);
		} else if (role == PK_ROLE_ENUM_SEARCH_DETAILS) {
			array = zif_store_array_search_details (store_array, search, state_local, &error);
		} else if (role == PK_ROLE_ENUM_SEARCH_GROUP) {
			gchar **search_stripped;
			guint search_entries;

			/* if the search temp is prefixed with '@' then it is a
			 * category search, and we have to strip it */
			if (search[0][0] == '@') {
				search_entries = g_strv_length (search);
				search_stripped = g_new0 (gchar *, search_entries + 1);
				for (i=0; i < search_entries; i++)
					search_stripped[i] = g_strdup (&search[i][1]);
				array = zif_store_array_search_category (store_array, search_stripped, state_local, &error);
				g_strfreev (search_stripped);
			} else if (g_strcmp0 (search[0], "newest") == 0) {
				recent = zif_config_get_uint (priv->config, "recent", &error);
				array = pk_backend_search_newest (store_array, state_local, recent, &error);
				if (array == NULL) {
					pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get packages: %s", error->message);
					g_error_free (error);
					goto out;
				}
			} else if (g_strcmp0 (search[0], "collections") == 0) {
				array = pk_backend_search_collections (store_array, state_local, &error);
				if (array == NULL) {
					pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get packages: %s", error->message);
					g_error_free (error);
					goto out;
				}
			} else {
				array = zif_store_array_search_group (store_array, search, state_local, &error);
			}
		} else if (role == PK_ROLE_ENUM_SEARCH_FILE) {
			array = zif_store_array_search_file (store_array, search, state_local, &error);
		} else if (role == PK_ROLE_ENUM_RESOLVE) {
			if (search[0][0] == '@') {
				/* this is a group */
				array = pk_backend_resolve_groups (store_array, search, state_local, &error);
			} else {
				array = zif_store_array_resolve (store_array, search, state_local, &error);
			}
		} else if (role == PK_ROLE_ENUM_WHAT_PROVIDES) {
			array = pk_backend_what_provides_helper (store_array, search, state_local, &error);
		}
		if (array == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to search: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* filter */
	result = pk_backend_filter_package_array (array, filters);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	pk_backend_set_percentage (backend, 100);

	/* emit */
	state_local = zif_state_get_child (priv->state);
	pk_backend_emit_package_array (backend, result, state_local);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	pk_backend_finished (backend);
#endif
	return TRUE;
}

/**
 * pk_backend_enable_media_repo:
 */
static void
pk_backend_enable_media_repo (gboolean enabled)
{
#ifdef HAVE_ZIF
	ZifStoreRemote *repo = NULL;
	gboolean ret;
	GError *error = NULL;
	ZifState *state;

	/* find the right repo */
	state = zif_state_new ();
	zif_state_set_cancellable (state, zif_state_get_cancellable (priv->state));
	repo = zif_repos_get_store (priv->repos, "InstallMedia", state, &error);
	if (repo == NULL) {
		g_debug ("failed to find install-media repo: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the state */
	ret = zif_store_remote_set_enabled (repo, enabled, &error);
	if (!ret) {
		g_debug ("failed to set enable: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_debug ("%s InstallMedia", enabled ? "enabled" : "disabled");
out:
	g_object_unref (state);
	if (repo != NULL)
		g_object_unref (repo);
#else
	GKeyFile *keyfile;
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;

	/* load */
	keyfile = g_key_file_new ();
	ret = g_key_file_load_from_file (keyfile, PACKAGE_MEDIA_REPO_FILENAME,
					 G_KEY_FILE_KEEP_COMMENTS, &error);
	if (!ret) {
		g_debug ("failed to load %s: %s",
			 PACKAGE_MEDIA_REPO_FILENAME,
			 error->message);
		g_error_free (error);
		goto out;
	}

	/* set data */
	g_key_file_set_integer (keyfile, "InstallMedia", "enabled", enabled);
	data = g_key_file_to_data (keyfile, NULL, &error);
	if (data == NULL) {
		g_warning ("failed to get data: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save */
	ret = g_file_set_contents (PACKAGE_MEDIA_REPO_FILENAME, data, -1, &error);
	if (!ret) {
		g_warning ("failed to save %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_debug ("%s InstallMedia", enabled ? "enabled" : "disabled");
out:
	g_free (data);
	g_key_file_free (keyfile);
#endif
}

/**
 * pk_backend_mount_add:
 */
static void
pk_backend_mount_add (GMount *mount, gpointer user_data)
{
	GFile *root;
	GFile *repo;
	GFile *dest;
	gchar *root_path;
	gchar *repo_path;
	gboolean ret;
	GError *error = NULL;

	/* check if any installed media is an install disk */
	root = g_mount_get_root (mount);
	root_path = g_file_get_path (root);
	repo_path = g_build_filename (root_path, "media.repo", NULL);
	repo = g_file_new_for_path (repo_path);
	dest = g_file_new_for_path (PACKAGE_MEDIA_REPO_FILENAME);

	/* media.repo exists */
	ret = g_file_query_exists (repo, NULL);
	g_debug ("checking for %s: %s", repo_path, ret ? "yes" : "no");
	if (!ret)
		goto out;

	/* copy to the system repo dir */
	ret = g_file_copy (repo, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
	if (!ret) {
		g_warning ("failed to copy: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (root_path);
	g_free (repo_path);
	g_object_unref (dest);
	g_object_unref (root);
	g_object_unref (repo);
}

/**
 * pk_backend_finished_cb:
 **/
static void
pk_backend_finished_cb (PkBackend *backend, PkExitEnum exit_enum, gpointer user_data)
{
	/* disable media repo */
	pk_backend_enable_media_repo (FALSE);
}

/**
 * pk_backend_status_changed_cb:
 **/
static void
pk_backend_status_changed_cb (PkBackend *backend, PkStatusEnum status, gpointer user_data)
{
	if (status != PK_STATUS_ENUM_WAIT)
		return;

	/* enable media repo */
	pk_backend_enable_media_repo (TRUE);
}

#ifdef HAVE_ZIF
/**
 * pk_backend_state_action_changed_cb:
 **/
static void
pk_backend_state_action_changed_cb (ZifState *state, ZifStateAction action, const gchar *action_hint, PkBackend *backend)
{
	PkStatusEnum status = PK_STATUS_ENUM_UNKNOWN;

	/* ignore this */
	if (action == ZIF_STATE_ACTION_UNKNOWN)
		goto out;

	/* try to map the ZifStateAction to a PkStatusEnum */
	if (action == ZIF_STATE_ACTION_DOWNLOADING) {
		if (g_strrstr (action_hint, "repomd") != NULL)
			status = PK_STATUS_ENUM_DOWNLOAD_REPOSITORY;
		else if (g_strrstr (action_hint, "primary") != NULL)
			status = PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST;
		else if (g_strrstr (action_hint, "filelist") != NULL)
			status = PK_STATUS_ENUM_DOWNLOAD_FILELIST;
		else if (g_strrstr (action_hint, "changelog") != NULL)
			status = PK_STATUS_ENUM_DOWNLOAD_CHANGELOG;
		else if (g_strrstr (action_hint, "comps") != NULL)
			status = PK_STATUS_ENUM_DOWNLOAD_GROUP;
		else if (g_strrstr (action_hint, "updatinfo") != NULL)
			status = PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO;
		goto out;
	}

	/* general cache loading */
	if (action == ZIF_STATE_ACTION_CHECKING ||
	    action == ZIF_STATE_ACTION_LOADING_REPOS ||
	    action == ZIF_STATE_ACTION_DECOMPRESSING) {
		status = PK_STATUS_ENUM_LOADING_CACHE;
		goto out;
	}
out:
	if (status != PK_STATUS_ENUM_UNKNOWN)
		pk_backend_set_status (backend, status);
}
#endif

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	gboolean ret;
	GFile *file = NULL;
	GError *error = NULL;
	GKeyFile *key_file = NULL;
	gchar *config_file = NULL;
	GList *mounts;
	gchar *use_zif = NULL;

	/* use logging */
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	pk_debug_add_log_domain ("Zif");

	/* create private area */
	priv = g_new0 (PkBackendYumPrivate, 1);

	/* connect to finished, so we can clean up */
	priv->signal_finished =
		g_signal_connect (backend, "finished",
				  G_CALLBACK (pk_backend_finished_cb), NULL);
	priv->signal_status =
		g_signal_connect (backend, "status-changed",
				  G_CALLBACK (pk_backend_status_changed_cb), NULL);

	g_debug ("backend: initialize");
	priv->spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_filter_stderr (priv->spawn, pk_backend_stderr_cb);
	pk_backend_spawn_set_filter_stdout (priv->spawn, pk_backend_stdout_cb);
	pk_backend_spawn_set_name (priv->spawn, "yum");
	pk_backend_spawn_set_allow_sigkill (priv->spawn, FALSE);

	/* coldplug the mounts */
	priv->volume_monitor = g_volume_monitor_get ();
	mounts = g_volume_monitor_get_mounts (priv->volume_monitor);
	g_list_foreach (mounts, (GFunc) pk_backend_mount_add, NULL);
	g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
	g_list_free (mounts);

	/* setup a file monitor on the repos directory */
	file = g_file_new_for_path (YUM_REPOS_DIRECTORY);
	priv->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, &error);
	if (priv->monitor != NULL) {
		g_signal_connect (priv->monitor, "changed", G_CALLBACK (pk_backend_yum_repos_changed_cb), backend);
	} else {
		g_warning ("failed to setup monitor: %s", error->message);
		g_error_free (error);
	}

	/* read the config file */
	key_file = g_key_file_new ();
	config_file = g_build_filename (SYSCONFDIR, "PackageKit", "Yum.conf", NULL);
	g_debug ("loading configuration from %s", config_file);
	ret = g_key_file_load_from_file (key_file, config_file, G_KEY_FILE_NONE, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "failed to load Yum.conf: %s", error->message);
		g_error_free (error);
		goto out;
	}

	#ifdef HAVE_ZIF
	/* it seems some people are not ready for the awesomeness */
	use_zif = g_key_file_get_string (key_file, "Backend", "UseZif", NULL);
	if (use_zif != NULL) {
		priv->use_zif = pk_role_bitfield_from_string (use_zif);
		if (priv->use_zif == 0)
			g_warning ("failed to parse UseZif '%s'", use_zif);
	}
	g_debug ("UseZif=%s (%i)", use_zif, (gint)priv->use_zif);

	/* use a timer for profiling */
	priv->timer = g_timer_new ();

	/* init rpm */
	zif_init ();

	/* profile */
	pk_backend_profile ("zif init");

	/* TODO: hook up errors */
	priv->cancellable = g_cancellable_new ();

	/* ZifState */
	priv->state = zif_state_new ();
	g_signal_connect (priv->state, "percentage-changed", G_CALLBACK (pk_backend_state_percentage_changed_cb), backend);
	g_signal_connect (priv->state, "subpercentage-changed", G_CALLBACK (pk_backend_state_subpercentage_changed_cb), backend);
	g_signal_connect (priv->state, "action-changed", G_CALLBACK (pk_backend_state_action_changed_cb), backend);

	/* ZifConfig */
	priv->config = zif_config_new ();
	ret = zif_config_set_filename (priv->config, "/etc/yum.conf", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING, "failed to set config: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("read config_file");

	/* ZifLock */
	priv->lock = zif_lock_new ();

	/* ZifRelease */
	priv->release = zif_release_new ();
	zif_release_set_boot_dir (priv->release, "/boot/upgrade");
	zif_release_set_cache_dir (priv->release, "/var/cache/PackageKit");
	zif_release_set_repo_dir (priv->release, "/var/cache/yum/preupgrade");
	zif_release_set_uri (priv->release, "http://mirrors.fedoraproject.org/releases.txt");

	/* ZifStoreLocal */
	priv->store_local = zif_store_local_new ();

	/* profile */
	pk_backend_profile ("read local store");

	/* ZifRepos */
	priv->repos = zif_repos_new ();
	ret = zif_repos_set_repos_dir (priv->repos, "/etc/yum.repos.d", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "failed to set repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("read repos");

	/* ZifGroups */
	priv->groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (priv->groups, "/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_LIST_INVALID, "failed to set mapping file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("read groups");
#else
	priv->use_zif = FALSE;
#endif
out:
	g_free (use_zif);
	g_free (config_file);
	if (key_file != NULL)
		g_key_file_free (key_file);
	if (file != NULL)
		g_object_unref (file);
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
	g_object_unref (priv->spawn);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
	g_signal_handler_disconnect (backend, priv->signal_finished);
	g_signal_handler_disconnect (backend, priv->signal_status);
#ifdef HAVE_ZIF
	if (priv->config != NULL)
		g_object_unref (priv->config);
	if (priv->release != NULL)
		g_object_unref (priv->release);
	if (priv->state != NULL)
		g_object_unref (priv->state);
	if (priv->repos != NULL)
		g_object_unref (priv->repos);
	if (priv->groups != NULL)
		g_object_unref (priv->groups);
	if (priv->store_local != NULL)
		g_object_unref (priv->store_local);
	if (priv->lock != NULL)
		g_object_unref (priv->lock);
	if (priv->timer != NULL)
		g_timer_destroy (priv->timer);
#endif
	if (priv->volume_monitor != NULL)
		g_object_unref (priv->volume_monitor);
	g_free (priv);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
#ifdef HAVE_ZIF
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	const gchar *group_str;
#endif
	PkBitfield groups = 0;

	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, PK_ROLE_ENUM_GET_CATEGORIES)) {
		groups = pk_bitfield_from_enums (
			PK_GROUP_ENUM_COLLECTIONS,
			PK_GROUP_ENUM_NEWEST,
			PK_GROUP_ENUM_ADMIN_TOOLS,
			PK_GROUP_ENUM_DESKTOP_GNOME,
			PK_GROUP_ENUM_DESKTOP_KDE,
			PK_GROUP_ENUM_DESKTOP_XFCE,
			PK_GROUP_ENUM_DESKTOP_OTHER,
			PK_GROUP_ENUM_EDUCATION,
			PK_GROUP_ENUM_FONTS,
			PK_GROUP_ENUM_GAMES,
			PK_GROUP_ENUM_GRAPHICS,
			PK_GROUP_ENUM_INTERNET,
			PK_GROUP_ENUM_LEGACY,
			PK_GROUP_ENUM_LOCALIZATION,
			PK_GROUP_ENUM_MULTIMEDIA,
			PK_GROUP_ENUM_OFFICE,
			PK_GROUP_ENUM_OTHER,
			PK_GROUP_ENUM_PROGRAMMING,
			PK_GROUP_ENUM_PUBLISHING,
			PK_GROUP_ENUM_SERVERS,
			PK_GROUP_ENUM_SYSTEM,
			PK_GROUP_ENUM_VIRTUALIZATION,
			-1);
		goto out;
	}

#ifdef HAVE_ZIF
	/* get the dynamic group list */
	array = zif_groups_get_groups (priv->groups, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_LIST_INVALID, "failed to get the list of groups: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to a bitfield */
	for (i=0; i<array->len; i++) {
		group_str = g_ptr_array_index (array, i);
		pk_bitfield_add (groups, pk_group_enum_from_string (group_str));
	}
#endif

	/* add the virtual groups */
	pk_bitfield_add (groups, PK_GROUP_ENUM_COLLECTIONS);
	pk_bitfield_add (groups, PK_GROUP_ENUM_NEWEST);
out:
	return groups;
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_BASENAME,
		PK_FILTER_ENUM_FREE,
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_ARCH,
		-1);
}

/**
 * pk_backend_get_roles:
 */
PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	PkBitfield roles;
	roles = pk_bitfield_from_enums (
		PK_ROLE_ENUM_CANCEL,
		PK_ROLE_ENUM_GET_DEPENDS,
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_FILES,
		PK_ROLE_ENUM_GET_REQUIRES,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_WHAT_PROVIDES,
		PK_ROLE_ENUM_GET_UPDATES,
		PK_ROLE_ENUM_GET_UPDATE_DETAIL,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_INSTALL_FILES,
		PK_ROLE_ENUM_INSTALL_SIGNATURE,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
		PK_ROLE_ENUM_RESOLVE,
		PK_ROLE_ENUM_SEARCH_DETAILS,
		PK_ROLE_ENUM_SEARCH_FILE,
		PK_ROLE_ENUM_SEARCH_GROUP,
		PK_ROLE_ENUM_SEARCH_NAME,
		PK_ROLE_ENUM_UPDATE_PACKAGES,
		PK_ROLE_ENUM_UPDATE_SYSTEM,
		PK_ROLE_ENUM_GET_REPO_LIST,
		PK_ROLE_ENUM_REPO_ENABLE,
		PK_ROLE_ENUM_GET_CATEGORIES,
		PK_ROLE_ENUM_SIMULATE_INSTALL_FILES,
		PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES,
		PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES,
		PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES,
		-1);

#ifdef HAVE_ZIF
	pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	pk_bitfield_add (roles, PK_ROLE_ENUM_UPGRADE_SYSTEM);
#else
	/* only add GetDistroUpgrades if the binary is present */
	if (g_file_test (PREUPGRADE_BINARY, G_FILE_TEST_EXISTS))
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
#endif

	return roles;
}

/**
 * pk_backend_get_mime_types:
 */
gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm;application/x-servicepack");
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
#ifdef HAVE_ZIF
	/* try to cancel the thread first */
	g_cancellable_cancel (priv->cancellable);
#endif
	/* this feels bad... */
	pk_backend_spawn_kill (priv->spawn);
}

/**
 * pk_backend_download_packages_thread:
 */
static gboolean
pk_backend_download_packages_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	const gchar *directory = pk_backend_get_string (backend, "directory");
	GPtrArray *store_array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	GPtrArray *packages = NULL;
	const gchar *id;
	guint i;
	guint len;
	gboolean ret;
	GError *error = NULL;
	const gchar *filename;
	gchar *basename;
	gchar *path;

	len = g_strv_length (package_ids);
	zif_state_set_number_steps (priv->state, (len * 4) + 1);

	/* find all the packages */
	packages = g_ptr_array_new ();
	state_local = zif_state_get_child (priv->state);
	store_array = pk_backend_get_default_store_array_for_filter (backend, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), state_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];
		state_local = zif_state_get_child (priv->state);
		package = zif_store_array_find_package (store_array, id, state_local, &error);
		if (package == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "failed to find %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		g_ptr_array_add (packages, g_object_ref (package));
		g_object_unref (package);
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* download list */
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);

		/* get filename */
		state_local = zif_state_get_child (priv->state);
		filename = zif_package_get_filename (package, state_local, &error);
		if (filename == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
					       "failed to get filename for %s: %s", zif_package_get_id (package), error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* download */
		state_local = zif_state_get_child (priv->state);
		ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package), directory, state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
					       "failed to download %s: %s", filename, error->message);
			g_error_free (error);
			goto out;
		}

		/* send a signal for the daemon so the file is copied */
		basename = g_path_get_basename (filename);
		path = g_build_filename (directory, basename, NULL);
		pk_backend_files (backend, zif_package_get_id (package), path);
		g_free (basename);
		g_free (path);

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	pk_backend_finished (backend);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
#endif
	return TRUE;
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *package_ids_temp;

		/* send the complete list as stdin */
		package_ids_temp = pk_package_ids_to_string (package_ids);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "download-packages", directory, package_ids_temp, NULL);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_download_packages_thread);
}

/**
 * pk_backend_get_depends_thread:
 */
static gboolean
pk_backend_get_depends_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	GPtrArray *store_array = NULL;
	ZifPackage *package;
	ZifPackage *package_provide;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_loop_inner;
	ZifDepend *require;
	const gchar *id;
	guint i, j, k;
	guint len;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *result;
	GPtrArray *requires;
	GPtrArray *provides;

	len = g_strv_length (package_ids);

	zif_state_set_number_steps (priv->state, len + 3);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	store_array = pk_backend_get_default_store_array_for_filter (backend, 0, state_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* new output array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];

		/* set up state */
		state_local = zif_state_get_child (priv->state);
		zif_state_set_number_steps (state_local, 2);

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array, id, state_loop, &error);
		if (package == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "failed to find %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get requires */
		state_loop = zif_state_get_child (state_local);
		requires = zif_package_get_requires (package, state_loop, &error);
		if (requires == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to get requires for %s: %s",
					       package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* match a package to each require */
		state_loop = zif_state_get_child (state_local);
		zif_state_set_number_steps (state_loop, requires->len);
		for (k=0; k<requires->len; k++) {

			/* setup deeper state */
			state_loop_inner = zif_state_get_child (state_loop);

			require = g_ptr_array_index (requires, k);

			/* find the package providing the depend */
			provides = zif_store_array_what_provides (store_array, require, state_loop_inner, &error);
			if (provides == NULL) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						       "failed to find provide for %s: %s",
						       zif_depend_get_name (require), error->message);
				g_error_free (error);
				goto out;
			}

			/* print all of them */
			for (j=0;j<provides->len;j++) {
				package_provide = g_ptr_array_index (provides, j);
				g_ptr_array_add (array, g_object_ref (package_provide));
			}
			g_ptr_array_unref (provides);

			/* this section done */
			ret = zif_state_done (state_loop, &error);
			if (!ret)
				goto out;
		}

		/* free */
		g_object_unref (package);
	}

	/* filter */
	result = pk_backend_filter_package_array (array, filters);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	pk_backend_set_percentage (backend, 100);

	/* emit */
	state_local = zif_state_get_child (priv->state);
	pk_backend_emit_package_array (backend, result, state_local);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
#endif
	return TRUE;
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		gchar *package_ids_temp;
		package_ids_temp = pk_package_ids_to_string (package_ids);
		filters_text = pk_filter_bitfield_to_string (filters);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-depends", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
		g_free (filters_text);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_get_depends_thread);
}

/**
 * pk_backend_get_details_thread:
 */
static gboolean
pk_backend_get_details_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GPtrArray *store_array = NULL;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	const gchar *id;
	guint i;
	guint len;
	GError *error = NULL;
	const gchar *license;
	const gchar *description;
	const gchar *url;
	const gchar *group_str;
	PkGroupEnum group;
	guint64 size;
	PkBitfield filters = PK_FILTER_ENUM_UNKNOWN;

	len = g_strv_length (package_ids);

	zif_state_set_number_steps (priv->state, len + 1);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	if (pk_backend_is_all_installed (package_ids))
		pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	store_array = pk_backend_get_default_store_array_for_filter (backend, filters, state_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];

		/* set up state */
		state_local = zif_state_get_child (priv->state);
		zif_state_set_number_steps (state_local, 6);

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array, id, state_loop, &error);
		if (package == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "failed to find %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get license */
		state_loop = zif_state_get_child (state_local);
		license = zif_package_get_license (package, state_loop, NULL);

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get group */
		state_loop = zif_state_get_child (state_local);
		group_str = zif_package_get_group (package, state_loop, &error);

		/* not being in a group is non-fatal */
		if (group_str == NULL) {
			g_warning ("failed to get group: %s", error->message);
			g_clear_error (&error);
		}
		group = pk_group_enum_from_text (group_str);

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get description */
		state_loop = zif_state_get_child (state_local);
		description = zif_package_get_description (package, state_loop, NULL);

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get url */
		state_loop = zif_state_get_child (state_local);
		url = zif_package_get_url (package, state_loop, NULL);

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get size */
		state_loop = zif_state_get_child (state_local);
		size = zif_package_get_size (package, state_loop, NULL);

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* emit */
		pk_backend_details (backend,
				    package_ids[i],
				    license,
				    group,
				    description,
				    url,
				    (gulong) size);

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* free */
		g_object_unref (package);
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
#endif
	return TRUE;
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	/* check if we can use zif */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *package_ids_temp;
		package_ids_temp = pk_package_ids_to_string (package_ids);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-details", package_ids_temp, NULL);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_get_details_thread);
}

/**
  * pk_backend_get_distro_upgrades_thread:
  */
static gboolean
pk_backend_get_distro_upgrades_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gchar *distro_id;
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	guint version;
	ZifUpgrade *upgrade;

	/* one shot */
	zif_state_reset (priv->state);

	/* get the current version */
	version = zif_config_get_uint (priv->config, "releasever", NULL);
	if (version == G_MAXUINT) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				       "could not get distro present version");
		goto out;
	}

	/* get the upgrades */
	array = zif_release_get_upgrades_new (priv->release, version, priv->state, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				       "could not get latest distro data : %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* emit the results */
	for (i=0; i<array->len; i++) {
		upgrade = g_ptr_array_index (array, i);
		if (!zif_upgrade_get_enabled (upgrade))
			continue;
		distro_id = g_strdup_printf ("fedora-%i", zif_upgrade_get_version (upgrade));
		pk_backend_distro_upgrade (backend,
					   zif_upgrade_get_stable (upgrade) ? PK_DISTRO_UPGRADE_ENUM_STABLE :
									      PK_DISTRO_UPGRADE_ENUM_UNSTABLE,
					   distro_id,
					   zif_upgrade_get_id (upgrade));
		g_free (distro_id);
	}
out:
	pk_backend_finished (backend);
	if (array != NULL)
		g_ptr_array_unref (array);
#endif
	return TRUE;
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-distro-upgrades", NULL);
		return;
	}

	pk_backend_thread_create (backend, pk_backend_get_distro_upgrades_thread);
}

/**
 * pk_backend_get_files_thread:
 */
static gboolean
pk_backend_get_files_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GPtrArray *store_array = NULL;
	ZifPackage *package;
	GPtrArray *files;
	ZifState *state_local;
	const gchar *id;
	guint i, j;
	guint len;
	GError *error = NULL;
	const gchar *file;
	GString *files_str;
	PkBitfield filters = PK_FILTER_ENUM_UNKNOWN;

	/* reset */
	pk_backend_profile (NULL);

	len = g_strv_length (package_ids);

	zif_state_set_number_steps (priv->state, (len * 2) + 1);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	if (pk_backend_is_all_installed (package_ids))
		pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	store_array = pk_backend_get_default_store_array_for_filter (backend, filters, state_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("add local");

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];
		state_local = zif_state_get_child (priv->state);
		package = zif_store_array_find_package (store_array, id, state_local, &error);
		if (package == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "failed to find %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* profile */
		pk_backend_profile ("find package");

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get files */
		state_local = zif_state_get_child (priv->state);
		files = zif_package_get_files (package, state_local, &error);
		if (files == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "no files for %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* profile */
		pk_backend_profile ("get files");

		files_str = g_string_new ("");
		for (j=0; j<files->len; j++) {
			file = g_ptr_array_index (files, j);
			g_string_append_printf (files_str, "%s\n", file);
		}
		pk_backend_files (backend, package_ids[i], files_str->str);

		/* profile */
		pk_backend_profile ("emit files");

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		g_string_free (files_str, TRUE);
		g_object_unref (package);
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
#endif
	return TRUE;
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		package_ids_temp = pk_package_ids_to_string (package_ids);
		pk_backend_spawn_helper (priv->spawn,  "yumBackend.py", "get-files", package_ids_temp, NULL);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_get_files_thread);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-requires", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_updates_thread:
 */
static gboolean
pk_backend_get_updates_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
#if 0
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	GPtrArray *store_array = NULL;
	ZifState *state_local;
	ZifState *state_loop;
	GPtrArray *array = NULL;
	GPtrArray *result = NULL;
	GPtrArray *packages = NULL;
	gboolean ret;
	GError *error = NULL;
	ZifUpdate *update;
	ZifPackage *package;
	PkInfoEnum info;
	ZifUpdateKind update_kind;
	guint i;
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* reset */
	pk_backend_profile (NULL);

	zif_state_set_number_steps (priv->state, 5);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("get remote stores");

	/* get all the installed packages */
	state_local = zif_state_get_child (priv->state);
	packages = zif_store_get_packages (priv->store_local, state_local, &error);
	if (packages == NULL) {
		g_print ("failed to get local store: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_debug ("searching for updates with %i packages", packages->len);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("get installed packages");

	/* remove any packages that are not newest (think kernel) */
	zif_package_array_filter_newest (packages);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("filter installed newest");

	/* get updates */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_error_handler (priv->state, (ZifStateErrorHandlerCb) pk_backend_error_handler_cb, backend);
	array = zif_store_array_get_updates (store_array, packages, state_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get updates: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("get updates of packages");

	/* setup steps on updatinfo state */
	state_local = zif_state_get_child (priv->state);
	if (array->len > 0)
		zif_state_set_number_steps (state_local, array->len);

	/* get update info */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		state_loop = zif_state_get_child (state_local);

		/* updates without updatinfo */
		info = PK_INFO_ENUM_NORMAL;

		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state_loop, &error);
		if (update == NULL) {
			g_debug ("failed to get updateinfo for %s", zif_package_get_id (package));
			g_clear_error (&error);
			ret = zif_state_finished (state_loop, NULL);
		} else {
			update_kind = zif_update_get_kind (update);
			if (update_kind == ZIF_UPDATE_KIND_BUGFIX)
				info = PK_INFO_ENUM_BUGFIX;
			else if (update_kind == ZIF_UPDATE_KIND_SECURITY)
				info = PK_INFO_ENUM_SECURITY;
			else if (update_kind == ZIF_UPDATE_KIND_ENHANCEMENT)
				info = PK_INFO_ENUM_ENHANCEMENT;
			g_object_unref (update);
		}

		/* set new severity */
		g_object_set_data (G_OBJECT(package), "kind", (gpointer)pk_info_enum_to_string (info));

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	pk_backend_profile ("get updateinfo");

	/* filter */
	result = pk_backend_filter_package_array (array, filters);

	/* done */
	pk_backend_set_percentage (backend, 100);

	/* emit */
	state_local = zif_state_get_child (priv->state);
	pk_backend_emit_package_array (backend, result, state_local);

	/* profile */
	pk_backend_profile ("filter and emit");

out:
	pk_backend_finished (backend);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (result != NULL)
		g_ptr_array_unref (result);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
#endif
#endif
	return TRUE;
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		filters_text = pk_filter_bitfield_to_string (filters);
		pk_backend_spawn_helper (priv->spawn,  "yumBackend.py", "get-updates", filters_text, NULL);
		g_free (filters_text);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_get_updates_thread);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		filters_text = pk_filter_bitfield_to_string (filters);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-packages", filters_text, NULL);
		g_free (filters_text);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

#ifdef HAVE_ZIF
/**
 * pk_backend_get_changelog_text:
 */
static gchar *
pk_backend_get_changelog_text (GPtrArray *changesets)
{
	guint i;
	ZifChangeset *changeset;
	GString *text;
	gchar date_str[128];
	GDate *date;

	/* create output string */
	text = g_string_new ("");
	date = g_date_new ();

	/* go through each one */
	for (i=0; i<changesets->len; i++) {
		changeset = g_ptr_array_index (changesets, i);

		/* format the indervidual changeset */
		g_date_set_time_t (date, zif_changeset_get_date (changeset));
		g_date_strftime (date_str, 128, "%F", date);
		g_string_append_printf (text, "**%s** %s - %s\n%s\n\n",
					date_str,
					zif_changeset_get_author (changeset),
					zif_changeset_get_version (changeset),
					zif_changeset_get_description (changeset));
	}
	g_date_free (date);
	return g_string_free (text, FALSE);
}
#endif

/**
 * pk_backend_get_update_detail_thread:
 */
static gboolean
pk_backend_get_update_detail_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gchar **package_ids;
	guint i;
	guint j;
	gboolean ret;
	ZifUpdate *update;
	ZifState *state_local;
	ZifPackage *package;
	GError *error = NULL;

	/* reset */
	pk_backend_profile (NULL);

	/* get the data */
	package_ids = pk_backend_get_strv (backend, "package_ids");
	zif_state_set_number_steps (priv->state, g_strv_length (package_ids));

	/* get the update info */
	for (i=0; package_ids[i] != NULL; i++) {

		package = zif_package_new ();
		ret = zif_package_set_id (package, package_ids[i], &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR,
					       "failed to set id: %s", error->message);
			g_error_free (error);
			goto out;
		}

		state_local = zif_state_get_child (priv->state);
		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state_local, &error);
		if (update == NULL) {
			g_debug ("failed to get updateinfo for %s", zif_package_get_id (package));
			g_clear_error (&error);
		} else {
			gchar *changelog_text = NULL;
			GPtrArray *array;
			GPtrArray *changesets;
			GString *string_cve;
			GString *string_bugzilla;
			ZifUpdateInfo *info;
			array = zif_update_get_update_infos (update);
			string_cve = g_string_new (NULL);
			string_bugzilla = g_string_new (NULL);
			for (j=0; j<array->len; j++) {
				info = g_ptr_array_index (array, j);
				switch (zif_update_info_get_kind (info)) {
				case ZIF_UPDATE_INFO_KIND_CVE:
					g_string_append_printf (string_cve, "%s\t%s\t",
								zif_update_info_get_title (info),
								zif_update_info_get_url (info));
					break;
				case ZIF_UPDATE_INFO_KIND_BUGZILLA:
					g_string_append_printf (string_bugzilla, "%s\t%s\t",
								zif_update_info_get_title (info),
								zif_update_info_get_url (info));
					break;
				default:
					break;
				}
			}

			/* format changelog */
			changesets = zif_update_get_changelog (update);
			if (changesets != NULL)
				changelog_text = pk_backend_get_changelog_text (changesets);
			pk_backend_update_detail (backend, package_ids[i],
						  NULL, //updates,
						  NULL, //obsoletes,
						  NULL, //vendor_url,
						  string_bugzilla->str,
						  string_cve->str,
						  PK_RESTART_ENUM_NONE,
						  zif_update_get_description (update),
						  changelog_text,
						  zif_update_get_state (update),
						  zif_update_get_issued (update),
						  NULL);
			if (changesets != NULL)
				g_ptr_array_unref (changesets);
			g_ptr_array_unref (array);
			g_string_free (string_cve, TRUE);
			g_string_free (string_bugzilla, TRUE);
			g_free (changelog_text);
		}

		g_object_unref (package);

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	pk_backend_finished (backend);
#endif
	return TRUE;
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *package_ids_temp;
		package_ids_temp = pk_package_ids_to_string (package_ids);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-update-detail", package_ids_temp, NULL);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_get_update_detail_thread);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "install-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_simulate_remove_packages:
 */
void
pk_backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-remove-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_simulate_update_packages:
 */
void
pk_backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-update-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_simulate_install_packages:
 */
void
pk_backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-install-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "install-files", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkSigTypeEnum type,
			   const gchar *key_id, const gchar *package_id)
{
	const gchar *type_text;

	type_text = pk_sig_type_enum_to_string (type);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "install-signature", type_text, key_id, package_id, NULL);
}

/**
 * pk_backend_refresh_cache_thread:
 */
static gboolean
pk_backend_refresh_cache_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	GPtrArray *store_array = NULL;
	gboolean ret;
	GError *error = NULL;
	ZifState *state_local;
	gboolean force = pk_backend_get_bool (backend, "force");

	zif_state_set_number_steps (priv->state, 2);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* don't nuke the metadata */
	if (!force) {
		g_debug ("not supported yet");
		goto out;
	}

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array, state_local, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to add enabled stores: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* clean all the repos */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_error_handler (priv->state, (ZifStateErrorHandlerCb) pk_backend_error_handler_cb, backend);
	ret = zif_store_array_clean (store_array, state_local, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to clean: %s\n", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
#endif
	return TRUE;
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "refresh-cache", pk_backend_bool_to_string (force), NULL);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_refresh_cache_thread);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "remove-packages", pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string (autoremove), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-details", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-file", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-group", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-name", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "update-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "update-system", pk_backend_bool_to_string (only_trusted), NULL);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		gchar *package_ids_temp;
		filters_text = pk_filter_bitfield_to_string (filters);
		package_ids_temp = pk_package_ids_to_string (packages);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "resolve", filters_text, package_ids_temp, NULL);
		g_free (filters_text);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_set_strv (backend, "search", packages);
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_get_repo_list_thread:
 */
static gboolean
pk_backend_get_repo_list_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	guint i;
	GPtrArray *array = NULL;
	ZifStoreRemote *store;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_tmp;
	const gchar *repo_id;
	const gchar *name;
	gboolean enabled;
	gboolean devel;
	GError *error = NULL;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	zif_state_set_number_steps (priv->state, 2);

	state_local = zif_state_get_child (priv->state);
	array = zif_repos_get_stores (priv->repos, state_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "failed to find repos: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* none? */
	if (array->len == 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "failed to find any repos");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* setup state */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);

	/* looks at each store */
	for (i=0; i<array->len; i++) {
		store = g_ptr_array_index (array, i);

		/* allow filtering on devel */
		state_loop = zif_state_get_child (state_local);
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {

			/* devel, name, enabled */
			zif_state_set_number_steps (state_loop, 3);

			state_tmp = zif_state_get_child (state_loop);
			devel = zif_store_remote_is_devel (store, state_tmp, NULL);
			if (devel)
				goto skip;

			/* this section done */
			ret = zif_state_done (state_loop, &error);
			if (!ret) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
				g_error_free (error);
				goto out;
			}
		} else {
			/* name, enabled */
			zif_state_set_number_steps (state_loop, 2);
		}

		/* get name */
		state_tmp = zif_state_get_child (state_loop);
		name = zif_store_remote_get_name (store, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		/* get state */
		state_tmp = zif_state_get_child (state_loop);
		enabled = zif_store_remote_get_enabled (store, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}

		repo_id = zif_store_get_id (ZIF_STORE (store));
		pk_backend_repo_detail (backend, repo_id, name, enabled);
skip:
		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (array != NULL)
		g_ptr_array_unref (array);
#endif
	return TRUE;
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		filters_text = pk_filter_bitfield_to_string (filters);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-repo-list", filters_text, NULL);
		g_free (filters_text);
		return;
	}

	pk_backend_thread_create (backend, pk_backend_get_repo_list_thread);
}

/**
 * pk_backend_repo_enable_thread:
 */
static gboolean
pk_backend_repo_enable_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	ZifStoreRemote *repo = NULL;
	gboolean ret;
	GError *error = NULL;
	gchar *warning = NULL;
	gboolean enabled = pk_backend_get_bool (backend, "enabled");
	const gchar *repo_id = pk_backend_get_string (backend, "repo_id");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* find the right repo */
	repo = zif_repos_get_store (priv->repos, repo_id, priv->state, &error);
	if (repo == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "failed to find repo: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the state */
	ret = zif_store_remote_set_enabled (repo, enabled, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_DISABLE_REPOSITORY, "failed to set enable: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* warn if rawhide */
	if (g_strstr_len (repo_id, -1, "rawhide") != NULL) {
		warning = g_strdup_printf ("These packages are untested and still under development."
					   "This repository is used for development of new releases.\n\n"
					   "This repository can see significant daily turnover and major "
					   "functionality changes which cause unexpected problems with "
					   "other development packages.\n"
					   "Please use these packages if you want to work with the "
					   "Fedora developers by testing these new development packages.\n\n"
					   "If this is not correct, please disable the %s software source.", repo_id);
		pk_backend_message (backend, PK_MESSAGE_ENUM_REPO_FOR_DEVELOPERS_ONLY, warning);
	}
out:
	pk_backend_finished (backend);
	g_free (warning);
	if (repo != NULL)
		g_object_unref (repo);
#endif
	return TRUE;
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *repo_id, gboolean enabled)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		if (enabled == TRUE) {
			pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "repo-enable", repo_id, "true", NULL);
		} else {
			pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "repo-enable", repo_id, "false", NULL);
		}
		return;
	}
	pk_backend_thread_create (backend, pk_backend_repo_enable_thread);
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	guint i;
	guint len;
	gchar **search = NULL;
	GPtrArray *array = NULL;
	gchar *search_tmp;

	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		gchar *filters_text;
		const gchar *provides_text;
		provides_text = pk_provides_enum_to_string (provides);
		filters_text = pk_filter_bitfield_to_string (filters);
		search_tmp = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "what-provides", filters_text, provides_text, search_tmp, NULL);
		g_free (filters_text);
		g_free (search_tmp);
		return;
	}

	/* iter on each provide string, and wrap it with the fedora prefix */
	len = g_strv_length (values);
	array = g_ptr_array_new_with_free_func (g_free);
	for (i=0; i<len; i++) {
		/* compatibility with previous versions of GPK */
		if (g_str_has_prefix (values[i], "gstreamer0.10(")) {
			g_ptr_array_add (array, g_strdup (values[i]));
		} else if (provides == PK_PROVIDES_ENUM_CODEC) {
			g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_FONT) {
			g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_MIMETYPE) {
			g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_POSTSCRIPT_DRIVER) {
			g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_ANY) {
			g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		} else {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED,
					       "provide type %s not supported", pk_provides_enum_to_string (provides));
		}
	}

	/* set the search terms and run */
	search = pk_ptr_array_to_strv (array);
	pk_backend_set_strv (backend, "search", search);
	pk_backend_thread_create (backend, pk_backend_search_thread);
	g_strfreev (search);
	g_ptr_array_unref (array);
}

/**
 * pk_backend_get_categories_thread:
 */
static gboolean
pk_backend_get_categories_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gboolean ret;
	guint i;
	GPtrArray *array = NULL;
	GPtrArray *stores = NULL;
	ZifCategory *cat;
	gchar *cat_id;

	ZifState *state_local;
	GError *error = NULL;

	zif_state_set_number_steps (priv->state, 3);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* get enabled repos */
	state_local = zif_state_get_child (priv->state);
	stores = zif_repos_get_stores_enabled (priv->repos, state_local, &error);
	if (stores == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "failed to add remote stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get sorted list of unique categories */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_error_handler (priv->state, (ZifStateErrorHandlerCb) pk_backend_error_handler_cb, backend);
	array = zif_store_array_get_categories (stores, state_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_LIST_INVALID, "failed to add get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* emit each cat obj */
	for (i=0; i<array->len; i++) {
		cat = g_ptr_array_index (array, i);
		/* FIXME: in the yum backend, we signify a group with a '@' prefix */
		if (zif_category_get_parent_id (cat) != NULL)
			cat_id = g_strdup_printf ("@%s", zif_category_get_id (cat));
		else
			cat_id = g_strdup (zif_category_get_id (cat));
		pk_backend_category (backend,
				     zif_category_get_parent_id (cat),
				     cat_id,
				     zif_category_get_name (cat),
				     zif_category_get_summary (cat),
				     zif_category_get_icon (cat));
		g_free (cat_id);
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "cancelled: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (stores != NULL)
		g_ptr_array_unref (stores);
#endif
	return TRUE;
}

/**
 * pk_backend_get_categories:
 */
void
pk_backend_get_categories (PkBackend *backend)
{
	/* it seems some people are not ready for the awesomeness */
	if (!pk_bitfield_contain (priv->use_zif, pk_backend_get_role (backend))) {
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-categories", NULL);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_get_categories_thread);
}

/**
 * pk_backend_simulate_install_files:
 */
void
pk_backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-install-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
  * pk_backend_upgrade_system_thread:
  */
static gboolean
pk_backend_upgrade_system_thread (PkBackend *backend)
{
#ifdef HAVE_ZIF
	gchar **distro_id_split = NULL;
	guint version;
	gboolean ret;
	PkErrorEnum error_code;
	GError *error = NULL;
	ZifReleaseUpgradeKind upgrade_kind_zif = ZIF_RELEASE_UPGRADE_KIND_DEFAULT;
	PkUpgradeKindEnum upgrade_kind = pk_backend_get_uint (backend, "upgrade_kind");
	const gchar *distro_id = pk_backend_get_string (backend, "distro_id");

	/* check valid */
	distro_id_split = g_strsplit (distro_id, "-", -1);
	if (g_strv_length (distro_id_split) != 2) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				       "distribution id %s invalid", distro_id);
		goto out;
	}

	/* check fedora */
	if (g_strcmp0 (distro_id_split[0], "fedora") != 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				       "only 'fedora' is supported");
		goto out;
	}

	/* map PK enum to Zif enumerated types */
	if (upgrade_kind == PK_UPGRADE_KIND_ENUM_MINIMAL)
		upgrade_kind_zif = ZIF_RELEASE_UPGRADE_KIND_MINIMAL;
	else if (upgrade_kind == PK_UPGRADE_KIND_ENUM_COMPLETE)
		upgrade_kind_zif = ZIF_RELEASE_UPGRADE_KIND_COMPLETE;

	/* do the upgrade */
	version = atoi (distro_id_split[1]);
	ret = zif_release_upgrade_version (priv->release,
					   version,
					   upgrade_kind_zif,
					   priv->state,
					   &error);
	if (!ret) {
		/* convert the ZifRelease error code into a PK error enum */
		switch (error->code) {
		case ZIF_RELEASE_ERROR_DOWNLOAD_FAILED:
			error_code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
			break;
		case ZIF_RELEASE_ERROR_FILE_INVALID:
			error_code = PK_ERROR_ENUM_FAILED_CONFIG_PARSING;
			break;
		case ZIF_RELEASE_ERROR_LOW_DISKSPACE:
			error_code = PK_ERROR_ENUM_NO_SPACE_ON_DEVICE;
			break;
		case ZIF_RELEASE_ERROR_NOT_FOUND:
			error_code = PK_ERROR_ENUM_PACKAGE_NOT_FOUND;
			break;
		case ZIF_RELEASE_ERROR_NOT_SUPPORTED:
			error_code = PK_ERROR_ENUM_NOT_SUPPORTED;
			break;
		case ZIF_RELEASE_ERROR_NO_UUID_FOR_ROOT:
		case ZIF_RELEASE_ERROR_SETUP_INVALID:
		case ZIF_RELEASE_ERROR_SPAWN_FAILED:
		case ZIF_RELEASE_ERROR_WRITE_FAILED:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
		pk_backend_error_code (backend, error_code,
				       "failed to upgrade: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	g_strfreev (distro_id_split);
#endif
	return TRUE;
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
	pk_backend_thread_create (backend, pk_backend_upgrade_system_thread);
}
