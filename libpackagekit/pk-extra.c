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

#include "pk-extra.h"
#include "pk-common.h"
#include "pk-debug.h"

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
	gchar			*icon;
	gchar			*exec;
	gchar			*summary;
	sqlite3			*db;
	GHashTable		*hash_locale;
	GHashTable		*hash_package;
};

G_DEFINE_TYPE (PkExtra, pk_extra, G_TYPE_OBJECT)
static gpointer pk_extra_object = NULL;

/**
 * pk_extra_populate_package_cache_callback:
 **/
static gint
pk_extra_populate_package_cache_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkExtra *extra = PK_EXTRA (data);
	gint i;
	gchar *col;
	gchar *value;

	g_return_val_if_fail (PK_IS_EXTRA (extra), 0);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		/* just insert it, as we match on the package */
		if (pk_strequal (col, "package") && value != NULL) {
			g_hash_table_insert (extra->priv->hash_package, g_strdup (value), GUINT_TO_POINTER (1));
		}
	}
	return 0;
}

/**
 * pk_extra_populate_locale_cache_callback:
 **/
static gint
pk_extra_populate_locale_cache_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkExtra *extra = PK_EXTRA (data);
	gint i;
	gchar *col;
	gchar *value;
	gchar **package = NULL;
	gchar **summary = NULL;

	g_return_val_if_fail (PK_IS_EXTRA (extra), 0);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		/* save the package name, and use it is the key */
		if (pk_strequal (col, "package") && value != NULL) {
			package = &argv[i];
		} else if (pk_strequal (col, "summary") && value != NULL) {
			summary = &argv[i];
		}
	}

	/* only when both non-NULL */
	if (package != NULL && summary != NULL) {
		g_hash_table_insert (extra->priv->hash_locale, g_strdup (*package), GUINT_TO_POINTER (1));
	}

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
	const gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		pk_debug ("no database");
		return FALSE;
	}

	/* get summary packages */
	statement = "SELECT package, summary FROM localised";
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_populate_locale_cache_callback, extra, &error_msg);
	if (rc != SQLITE_OK) {
		pk_warning ("SQL error: %s\n", error_msg);
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
		pk_debug ("no database");
		return FALSE;
	}

	/* get packages */
	statement = "SELECT package FROM data";
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_populate_package_cache_callback, extra, &error_msg);
	if (rc != SQLITE_OK) {
		pk_warning ("SQL error: %s\n", error_msg);
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
	len = strlen (locale);
	if (len > 10) {
		pk_warning ("locale really long (%i), truncating to 10", len);
		len = 10;
	}
	for (i=0; i<len; i++) {
		if (extra->priv->locale_base[i] == '_') {
			extra->priv->locale_base[i] = '\0';
			pk_debug ("locale_base is '%s'", extra->priv->locale_base);
			break;
		}
	}

	/* no point doing it twice if they are the same */
	if (pk_strequal (extra->priv->locale_base, extra->priv->locale)) {
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
 * pk_extra_detail_localised_callback:
 **/
static gint
pk_extra_detail_localised_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkExtra *extra = PK_EXTRA (data);
	gint i;
	gchar *col;
	gchar *value;

	g_return_val_if_fail (PK_IS_EXTRA (extra), 0);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (pk_strequal (col, "summary")) {
			g_free (extra->priv->summary);
			extra->priv->summary = g_strdup (value);
		} else {
			pk_warning ("%s = %s\n", col, value);
		}
	}
	return 0;
}

/**
 * pk_extra_get_localised_detail_try:
 * @extra: a valid #PkExtra instance
 *
 * TODO: This function is HOT in the profile chart
 *
 * Return value: the current locale
 **/
static gboolean
pk_extra_get_localised_detail_try (PkExtra *extra, const gchar *package, const gchar *locale)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;

	statement = g_strdup_printf ("SELECT summary FROM localised "
				     "WHERE package = '%s' AND locale = '%s'",
				     package, locale);
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_detail_localised_callback, extra, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		pk_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_extra_get_localised_detail:
 * @extra: a valid #PkExtra instance
 *
 * Return value: if we managed to get data
 **/
gboolean
pk_extra_get_localised_detail (PkExtra *extra, const gchar *package, gchar **summary)
{
	gpointer value;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (summary != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		pk_debug ("no database");
		return FALSE;
	}

	/* can we optimize the call */
	value = g_hash_table_lookup (extra->priv->hash_locale, package);
	if (value == NULL) {
		return FALSE;
	}

	/* try with default locale */
	pk_extra_get_localised_detail_try (extra, package, extra->priv->locale);

	/* try harder with a base locale */
	if (extra->priv->summary == NULL && extra->priv->locale_base != NULL) {
		pk_extra_get_localised_detail_try (extra, package, extra->priv->locale_base);
	}

	/* don't copy and g_free, just re-assign */
	if (extra->priv->summary != NULL) {
		*summary = extra->priv->summary;
		extra->priv->summary = NULL;
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_extra_detail_package_callback:
 **/
static gint
pk_extra_detail_package_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkExtra *extra = PK_EXTRA (data);
	gint i;
	gchar *col;
	gchar *value;

	g_return_val_if_fail (PK_IS_EXTRA (extra), 0);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (pk_strequal (col, "exec")) {
			g_free (extra->priv->exec);
			extra->priv->exec = g_strdup (value);
		} else if (pk_strequal (col, "icon")) {
			g_free (extra->priv->icon);
			extra->priv->icon = g_strdup (value);
		} else {
			pk_warning ("%s = %s\n", col, value);
		}
	}
	return 0;
}

