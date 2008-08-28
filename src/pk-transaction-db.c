/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <sqlite3.h>
#include <pk-common.h>

#include "egg-debug.h"
#include "pk-transaction-db.h"
#include "pk-marshal.h"

static void     pk_transaction_db_class_init	(PkTransactionDbClass *klass);
static void     pk_transaction_db_init		(PkTransactionDb      *tdb);
static void     pk_transaction_db_finalize	(GObject        *object);

#define PK_TRANSACTION_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbPrivate))

#if PK_BUILD_LOCAL
#define PK_TRANSACTION_DB_FILE		"./transactions.db"
#else
#define PK_TRANSACTION_DB_FILE		PK_DB_DIR "/transactions.db"
#endif

struct PkTransactionDbPrivate
{
	sqlite3			*db;
};

enum {
	PK_TRANSACTION_DB_TRANSACTION,
	PK_TRANSACTION_DB_LAST_SIGNAL
};

static guint signals [PK_TRANSACTION_DB_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkTransactionDb, pk_transaction_db, G_TYPE_OBJECT)

typedef struct {
	gboolean succeeded;
	guint duration;
	PkRoleEnum role;
	gchar *tid;
	gchar *data;
	gchar *timespec;
} PkTransactionDbItem;

/**
 * pk_transaction_db_item_clear:
 **/
static gboolean
pk_transaction_db_item_clear (PkTransactionDbItem *item)
{
	item->succeeded = FALSE;
	item->duration = 0;
	item->role = PK_ROLE_ENUM_UNKNOWN;
	item->tid = NULL;
	item->data = NULL;
	item->timespec = NULL;
	return TRUE;
}

/**
 * pk_transaction_db_item_free:
 **/
static gboolean
pk_transaction_db_item_free (PkTransactionDbItem *item)
{
	g_free (item->tid);
	g_free (item->data);
	g_free (item->timespec);
	return TRUE;
}

/**
 * pk_transaction_sqlite_callback:
 **/
static gint
pk_transaction_sqlite_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkTransactionDbItem item;
	PkTransactionDb *tdb = PK_TRANSACTION_DB (data);
	gint i;
	gchar *col;
	gchar *value;
	guint temp;
	gboolean ret;

	g_return_val_if_fail (tdb != NULL, 0);
	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), 0);

	pk_transaction_db_item_clear (&item);

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (pk_strequal (col, "succeeded")) {
			ret = pk_strtouint (value, &temp);
			if (!ret) {
				egg_warning ("failed to parse succeeded: %s", value);
			}
			if (temp == 1) {
				item.succeeded = TRUE;
			} else {
				item.succeeded = FALSE;
			}
			if (item.succeeded > 1) {
				egg_warning ("item.succeeded %i! Resetting to 1", item.succeeded);
				item.succeeded = 1;
			}
		} else if (pk_strequal (col, "role")) {
			if (value != NULL) {
				item.role = pk_role_enum_from_text (value);
			}
		} else if (pk_strequal (col, "transaction_id")) {
			if (value != NULL) {
				item.tid = g_strdup (value);
			}
		} else if (pk_strequal (col, "timespec")) {
			if (value != NULL) {
				item.timespec = g_strdup (value);
			}
		} else if (pk_strequal (col, "data")) {
			if (value != NULL) {
				item.data = g_strdup (value);
			}
		} else if (pk_strequal (col, "duration")) {
			ret = pk_strtouint (value, &item.duration);
			if (!ret) {
				egg_warning ("failed to parse duration: %s", value);
				item.duration = 0;
			}
			if (item.duration > 60*60*12) {
				egg_warning ("insane duration: %i", item.duration);
				item.duration = 0;
			}
		} else {
			egg_warning ("%s = %s\n", col, value);
		}
	}

	/* emit signal */
	g_signal_emit (tdb, signals [PK_TRANSACTION_DB_TRANSACTION], 0,
		       item.tid, item.timespec, item.succeeded, item.role,
		       item.duration, item.data);

	pk_transaction_db_item_free (&item);
	return 0;
}

/**
 * pk_transaction_db_sql_statement:
 **/
static gboolean
pk_transaction_db_sql_statement (PkTransactionDb *tdb, const gchar *sql)
{
	gchar *error_msg = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (tdb->priv->db != NULL, FALSE);

	rc = sqlite3_exec (tdb->priv->db, sql, pk_transaction_sqlite_callback, tdb, &error_msg);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_time_action_sqlite_callback:
 **/
static gint
pk_time_action_sqlite_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gint i;
	gchar *col;
	gchar *value;
	gchar **timespec = (gchar**) data;

	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (pk_strequal (col, "timespec")) {
			*timespec = g_strdup (value);
		} else {
			egg_warning ("%s = %s\n", col, value);
		}
	}
	return 0;
}

