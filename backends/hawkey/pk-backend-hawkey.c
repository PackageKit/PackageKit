/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <gmodule.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include <pk-backend.h>
#include <packagekit-glib2/pk-debug.h>

#include <hawkey/packagelist.h>
#include <hawkey/query.h>
#include <hawkey/sack.h>
#include <hawkey/stringarray.h>
#include <hawkey/goal.h>
#include <hawkey/version.h>
#include <hawkey/util.h>
#include <librepo/librepo.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmlog.h>
#include <rpm/rpmps.h>
#include <rpm/rpmts.h>
#include <rpm/rpmkeyring.h>

#include "hif-config.h"
#include "hif-db.h"
#include "hif-goal.h"
#include "hif-keyring.h"
#include "hif-package.h"
#include "hif-rpmts.h"
#include "hif-source.h"
#include "hif-state.h"
#include "hif-utils.h"

typedef struct {
	HifConfig	*config;
	gchar		*repos_dir;
	GFileMonitor	*monitor;
} PkBackendHifPrivate;

typedef struct {
	GPtrArray	*enabled_sources;
	GCancellable	*cancellable;
	HifDb		*db;
	HifState	*state;
	rpmts		 ts;
	rpmKeyring	 keyring;
} PkBackendHifJobData;

static PkBackendHifPrivate *priv;

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Hawkey");
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
 * pk_backend_yum_repos_changed_cb:
 **/
static void
pk_backend_yum_repos_changed_cb (GFileMonitor *monitor_,
				 GFile *file, GFile *other_file,
				 GFileMonitorEvent event_type,
				 PkBackend *backend)
{
	pk_backend_repo_list_changed (backend);
}

/**
 * pk_backend_initialize:
 */
void
pk_backend_initialize (PkBackend *backend)
{
	gboolean ret;
	GError *error = NULL;
	gint retval;
	GFile *file = NULL;

	/* use logging */
	pk_debug_add_log_domain (G_LOG_DOMAIN);

	/* create private area */
	priv = g_new0 (PkBackendHifPrivate, 1);

	g_debug ("Using Hawkey %i.%i.%i",
		 HY_VERSION_MAJOR,
		 HY_VERSION_MINOR,
		 HY_VERSION_PATCH);
	g_debug ("Using librepo %i.%i.%i\n",
		 LR_VERSION_MAJOR,
		 LR_VERSION_MINOR,
		 LR_VERSION_PATCH);

	retval = rpmReadConfigFiles (NULL, NULL);
	if (retval != 0)
		g_error ("failed to read rpm config files");

	/* HifConfig */
	priv->config = hif_config_new ();
	ret = hif_config_set_filename (priv->config, NULL, &error);
	if (!ret) {
		g_warning ("failed to set config: %s",
			   error->message);
		g_error_free (error);
	}

	/* set defaults */
	hif_config_set_boolean (priv->config, "DiskSpaceCheck", TRUE, NULL);
	hif_config_set_boolean (priv->config, "RpmCheckDebug", TRUE, NULL);
	hif_config_set_string (priv->config, "CacheDir", "/var/cache/PackageKit/metadata", NULL);
	hif_config_set_string (priv->config, "PidFile", "/var/run/hif", NULL);
	hif_config_set_string (priv->config, "ReposDir", "/etc/yum.repos.d", NULL);
	hif_config_set_string (priv->config, "RpmVerbosity", "info", NULL);

	/* setup a file monitor on the repos directory */
	priv->repos_dir = hif_config_get_string (priv->config, "ReposDir", &error);
	g_assert (priv->repos_dir != NULL);
	file = g_file_new_for_path (priv->repos_dir);
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

	lr_global_init ();

	if (file != NULL)
		g_object_unref (file);
}

/**
 * pk_backend_destroy:
 */
void
pk_backend_destroy (PkBackend *backend)
{
	if (priv->config != NULL)
		g_object_unref (priv->config);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
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
				    PkStatusEnum action,
				    const gchar *action_hint,
				    PkBackendJob *job)
{
	g_debug ("got state %s with hint %s",
		 pk_status_enum_to_string (action),
		 action_hint);
	if (action != PK_STATUS_ENUM_UNKNOWN)
		pk_backend_job_set_status (job, action);

	switch (action) {
	case PK_STATUS_ENUM_DOWNLOAD:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_DOWNLOADING,
						action_hint,
						"");
		}
		break;
	case PK_STATUS_ENUM_INSTALL:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_INSTALLING,
						action_hint,
						"");
		}
		break;
	case PK_STATUS_ENUM_REMOVE:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_REMOVING,
						action_hint,
						"");
		}
		break;
	case PK_STATUS_ENUM_UPDATE:
		if (pk_package_id_check (action_hint)) {
			pk_backend_job_package (job,
						PK_INFO_ENUM_UPDATING,
						action_hint,
						"");
		}
		break;
	case PK_STATUS_ENUM_CLEANUP:
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

	/* setup RPM */
	job_data->ts = rpmtsCreate ();
	job_data->keyring = rpmtsGetKeyring (job_data->ts, 1);

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

	/* #HifDb is a simple flat file 'database' for stroring details about
	 * installed packages, such as the command line that installed them,
	 * the uid of the user performing the action and the repository they
	 * came from.
	 *
	 * A yumdb is not really a database at all, and is really slow to read
	 * and especially slow to write data for packages. It is provided for
	 * compatibility with existing users of yum, but long term this
	 * functionality should either be folded into rpm itself, or just put
	 * into an actual database format like sqlite.
	 *
	 * Using the filesystem as a database probably wasn't a great design
	 * decision. */
	job_data->db = hif_db_new ();

	/* we don't want to enable this for normal runtime */
	hif_state_set_enable_profile (job_data->state, TRUE);
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
	if (job_data->state != NULL)
		g_object_unref (job_data->state);
	if (job_data->enabled_sources != NULL)
		g_ptr_array_unref (job_data->enabled_sources);
	rpmtsFree (job_data->ts);
	rpmKeyringFree (job_data->keyring);
	g_object_unref (job_data->db);
	g_free (job_data);
	pk_backend_job_set_user_data (job, NULL);
}

/**
 * hif_utils_add_source:
 */
