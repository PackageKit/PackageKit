/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>
#include <sqlite3.h>

#include "egg-debug.h"

#if PK_BUILD_LOCAL
#define PK_APP_INSTALL_DEFAULT_DATABASE "./desktop.db"
#else
#define PK_APP_INSTALL_DEFAULT_DATABASE DATADIR "/app-install/cache/desktop.db"
#endif

/**
 * pk_app_install_create:
 **/
static gboolean
pk_app_install_create (const gchar *cache)
{
	gboolean ret = TRUE;
	gboolean create_file;
	const gchar *statement;
	sqlite3 *db = NULL;
	gint rc;

	/* if the database file was not installed (or was nuked) recreate it */
	create_file = g_file_test (cache, G_FILE_TEST_EXISTS);
	if (create_file == TRUE) {
		egg_warning ("already exists");
		goto out;
	}

	egg_debug ("exists: %i", create_file);

	/* open database */
	rc = sqlite3_open (cache, &db);
	if (rc) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}

	/* don't sync */
	statement = "PRAGMA synchronous=OFF";
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't turn off sync: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}

	egg_debug ("create");
	if (create_file == FALSE) {
		statement = "CREATE TABLE general ("
			    "application_id TEXT primary key,"
			    "package_name TEXT,"
			    "group_id TEXT,"
			    "repo_name TEXT,"
			    "application_name TEXT,"
			    "application_summary TEXT);";
		rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
		if (rc) {
			egg_warning ("Can't create general table: %s\n", sqlite3_errmsg (db));
			ret = FALSE;
			goto out;
		}
		statement = "CREATE TABLE localised ("
			    "application_id TEXT primary key,"
			    "application_name TEXT,"
			    "application_summary TEXT,"
			    "locale TEXT);";
		rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
		if (rc) {
			egg_warning ("Can't create localised table: %s\n", sqlite3_errmsg (db));
			ret = FALSE;
			goto out;
		}
	}

out:
	if (db != NULL)
		sqlite3_close (db);
	return ret;
}

/**
 * pk_app_install_remove_icons_sqlite_cb:
 **/
static gint
pk_app_install_remove_icons_sqlite_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	gchar *col;
	gchar *value;
	const gchar *application_id = NULL;
	gchar *path;
	gchar *filename;
	const gchar *icondir = (const gchar *) data;

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (g_strcmp0 (col, "application_id") == 0)
			application_id = value;
	}
	if (application_id == NULL)
		goto out;

	egg_warning ("application_id=%s", application_id);
	filename = g_strdup_printf ("%s.png", application_id);
	path = g_build_filename (icondir, "48x48", filename, NULL);

//	g_unlink (path);
	egg_warning ("path=%s", path);

	g_free (filename);
	g_free (path);
out:
	return 0;
}

/**
 * pk_app_install_remove:
 **/
static gboolean
pk_app_install_remove (const gchar *cache, const gchar *icondir, const gchar *repo)
{
	gboolean ret = TRUE;
	gchar *statement = NULL;
	sqlite3 *db = NULL;
	gchar *error_msg;
	gint rc;

	/* open database */
	rc = sqlite3_open (cache, &db);
	if (rc) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}

	/* remove icons */
	if (icondir != NULL) {
		statement = g_strdup_printf ("SELECT application_id FROM general WHERE repo_name = '%s'", repo);
		rc = sqlite3_exec (db, statement, pk_app_install_remove_icons_sqlite_cb, (void*) icondir, &error_msg);
		g_free (statement);
		if (rc != SQLITE_OK) {
			egg_warning ("SQL error: %s\n", error_msg);
			sqlite3_free (error_msg);
			return 0;
		}
	}

	/* delete from localised (localised has no repo_name, so key off general) */
	statement = g_strdup_printf ("DELETE FROM localised WHERE EXISTS ( "
				      "SELECT general.application_id FROM general WHERE "
				      "general.application_id = general.application_id AND general.repo_name = '%s')", repo);
//	statement = g_strdup_printf ("SELECT general.application_id FROM general WHERE general.application_id == general.application_id AND general.repo_name == '%s'", repo);
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't remove rows: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}
	egg_debug ("%i removals from localised", sqlite3_changes (db));
	g_free (statement);

	/* delete from general */
	statement = g_strdup_printf ("DELETE FROM general WHERE repo_name = '%s'", repo);
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't remove rows: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}
	egg_debug ("%i removals from general", sqlite3_changes (db));
	g_free (statement);

	/* reclaim memory */
	statement = g_strdup ("VACUUM");
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't vacuum: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}