/**
 * pk_extra_get_package_detail:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
gboolean
pk_extra_get_package_detail (PkExtra *extra, const gchar *package, gchar **icon, gchar **exec)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	gpointer value;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		pk_debug ("no database");
		return FALSE;
	}

	/* can we optimize the call */
	value = g_hash_table_lookup (extra->priv->hash_package, package);
	if (value == NULL) {
		return FALSE;
	}

	statement = g_strdup_printf ("SELECT icon, exec FROM data WHERE package = '%s'", package);
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_detail_package_callback, extra, &error_msg);
	if (rc != SQLITE_OK) {
		pk_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	g_free (statement);

	/* report back */
	if (icon != NULL) {
		*icon = extra->priv->icon;
	} else {
		g_free (extra->priv->icon);
	}
	if (exec != NULL) {
		*exec = extra->priv->exec;
	} else {
		g_free (extra->priv->exec);
	}

	/* did we fail to get both? */
	if (extra->priv->icon == NULL &&
	    extra->priv->exec == NULL) {
		return FALSE;
	}

	/* reset */
	extra->priv->icon = NULL;
	extra->priv->exec = NULL;
	return TRUE;
}

/**
 * pk_extra_set_localised_detail:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
gboolean
pk_extra_set_localised_detail (PkExtra *extra, const gchar *package, const gchar *summary)
{
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
		pk_debug ("no database");
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
		pk_warning ("SQL failed to prepare");
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
		pk_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	/* add to cache */
	pk_debug ("adding summary:%s", package);
	g_hash_table_insert (extra->priv->hash_locale, g_strdup (package), GUINT_TO_POINTER (1));

	return TRUE;
}

