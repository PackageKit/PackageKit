/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gmodule.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <appstream-glib.h>

#include <pk-backend.h>
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-debug.h>

#include <libdnf/libdnf.h>
#include <libdnf/dnf-advisory.h>
#include <libdnf/dnf-advisoryref.h>
#include <libdnf/hy-packageset.h>
#include <libdnf/hy-query.h>
#include <libdnf/dnf-version.h>
#include <libdnf/hy-util.h>
#include <librepo/librepo.h>

#include "dnf-backend-vendor.h"
#include "dnf-backend.h"

typedef struct {
	DnfSack		*sack;
	gboolean	 valid;
	gchar		*key;
} DnfSackCacheItem;

typedef struct {
	GKeyFile	*conf;
	DnfContext	*context;
	GHashTable	*sack_cache;	/* of DnfSackCacheItem */
	GMutex		 sack_mutex;
	GTimer		*repos_timer;
} PkBackendDnfPrivate;

typedef struct {
	GPtrArray	*sources;
	DnfContext	*context;
	DnfTransaction	*transaction;
	DnfState	*state;
	PkBackend	*backend;
	PkBitfield	 transaction_flags;
	HyGoal		 goal;
} PkBackendDnfJobData;

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "Dnf";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Richard Hughes <richard@hughsie.com>";
}

/**
 * pk_backend_supports_parallelization:
 */
gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
	return FALSE;
}

/**
 * pk_backend_sack_cache_invalidate:
 **/
static void
pk_backend_sack_cache_invalidate (PkBackend *backend, const gchar *why)
{
	GList *l;
	DnfSackCacheItem *cache_item;
	PkBackendDnfPrivate *priv = pk_backend_get_user_data (backend);
	g_autoptr(GList) values = NULL;

	/* set all the cached sacks as invalid */
	g_mutex_lock (&priv->sack_mutex);
	values = g_hash_table_get_values (priv->sack_cache);
	for (l = values; l != NULL; l = l->next) {
		cache_item = l->data;
		if (cache_item->valid) {
			g_debug ("invalidating %s as %s", cache_item->key, why);
			cache_item->valid = FALSE;
		}
	}
	g_mutex_unlock (&priv->sack_mutex);
}

/**
 * pk_backend_yum_repos_changed_cb:
 **/
static void
pk_backend_yum_repos_changed_cb (DnfRepoLoader *self, PkBackend *backend)
{
	pk_backend_sack_cache_invalidate (backend, "yum.repos.d changed");
	pk_backend_repo_list_changed (backend);
}

/**
 * dnf_sack_cache_item_free:
 */
static void
dnf_sack_cache_item_free (DnfSackCacheItem *cache_item)
{
	g_object_unref (cache_item->sack);
	g_free (cache_item->key);
	g_slice_free (DnfSackCacheItem, cache_item);
}

/**
 * pk_backend_context_invalidate_cb:
 */
static void
pk_backend_context_invalidate_cb (DnfContext *context,
				 const gchar *message,
				 PkBackend *backend)
{
	pk_backend_sack_cache_invalidate (backend, message);
	pk_backend_installed_db_changed (backend);
}

/**
 * pk_backend_setup_dnf_context:
 */
static gboolean
pk_backend_setup_dnf_context (DnfContext *context, GKeyFile *conf, const gchar *release_ver, GError **error)
{
	gboolean keep_cache;
	g_autofree gchar *cache_dir = NULL;
	g_autofree gchar *destdir = NULL;
	g_autofree gchar *lock_dir = NULL;
	g_autofree gchar *repo_dir = NULL;
	g_autofree gchar *solv_dir = NULL;

	destdir = g_key_file_get_string (conf, "Daemon", "DestDir", NULL);
	if (destdir == NULL)
		destdir = g_strdup ("/");
	dnf_context_set_install_root (context, destdir);
	cache_dir = g_build_filename (destdir, "/var/cache/PackageKit", release_ver, "metadata", NULL);
	dnf_context_set_cache_dir (context, cache_dir);
	solv_dir = g_build_filename (destdir, "/var/cache/PackageKit", release_ver, "hawkey", NULL);
	dnf_context_set_solv_dir (context, solv_dir);
	repo_dir = g_build_filename (destdir, "/etc/yum.repos.d", NULL);
	dnf_context_set_repo_dir (context, repo_dir);
	lock_dir = g_build_filename (destdir, "/var/run", NULL);
	dnf_context_set_lock_dir (context, lock_dir);
	dnf_context_set_release_ver (context, release_ver);
	dnf_context_set_rpm_verbosity (context, "info");

	/* use this initial data if repos are not present */
	dnf_context_set_vendor_cache_dir (context, "/usr/share/PackageKit/metadata");
	dnf_context_set_vendor_solv_dir (context, "/usr/share/PackageKit/hawkey");

	/* do we keep downloaded packages */
	keep_cache = g_key_file_get_boolean (conf, "Daemon", "KeepCache", NULL);
	dnf_context_set_keep_cache (context, keep_cache);

	/* set up context */
	return dnf_context_setup (context, NULL, error);
}

/**
 * pk_backend_initialize:
 */
void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	gboolean ret;
	PkBackendDnfPrivate *priv;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *release_ver = NULL;

	/* use logging */
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	pk_debug_add_log_domain ("Dnf");

	/* create private area */
	priv = g_new0 (PkBackendDnfPrivate, 1);
	pk_backend_set_user_data (backend, priv);

	g_debug ("Using Dnf %i.%i.%i",
		 LIBDNF_MAJOR_VERSION,
		 LIBDNF_MINOR_VERSION,
		 LIBDNF_MICRO_VERSION);
	g_debug ("Using librepo %i.%i.%i",
		 LR_VERSION_MAJOR,
		 LR_VERSION_MINOR,
		 LR_VERSION_PATCH);

	release_ver = pk_get_distro_version_id (&error);
	if (release_ver == NULL)
		g_error ("Failed to parse os-release: %s", error->message);

	/* a cache of DnfSacks with the key being which sacks are loaded
	 *
	 * notes:
	 * - this deals with deallocating the sack when the backend is unloaded
	 * - all the cached sacks are dropped on any transaction that can
	 *   modify state or if the repos or rpmdb are changed
	 */
	g_mutex_init (&priv->sack_mutex);
	priv->sack_cache = g_hash_table_new_full (g_str_hash,
						  g_str_equal,
						  g_free,
						  (GDestroyNotify) dnf_sack_cache_item_free);

	priv->conf = g_key_file_ref (conf);

	/* set defaults */
	priv->context = dnf_context_new ();
	g_signal_connect (priv->context, "invalidate",
			  G_CALLBACK (pk_backend_context_invalidate_cb), backend);
	ret = pk_backend_setup_dnf_context (priv->context, conf, release_ver, &error);
	if (!ret)
		g_error ("failed to setup context: %s", error->message);

	/* use context's repository loaders */
	priv->repos_timer = g_timer_new ();
	g_signal_connect (dnf_context_get_repo_loader (priv->context), "changed",
			  G_CALLBACK (pk_backend_yum_repos_changed_cb), backend);

	lr_global_init ();
}

/**
 * pk_backend_destroy:
 */
void
pk_backend_destroy (PkBackend *backend)
{
	PkBackendDnfPrivate *priv = pk_backend_get_user_data (backend);
	if (priv->conf != NULL)
		g_key_file_unref (priv->conf);
	if (priv->context != NULL)
		g_object_unref (priv->context);
	g_timer_destroy (priv->repos_timer);
	g_mutex_clear (&priv->sack_mutex);
	g_hash_table_unref (priv->sack_cache);
	g_free (priv);
}

/**
 * pk_backend_state_percentage_changed_cb:
 */
static void
pk_backend_state_percentage_changed_cb (DnfState *state,
					guint percentage,
					PkBackendJob *job)
{
	pk_backend_job_set_percentage (job, percentage);
}

/**
 * pk_backend_state_action_changed_cb:
 **/
static void
pk_backend_state_action_changed_cb (DnfState *state,
				    DnfStateAction action,
				    const gchar *action_hint,
				    PkBackendJob *job)
{
	if (action != DNF_STATE_ACTION_UNKNOWN) {
		g_debug ("got state %s with hint %s",
			 pk_status_enum_to_string (action),
			 action_hint);
		pk_backend_job_set_status (job, action);
	}

	switch (action) {
	case DNF_STATE_ACTION_DOWNLOAD_PACKAGES:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_DOWNLOADING,
						action_hint,
						"");
		}
		break;
	case DNF_STATE_ACTION_INSTALL:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_INSTALLING,
						action_hint,
						"");
		}
		break;
	case DNF_STATE_ACTION_REINSTALL:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_REINSTALLING,
						action_hint,
						"");
		}
		break;
	case DNF_STATE_ACTION_REMOVE:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_REMOVING,
						action_hint,
						"");
		}
		break;
	case DNF_STATE_ACTION_DOWNGRADE:
	case DNF_STATE_ACTION_UPDATE:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_UPDATING,
						action_hint,
						"");
		}
		break;
	case DNF_STATE_ACTION_CLEANUP:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_CLEANUP,
						action_hint,
						"");
		}
		break;
	default:
		break;
	}
}

/**
 * pk_backend_speed_changed_cb:
 **/
static void
pk_backend_speed_changed_cb (DnfState *state,
			     GParamSpec *pspec,
			     PkBackendJob *job)
{
	pk_backend_job_set_speed (job, dnf_state_get_speed (state));
}

/**
 * pk_backend_state_allow_cancel_changed_cb:
 **/
static void
pk_backend_state_allow_cancel_changed_cb (DnfState *state,
					  gboolean allow_cancel,
					  PkBackendJob *job)
{
	pk_backend_job_set_allow_cancel (job, allow_cancel);
}

