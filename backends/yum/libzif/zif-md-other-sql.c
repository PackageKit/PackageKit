/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-md-other-sql
 * @short_description: Other metadata functionality
 *
 * Provide access to the other repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "zif-md.h"
#include "zif-changeset.h"
#include "zif-md-other-sql.h"
#include "zif-package-remote.h"

#include "egg-debug.h"

#define ZIF_MD_OTHER_SQL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_OTHER_SQL, ZifMdOtherSqlPrivate))

/**
 * ZifMdOtherSqlPrivate:
 *
 * Private #ZifMdOtherSql data
 **/
struct _ZifMdOtherSqlPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

G_DEFINE_TYPE (ZifMdOtherSql, zif_md_other_sql, ZIF_TYPE_MD)

/**
 * zif_md_other_sql_unload:
 **/
static gboolean
zif_md_other_sql_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_other_sql_load:
 **/
static gboolean
zif_md_other_sql_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	const gchar *filename;
	gint rc;
	ZifMdOtherSql *other_sql = ZIF_MD_OTHER_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_OTHER_SQL (md), FALSE);

	/* already loaded */
	if (other_sql->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for other_sql");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &other_sql->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (other_sql->priv->db));
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "can't open database: %s", sqlite3_errmsg (other_sql->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (other_sql->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	other_sql->priv->loaded = TRUE;
out:
	return other_sql->priv->loaded;
}

/**
 * zif_md_other_sql_sqlite_create_changelog_cb:
 **/
static gint
zif_md_other_sql_sqlite_create_changelog_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	GPtrArray *array = (GPtrArray *) data;
	ZifChangeset *changeset;
	gint i;
	guint64 date = 0;
	const gchar *author = NULL;
	const gchar *changelog = NULL;
	gboolean ret;
	GError *error = NULL;
	gchar *endptr = NULL;

	/* get the ID */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "date") == 0) {
			date = g_ascii_strtoull (argv[i], &endptr, 10);
			if (argv[i] == endptr)
				egg_warning ("failed to parse date %s", argv[i]);
		} else if (g_strcmp0 (col_name[i], "author") == 0) {
			author = argv[i];
		} else if (g_strcmp0 (col_name[i], "changelog") == 0) {
			changelog = argv[i];
		} else {
			egg_warning ("unrecognized: %s=%s", col_name[i], argv[i]);
		}
	}

	/* create new object */
	changeset = zif_changeset_new ();
	zif_changeset_set_date (changeset, date);
	zif_changeset_set_description (changeset, changelog);
	ret = zif_changeset_parse_header (changeset, author, &error);
	if (!ret) {
		egg_warning ("failed to parse header: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* add to the array */
	g_ptr_array_add (array, g_object_ref (changeset));
out:
	g_object_unref (changeset);
	return 0;
}

/**
 * zif_md_other_sql_search_pkgkey:
 **/
static GPtrArray *
zif_md_other_sql_search_pkgkey (ZifMdOtherSql *md, guint pkgkey,
			        GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	GPtrArray *array = NULL;

	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	statement = g_strdup_printf ("SELECT author, date, changelog FROM changelog WHERE pkgKey = '%i' ORDER BY date DESC", pkgkey);
	rc = sqlite3_exec (md->priv->db, statement, zif_md_other_sql_sqlite_create_changelog_cb, array, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		g_ptr_array_unref (array);
		array = NULL;
		goto out;
	}
out:
	g_free (statement);
	return array;
}

/**
 * zif_md_other_sql_sqlite_pkgkey_cb:
 **/
static gint
zif_md_other_sql_sqlite_pkgkey_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	guint pkgkey;
	gchar *endptr = NULL;
	GPtrArray *array = (GPtrArray *) data;

	/* get the ID */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "pkgKey") == 0) {
			pkgkey = g_ascii_strtoull (argv[i], &endptr, 10);
			if (argv[i] == endptr)
				egg_warning ("could not parse pkgKey '%s'", argv[i]);
			else
				g_ptr_array_add (array, GUINT_TO_POINTER (pkgkey));
		} else {
			egg_warning ("unrecognized: %s=%s", col_name[i], argv[i]);
		}
	}
	return 0;
}

/**
 * zif_md_other_sql_get_changelog:
 **/
