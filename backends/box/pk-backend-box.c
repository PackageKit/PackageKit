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
#include <unistd.h>
#include <pk-backend.h>
#include <pk-backend-thread.h>
#include <pk-debug.h>
#include <pk-network.h>
#include <pk-filter.h>

#include <sqlite3.h>
#include <libbox/libbox-db.h>
#include <libbox/libbox-db-utils.h>
#include <libbox/libbox-db-repos.h>
#include <libbox/libbox-repos.h>
#include <libbox/libbox.h>

#define ROOT_DIRECTORY "/"

static PkBackendThread *thread;
static PkNetwork *network;

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

typedef struct {
	gchar *search;
	gchar *filter;
	gint mode;
} FindData;

typedef struct {
	gchar *package_id;
	gchar **package_ids;
	gint type;
} ThreadData;


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

static void
find_packages_real (PkBackend *backend, const gchar *search, const gchar *filter_text, gint mode)
{
	GList *list = NULL;
	sqlite3 *db = NULL;
	gint search_filter = 0;
	PkFilter *filter;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* parse */
	filter = pk_filter_new_from_string (filter_text);
	if (filter == NULL) {
		pk_error ("filter invalid, daemon broken");
	}

	if (filter->installed == TRUE) {
		search_filter = search_filter | PKG_INSTALLED;
	}
	if (filter->not_installed == TRUE) {
		search_filter = search_filter | PKG_AVAILABLE;
	}
	if (filter->devel == TRUE) {
		search_filter = search_filter | PKG_DEVEL;
	}
	if (filter->not_devel == TRUE) {
		search_filter = search_filter | PKG_NON_DEVEL;
	}
	if (filter->gui == TRUE) {
		search_filter = search_filter | PKG_GUI;
	}
	if (filter->not_gui == TRUE) {
		search_filter = search_filter | PKG_TEXT;
	}
	if (mode == SEARCH_TYPE_DETAILS) {
		search_filter = search_filter | PKG_SEARCH_DETAILS;
	}

	pk_backend_no_percentage_updates (backend);

	db = db_open();

	if (mode == SEARCH_TYPE_FILE) {
		if (filter->installed == FALSE && filter->not_installed == FALSE) {
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
		if (filter->installed == FALSE && filter->not_installed == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, "invalid search mode");
		} else	{
			if (filter->installed == TRUE && filter->not_installed == TRUE) {
				list = box_db_repos_packages_search_all(db, (gchar *)search, search_filter);
			} else if (filter->installed == TRUE) {
				list = box_db_repos_packages_search_installed(db, (gchar *)search, search_filter);
			} else if (filter->not_installed == TRUE) {
				list = box_db_repos_packages_search_available(db, (gchar *)search, search_filter);
			}
			add_packages_from_list (backend, list, FALSE);
			box_db_repos_package_list_free (list);
		}
	}

	pk_filter_free (filter);
	db_close(db);
}

static gboolean
backend_find_packages_thread (PkBackendThread *thread, gpointer data)
{
	FindData *d = (FindData*) data;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	find_packages_real (backend, d->search, d->filter, d->mode);

	g_free(d->search);
	g_free(d->filter);
	g_free(d);
	pk_backend_finished (backend);

	return TRUE;
}


static void
find_packages (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	FindData *data = g_new0(FindData, 1);

	g_return_if_fail (backend != NULL);

	data->search = g_strdup(search);
	data->filter = g_strdup(filter);
	data->mode = mode;
	pk_backend_thread_create (thread, backend_find_packages_thread, data);
}

static gboolean
backend_get_updates_thread (PkBackendThread *thread, gpointer data)
{
	GList *list = NULL;
	sqlite3 *db = NULL;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	db = db_open ();

	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (backend, list, TRUE);
	box_db_repos_package_list_free (list);

	db_close (db);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_update_system_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	box_upgrade_dist(ROOT_DIRECTORY, common_progress, backend);
	pk_backend_finished (backend);

	return TRUE;
}

static gboolean
backend_install_package_thread (PkBackendThread *thread, gpointer data)
{
	ThreadData *d = (ThreadData*) data;
	gboolean result;
	PkPackageId *pi;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		g_free (d->package_id);
		g_free (d);

		return FALSE;
	}
	result = box_package_install(pi->name, ROOT_DIRECTORY, common_progress, backend, FALSE);

	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

	return result;
}

static gboolean
backend_update_packages_thread (PkBackendThread *thread, gpointer data)
{
	ThreadData *d = (ThreadData*) data;
	gboolean result = TRUE;
	PkPackageId *pi;
	PkBackend *backend;
	gint i;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	for (i = 0; i < g_strv_length (d->package_ids); i++)
	{
		pi = pk_package_id_new_from_string (d->package_ids[i]);
		if (pi == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
			pk_package_id_free (pi);
			g_strfreev (d->package_ids);
			g_free (d);

			return FALSE;
		}
		result |= box_package_install(pi->name, ROOT_DIRECTORY, common_progress, backend, FALSE);

	}

	g_strfreev (d->package_ids);
	g_free (d);
	pk_backend_finished (backend);

	return result;
}
static gboolean
backend_install_file_thread (PkBackendThread *thread, gpointer data)
{
	ThreadData *d = (ThreadData*) data;
	gboolean result;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	result = box_package_install(d->package_id, ROOT_DIRECTORY, common_progress, backend, FALSE);

	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

	return result;
}

