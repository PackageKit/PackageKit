/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi: set noexpandtab sts=8 sw=8:
 *
 * Copyright (C) 2007 OpenMoko, Inc
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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <pk-backend-thread.h>
#include <pk-debug.h>
#include <pk-package-ids.h>


#define OPKG_LIB
#include <libopkg.h>

static PkBackendThread *thread;

/* this is implemented in libopkg.a */
int opkg_upgrade_pkg(opkg_conf_t *conf, pkg_t *old);


enum filters {
	PKG_INSTALLED = 1,
	PKG_NOT_INSTALLED = 2,
	PKG_DEVEL = 4,
	PKG_NOT_DEVEL = 8,
	PKG_GUI = 16,
	PKG_NOT_GUI = 32
};

enum {
	SEARCH_NAME,
	SEARCH_DESCRIPTION,
	SEARCH_TAG
};

/* parameters passed to the search thread */
typedef struct {
	gint search_type;
	gchar *needle;
	gint filter;
} SearchParams;

/* global config structures */
static opkg_conf_t global_conf;
static args_t args;

/* Opkg message callback function */
extern opkg_message_callback opkg_cb_message;
static gchar *last_error;

/* Opkg progress callback function */
extern opkg_download_progress_callback opkg_cb_download_progress;

/* Opkg state changed callback function */
extern opkg_state_changed_callback opkg_cb_state_changed;


int
opkg_debug (opkg_conf_t *conf, message_level_t level, char *msg)
{
	PkBackend *backend;
	backend = pk_backend_thread_get_backend (thread);

	if (level == OPKG_NOTICE)
		pk_debug (msg);
	if (level == OPKG_ERROR)
		pk_warning (msg);

	/* print messages only if in verbose mode */
	if (level < OPKG_NOTICE && pk_debug_enabled ())
		printf ("OPKG: %s", msg);

	/* free the last error message and store the new one */
	if (level == OPKG_ERROR)
	{
		g_free (last_error);
		last_error = g_strdup (msg);
	}
	return 0;
}



void
pk_opkg_state_changed (opkg_state_t state, const char *data)
{
	PkBackend *backend;
	backend = pk_backend_thread_get_backend (thread);

	/* data is conveniently in pkgid format :-) */
	switch (state) {
	case OPKG_STATE_DOWNLOADING_PKG:
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING, data, NULL);
		break;
	case OPKG_STATE_INSTALLING_PKG:
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING, data, NULL);
		break;
	case OPKG_STATE_REMOVING_PKG:
		pk_backend_package (backend, PK_INFO_ENUM_REMOVING, data, NULL);
		break;
	case OPKG_STATE_UPGRADING_PKG:
		pk_backend_package (backend, PK_INFO_ENUM_UPDATING, data, NULL);
		break;
	default: return;
	}
}

static void
opkg_unknown_error (PkBackend *backend, gint error_code, gchar *failed_cmd)
{
	gchar *msg;

	msg = g_strdup_printf ("%s failed with error code %d. Last message was:\n\n%s", failed_cmd, error_code, last_error);
	pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, msg);

	g_free (msg);
}

/**
 * opkg_is_gui_pkg:
 *
 * check an opkg package for known GUI dependancies
 */
static gboolean
opkg_is_gui_pkg (pkg_t *pkg)
{
  gint i;

  for (i = 0; i < pkg->depends_count; i++)
  {
    if (g_strrstr (pkg->depends_str[i], "gtk"))
      return TRUE;
  }
  return FALSE;
}

/**
 * opkg_is_devel_pkg:
 *
 * check an opkg package to determine if it is a development package
 */
static gboolean
opkg_is_devel_pkg (pkg_t *pkg)
{
  if (g_strrstr (pkg->name, "-dev"))
      return TRUE;

  if (g_strrstr (pkg->name, "-dbg"))
      return TRUE;

  if (g_strrstr (pkg->section, "devel"))
      return TRUE;

  return FALSE;
}

/**
 * opkg_vec_find_latest:
 *
 * search a pkg_vec for the latest version of a package
 */

static pkg_t*
opkg_vec_find_latest_helper (pkg_vec_t *vec, pkg_t *pkg)
{
	gint i;
	for (i = 0; i < vec->len; i++)
	{
		/* if the version found is newer, return it */
		if (pkg_compare_versions (pkg, vec->pkgs[i]) > 0)
			return vec->pkgs[i];
	}
	/* return NULL if there is no package newer than pkg */
	return NULL;
}