out:
	g_free (statement);
	if (db != NULL)
		sqlite3_close (db);
	return ret;
}

/**
 * pk_app_install_add:
 **/
static gboolean
pk_app_install_add (const gchar *cache, const gchar *icondir, const gchar *repo, const gchar *source)
{
	egg_warning ("cache=%s, source=%s, repo=%s, icondir=%s", cache, source, repo, icondir);
	return TRUE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	GOptionContext *context;
	gint retval = 0;
	gchar *action = NULL;
	gchar *cache = NULL;
	gchar *repo = NULL;
	gchar *source = NULL;
	gchar *icondir = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "action", 'c', 0, G_OPTION_ARG_STRING, &action,
		  /* TRANSLATORS: the action is non-localised */
		  _("The action, one of 'create', 'add', or 'remove'"), NULL},
		{ "cache", 'c', 0, G_OPTION_ARG_STRING, &cache,
		  /* TRANSLATORS: if we are specifing a out-of-tree database */
		  _("Main cache file to use (if not specififed, default is used)"), NULL},
		{ "source", 's', 0, G_OPTION_ARG_STRING, &source,
		  /* TRANSLATORS: the source database, typically used for adding */
		  _("Source cache file to add to the main database"), NULL},
		{ "icondir", 'i', 0, G_OPTION_ARG_STRING, &icondir,
		  /* TRANSLATORS: the icon directory */
		  _("Icon directory"), NULL},
		{ "repo", 'n', 0, G_OPTION_ARG_STRING, &repo,
		  /* TRANSLATORS: the repo of the software source, e.g. fedora */
		  _("Name of the remote repo"), NULL},
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (NULL);
	/* TRANSLATORS: tool that gets called when the command is not found */
	g_option_context_set_summary (context, _("PackageKit Application Database Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);

	egg_debug ("cache=%s, source=%s, repo=%s, icondir=%s", cache, source, repo, icondir);

	/* use default */
	if (cache == NULL) {
		egg_debug ("cache not specified, using %s", PK_APP_INSTALL_DEFAULT_DATABASE);
		cache = g_strdup (PK_APP_INSTALL_DEFAULT_DATABASE);
	}

	if (g_strcmp0 (action, "create") == 0) {
		pk_app_install_create (cache);
	} else if (g_strcmp0 (action, "add") == 0) {
		if (repo == NULL) {
			egg_warning ("A repo name is required");
			retval = 1;
			goto out;
		}
		if (source == NULL) {
			egg_warning ("A source filename is required");
			retval = 1;
			goto out;
		}
		if (!g_file_test (source, G_FILE_TEST_EXISTS)) {
			egg_warning ("The source filename '%s' could not be found", source);
			retval = 1;
			goto out;
		}
		if (icondir == NULL || !g_file_test (icondir, G_FILE_TEST_IS_DIR)) {
			egg_warning ("The icon directory '%s' could not be found", icondir);
			retval = 1;
			goto out;
		}
		pk_app_install_add (cache, icondir, repo, source);
	} else if (g_strcmp0 (action, "remove") == 0) {
		if (repo == NULL) {
			egg_warning ("A repo name is required");
			retval = 1;
			goto out;
		}
		if (icondir == NULL || !g_file_test (icondir, G_FILE_TEST_IS_DIR)) {
			egg_warning ("The icon directory '%s' could not be found", icondir);
			retval = 1;
			goto out;
		}
		pk_app_install_remove (cache, icondir, repo);
	} else if (g_strcmp0 (action, "generate") == 0) {
		if (repo == NULL) {
			egg_warning ("A repo name is required");
			retval = 1;
			goto out;
		}
		if (icondir == NULL || !g_file_test (icondir, G_FILE_TEST_IS_DIR)) {
			egg_warning ("The icon directory '%s' could not be found", icondir);
			retval = 1;
			goto out;
		}
		pk_app_install_remove (cache, icondir, repo);
	} else {
		egg_warning ("An action is required");
		retval = 1;
	}

out:
	g_free (cache);
	g_free (repo);
	g_free (source);
	g_free (icondir);
	return 0;
}