static gboolean
backend_get_description_thread (PkBackendThread *thread, gpointer data)
{
	PkPackageId *pi;
	PackageSearch *ps;
	GList *list;
	ThreadData *d = (ThreadData*) data;
	sqlite3 *db;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

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

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

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

	pk_backend_description (backend, d->package_id, "unknown", PK_GROUP_ENUM_OTHER, ps->description, "", 0);

	pk_package_id_free (pi);
	box_db_repos_package_list_free (list);

	db_close(db);

	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

	return TRUE;
}

static gboolean
backend_get_files_thread (PkBackendThread *thread, gpointer data)
{
	PkPackageId *pi;
	ThreadData *d = (ThreadData*) data;
	gchar *files;
	sqlite3 *db;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

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

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	files = box_db_repos_get_files_string (db, pi->name, pi->version);
        pk_backend_files (backend, d->package_id, files);

	pk_package_id_free (pi);

	db_close(db);

	g_free (files);
	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

	return TRUE;
}

static gboolean
backend_get_depends_requires_thread (PkBackendThread *thread, gpointer data)
{
	PkPackageId *pi;
	GList *list = NULL;
	ThreadData *d = (ThreadData*) data;
	sqlite3 *db;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	db = db_open ();

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		db_close (db);
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

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
	pk_backend_finished (backend);

	return TRUE;
}

static gboolean
backend_remove_package_thread (PkBackendThread *thread, gpointer data)
{
	ThreadData *d = (ThreadData*) data;
	PkPackageId *pi;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		g_free (d->package_id);
		g_free (d);
		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);

	if (!box_package_uninstall (pi->name, ROOT_DIRECTORY, common_progress, backend, FALSE))
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Cannot uninstall");
	}

	pk_package_id_free (pi);
	g_free (d->package_id);
	g_free (d);
	pk_backend_finished (backend);

	return TRUE;
}

static gboolean
backend_refresh_cache_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);

	box_repos_sync(ROOT_DIRECTORY, common_progress, backend);
	pk_backend_finished (backend);

	return TRUE;
}

/* ===================================================================== */

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	thread = pk_backend_thread_new ();
	network = pk_network_new ();
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
   
	g_object_unref (thread);
	g_object_unref (network);
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
backend_get_depends (PkBackend *backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	data->package_id = g_strdup(package_id);
	data->type = DEPS_TYPE_DEPENDS;
	pk_backend_thread_create (thread, backend_get_depends_requires_thread, data);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	data->package_id = g_strdup(package_id);
	pk_backend_thread_create (thread, backend_get_description_thread, data);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	data->package_id = g_strdup(package_id);
	pk_backend_thread_create (thread, backend_get_files_thread, data);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	data->package_id = g_strdup(package_id);
	data->type = DEPS_TYPE_REQUIRES;
	pk_backend_thread_create (thread, backend_get_depends_requires_thread, data);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, const gchar *filter)
{
	g_return_if_fail (backend != NULL);
	pk_backend_thread_create (thread, backend_get_updates_thread, NULL);
}


/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	data->package_id = g_strdup(package_id);
	pk_backend_thread_create (thread, backend_install_package_thread, data);
}

/**
 * backend_install_file:
 */
static void
backend_install_file (PkBackend *backend, const gchar *file)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	data->package_id = g_strdup(file);
	pk_backend_thread_create (thread, backend_install_file_thread, data);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_thread_create (thread, backend_refresh_cache_thread, NULL);
}

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	if (allow_deps == TRUE) {
		data->type = DEPS_ALLOW;
	} else {
		data->type = DEPS_NO_ALLOW;
	}
	data->package_id = g_strdup (package_id);
    
	pk_backend_thread_create (thread, backend_remove_package_thread, data);
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
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);
	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}

	data->package_ids = g_strdupv (package_ids);
	pk_backend_thread_create (thread, backend_update_packages_thread, data);
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_thread_create (thread, backend_update_system_thread, NULL);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, const gchar *filter)
{
	GList *list;
	GList *li;
	RepoInfo *repo;

	g_return_if_fail (backend != NULL);

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
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
        g_return_if_fail (backend != NULL);
	
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

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

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (!box_repos_set_param (rid, parameter, value))
	{
		pk_warning ("Cannot set PARAMETER '%s' TO '%s' for REPO '%s'", parameter, value, rid);
	}

	pk_backend_finished (backend);
}


PK_BACKEND_OPTIONS (
	"Box",					/* description */
	"Grzegorz Dąbrowski <grzegorz.dabrowski@gmail.com>",	/* author */
	backend_initialize,			/* initalize */
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
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	NULL,					/* service_pack */
	NULL					/* what_provides */
);

