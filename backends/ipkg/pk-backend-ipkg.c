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
#include <pk-debug.h>


#define IPKG_LIB
#include <libipkg.h>

/* this is implemented in libipkg.a */
int ipkg_upgrade_pkg(ipkg_conf_t *conf, pkg_t *old);


enum filters {
	PKG_INSTALLED = 1,
	PKG_NOT_INSTALLED = 2,
	PKG_DEVEL = 4,
	PKG_NOT_DEVEL = 8,
	PKG_GUI = 16,
	PKG_NOT_GUI = 32
};

/* global config structures */
static ipkg_conf_t global_conf;
static args_t args;

/* Ipkg message callback function */
extern ipkg_message_callback ipkg_cb_message;
static gchar *last_error;

int
ipkg_debug (ipkg_conf_t *conf, message_level_t level, char *msg)
{
	if (level != 1)
		return 0;

	/* print messages only if in verbose mode */
	if (pk_debug_enabled ())
		printf ("IPKG <%d>: %s", level, msg);

	/* free the last error message and store the new one */
	g_free (last_error);
	last_error = g_strdup (msg);
	return 0;
}

static void
ipkg_unknown_error (PkBackend *backend, gint error_code, gchar *failed_cmd)
{
	gchar *msg;

	msg = g_strdup_printf ("%s failed with error code %d. Last message was:\n\n%s", failed_cmd, error_code, last_error);
	pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, msg);

	g_free (msg);
}

/**
 * ipkg_is_gui_pkg:
 *
 * check an ipkg package for known GUI dependancies
 */
static gboolean
ipkg_is_gui_pkg (pkg_t *pkg)
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
 * ipkg_is_devel_pkg:
 *
 * check an ipkg package to determine if it is a development package
 */
static gboolean
ipkg_is_devel_pkg (pkg_t *pkg)
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
 * ipkg_vec_find_latest:
 *
 * search a pkg_vec for the latest version of a package
 */

static pkg_t*
ipkg_vec_find_latest_helper (pkg_vec_t *vec, pkg_t *pkg)
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
ipkg_vec_find_latest (pkg_vec_t *vec)
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
		tmp = ipkg_vec_find_latest_helper (vec, ret);
		if (!tmp)
			return ret;
		else
			ret = tmp;
	}
	return NULL;
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
 * backend_initalize:
 */
static void
backend_initalize (PkBackend *backend)
{
	int err;
	g_return_if_fail (backend != NULL);

	/* Ipkg requires the PATH env variable to be set to find wget when
	 * downloading packages. PackageKit unsets all env variables as a
	 * security precaution, so we need to set PATH to something sensible
	 * here */
	setenv ("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);

	last_error = NULL;
	ipkg_cb_message = ipkg_debug;

	memset(&global_conf, 0 ,sizeof(global_conf));
	memset(&args, 0 ,sizeof(args));

	args_init (&args);

#ifdef IPKG_OFFLINE_ROOT
	args.offline_root = IPKG_OFFLINE_ROOT;
#endif

	err = ipkg_conf_init (&global_conf, &args);
	if (err) {
		ipkg_unknown_error (backend, err, "Initialization");
	}
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	/* this appears to (sometimes) be freed elsewhere ... */
	/* ipkg_conf_deinit (&global_conf); */
	args_deinit (&args);
	g_free (last_error);
}


static gboolean
backend_get_description_thread (PkBackend *backend, gchar *package_id)
{
	pkg_t *pkg;
	PkPackageId *pi;
	pi = pk_package_id_new_from_string (package_id);
	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	pk_backend_description (backend, pi->name,
	    "unknown", PK_GROUP_ENUM_OTHER, pkg->description, pkg->url, 0);

	g_free (package_id);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_thread_create (backend,
		(PkBackendThreadFunc) backend_get_description_thread,
		g_strdup (package_id));
}

static gboolean
backend_refresh_cache_thread (PkBackend *backend, gpointer data)
{
	int ret;

	ret = ipkg_lists_update (&args);
	if (ret) {
		ipkg_unknown_error (backend, ret, "Refreshing cache");
	}
	pk_backend_finished (backend);

	return (ret == 0);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_no_percentage_updates (backend);


	pk_backend_thread_create (backend,
		(PkBackendThreadFunc) backend_refresh_cache_thread,
		NULL);
}

/**
 * backend_search_name:
 */
static gboolean
backend_search_name_thread (PkBackend *backend, gchar *params[2])
{
	int i;
	pkg_vec_t *available;
	pkg_t *pkg;
	gchar *search;
	gint filter;

	search = params[0];
	filter = GPOINTER_TO_INT (params[1]);

	available = pkg_vec_alloc();
	pkg_hash_fetch_available (&global_conf.pkg_hash, available);
	for (i=0; i < available->len; i++) {
		char *uid;
		gint status;

		pkg = available->pkgs[i];
		if (!g_strrstr (pkg->name, search))
			continue;
		if ((filter & PKG_DEVEL) && !ipkg_is_devel_pkg (pkg))
			continue;
		if ((filter & PKG_NOT_DEVEL) && ipkg_is_devel_pkg (pkg))
			continue;
		if ((filter & PKG_GUI) && !ipkg_is_gui_pkg (pkg))
			continue;
		if ((filter & PKG_NOT_GUI) && ipkg_is_gui_pkg (pkg))
			continue;
		if ((filter & PKG_INSTALLED) && (pkg->state_status == SS_NOT_INSTALLED))
			continue;
		if ((filter & PKG_NOT_INSTALLED) && (pkg->state_status != SS_NOT_INSTALLED))
			continue;

		uid = g_strdup_printf ("%s;%s;%s;",
			pkg->name, pkg->version, pkg->architecture);

		if (pkg->state_status == SS_INSTALLED)
			status = PK_INFO_ENUM_INSTALLED;
		else
			status = PK_INFO_ENUM_AVAILABLE;

		pk_backend_package (backend, status, uid,pkg->description);
	}

	pkg_vec_free(available);
	pk_backend_finished (backend);

	g_free (params);
	g_free (search);
	return TRUE;
}

static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	gint filter_enum;
	gpointer *params;

	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	filter_enum = parse_filter (filter);

	/* params is a small array we can pack our thread parameters into */
	params = g_new0 (gpointer, 2);
	params[0] = g_strdup (search);
	params[1] = GINT_TO_POINTER (filter_enum);

	pk_backend_thread_create (backend,(PkBackendThreadFunc) backend_search_name_thread, params);
}

