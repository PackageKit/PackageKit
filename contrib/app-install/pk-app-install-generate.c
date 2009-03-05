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
#include <packagekit-glib/packagekit.h>

#include "pk-app-install-common.h"
#include "egg-debug.h"

static const gchar *icon_sizes[] = { "22x22", "24x24", "32x32", "48x48", "scalable", NULL };
static PkDesktop *desktop;

/**
 * pk_app_install_generate_create_icon_directories:
 **/
static gboolean
pk_app_install_generate_create_icon_directories (const gchar *directory)
{
	gboolean ret;
	GError *error = NULL;
	GFile *file;
	gchar *path;
	guint i;

	for (i=0; icon_sizes[i] != NULL; i++) {
		path = g_build_filename (directory, icon_sizes[i], NULL);
		ret = g_file_test (path, G_FILE_TEST_IS_DIR);
		if (!ret) {
			egg_debug ("creating %s", path);
			file = g_file_new_for_path (path);
			ret = g_file_make_directory (file, NULL, &error);
			if (!ret) {
				egg_warning ("cannot create %s: %s", path, error->message);
				g_clear_error (&error);
			}
			g_object_unref (file);
		}
		g_free (path);
	}
	return ret;
}

/**
 * pk_app_install_generate_get_desktop_files:
 **/
static GPtrArray *
pk_app_install_generate_get_desktop_files (const gchar *directory)
{
	GPtrArray *files = NULL;
	GError *error = NULL;
	const gchar *filename;
	GDir *dir;

	dir = g_dir_open (directory, 0, &error);
	if (dir == NULL) {
		egg_warning ("cannot open directory %s: %s", directory, error->message);
		g_error_free (error);
		goto out;
	}

	files = g_ptr_array_new ();
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		if (g_str_has_suffix (filename, ".desktop"))
			g_ptr_array_add (files, g_build_filename (directory, filename, NULL));
		filename = g_dir_read_name (dir);
	}
out:
	g_dir_close (dir);
	return files;
}


typedef struct {
	gchar	*key;
	gchar	*value;
	gchar	*locale;
} PkDesktopData;

/**
 * pk_app_install_generate_desktop_data_free:
 **/
static void
pk_app_install_generate_desktop_data_free (PkDesktopData *data)
{
	g_free (data->key);
	g_free (data->value);
	g_free (data->locale);
	g_free (data);
}

/**
 * pk_app_install_generate_get_desktop_data:
 **/
