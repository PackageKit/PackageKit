/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-md-primary-sql
 * @short_description: Primary metadata functionality
 *
 * Provide access to the primary repo metadata.
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
#include "zif-md-primary-sql.h"
#include "zif-package-remote.h"

#include "egg-debug.h"

#define ZIF_MD_PRIMARY_SQL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_PRIMARY_SQL, ZifMdPrimarySqlPrivate))

/**
 * ZifMdPrimarySqlPrivate:
 *
 * Private #ZifMdPrimarySql data
 **/
struct _ZifMdPrimarySqlPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
};

typedef struct {
	const gchar		*id;
	GPtrArray		*packages;
	ZifMdPrimarySql		*md;
} ZifMdPrimarySqlData;

G_DEFINE_TYPE (ZifMdPrimarySql, zif_md_primary_sql, ZIF_TYPE_MD)

/**
 * zif_md_primary_sql_unload:
 **/
static gboolean
zif_md_primary_sql_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_primary_sql_load:
 **/
static gboolean
zif_md_primary_sql_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	const gchar *filename;
	gint rc;
	ZifMdPrimarySql *primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), FALSE);

	/* already loaded */
	if (primary_sql->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for primary_sql");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	rc = sqlite3_open (filename, &primary_sql->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (primary_sql->priv->db));
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "can't open database: %s", sqlite3_errmsg (primary_sql->priv->db));
		goto out;
	}

	/* we don't need to keep syncing */
	sqlite3_exec (primary_sql->priv->db, "PRAGMA synchronous=OFF;", NULL, NULL, NULL);

	primary_sql->priv->loaded = TRUE;
out:
	return primary_sql->priv->loaded;
}

/**
 * zif_md_primary_sql_sqlite_create_package_cb:
 **/
static gint
zif_md_primary_sql_sqlite_create_package_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	ZifMdPrimarySqlData *fldata = (ZifMdPrimarySqlData *) data;
	ZifPackageRemote *package;
	ZifStoreRemote *store_remote;

	package = zif_package_remote_new ();
	store_remote = zif_md_get_store_remote (ZIF_MD (fldata->md));
	if (store_remote != NULL) {
		/* this is not set in a test harness */
		zif_package_remote_set_store_remote (package, store_remote);
	} else {
		egg_warning ("no remote store for %s", argv[1]);
	}
	zif_package_remote_set_from_repo (package, argc, col_name, argv, fldata->id, NULL);
	g_ptr_array_add (fldata->packages, package);

	return 0;
}

#define ZIF_MD_PRIMARY_SQL_HEADER "SELECT pkgId, name, arch, version, " \
				  "epoch, release, summary, description, url, " \
				  "rpm_license, rpm_group, size_package, location_href FROM packages"

/**
 * zif_md_primary_sql_search:
 **/
static GPtrArray *
zif_md_primary_sql_search (ZifMdPrimarySql *md, const gchar *statement,
			   GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *error_msg = NULL;
	gint rc;
	gboolean ret;
	GError *error_local = NULL;
	ZifMdPrimarySqlData *data = NULL;
	GPtrArray *array = NULL;

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_sql file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* create data struct we can pass to the callback */
	data = g_new0 (ZifMdPrimarySqlData, 1);
	data->md = md;
	data->id = zif_md_get_id (ZIF_MD (md));
	data->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	rc = sqlite3_exec (md->priv->db, statement, zif_md_primary_sql_sqlite_create_package_cb, data, &error_msg);
	if (rc != SQLITE_OK) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_BAD_SQL,
			     "SQL error, failed to execute '%s': %s\n", statement, error_msg);
		sqlite3_free (error_msg);
		g_ptr_array_unref (data->packages);
		goto out;
	}
	/* list of packages */
	array = data->packages;
out:
	g_free (data);
	return array;
}

/**
 * zif_md_primary_sql_strreplace:
 **/
static gchar *
zif_md_primary_sql_strreplace (const gchar *text, const gchar *find, const gchar *replace)
{
	gchar **array;
	gchar *retval;

	/* split apart and rejoin with new delimiter */
	array = g_strsplit (text, find, 0);
	retval = g_strjoinv (replace, array);
	g_strfreev (array);
	return retval;
}

/**
 * zif_md_primary_sql_get_statement_for_pred:
 **/