static void
pk_backend_job_set_context (PkBackendJob *job, DnfContext *context)
{
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	const gchar *value;

	/* DnfContext */
	g_clear_object (&job_data->context);
	job_data->context = g_object_ref (context);

	/* set proxy */
	value = pk_backend_job_get_proxy_http (job);
	if (value != NULL) {
		g_autofree gchar *uri = pk_backend_convert_uri (value);
		dnf_context_set_http_proxy (job_data->context, uri);
	}

	/* transaction */
	g_clear_object (&job_data->transaction);
	job_data->transaction = dnf_transaction_new (job_data->context);
	dnf_transaction_set_repos (job_data->transaction,
	                           dnf_context_get_repos (job_data->context));
	dnf_transaction_set_uid (job_data->transaction,
				 pk_backend_job_get_uid (job));
}

/**
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendDnfPrivate *priv = pk_backend_get_user_data (backend);
	PkBackendDnfJobData *job_data;
	job_data = g_new0 (PkBackendDnfJobData, 1);
	job_data->backend = backend;
	pk_backend_job_set_user_data (job, job_data);

	/* DnfState */
	job_data->state = dnf_state_new ();
	dnf_state_set_cancellable (job_data->state,
				   pk_backend_job_get_cancellable (job));
	g_signal_connect (job_data->state, "percentage-changed",
			  G_CALLBACK (pk_backend_state_percentage_changed_cb),
			  job);
	g_signal_connect (job_data->state, "action-changed",
			  G_CALLBACK (pk_backend_state_action_changed_cb),
			  job);
	g_signal_connect (job_data->state, "allow-cancel-changed",
			  G_CALLBACK (pk_backend_state_allow_cancel_changed_cb),
			  job);
	g_signal_connect (job_data->state, "notify::speed",
			  G_CALLBACK (pk_backend_speed_changed_cb),
			  job);

	pk_backend_job_set_context (job, priv->context);

#ifdef PK_BUILD_LOCAL
	/* we don't want to enable this for normal runtime */
	dnf_state_set_enable_profile (job_data->state, TRUE);
#endif

	/* no locks to get, so jump straight to 'running' */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);
}

/**
 * pk_backend_stop_job:
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);

	if (job_data->state != NULL) {
		dnf_state_release_locks (job_data->state);
		g_object_unref (job_data->state);
	}
	if (job_data->transaction != NULL)
		g_object_unref (job_data->transaction);
	if (job_data->context != NULL)
		g_object_unref (job_data->context);
	if (job_data->sources != NULL)
		g_ptr_array_unref (job_data->sources);
	if (job_data->goal != NULL)
		hy_goal_free (job_data->goal);
	g_free (job_data);
	pk_backend_job_set_user_data (job, NULL);
}

/**
 * pk_backend_ensure_repos:
 */
static gboolean
pk_backend_ensure_repos (PkBackendDnfJobData *job_data, GError **error)
{
	/* already set */
	if (job_data->sources != NULL)
		return TRUE;

	/* set the list of repos */
	job_data->sources = dnf_repo_loader_get_repos (dnf_context_get_repo_loader (job_data->context), error);
	if (job_data->sources == NULL)
		return FALSE;
	return TRUE;
}

/**
 * dnf_utils_add_remote:
 */
static gboolean
dnf_utils_add_remote (PkBackendJob *job,
		      DnfSack *sack,
		      DnfSackAddFlags flags,
		      DnfState *state,
		      GError **error)
{
	gboolean ret;
	DnfState *state_local;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);

	/* set state */
	ret = dnf_state_set_steps (state, error,
				   2, /* load files */
				   98, /* add sources */
				   -1);
	if (!ret)
		return FALSE;

	/* set the list of repos */
	if (!pk_backend_ensure_repos (job_data, error))
		return FALSE;

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;

	/* add each repo */
	state_local = dnf_state_get_child (state);
	ret = dnf_sack_add_repos (sack,
	                          job_data->sources,
	                          pk_backend_job_get_cache_age (job),
	                          flags,
	                          state_local,
	                          error);
	if (!ret)
		return FALSE;

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;
	return TRUE;
}

typedef enum {
	DNF_CREATE_SACK_FLAG_NONE,
	DNF_CREATE_SACK_FLAG_USE_CACHE,
	DNF_CREATE_SACK_FLAG_LAST
} DnfCreateSackFlags;

/**
 * dnf_utils_create_cache_key:
 */
static gchar *
dnf_utils_create_cache_key (const gchar *release_ver, DnfSackAddFlags flags)
{
	GString *key;

	key = g_string_new ("DnfSack::");
	g_string_append_printf (key, "release_ver[%s]::", release_ver);

	if (flags == DNF_SACK_ADD_FLAG_NONE) {
		g_string_append (key, "none");
	} else {
		if (flags & DNF_SACK_ADD_FLAG_FILELISTS)
			g_string_append (key, "filelists|");
		if (flags & DNF_SACK_ADD_FLAG_UPDATEINFO)
			g_string_append (key, "updateinfo|");
		if (flags & DNF_SACK_ADD_FLAG_REMOTE)
			g_string_append (key, "remote|");
		if (flags & DNF_SACK_ADD_FLAG_UNAVAILABLE)
			g_string_append (key, "unavailable|");
		g_string_truncate (key, key->len - 1);
	}
	return g_string_free (key, FALSE);
}

/**
 * dnf_utils_real_path:
 *
 * Resolves paths like ../../Desktop/bar.rpm to /home/hughsie/Desktop/bar.rpm
 **/
static gchar *
dnf_utils_real_path (const gchar *path)
{
	gchar *real = NULL;
	char *temp;

	/* don't trust realpath one little bit */
	if (path == NULL)
		return NULL;

	/* glibc allocates us a buffer to try and fix some brain damage */
	temp = realpath (path, NULL);
	if (temp == NULL)
		return NULL;
	real = g_strdup (temp);
	free (temp);
	return real;
}

/**
 * dnf_utils_create_sack_for_filters:
 */
