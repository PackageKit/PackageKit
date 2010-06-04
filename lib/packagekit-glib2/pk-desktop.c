/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-desktop
 * @short_description: Find desktop metadata about a package
 *
 * Desktop metadata such as icon name and localised summary may be stored in
 * a local sqlite cache, and this module allows applications to query this.
 */

#include "config.h"

#include <glib.h>
#include <sqlite3.h>
#include <packagekit-glib2/pk-desktop.h>

#include "egg-debug.h"
#include "egg-string.h"

static void     pk_desktop_finalize	(GObject        *object);

#define PK_DESKTOP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_DESKTOP, PkDesktopPrivate))

/* Database format is:
 *   CREATE TABLE cache ( filename TEXT, package TEXT, show INTEGER, md5 TEXT );
 */

/**
 * PkDesktopPrivate:
 *
 * Private #PkDesktop data
 **/
struct _PkDesktopPrivate
{
	sqlite3			*db;
};

G_DEFINE_TYPE (PkDesktop, pk_desktop, G_TYPE_OBJECT)
static gpointer pk_desktop_object = NULL;

/**
 * pk_desktop_sqlite_filename_cb:
 **/
static gint
pk_desktop_sqlite_filename_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	GPtrArray *array = (GPtrArray *) data;
	gint i;

	/* add the filename data to the array */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "filename") == 0 && argv[i] != NULL)
			g_ptr_array_add (array, g_strdup (argv[i]));
	}

	return 0;
}

/**
 * pk_desktop_sqlite_package_cb:
 **/
static gint
pk_desktop_sqlite_package_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gchar **package = (gchar **) data;
	gint i;

	/* add the filename data to the array */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "package") == 0 && argv[i] != NULL)
			*package = g_strdup (argv[i]);
	}

	return 0;
}

/**
 * pk_desktop_get_files_for_package:
 * @desktop: a valid #PkDesktop instance
 * @package: the package name, e.g. "gnome-power-manager"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return all desktop files owned by a package, regardless if they are shown
 * in the main menu or not.
 *
 * Return value: string array of results, free with g_ptr_array_unref()
 *
 * Since: 0.5.3
 **/
GPtrArray *
pk_desktop_get_files_for_package (PkDesktop *desktop, const gchar *package, GError **error)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	GPtrArray *array = NULL;

	g_return_val_if_fail (PK_IS_DESKTOP (desktop), NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* no database */
	if (desktop->priv->db == NULL) {
		g_set_error_literal (error, 1, 0, "database is not open");
		goto out;
	}

	/* get packages */
	array = g_ptr_array_new_with_free_func (g_free);
	statement = g_strdup_printf ("SELECT filename FROM cache WHERE package = '%s'", package);
	rc = sqlite3_exec (desktop->priv->db, statement, pk_desktop_sqlite_filename_cb, array, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
	}
out:
	return array;
}

/**
 * pk_desktop_get_shown_for_package:
 * @desktop: a valid #PkDesktop instance
 * @package: the package name, e.g. "gnome-power-manager"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return all desktop files owned by a package that would be shown in a menu,
 * i.e are an application
 *
 * Return value: string array of results, free with g_ptr_array_unref()
 *
 * Since: 0.5.3
 **/
GPtrArray *
pk_desktop_get_shown_for_package (PkDesktop *desktop, const gchar *package, GError **error)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	GPtrArray *array = NULL;

	g_return_val_if_fail (PK_IS_DESKTOP (desktop), NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* no database */
	if (desktop->priv->db == NULL) {
		g_set_error_literal (error, 1, 0, "database is not open");
		goto out;
	}

	/* get packages */
	array = g_ptr_array_new_with_free_func (g_free);
	statement = g_strdup_printf ("SELECT filename FROM cache WHERE package = '%s' AND show = 1", package);
	rc = sqlite3_exec (desktop->priv->db, statement, pk_desktop_sqlite_filename_cb, array, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
	}
out:
	return array;
}

/**
 * pk_desktop_get_package_for_file:
 * @desktop: a valid #PkDesktop instance
 * @filename: a fully qualified filename
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Returns the package name that owns the desktop file. Fast.
 *
 * Return value: package name, or %NULL
 *
 * Since: 0.5.3
 **/
gchar *
pk_desktop_get_package_for_file (PkDesktop *desktop, const gchar *filename, GError **error)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	gchar *package = NULL;

	g_return_val_if_fail (PK_IS_DESKTOP (desktop), NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	/* no database */
	if (desktop->priv->db == NULL) {
		g_set_error_literal (error, 1, 0, "database is not open");
		goto out;
	}

	/* get packages */
	statement = g_strdup_printf ("SELECT package FROM cache WHERE filename = '%s' LIMIT 1", filename);
	rc = sqlite3_exec (desktop->priv->db, statement, pk_desktop_sqlite_package_cb, &package, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
	}

	/* no result */
	if (package == NULL) {
		g_set_error (error, 1, 0, "could not find package for %s", filename);
		goto out;
	}
out:
	return package;
}