static pkg_t*
opkg_vec_find_latest (pkg_vec_t *vec)
{
	gint i;
	pkg_t *tmp, *ret;

	if (vec->len < 1)
		return NULL;
	if (vec->len == 1)
		return vec->pkgs[0];

	ret = tmp = vec->pkgs[0];

	for (i = 0; i < vec->len; i++)
	{
		tmp = opkg_vec_find_latest_helper (vec, ret);
		if (!tmp)
			return ret;
		else
			ret = tmp;
	}
	return NULL;
}



/**
 * opkg_check_tag:
 * check a tag name and value on a package
 *
 * returns true if the tag is present
 */
gboolean
opkg_check_tag (pkg_t *pkg, gchar *tag)
{
	if (pkg->tags && tag)
		return (g_strrstr (pkg->tags, tag) != NULL);
	else
		return FALSE;
}

/**
 * parse_filter:
 */
static int
parse_filter (const gchar *filter)
{
	gchar **sections = NULL;
	gint i = 0;
	gint retval = 0;

	sections = g_strsplit (filter, ";", 0);
	while (sections[i]) {
		if (strcmp(sections[i], "installed") == 0)
			retval = retval | PKG_INSTALLED;
		if (strcmp(sections[i], "~installed") == 0)
			retval = retval | PKG_NOT_INSTALLED;
		if (strcmp(sections[i], "devel") == 0)
			retval = retval | PKG_DEVEL;
		if (strcmp(sections[i], "~devel") == 0)
			retval = retval | PKG_NOT_DEVEL;
		if (strcmp(sections[i], "gui") == 0)
			retval = retval | PKG_GUI;
		if (strcmp(sections[i], "~gui") == 0)
			retval = retval | PKG_NOT_GUI;
		i++;
	}
	g_strfreev (sections);

	return retval;
}



/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	int err;
	g_return_if_fail (backend != NULL);

	/* we use the thread helper */
	thread = pk_backend_thread_new ();


	/* Ipkg requires the PATH env variable to be set to find wget when
	 * downloading packages. PackageKit unsets all env variables as a
	 * security precaution, so we need to set PATH to something sensible
	 * here */
	setenv ("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);

	last_error = NULL;
	opkg_cb_message = opkg_debug;

	opkg_cb_state_changed = pk_opkg_state_changed;

	memset(&global_conf, 0 ,sizeof(global_conf));

	args_init (&args);

#ifdef OPKG_OFFLINE_ROOT
	args.offline_root = OPKG_OFFLINE_ROOT;
#endif

	err = opkg_conf_init (&global_conf, &args);
	if (err) {
		opkg_unknown_error (backend, err, "Initialization");
	}
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	g_object_unref (thread);

	/* this appears to (sometimes) be freed elsewhere, perhaps
	 * by the functions in libopkg.c */
	/* opkg_conf_deinit (&global_conf); */
	args_deinit (&args);
	g_free (last_error);
	last_error = NULL;
}


static gboolean
backend_get_description_thread (PkBackendThread *thread, gchar *package_id)
{
	pkg_t *pkg = NULL;
	PkPackageId *pi;
	PkBackend *backend;
	gboolean ret = TRUE;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);
	pi = pk_package_id_new_from_string (package_id);

	if (!pi->name || !pi->version)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		ret = FALSE;
		goto out;
	}

	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	if (!pkg)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		ret = FALSE;
		goto out;
	}

	pk_backend_description (backend, package_id,
	    "unknown", PK_GROUP_ENUM_OTHER, pkg->description, pkg->url, 0);

out:
	g_free (package_id);
	pk_backend_finished (backend);
	return ret;

}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_get_description_thread,
		g_strdup (package_id));
}