/**
 * pk_extra_set_package_detail:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
gboolean
pk_extra_set_package_detail (PkExtra *extra, const gchar *package, const gchar *icon, const gchar *exec)
{
	gchar *statement;
	gchar *error_msg = NULL;
	sqlite3_stmt *sql_statement = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (icon != NULL || exec != NULL, FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);

	/* we failed to open */
	if (extra->priv->db == NULL) {
		pk_debug ("no database");
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
		pk_warning ("SQL failed to prepare");
		return FALSE;
	}

	/* add data */
	sqlite3_bind_text (sql_statement, 1, package, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 2, icon, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 3, exec, -1, SQLITE_STATIC);

	/* save this */
	sqlite3_step (sql_statement);
	rc = sqlite3_finalize (sql_statement);
	if (rc != SQLITE_OK) {
		pk_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	/* add to cache */
	pk_debug ("adding package:%s", package);
	g_hash_table_insert (extra->priv->hash_package, g_strdup (package), GUINT_TO_POINTER (1));

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
		pk_warning ("cannot assign extra than once");
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

	pk_debug ("trying to open database '%s'", filename);
	rc = sqlite3_open (filename, &extra->priv->db);
	if (rc) {
		pk_warning ("Can't open database: %s\n", sqlite3_errmsg (extra->priv->db));
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
				pk_warning ("SQL error: %s\n", error_msg);
				sqlite3_free (error_msg);
			}
			statement = "CREATE TABLE data ("
				    "id INTEGER PRIMARY KEY,"
				    "package TEXT,"
				    "icon TEXT,"
				    "exec TEXT);";
			rc = sqlite3_exec (extra->priv->db, statement, NULL, NULL, &error_msg);
			if (rc != SQLITE_OK) {
				pk_warning ("SQL error: %s\n", error_msg);
				sqlite3_free (error_msg);
			}
		}
	}

	/* try to populate a working cache */
	pk_extra_populate_package_cache (extra);

	return TRUE;
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
	extra->priv->icon = NULL;
	extra->priv->exec = NULL;
	extra->priv->summary = NULL;
	extra->priv->hash_package = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	extra->priv->hash_locale = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
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

	g_free (extra->priv->icon);
	g_free (extra->priv->exec);
	g_free (extra->priv->summary);
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
libst_extra (LibSelfTest *test)
{
	PkExtra *extra;
	const gchar *text;
	gboolean ret;
	gchar *icon = NULL;
	gchar *exec = NULL;
	gchar *summary = NULL;
	guint i;

	if (libst_start (test, "PkExtra", CLASS_AUTO) == FALSE) {
		return;
	}

	g_unlink ("extra.db");

	/************************************************************/
	libst_title (test, "get extra");
	extra = pk_extra_new ();
	if (extra != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set database");
	ret = pk_extra_set_database (extra, "extra.db");
	if (ret) {
		libst_success (test, "%ims", libst_elapsed (test));
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set database (again)");
	ret = pk_extra_set_database (extra, "angry.db");
	if (ret == FALSE) {
		libst_success (test, "%ims", libst_elapsed (test));
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set locale explicit en");
	ret = pk_extra_set_locale (extra, "en");
	if (ret) {
		libst_success (test, "%ims", libst_elapsed (test));
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check locale base");
	if (extra->priv->locale_base == NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get locale");
	text = pk_extra_get_locale (extra);
	if (pk_strequal (text, "en")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "locale was %s", text);
	}

	/************************************************************/
	libst_title (test, "insert localised data");
	ret = pk_extra_set_localised_detail (extra, "gnome-power-manager",
					     "Power manager for the GNOME's desktop");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "retrieve localised data");
	ret = pk_extra_get_localised_detail (extra, "gnome-power-manager", &summary);
	if (ret && summary != NULL) {
		libst_success (test, "%s", summary);
	} else {
		libst_failed (test, "failed!");
	}
	g_free (summary);
	summary = NULL;

	/************************************************************/
	libst_title (test, "set locale implicit en_GB");
	ret = pk_extra_set_locale (extra, "en_GB");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check locale base");
	if (pk_strequal (extra->priv->locale_base, "en")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "retrieve localised data");
	ret = pk_extra_get_localised_detail (extra, "gnome-power-manager", &summary);
	if (ret && summary != NULL) {
		libst_success (test, "%s", summary);
	} else {
		libst_failed (test, "failed!");
	}
	g_free (summary);
	summary = NULL;

	/************************************************************/
	libst_title (test, "insert package data");
	ret = pk_extra_set_package_detail (extra, "gnome-power-manager", "gpm-main.png", "gnome-power-manager");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "retrieve package data");
	ret = pk_extra_get_package_detail (extra, "gnome-power-manager", &icon, &exec);
	if (ret &&
	    pk_strequal (icon, "gpm-main.png") &&
	    pk_strequal (exec, "gnome-power-manager")) {
		libst_success (test, "%s:%s", icon, exec);
	} else {
		libst_failed (test, "%s:%s", icon, exec);
	}
	g_free (icon);
	g_free (exec);
	icon = NULL;
	exec = NULL;

	/************************************************************/
	libst_title (test, "insert new package data");
	ret = pk_extra_set_package_detail (extra, "gnome-power-manager", "gpm-prefs.png", "gnome-power-preferences");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "retrieve new package data");
	ret = pk_extra_get_package_detail (extra, "gnome-power-manager", &icon, &exec);
	if (ret &&
	    pk_strequal (icon, "gpm-prefs.png") &&
	    pk_strequal (exec, "gnome-power-preferences")) {
		libst_success (test, "%s:%s", icon, exec);
	} else {
		libst_failed (test, "%s:%s", icon, exec);
	}
	g_free (icon);
	g_free (exec);
	icon = NULL;
	exec = NULL;

	/************************************************************/
	libst_title (test, "retrieve missing package data");
	ret = pk_extra_get_package_detail (extra, "gnome-moo-manager", &icon, &exec);
	if (!ret && icon == NULL && exec == NULL) {
		libst_success (test, "passed");
	} else {
		libst_failed (test, "%s:%s", icon, exec);
	}

	/************************************************************/
	libst_title (test, "do lots of loops");
	for (i=0;i<250;i++) {
		ret = pk_extra_get_localised_detail (extra, "gnome-power-manager", &summary);
		if (!ret || summary == NULL) {
			libst_failed (test, "failed to get good!");
		}
		g_free (summary);
		summary = NULL;
		ret = pk_extra_get_localised_detail (extra, "gnome-moo-manager", &summary);
		if (ret || summary != NULL) {
			libst_failed (test, "failed to not get bad 1, %i, %s!", ret, summary);
		}
		ret = pk_extra_get_localised_detail (extra, "gnome-moo-manager", &summary);
		if (ret || summary != NULL) {
			libst_failed (test, "failed to not get bad 2!");
		}
		ret = pk_extra_get_localised_detail (extra, "gnome-moo-manager", &summary);
		if (ret || summary != NULL) {
			libst_failed (test, "failed to not get bad 3!");
		}
		ret = pk_extra_get_localised_detail (extra, "gnome-moo-manager", &summary);
		if (ret || summary != NULL) {
			libst_failed (test, "failed to not get bad 4!");
		}
	}
	libst_success (test, "%i get_localised_detail loops completed in %ims", i*5, libst_elapsed (test));

	g_object_unref (extra);
	g_unlink ("extra.db");

	libst_end (test);
}
#endif