static DnfSack *
dnf_utils_create_sack_for_filters (PkBackendJob *job,
				   PkBitfield filters,
				   DnfCreateSackFlags create_flags,
				   DnfState *state,
				   GError **error)
{
	gboolean ret;
	DnfSackAddFlags flags = DNF_SACK_ADD_FLAG_FILELISTS;
	DnfSackCacheItem *cache_item = NULL;
	DnfState *state_local;
	DnfSack *sack = NULL;
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBackendDnfPrivate *priv = pk_backend_get_user_data (backend);
	g_autofree gchar *cache_key = NULL;
	g_autofree gchar *install_root = NULL;
	g_autofree gchar *solv_dir = NULL;

	/* don't add if we're going to filter out anyway */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		flags |= DNF_SACK_ADD_FLAG_REMOTE;

	/* only load updateinfo when required */
	if (pk_backend_job_get_role (job) == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		flags |= DNF_SACK_ADD_FLAG_UPDATEINFO;

	/* only use unavailble packages for queries */
	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_RESOLVE:
	case PK_ROLE_ENUM_SEARCH_NAME:
	case PK_ROLE_ENUM_SEARCH_DETAILS:
	case PK_ROLE_ENUM_SEARCH_FILE:
	case PK_ROLE_ENUM_GET_DETAILS:
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		flags |= DNF_SACK_ADD_FLAG_UNAVAILABLE;
		break;
	default:
		break;
	}

	/* media repos could disappear at any time */
	if ((create_flags & DNF_CREATE_SACK_FLAG_USE_CACHE) > 0 &&
	    dnf_repo_loader_has_removable_repos (dnf_context_get_repo_loader (job_data->context)) &&
	    g_timer_elapsed (priv->repos_timer, NULL) > 1.0f) {
		g_debug ("not reusing sack as media may have disappeared");
		create_flags &= ~DNF_CREATE_SACK_FLAG_USE_CACHE;
	}
	g_timer_reset (priv->repos_timer);

	/* if we've specified a specific cache-age then do not use the cache */
	if ((flags & DNF_SACK_ADD_FLAG_REMOTE) > 0 &&
	    pk_backend_job_get_cache_age (job) != G_MAXUINT) {
		g_debug ("not reusing sack specific cache age requested");
		create_flags &= ~DNF_CREATE_SACK_FLAG_USE_CACHE;
	}

	/* do we have anything in the cache */
	cache_key = dnf_utils_create_cache_key (dnf_context_get_release_ver (job_data->context), flags);
	if ((create_flags & DNF_CREATE_SACK_FLAG_USE_CACHE) > 0) {
		g_mutex_lock (&priv->sack_mutex);
		cache_item = g_hash_table_lookup (priv->sack_cache, cache_key);
		if (cache_item != NULL && cache_item->sack != NULL) {
			if (cache_item->valid) {
				ret = TRUE;
				g_debug ("using cached sack %s", cache_key);
				sack = cache_item->sack;
				g_mutex_unlock (&priv->sack_mutex);
				goto out;
			} else {
				/* we have to do this now rather than rely on the
				 * callback of the hash table */
				g_hash_table_remove (priv->sack_cache, cache_key);
			}
		}
		g_mutex_unlock (&priv->sack_mutex);
	}

	/* update status */
	dnf_state_action_start (state, DNF_STATE_ACTION_QUERY, NULL);

	/* set state */
	if ((flags & DNF_SACK_ADD_FLAG_REMOTE) > 0) {
		ret = dnf_state_set_steps (state, error,
					   8, /* add installed */
					   92, /* add remote */
					   -1);
		if (!ret)
			goto out;
	} else {
		dnf_state_set_number_steps (state, 1);
	}

	/* create empty sack */
	solv_dir = dnf_utils_real_path (dnf_context_get_solv_dir (job_data->context));
	install_root = dnf_utils_real_path (dnf_context_get_install_root (job_data->context));
	sack = dnf_sack_new ();
	dnf_sack_set_cachedir (sack, solv_dir);
	dnf_sack_set_rootdir (sack, install_root);
	ret = dnf_sack_setup (sack, DNF_SACK_SETUP_FLAG_MAKE_CACHE_DIR, error);
	if (!ret) {
		g_prefix_error (error, "failed to create sack in %s for %s: ",
				dnf_context_get_solv_dir (job_data->context),
				dnf_context_get_install_root (job_data->context));
		goto out;
	}

	/* add installed packages */
	ret = dnf_sack_load_system_repo (sack, NULL, DNF_SACK_LOAD_FLAG_BUILD_CACHE, error);
	if (!ret) {
		g_prefix_error (error, "Failed to load system repo: ");
		goto out;
	}

	/* done */
	ret = dnf_state_done (state, error);
	if (!ret)
		goto out;

	/* add remote packages */
	if ((flags & DNF_SACK_ADD_FLAG_REMOTE) > 0) {
		state_local = dnf_state_get_child (state);
		ret = dnf_utils_add_remote (job, sack, flags,
					    state_local, error);
		if (!ret)
			goto out;

		/* done */
		ret = dnf_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* save in cache */
	g_mutex_lock (&priv->sack_mutex);
	cache_item = g_slice_new (DnfSackCacheItem);
	cache_item->key = g_strdup (cache_key);
	cache_item->sack = sack;
	cache_item->valid = TRUE;
	g_debug ("created cached sack %s", cache_item->key);
	g_hash_table_insert (priv->sack_cache, g_strdup (cache_key), cache_item);
	g_mutex_unlock (&priv->sack_mutex);
out:
	if (!ret && sack != NULL) {
		g_object_unref (sack);
		sack = NULL;
	}
	return sack;
}

/**
 * dnf_utils_run_query_with_newest_filter:
 */
static GPtrArray *
dnf_utils_run_query_with_newest_filter (DnfSack *sack, HyQuery query)
{
	GPtrArray *results;
	GPtrArray *results_tmp;
	DnfPackageSet *pkgset;
	DnfPackage *pkg;
	HyQuery query_tmp;
	guint i;

	/* Run the prepared query */
	pkgset = hy_query_run_set (query);

	/* Filter latest system packages */
	query_tmp = hy_query_create (sack);
	hy_query_filter_package_in (query_tmp, HY_PKG, HY_EQ, pkgset);
	hy_query_filter (query_tmp, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	hy_query_filter_latest_per_arch (query_tmp, TRUE);
	results = hy_query_run (query_tmp);
	hy_query_free (query_tmp);

	/* Filter latest available packages */
	query_tmp = hy_query_create (sack);
	hy_query_filter_package_in (query_tmp, HY_PKG, HY_EQ, pkgset);
	hy_query_filter (query_tmp, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);
	hy_query_filter_latest_per_arch (query_tmp, TRUE);
	results_tmp = hy_query_run (query_tmp);
	/* ... and add to the previous results */
	for (i = 0; i < results_tmp->len; i++) {
		pkg = g_ptr_array_index (results_tmp, i);
		g_ptr_array_add (results, g_object_ref (pkg));
	}
	hy_query_free (query_tmp);
	g_ptr_array_unref (results_tmp);

	g_object_unref (pkgset);

	return results;
}

/**
 * dnf_utils_run_query_with_filters:
 */
static GPtrArray *
dnf_utils_run_query_with_filters (PkBackendJob *job, DnfSack *sack,
				  HyQuery query, PkBitfield filters)
{
	GPtrArray *results;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	const gchar *application_glob = "/usr/share/applications/*.desktop";

	/* arch */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH)) {
		hy_query_filter_in (query, HY_PKG_ARCH, HY_EQ,
				    dnf_context_get_native_arches (job_data->context));
	} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH)) {
		hy_query_filter_in (query, HY_PKG_ARCH, HY_NEQ,
				    dnf_context_get_native_arches (job_data->context));
	}

	/* installed */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
		hy_query_filter (query, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);

	/* source */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SOURCE))
		hy_query_filter (query, HY_PKG_ARCH, HY_EQ, "src");
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SOURCE))
		hy_query_filter (query, HY_PKG_ARCH, HY_NEQ, "src");

	/* application */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_APPLICATION))
		hy_query_filter (query, HY_PKG_FILE, HY_GLOB, application_glob);
	else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_APPLICATION))
		hy_query_filter (query, HY_PKG_FILE, HY_NOT | HY_GLOB, application_glob);

	/* newest */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		results = dnf_utils_run_query_with_newest_filter (sack, query);
	else
		results = hy_query_run (query);

	return results;
}

/**
 * pk_backend_what_provides_decompose:
 */
static gchar **
pk_backend_what_provides_decompose (gchar **values, GError **error)
{
	guint i;
	GPtrArray *array;

	/* iter on each provide string, and wrap it with the fedora prefix */
	array = g_ptr_array_new ();
	for (i = 0; values[i] != NULL; i++) {
		g_ptr_array_add (array, g_strdup (values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer1(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma5(%s)", values[i]));
	}
	g_ptr_array_add (array, NULL);
	return (gchar **) g_ptr_array_free (array, FALSE);
}

/**
 * dnf_package_get_advisory:
 */
static DnfAdvisory *
dnf_package_get_advisory (DnfPackage *package)
{
	GPtrArray *advisorylist;
	DnfAdvisory *advisory = NULL;

	advisorylist = dnf_package_get_advisories (package, HY_EQ);

	if (advisorylist->len > 0)
		advisory = g_object_ref (g_ptr_array_index (advisorylist, 0));
	g_ptr_array_unref (advisorylist);

	return advisory;
}

/**
 * pk_backend_search_thread:
 */
static void
pk_backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	DnfDb *db;
	DnfState *state_local;
	GPtrArray *installs = NULL;
	GPtrArray *pkglist = NULL;
	HyQuery query = NULL;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = 0;
	g_autofree gchar **search_tmp = NULL;
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) search = NULL;

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   39, /* add repos */
				   50, /* query */
				   1, /* ensure source list */
				   1, /* ensure origin */
				   9, /* emit */
				   -1);
	g_assert (ret);

	/* get arguments */
	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_GET_UPDATES:
	case PK_ROLE_ENUM_GET_PACKAGES:
		g_variant_get (params, "(t)", &filters);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		g_variant_get (params, "(t^a&s)",
			       &filters,
			       &search_tmp);
		search = pk_backend_what_provides_decompose (search_tmp, &error);
		if (search == NULL) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			goto out;
		}
		break;
	default:
		g_variant_get (params, "(t^as)", &filters, &search);
		break;
	}

	/* set the list of repos */
	ret = pk_backend_ensure_repos (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* get sack */
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* run query */
	query = hy_query_create (sack);
	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_GET_PACKAGES:
		pkglist = dnf_utils_run_query_with_filters (job, sack, query, filters);
		break;
	case PK_ROLE_ENUM_RESOLVE:
		hy_query_filter_in (query, HY_PKG_NAME, HY_EQ, (const gchar **) search);
		pkglist = dnf_utils_run_query_with_filters (job, sack, query, filters);
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		hy_query_filter_in (query, HY_PKG_FILE, HY_EQ, (const gchar **) search);
		pkglist = dnf_utils_run_query_with_filters (job, sack, query, filters);
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		hy_query_filter_in (query, HY_PKG_DESCRIPTION, HY_SUBSTR, (const gchar **) search);
		pkglist = dnf_utils_run_query_with_filters (job, sack, query, filters);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		hy_query_filter_in (query, HY_PKG_NAME, HY_SUBSTR, (const gchar **) search);
		pkglist = dnf_utils_run_query_with_filters (job, sack, query, filters);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		hy_query_filter_provides_in (query, search);
		pkglist = dnf_utils_run_query_with_filters (job, sack, query, filters);
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		/* set up the sack for packages that should only ever be installed, never updated */
		dnf_sack_set_installonly (sack, dnf_context_get_installonly_pkgs (job_data->context));
		dnf_sack_set_installonly_limit (sack, dnf_context_get_installonly_limit (job_data->context));

		job_data->goal = hy_goal_create (sack);
		hy_goal_upgrade_all (job_data->goal);
		ret = dnf_goal_depsolve (job_data->goal, DNF_ALLOW_UNINSTALL, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			goto out;
		}
		/* get packages marked for upgrade */
		pkglist = hy_goal_list_upgrades (job_data->goal, NULL);
		/* add any packages marked for install */
		installs = hy_goal_list_installs (job_data->goal, NULL);
		if (installs != NULL) {
			guint i;
			DnfPackage *pkg;

			for (i = 0; i < installs->len; i++) {
				pkg = g_ptr_array_index (installs, i);
				g_ptr_array_add (pkglist, g_object_ref (pkg));
			}
		}
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* set the src on each package */
	ret = dnf_transaction_ensure_repo_list (job_data->transaction, pkglist, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* set the origin on each package */
	db = dnf_transaction_get_db (job_data->transaction);
	dnf_db_ensure_origin_pkglist (db, pkglist);

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* FIXME: actually get the right update severity */
	if (pk_backend_job_get_role (job) == PK_ROLE_ENUM_GET_UPDATES) {
		guint i;
		DnfPackage *pkg;
		DnfAdvisory *advisory;
		DnfAdvisoryKind kind;
		PkInfoEnum info_enum;
		for (i = 0; i < pkglist->len; i++) {
			pkg = g_ptr_array_index (pkglist, i);
			advisory = dnf_package_get_advisory (pkg);
			if (advisory != NULL) {
				kind = dnf_advisory_get_kind (advisory);
				g_object_unref (advisory);
				info_enum = dnf_advisory_kind_to_info_enum (kind);
				dnf_package_set_info (pkg, info_enum);
			}
		}
	}

	dnf_emit_package_list_filter (job, filters, pkglist);

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}
out:
	if (installs != NULL)
		g_ptr_array_unref (installs);
	if (pkglist != NULL)
		g_ptr_array_unref (pkglist);
	if (query != NULL)
		hy_query_free (query);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend,
			 PkBackendJob *job,
			 PkBitfield filters)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend,
		    PkBackendJob *job,
		    PkBitfield filters,
		    gchar **package_ids)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar **values)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend,
			   PkBackendJob *job,
			   PkBitfield filters,
			   gchar **values)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar **values)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield filters,
			  gchar **values)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend,
			PkBackendJob *job,
			PkBitfield filters)
{
	pk_backend_job_thread_create (job, pk_backend_search_thread, NULL, NULL);
}

