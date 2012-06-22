/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Grzegorz Dąbrowski grzegorz.dabrowski@gmail.com>
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
#include <pk-backend.h>

#include <sqlite3.h>
#include <libbox/libbox-db.h>
#include <libbox/libbox-db-utils.h>
#include <libbox/libbox-db-repos.h>
#include <libbox/libbox-repos.h>
#include <libbox/libbox.h>

#define ROOT_DIRECTORY "/"

enum PkgSearchType {
	SEARCH_TYPE_NAME = 0,
	SEARCH_TYPE_DETAILS = 1,
	SEARCH_TYPE_FILE = 2,
	SEARCH_TYPE_RESOLVE = 3
};

enum DepsType {
	DEPS_TYPE_DEPENDS = 0,
	DEPS_TYPE_REQUIRES = 1
};

enum DepsBehaviour {
	DEPS_ALLOW = 0,
	DEPS_NO_ALLOW = 1
};

static sqlite3*
db_open()
{
	sqlite3 *db;

	db = box_db_open(ROOT_DIRECTORY);
	box_db_attach_repos(db, ROOT_DIRECTORY);
	box_db_repos_init(db);

	return db;
}

static void
db_close(sqlite3 *db)
{
	box_db_detach_repos(db);
	box_db_close(db);
}

static void
common_progress(int value, gpointer user_data)
{
	PkBackend* backend = (PkBackend *) user_data;
	pk_backend_set_percentage (backend, value);
}

static void
add_packages_from_list (PkBackend *backend, GList *list, gboolean updates)
{
	PackageSearch *package = NULL;
	GList *li = NULL;
	gchar *pkg_string = NULL;
	PkInfoEnum info;

	for (li = list; li != NULL; li = li->next) {
		package = (PackageSearch*)li->data;
		pkg_string = pk_package_id_build (package->package, package->version, package->arch, package->reponame);
		if (updates == TRUE)
			info = PK_INFO_ENUM_NORMAL;
		else if (package->installed)
			info = PK_INFO_ENUM_INSTALLED;
		else
			info = PK_INFO_ENUM_AVAILABLE;
		pk_backend_package (backend, info, pkg_string, package->description);
		g_free (pkg_string);
	}
}

static void
backend_find_packages_thread (PkBackend *backend, gpointer user_data)
{
	PkBitfield filters;
	const gchar *search;
	gchar **values;
	guint mode;
	GList *list = NULL;
	sqlite3 *db = NULL;
	gint filter_box = 0;

	filters = pk_backend_get_uint (backend, "filters");
	mode = pk_backend_get_uint (backend, "mode");
	values = pk_backend_get_strv (backend, "search");
	/* FIXME: support multiple packages */
	search = values[0];

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		filter_box = filter_box | PKG_INSTALLED;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		filter_box = filter_box | PKG_AVAILABLE;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
		filter_box = filter_box | PKG_DEVEL;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		filter_box = filter_box | PKG_NON_DEVEL;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
		filter_box = filter_box | PKG_GUI;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
		filter_box = filter_box | PKG_TEXT;
	}
	if (mode == SEARCH_TYPE_DETAILS) {
		filter_box = filter_box | PKG_SEARCH_DETAILS;
	}

	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	db = db_open ();

	if (mode == SEARCH_TYPE_FILE) {
		list = box_db_repos_search_file_with_filter (db, search, filter_box);
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	} else if (mode == SEARCH_TYPE_RESOLVE) {
		list = box_db_repos_packages_search_one (db, (gchar *)search);
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	} else {
		if ((pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
		     pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) ||
		    (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
		     !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))) {
			list = box_db_repos_packages_search_all (db, (gchar *)search, filter_box);
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			list = box_db_repos_packages_search_installed (db, (gchar *)search, filter_box);
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			list = box_db_repos_packages_search_available (db, (gchar *)search, filter_box);
		}
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	}

	db_close (db);
	pk_backend_finished (backend);
}

static void
backend_get_packages_thread (PkBackend *backend, gpointer user_data)
{
	PkBitfield filters;
	GList *list = NULL;
	sqlite3 *db = NULL;

	filters = pk_backend_get_uint (backend, "filters");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	db = db_open();

	if ((pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
	     pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) ||
	    (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
	     !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))) {
		list = box_db_repos_packages_search_all (db, NULL, 0);
	} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		list = box_db_repos_packages_search_installed (db, NULL, 0);
	} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		list = box_db_repos_packages_search_available (db, NULL, 0);
	}

	add_packages_from_list (backend, list, FALSE);
	box_db_repos_package_list_free (list);

	db_close(db);
	pk_backend_finished (backend);
}

static void
backend_get_updates_thread (PkBackend *backend, gpointer user_data)
{
	GList *list = NULL;
	sqlite3 *db = NULL;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	db = db_open ();

	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (backend, list, TRUE);
	box_db_repos_package_list_free (list);

	db_close (db);
	pk_backend_finished (backend);
}

