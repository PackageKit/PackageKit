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
#include <libhif-private.h>

#include <pk-backend.h>
#include <packagekit-glib2/pk-debug.h>

#include <hawkey/query.h>
#include <hawkey/stringarray.h>
#include <hawkey/version.h>
#include <hawkey/util.h>
#include <librepo/librepo.h>

#include "hif-backend.h"

typedef struct {
	HySack		 sack;
	gboolean	 valid;
	gchar		*key;
} HifSackCacheItem;

typedef struct {
	HifContext	*context;
	GHashTable	*sack_cache;	/* of HifSackCacheItem */
	GMutex		 sack_mutex;
	HifRepos	*repos;
	GTimer		*repos_timer;
} PkBackendHifPrivate;

typedef struct {
	GPtrArray	*sources;
	GCancellable	*cancellable;
	HifState	*state;
	PkBitfield	 transaction_flags;
	HyGoal		 goal;
} PkBackendHifJobData;

static PkBackendHifPrivate *priv;

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Hif");
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return g_strdup ("Richard Hughes <richard@hughsie.com>");
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
pk_backend_sack_cache_invalidate (const gchar *why)
{
	GList *values;
	GList *l;
	HifSackCacheItem *cache_item;

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
pk_backend_yum_repos_changed_cb (HifRepos *self, PkBackend *backend)
{
	pk_backend_sack_cache_invalidate ("yum.repos.d changed");
	pk_backend_repo_list_changed (backend);
}

/**
 * hif_sack_cache_item_free:
 */
static void
hif_sack_cache_item_free (HifSackCacheItem *cache_item)
{
	hy_sack_free (cache_item->sack);
	g_free (cache_item->key);
	g_slice_free (HifSackCacheItem, cache_item);
}

/**
 * pk_backend_context_invaliate_cb:
 */
static void
pk_backend_context_invaliate_cb (HifContext *context,
				 const gchar *message,
				 PkBackend *backend)
{
	pk_backend_sack_cache_invalidate (message);
}

/**
 * pk_backend_initialize:
 */
void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	GError *error = NULL;
	gboolean ret;

	/* use logging */
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	pk_debug_add_log_domain ("Hif");

	/* create private area */
	priv = g_new0 (PkBackendHifPrivate, 1);

	g_debug ("Using Hif %i.%i.%i",
		 HIF_MAJOR_VERSION,
		 HIF_MINOR_VERSION,
		 HIF_MICRO_VERSION);
	g_debug ("Using Hawkey %i.%i.%i",
		 HY_VERSION_MAJOR,
		 HY_VERSION_MINOR,
		 HY_VERSION_PATCH);
	g_debug ("Using librepo %i.%i.%i",
		 LR_VERSION_MAJOR,
		 LR_VERSION_MINOR,
		 LR_VERSION_PATCH);

	/* a cache of HySacks with the key being which sacks are loaded
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
						  (GDestroyNotify) hif_sack_cache_item_free);

	/* set defaults */
	priv->context = hif_context_new ();
	g_signal_connect (priv->context, "invalidate",
			  G_CALLBACK (pk_backend_context_invaliate_cb), backend);
	hif_context_set_cache_dir (priv->context, "/var/cache/PackageKit/metadata");
	hif_context_set_solv_dir (priv->context, "/var/cache/PackageKit/hawkey/");
	hif_context_set_repo_dir (priv->context, "/etc/yum.repos.d");
	hif_context_set_rpm_verbosity (priv->context, "info");
	ret = hif_context_setup (priv->context, NULL, &error);
	if (!ret) {
		g_error ("failed to setup context: %s",
			 error->message);
		g_error_free (error);
	}

	/* used a cached list of sources */
	priv->repos = hif_repos_new (priv->context);
	priv->repos_timer = g_timer_new ();
	g_signal_connect (priv->repos, "changed",
			  G_CALLBACK (pk_backend_yum_repos_changed_cb), backend);

	lr_global_init ();
}

/**
 * pk_backend_destroy:
 */
void
pk_backend_destroy (PkBackend *backend)
{
	if (priv->context != NULL)
		g_object_unref (priv->context);
	g_timer_destroy (priv->repos_timer);
	g_object_unref (priv->repos);
	g_mutex_clear (&priv->sack_mutex);
	g_hash_table_unref (priv->sack_cache);
	g_free (priv);
}

/**
 * pk_backend_state_percentage_changed_cb:
 */
static void
pk_backend_state_percentage_changed_cb (HifState *state,
					guint percentage,
					PkBackendJob *job)
{
	pk_backend_job_set_percentage (job, percentage);
}

/**
 * pk_backend_state_action_changed_cb:
 **/
static void
pk_backend_state_action_changed_cb (HifState *state,
				    HifStateAction action,
				    const gchar *action_hint,
				    PkBackendJob *job)
{
	if (action != HIF_STATE_ACTION_UNKNOWN) {
		g_debug ("got state %s with hint %s",
			 pk_status_enum_to_string (action),
			 action_hint);
		pk_backend_job_set_status (job, action);
	}

	switch (action) {
	case HIF_STATE_ACTION_DOWNLOAD_PACKAGES:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_DOWNLOADING,
						action_hint,
						"");
		}
		break;
	case HIF_STATE_ACTION_INSTALL:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_INSTALLING,
						action_hint,
						"");
		}
		break;
	case HIF_STATE_ACTION_REMOVE:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_REMOVING,
						action_hint,
						"");
		}
		break;
	case HIF_STATE_ACTION_UPDATE:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_UPDATING,
						action_hint,
						"");
		}
		break;
	case HIF_STATE_ACTION_CLEANUP:
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
pk_backend_speed_changed_cb (HifState *state,
			     GParamSpec *pspec,
			     PkBackendJob *job)
{
	pk_backend_job_set_speed (job,
				  hif_state_get_speed (state));
}

/**
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendHifJobData *job_data;
	job_data = g_new0 (PkBackendHifJobData, 1);
	pk_backend_job_set_user_data (job, job_data);
	job_data->cancellable = g_cancellable_new ();

	/* HifState */
	job_data->state = hif_state_new ();
	hif_state_set_cancellable (job_data->state, job_data->cancellable);
	g_signal_connect (job_data->state, "percentage-changed",
			  G_CALLBACK (pk_backend_state_percentage_changed_cb),
			  job);
	g_signal_connect (job_data->state, "action-changed",
			  G_CALLBACK (pk_backend_state_action_changed_cb),
			  job);
	g_signal_connect (job_data->state, "notify::speed",
			  G_CALLBACK (pk_backend_speed_changed_cb),
			  job);