/* Obviously hardcoded based on the repository ID labels.
 * Colin Walters thinks this concept should be based on
 * user's trust of a GPG key or something more flexible.
 */
static gboolean
source_is_supported (DnfRepo *source)
{
	return dnf_validate_supported_source (dnf_repo_get_id (source));
}

/**
 * pk_backend_source_filter:
 */
static gboolean
pk_backend_source_filter (DnfRepo *src, PkBitfield filters)
{
	/* devel and ~devel */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) &&
	    !dnf_repo_is_devel (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) &&
	    dnf_repo_is_devel (src))
		return FALSE;

	/* source and ~source */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SOURCE) &&
	    !dnf_repo_is_repo (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SOURCE) &&
	    dnf_repo_is_repo (src))
		return FALSE;

	/* installed and ~installed == enabled */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
	    dnf_repo_get_enabled (src) == DNF_REPO_ENABLED_NONE)
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) &&
	    dnf_repo_get_enabled (src) != DNF_REPO_ENABLED_NONE)
		return FALSE;

	/* supported and ~supported == core */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED) &&
	    !source_is_supported (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SUPPORTED) &&
	    source_is_supported (src))
		return FALSE;

	/* not filtered */
	return TRUE;
}

/**
 * pk_backend_get_repo_list_thread:
 */
static void
pk_backend_get_repo_list_thread (PkBackendJob *job,
				 GVariant *params,
				 gpointer user_data)
{
	gboolean enabled;
	guint i;
	DnfRepo *src;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	g_autoptr(GPtrArray) sources = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(t)", &filters);

	/* set the list of repos */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	sources = dnf_repo_loader_get_repos (dnf_context_get_repo_loader (job_data->context), &error);
	if (sources == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to scan yum.repos.d: %s",
					   error->message);
		return;
	}

	/* none? */
	if (sources->len == 0) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_REPO_NOT_FOUND,
					   "failed to find any repos");
		return;
	}

	/* emit each repo */
	for (i = 0; i < sources->len; i++) {
		g_autofree gchar *description = NULL;
		src = g_ptr_array_index (sources, i);
		if (!pk_backend_source_filter (src, filters))
			continue;
		description = dnf_repo_get_description (src);
		enabled = (dnf_repo_get_enabled (src) & DNF_REPO_ENABLED_PACKAGES) > 0;
		pk_backend_job_repo_detail (job,
					    dnf_repo_get_id (src),
					    description, enabled);
	}
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield filters)
{
	pk_backend_job_thread_create (job, pk_backend_get_repo_list_thread, NULL, NULL);
}

/**
 * pk_backend_repo_set_data_thread:
 */
static void
pk_backend_repo_set_data_thread (PkBackendJob *job,
				 GVariant *params,
				 gpointer user_data)
{
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;
	gboolean ret = FALSE;
	DnfRepo *src;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(&s&s&s)", &repo_id, &parameter, &value);

	/* take lock */
	ret = dnf_state_take_lock (job_data->state,
				   DNF_LOCK_TYPE_REPO,
				   DNF_LOCK_MODE_PROCESS,
				   &error);
	if (!ret) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to get lock: %s",
					   error->message);
		goto out;
	}

	/* set the list of repos */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* find the correct repo */
	src = dnf_repo_loader_get_repo_by_id (dnf_context_get_repo_loader (job_data->context), repo_id, &error);
	if (src == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "%s", error->message);
		goto out;
	}

	/* check this isn't a waste of time */
	if (g_strcmp0 (parameter, "enabled") == 0) {
		ret = (dnf_repo_get_enabled (src) & DNF_REPO_ENABLED_PACKAGES) > 0;
		if (g_strcmp0 (value, "1") == 0 && ret) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_REPO_ALREADY_SET,
						   "repo already enabled");
			goto out;
		}
		if (g_strcmp0 (value, "0") == 0 && !ret) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_REPO_ALREADY_SET,
						   "repo already disabled");
			goto out;
		}
	}

	ret = dnf_repo_set_data (src, parameter, value, &error);
	if (!ret) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to write repo file: %s",
					   error->message);
		goto out;
	}
	ret = dnf_repo_commit (src, &error);
	if (!ret) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to write repo file: %s",
					   error->message);
		goto out;
	}

	/* nothing found */
	pk_backend_job_set_percentage (job, 100);
out:
	dnf_state_release_locks (job_data->state);
}

/**
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend,
			  PkBackendJob *job,
			  const gchar *repo_id,
			  const gchar *parameter,
			  const gchar *value)
{
	pk_backend_job_thread_create (job, pk_backend_repo_set_data_thread, NULL, NULL);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend,
			PkBackendJob *job,
			const gchar *repo_id,
			gboolean enabled)
{
	pk_backend_repo_set_data (backend, job, repo_id,
				  "enabled", enabled ? "1" : "0");
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_APPLICATION,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_SOURCE,
		PK_FILTER_ENUM_DOWNLOADED,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = { "application/x-rpm", NULL };
	return g_strdupv ((gchar **) mime_types);
}

/**
 * pk_backend_refresh_source:
 */
static gboolean
pk_backend_refresh_source (PkBackendJob *job,
			   DnfRepo *src,
			   DnfState *state,
			   GError **error)
{
	gboolean ret;
	gboolean src_okay;
	DnfState *state_local;
	GError *error_local = NULL;
	const gchar *as_basenames[] = { "appstream", "appstream-icons", NULL };
	const gchar *tmp;
	guint i;

	/* set state */
	ret = dnf_state_set_steps (state, error,
				   2, /* check */
				   98, /* download */
				   -1);
	if (!ret)
		return FALSE;

	/* is the source up to date? */
	state_local = dnf_state_get_child (state);
	src_okay = dnf_repo_check (src,
	                           pk_backend_job_get_cache_age (job),
	                           state_local,
	                           &error_local);
	if (!src_okay) {
		g_debug ("repo %s not okay [%s], refreshing",
			 dnf_repo_get_id (src), error_local->message);
		g_clear_error (&error_local);
		if (!dnf_state_finished (state_local, error))
			return FALSE;
	}

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;

	/* update repo, TODO: if we have network access */
	if (!src_okay) {
		state_local = dnf_state_get_child (state);
		ret = dnf_repo_update (src,
		                       DNF_REPO_UPDATE_FLAG_IMPORT_PUBKEY,
		                       state_local,
		                       &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     DNF_ERROR,
					     PK_ERROR_ENUM_CANNOT_FETCH_SOURCES)) {
				g_warning ("Skipping refresh of %s: %s",
					   dnf_repo_get_id (src),
					   error_local->message);
				g_clear_error (&error_local);
				if (!dnf_state_finished (state_local, error))
					return FALSE;
			} else {
				g_propagate_error (error, error_local);
				return FALSE;
			}
		}
	}

	/* copy the appstream files somewhere that the GUI will pick them up */
	for (i = 0; as_basenames[i] != NULL; i++) {
		tmp = dnf_repo_get_filename_md (src, as_basenames[i]);
		if (tmp != NULL) {
#if AS_CHECK_VERSION(0,3,4)
			if (!as_utils_install_filename (AS_UTILS_LOCATION_CACHE,
							tmp,
							dnf_repo_get_id (src),
							NULL,
							error)) {
				return FALSE;
			}
#else
			g_warning ("need to install AppStream metadata %s", tmp);
#endif
		}
	}

	/* done */
	return dnf_state_done (state, error);
}

/**
 * pk_backend_refresh_cache_thread:
 */
