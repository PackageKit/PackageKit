/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-extra
 * @short_description: Client singleton access to extra metadata about a package
 *
 * Extra metadata such as icon name and localised summary may be stored here
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <sqlite3.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-extra.h"
#include "pk-common.h"

static void     pk_extra_class_init	(PkExtraClass *klass);
static void     pk_extra_init		(PkExtra      *extra);
static void     pk_extra_finalize	(GObject     *object);

#define PK_EXTRA_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_EXTRA, PkExtraPrivate))
#define PK_EXTRA_DEFAULT_DATABASE_INTERNAL	PK_DB_DIR "/extra-data.db"

/**
 * PkExtraPrivate:
 *
 * Private #PkExtra data
 **/
struct _PkExtraPrivate
{
	gchar			*database;
	gchar			*locale;
	gchar			*locale_base;
	sqlite3			*db;
	GHashTable		*hash_locale;
	GHashTable		*hash_package;
};

typedef struct
{
	gchar			*summary;
} PkExtraLocaleObj;

typedef struct
{
	gchar			*icon_name;
	gchar			*exec;
} PkExtraPackageObj;

G_DEFINE_TYPE (PkExtra, pk_extra, G_TYPE_OBJECT)
static gpointer pk_extra_object = NULL;

/**
 * pk_extra_populate_package_cache_callback:
 **/
static gint
pk_extra_populate_package_cache_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkExtra *extra = PK_EXTRA (data);
	PkExtraPackageObj *obj;
	gint i;
	gchar *col;
	gchar *value;
	gchar *package = NULL;
	gchar *icon_name = NULL;
	gchar *exec = NULL;

	g_return_val_if_fail (PK_IS_EXTRA (extra), 0);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		/* save the package name, and use it is the key */
		if (egg_strequal (col, "package") && value != NULL) {
			package = g_strdup (argv[i]);
		} else if (egg_strequal (col, "icon") && value != NULL) {
			icon_name = g_strdup (argv[i]);
		} else if (egg_strequal (col, "exec") && value != NULL) {
			exec = g_strdup (argv[i]);
		}
	}

	/* sanity check */
	if (package == NULL) {
		egg_warning ("package data invalid (%s,%s,%s)", package, icon_name, exec);
		goto out;
	}

	/* check we are not already added */
	obj = g_hash_table_lookup (extra->priv->hash_package, package);
	if (obj != NULL) {
		g_free (exec);
		g_free (package);
		g_free (icon_name);
		goto out;
	}

	obj = g_new (PkExtraPackageObj, 1);
	obj->icon_name = icon_name;
	obj->exec = exec;
	g_hash_table_insert (extra->priv->hash_package, (gpointer) package, (gpointer) obj);
out:
	return 0;
}

/**
 * pk_extra_populate_locale_cache_callback:
 **/
static gint
pk_extra_populate_locale_cache_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkExtra *extra = PK_EXTRA (data);
	PkExtraLocaleObj *obj;
	gint i;
	gchar *col;
	gchar *value;
	gchar *package = NULL;
	gchar *summary = NULL;

	g_return_val_if_fail (PK_IS_EXTRA (extra), 0);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		/* save the package name, and use it is the key */
		if (egg_strequal (col, "package") && value != NULL) {
			package = g_strdup (argv[i]);
		} else if (egg_strequal (col, "summary") && value != NULL) {
			summary = g_strdup (argv[i]);
		}
	}

	/* sanity check */
	if (package == NULL) {
		egg_warning ("package data invalid (%s,%s)", package, summary);
		goto out;
	}

	/* check we are not already added */
	obj = g_hash_table_lookup (extra->priv->hash_locale, package);
	if (obj != NULL) {
		g_free (package);
		g_free (summary);
		goto out;
	}

	obj = g_new (PkExtraLocaleObj, 1);
	obj->summary = summary;
	g_hash_table_insert (extra->priv->hash_locale, (gpointer) package, (gpointer) obj);
out:
	return 0;
}

/**
 * pk_extra_populate_locale_cache:
 * @extra: a valid #PkExtra instance
 *
 * Return value: %TRUE if set correctly
 **/
