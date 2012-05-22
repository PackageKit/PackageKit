/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Matthias Klumpp <matthias@tenstral.net>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>
#include <sqlite3.h>
#include <gio/gio.h>
#include <stdlib.h>

#include "pk-package-cache.h"

static void     pk_package_cache_finalize	(GObject     *object);

#define PK_PACKAGE_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PACKAGE_CACHE, PkPackageCachePrivate))

struct _PkPackageCachePrivate
{
	sqlite3				*db;
	gchar				*filename;
	gboolean			 locked;
	guint				 dbversion;
};

enum {
	PROP_0,
	PROP_LOCKED,
	PROP_LAST
};

G_DEFINE_TYPE (PkPackageCache, pk_package_cache, G_TYPE_OBJECT)

/**
 * pk_package_cache_set_filename:
 *
 * The source database filename.
 */
gboolean
pk_package_cache_set_filename (PkPackageCache *pkcache, const gchar *filename, GError **error)
{
	gboolean ret = TRUE;
	PkPackageCachePrivate *priv = PK_PACKAGE_CACHE (pkcache)->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_CACHE (pkcache), FALSE);

	/* check database is in correct state */
	if (priv->locked) {
		g_set_error (error, 1, 0, "cache database is already open");
		ret = FALSE;
		goto out;
	}

	g_free (priv->filename);

	if (filename == NULL) {
		g_set_error (error, 1, 0, "cache database not specified");
		ret = FALSE;
		goto out;
	}

	priv->filename = g_strdup (filename);

out:
	return ret;
}

/**
 * pk_package_cache_get_dbversion_sqlite_cb:
 **/
static gint
pk_package_cache_get_dbversion_sqlite_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	guint *version = (guint *) data;
	/* we only expect one reply */
	if (argc != 1)
		return 1;

	/* parse version string */
	*version = atoi (argv[0]);
	return 0;
}

/**
 * pk_package_cache_update_db:
 */
static gboolean
pk_package_cache_update_db (PkPackageCache *pkcache, GError **error)
{
	gboolean ret = TRUE;
	const gchar *statement;
	gint rc;
	PkPackageCachePrivate *priv = PK_PACKAGE_CACHE (pkcache)->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_CACHE (pkcache), FALSE);

	/* check database is in correct state */
	if (!priv->locked) {
		g_set_error (error, 1, 0, "database is not open");
		ret = FALSE;
		goto out;
	}

	/* create table for packages */
	statement = "CREATE TABLE IF NOT EXISTS packages ("
		    "package_id TEXT primary key,"
		    "installed BOOLEAN DEFAULT FALSE,"
		    "repo_id TEXT,"
		    "summary TEXT,"
		    "description TEXT,"
		    "url TEXT,"
		    "size_download INT,"
		    "size_installed INT);";
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	if (rc) {
		g_set_error (error, 1, 0, "Can't create packages table: %s\n", sqlite3_errmsg (priv->db));
		ret = FALSE;
		goto out;
	}

	/* create config - we don't need this right now, but might be useful later */
	statement = "CREATE TABLE IF NOT EXISTS config ("
		    "data TEXT primary key,"
		    "value INTEGER);";
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	if (rc) {
		g_set_error (error, 1, 0, "Can't create config table: %s\n", sqlite3_errmsg (priv->db));
		ret = FALSE;
		goto out;
	}

out:
	return ret;
}

/**
 * pk_package_cache_open:
 *
 * Opens the package cache database
 */
gboolean
pk_package_cache_open (PkPackageCache *pkcache, gboolean synchronous, GError **error)
{
	gboolean ret = TRUE;
	GError *e = NULL;
	gint rc;
	const gchar *statement;
	PkPackageCachePrivate *priv = PK_PACKAGE_CACHE (pkcache)->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_CACHE (pkcache), FALSE);

	/* check database is in correct state */
	if (priv->locked) {
		g_set_error (error, 1, 0, "cache database is already open");
		ret = FALSE;
		goto out;
	}

	/* open database */
	rc = sqlite3_open (priv->filename, &priv->db);
	if (rc != SQLITE_OK) {
		g_set_error (error, 1, 0, "Can't open cache %s: %s\n", priv->filename, sqlite3_errmsg (priv->db));
		ret = FALSE;
		goto out;
	}

	/* don't sync */
	if (!synchronous) {
		statement = "PRAGMA synchronous=OFF";
		rc = sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		if (rc != SQLITE_OK) {
			g_set_error (error, 1, 0, "Can't turn off sync from %s: %s\n", priv->filename, sqlite3_errmsg (priv->db));
			ret = FALSE;
			goto out;
		}
	}

	/* get version, failure is okay as v1 databases didn't have this table */
	statement = "SELECT value FROM config WHERE data = 'dbversion'";
	rc = sqlite3_exec (priv->db, statement, pk_package_cache_get_dbversion_sqlite_cb, (void*) &priv->dbversion, NULL);
	if (rc != SQLITE_OK)
		priv->dbversion = 1;
	g_debug ("operating on database version %i", priv->dbversion);

	/* we're ready to use the db! */
	priv->locked = TRUE;

	/* create the database sheme */
	ret = pk_package_cache_update_db (pkcache, &e);
	if (!ret) {
		g_propagate_error (error, e);
		goto out;
	}