static void
pk_opkg_refresh_cache_progress_cb (int progress, char *url)
{
	PkBackend *backend;
	static gint sources_list_count = 0;
	static gint total_progress = 0;
	static char *old_url = NULL;

	/* this is a bit awkward, but basically in a package refresh there are
	 * multiple files to download but we only have a progress callback for
	 * each download. To create a combined total progress indication, we
	 * have to:
	 *
	 *  1. calculate the number of files we are downloading
	 *  2. notice when the file being downloaded changes, and increase the
	 *     total progress mark
	 *  3. report the progress percentage as a fraction of the current file
	 *     progress, plus the percentage of files we have already
	 *     downloaded
	 */

	/* calculate the number of files we are going to download */
	if (sources_list_count == 0)
	{
		pkg_src_list_elt_t *p;
		p = global_conf.pkg_src_list.head;
		while (p)
		{
			sources_list_count++;
			p = p->next;
		};
	}

	if (!old_url)
	{
		old_url = g_strdup (url);
	}

	/* increase the total progress mark if we are moving onto the next file */
	if (old_url && url && strcmp (old_url, url))
	{
		total_progress += 100 / sources_list_count;

		/* store the current url for comparison next time the progress callback
		 * is called */
		g_free (old_url);
		old_url = g_strdup (url);

	}

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	/* set the percentage as a fraction of the current progress plus the
	 * progress we have already recorded */
	if (total_progress  + (progress / sources_list_count) > 100)
		return;

	pk_backend_set_percentage (backend,
			total_progress + (progress / sources_list_count));

}

static gboolean
backend_refresh_cache_thread (PkBackendThread *thread, gpointer data)
{
	int ret;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	/* set the download progress callback */
	opkg_cb_download_progress = pk_opkg_refresh_cache_progress_cb;

	ret = opkg_lists_update (&args);
	if (ret) {
		opkg_unknown_error (backend, ret, "Refreshing cache");
	}
	pk_backend_finished (backend);

	/* unset the download progress callback */
	opkg_cb_download_progress = NULL;

	return (ret == 0);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_no_percentage_updates (backend);


	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_refresh_cache_thread,
		NULL);
}

/**
 * backend_search_name:
 */
static gboolean
backend_search_thread (PkBackendThread *thread, SearchParams *params)
{
	int i;
	pkg_vec_t *available;
	pkg_t *pkg;
	gchar *search;
	gint filter;
	PkBackend *backend;

	if (!params->needle)
	{
		g_free (params);
		return FALSE;
	}

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	search = params->needle;
	filter = params->filter;

	available = pkg_vec_alloc();
	pkg_hash_fetch_available (&global_conf.pkg_hash, available);
	for (i=0; i < available->len; i++) {
		char *uid;
		gint status;
		gchar *version;

		pkg = available->pkgs[i];

		if (params->search_type == SEARCH_NAME
				&& !g_strrstr (pkg->name, search))
			continue;

		else if (params->search_type == SEARCH_DESCRIPTION)
		{
			gchar *needle, *haystack;
			gboolean match;
			if (pkg->description && search)
			{
				needle = g_utf8_strdown (search, -1);
				haystack = g_utf8_strdown (pkg->description, -1);
				match = (g_strrstr (haystack, needle) != NULL);
				g_free (needle);
				g_free (haystack);
			}
			else
			{
				continue;
			}

			if (!match)
				continue;
		}

		else if (params->search_type == SEARCH_TAG
				&&
				(!pkg->tags || !g_strrstr (pkg->tags, search)))
			continue;

		if ((filter & PKG_DEVEL) && !opkg_is_devel_pkg (pkg))
			continue;
		if ((filter & PKG_NOT_DEVEL) && opkg_is_devel_pkg (pkg))
			continue;
		if ((filter & PKG_GUI) && !opkg_is_gui_pkg (pkg))
			continue;
		if ((filter & PKG_NOT_GUI) && opkg_is_gui_pkg (pkg))
			continue;
		if ((filter & PKG_INSTALLED) && (pkg->state_status == SS_NOT_INSTALLED))
			continue;
		if ((filter & PKG_NOT_INSTALLED) && (pkg->state_status != SS_NOT_INSTALLED))
			continue;

		version = pkg_version_str_alloc (pkg);
		uid = g_strdup_printf ("%s;%s;%s;",
			pkg->name, version, pkg->architecture);
		g_free (version);

		if (pkg->state_status == SS_INSTALLED)
			status = PK_INFO_ENUM_INSTALLED;
		else
			status = PK_INFO_ENUM_AVAILABLE;

		pk_backend_package (backend, status, uid,pkg->description);
	}

	pkg_vec_free(available);
	pk_backend_finished (backend);

	g_free (params->needle);
	g_free (params);
	return TRUE;
}

