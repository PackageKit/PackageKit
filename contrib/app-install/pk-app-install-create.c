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

#include "pk-app-install-common.h"
#include "egg-debug.h"

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean verbose = FALSE;
	GOptionContext *context;
	gchar *cache = NULL;
	gboolean create_file;
	const gchar *statement;
	sqlite3 *db = NULL;
	gint retval;
	gint rc;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "cache", 'c', 0, G_OPTION_ARG_STRING, &cache,
		  /* TRANSLATORS: if we are specifing a out-of-tree database */
		  _("Main database file to use (if not specififed, default is used)"), NULL},
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

	/* use default */
	if (cache == NULL) {
		egg_debug ("cache not specified, using %s", PK_APP_INSTALL_DEFAULT_DATABASE);
		cache = g_strdup (PK_APP_INSTALL_DEFAULT_DATABASE);
	}

	/* if the database file was not installed (or was nuked) recreate it */
	create_file = g_file_test (cache, G_FILE_TEST_EXISTS);
	if (create_file == TRUE) {
		egg_warning ("already exists");
		goto out;
	}

	/* open database */
	rc = sqlite3_open (cache, &db);
	if (rc) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (db));
		retval = 1;
		goto out;
	}

	/* don't sync */
	statement = "PRAGMA synchronous=OFF";
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't turn off sync: %s\n", sqlite3_errmsg (db));
		retval = 1;
		goto out;
	}

	/* create */
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
			retval = 1;
			goto out;
		}
		statement = "CREATE TABLE localised ("
			    "application_id TEXT,"
			    "application_name TEXT,"
			    "application_summary TEXT,"
			    "locale TEXT);";
		rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
		if (rc) {
			egg_warning ("Can't create localised table: %s\n", sqlite3_errmsg (db));
			retval = 1;
			goto out;
		}
	}
out:
	if (db != NULL)
		sqlite3_close (db);
	g_free (cache);
	return retval;
}