static void
pk_backend_refresh_cache_thread (PkBackendJob *job,
				 GVariant *params,
				 gpointer user_data)
{
	DnfRepo *src;
	DnfState *state_local;
	DnfState *state_loop;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	gboolean force;
	gboolean ret;
	guint cnt = 0;
	guint i;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) refresh_sources = NULL;

	/* set state */
	dnf_state_set_steps (job_data->state, NULL,
			     1, /* count */
			     95, /* download */
			     4, /* rebuild SAT */
			     -1);

	g_variant_get (params, "(b)", &force);

	/* set the list of repos */
	ret = pk_backend_ensure_repos (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* count the enabled sources */
	for (i = 0; i < job_data->sources->len; i++) {
		src = g_ptr_array_index (job_data->sources, i);
		if (dnf_repo_get_enabled (src) == DNF_REPO_ENABLED_NONE)
			continue;
		if (dnf_repo_get_kind (src) == DNF_REPO_KIND_MEDIA)
			continue;
		if (dnf_repo_get_kind (src) == DNF_REPO_KIND_LOCAL)
			continue;
		cnt++;
	}

	/* figure out which sources need refreshing */
	refresh_sources = g_ptr_array_new ();
	state_local = dnf_state_get_child (job_data->state);
	dnf_state_set_number_steps (state_local, cnt);
	for (i = 0; i < job_data->sources->len; i++) {
		gboolean src_okay;

		src = g_ptr_array_index (job_data->sources, i);
		if (dnf_repo_get_enabled (src) == DNF_REPO_ENABLED_NONE)
			continue;
		if (dnf_repo_get_kind (src) == DNF_REPO_KIND_MEDIA)
			continue;
		if (dnf_repo_get_kind (src) == DNF_REPO_KIND_LOCAL)
			continue;

		/* is the source up to date? */
		state_loop = dnf_state_get_child (state_local);
		src_okay = dnf_repo_check (src,
		                           pk_backend_job_get_cache_age (job),
		                           state_loop,
		                           NULL);
		if (!src_okay || force)
			g_ptr_array_add (refresh_sources,
			                 g_ptr_array_index (job_data->sources, i));

		/* done */
		ret = dnf_state_done (state_local, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			return;
		}
	}

	/* done */
	ret = dnf_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* is everything up to date? */
	if (refresh_sources->len == 0) {
		if (!dnf_state_finished (job_data->state, &error))
			pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* refresh each repo */
	state_local = dnf_state_get_child (job_data->state);
	dnf_state_set_number_steps (state_local, refresh_sources->len);
	for (i = 0; i < refresh_sources->len; i++) {
		src = g_ptr_array_index (refresh_sources, i);

		/* delete content even if up to date */
		if (force) {
			g_debug ("Deleting contents of %s as forced", dnf_repo_get_id (src));
			ret = dnf_repo_clean (src, &error);
			if (!ret) {
				pk_backend_job_error_code (job, error->code, "%s", error->message);
				return;
			}
		}

		/* check and download */
		state_loop = dnf_state_get_child (state_local);
		ret = pk_backend_refresh_source (job, src, state_loop, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			return;
		}

		/* done */
		ret = dnf_state_done (state_local, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			return;
		}
	}

	/* done */
	ret = dnf_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* regenerate the libsolv metadata */
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job, 0,
						  DNF_CREATE_SACK_FLAG_NONE,
						  state_local, &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	ret = dnf_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend,
			  PkBackendJob *job,
			  gboolean force)
{
	pk_backend_job_thread_create (job, pk_backend_refresh_cache_thread, NULL, NULL);
}

/**
 * dnf_utils_find_package_ids:
 *
 * Returns a hash table of all the packages found in the sack.
 * If a specific package-id is not found then the method does not fail, but
 * no package will be inserted into the hash table.
 *
 * If multiple packages are found, an error is returned, as the package-id is
 * supposed to uniquely identify the package across all repos.
 */
static GHashTable *
dnf_utils_find_package_ids (DnfSack *sack, gchar **package_ids, GError **error)
{
	const gchar *reponame;
	gboolean ret = TRUE;
	GHashTable *hash;
	guint i;
	GPtrArray *pkglist = NULL;
	DnfPackage *pkg;
	HyQuery query = NULL;

	/* run query */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) g_object_unref);
	query = hy_query_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		g_auto(GStrv) split = NULL;
		hy_query_clear (query);
		split = pk_package_id_split (package_ids[i]);
		reponame = split[PK_PACKAGE_ID_DATA];
		if (g_strcmp0 (reponame, "installed") == 0 ||
		    g_str_has_prefix (reponame, "installed:"))
			reponame = HY_SYSTEM_REPO_NAME;
		else if (g_strcmp0 (reponame, "local") == 0)
			reponame = HY_CMDLINE_REPO_NAME;
		hy_query_filter (query, HY_PKG_NAME, HY_EQ, split[PK_PACKAGE_ID_NAME]);
		hy_query_filter (query, HY_PKG_EVR, HY_EQ, split[PK_PACKAGE_ID_VERSION]);
		hy_query_filter (query, HY_PKG_ARCH, HY_EQ, split[PK_PACKAGE_ID_ARCH]);
		hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, reponame);
		pkglist = hy_query_run (query);

		/* no matches */
		if (pkglist->len == 0) {
			g_ptr_array_unref (pkglist);
			continue;
		}

		/* multiple matches */
		if (pkglist->len > 1) {
			ret = FALSE;
			g_set_error (error,
				     DNF_ERROR,
				     PK_ERROR_ENUM_PACKAGE_CONFLICTS,
				     "Multiple matches of %s", package_ids[i]);
			for (i = 0; i < pkglist->len; i++) {
				pkg = g_ptr_array_index (pkglist, i);
				g_debug ("possible matches: %s",
					 dnf_package_get_package_id (pkg));
			}
			goto out;
		}

		/* add to results */
		pkg = g_ptr_array_index (pkglist, 0);
		g_hash_table_insert (hash,
				     g_strdup (package_ids[i]),
				     g_object_ref (pkg));
		g_ptr_array_unref (pkglist);
	}
out:
	if (!ret && hash != NULL) {
		g_hash_table_unref (hash);
		hash = NULL;
	}
	if (query != NULL)
		hy_query_free (query);
	return hash;
}

/**
 * backend_get_details_thread:
 */
static void
backend_get_details_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	guint i;
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;

	g_variant_get (params, "(^a&s)", &package_ids);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   49, /* find packages */
				   1, /* emit */
				   -1);

	/* get sack */
	filters = dnf_get_filter_for_ids (package_ids);
	g_assert (ret);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find packages */
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* emit details */
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL)
			continue;
		pk_backend_job_details (job,
					package_ids[i],
					dnf_package_get_summary (pkg),
					dnf_package_get_license (pkg),
					PK_GROUP_ENUM_UNKNOWN,
					dnf_package_get_description (pkg),
					dnf_package_get_url (pkg),
					(gulong) dnf_package_get_size (pkg));
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_details_thread, NULL, NULL);
}

/**
 * backend_get_details_local_thread:
 */
static void
backend_get_details_local_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	guint i;
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	g_autofree gchar **full_paths = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(^a&s)", &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   50, /* create sack */
				   50, /* get details */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_NONE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* ensure packages are not already installed */
	if (!pk_bitfield_contain (job_data->transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL)) {
		for (i = 0; full_paths[i] != NULL; i++) {
			pkg = dnf_sack_add_cmdline_package (sack, full_paths[i]);
			if (pkg == NULL) {
				pk_backend_job_error_code (job,
							   PK_ERROR_ENUM_FILE_NOT_FOUND,
							   "Failed to open %s",
							   full_paths[i]);
				return;
			}
			pk_backend_job_details (job,
						dnf_package_get_package_id (pkg),
						dnf_package_get_summary (pkg),
						dnf_package_get_license (pkg),
						PK_GROUP_ENUM_UNKNOWN,
						dnf_package_get_description (pkg),
						dnf_package_get_url (pkg),
						(gulong) dnf_package_get_size (pkg));
		}
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_get_details_local:
 */
void
pk_backend_get_details_local (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_thread_create (job, backend_get_details_local_thread, NULL, NULL);
}

/**
 * backend_get_files_local_thread:
 */
static void
backend_get_files_local_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	gchar **files_array;
	guint i;
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	g_autofree gchar **full_paths = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get (params, "(^a&s)", &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   50, /* create sack */
				   50, /* get details */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_NONE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* ensure packages are not already installed */
	for (i = 0; full_paths[i] != NULL; i++) {
		pkg = dnf_sack_add_cmdline_package (sack, full_paths[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_FILE_NOT_FOUND,
						   "Failed to open %s",
						   full_paths[i]);
			return;
		}
		/* sort and list according to name */
		files_array = dnf_package_get_files (pkg);
		pk_backend_job_files (job,
				      dnf_package_get_package_id (pkg),
				      files_array);
		g_strfreev (files_array);
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_get_files_local:
 */
void
pk_backend_get_files_local (PkBackend *backend, PkBackendJob *job, gchar **files)
{
	pk_backend_job_thread_create (job, backend_get_files_local_thread, NULL, NULL);
}

/**
 * pk_backend_download_packages_thread:
 */
static void
pk_backend_download_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	const gchar *directory;
	gboolean ret;
	gchar *tmp;
	guint i;
	DnfRepo *src;
	DnfState *state_local;
	DnfState *state_loop;
	DnfPackage *pkg;
	DnfSack *sack;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GPtrArray) files = NULL;

	g_variant_get (params, "(^a&ss)",
		       &package_ids,
		       &directory);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   1, /* ensure repos */
				   3, /* get sack */
				   5, /* find packages */
				   90, /* download packages */
				   1, /* emit */
				   -1);
	g_assert (ret);

	/* set the list of repos */
	ret = pk_backend_ensure_repos (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* get sack */
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find packages */
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* download packages */
	files = g_ptr_array_new_with_free_func (g_free);
	state_local = dnf_state_get_child (job_data->state);
	dnf_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			return;
		}

		dnf_emit_package (job, PK_INFO_ENUM_DOWNLOADING, pkg);

		/* get correct package source */
		src = dnf_repo_loader_get_repo_by_id (dnf_context_get_repo_loader (job_data->context),
		                                      dnf_package_get_reponame (pkg),
		                                      &error);
		if (src == NULL) {
			g_prefix_error (&error, "Not sure where to download %s: ",
					dnf_package_get_name (pkg));
			pk_backend_job_error_code (job, error->code,
						   "%s", error->message);
			return;
		}

		/* download */
		state_loop = dnf_state_get_child (state_local);
		tmp = dnf_repo_download_package (src,
		                                 pkg,
		                                 directory,
		                                 state_loop,
		                                 &error);
		if (tmp == NULL) {
			pk_backend_job_error_code (job, error->code,
						   "%s", error->message);
			return;
		}

		/* add to download list */
		g_ptr_array_add (files, tmp);

		/* done */
		ret = dnf_state_done (state_local, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			return;
		}
	}
	g_ptr_array_add (files, NULL);

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* emit files so that the daemon will copy these */
	pk_backend_job_files (job, NULL, (gchar **) files->pdata);

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend,
			      PkBackendJob *job,
			      gchar **package_ids,
			      const gchar *directory)
{
	pk_backend_job_thread_create (job, pk_backend_download_packages_thread, NULL, NULL);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
}

