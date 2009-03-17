/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-transaction-db.h"
#include "pk-marshal.h"

static void     pk_transaction_db_finalize	(GObject        *object);

#define PK_TRANSACTION_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbPrivate))

#define PK_TRANSACTION_DB_ID_FILE_OBSOLETE	LOCALSTATEDIR "/lib/PackageKit/job_count.dat"

#if PK_BUILD_LOCAL
#define PK_TRANSACTION_DB_FILE			"./transactions.db"
#else
#define PK_TRANSACTION_DB_FILE			PK_DB_DIR "/transactions.db"
#endif

struct PkTransactionDbPrivate
{
	sqlite3			*db;
	guint			 job_count;
	guint			 database_save_id;
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
	guint uid;
	gchar *cmdline;
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
	item->uid = 0;
	item->cmdline = NULL;
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
	g_free (item->cmdline);
	return TRUE;
}

/**
 * pk_transaction_sqlite_transaction_cb:
 **/
static gint
pk_transaction_sqlite_transaction_cb (void *data, gint argc, gchar **argv, gchar **col_name)
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
		if (egg_strequal (col, "succeeded")) {
			ret = egg_strtouint (value, &temp);
			if (!ret)
				egg_warning ("failed to parse succeeded: %s", value);
			if (temp == 1)
				item.succeeded = TRUE;
			else
				item.succeeded = FALSE;
			if (item.succeeded > 1) {
				egg_warning ("item.succeeded %i! Resetting to 1", item.succeeded);
				item.succeeded = 1;
			}
		} else if (egg_strequal (col, "role")) {
			if (value != NULL)
				item.role = pk_role_enum_from_text (value);
		} else if (egg_strequal (col, "transaction_id")) {
			if (value != NULL)
				item.tid = g_strdup (value);
		} else if (egg_strequal (col, "timespec")) {
			if (value != NULL)
				item.timespec = g_strdup (value);
		} else if (egg_strequal (col, "cmdline")) {
			if (value != NULL)
				item.cmdline = g_strdup (value);
		} else if (egg_strequal (col, "data")) {
			if (value != NULL)
				item.data = g_strdup (value);
		} else if (egg_strequal (col, "uid")) {
			ret = egg_strtouint (value, &temp);
			if (ret)
				item.uid = temp;
		} else if (egg_strequal (col, "duration")) {
			ret = egg_strtouint (value, &item.duration);
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

	egg_debug (" duration: %i (seconds)", item.duration);
	egg_debug (" data: %s", item.data);
	egg_debug (" uid: %i", item.uid);
	egg_debug (" cmdline: %s", item.cmdline);

	/* emit signal */
	g_signal_emit (tdb, signals [PK_TRANSACTION_DB_TRANSACTION], 0,
		       item.tid, item.timespec, item.succeeded, item.role,
		       item.duration, item.data, item.uid, item.cmdline);

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

	rc = sqlite3_exec (tdb->priv->db, sql, pk_transaction_sqlite_transaction_cb, tdb, &error_msg);
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
		if (egg_strequal (col, "timespec"))
			*timespec = g_strdup (value);
		else
			egg_warning ("%s = %s\n", col, value);
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
	guint time_ms;

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
	time_ms = pk_iso8601_difference (timespec);
	egg_debug ("timespec=%s, difference=%i", timespec, time_ms);
	g_free (timespec);

	return time_ms;
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

	if (limit == 0)
		statement = g_strdup ("SELECT transaction_id, timespec, succeeded, duration, role, data, uid, cmdline "
				      "FROM transactions ORDER BY timespec DESC");
	else
		statement = g_strdup_printf ("SELECT transaction_id, timespec, succeeded, duration, role, data, uid, cmdline "
					     "FROM transactions ORDER BY timespec DESC LIMIT %i", limit);

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
 * pk_transaction_db_set_uid:
 **/
gboolean
pk_transaction_db_set_uid (PkTransactionDb *tdb, const gchar *tid, guint uid)
{
	gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = g_strdup_printf ("UPDATE transactions SET uid = '%i' WHERE transaction_id = '%s'", uid, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	g_free (statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_cmdline:
 **/
gboolean
pk_transaction_db_set_cmdline (PkTransactionDb *tdb, const gchar *tid, const gchar *cmdline)
{
	gchar *statement;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = g_strdup_printf ("UPDATE transactions SET cmdline = '%s' WHERE transaction_id = '%s'", cmdline, tid);
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
 * pk_transaction_sqlite_job_id_cb:
 **/
static gint
pk_transaction_sqlite_job_id_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkTransactionDb *tdb = PK_TRANSACTION_DB (data);
	if (argc != 1) {
		egg_warning ("wrong number of replies: %i", argc);
		goto out;
	}
	egg_strtouint (argv[0], &tdb->priv->job_count);
out:
	return 0;
}


/**
 * pk_transaction_db_get_random_hex_string:
 **/
static gchar *
pk_transaction_db_get_random_hex_string (guint length)
{
	GRand *gen;
	gint32 num;
	gchar *string;
	guint i;

	gen = g_rand_new ();

	/* allocate a string with the correct size */
	string = g_strnfill (length, 'x');
	for (i=0; i<length; i++) {
		num = g_rand_int_range (gen, (gint32) 'a', (gint32) 'f');
		/* assign a random number as a char */
		string[i] = (gchar) num;
	}
	g_rand_free (gen);
	return string;
}

/**
 * pk_transaction_db_defer_write_job_count_cb:
 **/
static gboolean
pk_transaction_db_defer_write_job_count_cb (PkTransactionDb *tdb)
{
	gchar *statement = NULL;
	gchar *error_msg = NULL;
	gint rc;

	/* force fsync as we don't want to repeat this number */
	sqlite3_exec (tdb->priv->db, "PRAGMA synchronous=ON", NULL, NULL, NULL);

	/* save the job count */
	egg_debug ("doing deferred write syncronous");
	statement = g_strdup_printf ("UPDATE config SET value = '%i' WHERE key = 'job_count'", tdb->priv->job_count);
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		egg_error ("failed to set job id: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* turn off fsync */
	sqlite3_exec (tdb->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);

	/* allow this to happen again */
	tdb->priv->database_save_id = 0;
out:
	g_free (statement);
	return FALSE;
}

/**
 * pk_transaction_db_generate_id:
 **/
gchar *
pk_transaction_db_generate_id (PkTransactionDb *tdb)
{
	gchar *rand_str = NULL;
	gchar *tid = NULL;

	/* increment */
	tdb->priv->job_count++;
	egg_debug ("job count now %i", tdb->priv->job_count);

	/* we don't need to wait for the database write, just do this the
	 * next time we are idle (but ensure we do this on shutdown) */
	if (tdb->priv->database_save_id == 0) {
		egg_debug ("deferring low priority write until idle");
		tdb->priv->database_save_id = g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc) pk_transaction_db_defer_write_job_count_cb, tdb, NULL);
	}

	/* make the tid */
	rand_str = pk_transaction_db_get_random_hex_string (8);
	tid = g_strdup_printf ("/%i_%s_data", tdb->priv->job_count, rand_str);
	g_free (rand_str);
	return tid;
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
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_UINT_UINT_STRING_UINT_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT,
			      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	g_type_class_add_private (klass, sizeof (PkTransactionDbPrivate));
}

/**
 * pk_transaction_db_init:
 **/
static void
pk_transaction_db_init (PkTransactionDb *tdb)
{
	const gchar *statement;
	gint rc;
	gchar *error_msg = NULL;
	const gchar *role_text;
	gchar *text;
	gchar *timespec;
	gboolean ret;
	guint i;

	g_return_if_fail (PK_IS_TRANSACTION_DB (tdb));

	tdb->priv = PK_TRANSACTION_DB_GET_PRIVATE (tdb);
	tdb->priv->db = NULL;
	tdb->priv->database_save_id = 0;

	egg_debug ("trying to open database '%s'", PK_TRANSACTION_DB_FILE);
	rc = sqlite3_open (PK_TRANSACTION_DB_FILE, &tdb->priv->db);
	if (rc != SQLITE_OK) {
		egg_error ("Can't open database: %s\n", sqlite3_errmsg (tdb->priv->db));
		sqlite3_close (tdb->priv->db);
		return;
	}

	/* we don't need to keep doing fsync */
	sqlite3_exec (tdb->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);

	/* check transactions */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM transactions LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		egg_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE transactions ("
			    "transaction_id TEXT PRIMARY KEY,"
			    "timespec TEXT,"
			    "duration INTEGER,"
			    "succeeded INTEGER DEFAULT 0,"
			    "role TEXT,"
			    "data TEXT,"
			    "description TEXT,"
			    "uid INTEGER DEFAULT 0,"
			    "cmdline TEXT);";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	}

	/* check transactions has enough data (since 0.3.11) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT uid, cmdline FROM transactions LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		egg_debug ("altering table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "ALTER TABLE transactions ADD COLUMN uid INTEGER DEFAULT 0;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE transactions ADD COLUMN cmdline TEXT;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	}

	/* check last_action (since 0.3.10) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM last_action LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		egg_debug ("adding last action details: %s", error_msg);
		statement = "CREATE TABLE last_action (role TEXT PRIMARY KEY, timespec TEXT);";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);

		/* create values for now */
		timespec = pk_iso8601_present ();
		for (i=0; i<PK_ROLE_ENUM_UNKNOWN; i++) {
			role_text = pk_role_enum_to_text (i);
			/* reset to now if the role does not exist */
			text = g_strdup_printf ("INSERT INTO last_action (role, timespec) VALUES ('%s', '%s')", role_text, timespec);
			sqlite3_exec (tdb->priv->db, text, NULL, NULL, NULL);
			g_free (text);
		}
		g_free (timespec);
	}

	/* check config (since 0.4.6) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM config LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		egg_debug ("adding config: %s", error_msg);
		statement = "CREATE TABLE config (key TEXT PRIMARY KEY, value TEXT);";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);

		/* save creation version */
		text = g_strdup_printf ("INSERT INTO config (key, value) VALUES ('version', '%s')", PACKAGE_VERSION);
		sqlite3_exec (tdb->priv->db, text, NULL, NULL, NULL);
		g_free (text);

		/* get the old job count from the text file (this is a legacy file) */
		ret = g_file_get_contents (PK_TRANSACTION_DB_ID_FILE_OBSOLETE, &text, NULL, NULL);
		if (ret)
			egg_strtouint (text, &tdb->priv->job_count);
		g_free (text);

		/* save job id */
		text = g_strdup_printf ("INSERT INTO config (key, value) VALUES ('job_count', '%i')", tdb->priv->job_count);
		sqlite3_exec (tdb->priv->db, text, NULL, NULL, NULL);
		g_free (text);
	} else {
		/* get the job count */
		statement = "SELECT value FROM config WHERE key = 'job_count'";
		rc = sqlite3_exec (tdb->priv->db, statement, pk_transaction_sqlite_job_id_cb, tdb, &error_msg);
		if (rc != SQLITE_OK) {
			egg_warning ("failed to get job id: %s\n", error_msg);
			sqlite3_free (error_msg);
		}
		egg_debug ("job count is now at %i", tdb->priv->job_count);
	}
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

	/* if we shutdown with a deferred database write, then enforce it here */
	if (tdb->priv->database_save_id != 0) {
		pk_transaction_db_defer_write_job_count_cb (tdb);
		g_source_remove (tdb->priv->database_save_id);
	}

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
#ifdef EGG_TEST
#include <glib/gstdio.h>
#include "egg-test.h"

void
pk_transaction_db_test (EggTest *test)
{
	PkTransactionDb *db;
	guint value;
	gchar *tid;
	gboolean ret;
	guint ms;

	if (!egg_test_start (test, "PkTransactionDb"))
		return;

	/* don't do these when doing make distcheck */
#ifndef PK_IS_DEVELOPER
	egg_test_end (test);
	return;
#endif

	/* remove the self check file */
#if PK_BUILD_LOCAL
	g_unlink (PK_TRANSACTION_DB_FILE);
#endif

	/************************************************************/
	egg_test_title (test, "check we created quickly");
	db = pk_transaction_db_new ();
	ms = egg_test_elapsed (test);
	if (ms < 1500)
		egg_test_success (test, "acceptable time %ims", ms);
	else
		egg_test_failed (test, "took a long time: %ims", ms);
	g_object_unref (db);

	/************************************************************/
	egg_test_title (test, "check we opened quickly");
	db = pk_transaction_db_new ();
	ms = egg_test_elapsed (test);
	if (ms < 100)
		egg_test_success (test, "acceptable time %ims", ms);
	else
		egg_test_failed (test, "took a long time: %ims", ms);

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/
	egg_test_title (test, "get an tid object");
	tid = pk_transaction_db_generate_id (db);
	ms = egg_test_elapsed (test);
	if (ms < 5)
		egg_test_success (test, "acceptable time %ims", ms);
	else
		egg_test_failed (test, "took a long time: %ims", ms);
	g_free (tid);

	/************************************************************/
	egg_test_title (test, "get an tid object (no wait)");
	tid = pk_transaction_db_generate_id (db);
	ms = egg_test_elapsed (test);
	if (ms < 5)
		egg_test_success (test, "acceptable time %ims", ms);
	else
		egg_test_failed (test, "took a long time: %ims", ms);
	g_free (tid);

	/************************************************************/
	egg_test_title (test, "set the correct time");
	ret = pk_transaction_db_action_time_reset (db, PK_ROLE_ENUM_REFRESH_CACHE);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to reset value");

	/************************************************************/
	egg_test_title (test, "do the deferred write");
	while (g_main_context_pending (NULL))
		g_main_context_iteration (NULL, TRUE);
	ms = egg_test_elapsed (test);
	if (ms > 1)
		egg_test_success (test, "acceptable time %ims", ms);
	else
		egg_test_failed (test, "took too short time: %ims", ms);

	g_usleep (2*1000*1000);

	/************************************************************/
	egg_test_title (test, "do we get the correct time");
	value = pk_transaction_db_action_time_since (db, PK_ROLE_ENUM_REFRESH_CACHE);
	if (value > 1 && value <= 4)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct time, %i", value);

	g_object_unref (db);
	egg_test_end (test);
}
#endif