static gboolean
pk_extra_populate_locale_cache (PkExtra *extra)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		egg_debug ("no database");
		return FALSE;
	}

	/* get summary packages */
	statement = g_strdup_printf ("SELECT package, summary FROM localised WHERE locale = '%s'", extra->priv->locale);
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_populate_locale_cache_callback, extra, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	/* get summary packages - base */
	statement = g_strdup_printf ("SELECT package, summary FROM localised WHERE locale = '%s'", extra->priv->locale_base);
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_populate_locale_cache_callback, extra, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_extra_populate_package_cache:
 * @extra: a valid #PkExtra instance
 *
 * Return value: %TRUE if set correctly
 **/
static gboolean
pk_extra_populate_package_cache (PkExtra *extra)
{
	const gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		egg_debug ("no database");
		return FALSE;
	}

	/* get packages */
	statement = "SELECT package, icon, exec FROM data";
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_populate_package_cache_callback, extra, &error_msg);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_extra_set_locale:
 * @extra: a valid #PkExtra instance
 * @locale: a correct locale
 *
 * Return value: %TRUE if set correctly
 **/
gboolean
pk_extra_set_locale (PkExtra *extra, const gchar *locale)
{
	guint i;
	guint len;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (locale != NULL, FALSE);

	g_free (extra->priv->locale);
	extra->priv->locale = g_strdup (locale);
	extra->priv->locale_base = g_strdup (locale);

	/* we only want the first section to compare */
	len = egg_strlen (locale, 10);
	for (i=0; i<len; i++) {
		if (extra->priv->locale_base[i] == '_') {
			extra->priv->locale_base[i] = '\0';
			egg_debug ("locale_base is '%s'", extra->priv->locale_base);
			break;
		}
	}

	/* no point doing it twice if they are the same */
	if (egg_strequal (extra->priv->locale_base, extra->priv->locale)) {
		g_free (extra->priv->locale_base);
		extra->priv->locale_base = NULL;
	}

	/* try to populate a working cache */
	pk_extra_populate_locale_cache (extra);

	return TRUE;
}

/**
 * pk_extra_get_locale:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
const gchar *
pk_extra_get_locale (PkExtra *extra)
{
	g_return_val_if_fail (PK_IS_EXTRA (extra), NULL);
	return extra->priv->locale;
}

/**
 * pk_extra_get_summary:
 * @extra: a valid #PkExtra instance
 *
 * Return value: if we managed to get data
 **/