#ifdef PK_BUILD_LOCAL
	/* we don't want to enable this for normal runtime */
	hif_state_set_enable_profile (job_data->state, TRUE);
#endif

	/* no locks to get, so jump straight to 'running' */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_RUNNING);
}

#if 0
/**
 * pk_backend_reset_job:
 */
void
pk_backend_reset_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	hif_state_reset (job_data->state);
	g_cancellable_reset (job_data->cancellable);
}
#endif

/**
 * pk_backend_stop_job:
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	g_object_unref (job_data->cancellable);
	if (job_data->state != NULL) {
		hif_state_release_locks (job_data->state);
		g_object_unref (job_data->state);
	}
	if (job_data->sources != NULL)
		g_ptr_array_unref (job_data->sources);
	if (job_data->goal != NULL)
		hy_goal_free (job_data->goal);
	g_free (job_data);
	pk_backend_job_set_user_data (job, NULL);
}

/**
 * pk_backend_ensure_sources:
 */
static gboolean
pk_backend_ensure_sources (PkBackendHifJobData *job_data, GError **error)
{
	gboolean ret = TRUE;

	/* already set */
	if (job_data->sources != NULL)
		goto out;

	/* set the list of repos */
	job_data->sources = hif_repos_get_sources (priv->repos, error);
	if (job_data->sources == NULL) {
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * hif_utils_add_remote:
 */
static gboolean
hif_utils_add_remote (PkBackendJob *job,
		      HySack sack,
		      HifSackAddFlags flags,
		      HifState *state,
		      GError **error)
{
	gboolean ret = TRUE;
	HifState *state_local;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	/* set state */
	ret = hif_state_set_steps (state, error,
				   2, /* load files */
				   98, /* add sources */
				   -1);
	if (!ret)
		goto out;

	/* set the list of repos */
	ret = pk_backend_ensure_sources (job_data, error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* add each repo */
	state_local = hif_state_get_child (state);
	ret = hif_sack_add_sources (sack,
				    job_data->sources,
				    pk_backend_job_get_cache_age (job),
				    flags,
				    state_local,
				    error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

typedef enum {
	HIF_CREATE_SACK_FLAG_NONE,
	HIF_CREATE_SACK_FLAG_USE_CACHE,
	HIF_CREATE_SACK_FLAG_LAST
} HifCreateSackFlags;

/**
 * hif_utils_create_cache_key:
 */
static gchar *
hif_utils_create_cache_key (HifSackAddFlags flags)
{
	GString *key;
	key = g_string_new ("HySack::");
	if (flags == HIF_SACK_ADD_FLAG_NONE) {
		g_string_append (key, "none");
	} else {
		if (flags & HIF_SACK_ADD_FLAG_FILELISTS)
			g_string_append (key, "filelists|");
		if (flags & HIF_SACK_ADD_FLAG_UPDATEINFO)
			g_string_append (key, "updateinfo|");
		if (flags & HIF_SACK_ADD_FLAG_REMOTE)
			g_string_append (key, "remote|");
		g_string_truncate (key, key->len - 1);
	}
	return g_string_free (key, FALSE);
}

/**
 * hif_utils_create_sack_for_filters:
 */
static HySack
hif_utils_create_sack_for_filters (PkBackendJob *job,
				   PkBitfield filters,
				   HifCreateSackFlags create_flags,
				   HifState *state,
				   GError **error)
{
	const gchar *cachedir = "/var/cache/PackageKit/hif";
	gboolean ret;
	gchar *cache_key = NULL;
	gint rc;
	HifSackAddFlags flags = HIF_SACK_ADD_FLAG_FILELISTS;
	HifSackCacheItem *cache_item = NULL;
	HifState *state_local;
	HySack sack = NULL;

	/* don't add if we're going to filter out anyway */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		flags |= HIF_SACK_ADD_FLAG_REMOTE;

	/* only load updateinfo when required */
	if (pk_backend_job_get_role (job) == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		flags |= HIF_SACK_ADD_FLAG_UPDATEINFO;

	/* media repos could disappear at any time */
	if ((create_flags & HIF_CREATE_SACK_FLAG_USE_CACHE) > 0 &&
	    hif_repos_has_removable (priv->repos) &&
	    g_timer_elapsed (priv->repos_timer, NULL) > 1.0f) {
		g_debug ("not reusing sack as media may have disappeared");
		create_flags &= ~HIF_CREATE_SACK_FLAG_USE_CACHE;
	}
	g_timer_reset (priv->repos_timer);

	/* if we've specified a specific cache-age then do not use the cache */
	if ((flags & HIF_SACK_ADD_FLAG_REMOTE) > 0 &&
	    pk_backend_job_get_cache_age (job) != G_MAXUINT) {
		g_debug ("not reusing sack specific cache age requested");
		create_flags &= ~HIF_CREATE_SACK_FLAG_USE_CACHE;
	}

	/* do we have anything in the cache */
	cache_key = hif_utils_create_cache_key (flags);
	if ((create_flags & HIF_CREATE_SACK_FLAG_USE_CACHE) > 0)
		cache_item = g_hash_table_lookup (priv->sack_cache, cache_key);
	if (cache_item != NULL && cache_item->sack != NULL) {
		if (cache_item->valid) {
			ret = TRUE;
			g_debug ("using cached sack %s", cache_key);
			sack = cache_item->sack;
			goto out;
		} else {
			/* we have to do this now rather than rely on the
			 * callback of the hash table */
			g_hash_table_remove (priv->sack_cache, cache_key);
		}
	}

	/* update status */
	hif_state_action_start (state, HIF_STATE_ACTION_QUERY, NULL);

	/* set state */
	if ((flags & HIF_SACK_ADD_FLAG_REMOTE) > 0) {
		ret = hif_state_set_steps (state, error,
					   8, /* add installed */
					   92, /* add remote */
					   -1);
		if (!ret)
			goto out;
	} else {
		hif_state_set_number_steps (state, 1);
	}

	/* create empty sack */
	sack = hy_sack_create (cachedir, NULL, NULL, HY_MAKE_CACHE_DIR);
	if (sack == NULL) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to create sack cache");
		goto out;
	}

	/* add installed packages */
	rc = hy_sack_load_system_repo (sack, NULL, HY_BUILD_CACHE);
	ret = hif_rc_to_gerror (rc, error);
	if (!ret) {
		g_prefix_error (error, "Failed to load system repo: ");
		goto out;
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* add remote packages */
	if ((flags & HIF_SACK_ADD_FLAG_REMOTE) > 0) {
		state_local = hif_state_get_child (state);
		ret = hif_utils_add_remote (job, sack, flags,
					    state_local, error);
		if (!ret)
			goto out;

		/* done */
		ret = hif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* creates repo for command line rpms */
	hy_sack_create_cmdline_repo (sack);

	/* save in cache */
	g_mutex_lock (&priv->sack_mutex);
	cache_item = g_slice_new (HifSackCacheItem);
	cache_item->key = g_strdup (cache_key);
	cache_item->sack = sack;
	cache_item->valid = TRUE;
	g_debug ("created cached sack %s", cache_item->key);
	g_hash_table_insert (priv->sack_cache, g_strdup (cache_key), cache_item);
	g_mutex_unlock (&priv->sack_mutex);
out:
	g_free (cache_key);
	if (!ret && sack != NULL) {
		hy_sack_free (sack);
		sack = NULL;
	}
	return sack;
}

/**
 * hif_utils_add_query_filters:
 */
static void
hif_utils_add_query_filters (HyQuery query, PkBitfield filters)
{
	const gchar *application_glob = "/usr/share/applications/*.desktop";

	/* newest */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		hy_query_filter_latest_per_arch (query, TRUE);

	/* arch */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH)) {
		hy_query_filter_in (query, HY_PKG_ARCH, HY_EQ,
				    hif_context_get_native_arches (priv->context));
	} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH)) {
		hy_query_filter_in (query, HY_PKG_ARCH, HY_NEQ,
				    hif_context_get_native_arches (priv->context));
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
}

/**
 * pk_backend_what_provides_decompose:
 */
static gchar **
pk_backend_what_provides_decompose (gchar **values, GError **error)
{
	guint i;
	guint len;
	gchar **search = NULL;
	GPtrArray *array = NULL;

	/* iter on each provide string, and wrap it with the fedora prefix */
	len = g_strv_length (values);
	array = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < len; i++) {
		g_ptr_array_add (array, g_strdup (values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer1(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma5(%s)", values[i]));
	}
	search = pk_ptr_array_to_strv (array);
	for (i = 0; search[i] != NULL; i++)
		g_debug ("Querying provide '%s'", search[i]);
	return search;
}

/**
 * pk_backend_search_thread:
 */
static void
pk_backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	gchar **search = NULL;
	gchar **search_tmp;
	GError *error = NULL;
	HifDb *db;
	HifState *state_local;
	HifTransaction *transaction;
	HyPackageList pkglist = NULL;
	HyQuery query = NULL;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = 0;

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
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
			g_error_free (error);
			goto out;
		}
		break;
	default:
		g_variant_get (params, "(t^as)", &filters, &search);
		break;
	}

	/* set the list of repos */
	ret = pk_backend_ensure_sources (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get sack */
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* run query */
	query = hy_query_create (sack);
	hif_utils_add_query_filters (query, filters);
	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_GET_PACKAGES:
		pkglist = hy_query_run (query);
		break;
	case PK_ROLE_ENUM_RESOLVE:
		hy_query_filter_in (query, HY_PKG_NAME, HY_EQ, (const gchar **) search);
		pkglist = hy_query_run (query);
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		hy_query_filter_in (query, HY_PKG_FILE, HY_EQ, (const gchar **) search);
		pkglist = hy_query_run (query);
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		hy_query_filter_in (query, HY_PKG_DESCRIPTION, HY_SUBSTR, (const gchar **) search);
		pkglist = hy_query_run (query);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		hy_query_filter_in (query, HY_PKG_NAME, HY_SUBSTR, (const gchar **) search);
		pkglist = hy_query_run (query);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		hy_query_filter_provides_in (query, search);
		pkglist = hy_query_run (query);
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		job_data->goal = hy_goal_create (sack);
		hy_goal_upgrade_all (job_data->goal);
		ret = hif_goal_depsolve (job_data->goal, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			g_error_free (error);
			goto out;
		}
		pkglist = hy_goal_list_upgrades (job_data->goal);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the src on each package */
	transaction = hif_context_get_transaction (priv->context);
	ret = hif_transaction_ensure_source_list (transaction, pkglist, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the origin on each package */
	db = hif_transaction_get_db (hif_context_get_transaction (priv->context));
	hif_db_ensure_origin_pkglist (db, pkglist);

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* FIXME: actually get the right update severity */
	if (pk_backend_job_get_role (job) == PK_ROLE_ENUM_GET_UPDATES) {
		guint i;
		HyPackage pkg;
		HyUpdateSeverity severity;
		PkInfoEnum info_enum;
		FOR_PACKAGELIST(pkg, pkglist, i) {
			severity = hy_package_get_update_severity (pkg);
			info_enum = hif_update_severity_to_info_enum (severity);
			hif_package_set_info (pkg, info_enum);
		}
	}

	hif_emit_package_list_filter (job, filters, pkglist);

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (search);
	if (pkglist != NULL)
		hy_packagelist_free (pkglist);
	if (query != NULL)
		hy_query_free (query);
	pk_backend_job_finished (job);
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

/**
 * pk_backend_source_filter:
 */
static gboolean
pk_backend_source_filter (HifSource *src, PkBitfield filters)
{
	/* devel and ~devel */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) &&
	    !hif_source_is_devel (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) &&
	    hif_source_is_devel (src))
		return FALSE;

	/* source and ~source */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SOURCE) &&
	    !hif_source_is_source (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SOURCE) &&
	    hif_source_is_source (src))
		return FALSE;

	/* installed and ~installed == enabled */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
	    !hif_source_get_enabled (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) &&
	    hif_source_get_enabled (src))
		return FALSE;

	/* supported and ~supported == core */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_SUPPORTED) &&
	    !hif_source_is_supported (src))
		return FALSE;
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_SUPPORTED) &&
	    hif_source_is_supported (src))
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

	gchar *description;
	GError *error = NULL;
	GPtrArray *sources;
	guint i;
	HifSource *src;
	PkBitfield filters;

	g_variant_get (params, "(t)", &filters);

	/* set the list of repos */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	sources = hif_repos_get_sources (priv->repos, &error);
	if (sources == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to scan yum.repos.d: %s",
					   error->message);
		g_error_free (error);
		goto out;
	}

	/* none? */
	if (sources->len == 0) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_REPO_NOT_FOUND,
					   "failed to find any repos");
		goto out;
	}

	/* emit each repo */
	for (i = 0; i < sources->len; i++) {
		src = g_ptr_array_index (sources, i);
		if (!pk_backend_source_filter (src, filters))
			continue;
		description = hif_source_get_description (src);
		pk_backend_job_repo_detail (job,
					    hif_source_get_id (src),
					    description,
					    hif_source_get_enabled (src));
		g_free (description);
	}