out:
	return ret;
}

/**
 * pk_package_cache_get_version:
 *
 * Get SQLite3 database version
 */
guint
pk_package_cache_get_version (PkPackageCache *pkcache)
{
	g_return_val_if_fail (PK_IS_PACKAGE_CACHE (pkcache), 0);
	return pkcache->priv->dbversion;
}

/**
 * pk_package_cache_close:
 */
gboolean
pk_package_cache_close (PkPackageCache *pkcache, gboolean vaccuum, GError **error)
{
	gboolean ret = TRUE;
	gint rc;
	const gchar *statement;
	PkPackageCachePrivate *priv = PK_PACKAGE_CACHE (pkcache)->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_CACHE (pkcache), FALSE);

	/* check database is in correct state */
	if (!priv->locked) {
		g_set_error (error, 1, 0, "database is not open");
		ret = FALSE;
		goto out;
	}

	/* reclaim memory */
	if (vaccuum) {
		statement = "VACUUM";
		rc = sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		if (rc) {
			g_set_error (error, 1, 0, "Can't vaccuum: %s\n", sqlite3_errmsg (priv->db));
			ret = FALSE;
			goto out;
		}
	}

	sqlite3_close (priv->db);
	priv->locked = FALSE;
	priv->dbversion = 0;
out:
	return ret;
}

/**
 * pk_package_cache_add_package:
 */
gboolean
pk_package_cache_add_package (PkPackageCache *pkcache, PkPackage *package, GError **error)
{
	gboolean ret = TRUE;
	gint rc;
	gchar *statement = NULL;
	gboolean pkg_installed;
	PkPackageCachePrivate *priv = PK_PACKAGE_CACHE (pkcache)->priv;

	g_return_val_if_fail (PK_IS_PACKAGE_CACHE (pkcache), FALSE);
	g_return_val_if_fail (PK_IS_PACKAGE (package), FALSE);

	/* check database is in correct state */
	if (!priv->locked) {
		g_set_error (error, 1, 0, "database is not open");
		ret = FALSE;
		goto out;
	}

	pkg_installed = (pk_package_get_info (package) == PK_INFO_ENUM_INSTALLED);

	/* generate SQL */
	statement = sqlite3_mprintf ("INSERT INTO packages (package_id, installed, repo_id, summary, "
				     "description, url, size_download, size_installed)"
				     "VALUES (%Q, %i, %Q, %Q, %Q, %Q, %i, %i);",
					pk_package_get_id (package),
					pkg_installed,
					pk_package_get_data (package),
					pk_package_get_summary (package),
					"::TODO",
					"::TODO",
					0,
					0);
	rc = sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
	if (rc) {
		g_set_error (error, 1, 0, "Can't add package: %s\n", sqlite3_errmsg (priv->db));
		ret = FALSE;
		goto out;
	}
out:
	sqlite3_free (statement);
	return ret;
}

/**
 * pk_package_cache_get_property:
 */
static void
pk_package_cache_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkPackageCache *pkcache = PK_PACKAGE_CACHE (object);
	PkPackageCachePrivate *priv = pkcache->priv;

	switch (prop_id) {
		case PROP_LOCKED:
			g_value_set_boolean (value, priv->locked);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

/**
 * pk_package_cache_set_property:
 */
static void
pk_package_cache_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

/**
 * pk_package_cache_class_init:
 */
static void
pk_package_cache_class_init (PkPackageCacheClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_package_cache_finalize;
	object_class->get_property = pk_package_cache_get_property;
	object_class->set_property = pk_package_cache_set_property;

	/*
	 * PkPackageCache:locked:
	 */
	pspec = g_param_spec_boolean ("locked", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_LOCKED, pspec);

	g_type_class_add_private (klass, sizeof (PkPackageCachePrivate));
}

/**
 * pk_package_cache_init:
 */
static void
pk_package_cache_init (PkPackageCache *pkcache)
{
	pkcache->priv = PK_PACKAGE_CACHE_GET_PRIVATE (pkcache);
	pkcache->priv->filename = NULL;
}

/**
 * pk_package_cache_finalize:
 */
static void
pk_package_cache_finalize (GObject *object)
{
	PkPackageCache *pkcache = PK_PACKAGE_CACHE (object);
	PkPackageCachePrivate *priv = pkcache->priv;

	g_free (priv->filename);

	if (priv->locked) {
		g_warning ("YOU HAVE TO MANUALLY CALL pk_package_cache_close()!!!");
		sqlite3_close (priv->db);
	}

	G_OBJECT_CLASS (pk_package_cache_parent_class)->finalize (object);
}

/**
 * pk_package_cache_new:
 *
 * Return value: a new PkPackageCache object.
 */
PkPackageCache *
pk_package_cache_new (void)
{
	PkPackageCache *cache;
	cache = g_object_new (PK_TYPE_PACKAGE_CACHE, NULL);
	return PK_PACKAGE_CACHE (cache);
}
