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
struct PkExtraPrivate
{
	gchar			*database;
	gchar			*locale;
	gchar			*icon;
	gchar			*exec;
	gchar			*summary;
	sqlite3			*db;
};

G_DEFINE_TYPE (PkExtra, pk_extra, G_TYPE_OBJECT)
static gpointer pk_extra_object = NULL;

/**
 * pk_extra_set_locale:
 * @extra: a valid #PkExtra instance
 * @tid: a transaction id
 *
 * Return value: %TRUE if set correctly
 **/
gboolean
pk_extra_set_locale (PkExtra *extra, const gchar *locale)
{
	g_free (extra->priv->locale);
	extra->priv->locale = g_strdup (locale);
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

	g_return_val_if_fail (extra != NULL, 0);
	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (pk_strequal (col, "summary") == TRUE) {
			extra->priv->summary = g_strdup (value);
		} else if (pk_strequal (col, "locale") == TRUE) {
			pk_debug ("locale: %s", value);
		} else if (pk_strequal (col, "package") == TRUE) {
			pk_debug ("package: %s", value);
		} else {
			pk_warning ("%s = %s\n", col, value);
		}
	}
	return 0;
}

/**
 * pk_extra_get_localised_detail:
 * @extra: a valid #PkExtra instance
 *
 * Return value: the current locale
 **/
gboolean
pk_extra_get_localised_detail (PkExtra *extra, const gchar *package, gchar **summary)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (extra != NULL, FALSE);
	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);
	g_return_val_if_fail (extra->priv->database != NULL, FALSE);

	/* do we have a connection */
	if (extra->priv->db == NULL) {
		pk_debug ("no database connection");
		return FALSE;
	}

	statement = g_strdup_printf ("SELECT package, summary, locale FROM localised "
				     "WHERE package = '%s' AND locale = '%s'",
				     package, extra->priv->locale);
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_detail_localised_callback, extra, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		pk_error ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	if (summary != NULL) {
		*summary = extra->priv->summary;
	} else {
		g_free (extra->priv->summary);
	}
	extra->priv->summary = NULL;
	return TRUE;
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

	g_return_val_if_fail (extra != NULL, 0);
	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (pk_strequal (col, "exec") == TRUE) {
			extra->priv->exec = g_strdup (value);
		} else if (pk_strequal (col, "icon") == TRUE) {
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

	g_return_val_if_fail (extra != NULL, FALSE);
	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);
	g_return_val_if_fail (extra->priv->database != NULL, FALSE);

	/* do we have a connection */
	if (extra->priv->db == NULL) {
		pk_debug ("no database connection");
		return FALSE;
	}

	statement = g_strdup_printf ("SELECT icon, exec FROM data WHERE package = '%s'", package);
	rc = sqlite3_exec (extra->priv->db, statement, pk_extra_detail_package_callback, extra, &error_msg);
	if (rc != SQLITE_OK) {
		pk_error ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

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
	extra->priv->icon = NULL;
	extra->priv->exec = NULL;
	g_free (statement);
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

	g_return_val_if_fail (extra != NULL, FALSE);
	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);
	g_return_val_if_fail (extra->priv->database != NULL, FALSE);

	/* do we have a connection */
	if (extra->priv->db == NULL) {
		pk_debug ("no database connection");
		return FALSE;
	}

	/* the row might already exist */
	statement = g_strdup_printf ("DELETE FROM localised WHERE "
				     "package = '%s' AND locale = '%s'",
				     package, extra->priv->locale);