static gboolean
hif_utils_add_source (HySack sack,
		      HifSource *src,
		      HifState *state,
		      GError **error)
{
	gboolean ret = TRUE;
	GError *error_local = NULL;
	gint rc;
	HifState *state_local;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   5, /* check repo */
				   95, /* load solv */
				   -1);
	if (!ret)
		goto out;

	/* check repo */
	state_local = hif_state_get_child (state);
	ret = hif_source_check (src, state_local, &error_local);
	if (!ret) {
		g_debug ("failed to check, attempting update: %s",
			 error_local->message);
		g_error_free (error_local);
		hif_state_reset (state_local);
		ret = hif_source_update (src, state_local, error);
		if (!ret)
			goto out;
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* load solv */
	g_debug ("Loading repo %s", hif_source_get_id (src));
	hif_state_action_start (state, PK_STATUS_ENUM_LOADING_CACHE, NULL);
	rc = hy_sack_load_yum_repo (sack,
				    hif_source_get_repo (src),
				    HY_LOAD_FILELISTS |
				    HY_LOAD_UPDATEINFO |
				    HY_BUILD_CACHE);
	ret = hif_rc_to_gerror (rc, error);
	if (!ret) {
		g_prefix_error (error, "Failed to load repo %s: ",
				hif_source_get_id (src));
		goto out;
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * hif_utils_add_sources:
 */
static gboolean
hif_utils_add_sources (HySack sack,
		       GPtrArray *sources,
		       HifState *state,
		       GError **error)
{
	gboolean ret = TRUE;
	guint i;
	HifSource *src;
	HifState *state_local;

	/* add each repo */
	hif_state_set_number_steps (state, sources->len);
	for (i = 0; i < sources->len; i++) {
		src = g_ptr_array_index (sources, i);

		state_local = hif_state_get_child (state);
		ret = hif_utils_add_source (sack, src, state_local, error);
		if (!ret)
			goto out;

		/* done */
		ret = hif_state_done (state, error);
		if (!ret)
			goto out;
	}

	/* success */
	ret = TRUE;
out:
	return ret;
}



/**
 * pk_backend_ensure_enabled_sources:
 */
static gboolean
pk_backend_ensure_enabled_sources (PkBackendHifJobData *job_data, GError **error)
{
	gboolean ret = TRUE;

	/* already set */
	if (job_data->enabled_sources != NULL)
		goto out;

	/* set the list of repos */
	job_data->enabled_sources = hif_source_find_all (priv->repos_dir,
							 HIF_SOURCE_SCAN_FLAG_ONLY_ENABLED,
							 error);
	if (job_data->enabled_sources == NULL) {
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
	ret = pk_backend_ensure_enabled_sources (job_data, error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* add each repo */
	state_local = hif_state_get_child (state);
	ret = hif_utils_add_sources (sack, job_data->enabled_sources, state_local, error);
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
 * hif_utils_create_sack_for_filters:
 */
static HySack
hif_utils_create_sack_for_filters (PkBackendJob *job,
				   PkBitfield filters,
				   HifState *state,
				   GError **error)
{
	const gchar *cachedir = "/var/cache/PackageKit/hawkey";
	gboolean add_repos = TRUE;
	gboolean ret;
	gint rc;
	HifState *state_local;
	HySack sack = NULL;

	/* don't add if we're going to filter out anyway */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		add_repos = FALSE;

	/* update status */
	hif_state_action_start (state, PK_STATUS_ENUM_QUERY, NULL);

	/* set state */
	if (add_repos) {
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
	//FIXME: get from config
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
	if (add_repos) {
		state_local = hif_state_get_child (state);
		ret = hif_utils_add_remote (job, sack, state_local, error);
		if (!ret)
			goto out;

		/* done */
		ret = hif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
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
	const gchar *native_arches[] = { "x86_64", NULL };

	/* newest */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		hy_query_filter_latest (query, TRUE);

	/* arch */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH))
		hy_query_filter_in (query, HY_PKG_ARCH, HY_EQ, (const gchar **) native_arches);
	else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH))
		hy_query_filter_in (query, HY_PKG_ARCH, HY_NEQ, (const gchar **) native_arches);

	/* installed */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
		hy_query_filter (query, HY_PKG_REPONAME, HY_NEQ, HY_SYSTEM_REPO_NAME);

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
pk_backend_what_provides_decompose (PkProvidesEnum provides,
				    gchar **values,
				    GError **error)
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
		if (g_str_has_prefix (values[i], "gstreamer0.10(") ||
		    g_str_has_prefix (values[i], "gstreamer1(")) {
			g_ptr_array_add (array, g_strdup (values[i]));
		} else if (provides == PK_PROVIDES_ENUM_CODEC) {
			g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
			g_ptr_array_add (array, g_strdup_printf ("gstreamer1(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_FONT) {
			g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_MIMETYPE) {
			g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_POSTSCRIPT_DRIVER) {
			g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		} else if (provides == PK_PROVIDES_ENUM_PLASMA_SERVICE) {
			/* We need to allow the Plasma version to be specified. */
			if (g_str_has_prefix (values[i], "plasma")) {
				g_ptr_array_add (array, g_strdup (values[i]));
			} else {
				/* For compatibility, we default to plasma4. */
				g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
			}
		} else if (provides == PK_PROVIDES_ENUM_ANY) {
			/* We need to allow the Plasma version to be specified. */
			if (g_str_has_prefix (values[i], "plasma")) {
				g_ptr_array_add (array, g_strdup (values[i]));
			} else {
				g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
				g_ptr_array_add (array, g_strdup_printf ("gstreamer1(%s)", values[i]));
				g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
				g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
				g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
				g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
				g_ptr_array_add (array, g_strdup_printf ("plasma5(%s)", values[i]));
			}
		} else {
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED,
				     "provide type %s not supported",
				     pk_provides_enum_to_string (provides));
			goto out;
		}
	}
	search = pk_ptr_array_to_strv (array);
	for (i = 0; search[i] != NULL; i++)
		g_debug ("Querying provide '%s'", search[i]);
out:
	return search;
}

/**
 * hif_package_ensure_filename:
 */
static gboolean
hif_package_ensure_filename (GPtrArray *sources, HyPackage pkg, GError **error)
{
	gboolean ret = TRUE;
	gchar *basename = NULL;
	gchar *filename = NULL;
	HifSource *src;

	/* get repo */
	if (hy_package_installed (pkg))
		goto out;
	src = hif_source_filter_by_id (sources,
				       hy_package_get_reponame (pkg),
				       error);
	if (src == NULL) {
		ret = FALSE;
		goto out;
	}

	/* make default cache filename location */
	basename = g_path_get_basename (hy_package_get_location (pkg));
	filename = g_build_filename (hif_source_get_location (src),
				     "packages",
				     basename,
				     NULL);
	hif_package_set_filename (pkg, filename);
out:
	g_free (filename);
	g_free (basename);
	return ret;
}

/**
 * hif_package_ensure_filename_list:
 */
static gboolean
hif_package_ensure_filename_list (GPtrArray *sources,
				  HyPackageList pkglist,
				  GError **error)
{
	gboolean ret = TRUE;
	guint i;
	HyPackage pkg;

	FOR_PACKAGELIST(pkg, pkglist, i) {
		ret = hif_package_ensure_filename (sources, pkg, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * pk_backend_search:
 */
static void
pk_backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gboolean ret;
	gchar **search = NULL;
	gchar **search_tmp;
	GError *error = NULL;
	HifState *state_local;
	HyPackageList pkglist = NULL;
	HyQuery query = NULL;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters = 0;
	PkProvidesEnum provides;

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   98, /* add repos */
				   2, /* query */
				   -1);
	g_assert (ret);

	/* get arguments */
	switch (pk_backend_job_get_role (job)) {
	case PK_ROLE_ENUM_GET_UPDATES:
	case PK_ROLE_ENUM_GET_PACKAGES:
		g_variant_get (params, "(t)", &filters);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		g_variant_get (params, "(tu^a&s)",
			       &filters,
			       &provides,
			       &search_tmp);
		search = pk_backend_what_provides_decompose (provides,
							     search_tmp,
							     &error);
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
	ret = pk_backend_ensure_enabled_sources (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get sack */
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		break;
	case PK_ROLE_ENUM_RESOLVE:
		hy_query_filter_in (query, HY_PKG_NAME, HY_EQ, (const gchar **) search);
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		hy_query_filter_in (query, HY_PKG_FILE, HY_EQ, (const gchar **) search);
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		hy_query_filter_in (query, HY_PKG_DESCRIPTION, HY_SUBSTR, (const gchar **) search);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		hy_query_filter_in (query, HY_PKG_NAME, HY_SUBSTR, (const gchar **) search);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		//FIXME: we want to use hy_query_filter_provides_in()
		hy_query_filter_provides (query, HY_EQ, search[0], NULL);
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		//* FIXME: We should really use hy_goal_upgrade_all */
		hy_query_filter_upgrades (query, TRUE);
		hy_query_filter_latest (query, TRUE);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	pkglist = hy_query_run (query);

	/* set the cache filename on each package */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DOWNLOADED) ||
	    pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DOWNLOADED)) {
		ret = hif_package_ensure_filename_list (job_data->enabled_sources,
							pkglist,
							&error);
		if (!ret) {
			pk_backend_job_error_code (job, error->code, "%s", error->message);
			g_error_free (error);
			goto out;
		}
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
	if (sack != NULL)
		hy_sack_free (sack);
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
			  PkProvidesEnum provides,
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
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield filters)
{
	gchar *description;
	GError *error = NULL;
	GPtrArray *sources;
	guint i;
	HifSource *src;

	/* set the list of repos */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	sources = hif_source_find_all (priv->repos_dir,
				       HIF_SOURCE_SCAN_FLAG_NONE,
				       &error);
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

		/* allow filtering on devel and ~devel */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) && !hif_source_is_devel (src))
			continue;
		else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && hif_source_is_devel (src))
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
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend,
			  PkBackendJob *job,
			  const gchar *repo_id,
			  const gchar *parameter,
			  const gchar *value)
{
	gboolean ret = FALSE;
	GError *error = NULL;
	GPtrArray *sources;
	HifSource *src;

	/* set the list of repos */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, 0);
	sources = hif_source_find_all (priv->repos_dir,
				       HIF_SOURCE_SCAN_FLAG_NONE,
				       &error);
	if (sources == NULL) {
		pk_backend_job_error_code (job,
					   error->code,
					   "failed to scan yum.repos.d: %s",
					   error->message);
		g_error_free (error);
		goto out;
	}

	/* find the correct repo */
	src = hif_source_filter_by_id (sources, repo_id, &error);
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
	if (sources != NULL)
		g_ptr_array_unref (sources);
	pk_backend_job_finished (job);
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
		PK_FILTER_ENUM_DOWNLOADED,
		-1);
}
/**
 * pk_backend_get_provides:
 */