static void
backend_update_system_thread (PkBackend *backend, gpointer user_data)
{
	/* FIXME: support only_trusted */

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	box_upgrade_dist(ROOT_DIRECTORY, common_progress, backend);
	pk_backend_finished (backend);
}

static void
backend_install_packages_thread (PkBackend *backend, gpointer user_data)
{
	gboolean result = TRUE;
	gchar **package_ids;
	size_t i;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* FIXME: support only_trusted */

	package_ids = pk_backend_get_strv (backend, "package_ids");
        for (i = 0; i < g_strv_length (package_ids); i++) {
		gchar **package_id_data = pk_package_id_split (package_ids[i]);
		result = box_package_install (package_id_data[PK_PACKAGE_ID_NAME], ROOT_DIRECTORY, common_progress, backend, FALSE);
        }

	pk_backend_finished (backend);

	return result;
}

static void
backend_update_packages_thread (PkBackend *backend, gpointer user_data)
{
	gboolean result = TRUE;
	gchar **package_ids;
	size_t i;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	/* FIXME: support only_trusted */
	package_ids = pk_backend_get_strv (backend, "package_ids");

	for (i = 0; i < g_strv_length (package_ids); i++)
	{
		result |= box_package_install (package_ids[i], ROOT_DIRECTORY, common_progress, backend, FALSE);
	}

	pk_backend_finished (backend);
	return result;
}

static void
backend_install_files_thread (PkBackend *backend, gpointer user_data)
{
	gboolean result;
	gchar **full_paths;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	full_paths = pk_backend_get_strv (backend, "full_paths");
	result = box_package_install (full_paths[0], ROOT_DIRECTORY, common_progress, backend, FALSE);

	pk_backend_finished (backend);

	return result;
}

static void
backend_get_details_thread (PkBackend *backend, gpointer user_data)
{
	PackageSearch *ps;
	GList *list;
	sqlite3 *db;
	gchar **package_ids;
	gchar **package_id_data;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	/* FIXME: support multiple packages */
	package_id_data = pk_package_id_split (package_ids[0]);

	db = db_open ();

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* only one element is returned */
	list = box_db_repos_packages_search_by_data (db, package_id_data[PK_PACKAGE_ID_NAME], package_id_data[PK_PACKAGE_ID_VERSION]);

	if (list == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "cannot find package by id");
		db_close (db);
		return;
	}
	ps = (PackageSearch*) list->data;

	pk_backend_details (backend, package_ids[0], "unknown", PK_GROUP_ENUM_OTHER, ps->description, "", 0);

	box_db_repos_package_list_free (list);

	db_close (db);
	pk_backend_finished (backend);
}

static void
backend_get_files_thread (PkBackend *backend, gpointer user_data)
{
	gchar *files;
	sqlite3 *db;
	gchar **package_ids;
	gchar **package_id_data;

	db = db_open();
	package_ids = pk_backend_get_strv (backend, "package_ids");
	/* FIXME: support multiple packages */
	package_id_data = pk_package_id_split (package_ids[0]);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	files = box_db_repos_get_files_string (db, package_id_data[PK_PACKAGE_ID_NAME], package_id_data[PK_PACKAGE_ID_VERSION]);
	pk_backend_files (backend, package_ids[0], files);

	db_close (db);
	g_free (files);

	pk_backend_finished (backend);
}

static void
backend_get_depends_requires_thread (PkBackend *backend, gpointer user_data)
{
	GList *list = NULL;
	sqlite3 *db;
	gchar **package_ids;
	int deps_type;
        gchar **package_id_data;

	db = db_open ();
	package_ids = pk_backend_get_strv (backend, "package_ids");
	deps_type = pk_backend_get_uint (backend, "type");
	/* FIXME: support multiple packages */
	package_id_data = pk_package_id_split (package_ids[0]);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (deps_type == DEPS_TYPE_DEPENDS)
		list = box_db_repos_get_depends (db, package_id_data[PK_PACKAGE_ID_NAME]);
	else if (deps_type == DEPS_TYPE_REQUIRES)
		list = box_db_repos_get_requires (db, package_id_data[PK_PACKAGE_ID_NAME]);

	add_packages_from_list (backend, list, FALSE);
	box_db_repos_package_list_free (list);

	db_close (db);

	pk_backend_finished (backend);
}

static void
backend_remove_packages_thread (PkBackend *backend, gpointer user_data)
{
	gchar **package_ids;
	gchar **package_id_data;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	/* FIXME: support multiple packages */
	package_id_data = pk_package_id_split (package_ids[0]);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);

	if (!box_package_uninstall (package_id_data[PK_PACKAGE_ID_NAME], ROOT_DIRECTORY, common_progress, backend, FALSE))
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Cannot uninstall");
	}

	pk_backend_finished (backend);
}

static void
backend_refresh_cache_thread (PkBackend *backend, gpointer user_data)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);

	box_repos_sync(ROOT_DIRECTORY, common_progress, backend);
	pk_backend_finished (backend);
}

