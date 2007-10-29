/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Grzegorz Dąbrowski <gdx@o2.pl>
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
#include <unistd.h>
#include <pk-debug.h>

#include <sqlite3.h>
#include <libbox/libbox-db.h>
#include <libbox/libbox-db-utils.h>
#include <libbox/libbox-db-repos.h>
#include <libbox/libbox-repos.h>

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

typedef struct {
	gchar *search;
	gchar *filter;
	gint mode;
} FindData;

typedef struct {
	gchar *package_id;
	gint type;
} ThreadData;


static sqlite3*
db_open()
{
	sqlite3 *db;

	db = box_db_open("/");
	box_db_attach_repos(db, "/");
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
add_packages_from_list (PkBackend *backend, GList *list, gboolean updates)
{
	PackageSearch *package = NULL;
	GList *li = NULL;
	gchar *pkg_string = NULL;
	PkInfoEnum info;

	for (li = list; li != NULL; li = li->next) {
		package = (PackageSearch*)li->data;
		pkg_string = pk_package_id_build(package->package, package->version, package->arch, package->reponame);
		if (updates == TRUE)
			info = PK_INFO_ENUM_NORMAL;
		else if (package->installed)
			info = PK_INFO_ENUM_INSTALLED;
		else
			info = PK_INFO_ENUM_AVAILABLE;
		pk_backend_package (backend, info, pkg_string, package->description);

		g_free(pkg_string);
	}
}

/* TODO: rewrite and share this code */
static void
parse_filter (const gchar *filter, gboolean *installed, gboolean *available,
	      gboolean *devel, gboolean *nondevel, gboolean *gui, gboolean *text)
{
	gchar **sections = NULL;
	gint i = 0;

	*installed = TRUE;
	*available = TRUE;
	*devel = TRUE;
	*nondevel = TRUE;
	*gui = TRUE;
	*text = TRUE;

	sections = g_strsplit (filter, ";", 0);
	while (sections[i]) {
		if (strcmp(sections[i], "installed") == 0)
			*available = FALSE;
		if (strcmp(sections[i], "~installed") == 0)
			*installed = FALSE;
		if (strcmp(sections[i], "devel") == 0)
			*nondevel = FALSE;
		if (strcmp(sections[i], "~devel") == 0)
			*devel = FALSE;
		if (strcmp(sections[i], "gui") == 0)
			*text = FALSE;
		if (strcmp(sections[i], "~gui") == 0)
			*gui = FALSE;
		i++;
	}
	g_strfreev (sections);
}

static void
find_packages_real (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	GList *list = NULL;
	sqlite3 *db = NULL;
	gint search_filter = 0;
	gboolean installed;
	gboolean available;
	gboolean devel;
	gboolean nondevel;
	gboolean gui;
	gboolean text;

	g_return_if_fail (backend != NULL);

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	parse_filter (filter, &installed, &available, &devel, &nondevel, &gui, &text);

	if (installed == TRUE) {
		search_filter = search_filter | PKG_INSTALLED;
	}
	if (available == TRUE) {
		search_filter = search_filter | PKG_AVAILABLE;
	}
	if (devel == TRUE) {
		search_filter = search_filter | PKG_DEVEL;
	}
	if (nondevel == TRUE) {
		search_filter = search_filter | PKG_NON_DEVEL;
	}
	if (gui == TRUE) {
		search_filter = search_filter | PKG_GUI;
	}
	if (text == TRUE) {
		search_filter = search_filter | PKG_TEXT;
	}
	if (mode == SEARCH_TYPE_DETAILS) {
		search_filter = search_filter | PKG_SEARCH_DETAILS;
	}

	pk_backend_no_percentage_updates (backend);

	db = db_open();

	if (mode == SEARCH_TYPE_FILE) {
		if (installed == FALSE && available == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, "invalid search mode");
		} else	{
			list = box_db_repos_search_file_with_filter (db, search, search_filter);
			add_packages_from_list (backend, list, FALSE);
			box_db_repos_package_list_free (list);
		}
	} else if (mode == SEARCH_TYPE_RESOLVE) {
		list = box_db_repos_packages_search_one (db, (gchar *)search);
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	} else {
		if (installed == FALSE && available == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, "invalid search mode");
		} else	{
			if (installed == TRUE && available == TRUE) {
				list = box_db_repos_packages_search_all(db, (gchar *)search, search_filter);
			} else if (installed == TRUE) {
				list = box_db_repos_packages_search_installed(db, (gchar *)search, search_filter);
			} else if (available == TRUE) {
				list = box_db_repos_packages_search_available(db, (gchar *)search, search_filter);
			}
			add_packages_from_list (backend, list, FALSE);
			box_db_repos_package_list_free (list);
		}
	}

	db_close(db);
}

static gboolean
backend_find_packages_thread (PkBackend *backend, gpointer data)
{
	FindData *d = (FindData*) data;

	g_return_val_if_fail (backend != NULL, FALSE);

	find_packages_real (backend, d->search, d->filter, d->mode);

	g_free(d->search);
	g_free(d->filter);
	g_free(d);

	return TRUE;
}


static void
find_packages (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	FindData *data = g_new0(FindData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
	} else {
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->mode = mode;
		pk_backend_thread_helper (backend, backend_find_packages_thread, data);
	}
}