static gchar *
zif_md_primary_sql_get_statement_for_pred (const gchar *pred, gchar **search)
{
	guint i;
	const guint max_items = 20;
	GString *statement;
	gchar *temp;

	/* search with predicate */
	statement = g_string_new ("BEGIN;\n");
	for (i=0; search[i] != NULL; i++) {
		if (i % max_items == 0)
			g_string_append (statement, ZIF_MD_PRIMARY_SQL_HEADER " WHERE ");
		temp = zif_md_primary_sql_strreplace (pred, "###", search[i]);
		g_string_append (statement, temp);
		if (i % max_items == max_items - 1)
			g_string_append (statement, ";\n");
		else
			g_string_append (statement, " OR ");
		g_free (temp);
	}

	/* remove trailing OR entry */
	if (g_str_has_suffix (statement->str, " OR ")) {
		g_string_set_size (statement, statement->len - 4);
		g_string_append (statement, ";\n");
	}
	g_string_append (statement, "END;");
	return g_string_free (statement, FALSE);
}

/**
 * zif_md_primary_sql_resolve:
 **/
static GPtrArray *
zif_md_primary_sql_resolve (ZifMd *md, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* simple name match */
	statement = zif_md_primary_sql_get_statement_for_pred ("name = '###'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);
	return array;
}

/**
 * zif_md_primary_sql_search_name:
 **/
static GPtrArray *
zif_md_primary_sql_search_name (ZifMd *md, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* fuzzy name match */
	statement = zif_md_primary_sql_get_statement_for_pred ("name LIKE '%%###%%'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_details:
 **/
static GPtrArray *
zif_md_primary_sql_search_details (ZifMd *md, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* fuzzy details match */
	statement = zif_md_primary_sql_get_statement_for_pred ("name LIKE '%%###%%' OR "
							       "summary LIKE '%%###%%' OR "
							       "description LIKE '%%###%%'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_group:
 **/
static GPtrArray *
zif_md_primary_sql_search_group (ZifMd *md, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* simple group match */
	statement = zif_md_primary_sql_get_statement_for_pred ("rpm_group = '###'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_pkgid:
 **/
static GPtrArray *
zif_md_primary_sql_search_pkgid (ZifMd *md, gchar **search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* simple pkgid match */
	statement = zif_md_primary_sql_get_statement_for_pred ("pkgid = '###'", search);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);

	return array;
}

/**
 * zif_md_primary_sql_search_pkgkey:
 **/
static GPtrArray *
zif_md_primary_sql_search_pkgkey (ZifMd *md, guint pkgkey,
				  GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	statement = g_strdup_printf (ZIF_MD_PRIMARY_SQL_HEADER " WHERE pkgKey = '%i'", pkgkey);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);
	return array;
}

/**
 * zif_md_primary_sql_sqlite_pkgkey_cb:
 **/
static gint
zif_md_primary_sql_sqlite_pkgkey_cb (void *data, gint argc, gchar **argv, gchar **col_name)
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
				egg_warning ("failed to parse pkgKey %s", argv[i]);
			else
				g_ptr_array_add (array, GUINT_TO_POINTER (pkgkey));
		} else {
			egg_warning ("unrecognized: %s=%s", col_name[i], argv[i]);
		}
	}
	return 0;
}

/**
 * zif_md_primary_sql_what_provides:
 **/
static GPtrArray *
zif_md_primary_sql_what_provides (ZifMd *md, gchar **search,
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
	guint i;
	guint pkgkey;
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	ZifPackage *package;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	/* setup completion */
	if (md_primary_sql->priv->loaded)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* if not already loaded, load */
	if (!md_primary_sql->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_md_load (md, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_sql file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* create data struct we can pass to the callback */
	pkgkey_array = g_ptr_array_new ();
	statement = g_strdup_printf ("SELECT pkgKey FROM provides WHERE name = '%s'", search[0]);
	rc = sqlite3_exec (md_primary_sql->priv->db, statement, zif_md_primary_sql_sqlite_pkgkey_cb, pkgkey_array, &error_msg);
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

		/* get packages for pkgKey */
		completion_loop = zif_completion_get_child (completion_local);
		array_tmp = zif_md_primary_sql_search_pkgkey (md, pkgkey, cancellable, completion_loop, error);
		if (array_tmp == NULL) {
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* check we only got one result */
		if (array_tmp->len == 0) {
			egg_warning ("no package for pkgKey %i", pkgkey);
		} else if (array_tmp->len > 1 || array_tmp->len == 0) {
			egg_warning ("more than one package for pkgKey %i", pkgkey);
		} else {
			package = g_ptr_array_index (array_tmp, 0);
			g_ptr_array_add (array, g_object_ref (package));
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
 * zif_md_primary_sql_find_package:
 **/
static GPtrArray *
zif_md_primary_sql_find_package (ZifMd *md, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gchar *statement;
	GPtrArray *array;
	gchar **split;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate, TODO: search version (epoch+release) */
	split = pk_package_id_split (package_id);
	statement = g_strdup_printf (ZIF_MD_PRIMARY_SQL_HEADER " WHERE name = '%s' AND arch = '%s'",
				     split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_ARCH]);
	array = zif_md_primary_sql_search (md_primary_sql, statement, cancellable, completion, error);
	g_free (statement);
	g_strfreev (split);

	return array;
}

/**
 * zif_md_primary_sql_get_packages:
 **/
static GPtrArray *
zif_md_primary_sql_get_packages (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array;
	ZifMdPrimarySql *md_primary_sql = ZIF_MD_PRIMARY_SQL (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_SQL (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search with predicate */
	array = zif_md_primary_sql_search (md_primary_sql, ZIF_MD_PRIMARY_SQL_HEADER, cancellable, completion, error);
	return array;
}

/**
 * zif_md_primary_sql_finalize:
 **/
static void
zif_md_primary_sql_finalize (GObject *object)
{
	ZifMdPrimarySql *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_PRIMARY_SQL (object));
	md = ZIF_MD_PRIMARY_SQL (object);

	sqlite3_close (md->priv->db);

	G_OBJECT_CLASS (zif_md_primary_sql_parent_class)->finalize (object);
}

/**
 * zif_md_primary_sql_class_init:
 **/
static void
zif_md_primary_sql_class_init (ZifMdPrimarySqlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_primary_sql_finalize;

	/* map */
	md_class->load = zif_md_primary_sql_load;
	md_class->unload = zif_md_primary_sql_unload;
	md_class->search_name = zif_md_primary_sql_search_name;
	md_class->search_details = zif_md_primary_sql_search_details;
	md_class->search_group = zif_md_primary_sql_search_group;
	md_class->search_pkgid = zif_md_primary_sql_search_pkgid;
	md_class->what_provides = zif_md_primary_sql_what_provides;
	md_class->resolve = zif_md_primary_sql_resolve;
	md_class->get_packages = zif_md_primary_sql_get_packages;
	md_class->find_package = zif_md_primary_sql_find_package;
	g_type_class_add_private (klass, sizeof (ZifMdPrimarySqlPrivate));
}

/**
 * zif_md_primary_sql_init:
 **/
static void
zif_md_primary_sql_init (ZifMdPrimarySql *md)
{
	md->priv = ZIF_MD_PRIMARY_SQL_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->db = NULL;
}

/**
 * zif_md_primary_sql_new:
 *
 * Return value: A new #ZifMdPrimarySql class instance.
 *
 * Since: 0.0.1
 **/
ZifMdPrimarySql *
zif_md_primary_sql_new (void)
{
	ZifMdPrimarySql *md;
	md = g_object_new (ZIF_TYPE_MD_PRIMARY_SQL, NULL);
	return ZIF_MD_PRIMARY_SQL (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_primary_sql_test (EggTest *test)
{
	ZifMdPrimarySql *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	const gchar *summary;
	GCancellable *cancellable;
	ZifCompletion *completion;
	gchar *data[] = { "gnome-power-manager", "gnome-color-manager", NULL };

	if (!egg_test_start (test, "ZifMdPrimarySql"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_primary_sql md");
	md = zif_md_primary_sql_new ();
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
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_PRIMARY_SQL);
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
	ret = zif_md_set_checksum (ZIF_MD (md), "35d817e2bac701525fa72cec57387a2e3457bf32642adeee1e345cc180044c86");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "9b2b072a83b5175bc88d03ee64b52b39c0d40fec1516baa62dba81eea73cc645");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/fedora/35d817e2bac701525fa72cec57387a2e3457bf32642adeee1e345cc180044c86-primary.sqlite.bz2");
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
	egg_test_title (test, "resolve");
	array = zif_md_primary_sql_resolve (ZIF_MD (md), data, cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	egg_test_assert (test, array->len == 1);

	/************************************************************/
	egg_test_title (test, "correct value");
	package = g_ptr_array_index (array, 0);
	zif_completion_reset (completion);
	summary = zif_package_get_summary (package, NULL, completion, NULL);
	if (g_strcmp0 (summary, "GNOME Power Manager") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct summary '%s'", summary);
	g_ptr_array_unref (array);

	g_object_unref (cancellable);
	g_object_unref (completion);
	g_object_unref (md);

	egg_test_end (test);
}
#endif