PkBitfield
pk_backend_get_provides (PkBackend *backend)
{
	return pk_bitfield_from_enums(
		PK_PROVIDES_ENUM_ANY,
		PK_PROVIDES_ENUM_CODEC,
		PK_PROVIDES_ENUM_FONT,
		PK_PROVIDES_ENUM_MIMETYPE,
		PK_PROVIDES_ENUM_POSTSCRIPT_DRIVER,
		PK_PROVIDES_ENUM_PLASMA_SERVICE,
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
pk_backend_refresh_source (HifSource *src, HifState *state, GError **error)
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
	src_okay = hif_source_check (src, state_local, &error_local);
	if (!src_okay) {
		g_debug ("repo %s not okay [%s], refreshing",
			 hif_source_get_id (src), error_local->message);
		g_error_free (error_local);
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* update repo, TODO: if we have network access */
	if (!src_okay) {
		state_local = hif_state_get_child (state);
		ret = hif_source_update (src, state_local, error);
		if (!ret)
			goto out;
	}

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend,
			  PkBackendJob *job,
			  gboolean force)
{
	gboolean ret;
	GError *error = NULL;
	guint i;
	HifSource *src;
	HifState *state_local;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	/* set the list of repos */
	ret = pk_backend_ensure_enabled_sources (job_data, &error);
	if (!ret) {
		pk_backend_job_error_code (job, error->code, "%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* refresh each repo */
	hif_state_set_number_steps (job_data->state, job_data->enabled_sources->len);
	for (i = 0; i < job_data->enabled_sources->len; i++) {
		src = g_ptr_array_index (job_data->enabled_sources, i);

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
		ret = pk_backend_refresh_source (src, state_local, &error);
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
 * hif_utils_find_package_ids:
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
		if (g_strcmp0 (reponame, "installed") == 0)
			reponame = HY_SYSTEM_REPO_NAME;
		hy_query_filter (query, HY_PKG_NAME, HY_EQ, split[PK_PACKAGE_ID_NAME]);
		hy_query_filter (query, HY_PKG_EVR, HY_EQ, split[PK_PACKAGE_ID_VERSION]);
		hy_query_filter (query, HY_PKG_ARCH, HY_EQ, split[PK_PACKAGE_ID_ARCH]);
		hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, reponame);
		pkglist = hy_query_run (query);
		g_strfreev (split);

		/* no matches */
		if (hy_packagelist_count (pkglist) == 0) {
			ret = FALSE;
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				     "Failed to find %s", package_ids[i]);
			goto out;
		}

		/* multiple matches */
		if (hy_packagelist_count (pkglist) > 1) {
			ret = FALSE;
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_PACKAGE_CONFLICTS,
				     "Multiple matches of %s", package_ids[i]);
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
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend,
			PkBackendJob *job,
			gchar **package_ids)
{
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash = NULL;
	guint i;
	HifState *state_local;
	HyPackage pkg;
	HySack sack;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

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
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		g_assert (pkg != NULL);
		pk_backend_job_details (job,
					package_ids[i],
					hy_package_get_license (pkg),
					PK_GROUP_ENUM_UNKNOWN,
					hy_package_get_description (pkg),
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
	if (sack != NULL)
		hy_sack_free (sack);
	pk_backend_job_finished (job);
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
				   50, /* add repos */
				   48, /* find packages */
				   1, /* download packages */
				   1, /* emit */
				   -1);
	g_assert (ret);

	/* get sack */
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		g_assert (pkg != NULL);
		hif_emit_package (job, PK_INFO_ENUM_DOWNLOADING, pkg);

		/* get correct package source */
		src = hif_source_filter_by_id (job_data->enabled_sources,
					       hy_package_get_reponame (pkg),
					       &error);
		if (src == NULL) {
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
	if (sack != NULL)
		hy_sack_free (sack);
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
 * pk_backend_transaction_download:
 */
static gboolean
pk_backend_transaction_download (GPtrArray *sources,
				 HyGoal goal,
				 HifState *state,
				 GError **error)
{
	gboolean ret = TRUE;
	gchar *tmp;
	GPtrArray *downloads;
	guint i;
	HifSource *src;
	HifState *state_local;
	PkBitfield types;
	HyPackage pkg;

	/* find a list of all the packages we might have to download */
	types = pk_bitfield_from_enums (PK_INFO_ENUM_INSTALLING,
					PK_INFO_ENUM_REINSTALLING,
					PK_INFO_ENUM_DOWNGRADING,
					PK_INFO_ENUM_UPDATING,
					-1);
	downloads = hif_goal_get_packages (goal, types);
	if (downloads->len == 0)
		goto out;

	/* download any package that is not currently installed */
	hif_state_set_number_steps (state, downloads->len);
	for (i = 0; i < downloads->len; i++) {
		pkg = g_ptr_array_index (downloads, i);

		/* find repo */
		src = hif_source_filter_by_id (sources,
					       hy_package_get_reponame (pkg),
					       error);
		if (src == NULL) {
			ret = FALSE;
			goto out;
		}

		/* get correct package source */
		ret = hif_package_ensure_filename (sources, pkg, error);
		if (!ret)
			goto out;

		/* download package: TODO: check if package already exists and checksum is okay */
		state_local = hif_state_get_child (state);
		tmp = hif_source_download_package (src, pkg, NULL, state_local, error);
		if (tmp == NULL) {
			ret = FALSE;
			goto out;
		}
		g_free (tmp);

		/* done */
		ret = hif_state_done (state, error);
		if (!ret)
			goto out;
	}
out:
	g_ptr_array_unref (downloads);
	return ret;
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
	PkBitfield types;

	/* find a list of all the packages we might have to download */
	types = pk_bitfield_from_enums (PK_INFO_ENUM_INSTALLING,
					PK_INFO_ENUM_REINSTALLING,
					PK_INFO_ENUM_DOWNGRADING,
					PK_INFO_ENUM_UPDATING,
					-1);

	/* find any packages in untrusted repos */
	install = hif_goal_get_packages (goal, types);
	array = g_ptr_array_new ();
	for (i = 0; i < install->len; i++) {
		pkg = g_ptr_array_index (install, i);

		/* find repo */
		src = hif_source_filter_by_id (sources,
					       hy_package_get_reponame (pkg),
					       error);
		if (src == NULL) {
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
 * pk_backend_transaction_check_untrusted:
 */
static gboolean
pk_backend_transaction_check_untrusted (rpmKeyring keyring,
					GPtrArray *sources,
					HyGoal goal,
					GError **error)
{
	const gchar *filename;
	gboolean ret = TRUE;
	GPtrArray *install;
	guint i;
	HyPackage pkg;
	PkBitfield types;

	/* find a list of all the packages we might have to download */
	types = pk_bitfield_from_enums (PK_INFO_ENUM_INSTALLING,
					PK_INFO_ENUM_REINSTALLING,
					PK_INFO_ENUM_DOWNGRADING,
					PK_INFO_ENUM_UPDATING,
					-1);
	install = hif_goal_get_packages (goal, types);
	if (install->len == 0)
		goto out;

	/* find any packages in untrusted repos */
	for (i = 0; i < install->len; i++) {
		pkg = g_ptr_array_index (install, i);

		/* ensure the filename is set */
		ret = hif_package_ensure_filename (sources, pkg, error);

		/* find the location of the local file */
		filename = hif_package_get_filename (pkg);
		if (filename == NULL) {
			ret = FALSE;
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_FILE_NOT_FOUND,
				     "Downloaded file for %s not found",
				     hy_package_get_name (pkg));
			goto out;
		}

		/* check file */
		ret = hif_keyring_check_untrusted_file (keyring,
							filename,
							error);
		if (!ret)
			goto out;
	}
out:
	g_ptr_array_unref (install);
	return ret;
}

typedef enum {
	HIF_TRANSACTION_STEP_STARTED,
	HIF_TRANSACTION_STEP_PREPARING,
	HIF_TRANSACTION_STEP_WRITING,
	HIF_TRANSACTION_STEP_IGNORE
} HifTransactionStep;

typedef struct {
	HifState		*state;
	HifState		*child;
	FD_t			 fd;
	HifTransactionStep	 step;
	GTimer			*timer;
	guint			 last_progress;
	GPtrArray		*remove;
	GPtrArray		*install;
} HifTransactionCommit;

/**
 * hif_commit_rpmcb_type_to_string:
 **/
static const gchar *
hif_commit_rpmcb_type_to_string (const rpmCallbackType what)
{
	const gchar *type = NULL;
	switch (what) {
	case RPMCALLBACK_UNKNOWN:
		type = "unknown";
		break;
	case RPMCALLBACK_INST_PROGRESS:
		type = "install-progress";
		break;
	case RPMCALLBACK_INST_START:
		type = "install-start";
		break;
	case RPMCALLBACK_INST_OPEN_FILE:
		type = "install-open-file";
		break;
	case RPMCALLBACK_INST_CLOSE_FILE:
		type = "install-close-file";
		break;
	case RPMCALLBACK_TRANS_PROGRESS:
		type = "transaction-progress";
		break;
	case RPMCALLBACK_TRANS_START:
		type = "transaction-start";
		break;
	case RPMCALLBACK_TRANS_STOP:
		type = "transaction-stop";
		break;
	case RPMCALLBACK_UNINST_PROGRESS:
		type = "uninstall-progress";
		break;
	case RPMCALLBACK_UNINST_START:
		type = "uninstall-start";
		break;
	case RPMCALLBACK_UNINST_STOP:
		type = "uninstall-stop";
		break;
	case RPMCALLBACK_REPACKAGE_PROGRESS:
		type = "repackage-progress";
		break;
	case RPMCALLBACK_REPACKAGE_START:
		type = "repackage-start";
		break;
	case RPMCALLBACK_REPACKAGE_STOP:
		type = "repackage-stop";
		break;
	case RPMCALLBACK_UNPACK_ERROR:
		type = "unpack-error";
		break;
	case RPMCALLBACK_CPIO_ERROR:
		type = "cpio-error";
		break;
	case RPMCALLBACK_SCRIPT_ERROR:
		type = "script-error";
		break;
	case RPMCALLBACK_SCRIPT_START:
		type = "script-start";
		break;
	case RPMCALLBACK_SCRIPT_STOP:
		type = "script-stop";
		break;
	case RPMCALLBACK_INST_STOP:
		type = "install-stop";
		break;
	}
	return type;
}

/**
 * hif_find_pkg_from_header:
 **/
static HyPackage
hif_find_pkg_from_header (GPtrArray *array, Header hdr)
{
	const gchar *arch;
	const gchar *name;
	const gchar *release;
	const gchar *version;
	guint epoch;
	guint i;
	HyPackage pkg;

	/* get details */
	name = headerGetString (hdr, RPMTAG_NAME);
	epoch = headerGetNumber (hdr, RPMTAG_EPOCH);
	version = headerGetString (hdr, RPMTAG_VERSION);
	release = headerGetString (hdr, RPMTAG_RELEASE);
	arch = headerGetString (hdr, RPMTAG_ARCH);

	/* find in array */
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		if (g_strcmp0 (name, hy_package_get_name (pkg)) != 0)
			continue;
		if (g_strcmp0 (version, hy_package_get_version (pkg)) != 0)
			continue;
		if (g_strcmp0 (release, hy_package_get_release (pkg)) != 0)
			continue;
		if (g_strcmp0 (arch, hy_package_get_arch (pkg)) != 0)
			continue;
		if (epoch != hy_package_get_epoch (pkg))
			continue;
		return pkg;
	}
	return NULL;
}

/**
 * hif_find_pkg_from_filename_suffix:
 **/
static HyPackage
hif_find_pkg_from_filename_suffix (GPtrArray *array,
				   const gchar *filename_suffix)
{
	const gchar *filename;
	guint i;
	HyPackage pkg;

	/* find in array */
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		filename = hif_package_get_filename (pkg);
		if (filename == NULL)
			continue;
		if (g_str_has_suffix (filename, filename_suffix))
			return pkg;
	}
	return NULL;
}

/**
 * hif_commit_ts_progress_cb:
 **/
static void *
hif_commit_ts_progress_cb (const void *arg,
				const rpmCallbackType what,
				const rpm_loff_t amount,
				const rpm_loff_t total,
				fnpyKey key, void *data)
{
	const char *filename = (const char *) key;
	const gchar *name = NULL;
	gboolean ret;
	gchar *package_id = NULL;
	GError *error_local = NULL;
	guint percentage;
	guint speed;
	Header hdr = (Header) arg;
	HyPackage pkg;
	PkStatusEnum action;
	void *rc = NULL;

	HifTransactionCommit *commit = (HifTransactionCommit *) data;

	if (hdr != NULL)
		name = headerGetString (hdr, RPMTAG_NAME);
	g_debug ("phase: %s (%i/%i, %s/%s)",
		 hif_commit_rpmcb_type_to_string (what),
		 (gint32) amount,
		 (gint32) total,
		 (const gchar *) key,
		 name);

	switch (what) {
	case RPMCALLBACK_INST_OPEN_FILE:

		/* valid? */
		if (filename == NULL || filename[0] == '\0')
			return NULL;

		/* open the file and return file descriptor */
		commit->fd = Fopen (filename, "r.ufdio");
		return (void *) commit->fd;
		break;

	case RPMCALLBACK_INST_CLOSE_FILE:

		/* just close the file */
		if (commit->fd != NULL) {
			Fclose (commit->fd);
			commit->fd = NULL;
		}
		break;

	case RPMCALLBACK_INST_START:

		/* find pkg */
		pkg = hif_find_pkg_from_filename_suffix (commit->install,
							 filename);
		if (pkg == NULL)
			g_assert_not_reached ();

		/* map to correct action code */
		action = PK_STATUS_ENUM_INSTALL;
//		if (pkg->reason == HIF_TRANSACTION_REASON_INSTALL_FOR_UPDATE)
//			action = PK_STATUS_ENUM_UPDATING;

		/* install start */
		commit->step = HIF_TRANSACTION_STEP_WRITING;
		commit->child = hif_state_get_child (commit->state);
		hif_state_action_start (commit->child,
					action,
					hif_package_get_id (pkg));
		g_debug ("install start: %s size=%i", filename, (gint32) total);
		break;

	case RPMCALLBACK_UNINST_START:

		/* invalid? */
		if (filename == NULL) {
			g_debug ("no filename set in uninst-start with total %i",
				 (gint32) total);
			commit->step = HIF_TRANSACTION_STEP_WRITING;
			break;
		}

		/* find pkg */
		pkg = hif_find_pkg_from_filename_suffix (commit->remove,
							 filename);
		if (pkg == NULL) {
			g_debug ("cannot find %s", filename);
			break;
		}

		/* remove start */
		commit->step = HIF_TRANSACTION_STEP_WRITING;
		commit->child = hif_state_get_child (commit->state);
		package_id = hif_package_get_id (pkg);
		hif_state_action_start (commit->child,
					PK_STATUS_ENUM_REMOVE,
					package_id);
		g_debug ("remove start: %s size=%i", filename, (gint32) total);
		break;

	case RPMCALLBACK_TRANS_PROGRESS:
	case RPMCALLBACK_INST_PROGRESS:

		/* we're preparing the transaction */
		if (commit->step == HIF_TRANSACTION_STEP_PREPARING ||
		    commit->step == HIF_TRANSACTION_STEP_IGNORE) {
			g_debug ("ignoring preparing %i / %i",
				 (gint32) amount, (gint32) total);
			break;
		}

		/* work out speed */
		speed = (amount - commit->last_progress) /
				g_timer_elapsed (commit->timer, NULL);
		hif_state_set_speed (commit->state, speed);
		commit->last_progress = amount;
		g_timer_reset (commit->timer);

		/* progress */
		percentage = (100.0f / (gfloat) total) * (gfloat) amount;
		g_debug ("progress %i/%i", (gint32) amount, (gint32) total);
		if (commit->child != NULL)
			hif_state_set_percentage (commit->child, percentage);

		/* update UI */
		pkg = hif_find_pkg_from_header (commit->install, hdr);
		if (pkg == NULL) {
			pkg = hif_find_pkg_from_filename_suffix (commit->install,
								 filename);
		}
		if (pkg == NULL) {
			g_debug ("cannot find %s (%s)", filename, name);
			break;
		}

		package_id = hif_package_get_id (pkg);
		hif_state_set_package_progress (commit->state,
						package_id,
						PK_STATUS_ENUM_INSTALL,
						percentage);
		break;

	case RPMCALLBACK_UNINST_PROGRESS:

		/* we're preparing the transaction */
		if (commit->step == HIF_TRANSACTION_STEP_PREPARING ||
		    commit->step == HIF_TRANSACTION_STEP_IGNORE) {
			g_debug ("ignoring preparing %i / %i",
				 (gint32) amount, (gint32) total);
			break;
		}

		/* progress */
		percentage = (100.0f / (gfloat) total) * (gfloat) amount;
		g_debug ("progress %i/%i", (gint32) amount, (gint32) total);
		if (commit->child != NULL)
			hif_state_set_percentage (commit->child, percentage);

		/* update UI */
		pkg = hif_find_pkg_from_header (commit->remove, hdr);
		if (pkg == NULL) {
			pkg = hif_find_pkg_from_filename_suffix (commit->remove,
								 filename);
		}
		if (pkg == NULL) {
			g_debug ("cannot find %s", name);
			break;
		}
		package_id = hif_package_get_id (pkg);
		hif_state_set_package_progress (commit->state,
						package_id,
						PK_STATUS_ENUM_REMOVE,
						percentage);
		break;

	case RPMCALLBACK_TRANS_START:

		/* we setup the state */
		g_debug ("preparing transaction with %i items", (gint32) total);
		if (commit->step == HIF_TRANSACTION_STEP_IGNORE)
			break;

		hif_state_set_number_steps (commit->state, total);
		commit->step = HIF_TRANSACTION_STEP_PREPARING;
		break;

	case RPMCALLBACK_TRANS_STOP:

		/* don't do anything */
		g_debug ("transaction stop");
		break;

	case RPMCALLBACK_INST_STOP:
	case RPMCALLBACK_UNINST_STOP:

		/* phase complete */
		ret = hif_state_done (commit->state, &error_local);
		if (!ret) {
			g_warning ("state increment failed: %s",
				   error_local->message);
			g_error_free (error_local);
		}
		break;

	case RPMCALLBACK_UNPACK_ERROR:
	case RPMCALLBACK_CPIO_ERROR:
	case RPMCALLBACK_SCRIPT_ERROR:
	case RPMCALLBACK_SCRIPT_START:
	case RPMCALLBACK_SCRIPT_STOP:
	case RPMCALLBACK_UNKNOWN:
	case RPMCALLBACK_REPACKAGE_PROGRESS:
	case RPMCALLBACK_REPACKAGE_START:
	case RPMCALLBACK_REPACKAGE_STOP:
		g_debug ("%s uninteresting",
			 hif_commit_rpmcb_type_to_string (what));
		break;
	default:
		g_warning ("unknown transaction phase: %u (%s)",
			   what,
			   hif_commit_rpmcb_type_to_string (what));
		break;
	}

	g_free (package_id);
	return rc;
}

/**
 * hif_rpm_verbosity_string_to_value:
 **/
static gint
hif_rpm_verbosity_string_to_value (const gchar *value)
{
	if (g_strcmp0 (value, "critical") == 0)
		return RPMLOG_CRIT;
	if (g_strcmp0 (value, "emergency") == 0)
		return RPMLOG_EMERG;
	if (g_strcmp0 (value, "error") == 0)
		return RPMLOG_ERR;
	if (g_strcmp0 (value, "warn") == 0)
		return RPMLOG_WARNING;
	if (g_strcmp0 (value, "debug") == 0)
		return RPMLOG_DEBUG;
	if (g_strcmp0 (value, "info") == 0)
		return RPMLOG_INFO;
	return RPMLOG_EMERG;
}

/**
 * hif_transaction_delete_packages:
 **/
static gboolean
hif_transaction_delete_packages (GPtrArray *install,
				 HifState *state,
				 GError **error)
{
	const gchar *filename;
	gchar *cachedir = NULL;
	GFile *file;
	guint i;
	guint ret = TRUE;
	HifState *state_local;
	HyPackage pkg;

	/* nothing to delete? */
	if (install->len == 0)
		goto out;

	/* get the cachedir so we only delete packages in the actual
	 * cache, not local-install packages */
	cachedir = hif_config_get_string (priv->config, "CacheDir", NULL);
	if (cachedir == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
				     "Failed to get value for CacheDir");
		goto out;
	}

	/* delete each downloaded file */
	state_local = hif_state_get_child (state);
	hif_state_set_number_steps (state_local, install->len);
	for (i = 0; i < install->len; i++) {
		pkg = g_ptr_array_index (install, i);

		/* don't delete files not in the repo */
		filename = hif_package_get_filename (pkg);
		if (g_str_has_prefix (filename, cachedir)) {
			file = g_file_new_for_path (filename);
			ret = g_file_delete (file, NULL, error);
			if (!ret)
				goto out;
		}

		/* done */
		ret = hif_state_done (state_local, error);
		if (!ret)
			goto out;
	}
out:
	g_free (cachedir);
	return ret;
}

/**
 * pk_hy_convert_to_system_repo:
 **/
static HyPackage
pk_hy_convert_to_system_repo (PkBackendJob *job, HyPackage pkg, HifState *state, GError **error)
{
	HyPackageList pkglist = NULL;
	HyPackage pkg_installed = NULL;
	HyQuery query = NULL;
	HySack sack = NULL;

	/* get local packages */
	sack = hif_utils_create_sack_for_filters (job, 0, state, error);
	if (sack == NULL)
		goto out;

	/* find exact package */
	query = hy_query_create (sack);
	hy_query_filter (query, HY_PKG_NAME, HY_EQ, hy_package_get_name (pkg));
	hy_query_filter (query, HY_PKG_EVR, HY_EQ, hy_package_get_evr (pkg));
	hy_query_filter (query, HY_PKG_ARCH, HY_EQ, hy_package_get_arch (pkg));
	hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	pkglist = hy_query_run (query);
	if (hy_packagelist_count (pkglist) != 1) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
			     "Failed to find installed version of %s [%i]",
			     hy_package_get_name (pkg),
			     hy_packagelist_count (pkglist));
		goto out;
	}

	/* success */
	pkg_installed = hy_packagelist_get (pkglist, 0);
out:
	if (query != NULL)
		hy_query_free (query);
	if (pkglist != NULL)
		hy_packagelist_free (pkglist);
	return pkg_installed;
}

/**
 * hif_transaction_write_yumdb_install_item:
 **/
static gboolean
hif_transaction_write_yumdb_install_item (PkBackendJob *job,
					  HifTransactionCommit *commit,
					  HyPackage pkg,
					  HifState *state,
					  GError **error)
{
	const gchar *reason;
	gboolean ret;
	gchar *releasever = NULL;
	gchar *tmp;
	HifState *state_local;
	HyPackage pkg_installed;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	/* set steps */
	hif_state_set_number_steps (state, 5);

	/* need to find the HyPackage in the rpmdb, not the remote one that we
	 * just installed */
	state_local = hif_state_get_child (state);
	pkg_installed = pk_hy_convert_to_system_repo (job, pkg, state_local, error);
	if (pkg_installed == NULL) {
		ret = FALSE;
		goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* set the repo this came from */
	ret = hif_db_set_string (job_data->db,
				 pkg_installed,
				 "from_repo",
				 hy_package_get_reponame (pkg),
				 error);
	if (!ret)
		goto out;

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* write euid */
	tmp = g_strdup_printf ("%i", pk_backend_job_get_uid (job));
	ret = hif_db_set_string (job_data->db,
				 pkg_installed,
				 "installed_by",
				 tmp,
				 error);
	g_free (tmp);
	if (!ret)
		goto out;

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* set the correct reason */
	if (hif_package_get_user_action (pkg)) {
		reason = "user";
	} else {
		reason = "dep";
	}
	ret = hif_db_set_string (job_data->db,
				 pkg_installed,
				 "reason",
				 reason,
				 error);
	if (!ret)
		goto out;

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* set the correct release */
	releasever = hif_config_get_string (priv->config,
					    "releasever",
					     NULL);
	ret = hif_db_set_string (job_data->db,
				 pkg_installed,
				 "releasever",
				 releasever,
				 error);
	if (!ret)
		goto out;

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	g_free (releasever);
	return ret;
}

/**
 * hif_transaction_write_yumdb:
 **/
static gboolean
hif_transaction_write_yumdb (PkBackendJob *job,
			     HifTransactionCommit *commit,
			     HifState *state,
			     GError **error)
{
	gboolean ret;
	guint i;
	HifState *state_local;
	HifState *state_loop;
	HyPackage pkg;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	ret = hif_state_set_steps (state,
				   error,
				   50, /* remove */
				   50, /* add */
				   -1);
	if (!ret)
		goto out;

	/* remove all the old entries */
	state_local = hif_state_get_child (state);
	if (commit->remove->len > 0)
		hif_state_set_number_steps (state_local,
					    commit->remove->len);
	for (i = 0; i < commit->remove->len; i++) {
		pkg = g_ptr_array_index (commit->remove, i);
		ret = hif_db_remove_all (job_data->db,
					 pkg,
					 error);
		if (!ret)
			goto out;
		ret = hif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* add all the new entries */
	if (commit->install->len > 0)
		hif_state_set_number_steps (state_local,
					    commit->install->len);
	for (i = 0; i < commit->install->len; i++) {
		pkg = g_ptr_array_index (commit->install, i);
		state_loop = hif_state_get_child (state_local);
		ret = hif_transaction_write_yumdb_install_item (job,
								commit,
								pkg,
								state_loop,
								error);
		if (!ret)
			goto out;
		ret = hif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * pk_backend_transaction_commit:
 *
 * FIXME: move to hif-rpmts.c
 */
static gboolean
pk_backend_transaction_commit (PkBackendJob *job,
			       rpmts ts,
			       GPtrArray *sources,
			       gboolean allow_untrusted,
			       HyGoal goal,
			       HifState *state,
			       GError **error)
{
	const gchar *filename;
	gboolean keep_cache;
	gboolean ret = FALSE;
	gchar *verbosity_string = NULL;
	gint rc;
	gint verbosity;
	gint vs_flags;
	guint i;
	HifState *state_local;
	HifTransactionCommit *commit = NULL;
	HyPackage pkg;
	PkBitfield selector;
	rpmprobFilterFlags problems_filter = 0;

	/* take lock */
	ret = hif_state_take_lock (state,
				   HIF_LOCK_TYPE_RPMDB,
				   HIF_LOCK_MODE_PROCESS,
				   error);
	if (!ret)
		goto out;

	/* set state */
	ret = hif_state_set_steps (state,
				   error,
				   2, /* install */
				   2, /* remove */
				   10, /* test-commit */
				   83, /* commit */
				   1, /* write yumDB */
				   2, /* delete files */
				   -1);
	if (!ret)
		goto out;

	hif_state_action_start (state, PK_STATUS_ENUM_REQUEST, NULL);

	/* get verbosity from the config file */
	verbosity_string = hif_config_get_string (priv->config, "RpmVerbosity", NULL);
	verbosity = hif_rpm_verbosity_string_to_value (verbosity_string);
	rpmSetVerbosity (verbosity);

	/* setup the transaction */
	commit = g_new0 (HifTransactionCommit, 1);
	commit->timer = g_timer_new ();
	rc = rpmtsSetRootDir (ts, "/");
	if (rc < 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "failed to set root");
		goto out;
	}
	rpmtsSetNotifyCallback (ts,
				hif_commit_ts_progress_cb,
				commit);

	/* add things to install */
	state_local = hif_state_get_child (state);
	selector = pk_bitfield_from_enums (PK_INFO_ENUM_INSTALLING,
					   PK_INFO_ENUM_REINSTALLING,
					   PK_INFO_ENUM_DOWNGRADING,
					   PK_INFO_ENUM_UPDATING,
					   -1);
	commit->install = hif_goal_get_packages (goal, selector);
	if (commit->install->len > 0)
		hif_state_set_number_steps (state_local,
					    commit->install->len);
	for (i = 0; i < commit->install->len; i++) {

		pkg = g_ptr_array_index (commit->install, i);
		ret = hif_package_ensure_filename (sources, pkg, error);
		if (!ret)
			goto out;

		/* add the install */
		filename = hif_package_get_filename (pkg);
		ret = hif_rpmts_add_install_filename (ts,
						      filename,
						      allow_untrusted,
						      hif_goal_is_upgrade_package (goal, pkg),
						      error);
		if (!ret)
			goto out;

		/* this section done */
		ret = hif_state_done (state_local, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* add things to remove */
	selector = pk_bitfield_from_enums (PK_INFO_ENUM_REMOVING, -1);
	commit->remove = hif_goal_get_packages (goal, selector);
	for (i = 0; i < commit->remove->len; i++) {
		pkg = g_ptr_array_index (commit->remove, i);
		ret = hif_rpmts_add_remove_pkg (ts, pkg, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* generate ordering for the transaction */
	rpmtsOrder (ts);

	/* run the test transaction */
	if (hif_config_get_boolean (priv->config, "RpmCheckDebug", NULL)) {
		g_debug ("running test transaction");
		hif_state_action_start (state,
					PK_STATUS_ENUM_TEST_COMMIT,
					NULL);
		commit->state = hif_state_get_child (state);
		commit->step = HIF_TRANSACTION_STEP_IGNORE;
		/* the output value of rpmtsCheck is not meaningful */
		rpmtsCheck (ts);
		ret = hif_rpmts_look_for_problems (ts, error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* no signature checking, we've handled that already */
	vs_flags = rpmtsSetVSFlags (ts,
				    _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);
	rpmtsSetVSFlags (ts, vs_flags);

	/* filter diskspace */
	if (!hif_config_get_boolean (priv->config, "DiskSpaceCheck", NULL))
		problems_filter += RPMPROB_FILTER_DISKSPACE;

	/* run the transaction */
	commit->state = hif_state_get_child (state);
	commit->step = HIF_TRANSACTION_STEP_STARTED;
	rpmtsSetFlags (ts, RPMTRANS_FLAG_NONE);
	g_debug ("Running actual transaction");
	rc = rpmtsRun (ts, NULL, problems_filter);
	if (rc < 0) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Error %i running transaction", rc);
		goto out;
	}
	if (rc > 0) {
		ret = hif_rpmts_look_for_problems (ts, error);
		if (!ret)
			goto out;
	}

	/* hmm, nothing was done... */
	if (commit->step != HIF_TRANSACTION_STEP_WRITING) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Transaction did not go to writing phase, "
			     "but returned no error (%i)",
			     commit->step);
		goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* write to the yumDB */
	state_local = hif_state_get_child (state);
	ret = hif_transaction_write_yumdb (job,
					   commit,
					   state_local,
					   error);
	if (!ret)
		goto out;

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* remove the files we downloaded */
	keep_cache = hif_config_get_boolean (priv->config, "KeepCache", NULL);
	if (!keep_cache) {
		state_local = hif_state_get_child (state);
		ret = hif_transaction_delete_packages (commit->install,
						       state_local,
						       error);
		if (!ret)
			goto out;
	}

	/* this section done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* success */
	g_debug ("Done!");
out:
	g_free (verbosity_string);
	if (commit != NULL) {
		g_timer_destroy (commit->timer);
		if (commit->install != NULL)
			g_ptr_array_unref (commit->install);
		if (commit->remove != NULL)
			g_ptr_array_unref (commit->remove);
		g_free (commit);
	}
	return ret;
}

/**
 * pk_backend_transaction_run:
 */
static gboolean
pk_backend_transaction_run (PkBackendJob *job,
			    PkBitfield transaction_flags,
			    HyGoal goal,
			    HifState *state,
			    GError **error)
{
	gboolean ret = TRUE;
	GPtrArray *pkg_untrusted_repos = NULL;
	HifState *state_local;
	HyPackageList pkglist;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	/* set state */
	if (pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		hif_state_set_number_steps (state, 1);
	} else if (pk_bitfield_contain (transaction_flags,
					PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		ret = hif_state_set_steps (state, error,
					   50, /* depsolve */
					   50, /* download */
					   -1);
	} else {
		ret = hif_state_set_steps (state, error,
					   50, /* depsolve */
					   25, /* download */
					   25, /* install/remove */
					   -1);
	}
	if (!ret)
		goto out;

	/* depsolve */
	ret = hif_goal_depsolve (goal, error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* set the list of repos */
	ret = pk_backend_ensure_enabled_sources (job_data, error);
	if (!ret)
		goto out;

	/* simulate */
	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {

		/* mark any explicitly-untrusted packages so that the
		 * transaction skips straight to only_trusted=FALSE after
		 * simulate */
		pkg_untrusted_repos = pk_backend_transaction_check_untrusted_repos (job_data->enabled_sources, goal, error);
		if (pkg_untrusted_repos == NULL) {
			ret = FALSE;
			goto out;
		}
		hif_emit_package_array (job,
					PK_INFO_ENUM_UNTRUSTED,
					pkg_untrusted_repos);

		/* emit what we're going to do */
		pkglist = hy_goal_list_erasures (goal);
		hif_emit_package_list (job, PK_INFO_ENUM_REMOVING, pkglist);
		pkglist = hy_goal_list_installs (goal);
		hif_emit_package_list (job, PK_INFO_ENUM_INSTALLING, pkglist);
		pkglist = hy_goal_list_obsoleted (goal);
		hif_emit_package_list (job, PK_INFO_ENUM_OBSOLETING, pkglist);
		pkglist = hy_goal_list_reinstalls (goal);
		hif_emit_package_list (job, PK_INFO_ENUM_REINSTALLING, pkglist);
		pkglist = hy_goal_list_upgrades (goal);
		hif_emit_package_list (job, PK_INFO_ENUM_UPDATING, pkglist);
		pkglist = hy_goal_list_downgrades (goal);
		hif_emit_package_list (job, PK_INFO_ENUM_DOWNGRADING, pkglist);
		goto out;
	}

	/* download */
	state_local = hif_state_get_child (state);
	ret = pk_backend_transaction_download (job_data->enabled_sources,
					       goal,
					       state_local,
					       error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* only-download */
	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
		goto out;

	/* import all GPG keys */
	ret = hif_keyring_add_public_keys (job_data->keyring, error);
	if (!ret)
		goto out;

	/* find any packages without valid GPG signatures */
	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
		ret = pk_backend_transaction_check_untrusted (job_data->keyring,
							      job_data->enabled_sources,
							      goal,
							      error);
		if (!ret)
			goto out;
	}

	/* run transaction */
	state_local = hif_state_get_child (state);
	ret = pk_backend_transaction_commit (job,
					     job_data->ts,
					     job_data->enabled_sources,
					     !pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED),
					     goal,
					     state_local,
					     error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	if (pkg_untrusted_repos != NULL)
		g_ptr_array_unref (pkg_untrusted_repos);
	return ret;
}

/**
 * hif_is_installed_package_name:
 */
static gboolean
hif_is_installed_package_name (HySack sack, const gchar *name)
{
	gboolean ret;
	HyPackageList pkglist = NULL;
	HyQuery query = NULL;

	/* run query */
	query = hy_query_create (sack);
	hy_query_filter (query, HY_PKG_NAME, HY_EQ, name);
	hy_query_filter (query, HY_PKG_REPONAME, HY_EQ, HY_SYSTEM_REPO_NAME);
	pkglist = hy_query_run (query);

	/* any matches? */
	ret = hy_packagelist_count (pkglist) > 0;

	hy_packagelist_free (pkglist);
	hy_query_free (query);
	return ret;
}

/**
 * hif_is_installed_package_id:
 */
static gboolean
hif_is_installed_package_id (HySack sack, const gchar *package_id)
{
	gboolean ret;
	gchar **split;
	split = pk_package_id_split (package_id);
	ret = hif_is_installed_package_name (sack, split[PK_PACKAGE_ID_NAME]);
	g_strfreev (split);
	return ret;
}

/**
 * pk_backend_remove_packages:
 *
 * FIXME: Use autoremove
 * FIXME: Use allow_deps
 */
void
pk_backend_remove_packages (PkBackend *backend,
			    PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash = NULL;
	guint i;
	HyGoal goal = NULL;
	HyPackage pkg;
	HySack sack = NULL;
	PkBitfield filters;
	HifState *state_local;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   25, /* check installed */
				   12, /* find packages */
				   13, /* run transaction */
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
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		ret = hif_is_installed_package_id (sack, package_ids[i]);
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
	goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		hif_package_set_user_action (pkg, TRUE);
		hy_goal_erase (goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, transaction_flags, goal, state_local, &error);
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
	if (goal != NULL)
		hy_goal_free (goal);
	if (hash != NULL)
		g_hash_table_unref (hash);
	if (sack != NULL)
		hy_sack_free (sack);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend,
			     PkBackendJob *job,
			     PkBitfield transaction_flags,
			     gchar **package_ids)
{
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash = NULL;
	guint i;
	HifState *state_local;
	HyGoal goal = NULL;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   25, /* check installed */
				   12, /* find packages */
				   13, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		ret = hif_is_installed_package_id (sack, package_ids[i]);
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
	goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		hif_package_set_user_action (pkg, TRUE);
		hy_goal_install (goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, transaction_flags, goal, state_local, &error);
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
	if (goal != NULL)
		hy_goal_free (goal);
	if (hash != NULL)
		g_hash_table_unref (hash);
	if (sack != NULL)
		hy_sack_free (sack);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield transaction_flags,
			  gchar **full_paths)
{
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash = NULL;
	GPtrArray *array = NULL;
	guint i;
	HifState *state_local;
	HyGoal goal = NULL;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

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
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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

		/* keep for later */
		g_ptr_array_add (array, pkg);
		ret = hif_is_installed_package_name (sack, hy_package_get_name (pkg));
		if (ret) {
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
						   "%s is aleady installed",
						   hy_package_get_name (pkg));
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

	/* install packages */
	goal = hy_goal_create (sack);
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		hy_goal_install (goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, transaction_flags, goal, state_local, &error);
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
	if (goal != NULL)
		hy_goal_free (goal);
	if (hash != NULL)
		g_hash_table_unref (hash);
	if (sack != NULL)
		hy_sack_free (sack);
	pk_backend_job_finished (job);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend,
			    PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids)
{
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash = NULL;
	guint i;
	HifState *state_local;
	HyGoal goal = NULL;
	HyPackage pkg;
	HySack sack = NULL;
	PkBackendHifJobData *job_data = pk_backend_job_get_user_data (job);
	PkBitfield filters;

	/* set state */
	ret = hif_state_set_steps (job_data->state, NULL,
				   50, /* add repos */
				   25, /* check installed */
				   12, /* find packages */
				   13, /* run transaction */
				   -1);
	g_assert (ret);

	/* get sack */
	filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		ret = hif_is_installed_package_id (sack, package_ids[i]);
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

	/* install packages */
	goal = hy_goal_create (sack);
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = g_hash_table_lookup (hash, package_ids[i]);
		hif_package_set_user_action (pkg, TRUE);
		hy_goal_upgrade_to (goal, pkg);
	}

	/* run transaction */
	state_local = hif_state_get_child (job_data->state);
	ret = pk_backend_transaction_run (job, transaction_flags, goal, state_local, &error);
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
	if (goal != NULL)
		hy_goal_free (goal);
	if (hash != NULL)
		g_hash_table_unref (hash);
	if (sack != NULL)
		hy_sack_free (sack);
	pk_backend_job_finished (job);
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
				   50, /* add repos */
				   25, /* find packages */
				   25, /* emit files */
				   -1);
	g_assert (ret);

	/* get sack */
	g_variant_get (params, "(^a&s)", &package_ids);
	filters = hif_get_filter_for_ids (package_ids);
	state_local = hif_state_get_child (job_data->state);
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		g_assert (pkg != NULL);

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
	if (sack != NULL)
		hy_sack_free (sack);
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
	sack = hif_utils_create_sack_for_filters (job, filters, state_local, &error);
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
		g_assert (pkg != NULL);
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
	if (sack != NULL)
		hy_sack_free (sack);
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

#if 0

/**
 * pk_backend_get_categories:
 */
void
pk_backend_get_categories (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_job_finished (job);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield filters,
			  gchar **values)
{
	pk_backend_job_finished (job);
}

#endif