static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchParams *params;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	params = g_new0 (SearchParams, 1);
	params->filter = parse_filter (filter);
	params->search_type = SEARCH_NAME;
	params->needle = g_strdup (search);

	pk_backend_thread_create (thread, (PkBackendThreadFunc) backend_search_thread, params);
}

/**
 * backend_search_description:
 */
static void
backend_search_description (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchParams *params;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	params = g_new0 (SearchParams, 1);
	params->filter = parse_filter (filter);
	params->search_type = SEARCH_DESCRIPTION;
	params->needle = g_strdup (search);

	pk_backend_thread_create (thread, (PkBackendThreadFunc) backend_search_thread, params);
}

static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	SearchParams *params;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	params = g_new0 (SearchParams, 1);
	params->filter = parse_filter (filter);
	params->search_type = SEARCH_TAG;
	params->needle = g_strdup_printf ("group::%s", search);

	pk_backend_thread_create (thread, (PkBackendThreadFunc) backend_search_thread, params);
}




static void
pk_opkg_install_progress_cb (int percent, char* url)
{
	PkBackend *backend;

	/* get current backend and set percentage */
	backend = pk_backend_thread_get_backend (thread);
	pk_backend_set_percentage (backend, percent);
}

static gboolean
backend_install_package_thread (PkBackendThread *thread, gchar *package_id)
{
	PkPackageId *pi;
	gint err;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	/* set the download progress callback */
	opkg_cb_download_progress = pk_opkg_install_progress_cb;

	err = opkg_packages_install (&args, pi->name);
	if (err != 0)
		opkg_unknown_error (backend, err, "Install");

	/* unset the download progress callback */
	opkg_cb_download_progress = NULL;

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err == 0);
}

static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_no_percentage_updates (backend);
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_install_package_thread,
		g_strdup (package_id));
}

static gboolean
backend_remove_package_thread (PkBackendThread *thread, gpointer data[3])
{
	PkPackageId *pi;
	gint err;
	PkBackend *backend;
	gchar *package_id;
	gboolean allow_deps;
	gboolean autoremove;


	package_id = (gchar*) data[0];
	allow_deps = GPOINTER_TO_INT (data[1]);
	autoremove = GPOINTER_TO_INT (data[2]);
	g_free (data);


	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	args.autoremove = autoremove;
	args.force_removal_of_dependent_packages = allow_deps;

	err = opkg_packages_remove (&args, pi->name, 0);
	/* TODO: improve error reporting */
	if (err != 0)
		opkg_unknown_error (backend, err, "Remove");

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err == 0);
}

static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	gpointer *params;

	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_no_percentage_updates (backend);

	/* params is a small array we can pack our thread parameters into */
	params = g_new0 (gpointer, 2);

	params[0] = g_strdup (package_id);
	params[1] = GINT_TO_POINTER (allow_deps);
	params[2] = GINT_TO_POINTER (autoremove);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_remove_package_thread,
		params);

}

/**
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				      PK_FILTER_ENUM_GUI,
				      -1);
}


static gboolean
backend_update_system_thread (PkBackendThread *thread, gpointer data)
{
	gint err;
	err = opkg_packages_upgrade (&args);
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	if (err)
		opkg_unknown_error (backend, err, "Upgrading system");

	pk_backend_finished (backend);
	return (err != 0);
}

static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_update_system_thread,
		NULL);
}

/**
 * backend_get_depends:
 */