//	sqlite3_exec (extra->priv->db, statement, NULL, extra, NULL);
	g_free (statement);

	/* prepare the query, as we don't escape it */
	rc = sqlite3_prepare_v2 (extra->priv->db,
				 "INSERT INTO localised (package, locale, summary) "
				 "VALUES (?, ?, ?)", -1, &sql_statement, NULL);
	if (rc != SQLITE_OK) {
		pk_error ("SQL failed to prepare");
		return FALSE;
	}

	/* add data */
	sqlite3_bind_text(sql_statement, 1, package, -1, SQLITE_STATIC);
	sqlite3_bind_text(sql_statement, 2, extra->priv->locale, -1, SQLITE_STATIC);
	sqlite3_bind_text(sql_statement, 3, summary, -1, SQLITE_STATIC);

	/* save this */
	sqlite3_step(sql_statement);
	rc = sqlite3_finalize(sql_statement);
	if (rc != SQLITE_OK) {
		pk_error ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

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

	g_return_val_if_fail (extra != NULL, FALSE);
	g_return_val_if_fail (PK_IS_EXTRA (extra), FALSE);
	g_return_val_if_fail (extra->priv->locale != NULL, FALSE);
	g_return_val_if_fail (extra->priv->database != NULL, FALSE);

	/* do we have a connection */
	if (extra->priv->db == NULL) {
		pk_debug ("no database connection");
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
		pk_error ("SQL failed to prepare");
		return FALSE;
	}

	/* add data */
	sqlite3_bind_text(sql_statement, 1, package, -1, SQLITE_STATIC);
	sqlite3_bind_text(sql_statement, 2, icon, -1, SQLITE_STATIC);
	sqlite3_bind_text(sql_statement, 3, exec, -1, SQLITE_STATIC);

	/* save this */
	sqlite3_step(sql_statement);
	rc = sqlite3_finalize(sql_statement);
	if (rc != SQLITE_OK) {
		pk_error ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_extra_set_database:
 * @extra: a valid #PkExtra instance
 * @tid: a transaction id
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

	g_return_val_if_fail (extra != NULL, FALSE);
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
			rc = sqlite3_exec (extra->priv->db, statement, NULL, 0, &error_msg);
			if (rc != SQLITE_OK) {
				pk_error ("SQL error: %s\n", error_msg);
				sqlite3_free (error_msg);
			}
			statement = "CREATE TABLE data ("
				    "id INTEGER PRIMARY KEY,"
				    "package TEXT,"
				    "icon TEXT,"
				    "exec TEXT);";
			rc = sqlite3_exec (extra->priv->db, statement, NULL, 0, &error_msg);
			if (rc != SQLITE_OK) {
				pk_error ("SQL error: %s\n", error_msg);
				sqlite3_free (error_msg);
			}
		}
	}
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
	extra->priv->locale = NULL;
	extra->priv->icon = NULL;
	extra->priv->exec = NULL;
	extra->priv->summary = NULL;
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
	sqlite3_close (extra->priv->db);

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
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set database (again)");
	ret = pk_extra_set_database (extra, "angry.db");
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "set locale");
	ret = pk_extra_set_locale (extra, "en_GB");
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get locale");
	text = pk_extra_get_locale (extra);
	if (pk_strequal (text, "en_GB") == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "locale was %s", text);
	}

	gchar *icon;
	gchar *exec;
	gchar *summary;

	/************************************************************/
	libst_title (test, "insert localised data");
	ret = pk_extra_set_localised_detail (extra, "gnome-power-manager",
					     "Power manager for the GNOME's desktop");
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "retrieve localised data");
	ret = pk_extra_get_localised_detail (extra, "gnome-power-manager", &summary);
	if (ret == TRUE) {
		libst_success (test, "%s", summary);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "insert package data");
	ret = pk_extra_set_package_detail (extra, "gnome-power-manager", "gpm-main.png", "gnome-power-manager");
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "retrieve package data");
	ret = pk_extra_get_package_detail (extra, "gnome-power-manager", &icon, &exec);
	if (ret == TRUE &&
	    pk_strequal (icon, "gpm-main.png") == TRUE &&
	    pk_strequal (exec, "gnome-power-manager") == TRUE) {
		libst_success (test, "%s:%s", icon, exec);
	} else {
		libst_failed (test, "%s:%s", icon, exec);
	}

	/************************************************************/
	libst_title (test, "insert new package data");
	ret = pk_extra_set_package_detail (extra, "gnome-power-manager", "gpm-prefs.png", "gnome-power-preferences");
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed!");
	}

	/************************************************************/
	libst_title (test, "retrieve new package data");
	ret = pk_extra_get_package_detail (extra, "gnome-power-manager", &icon, &exec);
	if (ret == TRUE &&
	    pk_strequal (icon, "gpm-prefs.png") == TRUE &&
	    pk_strequal (exec, "gnome-power-preferences") == TRUE) {
		libst_success (test, "%s:%s", icon, exec);
	} else {
		libst_failed (test, "%s:%s", icon, exec);
	}

	/************************************************************/
	libst_title (test, "retrieve missing package data");
	ret = pk_extra_get_package_detail (extra, "gnome-moo-manager", &icon, &exec);
	if (ret == TRUE && icon == NULL && exec == NULL) {
		libst_success (test, "passed");
	} else {
		libst_failed (test, "%s:%s", icon, exec);
	}

	g_object_unref (extra);

	libst_end (test);
}
#endif