static gboolean
backend_install_package_thread (PkBackend *backend, gchar *package_id)
{
	PkPackageId *pi;
	gint err;

	pi = pk_package_id_new_from_string (package_id);

	err = ipkg_packages_install (&args, pi->name);
	if (err != 0)
		ipkg_unknown_error (backend, err, "Install");

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err == 0);
}

static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_INSTALL);
	pk_backend_no_percentage_updates (backend);
	pk_backend_thread_create (backend,
		(PkBackendThreadFunc) backend_install_package_thread,
		g_strdup (package_id));
}

static gboolean
backend_remove_package_thread (PkBackend *backend, gchar *package_id)
{
	PkPackageId *pi;
	gint err;

	pi = pk_package_id_new_from_string (package_id);

	err = ipkg_packages_remove (&args, pi->name, 0);
	/* TODO: improve error reporting */
	if (err != 0)
		ipkg_unknown_error (backend, err, "Install");

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err == 0);
}

static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	pk_backend_change_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_no_percentage_updates (backend);
	/* TODO: allow_deps is currently ignored */
	pk_backend_thread_create (backend,
		(PkBackendThreadFunc) backend_remove_package_thread,
		g_strdup (package_id));

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
backend_update_system_thread (PkBackend *backend, gpointer data)
{
	gint err;
	err = ipkg_packages_upgrade (&args);
	if (err)
		ipkg_unknown_error (backend, err, "Upgrading system");

	pk_backend_finished (backend);
	return (err != 0);
}

static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_change_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (backend,
		(PkBackendThreadFunc) backend_update_system_thread,
		NULL);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id, gboolean recursive)
{
	/* TODO: revursive is ignored */
	PkPackageId *pi;
	pkg_t *pkg = NULL;
	gint i;
	GRegex *regex;

	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	pi = pk_package_id_new_from_string (package_id);
	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	if (!pkg)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return;
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

		d_pkg = ipkg_vec_find_latest (p_vec);

		/* TODO: check the version requirements are satisfied */

		g_free (pkg_name);
		g_free (pkg_req);
		g_free (pkg_v);

		uid = g_strdup_printf ("%s;%s;%s;",
			d_pkg->name, d_pkg->version, d_pkg->architecture);
		if (d_pkg->state_status == SS_INSTALLED)
			status = PK_INFO_ENUM_INSTALLED;
		else
			status = PK_INFO_ENUM_AVAILABLE;
		pk_backend_package (backend, status, uid, d_pkg->description);
	}
	g_regex_unref (regex);
	pk_backend_finished (backend);
}

/**
 * backend_update_package:
 */
static gboolean
backend_update_package_thread (PkBackend *backend, gchar *package_id)
{
	PkPackageId *pi;
	pkg_t *pkg;
	gint err = 0;

	pi = pk_package_id_new_from_string (package_id);
	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	if (!pkg) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Packge not found");
		err = -1;
	} else {
		/* TODO: determine if package is already latest? */
		err = ipkg_upgrade_pkg (&global_conf, pkg);
		if (err != 0)
			ipkg_unknown_error (backend, err, "Update package");
	}

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err != 0);
}

static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (backend,
		(PkBackendThreadFunc) backend_update_package_thread,
		g_strdup (package_id));
}


PK_BACKEND_OPTIONS (
	"ipkg",					/* description */
	"Thomas Wood <thomas@openedhand.com>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	backend_install_package,		/* install_package */
	NULL,					/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	NULL,					/* search_details */
	NULL,					/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	backend_update_package,			/* update_package */
	backend_update_system,			/* update_system */
	NULL,					/* get_repo_list */
	NULL,					/* repo_enable */
	NULL					/* repo_set_data */
);