out:
	if (sources != NULL)
		g_ptr_array_unref (sources);
	pk_backend_job_finished (job);
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
	GError *error = NULL;
	HifSource *src;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	g_variant_get (params, "(&s&s&s)", &repo_id, &parameter, &value);

	/* take lock */
	ret = hif_state_take_lock (job_data->state,
				   HIF_LOCK_TYPE_REPO,
				   HIF_LOCK_MODE_PROCESS,
				   &error);
	if (!ret) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to get lock: %s",
					   error->message);
		g_error_free (error);
		goto out;
	}

	/* set the list of repos */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* find the correct repo */
	src = hif_repos_get_source_by_id (priv->repos, repo_id, &error);
	if (src == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "%s", error->message);
		g_error_free (error);
		goto out;
	}
	ret = hif_source_set_data (src, parameter, value, &error);
	if (!ret) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to write repo file: %s",
					   error->message);
		g_error_free (error);
		goto out;
	}

	/* nothing found */
	pk_backend_job_set_percentage (job, 100);
out:
	hif_state_release_locks (job_data->state);
	pk_backend_job_finished (job);
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
			   HifSource *src,
			   HifState *state,
			   GError **error)
{
	gboolean ret;
	gboolean src_okay;
	HifState *state_local;
	GError *error_local = NULL;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   50, /* check */
				   50, /* download */
				   -1);
	if (!ret)
		goto out;

	/* is the source up to date? */
	state_local = hif_state_get_child (state);
	src_okay = hif_source_check (src,
				     pk_backend_job_get_cache_age (job),
				     state_local,
				     &error_local);
	if (!src_okay) {
		g_debug ("repo %s not okay [%s], refreshing",
			 hif_source_get_id (src), error_local->message);
		g_clear_error (&error_local);
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* update repo, TODO: if we have network access */
	if (!src_okay) {
		state_local = hif_state_get_child (state);
		ret = hif_source_update (src,
					 HIF_SOURCE_UPDATE_FLAG_NONE,
					 state_local,
					 &error_local);
		if (!ret) {
			if (g_error_matches (error_local,
					     HIF_ERROR,
					     PK_ERROR_ENUM_CANNOT_FETCH_SOURCES)) {
				g_warning ("Skipping refresh of %s: %s",
					   hif_source_get_id (src),
					   error_local->message);
				g_clear_error (&error_local);
			} else {
				g_propagate_error (error, error_local);
				goto out;
			}
		}
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_backend_refresh_cache_thread:
 */
static void
pk_backend_refresh_cache_thread (PkBackendJob *job,
				 GVariant *params,
				 gpointer user_data)
{
	GError *error = NULL;
	HifSource *src;
	HifState *state_local;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	gboolean force;
	gboolean ret;
	guint cnt = 0;
	guint i;

	g_variant_get (params, "(b)", &force);

	/* set the list of repos */
	ret = pk_backend_ensure_sources (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* count the enabled sources */
	for (i = 0; i < job_data->sources->len; i++) {
		src = g_ptr_array_index (job_data->sources, i);
		if (!hif_source_get_enabled (src))
			continue;
		if (hif_source_get_kind (src) == HIF_SOURCE_KIND_MEDIA)
			continue;
		cnt++;
	}

	/* refresh each repo */
	hif_state_set_number_steps (job_data->state, cnt);
	for (i = 0; i < job_data->sources->len; i++) {
		src = g_ptr_array_index (job_data->sources, i);
		if (!hif_source_get_enabled (src))
			continue;
		if (hif_source_get_kind (src) == HIF_SOURCE_KIND_MEDIA)
			continue;

		/* delete content even if up to date */
		if (force) {
			g_debug ("Deleting contents of %s as forced", hif_source_get_id (src));
			ret = hif_source_clean (src, &error);
			if (!ret) {
				pk_backend_job_error_code (job, error->code, "%s", error->message);
				g_error_free (error);
				goto out;
			}
		}

		/* check and download */
		state_local = hif_state_get_child (job_data->state);
		ret = pk_backend_refresh_source (job, src, state_local, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			g_error_free (error);
			goto out;
		}

		/* done */
		ret = hif_state_done (job_data->state, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			g_error_free (error);
			goto out;
		}
	}
out:
	pk_backend_job_finished (job);
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
 * hif_utils_find_package_ids:
 *
 * Returns a hash table of all the packages found in the sack.
 * If a specific package-id is not found then the method does not fail, but
 * no package will be inserted into the hash table.
 *
 * If multiple packages are found, an error is returned, as the package-id is
 * supposed to uniquely identify the package across all repos.
 */
static GHashTable *
hif_utils_find_package_ids (HySack sack, gchar **package_ids, GError **error)
{
	const gchar *reponame;
	gboolean ret = TRUE;
	gchar **split;
	GHashTable *hash;
	guint i;
	HyPackageList pkglist = NULL;
	HyPackage pkg;
	HyQuery query = NULL;

	/* run query */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) hy_package_free);
	query = hy_query_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
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
		g_strfreev (split);

		/* no matches */
		if (hy_packagelist_count (pkglist) == 0) {
			hy_packagelist_free (pkglist);
			continue;
		}

		/* multiple matches */
		if (hy_packagelist_count (pkglist) > 1) {
			ret = FALSE;
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_PACKAGE_CONFLICTS,
				     "Multiple matches of %s", package_ids[i]);
			FOR_PACKAGELIST(pkg, pkglist, i) {
				g_debug ("possible matches: %s",
					 hif_package_get_id (pkg));
			}
			goto out;
		}

		/* add to results */
		pkg = hy_packagelist_get (pkglist, 0);
		g_hash_table_insert (hash,
				     g_strdup (package_ids[i]),
				     hy_package_link (pkg));
		hy_packagelist_free (pkglist);
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
	gchar **package_ids;
	GError *error = NULL;
	GHashTable *hash = NULL;
	guint i;
	HifState *state_local;
	HyPackage pkg;
	HySack sack;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	g_variant_get (params, "(^a&s)", &package_ids);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   49, /* find packages */
				   1, /* emit */
				   -1);

	/* get sack */
	filters = hif_get_filter_for_ids (package_ids);
	g_assert (ret);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find packages */
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* emit details */
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL)
			continue;
		pk_backend_job_details (job,
					package_ids[i],
					hy_package_get_summary (pkg),
					hy_package_get_license (pkg),
					PK_GROUP_ENUM_UNKNOWN,
					hif_package_get_description (pkg),
					hy_package_get_url (pkg),
					(gulong) hy_package_get_size (pkg));
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	gchar **full_paths;
	GError *error = NULL;
	guint i;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	g_variant_get (params, "(^a&s)", &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* create sack */
				   50, /* get details */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_NONE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ensure packages are not already installed */
	for (i = 0; full_paths[i] != NULL; i++) {
		pkg = hy_sack_add_cmdline_package (sack, full_paths[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_FILE_NOT_FOUND,
						   "Failed to open %s",
						   full_paths[i]);
			goto out;
		}
		pk_backend_job_details (job,
					hif_package_get_id (pkg),
					hy_package_get_summary (pkg),
					hy_package_get_license (pkg),
					PK_GROUP_ENUM_UNKNOWN,
					hif_package_get_description (pkg),
					hy_package_get_url (pkg),
					(gulong) hy_package_get_size (pkg));
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_job_finished (job);
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
	gchar **full_paths;
	GError *error = NULL;
	guint i;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	HyStringArray files_array;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	g_variant_get (params, "(^a&s)", &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* create sack */
				   50, /* get details */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_NONE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ensure packages are not already installed */
	for (i = 0; full_paths[i] != NULL; i++) {
		pkg = hy_sack_add_cmdline_package (sack, full_paths[i]);
		g_warning ("full_paths[i]=%s", full_paths[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_FILE_NOT_FOUND,
						   "Failed to open %s",
						   full_paths[i]);
			goto out;
		}
		/* sort and list according to name */
		files_array = hy_package_get_files (pkg);
		pk_backend_job_files (job,
				      hif_package_get_id (pkg),
				      (gchar **) files_array);
		hy_stringarray_free (files_array);
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	pk_backend_job_finished (job);
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
	gchar **package_ids;
	gchar *tmp;
	GError *error = NULL;
	GHashTable *hash = NULL;
	GPtrArray *files = NULL;
	guint i;
	HifSource *src;
	HifState *state_local;
	HifState *state_loop;
	HyPackage pkg;
	HySack sack;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);

	g_variant_get (params, "(^a&ss)",
		       &package_ids,
		       &directory);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   1, /* ensure repos */
				   3, /* get sack */
				   5, /* find packages */
				   90, /* download packages */
				   1, /* emit */
				   -1);
	g_assert (ret);

	/* set the list of repos */
	ret = pk_backend_ensure_sources (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get sack */
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find packages */
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* download packages */
	files = g_ptr_array_new_with_free_func (g_free);
	state_local = hif_state_get_child (job_data->state);
	hif_state_set_number_steps (state_local, g_strv_length (package_ids));
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			goto out;
		}

		hif_emit_package (job, PK_INFO_ENUM_DOWNLOADING, pkg);

		/* get correct package source */
		src = hif_repos_get_source_by_id (priv->repos,
						  hy_package_get_reponame (pkg),
						  &error);
		if (src == NULL) {
			g_prefix_error (&error, "Not sure where to download %s: ",
					hy_package_get_name (pkg));
			pk_backend_job_error_code (job, error->code,
						   "%s", error->message);
			g_error_free (error);
			goto out;
		}

		/* download */
		state_loop = hif_state_get_child (state_local);
		tmp = hif_source_download_package (src,
						   pkg,
						   directory,
						   state_loop,
						   &error);
		if (tmp == NULL) {
			pk_backend_job_error_code (job, error->code,
						   "%s", error->message);
			g_error_free (error);
			goto out;
		}

		/* add to download list */
		g_ptr_array_add (files, tmp);

		/* done */
		ret = hif_state_done (state_local, &error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			g_error_free (error);
			goto out;
		}
	}
	g_ptr_array_add (files, NULL);

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* emit files so that the daemon will copy these */
	pk_backend_job_files (job, NULL, (gchar **) files->pdata);

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (files != NULL)
		g_ptr_array_unref (files);
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	g_cancellable_cancel (job_data->cancellable);
}