static gboolean
backend_get_depends_thread (PkBackendThread *thread, gchar *package_id)
{
	PkPackageId *pi;
	pkg_t *pkg = NULL;
	gint i;
	GRegex *regex;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	if (!pi->name || !pi->version)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	if (!pkg)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	/* compile a regex expression to parse depends_str package names */
	regex = g_regex_new ("(.+) \\(([>=<]+) (.+)\\)", G_REGEX_OPTIMIZE, 0, NULL);

	for (i = 0; i < pkg->depends_count; i++)
	{
		pkg_t *d_pkg = NULL;
		pkg_vec_t *p_vec;
		GMatchInfo *match_info = NULL;
		gchar *uid = NULL, *pkg_name = NULL, *pkg_v = NULL, *pkg_req = NULL;
		gint status;
		gchar *version;

		/* find the package by name and select the package with the
		 * latest version number
		 */

		if (!g_regex_match (regex, pkg->depends_str[i], 0, &match_info))
		{
			/* we couldn't parse the depends string */

			/* match_info is always allocated, even if the match
			 * failed */
			g_match_info_free (match_info);
			continue;
		}

		pkg_name = g_match_info_fetch (match_info, 1);
		pkg_req = g_match_info_fetch (match_info, 2);
		pkg_v = g_match_info_fetch (match_info, 3);
		g_match_info_free (match_info);

		p_vec = pkg_vec_fetch_by_name (&global_conf.pkg_hash, pkg_name);

		if (!p_vec || p_vec->len < 1 || !p_vec->pkgs[0])
			continue;

		d_pkg = opkg_vec_find_latest (p_vec);

		/* TODO: check the version requirements are satisfied */

		g_free (pkg_name);
		g_free (pkg_req);
		g_free (pkg_v);

		version = pkg_version_str_alloc (d_pkg);
		uid = g_strdup_printf ("%s;%s;%s;",
			d_pkg->name, version, d_pkg->architecture);
		g_free (version);

		if (d_pkg->state_status == SS_INSTALLED)
			status = PK_INFO_ENUM_INSTALLED;
		else
			status = PK_INFO_ENUM_AVAILABLE;
		pk_backend_package (backend, status, uid, d_pkg->description);
	}

	g_regex_unref (regex);
	pk_backend_finished (backend);
	g_free (package_id);

	return TRUE;
}

static void
backend_get_depends (PkBackend *backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	/* TODO: revursive is ignored */
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_get_depends_thread,
		g_strdup (package_id));
}

/**
 * backend_update_package:
 */
static gboolean
backend_update_package_thread (PkBackendThread *thread, gchar *package_id)
{
	PkPackageId *pi;
	pkg_t *pkg;
	gint err = 0;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	if (!pi->name || !pi->version)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	if (!pkg) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Packge not found");
		err = -1;
	} else {
		/* TODO: determine if package is already latest? */
		err = opkg_upgrade_pkg (&global_conf, pkg);
		if (err != 0)
			opkg_unknown_error (backend, err, "Update package");
	}

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err != 0);
}

static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_update_package_thread,
		/* TODO: process the entire list */
		g_strdup (package_ids[0]));
}

/**
 * backend_get_updates:
 */

static gboolean
backend_get_updates_thread (PkBackendThread *thread, gpointer data)
{
	pkg_vec_t *installed;
	gint i;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	installed = pkg_vec_alloc();

	pkg_hash_fetch_all_installed (&global_conf.pkg_hash, installed);

	for (i=0; i < installed->len; i++) {

		gchar *uid;
		pkg_t *pkg, *best_pkg;
		gint status;
		gchar *version;

		pkg = installed->pkgs[i];
		best_pkg = pkg_hash_fetch_best_installation_candidate_by_name (&global_conf, pkg->name);

		/* couldn't find an install candidate?! */
		if (!best_pkg)
			continue;

		/* check to see if the best candidate is actually newer */
		if (pkg_compare_versions (best_pkg, pkg) <= 0)
			continue;

		version = pkg_version_str_alloc (pkg);
		uid = g_strdup_printf ("%s;%s;%s;",
			pkg->name, version, pkg->architecture);
		g_free (version);

		if (pkg->state_status == SS_INSTALLED)
			status = PK_INFO_ENUM_INSTALLED;
		else
			status = PK_INFO_ENUM_AVAILABLE;

		pk_backend_package (backend, status, uid, pkg->description);
	}
	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_updates (PkBackend *backend, const gchar *filter)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_get_updates_thread,
		NULL);
}

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_COMMUNICATION,
				      PK_GROUP_ENUM_PROGRAMMING,
				      PK_GROUP_ENUM_GAMES,
				      PK_GROUP_ENUM_OTHER,
				      PK_GROUP_ENUM_INTERNET,
				      PK_GROUP_ENUM_REPOS,
				      PK_GROUP_ENUM_MAPS,
				      -1
			);
}


PK_BACKEND_OPTIONS (
	"opkg",					/* description */
	"Thomas Wood <thomas@openedhand.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	NULL,					/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	backend_search_description,		/* search_details */
	NULL,					/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	NULL,					/* get_repo_list */
	NULL,					/* repo_enable */
	NULL,					/* repo_set_data */
	NULL,					/* service_pack */
	NULL					/* what_provides */
);

