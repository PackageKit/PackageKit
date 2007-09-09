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

typedef struct {
	PkBackend *backend;
	gchar *search;
	gchar *filter;
	gint mode;
} FindData;

typedef struct {
	PkBackend *backend;
	gchar *package_id;
} ThreadData;


static sqlite3*
db_open()
{
	sqlite3 *db;

	db = box_db_open("/");
	box_db_attach_repo(db, "/", "core");
	box_db_repos_init(db);

	return db;
}

static void
db_close(sqlite3 *db)
{
	box_db_detach_repo(db, "core");
	box_db_close(db);
}


static void
add_packages_from_list (PkBackend *backend, GList *list)
{
	PackageSearch *package = NULL;
	GList *li = NULL;
	gchar *pkg_string = NULL;

	for (li = list; li != NULL; li = li->next) {
		package = (PackageSearch*)li->data;
		pkg_string = pk_package_id_build(package->package, package->version, package->arch, package->reponame);

		pk_backend_package (backend, package->installed, pkg_string, package->description);

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

	pk_backend_change_job_status (backend, PK_STATUS_ENUM_QUERY);

	parse_filter (filter, &installed, &available, &devel, &nondevel, &gui, &text);

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
	pk_debug("filter: %d", search_filter);

	pk_backend_no_percentage_updates (backend);

	db = db_open();

	if (mode == 1) {
		/* TODO: allow filtering */
		list = box_db_repos_search_file (db, search);
		add_packages_from_list (backend, list);
		box_db_repos_package_list_free (list);
		pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
	} else {
		if (installed == FALSE && available == FALSE) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, "invalid search mode");
			pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
		} else	{
			if (installed == TRUE && available == TRUE) {
				list = box_db_repos_packages_search_all(db, (gchar *)search, search_filter);
			} else if (installed == TRUE) {
				list = box_db_repos_packages_search_installed(db, (gchar *)search, search_filter);
			} else if (available == TRUE) {
				list = box_db_repos_packages_search_available(db, (gchar *)search, search_filter);
			}
			add_packages_from_list (backend, list);
			box_db_repos_package_list_free (list);
			// FIXME: temporary workaround
			sleep(1);
			pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
		}
	}

	db_close(db);
}

void*
find_packages_thread (gpointer data)
{
	FindData *d = (FindData*) data;

	g_return_val_if_fail (d->backend != NULL, NULL);

	find_packages_real (d->backend, d->search, d->filter, d->mode);

	g_free(d->search);
	g_free(d->filter);
	g_free(d);

	return NULL;
}


static void
find_packages (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	FindData *data = g_new0(FindData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
	} else {
		data->backend = backend;
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->mode = mode;

		if (g_thread_create(find_packages_thread, data, FALSE, NULL) == NULL) {
			pk_backend_error_code(backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to create thread");
			pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		}
		
	}
}

static GList*
find_package_by_id (PkPackageId *pi)
{
	sqlite3 *db = NULL;
	GList *list;

	db = db_open();

	/* only one element is returned */
	list = box_db_repos_packages_search_by_data(db, pi->name, pi->version);
	if (list == NULL)
		return NULL;

	db_close(db);

	return list;
}


static void*
get_updates_thread(gpointer data)
{
	GList *list = NULL;
	sqlite3 *db = NULL;
	ThreadData *d = (ThreadData*) data;

	pk_backend_change_job_status (d->backend, PK_STATUS_ENUM_QUERY);

	db = db_open ();

	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (d->backend, list);
	box_db_repos_package_list_free (list);

	pk_backend_finished (d->backend, PK_EXIT_ENUM_SUCCESS);

	g_free(d);
	db_close (db);

	return NULL;
}

static void*
get_description_thread(gpointer data)
{
	PkPackageId *pi;
	PackageSearch *ps;
	GList *list;
	ThreadData *d = (ThreadData*) data;

	pi = pk_package_id_new_from_string (d->package_id);
	if (pi == NULL) {
		pk_backend_error_code (d->backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (d->backend, PK_EXIT_ENUM_FAILED);
		return NULL;
	}

	pk_backend_change_job_status (d->backend, PK_STATUS_ENUM_QUERY);
	list = find_package_by_id (pi);
	ps = (PackageSearch*) list->data;
	if (list == NULL) {
		pk_backend_error_code (d->backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "cannot find package by id");
		pk_backend_finished (d->backend, PK_EXIT_ENUM_FAILED);
		return NULL;
	}

	pk_backend_description (d->backend, pi->name, PK_GROUP_ENUM_OTHER, ps->description, "");

	pk_package_id_free (pi);
	box_db_repos_package_list_free (list);

	pk_backend_finished (d->backend, PK_EXIT_ENUM_SUCCESS);
	g_free (d->package_id);
	g_free (d);

	return NULL;
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
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_ACCESSIBILITY,
				      PK_GROUP_ENUM_GAMES,
				      PK_GROUP_ENUM_SYSTEM,
				      0);
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
				      0);
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
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
	} else {
		data->backend = backend;
		data->package_id = g_strdup(package_id);

		if (g_thread_create(get_description_thread, data, FALSE, NULL) == NULL) {
			pk_backend_error_code(backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to create thread");
			pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		}
	}

	return;
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	ThreadData *data = g_new0(ThreadData, 1);

	g_return_if_fail (backend != NULL);

	if (data == NULL) {
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory");
		pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
	} else {
		data->backend = backend;

		if (g_thread_create(get_updates_thread, data, FALSE, NULL) == NULL) {
			pk_backend_error_code(backend, PK_ERROR_ENUM_CREATE_THREAD_FAILED, "Failed to create thread");
			pk_backend_finished(backend, PK_EXIT_ENUM_FAILED);
		}
	}
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
		pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
		return;
	}
	pk_backend_change_job_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_spawn_helper (backend, "refresh-cache.sh", NULL);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, 1);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	find_packages (backend, search, filter, 0);
}

PK_BACKEND_OPTIONS (
	"Box Backend",				/* description */
	"0.0.1",				/* version */
	"Grzegorz Dąbrowski <gdx@o2.pl>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel_job_try */
	NULL,					/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_requires */
	backend_get_updates,			/* get_updates */
	NULL,					/* install_package */
	backend_refresh_cache,			/* refresh_cache */
	NULL,					/* remove_package */
	NULL,					/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL					/* update_system */
);