/**
 * pk_backend_transaction_check_untrusted_repos:
 */
static GPtrArray *
pk_backend_transaction_check_untrusted_repos (PkBackendJob *job, GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	guint i;
	DnfRepo *src;
	DnfPackage *pkg;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	g_autoptr(GPtrArray) install = NULL;

	/* find any packages in untrusted repos */
	install = dnf_goal_get_packages (job_data->goal,
					 DNF_PACKAGE_INFO_INSTALL,
					 DNF_PACKAGE_INFO_REINSTALL,
					 DNF_PACKAGE_INFO_DOWNGRADE,
					 DNF_PACKAGE_INFO_UPDATE,
					 -1);
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i = 0; i < install->len; i++) {
		pkg = g_ptr_array_index (install, i);

		/* this is a standalone file, so by definition is from an
		 * untrusted repo */
		if (g_strcmp0 (dnf_package_get_reponame (pkg),
			       HY_CMDLINE_REPO_NAME) == 0) {
			g_ptr_array_add (array, g_object_ref (pkg));
			continue;
		}

		/* find repo */
		src = dnf_repo_loader_get_repo_by_id (dnf_context_get_repo_loader (job_data->context),
		                                      dnf_package_get_reponame (pkg),
		                                      error);
		if (src == NULL) {
			g_prefix_error (error, "Can't GPG check %s: ",
					dnf_package_get_name (pkg));
			ret = FALSE;
			goto out;
		}

		/* repo has no gpg key */
		if (!dnf_repo_get_gpgcheck (src))
			g_ptr_array_add (array, g_object_ref (pkg));
	}
out:
	if (array != NULL && !ret) {
		g_ptr_array_unref (array);
		array = NULL;
	}
	return array;
}

/**
 * pk_backend_transaction_simulate:
 */
static gboolean
pk_backend_transaction_simulate (PkBackendJob *job,
				 DnfState *state,
				 GError **error)
{
	DnfDb *db;
	GPtrArray *pkglist;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	gboolean ret;
	g_autoptr(GPtrArray) untrusted = NULL;

	/* set state */
	ret = dnf_state_set_steps (state, error,
				   99, /* check for untrusted repos */
				   1, /* emit */
				   -1);
	if (!ret)
		return FALSE;

	/* set the list of repos */
	ret = pk_backend_ensure_repos (job_data, error);
	if (!ret)
		return FALSE;

	/* mark any explicitly-untrusted packages so that the transaction skips
	 * straight to only_trusted=FALSE after simulate */
	untrusted = pk_backend_transaction_check_untrusted_repos (job, error);
	if (untrusted == NULL)
		return FALSE;

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;

	/* emit what we're going to do */
	db = dnf_transaction_get_db (job_data->transaction);
	dnf_emit_package_array (job, PK_INFO_ENUM_UNTRUSTED, untrusted);

	/* remove */
	pkglist = hy_goal_list_erasures (job_data->goal, NULL);
	dnf_db_ensure_origin_pkglist (db, pkglist);
	dnf_emit_package_list (job, PK_INFO_ENUM_REMOVING, pkglist);
	g_ptr_array_unref (pkglist);

	/* install */
	pkglist = hy_goal_list_installs (job_data->goal, NULL);
	dnf_db_ensure_origin_pkglist (db, pkglist);
	dnf_emit_package_list (job, PK_INFO_ENUM_INSTALLING, pkglist);
	g_ptr_array_unref (pkglist);

	/* obsolete */
	pkglist = hy_goal_list_obsoleted (job_data->goal, NULL);
	dnf_emit_package_list (job, PK_INFO_ENUM_OBSOLETING, pkglist);
	g_ptr_array_unref (pkglist);

	/* reinstall */
	pkglist = hy_goal_list_reinstalls (job_data->goal, NULL);
	dnf_db_ensure_origin_pkglist (db, pkglist);
	dnf_emit_package_list (job, PK_INFO_ENUM_REINSTALLING, pkglist);
	g_ptr_array_unref (pkglist);

	/* update */
	pkglist = hy_goal_list_upgrades (job_data->goal, NULL);
	dnf_db_ensure_origin_pkglist (db, pkglist);
	dnf_emit_package_list (job, PK_INFO_ENUM_UPDATING, pkglist);
	g_ptr_array_unref (pkglist);

	/* downgrade */
	pkglist = hy_goal_list_downgrades (job_data->goal, NULL);
	dnf_db_ensure_origin_pkglist (db, pkglist);
	dnf_emit_package_list (job, PK_INFO_ENUM_DOWNGRADING, pkglist);
	g_ptr_array_unref (pkglist);

	/* done */
	return dnf_state_done (state, error);
}

/**
 * pk_backend_transaction_download_commit:
 */
static gboolean
pk_backend_transaction_download_commit (PkBackendJob *job,
					DnfState *state,
					GError **error)
{
	gboolean ret = TRUE;
	DnfState *state_local;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);

	/* nothing to download */
	if (dnf_transaction_get_remote_pkgs (job_data->transaction)->len == 0) {
		pk_backend_transaction_inhibit_start (job_data->backend);
		ret = dnf_transaction_commit (job_data->transaction,
					      job_data->goal,
					      state,
					      error);
		pk_backend_transaction_inhibit_end (job_data->backend);
		return ret;
	}

	/* set state */
	ret = dnf_state_set_steps (state, error,
				   50, /* download */
				   50, /* install/remove */
				   -1);
	if (!ret)
		return FALSE;

	/* download */
	state_local = dnf_state_get_child (state);
	ret = dnf_transaction_download (job_data->transaction,
					state_local,
					error);
	if (!ret)
		return FALSE;

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;

	/* run transaction */
	state_local = dnf_state_get_child (state);
	pk_backend_transaction_inhibit_start (job_data->backend);
	ret = dnf_transaction_commit (job_data->transaction,
				      job_data->goal,
				      state_local,
				      error);
	pk_backend_transaction_inhibit_end (job_data->backend);
	if (!ret)
		return FALSE;

	/* done */
	return dnf_state_done (state, error);
}

/**
 * pk_backend_transaction_run:
 */