/**
 * pk_desktop_open_database:
 * @desktop: a valid #PkDesktop instance
 *
 * Return value: %TRUE if opened correctly
 *
 * Since: 0.5.3
 **/
gboolean
pk_desktop_open_database (PkDesktop *desktop, GError **error)
{
	gboolean ret;
	gint rc;

	g_return_val_if_fail (PK_IS_DESKTOP (desktop), FALSE);

	/* already opened */
	if (desktop->priv->db != NULL)
		return TRUE;

	/* if the database file was not installed (or was nuked) recreate it */
	ret = g_file_test (PK_DESKTOP_DEFAULT_DATABASE, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error (error, 1, 0, "database %s is not present", PK_DESKTOP_DEFAULT_DATABASE);
		return FALSE;
	}

	egg_debug ("trying to open database '%s'", PK_DESKTOP_DEFAULT_DATABASE);
	rc = sqlite3_open (PK_DESKTOP_DEFAULT_DATABASE, &desktop->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (desktop->priv->db));
		g_set_error (error, 1, 0, "can't open database: %s", sqlite3_errmsg (desktop->priv->db));
		sqlite3_close (desktop->priv->db);
		desktop->priv->db = NULL;
		return FALSE;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (desktop->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);

	return TRUE;
}

/**
 * pk_desktop_class_init:
 **/
static void
pk_desktop_class_init (PkDesktopClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_desktop_finalize;
	g_type_class_add_private (klass, sizeof (PkDesktopPrivate));
}

/**
 * pk_desktop_init:
 **/
static void
pk_desktop_init (PkDesktop *desktop)
{
	desktop->priv = PK_DESKTOP_GET_PRIVATE (desktop);
}

/**
 * pk_desktop_finalize:
 **/
static void
pk_desktop_finalize (GObject *object)
{
	PkDesktop *desktop;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_DESKTOP (object));
	desktop = PK_DESKTOP (object);
	g_return_if_fail (desktop->priv != NULL);

	sqlite3_close (desktop->priv->db);

	G_OBJECT_CLASS (pk_desktop_parent_class)->finalize (object);
}

/**
 * pk_desktop_new:
 *
 * Since: 0.5.3
 **/
PkDesktop *
pk_desktop_new (void)
{
	if (pk_desktop_object != NULL) {
		g_object_ref (pk_desktop_object);
	} else {
		pk_desktop_object = g_object_new (PK_TYPE_DESKTOP, NULL);
		g_object_add_weak_pointer (pk_desktop_object, &pk_desktop_object);
	}
	return PK_DESKTOP (pk_desktop_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_desktop_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkDesktop *desktop;
	gboolean ret;
	gchar *package;
	GPtrArray *array;
	GError *error = NULL;

	if (!egg_test_start (test, "PkDesktop"))
		return;

	/************************************************************/
	egg_test_title (test, "get desktop");
	desktop = pk_desktop_new ();
	egg_test_assert (test, desktop != NULL);

	/************************************************************/
	egg_test_title (test, "get package when not valid");
	package = pk_desktop_get_package_for_file (desktop, "/usr/share/applications/gpk-update-viewer.desktop", NULL);
	egg_test_assert (test, package == NULL);

	/* file does not exist */
	ret = g_file_test (PK_DESKTOP_DEFAULT_DATABASE, G_FILE_TEST_EXISTS);
	if (!ret) {
		egg_warning ("skipping checks as database does not exist");
		goto out;
	}

	/************************************************************/
	egg_test_title (test, "open database");
	ret = pk_desktop_open_database (desktop, &error);
	if (ret)
		egg_test_success (test, "%ims", egg_test_elapsed (test));
	else
		egg_test_failed (test, "failed to open: %s", error->message);

	/************************************************************/
	egg_test_title (test, "get package");
	package = pk_desktop_get_package_for_file (desktop, "/usr/share/applications/gpk-update-viewer.desktop", NULL);

	/* dummy, not yum */
	if (g_strcmp0 (package, "vips-doc") == 0) {
		egg_test_success (test, "created db with dummy, skipping remaining tests");
		goto out;
	}
	if (g_strcmp0 (package, "gnome-packagekit") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "package was: %s", package);
	g_free (package);

	/************************************************************/
	egg_test_title (test, "get files");
	array = pk_desktop_get_files_for_package (desktop, "gnome-packagekit", NULL);
	if (array == NULL)
		egg_test_failed (test, "array NULL");
	else if (array->len >= 5)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "length=%i", array->len);
	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "get shown files");
	array = pk_desktop_get_shown_for_package (desktop, "gnome-packagekit", NULL);
	if (array == NULL)
		egg_test_failed (test, "array NULL");
	else if (array->len > 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "length=%i", array->len);
	g_ptr_array_unref (array);
out:
	g_object_unref (desktop);

	egg_test_end (test);
}
#endif

