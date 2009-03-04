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
#include <gio/gio.h>

#include "pk-app-install-common.h"
#include "egg-debug.h"

#if PK_BUILD_LOCAL
#define PK_APP_INSTALL_DEFAULT_DATABASE "./desktop.db"
#else
#define PK_APP_INSTALL_DEFAULT_DATABASE DATADIR "/app-install/cache/desktop.db"
#endif

const gchar *icon_sizes[] = { "22x22", "24x24", "32x32", "48x48", "scalable", NULL };

/**
 * pk_app_install_add_get_number_sqlite_cb:
 **/
static gint
pk_app_install_add_get_number_sqlite_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint *number = (guint *) data;
	(*number)++;
	return 0;
}

/**
 * pk_app_install_add_copy_icons_sqlite_cb:
 **/
static gint
pk_app_install_add_copy_icons_sqlite_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint i;
	gchar *col;
	gchar *value;
	const gchar *application_id = NULL;
	const gchar *icon_name = NULL;
	gchar *path;
	gchar *dest;
	GFile *file;
	GFile *remote;
	const gchar *icondir = (const gchar *) data;
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

	/* copy all icon sizes if they exist */
	for (i=0; icon_sizes[i] != NULL; i++) {
		path = g_build_filename (icondir, icon_sizes[i], icon_name, NULL);
		ret = g_file_test (path, G_FILE_TEST_EXISTS);
		if (ret) {
			dest = g_build_filename (PK_APP_INSTALL_DEFAULT_ICONDIR, icon_sizes[i], icon_name, NULL);
			egg_debug ("copying file %s to %s", path, dest);
			file = g_file_new_for_path (path);
			remote = g_file_new_for_path (dest);
			ret = g_file_copy (file, remote, G_FILE_COPY_TARGET_DEFAULT_PERMS, NULL, NULL, NULL, &error);
			if (!ret) {
				egg_warning ("cannot copy %s: %s", path, error->message);
				g_clear_error (&error);
			}
			g_object_unref (file);
			g_object_unref (remote);
			g_free (dest);
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
	gchar *source = NULL;
	gchar *icondir = NULL;
	sqlite3 *db = NULL;
	gchar *error_msg;
	gint rc;
	guint number = 0;
	gchar *statement;
	gchar *contents;
	gboolean ret;
	GError *error = NULL;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
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

	/* check that there are no existing entries from this repo */
	rc = sqlite3_open (cache, &db);
	if (rc) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (db));
		retval = 1;
		goto out;
	}

	/* check that there are no existing entries from this repo */
	statement = g_strdup_printf ("SELECT application_id FROM applications WHERE repo_id = '%s'", repo);
	rc = sqlite3_exec (db, statement, pk_app_install_add_get_number_sqlite_cb, (void*) &number, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		retval = 1;
		goto out;
	}

	/* already have data for this repo */
	if (number > 0) {
		egg_warning ("There are already %i entries for repo_id=%s", number, repo);
		goto out;
	}

	/* get all the sql from the source file */
	ret = g_file_get_contents (source, &contents, NULL, &error);
	if (!ret) {
		egg_warning ("cannot read source file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* copy all the applications and translations into remote db */
	rc = sqlite3_exec (db, contents, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		retval = 1;
		goto out;
	}
	egg_debug ("%i additions to the database", sqlite3_changes (db));

	/* copy all the icons */
	statement = g_strdup_printf ("SELECT application_id, icon_name FROM applications WHERE repo_id = '%s'", repo);
	rc = sqlite3_exec (db, statement, pk_app_install_add_copy_icons_sqlite_cb, (void*) icondir, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		retval = 1;
		goto out;
	}

out:
	if (db != NULL)
		sqlite3_close (db);
	g_free (contents);
	g_free (cache);
	g_free (repo);
	g_free (source);
	g_free (icondir);
	return 0;
}