static gboolean
pk_backend_transaction_run (PkBackendJob *job,
			    DnfState *state,
			    GError **error)
{
	DnfState *state_local;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	gboolean ret = TRUE;
	/* allow downgrades for all transaction types */
	int flags = DNF_TRANSACTION_FLAG_ALLOW_DOWNGRADE;

	/* set state */
	ret = dnf_state_set_steps (state, error,
				   5, /* depsolve */
				   95, /* everything else */
				   -1);
	if (!ret)
		return FALSE;

	/* depsolve */
	if (pk_bitfield_contain (job_data->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED))
		flags |= DNF_TRANSACTION_FLAG_ONLY_TRUSTED;
	if (pk_bitfield_contain (job_data->transaction_flags,
				PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL))
		flags |= DNF_TRANSACTION_FLAG_ALLOW_REINSTALL;
	/* only download packages and run a transaction test */
	if (pk_bitfield_contain (job_data->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
		flags |= DNF_TRANSACTION_FLAG_TEST;

	dnf_transaction_set_flags (job_data->transaction, flags);

	state_local = dnf_state_get_child (state);
	ret = dnf_transaction_depsolve (job_data->transaction,
					job_data->goal,
					state_local,
					error);
	if (!ret)
		return FALSE;

	/* done */
	if (!dnf_state_done (state, error))
		return FALSE;

	/* just simulate */
	if (pk_bitfield_contain (job_data->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		state_local = dnf_state_get_child (state);
		ret = pk_backend_transaction_simulate (job,
						       state_local,
						       error);
		if (!ret)
			return FALSE;
		return dnf_state_done (state, error);
	}

	/* download and commit transaction */
	state_local = dnf_state_get_child (state);
	ret = pk_backend_transaction_download_commit (job, state_local, error);
	if (!ret)
		return FALSE;

	/* done */
	return dnf_state_done (state, error);
}

/**
 * pk_backend_repo_remove_thread:
 */
static void
pk_backend_repo_remove_thread (PkBackendJob *job,
			       GVariant *params,
			       gpointer user_data)
{
	DnfDb *db;
	DnfRepo *src;
	DnfState *state_local;
	DnfPackage *pkg;
	GPtrArray *pkglist = NULL;
	GPtrArray *pkglist_releases = NULL;
	HyQuery query = NULL;
	HyQuery query_release = NULL;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
	const gchar *from_repo;
	const gchar *repo_filename;
	const gchar *repo_id;
	const gchar *tmp;
	gboolean autoremove;
	gboolean ret;
	gboolean found;
	guint cnt = 0;
	guint i;
	guint j;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) removed_id = NULL;
	g_autoptr(GPtrArray) sources = NULL;
	g_auto(GStrv) search = NULL;

	g_variant_get (params, "(t&sb)",
		       &job_data->transaction_flags,
		       &repo_id,
		       &autoremove);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   1, /* get the .repo filename for @repo_id */
				   1, /* find any other repos in the same file */
				   10, /* remove any packages from repos */
				   3, /* remove repo-releases */
				   85, /* run transaction */
				   -1);
	g_assert (ret);

	/* find the repo-release package name for @repo_id */
	src = dnf_repo_loader_get_repo_by_id (dnf_context_get_repo_loader (job_data->context), repo_id, &error);
	if (src == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "%s", error->message);
		goto out;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* find all the .repo files the repo-release package installed */
	sources = dnf_repo_loader_get_repos (dnf_context_get_repo_loader (job_data->context), &error);
	search = g_new0 (gchar *, sources->len + 0);
	removed_id = g_ptr_array_new_with_free_func (g_free);
	repo_filename = dnf_repo_get_filename (src);
	for (i = 0; i < sources->len; i++) {
		src = g_ptr_array_index (sources, i);
		if (g_strcmp0 (dnf_repo_get_filename (src), repo_filename) != 0)
			continue;

		/* this repo_id will get purged */
		tmp = dnf_repo_get_id (src);
		g_debug ("adding id %s to check", tmp);
		g_ptr_array_add (removed_id, g_strdup (tmp));

		/* the package that installed the .repo file will be removed */
		tmp = dnf_repo_get_filename (src);
		for (j = 0, found = FALSE; search[j] != NULL; j++) {
			if (g_strcmp0 (tmp, search[j]) == 0)
				found = TRUE;
		}
		if (!found) {
			g_debug ("adding filename %s to search", tmp);
			search[cnt++] = g_strdup (tmp);
		}
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* remove all the packages installed from all these repos */
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}
	job_data->goal = hy_goal_create (sack);
	query = hy_query_create (sack);
	pkglist = hy_query_run (query);
	db = dnf_transaction_get_db (job_data->transaction);
	for (i = 0; i < pkglist->len; i++) {
		pkg = g_ptr_array_index (pkglist, i);
		dnf_db_ensure_origin_pkg (db, pkg);
		from_repo = dnf_package_get_origin (pkg);
		if (from_repo == NULL)
			continue;
		for (j = 0; j < removed_id->len; j++) {
			tmp = g_ptr_array_index (removed_id, j);
			if (g_strcmp0 (tmp, from_repo) == 0) {
				g_debug ("%s %s as installed from %s",
					 autoremove ? "removing" : "ignoring",
					 dnf_package_get_name (pkg),
					 from_repo);
				if (autoremove) {
					hy_goal_erase (job_data->goal, pkg);
				}
				break;
			}
		}
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* remove the repo-releases */
	query_release = hy_query_create (sack);
	hy_query_filter_in (query_release, HY_PKG_FILE, HY_EQ, (const gchar **) search);
	pkglist_releases = hy_query_run (query_release);
	for (i = 0; i < pkglist_releases->len; i++) {
		pkg = g_ptr_array_index (pkglist, i);
		dnf_db_ensure_origin_pkg (db, pkg);
		g_debug ("removing %s as installed for repo",
			 dnf_package_get_name (pkg));
		hy_goal_erase (job_data->goal, pkg);
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* run transaction */
	state_local = dnf_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		goto out;
	}
out:
	if (pkglist != NULL)
		g_ptr_array_unref (pkglist);
	if (pkglist_releases != NULL)
		g_ptr_array_unref (pkglist_releases);
	if (query != NULL)
		hy_query_free (query);
	if (query_release != NULL)
		hy_query_free (query_release);
}

/**
 * pk_backend_repo_remove:
 */
void
pk_backend_repo_remove (PkBackend *backend,
			PkBackendJob *job,
			PkBitfield transaction_flags,
			const gchar *repo_id,
			gboolean autoremove)
{
	pk_backend_job_thread_create (job, pk_backend_repo_remove_thread, NULL, NULL);
}

/**
 * dnf_is_installed_package_id_name_arch:
 */
static gboolean
dnf_is_installed_package_id_name_arch (DnfSack *sack, const gchar *package_id)
{
	gboolean ret;
	GPtrArray *pkglist = NULL;
	HyQuery query = NULL;
	g_auto(GStrv) split = NULL;

	/* run query */
	query = hy_query_create (sack);
	split = pk_package_id_split (package_id);
	hy_query_filter (query, HY_PKG_NAME, HY_EQ, split[PK_PACKAGE_ID_NAME]);
	hy_query_filter (query, HY_PKG_ARCH, HY_EQ, split[PK_PACKAGE_ID_ARCH]);
	hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	pkglist = hy_query_run (query);

	/* any matches? */
	ret = pkglist->len > 0;

	g_ptr_array_unref (pkglist);
	hy_query_free (query);
	return ret;
}

/**
 * pk_backend_remove_packages_thread:
 *
 * FIXME: Use autoremove
 * FIXME: Use allow_deps
 */
static void
pk_backend_remove_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean allow_deps;
	gboolean autoremove;
	gboolean ret;
	guint i;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;

	g_variant_get (params, "(t^a&sbb)",
		       &job_data->transaction_flags,
		       &package_ids,
		       &allow_deps,
		       &autoremove);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   3, /* add repos */
				   1, /* check installed */
				   1, /* find packages */
				   95, /* run transaction */
				   -1);
	g_assert (ret);

	/* not supported */
	if (autoremove) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_NOT_SUPPORTED,
					   "autoremove is not supported");
		return;
	}
	if (!allow_deps) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_NOT_SUPPORTED,
					   "!allow_deps is not supported");
		return;
	}

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	//TODO: check if we're trying to remove protected packages like:
	//glibc, kernel, etc

	/* ensure packages are already installed */
	for (i = 0; package_ids[i] != NULL; i++) {
		ret = dnf_is_installed_package_id_name_arch (sack, package_ids[i]);
		if (!ret) {
			g_autofree gchar *printable_tmp = NULL;
			printable_tmp = pk_package_id_to_printable (package_ids[i]);
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
						   "%s is not already installed",
						   printable_tmp);
			return;
		}
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find packages */
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* remove packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			return;
		}
		hy_goal_erase (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = dnf_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	pk_backend_job_thread_create (job, pk_backend_remove_packages_thread, NULL, NULL);
}

/**
 * pk_backend_install_packages_thread:
 */
static void
pk_backend_install_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean ret;
	guint i;
	g_autofree enum _hy_comparison_type_e *relations = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;

	g_variant_get (params, "(t^a&s)",
		       &job_data->transaction_flags,
		       &package_ids);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   3, /* add repos */
				   1, /* check installed */
				   1, /* find packages */
				   95, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	relations = g_new0 (enum _hy_comparison_type_e, g_strv_length (package_ids));
	/**
	 * HY_EQ - the same version of package is installed -> reinstallation
	 * HY_GT - higher version of package is installed   -> update
	 * HY_LT - older version of package is installed    -> downgrade
	 * 0     - package is not installed
	 */
	/* ensure packages are not already installed */
	for (i = 0; package_ids[i] != NULL; i++) {
		HyQuery query = NULL;
		DnfPackage *inst_pkg = NULL;
		gchar **split = NULL;
		DnfPackage *latest = NULL;
		GPtrArray *pkglist = NULL;
		guint pli;

		split = pk_package_id_split (package_ids[i]);
		query = hy_query_create (sack);
		hy_query_filter (query, HY_PKG_NAME, HY_EQ, split[PK_PACKAGE_ID_NAME]);
		hy_query_filter (query, HY_PKG_ARCH, HY_EQ, split[PK_PACKAGE_ID_ARCH]);
		hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
		pkglist = hy_query_run (query);
		hy_query_free (query);

		for (pli = 0; pli < pkglist->len; ++pli) {
			inst_pkg = g_ptr_array_index (pkglist, pli);
			ret = dnf_sack_evr_cmp (sack, split[PK_PACKAGE_ID_VERSION], dnf_package_get_evr (inst_pkg));
			if (relations[i] == 0 && ret > 0) {
				relations[i] = HY_GT;
			} else if (relations[i] != HY_EQ && ret < 0) {
				relations[i] = HY_LT;
				if (!latest || dnf_package_evr_cmp (latest, inst_pkg) < 0)
					latest = inst_pkg;
			} else if (ret == 0) {
				relations[i] = HY_EQ;
				break;
			}
		}

		if (relations[i] == HY_EQ &&
		    !pk_bitfield_contain (job_data->transaction_flags,
					  PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL)) {
			g_autofree gchar *printable_tmp = pk_package_id_to_printable (package_ids[i]);
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
						   "%s is already installed",
						   printable_tmp);
			return;
		}

		if (relations[i] == HY_LT &&
		    !pk_bitfield_contain (job_data->transaction_flags,
					  PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE)) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
						   "higher version \"%s\" of package %s.%s is already installed",
						   dnf_package_get_evr (latest), split[PK_PACKAGE_ID_NAME],
						   split[PK_PACKAGE_ID_ARCH]);
			return;
		}

		if (relations[i] && relations[i] != HY_EQ &&
		    pk_bitfield_contain (job_data->transaction_flags,
					 PK_TRANSACTION_FLAG_ENUM_JUST_REINSTALL)) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_NOT_AUTHORIZED,
						   "missing authorization to update or downgrade software");
			return;
		}
		g_ptr_array_unref (pkglist);
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find remote packages */
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* install packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			return;
		}
		if (relations[i] == HY_LT) {
			hy_goal_downgrade_to (job_data->goal, pkg);
		} else {
			if (relations[i] == HY_EQ) {
				dnf_package_set_action (pkg, DNF_STATE_ACTION_REINSTALL);
			}
			hy_goal_install (job_data->goal, pkg);
		}
	}

	/* run transaction */
	state_local = dnf_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job,
			     PkBitfield transaction_flags,
			     gchar **package_ids)
{
	pk_backend_job_thread_create (job, pk_backend_install_packages_thread, NULL, NULL);
}

/**
 * pk_backend_install_files_thread:
 */