/**
 * pk_transaction_db_action_time_since:
 **/
guint
pk_transaction_db_action_time_since (PkTransactionDb *tdb, PkRoleEnum role)
{
	gchar *error_msg = NULL;
	gint rc;
	const gchar *role_text;
	gchar *statement;
	gchar *timespec = NULL;
	guint time;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), 0);
	g_return_val_if_fail (tdb->priv->db != NULL, 0);

	role_text = pk_role_enum_to_text (role);
	egg_debug ("get_time_since_action=%s", role_text);

	statement = g_strdup_printf ("SELECT timespec FROM last_action WHERE role = '%s'", role_text);
	rc = sqlite3_exec (tdb->priv->db, statement, pk_time_action_sqlite_callback, &timespec, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return 0;
	}
	if (timespec == NULL) {
		egg_warning ("no response, assume zero");
		return 0;
	}

	/* work out the difference */
	time = pk_iso8601_difference (timespec);
	egg_debug ("timespec=%s, difference=%i", timespec, time);
	g_free (timespec);

	return time;
}

/**
 * pk_transaction_db_action_time_reset:
 **/
gboolean
pk_transaction_db_action_time_reset (PkTransactionDb *tdb, PkRoleEnum role)
{
	gchar *error_msg = NULL;
	gint rc;
	const gchar *role_text;
	gchar *statement;
	gchar *timespec;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (tdb->priv->db != NULL, FALSE);

	timespec = pk_iso8601_present ();
	role_text = pk_role_enum_to_text (role);
	egg_debug ("reset action time=%s to %s", role_text, timespec);

	statement = g_strdup_printf ("UPDATE last_action SET timespec = '%s' WHERE role = '%s'", timespec, role_text);
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	g_free (timespec);
	g_free (statement);

	/* did we fail? */
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_transaction_db_get_list:
 **/
gboolean
pk_transaction_db_get_list (PkTransactionDb *tdb, guint limit)
{
	gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	if (limit == 0) {
		statement = g_strdup ("SELECT transaction_id, timespec, succeeded, duration, role, data "
				      "FROM transactions ORDER BY timespec DESC");
	} else {
		statement = g_strdup_printf ("SELECT transaction_id, timespec, succeeded, duration, role, data "
					     "FROM transactions ORDER BY timespec DESC LIMIT %i", limit);
	}
	pk_transaction_db_sql_statement (tdb, statement);
	g_free (statement);

	return TRUE;
}

/**
 * pk_transaction_db_add:
 **/
gboolean
pk_transaction_db_add (PkTransactionDb *tdb, const gchar *tid)
{
	gchar *timespec;
	gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	egg_debug ("adding transaction %s", tid);

	timespec = pk_iso8601_present ();
	statement = g_strdup_printf ("INSERT INTO transactions (transaction_id, timespec) VALUES ('%s', '%s')", tid, timespec);
	pk_transaction_db_sql_statement (tdb, statement);
	g_free (statement);

	g_free (timespec);

	return TRUE;
}

/**
 * pk_transaction_db_set_role:
 **/
gboolean
pk_transaction_db_set_role (PkTransactionDb *tdb, const gchar *tid, PkRoleEnum role)
{
	gchar *statement;
	const gchar *role_text;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	role_text = pk_role_enum_to_text (role);
	statement = g_strdup_printf ("UPDATE transactions SET role = '%s' WHERE transaction_id = '%s'", role_text, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	g_free (statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_data:
 **/
gboolean
pk_transaction_db_set_data (PkTransactionDb *tdb, const gchar *tid, const gchar *data)
{
	gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	/* TODO: we have to be careful of SQL injection attacks */
	statement = g_strdup_printf ("UPDATE transactions SET data = \"%s\" WHERE transaction_id = '%s'",
				     data, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	g_free (statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_finished:
 **/
gboolean
pk_transaction_db_set_finished (PkTransactionDb *tdb, const gchar *tid, gboolean success, guint runtime)
{
	gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = g_strdup_printf ("UPDATE transactions SET succeeded = %i, duration = %i WHERE transaction_id = '%s'",
				     success, runtime, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	g_free (statement);
	return TRUE;
}

/**
 * pk_transaction_db_print:
 **/
gboolean
pk_transaction_db_print (PkTransactionDb *tdb)
{
	const gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = "SELECT transaction_id, timespec, succeeded, duration, role FROM transactions";
	pk_transaction_db_sql_statement (tdb, statement);

	return TRUE;
}

/**
 * pk_transaction_db_class_init:
 * @klass: The PkTransactionDbClass
 **/
static void
pk_transaction_db_class_init (PkTransactionDbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_transaction_db_finalize;
	signals [PK_TRANSACTION_DB_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_UINT_UINT_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);
	g_type_class_add_private (klass, sizeof (PkTransactionDbPrivate));
}

/**
 * pk_transaction_db_empty:
 **/
gboolean
pk_transaction_db_empty (PkTransactionDb *tdb)
{
	const gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (tdb->priv->db != NULL, FALSE);

	statement = "TRUNCATE TABLE transactions;";
	sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	return TRUE;
}

/**
 * pk_transaction_db_create_table_last_action:
 **/
static gboolean
pk_transaction_db_create_table_last_action (PkTransactionDb *tdb)
{
	const gchar *role_text;
	gchar *statement;
	gchar *timespec;
	guint i;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (tdb->priv->db != NULL, FALSE);

	timespec = pk_iso8601_present ();
	statement = "CREATE TABLE last_action (role TEXT primary key, timespec TEXT);";
	sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
		role_text = pk_role_enum_to_text (i);
		/* reset to now if the role does not exist */
		statement = g_strdup_printf ("INSERT INTO last_action (role, timespec) VALUES ('%s', '%s')", role_text, timespec);
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		g_free (statement);
	}
	g_free (timespec);
	return TRUE;
}

/**
 * pk_transaction_db_init:
 **/
static void
pk_transaction_db_init (PkTransactionDb *tdb)
{
	gboolean create_file;
	const gchar *statement;
	gint rc;

	g_return_if_fail (PK_IS_TRANSACTION_DB (tdb));

	tdb->priv = PK_TRANSACTION_DB_GET_PRIVATE (tdb);
	tdb->priv->db = NULL;

	/* if the database file was not installed (or was nuked) recreate it */
	create_file = g_file_test (PK_TRANSACTION_DB_FILE, G_FILE_TEST_EXISTS);

	egg_debug ("trying to open database '%s'", PK_TRANSACTION_DB_FILE);
	rc = sqlite3_open (PK_TRANSACTION_DB_FILE, &tdb->priv->db);
	if (rc) {
		egg_error ("Can't open database: %s\n", sqlite3_errmsg (tdb->priv->db));
		sqlite3_close (tdb->priv->db);
		return;
	} else {
		if (create_file == FALSE) {
			statement = "CREATE TABLE transactions ("
				    "transaction_id TEXT primary key,"
				    "timespec TEXT,"
				    "duration INTEGER,"
				    "succeeded INTEGER DEFAULT 0,"
				    "role TEXT,"
				    "data TEXT,"
				    "description TEXT);";
			sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		}
	}

	/* we might be running an old database, recreate */
	pk_transaction_db_create_table_last_action (tdb);
}

/**
 * pk_transaction_db_finalize:
 * @object: The object to finalize
 **/
static void
pk_transaction_db_finalize (GObject *object)
{
	PkTransactionDb *tdb;
	g_return_if_fail (PK_IS_TRANSACTION_DB (object));
	tdb = PK_TRANSACTION_DB (object);
	g_return_if_fail (tdb->priv != NULL);

	/* close the database */
	sqlite3_close (tdb->priv->db);

	G_OBJECT_CLASS (pk_transaction_db_parent_class)->finalize (object);
}

/**
 * pk_transaction_db_new:
 *
 * Return value: a new PkTransactionDb object.
 **/
PkTransactionDb *
pk_transaction_db_new (void)
{
	PkTransactionDb *tdb;
	tdb = g_object_new (PK_TYPE_TRANSACTION_DB, NULL);
	return PK_TRANSACTION_DB (tdb);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_transaction_db (LibSelfTest *test)
{
	PkTransactionDb *db;
	guint value;
	gboolean ret;

	if (libst_start (test, "PkTransactionDb", CLASS_AUTO) == FALSE) {
		return;
	}

	/* don't do these when doing make distcheck */
#ifndef PK_IS_DEVELOPER
	libst_end (test);
	return;
#endif

	db = pk_transaction_db_new ();

	/************************************************************/
	libst_title (test, "set the correct time");
	ret = pk_transaction_db_action_time_reset (db, PK_ROLE_ENUM_REFRESH_CACHE);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed to reset value");
	}

	g_usleep (2*1000*1000);

	/************************************************************/
	libst_title (test, "do we get the correct time");
	value = pk_transaction_db_action_time_since (db, PK_ROLE_ENUM_REFRESH_CACHE);
	if (value > 1 && value <= 4) {
		libst_success (test, "failed to get correct time");
	} else {
		libst_failed (test, "failed to get correct time, %i", value);
	}

	g_object_unref (db);

	libst_end (test);
}
#endif

