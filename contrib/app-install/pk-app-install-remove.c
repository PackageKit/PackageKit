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
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "pk-app-install-common.h"
#include "egg-debug.h"

const gchar *icon_sizes[] = { "22x22", "24x24", "32x32", "48x48", "scalable", NULL };

/**
 * pk_app_install_remove_icons_sqlite_cb:
 **/
static gint
pk_app_install_remove_icons_sqlite_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint i;
	gchar *col;
	gchar *value;
	const gchar *application_id = NULL;
	const gchar *icon_name = NULL;
	gchar *path;
	const gchar *icondir = (const gchar *) data;
	GFile *file;
	gboolean ret;
	GError *error = NULL;

	for (i=0; i<(guint)argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (g_strcmp0 (col, "application_id") == 0)
			application_id = value;
		else if (g_strcmp0 (col, "icon_name") == 0)
			icon_name = value;
	}
	if (application_id == NULL || icon_name == NULL)
		goto out;

	egg_debug ("removing icons for application: %s", application_id);

	/* delete all icon sizes */
	for (i=0; icon_sizes[i] != NULL; i++) {
		path = g_build_filename (icondir, icon_sizes[i], icon_name, NULL);
		ret = g_file_test (path, G_FILE_TEST_EXISTS);
		if (ret) {
			egg_debug ("removing file %s", path);
			file = g_file_new_for_path (path);
			ret = g_file_delete (file, NULL, &error);
			if (!ret) {
				egg_warning ("cannot delete %s: %s", path, error->message);
				g_clear_error (&error);
			}
			g_object_unref (file);
		}
		g_free (path);
	}
out:
	return 0;
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
	gchar *cache = NULL;
	gchar *repo = NULL;
	gchar *icondir = NULL;
	gboolean ret = TRUE;
	gchar *statement = NULL;
	sqlite3 *db = NULL;
	gchar *error_msg;
	gint rc;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "cache", 'c', 0, G_OPTION_ARG_STRING, &cache,
		  /* TRANSLATORS: if we are specifing a out-of-tree database */
		  _("Main cache file to use (if not specififed, default is used)"), NULL},
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

	/* use default */
	if (cache == NULL) {
		egg_debug ("cache not specified, using %s", PK_APP_INSTALL_DEFAULT_DATABASE);
		cache = g_strdup (PK_APP_INSTALL_DEFAULT_DATABASE);
	}
	if (icondir == NULL) {
		egg_debug ("icondir not specified, using %s", PK_APP_INSTALL_DEFAULT_ICONDIR);
		icondir = g_strdup (PK_APP_INSTALL_DEFAULT_ICONDIR);
	}

	/* check */
	if (repo == NULL) {
		egg_warning ("A repo name is required");
		retval = 1;
		goto out;
	}
	if (!g_file_test (icondir, G_FILE_TEST_IS_DIR)) {
		egg_warning ("The icon directory '%s' could not be found", icondir);
		retval = 1;
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
	rc = sqlite3_exec (db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't turn off sync: %s\n", sqlite3_errmsg (db));
		retval = 1;
		goto out;
	}

	/* remove icons */
	statement = g_strdup_printf ("SELECT application_id, icon_name FROM applications WHERE repo_id = '%s'", repo);
	rc = sqlite3_exec (db, statement, pk_app_install_remove_icons_sqlite_cb, (void*) icondir, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return 0;
	}

	/* delete from translations (translations has no repo_id, so key of applications) */
	statement = g_strdup_printf ("DELETE FROM translations WHERE EXISTS ( "
				      "SELECT applications.application_id FROM applications WHERE "
				      "applications.application_id = applications.application_id AND applications.repo_id = '%s')", repo);
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	g_free (statement);
	if (rc) {
		egg_warning ("Can't remove rows: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}
	egg_debug ("%i removals from translations", sqlite3_changes (db));

	/* delete from applications */
	statement = g_strdup_printf ("DELETE FROM applications WHERE repo_id = '%s'", repo);
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	g_free (statement);
	if (rc) {
		egg_warning ("Can't remove rows: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}
	egg_debug ("%i removals from applications", sqlite3_changes (db));

	/* reclaim memory */
	statement = g_strdup ("VACUUM");
	rc = sqlite3_exec (db, statement, NULL, NULL, NULL);
	if (rc) {
		egg_warning ("Can't vacuum: %s\n", sqlite3_errmsg (db));
		ret = FALSE;
		goto out;
	}
out:
	if (db != NULL)
		sqlite3_close (db);
	g_free (statement);
	g_free (cache);
	g_free (repo);
	g_free (icondir);
	return 0;
}