/**
 * pk_backend_transaction_check_untrusted_repos:
 */
static GPtrArray *
pk_backend_transaction_check_untrusted_repos (GPtrArray *sources,
					      HyGoal goal,
					      GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *array = NULL;
	GPtrArray *install;
	guint i;
	HifSource *src;
	HyPackage pkg;

	/* find any packages in untrusted repos */
	install = hif_goal_get_packages (goal,
					 HIF_PACKAGE_INFO_INSTALL,
					 HIF_PACKAGE_INFO_REINSTALL,
					 HIF_PACKAGE_INFO_DOWNGRADE,
					 HIF_PACKAGE_INFO_UPDATE,
					 -1);
	array = g_ptr_array_new ();
	for (i = 0; i < install->len; i++) {
		pkg = g_ptr_array_index (install, i);

		/* this is a standalone file, so by definition is from an
		 * untrusted repo */
		if (g_strcmp0 (hy_package_get_reponame (pkg),
			       HY_CMDLINE_REPO_NAME) == 0) {
			g_ptr_array_add (array, pkg);
			continue;
		}

		/* find repo */
		src = hif_repos_get_source_by_id (priv->repos,
						  hy_package_get_reponame (pkg),
						  error);
		if (src == NULL) {
			g_prefix_error (error, "Can't GPG check %s: ",
					hy_package_get_name (pkg));
			ret = FALSE;
			goto out;
		}

		/* repo has no gpg key */
		if (!hif_source_get_gpgcheck (src))
			g_ptr_array_add (array, pkg);
	}
out:
	if (array != NULL && !ret) {
		g_ptr_array_unref (array);
		array = NULL;
	}
	g_ptr_array_unref (install);
	return array;
}