static GPtrArray *
pk_app_install_generate_get_desktop_data (const gchar *filename)
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *data = NULL;
	gchar *contents = NULL;
	gchar **lines;
	gchar **parts;
	guint i, len;
	PkDesktopData *obj;

	/* get all the contents */
	ret = g_file_get_contents (filename, &contents, NULL, &error);
	if (!ret) {
		egg_warning ("cannot read source file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	data = g_ptr_array_new ();

	/* split lines and extract data */
	lines = g_strsplit (contents, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		parts = g_strsplit_set (lines[i], "=[]", -1);
		len = g_strv_length (parts);
		if (len == 2) {
			obj = g_new0 (PkDesktopData, 1);
			obj->key = g_strdup (parts[0]);
			obj->value = g_strdup (parts[1]);
			g_ptr_array_add (data, obj);
		} else if (len == 4) {
			obj = g_new0 (PkDesktopData, 1);
			obj->key = g_strdup (parts[0]);
			obj->locale = g_strdup (parts[1]);
			obj->value = g_strdup (parts[3]);
			g_ptr_array_add (data, obj);
		}
		g_strfreev (parts);
	}
	g_strfreev (lines);
out:
	return data;
}

/**
 * pk_app_install_generate_get_value_for_locale:
 **/
static gchar *
pk_app_install_generate_get_value_for_locale (GPtrArray *data, const gchar *key, const gchar *locale)
{
	guint i;
	gchar *value = NULL;
	const PkDesktopData *obj;

	/* find data matching key name and locale */
	for (i=0; i<data->len; i++) {
		obj = g_ptr_array_index (data, i);
		if (g_strcmp0 (key, obj->key) == 0 && g_strcmp0 (locale, obj->locale) == 0) {
			value = g_strdup (obj->value);
			break;
		}
	}
	return value;
}

/**
 * pk_app_install_generate_get_locales:
 **/
static GPtrArray *
pk_app_install_generate_get_locales (GPtrArray *data)
{
	guint i, j;
	GPtrArray *locales;
	const PkDesktopData *obj;

	/* find data matching key name and locale */
	locales = g_ptr_array_new ();
	for (i=0; i<data->len; i++) {
		obj = g_ptr_array_index (data, i);

		/* no point */
		if (obj->locale == NULL)
			continue;

		/* is already in locale list */
		for (j=0; j<locales->len; j++) {
			if (g_strcmp0 (obj->locale, g_ptr_array_index (locales, j)) == 0)
				break;
		}
		/* not already there */
		if (j == locales->len)
			g_ptr_array_add (locales, g_strdup (obj->locale));
	}
	return locales;
}

/**
 * pk_app_install_generate_get_package_for_file:
 **/
static gchar *
pk_app_install_generate_get_package_for_file (const gchar *filename)
{
	gchar *package;
	GError *error = NULL;

	/* get package providing file */
	package = pk_desktop_get_package_for_file (desktop, filename, &error);
	if (package == NULL) {
		egg_warning ("failed to get package for %s: %s", filename, error->message);
		g_error_free (error);
	}
	return package;
}

/**
 * pk_app_install_generate_get_application_id:
 **/
static gchar *
pk_app_install_generate_get_application_id (const gchar *filename)
{
	gchar *find;
	gchar *application_id;

	find = g_strrstr (filename, "/");
	application_id = g_strdup (find+1);
	find = g_strrstr (application_id, ".");
	*find = '\0';
	return application_id;
}

/**
 * pk_app_install_generate_applications_sql:
 **/
static gchar *
pk_app_install_generate_applications_sql (GPtrArray *data, const gchar *repo, const gchar *package, const gchar *application_id)
{
	GString *sql;
	gchar *name = NULL;
	gchar *comment = NULL;
	gchar *icon_name = NULL;
	gchar *categories = NULL;
	gchar *escaped;

	sql = g_string_new ("");
	name = pk_app_install_generate_get_value_for_locale (data, "Name", NULL);
	icon_name = pk_app_install_generate_get_value_for_locale (data, "Icon", NULL);
	comment = pk_app_install_generate_get_value_for_locale (data, "Comment", NULL);
	categories = pk_app_install_generate_get_value_for_locale (data, "Categories", NULL);

	/* remove invalid icons */
	if (icon_name != NULL &&
	    (g_str_has_prefix (icon_name, "/") ||
	     g_str_has_suffix (icon_name, ".png"))) {
		g_free (icon_name);
		icon_name = NULL;
	}

	egg_debug ("application_id=%s, name=%s, comment=%s, icon=%s, categories=%s", application_id, name, comment, icon_name, categories);

	/* append the application data to the sql string */
	escaped = sqlite3_mprintf ("INSERT INTO applications (application_id, package_name, categories, "
				   "repo_id, icon_name, application_name, application_summary) "
				   "VALUES (%Q, %Q, %Q, %Q, %Q, %Q, %Q);",
				   application_id, package, categories, repo, icon_name, name, comment);
	g_string_append_printf (sql, "%s\n", escaped);

	sqlite3_free (escaped);
	g_free (name);
	g_free (comment);
	g_free (icon_name);
	g_free (categories);
	return g_string_free (sql, FALSE);
}

/**
 * pk_app_install_generate_translations_sql:
 **/
static gchar *
pk_app_install_generate_translations_sql (GPtrArray *data, GPtrArray *locales, const gchar *application_id)
{
	GString *sql;
	gchar *name = NULL;
	gchar *comment = NULL;
	gchar *escaped;
	const gchar *locale;
	guint i;

	sql = g_string_new ("");
	for (i=0; i<locales->len; i++) {
		locale = g_ptr_array_index (locales, i);
		name = pk_app_install_generate_get_value_for_locale (data, "Name", locale);
		comment = pk_app_install_generate_get_value_for_locale (data, "Comment", locale);

		/* append the application data to the sql string */
		escaped = sqlite3_mprintf ("INSERT INTO translations (application_id, application_name, application_summary, locale) "
					   "VALUES (%Q, %Q, %Q, %Q);", application_id, name, comment, locale);
		g_string_append_printf (sql, "%s\n", escaped);

		sqlite3_free (escaped);
		g_free (name);
		g_free (comment);
	}

	return g_string_free (sql, FALSE);
}

/**
 * pk_app_install_generate_copy_icons:
 **/
static gboolean
pk_app_install_generate_copy_icons (const gchar *directory, const gchar *icon_name)
{
	gboolean ret;
	GError *error = NULL;
	GFile *file;
	GFile *remote;
	gchar *dest;
	gchar *iconpath;
	gchar *icon_name_full;
	guint i;

	/* copy all icon sizes if they exist */
	icon_name_full = g_strdup_printf ("%s.png", icon_name);
	for (i=0; icon_sizes[i] != NULL; i++) {
		iconpath = g_build_filename (PK_APP_INSTALL_DEFAULT_APPICONDIR, icon_sizes[i], "apps", icon_name_full, NULL);
		ret = g_file_test (iconpath, G_FILE_TEST_EXISTS);
		if (ret) {
			dest = g_build_filename (directory, icon_sizes[i], icon_name_full, NULL);
			egg_debug ("copying file %s to %s", iconpath, dest);
			file = g_file_new_for_path (iconpath);
			remote = g_file_new_for_path (dest);
			ret = g_file_copy (file, remote, G_FILE_COPY_TARGET_DEFAULT_PERMS | G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
			if (!ret) {
				egg_warning ("cannot copy %s: %s", dest, error->message);
				g_clear_error (&error);
			}
			g_object_unref (file);
			g_object_unref (remote);
			g_free (dest);
		} else {
			egg_debug ("does not exist: %s, so not copying", iconpath);
		}
		g_free (iconpath);
	}
	g_free (icon_name_full);
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
	gchar *cache = NULL;
	gchar *repo = NULL;
	gchar *applicationdir = NULL;
	gchar *icondir = NULL;
	gchar *outputdir = NULL;
	gboolean ret;
	GError *error = NULL;
	const gchar *filename;
	GString *string = NULL;
	GPtrArray *files = NULL;
	GPtrArray *data;
	guint k;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "cache", 'c', 0, G_OPTION_ARG_STRING, &cache,
		  /* TRANSLATORS: if we are specifing a out-of-tree database */
		  _("Main cache file to use (if not specififed, default is used)"), NULL},
		{ "applicationdir", 's', 0, G_OPTION_ARG_STRING, &applicationdir,
		  /* TRANSLATORS: the applicationdir database, typically used for adding */
		  _("Source cache file to add to the main database"), NULL},
		{ "icondir", 'i', 0, G_OPTION_ARG_STRING, &icondir,
		  /* TRANSLATORS: the icon directory */
		  _("Icon directory"), NULL},
		{ "outputdir", 'i', 0, G_OPTION_ARG_STRING, &outputdir,
		  /* TRANSLATORS: the output directory */
		  _("Icon directory"), NULL},
		{ "repo", 'n', 0, G_OPTION_ARG_STRING, &repo,
		  /* TRANSLATORS: the repo of the software applicationdir, e.g. fedora */
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

	g_type_init ();
	egg_debug_init (verbose);
	desktop = pk_desktop_new ();
	ret = pk_desktop_open_database (desktop, &error);
	if (!ret) {
		egg_warning ("cannot open database: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* use default */
	if (cache == NULL) {
		egg_debug ("cache not specified, using %s", PK_APP_INSTALL_DEFAULT_DATABASE);
		cache = g_strdup (PK_APP_INSTALL_DEFAULT_DATABASE);
	}

	/* things we require */
	if (repo == NULL) {
		egg_warning ("A repo name is required");
		retval = 1;
		goto out;
	}
	if (outputdir == NULL) {
		egg_warning ("A icon output directory is required");
		retval = 1;
		goto out;
	}

	/* use defaults */
	if (applicationdir == NULL) {
		egg_debug ("applicationdir not specified, using %s", PK_APP_INSTALL_DEFAULT_APPDIR);
		applicationdir = g_strdup (PK_APP_INSTALL_DEFAULT_APPDIR);
	}
	if (icondir == NULL) {
		egg_debug ("icondir not specified, using %s", PK_APP_INSTALL_DEFAULT_APPICONDIR);
		icondir = g_strdup (PK_APP_INSTALL_DEFAULT_APPICONDIR);
	}

	/* check directories exist */
	if (!g_file_test (applicationdir, G_FILE_TEST_IS_DIR)) {
		egg_warning ("The applicationdir filename '%s' could not be found", applicationdir);
		retval = 1;
		goto out;
	}
	if (!g_file_test (icondir, G_FILE_TEST_IS_DIR)) {
		egg_warning ("The icondir filename '%s' could not be found", icondir);
		retval = 1;
		goto out;
	}
	if (!g_file_test (outputdir, G_FILE_TEST_IS_DIR)) {
		egg_warning ("The icon output directory '%s' could not be found", outputdir);
		retval = 1;
		goto out;
	}

	/* just dump them */
	egg_warning ("cache=%s, applicationdir=%s, repo=%s, icondir=%s, outputdir=%s", cache, applicationdir, repo, icondir, outputdir);

	/* generate the sub directories in the outputdir if they dont exist */
	pk_app_install_generate_create_icon_directories (outputdir);

	/* use this to dump the data */
	string = g_string_new ("/* auto generated today */\n");

	/* get a list of desktop files in applicationdir */
	files = pk_app_install_generate_get_desktop_files (applicationdir);

	for (k=0; k<files->len; k++) {
		gchar *sql;
		gchar *package;
		gchar *application_id;
		gchar *icon_name;
		GPtrArray *locales;

		filename = g_ptr_array_index (files, k);
		egg_debug ("filename: %s", filename);

		/* get package name */
		package = pk_app_install_generate_get_package_for_file (filename);
		if (package == NULL)
			continue;

		/* get app-id */
		application_id = pk_app_install_generate_get_application_id (filename);

		/* extract data */
		data = pk_app_install_generate_get_desktop_data (filename);

		/* form application SQL */
		sql = pk_app_install_generate_applications_sql (data, repo, package, application_id);
		g_string_append_printf (string, "%s", sql);

		/* get list of locales in this file */
		locales = pk_app_install_generate_get_locales (data);

		/* form translations SQL */
		sql = pk_app_install_generate_translations_sql (data, locales, application_id);
		g_string_append_printf (string, "%s\n", sql);

		/* copy icons */
		icon_name = pk_app_install_generate_get_value_for_locale (data, "Icon", NULL);
		if (icon_name != NULL)
			pk_app_install_generate_copy_icons (outputdir, icon_name);

		/* free temp data */
		g_ptr_array_foreach (locales, (GFunc) g_free, NULL);
		g_ptr_array_free (locales, TRUE);
		g_ptr_array_foreach (data, (GFunc) pk_app_install_generate_desktop_data_free, NULL);
		g_ptr_array_free (data, TRUE);
		g_free (icon_name);
		g_free (sql);
		g_free (package);
		g_free (application_id);
	}

	/* save to disk */
	ret = g_file_set_contents (cache, string->str, -1, &error);
	if (!ret) {
		egg_warning ("cannot write data file: %s", error->message);
		g_error_free (error);
		goto out;
	}
	egg_debug ("saved to %s", cache);

out:
	if (string != NULL)
		g_string_free (string, TRUE);
	g_ptr_array_foreach (files, (GFunc) g_free, NULL);
	g_ptr_array_free (files, TRUE);
	g_free (cache);
	g_free (repo);
	g_free (applicationdir);
	g_free (icondir);
	g_free (outputdir);
	g_object_unref (desktop);
	return 0;
}