/* ===================================================================== */

/**
 * pk_backend_initialize:
 */
static void
pk_backend_initialize (PkBackend *backend)
{
}

/**
 * pk_backend_destroy:
 */
static void
pk_backend_destroy (PkBackend *backend)
{
}

/**
 * pk_backend_get_filters:
 */
static PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
static gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
        return g_strdup ("application/x-box-package");
}

/**
 * pk_backend_get_depends:
 */
static void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_uint (backend, "type", DEPS_TYPE_DEPENDS);
	pk_backend_set_strv (backend, "package_ids", package_ids);
	/* TODO: param recursive */
	pk_backend_thread_create (backend, backend_get_depends_requires_thread, NULL, NULL);
}

/**
 * pk_backend_get_details:
 */
static void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_strv (backend, "package_ids", package_ids);
	pk_backend_thread_create (backend, backend_get_details_thread, NULL, NULL);
}

/**
 * pk_backend_get_files:
 */
static void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_strv (backend, "package_ids", package_ids);
	pk_backend_thread_create (backend, backend_get_files_thread, NULL, NULL);
}

/**
 * pk_backend_get_packages:
 */
static void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_uint (backend, "filters", filters);
	pk_backend_thread_create (backend, backend_get_packages_thread, NULL, NULL);
}

/**
 * pk_backend_get_requires:
 */
static void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_uint (backend, "type", DEPS_TYPE_REQUIRES);
	pk_backend_set_strv (backend, "package_ids", package_ids);
	/* TODO: param recursive */
	pk_backend_thread_create (backend, backend_get_depends_requires_thread, NULL, NULL);
}

/**
 * pk_backend_get_updates:
 */
static void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	/* TODO: filters */
	pk_backend_thread_create (backend, backend_get_updates_thread, NULL, NULL);
}

/**
 * pk_backend_install_packages:
 */
static void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_set_strv (backend, "package_ids", package_ids);

	pk_backend_thread_create (backend, backend_install_packages_thread, NULL, NULL);
}

/**
 * pk_backend_install_files:
 */
static void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	pk_backend_set_strv (backend, "full_paths", full_paths);
	pk_backend_thread_create (backend, backend_install_files_thread, NULL, NULL);
}

/**
 * pk_backend_refresh_cache:
 */
static void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}
	/* FIXME: support force */
	pk_backend_thread_create (backend, backend_refresh_cache_thread, NULL, NULL);
}

/**
 * pk_backend_remove_packages:
 */
static void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_set_uint (backend, "type", DEPS_ALLOW);
	pk_backend_set_strv (backend, "package_ids", package_ids);
	pk_backend_thread_create (backend, backend_remove_packages_thread, NULL, NULL);
}

/**
 * pk_backend_resolve:
 */
static void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_RESOLVE);
	pk_backend_set_strv (backend, "search", packages);
	pk_backend_thread_create (backend, backend_find_packages_thread, NULL, NULL);
}

/**
 * pk_backend_search_details:
 */
static void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_DETAILS);
	pk_backend_set_strv (backend, "search", values);
	pk_backend_thread_create (backend, backend_find_packages_thread, NULL, NULL);
}

/**
 * pk_backend_search_files:
 */
static void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_FILE);
	pk_backend_set_strv (backend, "search", values);
	pk_backend_thread_create (backend, backend_find_packages_thread, NULL, NULL);
}

/**
 * pk_backend_search_name:
 */
static void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_NAME);
	pk_backend_set_strv (backend, "search", values);
	pk_backend_thread_create (backend, backend_find_packages_thread, NULL, NULL);
}

/**
 * pk_backend_update_packages:
 */
static void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_thread_create (backend, backend_update_packages_thread, NULL, NULL);
}

/**
 * pk_backend_update_system:
 */
static void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_thread_create (backend, backend_update_system_thread, NULL, NULL);
}

/**
 * pk_backend_get_repo_list:
 */
static void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	GList *list;
	GList *li;
	RepoInfo *repo;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	list = box_repos_list_get ();
	for (li = list; li != NULL; li=li->next)
	{
		repo = (RepoInfo*) li->data;
		pk_backend_repo_detail (backend, repo->name, repo->description, repo->enabled);
	}
	box_repos_list_free (list);

	pk_backend_finished (backend);
}

/**
 * pk_backend_repo_enable:
 */
static void
pk_backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	box_repos_enable_repo(rid, enabled);

	pk_backend_finished (backend);
}

/**
 * pk_backend_repo_set_data:
 */
static void
pk_backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (!box_repos_set_param (rid, parameter, value)) {
		g_warning ("Cannot set PARAMETER '%s' TO '%s' for REPO '%s'", parameter, value, rid);
	}

	pk_backend_finished (backend);
}

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "Box";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Grzegorz Dąbrowski <grzegorz.dabrowski@gmail.com>";
}