const gchar *
pk_extra_get_summary (PkExtra *extra, const gchar *package)
{
	PkExtraLocaleObj *obj;

	g_return_val_if_fail (PK_IS_EXTRA (extra), NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* super quick if exists in cache */
	obj = g_hash_table_lookup (extra->priv->hash_locale, package);
	if (obj == NULL) {
		return FALSE;
	}
	return obj->summary;
}

/**
 * pk_extra_get_icon_name:
 * @extra: a valid #PkExtra instance
 *
 * Return value: if we managed to get data
 **/
const gchar *
pk_extra_get_icon_name (PkExtra *extra, const gchar *package)
{
	PkExtraPackageObj *obj;

	g_return_val_if_fail (PK_IS_EXTRA (extra), NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* super quick if exists in cache */
	obj = g_hash_table_lookup (extra->priv->hash_package, package);
	if (obj == NULL) {
		return FALSE;
	}
	return obj->icon_name;
}

/**
 * pk_extra_get_exec:
 * @extra: a valid #PkExtra instance
 *
 * Return value: if we managed to get data
 **/
const gchar *
pk_extra_get_exec (PkExtra *extra, const gchar *package)
{
	PkExtraPackageObj *obj;

	g_return_val_if_fail (PK_IS_EXTRA (extra), NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* super quick if exists in cache */
	obj = g_hash_table_lookup (extra->priv->hash_package, package);
	if (obj == NULL) {
		return FALSE;
	}
	return obj->exec;
}

/**
 * pk_extra_set_data_locale:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
gboolean
pk_extra_set_data_locale (PkExtra *extra, const gchar *package, const gchar *summary)
{
	PkExtraLocaleObj *obj;
	gchar *statement;
	gchar *error_msg = NULL;
	sqlite3_stmt *sql_statement = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (summary != NULL, FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		egg_debug ("no database");
		return FALSE;
	}

	/* the row might already exist */
	statement = g_strdup_printf ("DELETE FROM localised WHERE "
				     "package = '%s' AND locale = '%s'",
				     package, extra->priv->locale);
	sqlite3_exec (extra->priv->db, statement, NULL, extra, NULL);
	g_free (statement);

	/* prepare the query, as we don't escape it */
	rc = sqlite3_prepare_v2 (extra->priv->db,
				 "INSERT INTO localised (package, locale, summary) "
				 "VALUES (?, ?, ?)", -1, &sql_statement, NULL);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL failed to prepare");
		return FALSE;
	}

	/* add data */
	sqlite3_bind_text (sql_statement, 1, package, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 2, extra->priv->locale, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 3, summary, -1, SQLITE_STATIC);

	/* save this */
	sqlite3_step (sql_statement);
	rc = sqlite3_finalize (sql_statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	/* add to cache */
	obj = g_new (PkExtraLocaleObj, 1);
	obj->summary = g_strdup (summary);
	g_hash_table_insert (extra->priv->hash_locale, g_strdup (package), (gpointer) obj);

	return TRUE;
}

/**
 * pk_extra_set_data_package:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
gboolean
pk_extra_set_data_package (PkExtra *extra, const gchar *package, const gchar *icon_name, const gchar *exec)
{
	PkExtraPackageObj *obj;
	gchar *statement;
	gchar *error_msg = NULL;
	sqlite3_stmt *sql_statement = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (icon_name != NULL || exec != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		egg_debug ("no database");
		return FALSE;
	}

	/* the row might already exist */
	statement = g_strdup_printf ("DELETE FROM data WHERE package = '%s'", package);
	sqlite3_exec (extra->priv->db, statement, NULL, extra, NULL);
	g_free (statement);

	/* prepare the query, as we don't escape it */
	rc = sqlite3_prepare_v2 (extra->priv->db, "INSERT INTO data (package, icon, exec) "
				 "VALUES (?, ?, ?)", -1, &sql_statement, NULL);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL failed to prepare");
		return FALSE;
	}

	/* add data */
	sqlite3_bind_text (sql_statement, 1, package, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 2, icon_name, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 3, exec, -1, SQLITE_STATIC);

	/* save this */
	sqlite3_step (sql_statement);
	rc = sqlite3_finalize (sql_statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	/* add to cache */
	egg_debug ("adding package:%s", package);
	obj = g_new (PkExtraPackageObj, 1);
	obj->icon_name = g_strdup (icon_name);
	obj->exec = g_strdup (exec);
	g_hash_table_insert (extra->priv->hash_package, g_strdup (package), (gpointer) obj);

	return TRUE;
}

/**
 * pk_extra_set_database:
 * @extra: a valid #PkExtra instance
 * @filename: a valid database
 *
 * Return value: %TRUE if set correctly
 **/
gboolean
pk_extra_set_database (PkExtra *extra, const gchar *filename)
{
	gboolean create_file;
	const gchar *statement;
	gint rc;
	gchar *error_msg = NULL;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);

	if (extra->priv->database != NULL) {
		egg_warning ("cannot assign extra than once");
		return FALSE;
	}

	/* if this is NULL, then assume default */
	if (filename == NULL) {
		filename = PK_EXTRA_DEFAULT_DATABASE_INTERNAL;
	}

	/* save for later */
	extra->priv->database = g_strdup (filename);

	/* if the database file was not installed (or was nuked) recreate it */
	create_file = g_file_test (filename, G_FILE_TEST_EXISTS);

	egg_debug ("trying to open database '%s'", filename);
	rc = sqlite3_open (filename, &extra->priv->db);
	if (rc) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (extra->priv->db));
		sqlite3_close (extra->priv->db);
		extra->priv->db = NULL;
		return FALSE;
	} else {
		if (create_file == FALSE) {
			statement = "CREATE TABLE localised ("
				    "id INTEGER PRIMARY KEY,"
				    "package TEXT,"
				    "locale TEXT,"
				    "summary TEXT);";
			rc = sqlite3_exec (extra->priv->db, statement, NULL, NULL, &error_msg);
			if (rc != SQLITE_OK) {
				egg_warning ("SQL error: %s\n", error_msg);
				sqlite3_free (error_msg);
			}
			statement = "CREATE TABLE data ("
				    "id INTEGER PRIMARY KEY,"
				    "package TEXT,"
				    "icon TEXT,"
				    "exec TEXT);";
			rc = sqlite3_exec (extra->priv->db, statement, NULL, NULL, &error_msg);
			if (rc != SQLITE_OK) {
				egg_warning ("SQL error: %s\n", error_msg);
				sqlite3_free (error_msg);
			}
		}
	}

	/* try to populate a working cache */
	pk_extra_populate_package_cache (extra);

	return TRUE;
}

