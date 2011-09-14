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
#include <string.h>
#include <packagekit-glib2/pk-debug.h>
#include <zif.h>

#define PACKAGE_MEDIA_REPO_FILENAME		"/etc/yum.repos.d/packagekit-media.repo"

typedef struct {
	GCancellable	*cancellable;
	GFileMonitor	*monitor;
	guint		 signal_finished;
	guint		 signal_status;
	GVolumeMonitor	*volume_monitor;
	ZifConfig	*config;
	ZifGroups	*groups;
	ZifLock		*lock;
	ZifRelease	*release;
	ZifRepos	*repos;
	ZifState	*state;
	ZifStore	*store_local;
	ZifTransaction	*transaction;
} PkBackendYumPrivate;

static PkBackendYumPrivate *priv;

/**
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Zif");
}

/**
 * pk_backend_get_author:
 */
gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Richard Hughes <richard@hughsie.com>");
}

/**
 * pk_backend_yum_repos_changed_cb:
 **/
static void
pk_backend_yum_repos_changed_cb (GFileMonitor *monitor_,
				 GFile *file, GFile *other_file,
				 GFileMonitorEvent event_type,
				 PkBackend *backend)
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

/**
 * pk_backend_state_percentage_changed_cb:
 */
static void
pk_backend_state_percentage_changed_cb (ZifState *state,
					guint percentage,
					PkBackend *backend)
{
	pk_backend_set_percentage (backend, percentage);
}

/**
 * pk_backend_state_subpercentage_changed_cb:
 */
