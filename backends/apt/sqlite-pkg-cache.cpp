/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
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

#include <sqlite3.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "sqlite-pkg-cache.h"

static sqlite3 *db = NULL;

struct desc_task {
	PkPackageId *pi;
};

struct search_task {
	gchar *search;
	gchar *filter;
	SearchDepth depth;
};

void
sqlite_init_cache(PkBackend *backend, const char* dbname, const char *compare_fname, void (*build_db)(PkBackend *, sqlite3 *))
{
	int ret;
	struct stat st;
	time_t db_age;

	ret = sqlite3_open (dbname, &db);
	ret = sqlite3_exec(db,"PRAGMA synchronous = OFF",NULL,NULL,NULL);
	g_assert(ret == SQLITE_OK);

	g_stat(dbname, &st);
	db_age = st.st_mtime;
	g_stat(compare_fname, &st);
	if (db_age>=st.st_mtime)
	{
		ret = sqlite3_exec(db, "select value from params where name = 'build_complete'", NULL, NULL, NULL);
		if (ret != SQLITE_ERROR)
			return;
	}
	ret = sqlite3_exec(db,"drop table packages",NULL,NULL,NULL); // wipe it!
	//g_assert(ret == SQLITE_OK);
	pk_debug("wiped db");
	ret = sqlite3_exec(db,"create table packages (name text, version text, deps text, arch text, short_desc text, long_desc text, repo string, primary key(name,version,arch,repo))",NULL,NULL,NULL);
	g_assert(ret == SQLITE_OK);

	build_db(backend,db);

	sqlite3_exec(db,"create table params (name text primary key, value integer)", NULL, NULL, NULL);
	sqlite3_exec(db,"insert into params values ('build_complete',1)", NULL, NULL, NULL);
}

// sqlite_search_packages_thread
static gboolean
sqlite_search_packages_thread (PkBackend *backend, gpointer data)
{
	search_task *st = (search_task *) data;
	int res;

	pk_backend_change_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	pk_debug("finding %s", st->search);

	sqlite3_stmt *package = NULL;
	g_strdelimit(st->search," ",'%');
	gchar *sel = g_strdup_printf("select name,version,arch,repo,short_desc from packages where name like '%%%s%%'",st->search);
	pk_debug("statement is '%s'",sel);
	res = sqlite3_prepare_v2(db,sel, -1, &package, NULL);
	g_free(sel);
	if (res!=SQLITE_OK)
		pk_error("sqlite error during select prepare: %s", sqlite3_errmsg(db));
	res = sqlite3_step(package);
	while (res == SQLITE_ROW)
	{
		gchar *pid = pk_package_id_build((const gchar*)sqlite3_column_text(package,0),
				(const gchar*)sqlite3_column_text(package,1),
				(const gchar*)sqlite3_column_text(package,2),
				(const gchar*)sqlite3_column_text(package,3));
		pk_backend_package(backend, PK_INFO_ENUM_UNKNOWN, pid, (const gchar*)sqlite3_column_text(package,4));
		g_free(pid);
		if (res==SQLITE_ROW)
			res = sqlite3_step(package);
	}
	if (res!=SQLITE_DONE)
	{
		pk_debug("sqlite error during step (%d): %s", res, sqlite3_errmsg(db));
		g_assert(0);
	}

	g_free(st->search);
	g_free(st);

	return TRUE;
}

/**
 * backend_search_common
 **/
void
backend_search_common(PkBackend * backend, const gchar * filter, const gchar * search, SearchDepth which, PkBackendThreadFunc func)
{
	g_return_if_fail (backend != NULL);
	search_task *data = g_new(struct search_task, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for search task");
		pk_backend_finished(backend);
	}
	else
	{
		data->search = g_strdup(search);
		data->filter = g_strdup(filter);
		data->depth = which;
		pk_backend_thread_helper (backend, func, data);
	}
}

/**
 * sqlite_search_details:
 */
void
sqlite_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_DETAILS, sqlite_search_packages_thread);
}

/**
 * sqlite_search_name:
 */
void
sqlite_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	backend_search_common(backend, filter, search, SEARCH_NAME, sqlite_search_packages_thread);
}

// sqlite_get_description_thread
static gboolean sqlite_get_description_thread (PkBackend *backend, gpointer data)
{
	desc_task *dt = (desc_task *) data;
	int res;

	pk_backend_change_status(backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates(backend);

	pk_debug("finding %s", dt->pi->name);

	sqlite3_stmt *package = NULL;
	gchar *sel = g_strdup_printf("select long_desc from packages where name = '%s' and version = '%s' and repo = '%s'",dt->pi->name,dt->pi->version,dt->pi->data);
	pk_debug("statement is '%s'",sel);
	res = sqlite3_prepare_v2(db,sel, -1, &package, NULL);
	g_free(sel);
	if (res!=SQLITE_OK)
		pk_error("sqlite error during select prepare: %s", sqlite3_errmsg(db));
	res = sqlite3_step(package);
	pk_backend_description(backend,dt->pi->name, "unknown", PK_GROUP_ENUM_OTHER,(const gchar*)sqlite3_column_text(package,0),"",0,"");
	res = sqlite3_step(package);
	if (res==SQLITE_ROW)
		pk_error("multiple matches for that package!");
	if (res!=SQLITE_DONE)
	{
		pk_debug("sqlite error during step (%d): %s", res, sqlite3_errmsg(db));
		g_assert(0);
	}

	g_free(dt);

	return TRUE;
}

/**
 * sqlite_get_description:
 */
void
sqlite_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	desc_task *data = g_new(struct desc_task, 1);
	if (data == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_OOM, "Failed to allocate memory for search task");
		pk_backend_finished(backend);
		return;
	}

	data->pi = pk_package_id_new_from_string(package_id);
	if (data->pi == NULL)
	{
		pk_backend_error_code(backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_finished(backend);
		return;
	}

	pk_backend_thread_helper (backend, sqlite_get_description_thread, data);
	return;
}