/**
 * pk_free_locale_obj:
 **/
static void
pk_free_locale_obj (gpointer mem)
{
	PkExtraLocaleObj *obj = (PkExtraLocaleObj *) mem;
	g_free (obj->summary);
	g_free (obj);
}

/**
 * pk_free_package_obj:
 **/
static void
pk_free_package_obj (gpointer mem)
{
	PkExtraPackageObj *obj = (PkExtraPackageObj *) mem;
	g_free (obj->exec);
	g_free (obj->icon_name);
	g_free (obj);
}

/**
 * pk_extra_class_init:
 **/
static void
pk_extra_class_init (PkExtraClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_extra_finalize;
	g_type_class_add_private (klass, sizeof (PkExtraPrivate));
}

/**
 * pk_extra_init:
 **/
static void
pk_extra_init (PkExtra *extra)
{
	extra->priv = PK_EXTRA_GET_PRIVATE (extra);
	extra->priv->database = NULL;
	extra->priv->db = NULL;
	extra->priv->locale = NULL;
	extra->priv->locale_base = NULL;
	extra->priv->hash_package = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, pk_free_package_obj);
	extra->priv->hash_locale = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, pk_free_locale_obj);
}

/**
 * pk_extra_finalize:
 **/
static void
pk_extra_finalize (GObject *object)
{
	PkExtra *extra;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_EXTRA (object));
	extra = PK_EXTRA (object);
	g_return_if_fail (extra->priv != NULL);

	g_free (extra->priv->locale);
	g_free (extra->priv->locale_base);
	sqlite3_close (extra->priv->db);
	g_hash_table_destroy (extra->priv->hash_package);
	g_hash_table_destroy (extra->priv->hash_locale);

	G_OBJECT_CLASS (pk_extra_parent_class)->finalize (object);
}

/**
 * pk_extra_new:
 **/