/**
 * pk_backend_transaction_simulate:
 */
static gboolean
pk_backend_transaction_simulate (PkBackendJob *job,
				 HifState *state,
				 GError **error)
{
	GPtrArray *untrusted = NULL;
	HifDb *db;
	HyPackageList pkglist;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	gboolean ret;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   99, /* check for untrusted repos */
				   1, /* emit */
				   -1);
	if (!ret)
		goto out;

	/* set the list of repos */
	ret = pk_backend_ensure_sources (job_data, error);
	if (!ret)
		goto out;

	/* mark any explicitly-untrusted packages so that the transaction skips
	 * straight to only_trusted=FALSE after simulate */
	untrusted = pk_backend_transaction_check_untrusted_repos (job_data->sources,
								  job_data->goal, error);
	if (untrusted == NULL) {
		ret = FALSE;
		goto out;
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* emit what we're going to do */
	db = hif_transaction_get_db (hif_context_get_transaction (priv->context));
	hif_emit_package_array (job, PK_INFO_ENUM_UNTRUSTED, untrusted);
	pkglist = hy_goal_list_erasures (job_data->goal);
	hif_db_ensure_origin_pkglist (db, pkglist);
	hif_emit_package_list (job, PK_INFO_ENUM_REMOVING, pkglist);
	pkglist = hy_goal_list_installs (job_data->goal);
	hif_db_ensure_origin_pkglist (db, pkglist);
	hif_emit_package_list (job, PK_INFO_ENUM_INSTALLING, pkglist);
	pkglist = hy_goal_list_obsoleted (job_data->goal);
	hif_emit_package_list (job, PK_INFO_ENUM_OBSOLETING, pkglist);
	pkglist = hy_goal_list_reinstalls (job_data->goal);
	hif_db_ensure_origin_pkglist (db, pkglist);
	hif_emit_package_list (job, PK_INFO_ENUM_REINSTALLING, pkglist);
	pkglist = hy_goal_list_upgrades (job_data->goal);
	hif_db_ensure_origin_pkglist (db, pkglist);
	hif_emit_package_list (job, PK_INFO_ENUM_UPDATING, pkglist);
	pkglist = hy_goal_list_downgrades (job_data->goal);
	hif_db_ensure_origin_pkglist (db, pkglist);
	hif_emit_package_list (job, PK_INFO_ENUM_DOWNGRADING, pkglist);

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (untrusted != NULL)
		g_ptr_array_unref (untrusted);
	return ret;
}