static GPtrArray *
zif_md_other_sql_get_changelog (ZifMd *md, const gchar *pkgid,
			        GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_tmp = NULL;
	GPtrArray *pkgkey_array = NULL;
	guint i, j;
	guint pkgkey;
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	ZifChangeset *changeset;
	ZifMdOtherSql *md_other_sql = ZIF_MD_OTHER_SQL (md);

	/* setup completion */
	if (md_other_sql->priv->loaded)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* if not already loaded, load */
	if (!md_other_sql->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_md_load (md, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_other_sql file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* create data struct we can pass to the callback */
	pkgkey_array = g_ptr_array_new ();
	statement = g_strdup_printf ("SELECT pkgKey FROM packages WHERE pkgId = '%s'", pkgid);
	rc = sqlite3_exec (md_other_sql->priv->db, statement, zif_md_other_sql_sqlite_pkgkey_cb, pkgkey_array, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* output array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* resolve each pkgkey to a package */
	completion_local = zif_completion_get_child (completion);
	if (pkgkey_array->len > 0)
		zif_completion_set_number_steps (completion_local, pkgkey_array->len);
	for (i=0; i<pkgkey_array->len; i++) {
		pkgkey = GPOINTER_TO_UINT (g_ptr_array_index (pkgkey_array, i));

		/* get changeset for pkgKey */
		completion_loop = zif_completion_get_child (completion_local);
		array_tmp = zif_md_other_sql_search_pkgkey (md_other_sql, pkgkey, cancellable, completion_loop, error);
		if (array_tmp == NULL) {
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* no results */
		if (array_tmp->len == 0)
			egg_warning ("no changelog for pkgKey %i", pkgkey);
		for (j=0; j<array_tmp->len; j++) {
			changeset = g_ptr_array_index (array_tmp, j);
			g_ptr_array_add (array, g_object_ref (changeset));
		}

		/* clear array */
		g_ptr_array_unref (array_tmp);

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	g_free (statement);
	if (pkgkey_array != NULL)
		g_ptr_array_unref (pkgkey_array);
	return array;
}

/**
 * zif_md_other_sql_finalize:
 **/
static void
zif_md_other_sql_finalize (GObject *object)
{
	ZifMdOtherSql *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_OTHER_SQL (object));
	md = ZIF_MD_OTHER_SQL (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (zif_md_other_sql_parent_class)->finalize (object);
}

/**
 * zif_md_other_sql_class_init:
 **/
static void
zif_md_other_sql_class_init (ZifMdOtherSqlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_other_sql_finalize;

	/* map */
	md_class->load = zif_md_other_sql_load;
	md_class->unload = zif_md_other_sql_unload;
	md_class->get_changelog = zif_md_other_sql_get_changelog;
	g_type_class_add_private (klass, sizeof (ZifMdOtherSqlPrivate));
}

/**
 * zif_md_other_sql_init:
 **/
static void
zif_md_other_sql_init (ZifMdOtherSql *md)
{
	md->priv = ZIF_MD_OTHER_SQL_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * zif_md_other_sql_new:
 *
 * Return value: A new #ZifMdOtherSql class instance.
 *
 * Since: 0.0.1
 **/
ZifMdOtherSql *
zif_md_other_sql_new (void)
{
	ZifMdOtherSql *md;
	md = g_object_new (ZIF_TYPE_MD_OTHER_SQL, NULL);
	return ZIF_MD_OTHER_SQL (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_other_sql_test (EggTest *test)
{
	ZifMdOtherSql *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifChangeset *changeset;
	GCancellable *cancellable;
	ZifCompletion *completion;
	const gchar *text;

	if (!egg_test_start (test, "ZifMdOtherSql"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_other_sql md");
	md = zif_md_other_sql_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_md_set_id (ZIF_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set type");
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_OTHER_SQL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum type");
	ret = zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum compressed");
	ret = zif_md_set_checksum (ZIF_MD (md), "bc58c56b371a83dc546c86e1796d83b9ff78adbf733873c815c3fe5dd48b0d56");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "c378618f9764ff9fa271a40b962a0c3569ff274e741ada2342d0fe3554614488");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/fedora/bc58c56b371a83dc546c86e1796d83b9ff78adbf733873c815c3fe5dd48b0d56-other.sqlite.bz2");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "search for files");
	zif_completion_reset (completion);
	array = zif_md_other_sql_get_changelog (ZIF_MD (md),
						"42b8d71b303b19c2fcc2b06bb9c764f2902dd72b9376525025ee9ba4a41c38e9",
						cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len != 10)
		egg_test_failed (test, "failed to get correct number: %i", array->len);
	egg_test_success (test, NULL);

	/* get first entry */
	changeset = g_ptr_array_index (array, 1);

	/************************************************************/
	egg_test_title (test, "correct version");
	text = zif_changeset_get_version (changeset);
	if (g_strcmp0 (text, "1.2-3") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct value '%s'", text);

	/************************************************************/
	egg_test_title (test, "correct author");
	text = zif_changeset_get_author (changeset);
	if (g_strcmp0 (text, "Rex Dieter <rdieter@fedoraproject.org>") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct value '%s'", text);

	/************************************************************/
	egg_test_title (test, "correct description");
	text = zif_changeset_get_description (changeset);
	if (g_strcmp0 (text, "- BR: libfac-devel,factory-devel >= 3.1\n- restore ExcludeArch: ppc64 (#253847)") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct value '%s'", text);

	/* remove array */
	g_ptr_array_unref (array);

	g_object_unref (cancellable);
	g_object_unref (completion);
	g_object_unref (md);

	egg_test_end (test);
}
#endif