PkExtra *
pk_extra_new (void)
{
	if (pk_extra_object != NULL) {
		g_object_ref (pk_extra_object);
	} else {
		pk_extra_object = g_object_new (PK_TYPE_EXTRA, NULL);
		g_object_add_weak_pointer (pk_extra_object, &pk_extra_object);
	}
	return PK_EXTRA (pk_extra_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>
#include <glib/gstdio.h>

void
egg_test_extra (EggTest *test)
{
	PkExtra *extra;
	const gchar *text;
	gboolean ret;
	const gchar *icon = NULL;
	const gchar *exec = NULL;
	const gchar *summary = NULL;
	guint i;

	if (!egg_test_start (test, "PkExtra"))
		return;

	g_unlink ("extra.db");

	/************************************************************/
	egg_test_title (test, "get extra");
	extra = pk_extra_new ();
	if (extra != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "set database");
	ret = pk_extra_set_database (extra, "extra.db");
	if (ret) {
		egg_test_success (test, "%ims", egg_test_elapsed (test));
	} else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "set database (again)");
	ret = pk_extra_set_database (extra, "angry.db");
	if (ret == FALSE) {
		egg_test_success (test, "%ims", egg_test_elapsed (test));
	} else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "set locale explicit en");
	ret = pk_extra_set_locale (extra, "en");
	if (ret) {
		egg_test_success (test, "%ims", egg_test_elapsed (test));
	} else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check locale base");
	if (extra->priv->locale_base == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "get locale");
	text = pk_extra_get_locale (extra);
	if (egg_strequal (text, "en"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "locale was %s", text);
	}

	/************************************************************/
	egg_test_title (test, "insert localised data");
	ret = pk_extra_set_data_locale (extra, "gnome-power-manager", "Power manager for the GNOME's desktop");
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed!");
	}

	/************************************************************/
	egg_test_title (test, "retrieve localised data");
	summary = pk_extra_get_summary (extra, "gnome-power-manager");
	if (summary != NULL) {
		egg_test_success (test, "%s", summary);
	} else {
		egg_test_failed (test, "failed!");
	}

	/************************************************************/
	egg_test_title (test, "set locale implicit en_GB");
	ret = pk_extra_set_locale (extra, "en_GB");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check locale base");
	if (egg_strequal (extra->priv->locale_base, "en"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "retrieve localised data2");
	summary = pk_extra_get_summary (extra, "gnome-power-manager");
	if (summary != NULL) {
		egg_test_success (test, "%s", summary);
	} else {
		egg_test_failed (test, "failed!");
	}

	/************************************************************/
	egg_test_title (test, "insert package data");
	ret = pk_extra_set_data_package (extra, "gnome-power-manager", "gpm-main.png", "gnome-power-manager");
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed!");
	}

	/************************************************************/
	egg_test_title (test, "retrieve package data");
	icon = pk_extra_get_icon_name (extra, "gnome-power-manager");
	exec = pk_extra_get_exec (extra, "gnome-power-manager");
	if (egg_strequal (icon, "gpm-main.png")) {
		egg_test_success (test, "%s:%s", icon, exec);
	} else {
		egg_test_failed (test, "%s:%s", icon, exec);
	}

	/************************************************************/
	egg_test_title (test, "insert new package data");
	ret = pk_extra_set_data_package (extra, "gnome-power-manager", "gpm-prefs.png", "gnome-power-preferences");
	if (ret)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed!");
	}

	/************************************************************/
	egg_test_title (test, "retrieve new package data");
	icon = pk_extra_get_icon_name (extra, "gnome-power-manager");
	exec = pk_extra_get_exec (extra, "gnome-power-manager");
	if (egg_strequal (icon, "gpm-prefs.png") &&
	    egg_strequal (exec, "gnome-power-preferences")) {
		egg_test_success (test, "%s:%s", icon, exec);
	} else {
		egg_test_failed (test, "%s:%s", icon, exec);
	}

	/************************************************************/
	egg_test_title (test, "retrieve missing package data");
	icon = pk_extra_get_icon_name (extra, "gnome-moo-manager");
	exec = pk_extra_get_exec (extra, "gnome-moo-manager");
	if (icon == NULL && exec == NULL) {
		egg_test_success (test, "passed");
	} else {
		egg_test_failed (test, "%s:%s", icon, exec);
	}

	/************************************************************/
	egg_test_title (test, "do lots of loops");
	for (i=0;i<250;i++) {
		summary = pk_extra_get_summary (extra, "gnome-power-manager");
		if (summary == NULL) {
			egg_test_failed (test, "failed to get good!");
		}
		summary = pk_extra_get_summary (extra, "gnome-moo-manager");
		if (summary != NULL) {
			egg_test_failed (test, "failed to not get bad 2!");
		}
		summary = pk_extra_get_summary (extra, "gnome-moo-manager");
		if (summary != NULL) {
			egg_test_failed (test, "failed to not get bad 3!");
		}
		summary = pk_extra_get_summary (extra, "gnome-moo-manager");
		if (summary != NULL) {
			egg_test_failed (test, "failed to not get bad 4!");
		}
	}
	egg_test_success (test, "%i get_summary loops completed in %ims", i*5, egg_test_elapsed (test));

	g_object_unref (extra);
	g_unlink ("extra.db");

	egg_test_end (test);
}
#endif