static void
pk_backend_state_subpercentage_changed_cb (ZifState *state,
					   guint subpercentage,
					   PkBackend *backend)
{
	pk_backend_set_sub_percentage (backend, subpercentage);
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

/**
 * pk_backend_convert_error:
 */
static PkErrorEnum
pk_backend_convert_error (const GError *error)
{
	PkErrorEnum error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
	if (error->domain == ZIF_STATE_ERROR) {
		switch (error->code) {
		case ZIF_STATE_ERROR_CANCELLED:
			error_code = PK_ERROR_ENUM_TRANSACTION_CANCELLED;
			break;
		case ZIF_STATE_ERROR_INVALID:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_TRANSACTION_ERROR) {
		switch (error->code) {
		case ZIF_TRANSACTION_ERROR_FAILED:
			error_code = PK_ERROR_ENUM_TRANSACTION_ERROR;
			break;
		case ZIF_TRANSACTION_ERROR_NOTHING_TO_DO:
			error_code = PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE;
			break;
		case ZIF_TRANSACTION_ERROR_NOT_SUPPORTED:
			error_code = PK_ERROR_ENUM_NOT_SUPPORTED;
			break;
		case ZIF_TRANSACTION_ERROR_CONFLICTING:
			error_code = PK_ERROR_ENUM_FILE_CONFLICTS;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_STORE_ERROR) {
		switch (error->code) {
		case ZIF_STORE_ERROR_FAILED_AS_OFFLINE:
			error_code = PK_ERROR_ENUM_NO_NETWORK;
			break;
		case ZIF_STORE_ERROR_FAILED_TO_FIND:
			error_code = PK_ERROR_ENUM_PACKAGE_NOT_FOUND;
			break;
		case ZIF_STORE_ERROR_FAILED_TO_DOWNLOAD:
			error_code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
			break;
		case ZIF_STORE_ERROR_ARRAY_IS_EMPTY:
			error_code = PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE;
			break;
		case ZIF_STORE_ERROR_NO_SUPPORT:
			error_code = PK_ERROR_ENUM_NOT_SUPPORTED;
			break;
		case ZIF_STORE_ERROR_NOT_LOCKED:
			error_code = PK_ERROR_ENUM_NOT_SUPPORTED;
			break;
		case ZIF_STORE_ERROR_FAILED:
		case ZIF_STORE_ERROR_MULTIPLE_MATCHES:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_PACKAGE_ERROR) {
		switch (error->code) {
		case ZIF_PACKAGE_ERROR_FAILED:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_CONFIG_ERROR) {
		switch (error->code) {
		case ZIF_CONFIG_ERROR_FAILED:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_DOWNLOAD_ERROR) {
		switch (error->code) {
		case ZIF_DOWNLOAD_ERROR_FAILED:
		case ZIF_DOWNLOAD_ERROR_PERMISSION_DENIED:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_MD_ERROR) {
		switch (error->code) {
		case ZIF_MD_ERROR_NO_SUPPORT:
			error_code = PK_ERROR_ENUM_NOT_SUPPORTED;
			break;
		case ZIF_MD_ERROR_FAILED_AS_OFFLINE:
			error_code = PK_ERROR_ENUM_NO_NETWORK;
			break;
		case ZIF_MD_ERROR_FAILED_DOWNLOAD:
			error_code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
			break;
		case ZIF_MD_ERROR_BAD_SQL:
		case ZIF_MD_ERROR_FAILED_TO_LOAD:
		case ZIF_MD_ERROR_FILE_TOO_OLD:
		case ZIF_MD_ERROR_FAILED:
		case ZIF_MD_ERROR_NO_FILENAME:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
			break;
		default:
			error_code = PK_ERROR_ENUM_INTERNAL_ERROR;
		}
	} else if (error->domain == ZIF_RELEASE_ERROR) {
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
	}
	if (error_code == PK_ERROR_ENUM_INTERNAL_ERROR) {
		g_warning ("failed to match error: %s:%i: %s",
			   g_quark_to_string (error->domain),
			   error->code,
			   error->message);
	}
	return error_code;
}

/**
 * pk_backend_transaction_start:
 */
void
pk_backend_transaction_start (PkBackend *backend)
{
	const gchar *root;
	gboolean ret = FALSE;
	gchar *http_proxy = NULL;
	GError *error = NULL;
	guint cache_age;
	guint i;
	guint lock_delay;
	guint lock_retries;
	guint pid = 0;

	/* only try a finite number of times */
	lock_retries = zif_config_get_uint (priv->config, "lock_retries", NULL);
	lock_delay = zif_config_get_uint (priv->config, "lock_delay", NULL);
	for (i=0; i<lock_retries; i++) {

		/* try to lock */
		ret = zif_lock_set_locked (priv->lock, &pid, &error);
		if (ret)
			break;

		/* we're now waiting */
		pk_backend_set_status (backend, PK_STATUS_ENUM_WAITING_FOR_LOCK);

		/* now wait */
		g_debug ("Failed to lock on try %i of %i, already locked by PID %i "
			 "(sleeping for %ims): %s\n",
			   i+1, lock_retries,
			   pid,
			   lock_delay,
			   error->message);
		g_clear_error (&error);
		g_usleep (lock_delay * 1000);
	}

	/* we failed to lock */
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_CANNOT_GET_LOCK,
				       "failed to get lock, held by PID: %i",
				       pid);
		goto out;
	}

	/* this backend does not support a relocatable root... yet */
	root = pk_backend_get_root (backend);
	if (g_strcmp0 (root, "/") != 0) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_INSTALL_ROOT_INVALID,
				       "backend does not support this root: '%s'",
				       root);
		goto out;
	}

	/* try to set, or re-set install root */
	ret = zif_store_local_set_prefix (ZIF_STORE_LOCAL (priv->store_local),
					  root,
					  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to set prefix: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* get network state */
	ret = pk_backend_is_online (backend);
	if (!ret) {
		zif_config_set_boolean (priv->config, "network",
					FALSE, NULL);
		goto out;
	}

	/* tell ZifConfig it's okay to contact the network */
	zif_config_set_boolean (priv->config, "network",
				TRUE, NULL);

	/* set cache age */
	cache_age = pk_backend_get_cache_age (backend);
	if (cache_age > 0)
		zif_config_set_uint (priv->config, "metadata_expire",
				     cache_age, NULL);

	/* set the proxy */
	http_proxy = pk_backend_get_proxy_http (backend);
	zif_config_set_string (priv->config, "http_proxy",
			       http_proxy, NULL);

	/* packages we can't remove */
	zif_config_set_string (priv->config, "protected_packages",
			       "PackageKit,zif,rpm,glibc", NULL);

	/* always skip broken transactions */
	zif_config_set_boolean (priv->config, "skip_broken",
				TRUE, NULL);

	/* set background mode */
	zif_config_set_boolean (priv->config, "background",
				pk_backend_use_background (backend), NULL);

	/* setup state */
	zif_state_reset (priv->state);

	/* allow cancelling again */
	g_cancellable_reset (priv->cancellable);

	/* start with a new transaction */
	zif_transaction_reset (priv->transaction);
out:
	g_free (http_proxy);
	return;
}

/**
 * pk_backend_transaction_stop:
 */
void
pk_backend_transaction_stop (PkBackend *backend)
{
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
	return;
}

/**
 * pk_backend_filter_package_array:
 **/
static GPtrArray *
pk_backend_filter_package_array (GPtrArray *array, PkBitfield filters)
{
	GPtrArray *result = NULL;
	guint i;
	ZifPackage *package;

	result = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* pre-result */
	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);

		/* installed */
		if (pk_bitfield_contain (filters,
					 PK_FILTER_ENUM_INSTALLED)) {
			if (!zif_package_is_installed (package))
				continue;
		} else if (pk_bitfield_contain (filters,
						PK_FILTER_ENUM_NOT_INSTALLED)) {
			if (zif_package_is_installed (package))
				continue;
		}

		/* development */
		if (pk_bitfield_contain (filters,
					 PK_FILTER_ENUM_DEVELOPMENT)) {
			if (!zif_package_is_devel (package))
				continue;
		} else if (pk_bitfield_contain (filters,
						PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			if (zif_package_is_devel (package))
				continue;
		}

		/* gui */
		if (pk_bitfield_contain (filters,
					 PK_FILTER_ENUM_GUI)) {
			if (!zif_package_is_gui (package))
				continue;
		} else if (pk_bitfield_contain (filters,
						PK_FILTER_ENUM_NOT_GUI)) {
			if (zif_package_is_gui (package))
				continue;
		}

		/* free */
		if (pk_bitfield_contain (filters,
					 PK_FILTER_ENUM_FREE)) {
			if (!zif_package_is_free (package))
				continue;
		} else if (pk_bitfield_contain (filters,
						PK_FILTER_ENUM_NOT_FREE)) {
			if (zif_package_is_free (package))
				continue;
		}

		/* arch */
		if (pk_bitfield_contain (filters,
					 PK_FILTER_ENUM_ARCH)) {
			if (!zif_package_is_native (package))
				continue;
		} else if (pk_bitfield_contain (filters,
						PK_FILTER_ENUM_NOT_ARCH)) {
			if (zif_package_is_native (package))
				continue;
		}

		/* add to array so we can post process */
		g_ptr_array_add (result, g_object_ref (package));
	}

	/* do newest filtering */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		zif_package_array_filter_newest (result);

	return result;
}

/**
 * pk_backend_emit_package_array:
 **/
static gboolean
pk_backend_emit_package_array (PkBackend *backend,
			       GPtrArray *array,
			       ZifState *state)
{
	const gchar *info_hint;
	const gchar *package_id;
	const gchar *summary;
	gboolean installed;
	gboolean ret = TRUE;
	guint i;
	PkInfoEnum info;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	g_return_val_if_fail (array != NULL, FALSE);

	state_local = zif_state_get_child (state);
	if (array->len > 0)
		zif_state_set_number_steps (state_local, array->len);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		installed = zif_package_is_installed (package);
		package_id = zif_package_get_package_id (package);

		/* should be quick as shouldn't be doing any action */
		state_loop = zif_state_get_child (state_local);
		summary = zif_package_get_summary (package, state_loop, NULL);

		/* if we set a hint, use that, otherwise just get the installed status correct */
		info_hint = (const gchar *)g_object_get_data (G_OBJECT(package), "kind");
		if (info_hint == NULL) {
			info = installed ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
		} else {
			info = pk_info_enum_from_string (info_hint);
		}

		pk_backend_package (backend, info, package_id, summary);

		/* done */
		ret = zif_state_done (state_local, NULL);
		if (!ret)
			goto out;
	}
out:
	return ret;
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
		g_debug ("ignoring operation on PkStoreLocal: %s",
			 error->message);
		return TRUE;
	}

	/* emit a warning, this isn't fatal */
	pk_backend_message (backend,
			    PK_MESSAGE_ENUM_BROKEN_MIRROR,
			    "%s",
			    error->message);
	return TRUE;
}

/**
 * pk_backend_get_store_array_for_filter:
 */
static GPtrArray *
pk_backend_get_store_array_for_filter (PkBackend *backend,
				       PkBitfield filters,
				       ZifState *state,
				       GError **error)
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
		array = zif_repos_get_stores_enabled (priv->repos,
						      state,
						      &error_local);
		if (array == NULL) {
			g_set_error (error, 1, 0,
				     "failed to get enabled stores: %s",
				     error_local->message);
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
pk_backend_search_newest (GPtrArray *store_array,
			  ZifState *state,
			  guint recent,
			  GError **error)
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
pk_backend_create_meta_package_for_category (GPtrArray *store_array,
					     ZifCategory *cat,
					     ZifState *state,
					     GError **error)
{
	const gchar *to_array[] = { NULL, NULL };
	gboolean ret;
	gchar *package_id = NULL;
	GPtrArray *array_packages;
	guint j;
	PkInfoEnum info = PK_INFO_ENUM_COLLECTION_INSTALLED;
	ZifPackage *package = NULL;
	ZifPackage *package_tmp;
	ZifString *string;

	/* are all the packages in this group installed? */
	to_array[0] = zif_category_get_id (cat);
	array_packages = zif_store_array_search_category (store_array,
							  (gchar**)to_array,
							  state,
							  error);
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
 * pk_backend_search_repos:
 */
static GPtrArray *
pk_backend_search_repos (gchar **repos,
			 ZifState *state,
			 GError **error)
{
	gboolean ret;
	gchar *installed_repo_id = NULL;
	GPtrArray *array_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_remote = NULL;
	GPtrArray *array_tmp = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifStoreRemote *store = NULL;

	/* set steps */
	ret = zif_state_set_steps (state,
				   NULL,
				   90, /* search installed */
				   5, /* get remote store */
				   5, /* get store */
				   -1);
	g_assert (ret);

	/* results array */
	array_tmp = zif_object_array_new ();

	/* blank */
	if (g_strcmp0 (repos[0], "repo:") == 0)
		goto skip;

	/* get all installed packages that were installed from this repo */
	installed_repo_id = g_strdup_printf ("installed:%s", repos[0]);
	state_local = zif_state_get_child (state);
	array_local = zif_store_get_packages (priv->store_local, state_local, error);
	if (array_local == NULL)
		goto out;
	for (i=0; i<array_local->len; i++) {
		package = g_ptr_array_index (array_local, i);
		if (g_strcmp0 (zif_package_get_data (package),
			       installed_repo_id) == 0)
			zif_object_array_add (array_tmp, package);
	}

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* get all the available packages from this repo */
	state_local = zif_state_get_child (state);
	store = zif_repos_get_store (priv->repos, repos[0], state_local, error);
	if (store == NULL)
		goto out;

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	state_local = zif_state_get_child (state);
	array_remote = zif_store_get_packages (ZIF_STORE (store), state_local, error);
	if (array_remote == NULL)
		goto out;
	zif_object_array_add_array (array_tmp, array_remote);

	/* this section done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;
skip:
	/* success */
	array = g_ptr_array_ref (array_tmp);
out:
	g_free (installed_repo_id);
	if (store != NULL)
		g_object_unref (store);
	if (array_tmp != NULL)
		g_ptr_array_unref (array_tmp);
	if (array_local != NULL)
		g_ptr_array_unref (array_local);
	if (array_remote != NULL)
		g_ptr_array_unref (array_remote);
	return array;
}

/**
 * pk_backend_search_collections:
 */
static GPtrArray *
pk_backend_search_collections (GPtrArray *store_array,
			       ZifState *state,
			       GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	guint i;
	ZifCategory *cat;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	/* set steps */
	ret = zif_state_set_steps (state,
				   NULL,
				   50, /* get categories */
				   50, /* generate fake packages */
				   -1);
	g_assert (ret);

	/* get sorted list of unique categories */
	state_local = zif_state_get_child (state);
	array_tmp = zif_store_array_get_categories (store_array,
						    state_local,
						    error);
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
		package = pk_backend_create_meta_package_for_category (store_array,
								       cat,
								       state_loop,
								       &error_local);
		if (package != NULL) {
			g_ptr_array_add (array, g_object_ref (package));
		} else {
			g_warning ("failed to add id %s: %s",
				   zif_category_get_id (cat),
				   error_local->message);
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
pk_backend_get_cat_for_id (GPtrArray *store_array,
			   const gchar *id,
			   ZifState *state,
			   GError **error)
{
	GPtrArray *array = NULL;
	ZifCategory *cat = NULL;
	ZifCategory *cat_tmp;
	guint i;

	/* get all cats */
	array = zif_store_array_get_categories (store_array,
						state,
						error);
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
		g_set_error (error, 1, 0,
			     "no category %s found",
			     id);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return cat;
}

/**
 * pk_backend_resolve_groups:
 */
static GPtrArray *
pk_backend_resolve_groups (GPtrArray *store_array,
			   gchar **search,
			   ZifState *state,
			   GError **error)
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
		ret = zif_state_set_steps (state,
					   NULL,
					   50, /* get category */
					   50, /* create metapackage */
					   -1);
		g_assert (ret);

		/* get the category */
		state_loop = zif_state_get_child (state_local);
		cat = pk_backend_get_cat_for_id (store_array,
						 search[i]+1,
						 state_loop,
						 &error_local);
		if (cat == NULL) {
			g_debug ("group %s not found: %s",
				 search[i],
				 error_local->message);
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
			package = pk_backend_create_meta_package_for_category (store_array,
									       cat,
									       state_loop,
									       &error_local);
			if (package != NULL) {
				g_ptr_array_add (array, package);
			} else {
				g_warning ("failed to add id %s: %s",
					   zif_category_get_id (cat),
					   error_local->message);
				g_clear_error (&error_local);
				ret = zif_state_finished (state_loop, error);
				if (!ret)
					goto out;
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
pk_backend_what_provides_helper (GPtrArray *store_array,
				 gchar **search,
				 ZifState *state,
				 GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	GPtrArray *array_retval = NULL;
	GPtrArray *depend_array = NULL;
	guint i;
	ZifDepend *depend;
	ZifState *state_local;

	/* set steps */
	ret = zif_state_set_steps (state,
				   NULL,
				   50, /* parse depends */
				   50, /* do the query */
				   -1);
	g_assert (ret);

	/* resolve all the depends */
	depend_array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; search[i] != NULL; i++) {

		/* parse this depend */
		depend = zif_depend_new ();
		g_ptr_array_add (depend_array, depend);
		ret = zif_depend_parse_description (depend, search[i], error);
		if (!ret)
			goto out;
	}

	/* this part done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* find what provides this depend */
	state_local = zif_state_get_child (state);
	array = zif_store_array_what_provides (store_array,
					       depend_array,
					       state_local,
					       error);
	if (array == NULL)
		goto out;

	/* this part done */
	ret = zif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	array_retval = g_ptr_array_ref (array);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (depend_array != NULL)
		g_ptr_array_unref (depend_array);
	return array_retval;
}

/**
 * pk_backend_search_thread:
 */
static gboolean
pk_backend_search_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **search;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *result;
	GPtrArray *store_array = NULL;
	guint i;
	guint recent;
	PkBitfield filters;
	PkRoleEnum role;
	ZifState *state_local;

	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	role = pk_backend_get_role (backend);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   2, /* get default stores */
				   90, /* do the search */
				   6, /* filter */
				   2, /* emit */
				   -1);
	g_assert (ret);

	/* get default store_array */
	state_local = zif_state_get_child (priv->state);
	store_array = pk_backend_get_store_array_for_filter (backend,
							     filters,
							     state_local,
							     &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
	zif_state_set_error_handler (priv->state, (ZifStateErrorHandlerCb) pk_backend_error_handler_cb, backend);

	/* do get action */
	if (role == PK_ROLE_ENUM_GET_PACKAGES) {
		state_local = zif_state_get_child (priv->state);
		array = zif_store_array_get_packages (store_array, state_local, &error);
		if (array == NULL) {
			pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get packages: %s", error->message);
			g_error_free (error);
			goto out;
		}
	} else {
		/* treat these all the same */
		search = pk_backend_get_strv (backend, "search");
		if (search == NULL) {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_INTERNAL_ERROR,
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
			} else if (g_str_has_prefix (search[0], "category:")) {
				search_entries = g_strv_length (search);
				search_stripped = g_new0 (gchar *, search_entries + 1);
				for (i=0; i < search_entries; i++)
					search_stripped[i] = g_strdup (&search[i][9]);
				array = zif_store_array_search_category (store_array, search_stripped, state_local, &error);
				g_strfreev (search_stripped);
			} else if (g_str_has_prefix (search[0], "repo:")) {
				search_entries = g_strv_length (search);
				search_stripped = g_new0 (gchar *, search_entries + 1);
				for (i=0; i < search_entries; i++)
					search_stripped[i] = g_strdup (&search[i][5]);
				array = pk_backend_search_repos (search_stripped, state_local, &error);
				g_strfreev (search_stripped);
			} else if (g_strcmp0 (search[0], "newest") == 0) {
				recent = zif_config_get_uint (priv->config, "recent", &error);
				array = pk_backend_search_newest (store_array, state_local, recent, &error);
				if (array == NULL) {
					pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get packages: %s", error->message);
					g_error_free (error);
					goto out;
				}
			} else if (g_strcmp0 (search[0], "collections") == 0) {
				array = pk_backend_search_collections (store_array, state_local, &error);
				if (array == NULL) {
					pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get packages: %s", error->message);
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
#if ZIF_CHECK_VERSION(0,2,4)
				array = zif_store_array_resolve_full (store_array,
								      search,
								      ZIF_STORE_RESOLVE_FLAG_USE_ALL |
								      ZIF_STORE_RESOLVE_FLAG_PREFER_NATIVE,
								      state_local,
								      &error);
#else
				array = zif_store_array_resolve (store_array,
								 search,
								 state_local,
								 &error);
#endif
			}
		} else if (role == PK_ROLE_ENUM_WHAT_PROVIDES) {
			array = pk_backend_what_provides_helper (store_array, search, state_local, &error);
		}
		if (array == NULL) {
			pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to search: %s", error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* filter */
	result = pk_backend_filter_package_array (array, filters);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_enable_media_repo:
 */
static void
pk_backend_enable_media_repo (gboolean enabled)
{
	gboolean ret;
	GError *error = NULL;
	ZifState *state;
	ZifStoreRemote *repo = NULL;

	/* find the right repo */
	state = zif_state_new ();
	zif_state_set_cancellable (state, zif_state_get_cancellable (priv->state));
	repo = zif_repos_get_store (priv->repos,
				    "InstallMedia",
				    state,
				    &error);
	if (repo == NULL) {
		g_debug ("failed to find install-media repo: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the state */
	zif_state_reset (state);
	ret = zif_store_remote_set_enabled (repo,
					    enabled,
#if ZIF_CHECK_VERSION(0,1,6)
					    state,
#endif
					    &error);
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
		if (zif_package_id_check (action_hint)) {
			status = PK_STATUS_ENUM_DOWNLOAD;
			pk_backend_package (backend,
					    PK_INFO_ENUM_DOWNLOADING,
					    action_hint,
					    "");
			goto out;
		}
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

	/* package install */
	if (action == ZIF_STATE_ACTION_INSTALLING) {
		status = PK_STATUS_ENUM_INSTALL;
		pk_backend_package (backend,
				    PK_INFO_ENUM_INSTALLING,
				    action_hint,
				    "");
		goto out;
	}

	/* package remove */
	if (action == ZIF_STATE_ACTION_REMOVING) {
		status = PK_STATUS_ENUM_REMOVE;
		pk_backend_package (backend,
				    PK_INFO_ENUM_REMOVING,
				    action_hint,
				    "");
		goto out;
	}

	/* package update */
	if (action == ZIF_STATE_ACTION_UPDATING) {
		status = PK_STATUS_ENUM_UPDATE;
		pk_backend_package (backend,
				    PK_INFO_ENUM_UPDATING,
				    action_hint,
				    "");
		goto out;
	}

	/* package update */
	if (action == ZIF_STATE_ACTION_CLEANING) {
		status = PK_STATUS_ENUM_CLEANUP;
		pk_backend_package (backend,
				    PK_INFO_ENUM_CLEANUP,
				    action_hint,
				    "");
		goto out;
	}

	/* rpm test commit */
	if (action == ZIF_STATE_ACTION_TEST_COMMIT) {
		status = PK_STATUS_ENUM_TEST_COMMIT;
		goto out;
	}

	/* depsolving */
	if (action == ZIF_STATE_ACTION_DEPSOLVING_CONFLICTS ||
	    action == ZIF_STATE_ACTION_DEPSOLVING_INSTALL ||
	    action == ZIF_STATE_ACTION_DEPSOLVING_REMOVE ||
	    action == ZIF_STATE_ACTION_DEPSOLVING_UPDATE) {
		status = PK_STATUS_ENUM_DEP_RESOLVE;
		goto out;
	}

out:
	if (status != PK_STATUS_ENUM_UNKNOWN)
		pk_backend_set_status (backend, status);
}

#if ZIF_CHECK_VERSION(0,1,5)
/**
 * pk_backend_speed_changed_cb:
 **/
static void
pk_backend_speed_changed_cb (ZifState *state,
			     GParamSpec *pspec,
			     PkBackend *backend)
{
	pk_backend_set_speed (backend,
			      zif_state_get_speed (state));
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
	gchar *config_file = NULL;
	gchar *reposdir = NULL;
	GError *error = NULL;
	GFile *file = NULL;
	GList *mounts;

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

	/* coldplug the mounts */
	priv->volume_monitor = g_volume_monitor_get ();
	mounts = g_volume_monitor_get_mounts (priv->volume_monitor);
	g_list_foreach (mounts, (GFunc) pk_backend_mount_add, NULL);
	g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
	g_list_free (mounts);

	/* init rpm */
	zif_init ();

	/* TODO: hook up errors */
	priv->cancellable = g_cancellable_new ();

	/* ZifState */
	priv->state = zif_state_new ();
	zif_state_set_cancellable (priv->state, priv->cancellable);
	g_signal_connect (priv->state, "percentage-changed",
			  G_CALLBACK (pk_backend_state_percentage_changed_cb),
			  backend);
	g_signal_connect (priv->state, "subpercentage-changed",
			  G_CALLBACK (pk_backend_state_subpercentage_changed_cb),
			  backend);
	g_signal_connect (priv->state, "action-changed",
			  G_CALLBACK (pk_backend_state_action_changed_cb),
			  backend);
#if ZIF_CHECK_VERSION(0,1,5)
	g_signal_connect (priv->state, "notify::speed",
			  G_CALLBACK (pk_backend_speed_changed_cb),
			  backend);
#endif

	/* we don't want to enable this for normal runtime */
	//zif_state_set_enable_profile (priv->state, TRUE);

	/* ZifConfig */
	priv->config = zif_config_new ();
	ret = zif_config_set_filename (priv->config, NULL, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				       "failed to set config: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* setup a file monitor on the repos directory */
	reposdir = zif_config_get_string (priv->config, "reposdir", NULL);
	file = g_file_new_for_path (reposdir);
	priv->monitor = g_file_monitor_directory (file,
						  G_FILE_MONITOR_NONE,
						  NULL,
						  &error);
	if (priv->monitor != NULL) {
		g_signal_connect (priv->monitor, "changed",
				  G_CALLBACK (pk_backend_yum_repos_changed_cb), backend);
	} else {
		g_warning ("failed to setup monitor: %s",
			   error->message);
		g_error_free (error);
	}

	/* ZifLock */
	priv->lock = zif_lock_new ();

	/* ZifRelease */
	priv->release = zif_release_new ();

	/* ZifStoreLocal */
	priv->store_local = zif_store_local_new ();

	/* ZifTransaction */
	priv->transaction = zif_transaction_new ();
	zif_transaction_set_store_local (priv->transaction, priv->store_local);

	/* ZifRepos */
	priv->repos = zif_repos_new ();
	ret = zif_repos_set_repos_dir (priv->repos, NULL, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,
				       "failed to set repos dir: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* ZifGroups: FIXME: add this to config */
	priv->groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (priv->groups,
					   DATADIR "/PackageKit/helpers/zif/zif-comps-groups.conf",
					   &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_GROUP_LIST_INVALID,
				       "failed to set mapping file: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (reposdir);
	g_free (config_file);
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
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
	g_signal_handler_disconnect (backend, priv->signal_finished);
	g_signal_handler_disconnect (backend, priv->signal_status);
	if (priv->config != NULL)
		g_object_unref (priv->config);
	if (priv->release != NULL)
		g_object_unref (priv->release);
	if (priv->transaction != NULL)
		g_object_unref (priv->transaction);
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
	const gchar *group_str;
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	PkBitfield groups = 0;

	/* get the dynamic group list */
	array = zif_groups_get_groups (priv->groups, &error);
	if (array == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_GROUP_LIST_INVALID,
				       "failed to get the list of groups: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to a bitfield */
	for (i=0; i<array->len; i++) {
		group_str = g_ptr_array_index (array, i);
		pk_bitfield_add (groups,
				 pk_group_enum_from_string (group_str));
	}

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
	/* try to cancel the thread */
	g_debug ("cancelling transaction");
	g_cancellable_cancel (priv->cancellable);
}

/**
 * pk_backend_download_packages_thread:
 */
static gboolean
pk_backend_download_packages_thread (PkBackend *backend)
{
	const gchar *directory = pk_backend_get_string (backend, "directory");
	const gchar *filename;
	gboolean ret;
	gchar *basename;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	gchar *path;
	GError *error = NULL;
	GPtrArray *packages = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_tmp;

	ret = zif_state_set_steps (priv->state,
				   NULL,
				   2, /* get default stores */
				   8, /* find packages */
				   90, /* download */
				   -1);
	g_assert (ret);

	/* find all the packages */
	packages = zif_object_array_new ();
	state_local = zif_state_get_child (priv->state);
	store_array = zif_store_array_new ();
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* find */
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i=0; package_ids[i] != NULL; i++) {

		/* find packages */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array,
							package_ids[i],
							state_loop,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find %s: %s",
					       package_ids[i],
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
			g_error_free (error);
			goto out;
		}

		zif_object_array_add (packages, package);
		g_object_unref (package);
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* download list */
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, packages->len);
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);

		/* set steps */
		state_loop = zif_state_get_child (state_local);
		ret = zif_state_set_steps (state_loop,
					   NULL,
					   2, /* get filename */
					   96, /* download */
					   2, /* emit */
					   -1);
		g_assert (ret);

		/* get filename */
		state_tmp = zif_state_get_child (state_loop);
		filename = zif_package_get_filename (package, state_tmp, &error);
		if (filename == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
					       "failed to get filename for %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* download */
		state_tmp = zif_state_get_child (state_loop);
		ret = zif_package_remote_download (ZIF_PACKAGE_REMOTE (package),
						   directory,
						   state_tmp,
						   &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
					       "failed to download %s: %s",
					       filename,
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
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
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (packages != NULL)
		g_ptr_array_unref (packages);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_get_depends_thread:
 */
static gboolean
pk_backend_get_depends_thread (PkBackend *backend)
{
	const gchar *id;
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *provides;
	GPtrArray *requires;
	GPtrArray *result;
	GPtrArray *store_array = NULL;
	guint i, j;
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	ZifPackage *package;
	ZifPackage *package_provide;
	ZifState *state_local;
	ZifState *state_loop;

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   2, /* get stores */
				   94, /* what requires + provides */
				   2, /* filter */
				   2, /* emit */
				   -1);
	g_assert (ret);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	store_array = pk_backend_get_store_array_for_filter (backend,
							     0,
							     state_local,
							     &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
		ret = zif_state_set_steps (state_local,
					   NULL,
					   50, /* find package */
					   25, /* get requires */
					   25, /* what provides */
					   -1);
		g_assert (ret);

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array,
							id,
							state_loop,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find %s: %s",
					       package_ids[i],
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get requires */
		state_loop = zif_state_get_child (state_local);
		requires = zif_package_get_requires (package, state_loop, &error);
		if (requires == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						       "failed to get requires for %s: %s",
						       package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* match a package to each require */
		state_loop = zif_state_get_child (state_local);
		provides = zif_store_array_what_provides (store_array, requires, state_loop, &error);
		if (provides == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						       "failed to find provide for %s: %s",
						       zif_depend_get_name (g_ptr_array_index (requires, 0)),
						       error->message);
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
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* free */
		g_object_unref (package);
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* filter */
	result = pk_backend_filter_package_array (array, filters);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_get_requires_thread:
 */
static gboolean
pk_backend_get_requires_thread (PkBackend *backend)
{
	const gchar *id;
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *provides;
	GPtrArray *requires;
	GPtrArray *result;
	GPtrArray *store_array = NULL;
	guint i, j;
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	ZifPackage *package;
	ZifPackage *package_provide;
	ZifState *state_local;
	ZifState *state_loop;

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   2, /* get stores */
				   94, /* what depends + provides */
				   2, /* filter */
				   2, /* emit */
				   -1);
	g_assert (ret);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	store_array = pk_backend_get_store_array_for_filter (backend,
							     0,
							     state_local,
							     &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
		ret = zif_state_set_steps (state_local,
					   NULL,
					   50, /* find package */
					   25, /* get requires */
					   25, /* what provides */
					   -1);
		g_assert (ret);

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array,
							id,
							state_loop,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find %s: %s",
					       package_ids[i],
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get requires */
		state_loop = zif_state_get_child (state_local);
		requires = zif_package_get_provides (package, state_loop, &error);
		if (requires == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						       "failed to get requires for %s: %s",
						       package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* match a package to each require */
		state_loop = zif_state_get_child (state_local);
		provides = zif_store_array_what_requires (store_array,
							  requires,
							  state_loop,
							  &error);
		if (provides == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find provide for %s: %s",
					       zif_depend_get_name (g_ptr_array_index (requires, 0)),
					       error->message);
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
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* free */
		g_object_unref (package);
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* filter */
	result = pk_backend_filter_package_array (array, filters);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_get_details_thread:
 */
static gboolean
pk_backend_get_details_thread (PkBackend *backend)
{
	const gchar *description;
	const gchar *group_str;
	const gchar *id;
	const gchar *license;
	const gchar *url;
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	guint64 size;
	guint i;
	PkBitfield filters = PK_FILTER_ENUM_UNKNOWN;
	PkGroupEnum group;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_tmp;

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   4, /* get stores */
				   96, /* get details */
				   -1);
	g_assert (ret);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	if (pk_backend_is_all_installed (package_ids))
		pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	store_array = pk_backend_get_store_array_for_filter (backend,
							     filters,
							     state_local,
							     &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local,
				    g_strv_length (package_ids));
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];

		/* set up state */
		state_loop = zif_state_get_child (state_local);
		ret = zif_state_set_steps (state_loop,
					   NULL,
					   80, /* find package */
					   10, /* get licence */
					   5, /* get group */
					   1, /* get description */
					   1, /* get url */
					   1, /* get size */
					   2, /* emit */
					   -1);
		g_assert (ret);

		/* find package */
		state_tmp = zif_state_get_child (state_loop);
		package = zif_store_array_find_package (store_array,
							id,
							state_tmp,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find %s: %s",
					       package_ids[i],
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get license */
		state_tmp = zif_state_get_child (state_loop);
		license = zif_package_get_license (package,
						   state_tmp,
						   NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get group */
		state_tmp = zif_state_get_child (state_loop);
		group_str = zif_package_get_group (package, state_tmp, &error);

		/* not being in a group is non-fatal */
		if (group_str == NULL) {
			g_warning ("failed to get group: %s",
				   error->message);
			g_clear_error (&error);
		}
		group = pk_group_enum_from_text (group_str);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get description */
		state_tmp = zif_state_get_child (state_loop);
		description = zif_package_get_description (package,
							   state_tmp,
							   NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get url */
		state_tmp = zif_state_get_child (state_loop);
		url = zif_package_get_url (package, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get size */
		state_tmp = zif_state_get_child (state_loop);
		size = zif_package_get_size (package, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
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
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* free */
		g_object_unref (package);
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
  * pk_backend_get_distro_upgrades_thread:
  */
static gboolean
pk_backend_get_distro_upgrades_thread (PkBackend *backend)
{
	gchar *distro_id;
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	ZifUpgrade *upgrade;

	/* one shot */
	zif_state_reset (priv->state);

	/* get the upgrades */
	array = zif_release_get_upgrades_new (priv->release, priv->state, &error);
	if (array == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
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
	return TRUE;
}

/**
 * pk_backend_get_files_thread:
 */
static gboolean
pk_backend_get_files_thread (PkBackend *backend)
{
	const gchar *file;
	const gchar *id;
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GError *error = NULL;
	GPtrArray *files;
	GPtrArray *store_array = NULL;
	GString *files_str;
	guint i, j;
	PkBitfield filters = PK_FILTER_ENUM_UNKNOWN;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_tmp;

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   2, /* get stores */
				   98, /* get files */
				   -1);
	g_assert (ret);

	/* find all the packages */
	state_local = zif_state_get_child (priv->state);
	if (pk_backend_is_all_installed (package_ids))
		pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	store_array = pk_backend_get_store_array_for_filter (backend,
							     filters,
							     state_local,
							     &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, g_strv_length (package_ids));
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];
		state_loop = zif_state_get_child (state_local);

		/* set steps */
		ret = zif_state_set_steps (state_loop,
					   NULL,
					   10, /* find package */
					   90, /* get files & emit */
					   -1);
		g_assert (ret);

		state_tmp = zif_state_get_child (state_loop);
		package = zif_store_array_find_package (store_array,
							id,
							state_tmp,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find %s: %s",
					       package_ids[i],
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get files */
		state_tmp = zif_state_get_child (state_loop);
		files = zif_package_get_files (package, state_tmp, &error);
		if (files == NULL) {
			pk_backend_error_code (backend,
					       pk_backend_convert_error (error),
					       "no files for %s: %s",
					       package_ids[i],
					       error->message);
			g_error_free (error);
			goto out;
		}

		files_str = g_string_new ("");
		for (j=0; j<files->len; j++) {
			file = g_ptr_array_index (files, j);
			g_string_append_printf (files_str, "%s\n", file);
		}
		pk_backend_files (backend, package_ids[i], files_str->str);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
	return TRUE;
}

/**
 * pk_backend_get_updates_thread:
 */
static gboolean
pk_backend_get_updates_thread (PkBackend *backend)
{
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	gboolean background;
	gboolean ret;
	gchar **search = NULL;
	GError *error = NULL;
	gint val;
	GPtrArray *array = NULL;
	GPtrArray *result = NULL;
	GPtrArray *store_array = NULL;
	GPtrArray *updates_available = NULL;
	GPtrArray *updates = NULL;
	guint i, j;
	PkInfoEnum info;
	ZifPackage *package;
	ZifPackage *package_update;
	ZifState *state_local;
	ZifState *state_loop;
	ZifUpdateKind update_kind;
	ZifUpdate *update;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* set steps */
	background = zif_config_get_boolean (priv->config, "background", NULL);
	if (!background) {
		ret = zif_state_set_steps (priv->state,
					   NULL,
					   1, /* get remote stores */
					   1, /* get installed packages */
					   3, /* filter newest */
					   45, /* look in remote stores */
					   50, /* get updateinfo */
					   -1);
	} else {
		ret = zif_state_set_steps (priv->state,
					   NULL,
					   1, /* get remote stores */
					   1, /* get installed packages */
					   3, /* filter newest */
					   25, /* look in remote stores */
					   20, /* get updateinfo */
					   50, /* depsolve */
					   -1);
	}
	g_assert (ret);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* get all the installed packages */
	state_local = zif_state_get_child (priv->state);
	array = zif_store_get_packages (priv->store_local, state_local, &error);
	if (array == NULL) {
		g_debug ("failed to get local store: %s",
			 error->message);
		g_error_free (error);
		goto out;
	}
	g_debug ("searching for updates with %i packages", array->len);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* remove any packages that are not newest (think kernel) */
	zif_package_array_filter_newest (array);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* get updates */
	search = g_new0 (gchar *, array->len + 1);
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		search[i] = g_strdup (zif_package_get_name (package));
	}
	state_local = zif_state_get_child (priv->state);
	zif_state_set_error_handler (priv->state,
				     (ZifStateErrorHandlerCb) pk_backend_error_handler_cb,
				     backend);
	updates = zif_store_array_resolve (store_array, search, state_local, &error);
	if (updates == NULL) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to get updates: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* some repos contain lots of versions of one package */
	zif_package_array_filter_newest (updates);

	/* find each one in a remote repo */
	updates_available = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<array->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (array, i));

		/* find updates */
		for (j=0; j<updates->len; j++) {
			package_update = ZIF_PACKAGE (g_ptr_array_index (updates, j));

			/* newer? */
			val = zif_package_compare (package_update, package);
			if (val == G_MAXINT)
				continue;
			if (val > 0) {
				g_debug ("*** update %s from %s to %s",
					 zif_package_get_name (package),
					 zif_package_get_version (package),
					 zif_package_get_version (package_update));
				g_ptr_array_add (updates_available, g_object_ref (package_update));
				break;
			}
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* setup steps on updatinfo state */
	state_local = zif_state_get_child (priv->state);
	if (updates_available->len > 0)
		zif_state_set_number_steps (state_local, updates_available->len);

	/* get update info */
	for (i=0; i<updates_available->len; i++) {
		package = g_ptr_array_index (updates_available, i);
		state_loop = zif_state_get_child (state_local);

		/* updates without updatinfo */
		info = PK_INFO_ENUM_NORMAL;

		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package), state_loop, &error);
		if (update == NULL) {
			g_debug ("failed to get updateinfo for %s", zif_package_get_id (package));
			g_clear_error (&error);
			ret = zif_state_finished (state_loop, NULL);
			if (!ret) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
						       "cancelled: %s",
						       error->message);
				g_error_free (error);
				goto out;
			}
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
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* if the transaction is done in the background, then depsolve
	 * the updates transaction so we have all the file lists up to
	 * date, and the depends data calculated so the UI is snappy */
	if (background) {
		/* use these stores for the transaction */
		zif_transaction_set_stores_remote (priv->transaction, store_array);

		for (i=0; i<updates_available->len; i++) {
			package = g_ptr_array_index (updates_available, i);
			ret = zif_transaction_add_install_as_update (priv->transaction,
								     package,
								     &error);
			if (!ret) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						       "cannot add update: %s",
						       error->message);
				g_error_free (error);
				goto out;
			}
		}

		/* resolve this, which will take some time, as it's a
		 * background action and thus throttled */
		state_local = zif_state_get_child (priv->state);
		ret = zif_transaction_resolve (priv->transaction, state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
					       "cannot resolve transaction: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (priv->state, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* filter */
	result = pk_backend_filter_package_array (updates_available, filters);

	/* done */
	pk_backend_set_percentage (backend, 100);

	/* emit */
	state_local = zif_state_get_child (priv->state);
	pk_backend_emit_package_array (backend, result, state_local);
out:
	pk_backend_finished (backend);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	if (updates_available != NULL)
		g_ptr_array_unref (updates_available);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (result != NULL)
		g_ptr_array_unref (result);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	g_strfreev (search);
	return TRUE;
}

/**
 * pk_backend_get_changelog_text:
 */
static gchar *
pk_backend_get_changelog_text (GPtrArray *changesets)
{
	const gchar *version;
	gchar date_str[128];
	GDate *date;
	GString *text;
	guint i;
	ZifChangeset *changeset;

	/* create output string */
	text = g_string_new ("");
	date = g_date_new ();

	/* go through each one */
	for (i=0; i<changesets->len; i++) {
		changeset = g_ptr_array_index (changesets, i);

		/* format the indervidual changeset */
		g_date_set_time_t (date, zif_changeset_get_date (changeset));
		g_date_strftime (date_str, 128, "%F", date);
		version = zif_changeset_get_version (changeset);
		if (version != NULL) {
			g_string_append_printf (text, "**%s** %s - %s\n%s\n\n",
						date_str,
						zif_changeset_get_author (changeset),
						version,
						zif_changeset_get_description (changeset));
		} else {
			g_string_append_printf (text, "**%s** %s\n%s\n\n",
						date_str,
						zif_changeset_get_author (changeset),
						zif_changeset_get_description (changeset));
		}
	}
	g_date_free (date);
	return g_string_free (text, FALSE);
}

/**
 * pk_backend_get_update_detail_thread:
 */
static gboolean
pk_backend_get_update_detail_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **package_ids;
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	guint j;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_tmp;
	ZifUpdate *update;

	/* get the data */
	package_ids = pk_backend_get_strv (backend, "package_ids");
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   2, /* get stores */
				   98, /* get update detail */
				   -1);
	g_assert (ret);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* get the update info */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i=0; package_ids[i] != NULL; i++) {

		state_loop = zif_state_get_child (state_local);
		ret = zif_state_set_steps (state_loop,
					   NULL,
					   12, /* find package */
					   88, /* get update detail */
					   -1);
		g_assert (ret);

		/* need to get the packages from the find_id */
		state_tmp = zif_state_get_child (state_loop);
		package = zif_store_array_find_package (store_array,
							package_ids[i],
							state_tmp,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find package %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		state_tmp = zif_state_get_child (state_loop);
		update = zif_package_remote_get_update_detail (ZIF_PACKAGE_REMOTE (package),
							       state_tmp,
							       &error);
		if (update == NULL) {
			g_debug ("failed to get updateinfo for %s",
				 zif_package_get_id (package));
			g_clear_error (&error);
			pk_backend_update_detail (backend, package_ids[i],
						  NULL, NULL, NULL, NULL, NULL,
						  PK_RESTART_ENUM_NONE,
						  "",
						  "No description available",
						  PK_UPDATE_STATE_ENUM_UNKNOWN,
						  NULL, NULL);

			/* ensure we manually clear the state, as we're
			 * carrying on */
			ret = zif_state_finished (state_tmp, &error);
			if (!ret) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
						       "cancelled: %s",
						       error->message);
				g_error_free (error);
				goto out;
			}
		} else {
			gchar *changelog_text = NULL;
			GPtrArray *array;
			GPtrArray *changesets;
			GString *string_cve;
			GString *string_bugzilla;
			GString *string_vendor;
			ZifUpdateInfo *info;
			array = zif_update_get_update_infos (update);
			string_cve = g_string_new (NULL);
			string_bugzilla = g_string_new (NULL);
			string_vendor = g_string_new (NULL);
			for (j=0; j<array->len; j++) {
				info = g_ptr_array_index (array, j);
				switch (zif_update_info_get_kind (info)) {
				case ZIF_UPDATE_INFO_KIND_CVE:
					g_string_append_printf (string_cve, "%s;%s;",
								zif_update_info_get_url (info),
								zif_update_info_get_title (info));
					break;
				case ZIF_UPDATE_INFO_KIND_BUGZILLA:
					g_string_append_printf (string_bugzilla, "%s;%s;",
								zif_update_info_get_url (info),
								zif_update_info_get_title (info));
					break;
				case ZIF_UPDATE_INFO_KIND_VENDOR:
					g_string_append_printf (string_vendor, "%s;%s;",
								zif_update_info_get_url (info),
								zif_update_info_get_title (info));
					break;
				default:
					break;
				}
			}

			/* remove trailing ';' */
			if (string_cve->len > 0)
				g_string_set_size (string_cve, string_cve->len - 1);
			if (string_bugzilla->len > 0)
				g_string_set_size (string_bugzilla, string_bugzilla->len - 1);
			if (string_vendor->len > 0)
				g_string_set_size (string_vendor, string_vendor->len - 1);

			/* format changelog */
			changesets = zif_update_get_changelog (update);
			if (changesets != NULL)
				changelog_text = pk_backend_get_changelog_text (changesets);
			pk_backend_update_detail (backend, package_ids[i],
						  NULL, //updates,
						  NULL, //obsoletes,
						  string_vendor->str,
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
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_convert_transaction_reason_to_info_enum:
 */
static PkInfoEnum
pk_backend_convert_transaction_reason_to_info_enum (ZifTransactionReason reason)
{
	switch (reason) {
	case ZIF_TRANSACTION_REASON_INSTALL_DEPEND:
	case ZIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE:
	case ZIF_TRANSACTION_REASON_INSTALL_USER_ACTION:
		return PK_INFO_ENUM_INSTALLING;
	case ZIF_TRANSACTION_REASON_REMOVE_AS_ONLYN:
	case ZIF_TRANSACTION_REASON_REMOVE_FOR_DEP:
	case ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE:
	case ZIF_TRANSACTION_REASON_REMOVE_OBSOLETE:
	case ZIF_TRANSACTION_REASON_REMOVE_USER_ACTION:
		return PK_INFO_ENUM_REMOVING;
	case ZIF_TRANSACTION_REASON_UPDATE_DEPEND:
	case ZIF_TRANSACTION_REASON_UPDATE_FOR_CONFLICT:
	case ZIF_TRANSACTION_REASON_UPDATE_USER_ACTION:
		return PK_INFO_ENUM_UPDATING;
	default:
		return PK_INFO_ENUM_AVAILABLE;
	}
}

/**
 * pk_backend_run_transaction:
 */
static gboolean
pk_backend_run_transaction (PkBackend *backend, ZifState *state)
{
	gboolean only_trusted;
	gboolean ret;
	gboolean simulate;
	GError *error = NULL;
	GPtrArray *array_tmp;
	GPtrArray *install = NULL;
	GPtrArray *simulate_array = NULL;
	guint i, j;
	PkInfoEnum info_enum;
	ZifPackage *package;
	ZifPackageTrustKind trust_kind;
	ZifState *state_local;

	/* set steps */
	simulate = pk_backend_get_bool (backend, "hint:simulate");
	if (simulate) {
		ret = zif_state_set_steps (state,
					   NULL,
					   94, /* resolve */
					   1, /* check trusted */
					   5, /* print packages */
					   -1);
	} else {
		ret = zif_state_set_steps (state,
					   NULL,
					   30, /* resolve */
					   1, /* check trusted */
					   29, /* prepare */
					   40, /* commit */
					   -1);
	}
	g_assert (ret);

	/* resolve the transaction */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_resolve (priv->transaction,
				       state_local,
				       &error);
	if (!ret) {
		if (error->domain == ZIF_TRANSACTION_ERROR &&
		    error->code == ZIF_TRANSACTION_ERROR_NOTHING_TO_DO) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED,
					       error->message);
		} else {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
					       "failed to resolve transaction: %s",
					       error->message);
		}
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* mark any untrusted packages */
	install = zif_transaction_get_install (priv->transaction);
	for (i=0; i<install->len; i++) {
		package = g_ptr_array_index (install, i);
		trust_kind = zif_package_get_trust_kind (package);
		if (trust_kind != ZIF_PACKAGE_TRUST_KIND_PUBKEY) {

			/* ignore the trusted auth step */
			pk_backend_message (backend,
					    PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE,
					    "The package %s is untrusted",
					    zif_package_get_printable (package));
		}
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* list the packages in the transaction */
	if (simulate) {
		simulate_array = zif_object_array_new ();
		for (i=0; i<ZIF_TRANSACTION_REASON_LAST; i++) {
			if (i == ZIF_TRANSACTION_REASON_REMOVE_FOR_UPDATE)
				continue;
			info_enum = pk_backend_convert_transaction_reason_to_info_enum (i);
			array_tmp = zif_transaction_get_array_for_reason (priv->transaction, i);
			for (j=0; j<array_tmp->len; j++) {
				package = g_ptr_array_index (array_tmp, j);
				g_object_set_data (G_OBJECT(package),
						   "kind",
						   (gpointer)pk_info_enum_to_string (info_enum));
				zif_object_array_add (simulate_array, package);
			}
			g_ptr_array_unref (array_tmp);
		}
		state_local = zif_state_get_child (state);
		pk_backend_emit_package_array (backend, simulate_array, state_local);

		/* this section finished */
		ret = zif_state_finished (state, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
		goto out;
	}

	/* prepare the transaction */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_prepare (priv->transaction,
				       state_local,
				       &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
				       "failed to prepare transaction: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* check if any are not trusted */
	only_trusted = pk_backend_get_bool (backend, "only_trusted");
	if (only_trusted) {
		for (i=0; i<install->len; i++) {
			package = g_ptr_array_index (install, i);
			trust_kind = zif_package_get_trust_kind (package);
			if (trust_kind != ZIF_PACKAGE_TRUST_KIND_PUBKEY) {
				ret = FALSE;
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_MISSING_GPG_SIGNATURE,
						       "package %s is untrusted",
						       zif_package_get_printable (package));
				goto out;
			}
		}
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* commit the transaction */
	state_local = zif_state_get_child (state);
	ret = zif_transaction_commit (priv->transaction,
				      state_local,
				      &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to commit transaction: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (simulate_array != NULL)
		g_ptr_array_unref (simulate_array);
	if (install != NULL)
		g_ptr_array_unref (install);
	return ret;
}

/**
 * pk_backend_remove_packages_thread:
 */
static gboolean
pk_backend_remove_packages_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **package_ids;
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   10, /* find packages */
				   90, /* run transaction */
				   -1);
	g_assert (ret);

	state_local = zif_state_get_child (priv->state);
	package_ids = pk_backend_get_strv (backend, "package_ids");
	zif_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i=0; package_ids[i] != NULL; i++) {

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_find_package (priv->store_local,
						  package_ids[i],
						  state_loop,
						  &error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find package: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* add it as a remove to the transaction */
		ret = zif_transaction_add_remove (priv->transaction,
						   package,
						   &error);
		g_object_unref (package);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to add package %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* run transaction */
	state_local = zif_state_get_child (priv->state);
	ret = pk_backend_run_transaction (backend, state_local);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_update_packages_thread:
 */
static gboolean
pk_backend_update_packages_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **package_ids;
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   10, /* add remote */
				   10, /* find packages */
				   80, /* run transaction */
				   -1);
	g_assert (ret);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* use these stores for the transaction */
	zif_transaction_set_stores_remote (priv->transaction, store_array);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	package_ids = pk_backend_get_strv (backend, "package_ids");
	zif_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i=0; package_ids[i] != NULL; i++) {

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array,
							package_ids[i],
							state_loop,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find package: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* add it as an update to the transaction */
		ret = zif_transaction_add_install_as_update (priv->transaction,
							     package,
							     &error);
		g_object_unref (package);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to add package %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* run transaction */
	state_local = zif_state_get_child (priv->state);
	ret = pk_backend_run_transaction (backend, state_local);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_update_system_thread:
 */
static gboolean
pk_backend_update_system_thread (PkBackend *backend)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *updates = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifStore *store_local = NULL;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   10, /* add remote */
				   10, /* get updates */
				   10, /* add updates */
				   70, /* run transaction */
				   -1);
	g_assert (ret);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* use these stores for the transaction */
	zif_transaction_set_stores_remote (priv->transaction, store_array);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* get all updates */
	state_local = zif_state_get_child (priv->state);
	store_local = zif_store_local_new ();
	updates = zif_store_array_get_updates (store_array,
					       store_local,
					       state_local,
					       &error);
	if (updates == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_ERROR,
				       "failed to get updates: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* add them as an update to the transaction */
	for (i = 0; i < updates->len; i++) {
		package = g_ptr_array_index (updates, i);
		ret = zif_transaction_add_install_as_update (priv->transaction,
							     package,
							     &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_ERROR,
					       "failed to add package %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* run transaction */
	state_local = zif_state_get_child (priv->state);
	ret = pk_backend_run_transaction (backend, state_local);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_local != NULL)
		g_object_unref (store_local);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_install_packages_thread:
 */
static gboolean
pk_backend_install_packages_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **package_ids;
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;
	ZifState *state_loop;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   10, /* add remote */
				   10, /* find packages */
				   80, /* run transaction */
				   -1);
	g_assert (ret);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* use these stores for the transaction */
	zif_transaction_set_stores_remote (priv->transaction, store_array);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	package_ids = pk_backend_get_strv (backend, "package_ids");
	zif_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i=0; package_ids[i] != NULL; i++) {

		/* find package */
		state_loop = zif_state_get_child (state_local);
		package = zif_store_array_find_package (store_array,
							package_ids[i],
							state_loop,
							&error);
		if (package == NULL) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to find package: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* add it as an install to the transaction */
		ret = zif_transaction_add_install (priv->transaction,
						   package,
						   &error);
		g_object_unref (package);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to add package %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* run transaction */
	state_local = zif_state_get_child (priv->state);
	ret = pk_backend_run_transaction (backend, state_local);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_install_files_thread:
 */
static gboolean
pk_backend_install_files_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **full_paths;
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	guint i;
	ZifPackage *package;
	ZifState *state_local;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   10, /* add remote */
				   10, /* find packages */
				   80, /* run transaction */
				   -1);
	g_assert (ret);

	/* get a store_array of remote stores */
	store_array = zif_store_array_new ();
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* use these stores for the transaction */
	zif_transaction_set_stores_remote (priv->transaction, store_array);

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	state_local = zif_state_get_child (priv->state);
	full_paths = pk_backend_get_strv (backend, "full_paths");
	zif_state_set_number_steps (state_local, g_strv_length (full_paths));
	for (i=0; full_paths[i] != NULL; i++) {

		/* find package */
		package = zif_package_local_new ();
		ret = zif_package_local_set_from_filename (ZIF_PACKAGE_LOCAL (package),
							   full_paths[i],
							   &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to create package for %s: %s",
					       full_paths[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* add it as an install to the transaction */
		ret = zif_transaction_add_install (priv->transaction,
						   package,
						   &error);
		g_object_unref (package);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
					       "failed to add package %s: %s",
					       zif_package_get_printable (package),
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* run transaction */
	state_local = zif_state_get_child (priv->state);
	ret = pk_backend_run_transaction (backend, state_local);
	if (!ret)
		goto out;

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_refresh_cache_thread:
 */
static gboolean
pk_backend_refresh_cache_thread (PkBackend *backend)
{
	gboolean force = pk_backend_get_bool (backend, "force");
	gboolean ret;
	GError *error = NULL;
	GPtrArray *store_array = NULL;
	ZifState *state_local;

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   50, /* get stores */
				   50, /* refresh them */
				   -1);
	g_assert (ret);

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
	ret = zif_store_array_add_remote_enabled (store_array,
						  state_local,
						  &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to add enabled stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* clean all the repos */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_error_handler (priv->state, (ZifStateErrorHandlerCb) pk_backend_error_handler_cb, backend);
	ret = zif_store_array_clean (store_array, state_local, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to clean: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * pk_backend_get_repo_list_thread:
 */
static gboolean
pk_backend_get_repo_list_thread (PkBackend *backend)
{
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

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   50, /* get stores */
				   50, /* process and emit */
				   -1);
	g_assert (ret);

	state_local = zif_state_get_child (priv->state);
	array = zif_repos_get_stores (priv->repos, state_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_REPO_NOT_FOUND,
				       "failed to find repos: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* none? */
	if (array->len == 0) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_REPO_NOT_FOUND,
				       "failed to find any repos");
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* looks at each store */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, array->len);
	for (i=0; i<array->len; i++) {
		store = g_ptr_array_index (array, i);

		/* allow filtering on devel */
		state_loop = zif_state_get_child (state_local);
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {

			/* devel, name, enabled */
			ret = zif_state_set_steps (state_loop,
						   NULL,
						   60, /* is store devel? */
						   20, /* get name */
						   20, /* get enabled */
						   -1);
			g_assert (ret);

			state_tmp = zif_state_get_child (state_loop);
			devel = zif_store_remote_is_devel (store, state_tmp, NULL);
			if (devel)
				goto skip;

			/* this section done */
			ret = zif_state_done (state_loop, &error);
			if (!ret) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
						       "cancelled: %s",
						       error->message);
				g_error_free (error);
				goto out;
			}
		} else {
			/* name, enabled */
			ret = zif_state_set_steps (state_loop,
						   NULL,
						   50, /* get name */
						   50, /* get enabled */
						   -1);
			g_assert (ret);
		}

		/* get name */
		state_tmp = zif_state_get_child (state_loop);
		name = zif_store_remote_get_name (store, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
			g_error_free (error);
			goto out;
		}

		/* get state */
		state_tmp = zif_state_get_child (state_loop);
		enabled = zif_store_remote_get_enabled (store, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
			g_error_free (error);
			goto out;
		}

		repo_id = zif_store_get_id (ZIF_STORE (store));
		pk_backend_repo_detail (backend, repo_id, name, enabled);
skip:
		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (array != NULL)
		g_ptr_array_unref (array);
	return TRUE;
}

/**
 * pk_backend_repo_enable_thread:
 */
static gboolean
pk_backend_repo_enable_thread (PkBackend *backend)
{
	ZifStoreRemote *repo = NULL;
	ZifState *state_local;
	gboolean ret;
	GError *error = NULL;
	gchar *warning = NULL;
	gboolean enabled = pk_backend_get_bool (backend, "enabled");
	const gchar *repo_id = pk_backend_get_string (backend, "repo_id");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* set steps */
	zif_state_set_number_steps (priv->state, 2);

	/* find the right repo */
	state_local = zif_state_get_child (priv->state);
	repo = zif_repos_get_store (priv->repos,
				    repo_id, state_local,
				    &error);
	if (repo == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_REPO_NOT_FOUND,
				       "failed to find repo: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* set the state */
	state_local = zif_state_get_child (priv->state);
	ret = zif_store_remote_set_enabled (repo,
					    enabled,
#if ZIF_CHECK_VERSION(0,1,6)
					    state_local,
#endif
					    &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_CANNOT_DISABLE_REPOSITORY,
				       "failed to set enable: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
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
					   "If this is not correct, please disable the %s software source.",
					   repo_id);
		pk_backend_message (backend,
				    PK_MESSAGE_ENUM_REPO_FOR_DEVELOPERS_ONLY,
				    warning);
	}
out:
	pk_backend_finished (backend);
	g_free (warning);
	if (repo != NULL)
		g_object_unref (repo);
	return TRUE;
}

/**
 * pk_backend_get_categories_thread:
 */
static gboolean
pk_backend_get_categories_thread (PkBackend *backend)
{
	const gchar *name;
	const gchar *repo_id;
	gboolean enabled;
	gboolean ret;
	gchar *cat_id;
	GError *error = NULL;
	GPtrArray *array = NULL;
	GPtrArray *repos = NULL;
	GPtrArray *stores = NULL;
	guint i;
	ZifCategory *cat;
	ZifState *state_local;
	ZifState *state_loop;
	ZifState *state_tmp;
	ZifStoreRemote *store;

	/* set steps */
	ret = zif_state_set_steps (priv->state,
				   NULL,
				   25, /* get stores */
				   50, /* get cats */
				   5, /* get repos */
				   5, /* emit repos */
				   15, /* emit */
				   -1);
	g_assert (ret);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* get enabled repos */
	state_local = zif_state_get_child (priv->state);
	stores = zif_repos_get_stores_enabled (priv->repos,
					       state_local,
					       &error);
	if (stores == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,
				       "failed to add remote stores: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* get sorted list of unique categories */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_error_handler (priv->state,
				     (ZifStateErrorHandlerCb) pk_backend_error_handler_cb,
				     backend);
	array = zif_store_array_get_categories (stores, state_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_GROUP_LIST_INVALID,
				       "failed to add get categories: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* emit each cat obj */
	for (i=0; i<array->len; i++) {
		cat = g_ptr_array_index (array, i);
		/* in the yum backend, we signify a group with a '@' prefix */
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
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* add the repo category objects */
	pk_backend_category (backend,
			     NULL,
			     "repo:",
			     "Software Sources",
			     "Packages from specific software sources",
			     "base-system");
	state_local = zif_state_get_child (priv->state);
	repos = zif_repos_get_stores (priv->repos, state_local, &error);
	if (repos == NULL) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_REPO_NOT_FOUND,
				       "failed to find repos: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}

	/* looks at each store */
	state_local = zif_state_get_child (priv->state);
	zif_state_set_number_steps (state_local, repos->len);
	for (i=0; i<repos->len; i++) {
		store = g_ptr_array_index (repos, i);

		/* allow filtering on devel */
		state_loop = zif_state_get_child (state_local);

		/* devel, name, enabled */
		ret = zif_state_set_steps (state_loop,
					   NULL,
					   50, /* get enabled */
					   50, /* get name */
					   -1);
		g_assert (ret);

		state_tmp = zif_state_get_child (state_loop);
		enabled = zif_store_remote_get_enabled (store, state_tmp, NULL);
		if (!enabled) {
			ret = zif_state_finished (state_loop, &error);
			if (!ret) {
				pk_backend_error_code (backend,
						       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
						       "cancelled: %s",
						       error->message);
				g_error_free (error);
				goto out;
			}
			goto skip;
		}

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}

		/* get name */
		state_tmp = zif_state_get_child (state_loop);
		name = zif_store_remote_get_name (store, state_tmp, NULL);

		/* this section done */
		ret = zif_state_done (state_loop, &error);
		if (!ret) {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
			g_error_free (error);
			goto out;
		}

		/* emit */
		repo_id = zif_store_get_id (ZIF_STORE (store));
		cat_id = g_strdup_printf ("repo:%s", repo_id);
		pk_backend_category (backend,
				     "repo:",
				     cat_id,
				     name,
				     name,
				     "base-system");
		g_free (cat_id);
skip:
		/* this section done */
		ret = zif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_error_code (backend,
					       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "cancelled: %s",
					       error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* this section done */
	ret = zif_state_done (priv->state, &error);
	if (!ret) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				       "cancelled: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	if (repos != NULL)
		g_ptr_array_unref (repos);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (stores != NULL)
		g_ptr_array_unref (stores);
	return TRUE;
}

/**
  * pk_backend_upgrade_system_thread:
  */
static gboolean
pk_backend_upgrade_system_thread (PkBackend *backend)
{
	gchar **distro_id_split = NULL;
	guint version;
	gboolean ret;
	GError *error = NULL;
	ZifReleaseUpgradeKind upgrade_kind_zif = ZIF_RELEASE_UPGRADE_KIND_DEFAULT;
	PkUpgradeKindEnum upgrade_kind = pk_backend_get_uint (backend, "upgrade_kind");
	const gchar *distro_id = pk_backend_get_string (backend, "distro_id");

	/* check valid */
	distro_id_split = g_strsplit (distro_id, "-", -1);
	if (g_strv_length (distro_id_split) != 2) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				       "distribution id %s invalid",
				       distro_id);
		goto out;
	}

	/* check fedora */
	if (g_strcmp0 (distro_id_split[0], "fedora") != 0) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
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
		pk_backend_error_code (backend,
				       pk_backend_convert_error (error),
				       "failed to upgrade: %s",
				       error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_finished (backend);
	g_strfreev (distro_id_split);
	return TRUE;
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids,
			      const gchar *directory)
{
	pk_backend_thread_create (backend, pk_backend_download_packages_thread);
}

/**
 * pk_backend_get_categories:
 */
void
pk_backend_get_categories (PkBackend *backend)
{
	pk_backend_thread_create (backend, pk_backend_get_categories_thread);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters,
			gchar **package_ids, gboolean recursive)
{
	pk_backend_thread_create (backend, pk_backend_get_depends_thread);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_get_details_thread);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend)
{
	pk_backend_thread_create (backend, pk_backend_get_distro_upgrades_thread);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_get_files_thread);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, pk_backend_get_repo_list_thread);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters,
			 gchar **package_ids, gboolean recursive)
{
	pk_backend_thread_create (backend, pk_backend_get_requires_thread);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_get_update_detail_thread);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, pk_backend_get_updates_thread);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted,
			  gchar **full_paths)
{
	pk_backend_thread_create (backend, pk_backend_install_files_thread);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted,
			     gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_install_packages_thread);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_NO_NETWORK,
				       "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_thread_create (backend, pk_backend_refresh_cache_thread);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids,
			    gboolean allow_deps, gboolean autoremove)
{
	pk_backend_thread_create (backend, pk_backend_remove_packages_thread);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *repo_id, gboolean enabled)
{
	pk_backend_thread_create (backend, pk_backend_repo_enable_thread);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	pk_backend_set_strv (backend, "search", packages);
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, pk_backend_search_thread);
}

/**
 * pk_backend_simulate_install_files:
 */
void
pk_backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	pk_backend_thread_create (backend, pk_backend_install_files_thread);
}

/**
 * pk_backend_simulate_install_packages:
 */
void
pk_backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_install_packages_thread);
}

/**
 * pk_backend_simulate_remove_packages:
 */
void
pk_backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids,
				     gboolean autoremove)
{
	pk_backend_thread_create (backend, pk_backend_remove_packages_thread);
}

/**
 * pk_backend_simulate_update_packages:
 */
void
pk_backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_update_packages_thread);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	pk_backend_thread_create (backend, pk_backend_update_packages_thread);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_thread_create (backend, pk_backend_update_system_thread);
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend,
			   const gchar *distro_id,
			   PkUpgradeKindEnum upgrade_kind)
{
	pk_backend_thread_create (backend, pk_backend_upgrade_system_thread);
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBitfield filters,
			  PkProvidesEnum provides, gchar **values)
{
	guint i;
	guint len;
	gchar **search = NULL;
	GPtrArray *array = NULL;

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
		} else if (provides == PK_PROVIDES_ENUM_PLASMA_SERVICE) {
			g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_ANY) {
			g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
		} else {
			pk_backend_error_code (backend,
				       PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED,
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