/**
 * pk_backend_transaction_download_commit:
 */
static gboolean
pk_backend_transaction_download_commit (PkBackendJob *job,
					HifState *state,
					GError **error)
{
	gboolean ret = TRUE;
	HifState *state_local;
	HifTransaction *transaction;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	/* nothing to download */
	transaction = hif_context_get_transaction (priv->context);
	if (hif_transaction_get_remote_pkgs(transaction)->len == 0) {
		return hif_transaction_commit (transaction,
					       job_data->goal,
					       state,
					       error);
	}

	/* set state */
	ret = hif_state_set_steps (state, error,
				   50, /* download */
				   50, /* install/remove */
				   -1);
	if (!ret)
		goto out;

	/* download */
	state_local = hif_state_get_child (state);
	ret = hif_transaction_download (transaction,
					state_local,
					error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* run transaction */
	state_local = hif_state_get_child (state);
	ret = hif_transaction_commit (transaction,
				      job_data->goal,
				      state_local,
				      error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_backend_transaction_run:
 */
static gboolean
pk_backend_transaction_run (PkBackendJob *job,
			    HifState *state,
			    GError **error)
{
	HifState *state_local;
	HifTransaction *transaction;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	gboolean ret = TRUE;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   5, /* depsolve */
				   95, /* everything else */
				   -1);
	if (!ret)
		goto out;

	/* depsolve */
	transaction = hif_context_get_transaction (priv->context);
	if (pk_bitfield_contain (job_data->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
		hif_transaction_set_flags (transaction,
					   HIF_TRANSACTION_FLAG_ONLY_TRUSTED);
	} else {
		hif_transaction_set_flags (transaction,
					   HIF_TRANSACTION_FLAG_NONE);
	}
	state_local = hif_state_get_child (state);
	ret = hif_transaction_depsolve (transaction,
					job_data->goal,
					state_local,
					error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* just simulate */
	if (pk_bitfield_contain (job_data->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		state_local = hif_state_get_child (state);
		ret = pk_backend_transaction_simulate (job,
						       state_local,
						       error);
		if (!ret)
			goto out;
		return hif_state_done (state, error);
	}

	/* just download */
	if (pk_bitfield_contain (job_data->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		state_local = hif_state_get_child (state);
		ret = hif_transaction_download (transaction,
						state_local,
						error);
		if (!ret)
			goto out;
		return hif_state_done (state, error);
	}

	/* download and commit transaction */
	state_local = hif_state_get_child (state);
	ret = pk_backend_transaction_download_commit (job, state_local, error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_backend_repo_remove_thread:
 */
static void
pk_backend_repo_remove_thread (PkBackendJob *job,
			       GVariant *params,
			       gpointer user_data)
{
	GError *error = NULL;
	GPtrArray *removed_id = NULL;
	GPtrArray *sources = NULL;
	HifDb *db;
	HifSource *src;
	HifState *state_local;
	HyPackage pkg;
	HyPackageList pkglist;
	HyQuery query = NULL;
	HyQuery query_release = NULL;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
	const gchar *from_repo;
	const gchar *repo_filename;
	const gchar *repo_id;
	const gchar *tmp;
	gboolean autoremove;
	gboolean ret;
	gboolean found;
	gchar **search = NULL;
	guint cnt = 0;
	guint i;
	guint j;

	g_variant_get (params, "(t&sb)",
		       &job_data->transaction_flags,
		       &repo_id,
		       &autoremove);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   1, /* get the .repo filename for @repo_id */
				   1, /* find any other repos in the same file */
				   10, /* remove any packages from repos */
				   3, /* remove repo-releases */
				   85, /* run transaction */
				   -1);
	g_assert (ret);

	/* find the repo-release package name for @repo_id */
	src = hif_repos_get_source_by_id (priv->repos, repo_id, &error);
	if (src == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find all the .repo files the repo-release package installed */
	sources = hif_repos_get_sources (priv->repos, &error);
	search = g_new0 (gchar *, sources->len + 0);
	removed_id = g_ptr_array_new_with_free_func (g_free);
	repo_filename = hif_source_get_filename (src);
	for (i = 0; i < sources->len; i++) {
		src = g_ptr_array_index (sources, i);
		if (g_strcmp0 (hif_source_get_filename (src), repo_filename) != 0)
			continue;

		/* this repo_id will get purged */
		tmp = hif_source_get_id (src);
		g_debug ("adding id %s to check", tmp);
		g_ptr_array_add (removed_id, g_strdup (tmp));

		/* the package that installed the .repo file will be removed */
		tmp = hif_source_get_filename (src);
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
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* remove all the packages installed from all these repos */
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
	job_data->goal = hy_goal_create (sack);
	query = hy_query_create (sack);
	pkglist = hy_query_run (query);
	db = hif_transaction_get_db (hif_context_get_transaction (priv->context));
	FOR_PACKAGELIST(pkg, pkglist, i) {
		hif_db_ensure_origin_pkg (db, pkg);
		from_repo = hif_package_get_origin (pkg);
		if (from_repo == NULL)
			continue;
		for (j = 0; j < removed_id->len; j++) {
			tmp = g_ptr_array_index (removed_id, j);
			if (g_strcmp0 (tmp, from_repo) == 0) {
				g_debug ("%s %s as installed from %s",
					 autoremove ? "removing" : "ignoring",
					 hy_package_get_name (pkg),
					 from_repo);
				if (autoremove) {
					hif_package_set_user_action (pkg, TRUE);
					hy_goal_erase (job_data->goal, pkg);
				}
				break;
			}
		}
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* remove the repo-releases */
	query_release = hy_query_create (sack);
	hy_query_filter_in (query_release, HY_PKG_FILE, HY_EQ, (const gchar **) search);
	pkglist = hy_query_run (query_release);
	FOR_PACKAGELIST(pkg, pkglist, i) {
		hif_db_ensure_origin_pkg (db, pkg);
		g_debug ("removing %s as installed for repo",
			 hy_package_get_name (pkg));
		hif_package_set_user_action (pkg, TRUE);
		hy_goal_erase (job_data->goal, pkg);
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_strfreev (search);
	if (query != NULL)
		hy_query_free (query);
	if (query_release != NULL)
		hy_query_free (query_release);
	if (sources != NULL)
		g_ptr_array_unref (sources);
	if (removed_id != NULL)
		g_ptr_array_unref (removed_id);
	pk_backend_job_finished (job);
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
 * hif_is_installed_package_id_name:
 */
static gboolean
hif_is_installed_package_id_name (HySack sack, const gchar *package_id)
{
	gboolean ret;
	gchar **split;
	HyPackageList pkglist = NULL;
	HyQuery query = NULL;

	/* run query */
	query = hy_query_create (sack);
	split = pk_package_id_split (package_id);
	hy_query_filter (query, HY_PKG_NAME, HY_EQ, split[PK_PACKAGE_ID_NAME]);
	hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	pkglist = hy_query_run (query);

	/* any matches? */
	ret = hy_packagelist_count (pkglist) > 0;

	hy_packagelist_free (pkglist);
	hy_query_free (query);
	g_strfreev (split);
	return ret;
}

/**
 * hif_is_installed_package_id_name_arch:
 */
static gboolean
hif_is_installed_package_id_name_arch (HySack sack, const gchar *package_id)
{
	gboolean ret;
	gchar **split;
	HyPackageList pkglist = NULL;
	HyQuery query = NULL;

	/* run query */
	query = hy_query_create (sack);
	split = pk_package_id_split (package_id);
	hy_query_filter (query, HY_PKG_NAME, HY_EQ, split[PK_PACKAGE_ID_NAME]);
	hy_query_filter (query, HY_PKG_ARCH, HY_EQ, split[PK_PACKAGE_ID_ARCH]);
	hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	pkglist = hy_query_run (query);

	/* any matches? */
	ret = hy_packagelist_count (pkglist) > 0;

	hy_packagelist_free (pkglist);
	hy_query_free (query);
	g_strfreev (split);
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
	GError *error = NULL;
	GHashTable *hash = NULL;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean allow_deps;
	gboolean autoremove;
	gboolean ret;
	gchar **package_ids;
	guint i;

	g_variant_get (params, "(t^a&sbb)",
		       &job_data->transaction_flags,
		       &package_ids,
		       &allow_deps,
		       &autoremove);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
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
		goto out;
	}
	if (!allow_deps) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_NOT_SUPPORTED,
					   "!allow_deps is not supported");
		goto out;
	}

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	//TODO: check if we're trying to remove protected packages like:
	//glibc, kernel, etc

	/* ensure packages are already installed */
	for (i = 0; package_ids[i] != NULL; i++) {
		ret = hif_is_installed_package_id_name_arch (sack, package_ids[i]);
		if (!ret) {
			gchar *printable_tmp;
			printable_tmp = pk_package_id_to_printable (package_ids[i]);
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
						   "%s is not already installed",
						   printable_tmp);
			g_free (printable_tmp);
			goto out;
		}
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find packages */
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* remove packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			goto out;
		}
		hif_package_set_user_action (pkg, TRUE);
		hy_goal_erase (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	GError *error = NULL;
	GHashTable *hash = NULL;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean ret;
	gchar **package_ids;
	guint i;

	g_variant_get (params, "(t^a&s)",
		       &job_data->transaction_flags,
		       &package_ids);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   3, /* add repos */
				   1, /* check installed */
				   1, /* find packages */
				   95, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ensure packages are not already installed */
	for (i = 0; package_ids[i] != NULL; i++) {
		ret = hif_is_installed_package_id_name_arch (sack, package_ids[i]);
		if (ret) {
			gchar *printable_tmp;
			printable_tmp = pk_package_id_to_printable (package_ids[i]);
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
						   "%s is aleady installed",
						   printable_tmp);
			g_free (printable_tmp);
			goto out;
		}
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find remote packages */
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			goto out;
		}
		hif_package_set_user_action (pkg, TRUE);
		hy_goal_install (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	GError *error = NULL;
	GHashTable *hash = NULL;
	GPtrArray *array = NULL;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean ret;
	gchar **full_paths;
	guint i;

	g_variant_get (params, "(t^a&s)",
		       &job_data->transaction_flags,
		       &full_paths);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   25, /* check installed */
				   24, /* run transaction */
				   1, /* emit */
				   -1);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	g_assert (ret);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_NONE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ensure packages are not already installed */
	array = g_ptr_array_new ();
	for (i = 0; full_paths[i] != NULL; i++) {
		pkg = hy_sack_add_cmdline_package (sack, full_paths[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_FILE_NOT_FOUND,
						   "Failed to open %s",
						   full_paths[i]);
			goto out;
		}

		/* we don't download this, we just use it */
		hif_package_set_filename (pkg, full_paths[i]);
		g_ptr_array_add (array, pkg);
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		hy_goal_install (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	GError *error = NULL;
	GHashTable *hash = NULL;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;
	gboolean ret;
	gchar **package_ids;
	guint i;

	g_variant_get (params, "(t^a&s)",
		       &job_data->transaction_flags,
		       &package_ids);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   8, /* add repos */
				   1, /* check installed */
				   1, /* find packages */
				   90, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set up the sack for packages that should only ever be installed, never updated */
	hy_sack_set_installonly (sack, hif_context_get_installonly_pkgs (priv->context));
	hy_sack_set_installonly_limit (sack, hif_context_get_installonly_limit (priv->context));

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* ensure packages are not already installed */
	for (i = 0; package_ids[i] != NULL; i++) {
		ret = hif_is_installed_package_id_name (sack, package_ids[i]);
		if (!ret) {
			gchar *printable_tmp;
			printable_tmp = pk_package_id_to_printable (package_ids[i]);
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
						   "cannot update: %s is not already installed",
						   printable_tmp);
			g_free (printable_tmp);
			goto out;
		}
	}
	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find packages */
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* install packages */
	job_data->goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			goto out;
		}
		hif_package_set_user_action (pkg, TRUE);

		/* allow some packages to have multiple versions installed */
		if (hif_package_is_installonly (pkg))
			hy_goal_install (job_data->goal, pkg);
		else
			hy_goal_upgrade_to (job_data->goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, state_local, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	gchar **package_ids;
	GError *error = NULL;
	GHashTable *hash = NULL;
	GPtrArray *files;
	guint i;
	guint j;
	HifState *state_local;
	HyPackage pkg;
	HySack sack;
	HyStringArray files_array;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   90, /* add repos */
				   5, /* find packages */
				   5, /* emit files */
				   -1);
	g_assert (ret);

	/* get sack */
	g_variant_get (params, "(^a&s)", &package_ids);
	filters = hif_get_filter_for_ids (package_ids);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find packages */
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* emit details */
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
						   "Failed to find %s", package_ids[i]);
			goto out;
		}

		/* sort and list according to name */
		files_array = hy_package_get_files (pkg);
		if (FALSE) {
			files = g_ptr_array_new ();
			for (j = 0; files_array[j] != NULL; j++)
				g_ptr_array_add (files, files_array[j]);
			g_ptr_array_sort (files,
					  (GCompareFunc) pk_backend_sort_string_cb);
			g_ptr_array_add (files, NULL);
			pk_backend_job_files (job,
					      package_ids[i],
					      (gchar **) files->pdata);
			g_ptr_array_unref (files);
		} else {
			pk_backend_job_files (job,
					      package_ids[i],
					      (gchar **) files_array);
		}
		hy_stringarray_free (files_array);
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	gchar **package_ids;
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash = NULL;
	guint i;
	HifState *state_local;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   49, /* find packages */
				   1, /* emit update details */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job,
						  filters,
						  HIF_CREATE_SACK_FLAG_USE_CACHE,
						  state_local,
						  &error);
	if (sack == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* find remote packages */
	g_variant_get (params, "(^a&s)", &package_ids);
	hash = hif_utils_find_package_ids (sack, package_ids, &error);
	if (hash == NULL) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* emit details for each */
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		if (pkg == NULL)
			continue;
		pk_backend_job_update_detail (job,
					      package_ids[i],
					      NULL,
					      NULL,
					      hy_package_get_update_urls_vendor (pkg),
					      hy_package_get_update_urls_bugzilla (pkg),
					      hy_package_get_update_urls_cve (pkg),
					      PK_RESTART_ENUM_NONE, /* FIXME */
					      hy_package_get_update_description (pkg),
					      NULL,
					      PK_UPDATE_STATE_ENUM_STABLE, /* FIXME */
					      NULL, /* issued */
					      NULL /* updated */);
	}

	/* done */
	ret = hif_state_done (job_data->state, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (hash != NULL)
		g_hash_table_unref (hash);
	pk_backend_job_finished (job);
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
	GFile *file;
	gboolean ret;
	gchar *path;

	path = g_build_filename ("/var/lib/rpm", index_fn, NULL);
	g_debug ("deleting %s", path);
	file = g_file_new_for_path (path);
	ret = g_file_delete (file, NULL, error);
	if (!ret)
		goto out;
out:
	g_object_unref (file);
	g_free (path);
	return ret;
}

/**
 * pk_backend_repair_system_thread:
 */
static void
pk_backend_repair_system_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	GDir *dir = NULL;
	GError *error = NULL;
	PkBitfield transaction_flags;
	const gchar *tmp;
	gboolean ret;

	/* don't do anything when simulating */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	transaction_flags = pk_backend_job_get_transaction_flags (job);
	if (pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE))
		goto out;

	/* open the directory */
	dir = g_dir_open ("/var/lib/rpm", 0, &error);
	if (dir == NULL) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_INSTALL_ROOT_INVALID,
					   "%s", error->message);
		g_error_free (error);
		goto out;
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
			g_error_free (error);
			goto out;
		}
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	pk_backend_job_finished (job);
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