static gboolean
backend_get_updates_thread (PkBackend *backend, gpointer data)
{
	GList *list = NULL;
	sqlite3 *db = NULL;

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	db = db_open ();

	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (backend, list, TRUE);
	box_db_repos_package_list_free (list);

	db_close (db);
	return TRUE;
}

static gboolean
backend_get_description_thread (PkBackend *backend, gpointer data)
{
	PkPackageId *pi;
	PackageSearch *ps;
	GList *list;
	ThreadData *d = (ThreadData*) data;
	sqlite3 *db;

	db = db_open();

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		db_close (db);
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	/* only one element is returned */
	list = box_db_repos_packages_search_by_data(db, pi->name, pi->version);

	if (list == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "cannot find package by id");
		pk_package_id_free (pi);
		db_close (db);
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}
	ps = (PackageSearch*) list->data;

	pk_backend_description (backend, pi->name, "unknown", PK_GROUP_ENUM_OTHER, ps->description, "", 0, NULL);

	pk_package_id_free (pi);
	box_db_repos_package_list_free (list);

	db_close(db);

	g_free (d->package_id);
	g_free (d);

	return TRUE;
}

static gboolean
backend_get_files_thread (PkBackend *backend, gpointer data)
{
	PkPackageId *pi;
	ThreadData *d = (ThreadData*) data;
	gchar *files;
	sqlite3 *db;

	db = db_open();

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		db_close (db);
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}

	files = box_db_repos_get_files_string (db, pi->name, pi->version);
        pk_backend_files (backend, d->package_id, files);

	pk_package_id_free (pi);

	db_close(db);

	g_free (files);
	g_free (d->package_id);
	g_free (d);

	return TRUE;
}

static gboolean
backend_get_depends_requires_thread (PkBackend *backend, gpointer data)
{
	PkPackageId *pi;
	GList *list = NULL;
	ThreadData *d = (ThreadData*) data;
	sqlite3 *db;

	db = db_open ();

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		db_close (db);
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);

	if (d->type == DEPS_TYPE_DEPENDS)
		list = box_db_repos_get_depends(db, pi->name);
	else if (d->type == DEPS_TYPE_REQUIRES)
		list = box_db_repos_get_requires(db, pi->name);

	add_packages_from_list (backend, list, FALSE);
	box_db_repos_package_list_free (list);
	pk_package_id_free (pi);

	db_close (db);

	g_free (d->package_id);
	g_free (d);

	return TRUE;
}

/* ===================================================================== */

/**
 * backend_initalize:
 */
static void
backend_initalize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
}

/**
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_GUI,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				      -1);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
	} else {
		data->package_id = g_strdup(package_id);
		data->type = DEPS_TYPE_DEPENDS;
		pk_backend_thread_helper (backend, backend_get_depends_requires_thread, data);
	}
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
	} else {
		data->package_id = g_strdup(package_id);
		pk_backend_thread_helper (backend, backend_get_description_thread, data);
	}
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
	} else {
		data->package_id = g_strdup(package_id);
		pk_backend_thread_helper (backend, backend_get_files_thread, data);
	}
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished (backend);
	} else {
		data->package_id = g_strdup(package_id);
		data->type = DEPS_TYPE_REQUIRES;
		pk_backend_thread_helper (backend, backend_get_depends_requires_thread, data);
	}
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_thread_helper (backend, backend_get_updates_thread, NULL);
}


/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_spawn_helper (backend, "install-package.sh", package_id, NULL);
}

/**
 * backend_install_file:
 */
static void
backend_install_file (PkBackend *backend, const gchar *file)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "install-file.sh", file, NULL);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_change_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_spawn_helper (backend, "refresh-cache.sh", NULL);
}

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	const gchar *deps;

	g_return_if_fail (backend != NULL);
	if (allow_deps == TRUE) {
		deps = "yes";
	} else {
		deps = "no";
	}
	pk_backend_spawn_helper (backend, "remove-package.sh", deps, package_id, NULL);
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, package, filter, SEARCH_TYPE_RESOLVE);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, SEARCH_TYPE_DETAILS);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, SEARCH_TYPE_FILE);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, SEARCH_TYPE_NAME);
}

/**
 * backend_update_package:
 */
static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_backend_network_is_online (backend) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_spawn_helper (backend, "update-package.sh", package_id, NULL);
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_spawn_helper (backend, "update-system.sh", NULL);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	GList *list;
	GList *li;
	RepoInfo *repo;

	g_return_if_fail (backend != NULL);


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
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
        g_return_if_fail (backend != NULL);
	
	box_repos_enable_repo(rid, enabled);

        pk_backend_finished (backend);
}

/**
 * backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (backend != NULL);

	if (!box_repos_set_param (rid, parameter, value))
	{
		pk_warning ("Cannot set PARAMETER '%s' TO '%s' for REPO '%s'", parameter, value, rid);
	}

	pk_backend_finished (backend);
}


PK_BACKEND_OPTIONS (
	"Box",					/* description */
	"0.0.1",				/* version */
	"Grzegorz Dąbrowski <gdx@o2.pl>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_files,			/* get_files */
	backend_get_requires,			/* get_requires */
	NULL,					/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	backend_install_file,			/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	backend_update_package,			/* update_package */
	backend_update_system,			/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data			/* repo_set_data */
);

