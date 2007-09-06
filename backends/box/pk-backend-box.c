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

#include <sqlite3.h>
#include <libbox/libbox-db.h>
#include <libbox/libbox-db-utils.h>
#include <libbox/libbox-db-repos.h>

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

static gboolean
find_packages (PkBackend *backend, const gchar *search, const gchar *filter, gint mode)
{
	GList *list = NULL;
	sqlite3 *db = NULL;
	gint devel_filter = 0;
	gboolean installed;
	gboolean available;
	gboolean devel;
	gboolean nondevel;
	gboolean gui;
	gboolean text;

	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, search);
	parse_filter (filter, &installed, &available, &devel, &nondevel, &gui, &text);

	if (devel == TRUE) {
		devel_filter = devel_filter | PKG_DEVEL;
	}
	if (nondevel == TRUE) {
		devel_filter = devel_filter | PKG_NON_DEVEL;
	}

	pk_backend_change_job_status (backend, PK_TASK_STATUS_QUERY);
	pk_backend_no_percentage_updates (backend);

	db = box_db_open ("/");
	box_db_attach_repo (db, "/", "core");
	box_db_repos_init (db);

	if (mode == 1) {
		/* TODO: allow filtering */
		/* TODO: make it more async */
		list = box_db_repos_search_file (db, search);
		add_packages_from_list (backend, list);
		box_db_repos_package_list_free (list);
		pk_backend_finished (backend, PK_TASK_EXIT_SUCCESS);
	} else {

		if (installed == FALSE && available == FALSE) {
			pk_backend_error_code (backend, PK_TASK_ERROR_CODE_UNKNOWN, "invalid search mode");
			pk_backend_finished (backend, PK_TASK_EXIT_FAILED);
		} else	{
			/* TODO: make it more async */
			if (installed == TRUE && available == TRUE) {
				list = box_db_repos_packages_search_all(db, (gchar *)search, devel_filter);
			} else if (installed == TRUE) {
				list = box_db_repos_packages_search_installed(db, (gchar *)search, devel_filter);
			} else if (available == TRUE) {
				list = box_db_repos_packages_search_available(db, (gchar *)search, devel_filter);
			}
			add_packages_from_list (backend, list);
			box_db_repos_package_list_free (list);
			pk_backend_finished (backend, PK_TASK_EXIT_SUCCESS);
		}
	}

	box_db_detach_repo(db, "core");
	box_db_close(db);

	return TRUE;
}

static GList*
find_package_by_id (PkPackageId *pi)
{
	sqlite3 *db = NULL;
	GList *list;

	db = box_db_open("/");
	box_db_attach_repo(db, "/", "core");
	box_db_repos_init(db);

	/* only one element is returned */
	list = box_db_repos_packages_search_by_data(db, pi->name, pi->version);
	if (list == NULL)
		return NULL;

	box_db_detach_repo(db, "core");
	box_db_close(db);

	return list;
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	PkPackageId *pi;
	PackageSearch *ps;
	GList *list;

	g_return_if_fail (backend != NULL);

	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_TASK_ERROR_CODE_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished (backend, PK_TASK_EXIT_FAILED);
		return;
	}

	list = find_package_by_id (pi);
	ps = (PackageSearch*) list->data;
	if (list == NULL) {
		pk_backend_error_code (backend, PK_TASK_ERROR_CODE_PACKAGE_ID_INVALID, "cannot find package by id");
		pk_backend_finished (backend, PK_TASK_EXIT_FAILED);
		return;
	}


	pk_backend_set_job_role (backend, PK_TASK_ROLE_QUERY, package_id);
	pk_backend_description (backend, pi->name, PK_TASK_GROUP_OTHER, ps->description, "");

	pk_package_id_free (pi);
	box_db_repos_package_list_free (list);

	pk_backend_finished (backend, PK_TASK_EXIT_SUCCESS);
	return;
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	GList *list = NULL;
	sqlite3 *db = NULL;

	g_return_if_fail (backend != NULL);

	pk_backend_set_job_role (backend, PK_TASK_ROLE_QUERY, NULL);
	pk_backend_change_job_status (backend, PK_TASK_STATUS_QUERY);

	db = box_db_open ("/");
	box_db_attach_repo (db, "/", "core");
	box_db_repos_init (db);

	/* TODO: make it more async */
	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (backend, list);
	box_db_repos_package_list_free (list);

	pk_backend_finished (backend, PK_TASK_EXIT_SUCCESS);

	box_db_detach_repo (db, "core");
	box_db_close (db);
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
		pk_backend_error_code (backend, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend, PK_TASK_EXIT_FAILED);
		return;
	}
	pk_backend_set_job_role (backend, PK_TASK_ROLE_REFRESH_CACHE, NULL);
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
	"Dummy Backend",			/* description */
	"0.0.1",				/* version */
	"Grzegorz Dąbrowski <gdx@o2.pl>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
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