static void
pk_backend_install_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean ret;
	guint i;
	g_autofree gchar **full_paths = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_variant_get (params, "(t^a&s)",
		       &job_data->transaction_flags,
		       &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   25, /* check installed */
				   24, /* run transaction */
				   1, /* emit */
				   -1);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	g_assert (ret);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_NONE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* ensure packages are not already installed */
	array = g_ptr_array_new ();
	for (i = 0; full_paths[i] != NULL; i++) {
		pkg = dnf_sack_add_cmdline_package (sack, full_paths[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_FILE_NOT_FOUND,
						   "Failed to open %s",
						   full_paths[i]);
			return;
		}

		/* we don't download this, we just use it */
		dnf_package_set_filename (pkg, full_paths[i]);
		g_ptr_array_add (array, pkg);
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* install packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		hy_goal_install (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = dnf_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job,
			  PkBitfield transaction_flags,
			  gchar **full_paths)
{
	pk_backend_job_thread_create (job, pk_backend_install_files_thread, NULL, NULL);
}

/**
 * pk_backend_update_packages_thread:
 */
static void
pk_backend_update_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean ret;
	guint i;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;

	g_variant_get (params, "(t^a&s)",
		       &job_data->transaction_flags,
		       &package_ids);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   9, /* add repos */
				   1, /* find packages */
				   90, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* set up the sack for packages that should only ever be installed, never updated */
	dnf_sack_set_installonly (sack, dnf_context_get_installonly_pkgs (job_data->context));
	dnf_sack_set_installonly_limit (sack, dnf_context_get_installonly_limit (job_data->context));

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find packages */
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* install packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			return;
		}

		/* allow some packages to have multiple versions installed */
		if (dnf_package_is_installonly (pkg))
			hy_goal_install (job_data->goal, pkg);
		else
			hy_goal_upgrade_to (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = dnf_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job,
			    PkBitfield transaction_flags, gchar **package_ids)
{
	pk_backend_job_thread_create (job, pk_backend_update_packages_thread, NULL, NULL);
}

/**
 * pk_backend_upgrade_system_thread:
 */
static void
pk_backend_upgrade_system_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	DnfState *state_local;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBackendDnfPrivate *priv = pk_backend_get_user_data (job_data->backend);
	PkBitfield filters;
	gboolean ret;
	const gchar *release_ver = NULL;
	g_autoptr(GError) error = NULL;

	/* get arguments */
	g_variant_get (params, "(t&su)",
	               &job_data->transaction_flags,
	               &release_ver,
	               NULL);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* create a new context for the passed in release ver */
	if (release_ver != NULL) {
		g_autoptr(DnfContext) context = NULL;

		context = dnf_context_new ();
		ret = pk_backend_setup_dnf_context (context, priv->conf, release_ver, &error);
		if (!ret) {
			g_debug ("failed to setup context: %s", error->message);
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			return;
		}
		pk_backend_job_set_context (job, context);
	}

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   10, /* add repos */
				   90, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* set up the sack for packages that should only ever be installed, never updated */
	dnf_sack_set_installonly (sack, dnf_context_get_installonly_pkgs (job_data->context));
	/* set the installonly limit one higher than usual to avoid removing any kernels during system upgrades */
	dnf_sack_set_installonly_limit (sack, dnf_context_get_installonly_limit (job_data->context) + 1);

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* set up the distupgrade goal */
	job_data->goal = hy_goal_create (sack);
	hy_goal_distupgrade_all (job_data->goal);

	/* run transaction */
	state_local = dnf_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend,
                           PkBackendJob *job,
                           PkBitfield transaction_flags,
                           const gchar *distro_id,
                           PkUpgradeKindEnum upgrade_kind)
{
	pk_backend_job_thread_create (job, pk_backend_upgrade_system_thread, NULL, NULL);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
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
}

/**
 * pk_backend_sort_string_cb:
 **/
static gint
pk_backend_sort_string_cb (const gchar **a, const gchar **b)
{
	return g_strcmp0 (*a, *b);
}

/**
 * pk_backend_get_files_thread:
 */
static void
pk_backend_get_files_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	gchar **files_array;
	guint i;
	guint j;
	DnfState *state_local;
	DnfPackage *pkg;
	DnfSack *sack;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   90, /* add repos */
				   5, /* find packages */
				   5, /* emit files */
				   -1);
	g_assert (ret);

	/* get sack */
	g_variant_get (params, "(^a&s)", &package_ids);
	filters = dnf_get_filter_for_ids (package_ids);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find packages */
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* emit details */
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			return;
		}

		/* sort and list according to name */
		files_array = dnf_package_get_files (pkg);
		if (FALSE) {
			g_autoptr(GPtrArray) files = NULL;
			files = g_ptr_array_new ();
			for (j = 0; files_array[j] != NULL; j++)
				g_ptr_array_add (files, files_array[j]);
			g_ptr_array_sort (files,
					  (GCompareFunc) pk_backend_sort_string_cb);
			g_ptr_array_add (files, NULL);
			pk_backend_job_files (job,
					      package_ids[i],
					      (gchar **) files->pdata);
		} else {
			pk_backend_job_files (job,
					      package_ids[i],
					      files_array);
		}
		g_strfreev (files_array);
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend,
		      PkBackendJob *job,
		      gchar **package_ids)
{
	pk_backend_job_thread_create (job, pk_backend_get_files_thread, NULL, NULL);
}

/**
 * pk_backend_get_update_detail_thread:
 */
static void
pk_backend_get_update_detail_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	guint i;
	guint j;
	DnfState *state_local;
	DnfPackage *pkg;
	DnfAdvisory *advisory;
	GPtrArray *references;
	DnfSack *sack = NULL;
	PkBackendDnfJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) hash = NULL;

	/* set state */
	ret = dnf_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   49, /* find packages */
				   1, /* emit update details */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = dnf_state_get_child (job_data->state);
	sack = dnf_utils_create_sack_for_filters (job,
						  filters,
						  DNF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* find remote packages */
	g_variant_get (params, "(^a&s)", &package_ids);
	hash = dnf_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}

	/* emit details for each */
	for (i = 0; package_ids[i] != NULL; i++) {
		g_autoptr(GPtrArray) vendor_urls = NULL;
		g_autoptr(GPtrArray) bugzilla_urls = NULL;
		g_autoptr(GPtrArray) cve_urls = NULL;

		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL)
			continue;
		advisory = dnf_package_get_advisory (pkg);
		if (advisory == NULL)
			continue;

		references = dnf_advisory_get_references (advisory);
		vendor_urls = g_ptr_array_new_with_free_func (g_free);
		bugzilla_urls = g_ptr_array_new_with_free_func (g_free);
		cve_urls = g_ptr_array_new_with_free_func (g_free);
		for (j = 0; j < references->len; j++) {
			DnfAdvisoryRef *reference;
			DnfAdvisoryRefKind kind;
			const gchar *url;

			reference = g_ptr_array_index (references, j);
			kind = dnf_advisoryref_get_kind (reference);
			url = dnf_advisoryref_get_url (reference);
			if (url == NULL)
				continue;
			switch (kind) {
			case DNF_REFERENCE_KIND_VENDOR:
				g_ptr_array_add (vendor_urls, g_strdup (url));
				break;
			case DNF_REFERENCE_KIND_BUGZILLA:
				g_ptr_array_add (bugzilla_urls, g_strdup (url));
				break;
			case DNF_REFERENCE_KIND_CVE:
				g_ptr_array_add (cve_urls, g_strdup (url));
				break;
			default:
				break;
			}

		}
		g_ptr_array_add (vendor_urls, NULL);
		g_ptr_array_add (bugzilla_urls, NULL);
		g_ptr_array_add (cve_urls, NULL);

		pk_backend_job_update_detail (job,
					      package_ids[i],
					      NULL,
					      NULL,
					      (gchar **) vendor_urls->pdata,
					      (gchar **) bugzilla_urls->pdata,
					      (gchar **) cve_urls->pdata,
					      PK_RESTART_ENUM_NONE, /* FIXME */
					      dnf_advisory_get_description (advisory),
					      NULL,
					      PK_UPDATE_STATE_ENUM_STABLE, /* FIXME */
					      NULL, /* issued */
					      NULL /* updated */);

		g_ptr_array_unref (references);
		g_object_unref (advisory);
	}

	/* done */
	if (!dnf_state_done (job_data->state, &error)) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		return;
	}
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend,
			      PkBackendJob *job,
			      gchar **package_ids)
{
	pk_backend_job_thread_create (job, pk_backend_get_update_detail_thread, NULL, NULL);
}

/**
 * pk_backend_repair_remove_rpm_index:
 */
static gboolean
pk_backend_repair_remove_rpm_index (const gchar *index_fn, GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GFile) file = NULL;

	path = g_build_filename ("/var/lib/rpm", index_fn, NULL);
	g_debug ("deleting %s", path);
	file = g_file_new_for_path (path);
	return g_file_delete (file, NULL, error);
}

/**
 * pk_backend_repair_system_thread:
 */
static void
pk_backend_repair_system_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	PkBitfield transaction_flags;
	const gchar *tmp;
	gboolean ret;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error = NULL;

	/* don't do anything when simulating */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	transaction_flags = pk_backend_job_get_transaction_flags (job);
	if (pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE))
		return;

	/* open the directory */
	dir = g_dir_open ("/var/lib/rpm", 0, &error);
	if (dir == NULL) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_INSTALL_ROOT_INVALID,
					   "%s", error->message);
		return;
	}

	/* remove the indexes */
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		if (!g_str_has_prefix (tmp, "__db."))
			continue;
		pk_backend_job_set_status (job, PK_STATUS_ENUM_CLEANUP);
		ret = pk_backend_repair_remove_rpm_index (tmp, &error);
		if (!ret) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_FILE_CONFLICTS,
						   "Failed to delete %s: %s",
						   tmp, error->message);
			return;
		}
	}
}

/**
 * pk_backend_repair_system:
 */
void
pk_backend_repair_system (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield transaction_flags)
{
	pk_backend_job_thread_create (job, pk_backend_repair_system_thread, NULL, NULL);
}
