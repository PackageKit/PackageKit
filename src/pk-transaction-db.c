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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-common.h>

#include "egg-string.h"

#include "pk-transaction-db.h"

static void     pk_transaction_db_finalize	(GObject        *object);

#define PK_TRANSACTION_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbPrivate))

#define PK_TRANSACTION_DB_ID_FILE_OBSOLETE	LOCALSTATEDIR "/lib/PackageKit/job_count.dat"

struct PkTransactionDbPrivate
{
	sqlite3			*db;
	guint			 job_count;
	guint			 database_save_id;
};

enum {
	SIGNAL_TRANSACTION,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkTransactionDb, pk_transaction_db, G_TYPE_OBJECT)

typedef struct {
	gchar		*proxy_http;
	gchar		*proxy_https;
	gchar		*proxy_ftp;
	gchar		*proxy_socks;
	gchar		*no_proxy;
	gchar		*pac;
	gboolean	set;
} PkTransactionDbProxyItem;

/**
 * pk_transaction_sqlite_transaction_cb:
 **/
static gint
pk_transaction_sqlite_transaction_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkTransactionPast *item;
	PkTransactionDb *tdb = PK_TRANSACTION_DB (data);
	gint i;
	gchar *col;
	gchar *value;
	guint temp;
	gboolean ret;

	g_return_val_if_fail (tdb != NULL, 0);
	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), 0);

	item = pk_transaction_past_new ();
	for (i=0; i<argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (g_strcmp0 (col, "succeeded") == 0) {
			ret = egg_strtouint (value, &temp);
			if (!ret)
				g_warning ("failed to parse succeeded: %s", value);
			if (temp == 1)
				g_object_set (item, "succeeded", TRUE, NULL);
			else
				g_object_set (item, "succeeded", FALSE, NULL);
		} else if (g_strcmp0 (col, "role") == 0) {
			if (value != NULL)
				g_object_set (item, "role", pk_role_enum_from_string (value), NULL);
		} else if (g_strcmp0 (col, "transaction_id") == 0) {
			if (value != NULL)
				g_object_set (item, "tid", value, NULL);
		} else if (g_strcmp0 (col, "timespec") == 0) {
			if (value != NULL)
				g_object_set (item, "timespec", value, NULL);
		} else if (g_strcmp0 (col, "cmdline") == 0) {
			if (value != NULL)
				g_object_set (item, "cmdline", value, NULL);
		} else if (g_strcmp0 (col, "data") == 0) {
			if (value != NULL)
				g_object_set (item, "data", value, NULL);
		} else if (g_strcmp0 (col, "uid") == 0) {
			ret = egg_strtouint (value, &temp);
			if (ret)
				g_object_set (item, "uid", temp, NULL);
		} else if (g_strcmp0 (col, "duration") == 0) {
			ret = egg_strtouint (value, &temp);
			if (!ret) {
				g_warning ("failed to parse duration: %s", value);
			} else if (temp > 60*60*12) {
				g_warning ("insane duration: %i", temp);
			} else {
				g_object_set (item, "duration", temp, NULL);
			}
		} else {
			g_warning ("%s = %s\n", col, value);
		}
	}

	/* emit signal */
	g_signal_emit (tdb, signals [SIGNAL_TRANSACTION], 0, item);

	g_object_unref (item);
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
		g_warning ("SQL error: %s\n", error_msg);
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
		if (g_strcmp0 (col, "timespec") == 0)
			*timespec = g_strdup (value);
		else
			g_warning ("%s = %s\n", col, value);
	}
	return 0;
}

/**
 * pk_transaction_db_iso8601_difference:
 * @isodate: The ISO8601 date to compare
 *
 * Return value: The difference in seconds between the iso8601 date and current
 **/
static guint
pk_transaction_db_iso8601_difference (const gchar *isodate)
{
	GTimeVal timeval_then;
	GTimeVal timeval_now;
	gboolean ret;
	guint time_s;

	g_return_val_if_fail (isodate != NULL, 0);

	/* convert date */
	ret = g_time_val_from_iso8601 (isodate, &timeval_then);
	if (!ret) {
		g_warning ("failed to parse '%s'", isodate);
		return 0;
	}
	g_get_current_time (&timeval_now);

	/* work out difference */
	time_s = timeval_now.tv_sec - timeval_then.tv_sec;

	return time_s;
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

	role_text = pk_role_enum_to_string (role);
	g_debug ("get_time_since_action=%s", role_text);

	statement = g_strdup_printf ("SELECT timespec FROM last_action WHERE role = '%s'", role_text);
	rc = sqlite3_exec (tdb->priv->db, statement, pk_time_action_sqlite_callback, &timespec, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		return G_MAXUINT;
	}
	if (timespec == NULL) {
		g_debug ("no response, assume maximum value");
		return G_MAXUINT;
	}

	/* work out the difference */
	time_ms = pk_transaction_db_iso8601_difference (timespec);
	g_debug ("timespec=%s, difference=%i", timespec, time_ms);
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
	gboolean ret = TRUE;
	gchar *statement;
	gchar *timespec;
	guint since;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (tdb->priv->db != NULL, FALSE);

	timespec = pk_iso8601_present ();
	role_text = pk_role_enum_to_string (role);

	/* get the previous entry */
	since = pk_transaction_db_action_time_since (tdb, role);
	if (since == G_MAXUINT) {
		g_debug ("set action time=%s to %s", role_text, timespec);
		statement = g_strdup_printf ("INSERT INTO last_action (role, timespec) VALUES ('%s', '%s')", role_text, timespec);
	} else {
		g_debug ("reset action time=%s to %s", role_text, timespec);
		statement = g_strdup_printf ("UPDATE last_action SET timespec = '%s' WHERE role = '%s'", timespec, role_text);
	}

	/* update or insert the entry */
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);

	/* did we fail? */
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		ret = FALSE;
		goto out;
	}
out:
	g_free (timespec);
	g_free (statement);
	return ret;
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

	g_debug ("adding transaction %s", tid);

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

	role_text = pk_role_enum_to_string (role);
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
		g_warning ("wrong number of replies: %i", argc);
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
	g_debug ("doing deferred write syncronous");
	statement = g_strdup_printf ("UPDATE config SET value = '%i' WHERE key = 'job_count'", tdb->priv->job_count);
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_error ("failed to set job id: %s\n", error_msg);
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
	g_debug ("job count now %i", tdb->priv->job_count);

	/* we don't need to wait for the database write, just do this the
	 * next time we are idle (but ensure we do this on shutdown) */
	if (tdb->priv->database_save_id == 0) {
		g_debug ("deferring low priority write until idle");
		tdb->priv->database_save_id =
			g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc)
					 pk_transaction_db_defer_write_job_count_cb, tdb, NULL);
#if GLIB_CHECK_VERSION(2,25,8)
		g_source_set_name_by_id (tdb->priv->database_save_id, "[PkTransactionDb] save");
#endif
	}

	/* make the tid */
	rand_str = pk_transaction_db_get_random_hex_string (8);
	tid = g_strdup_printf ("/%i_%s_data", tdb->priv->job_count, rand_str);
	g_free (rand_str);
	return tid;
}

/**
 * pk_transaction_sqlite_proxy_cb:
 **/
static gint
pk_transaction_sqlite_proxy_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkTransactionDbProxyItem *item = (PkTransactionDbProxyItem *) data;
	gint i;

	g_return_val_if_fail (item != NULL, 0);

	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "proxy_http") == 0) {
			item->proxy_http = g_strdup (argv[i]);
			item->set = TRUE;
		} else if (g_strcmp0 (col_name[i], "proxy_ftp") == 0) {
			item->proxy_ftp = g_strdup (argv[i]);
			item->set = TRUE;
		} else {
			g_warning ("%s = %s\n", col_name[i], argv[i]);
		}
	}
	return 0;
}

/**
 * pk_transaction_db_proxy_item_free:
 **/
static void
pk_transaction_db_proxy_item_free (PkTransactionDbProxyItem *item)
{
	if (item == NULL)
		return;
	g_free (item->proxy_http);
	g_free (item->proxy_https);
	g_free (item->proxy_ftp);
	g_free (item->proxy_socks);
	g_free (item->no_proxy);
	g_free (item->pac);
	g_free (item);
}

/**
 * pk_transaction_db_get_proxy:
 * @tdb: the #PkTransactionDb instance
 * @uid: the user ID of the user
 * @session: the ConsoleKit session
 * @proxy_http: the HTTP proxy, or %NULL
 * @proxy_ftp: the FTP proxy, or %NULL
 *
 * Retrieves the proxy information from the database.
 *
 * Return value: %TRUE if we matched a proxy
 **/
gboolean
pk_transaction_db_get_proxy (PkTransactionDb *tdb, guint uid, const gchar *session,
			     gchar **proxy_http,
			     gchar **proxy_https,
			     gchar **proxy_ftp,
			     gchar **proxy_socks,
			     gchar **no_proxy,
			     gchar **pac)
{
	gchar *error_msg = NULL;
	gchar *statement;
	gboolean ret = FALSE;
	gint rc;
	PkTransactionDbProxyItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (uid != G_MAXUINT, FALSE);
	g_return_val_if_fail (proxy_http != NULL, FALSE);
	g_return_val_if_fail (proxy_ftp != NULL, FALSE);

	/* get existing data */
	item = g_new0 (PkTransactionDbProxyItem, 1);
	statement = g_strdup_printf ("SELECT proxy_http, proxy_ftp FROM proxy WHERE uid = '%i' AND session = '%s' LIMIT 1",
				     uid, session);
	rc = sqlite3_exec (tdb->priv->db, statement,
			   pk_transaction_sqlite_proxy_cb,
			   item,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* nothing matched */
	if (!item->set) {
		g_debug ("no data");
		goto out;
	}

	/* success, even if we got no data */
	ret = TRUE;

	/* copy data */
	if (proxy_http != NULL)
		*proxy_http = g_strdup (item->proxy_http);
	if (proxy_https != NULL)
		*proxy_http = g_strdup (item->proxy_https);
	if (proxy_ftp != NULL)
		*proxy_ftp = g_strdup (item->proxy_ftp);
	if (proxy_socks != NULL)
		*proxy_socks = g_strdup (item->proxy_socks);
	if (no_proxy != NULL)
		*no_proxy = g_strdup (item->no_proxy);
	if (pac != NULL)
		*pac = g_strdup (item->pac);
out:
	pk_transaction_db_proxy_item_free (item);
	g_free (statement);
	return ret;
}

/**
 * pk_transaction_db_set_proxy:
 * @tdb: the #PkTransactionDb instance
 * @uid: the user ID of the user
 * @session: the ConsoleKit session
 * @proxy_http: the HTTP proxy
 * @proxy_ftp: the FTP proxy
 *
 * Saves the proxy information to the database.
 *
 * Return value: %TRUE for success
 **/
gboolean
pk_transaction_db_set_proxy (PkTransactionDb *tdb, guint uid,
			     const gchar *session,
			     const gchar *proxy_http,
			     const gchar *proxy_https,
			     const gchar *proxy_ftp,
			     const gchar *proxy_socks,
			     const gchar *no_proxy,
			     const gchar *pac)
{
	gchar *timespec = NULL;
	gchar *proxy_http_tmp = NULL;
	gboolean ret = FALSE;
	gint rc;
	sqlite3_stmt *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (uid != G_MAXUINT, FALSE);

	/* check for previous entries */
	ret = pk_transaction_db_get_proxy (tdb, uid, session,
					   &proxy_http_tmp,
					   NULL,
					   NULL,
					   NULL,
					   NULL,
					   NULL);
	if (ret) {
		g_debug ("updated proxy %s, %s for uid:%i and session:%s",
			 proxy_http, proxy_ftp, uid, session);

		/* prepare statement */
		rc = sqlite3_prepare_v2 (tdb->priv->db,
					 "UPDATE proxy SET "
					 "proxy_http = ?, "
					 "proxy_https = ? , "
					 "proxy_ftp = ? "
					 "proxy_socks = ? "
					 "no_proxy = ? "
					 "pac = ? "
					 "WHERE uid = ? AND session = ?",
					 -1, &statement, NULL);
		if (rc != SQLITE_OK) {
			g_warning ("failed to prepare statement: %s", sqlite3_errmsg (tdb->priv->db));
			goto out;
		}

		/* bind data, so that the freeform proxy text cannot be used to inject SQL */
		sqlite3_bind_text (statement, 1, proxy_http, -1, SQLITE_STATIC);
		sqlite3_bind_text (statement, 2, proxy_https, -1, SQLITE_STATIC);
		sqlite3_bind_text (statement, 3, proxy_ftp, -1, SQLITE_STATIC);
		sqlite3_bind_text (statement, 4, proxy_socks, -1, SQLITE_STATIC);
		sqlite3_bind_text (statement, 5, no_proxy, -1, SQLITE_STATIC);
		sqlite3_bind_text (statement, 6, pac, -1, SQLITE_STATIC);
		sqlite3_bind_int (statement, 7, uid);
		sqlite3_bind_text (statement, 8, session, -1, SQLITE_STATIC);

		/* execute statement */
		rc = sqlite3_step (statement);
		if (rc != SQLITE_DONE) {
			g_warning ("failed to execute statement: %s", sqlite3_errmsg (tdb->priv->db));
			goto out;
		}
		goto out;
	}

	/* insert new entry */
	timespec = pk_iso8601_present ();
	g_debug ("set proxy %s, %s for uid:%i and session:%s", proxy_http, proxy_ftp, uid, session);

	/* prepare statement */
	rc = sqlite3_prepare_v2 (tdb->priv->db,
				 "INSERT INTO proxy (created, uid, session, "
				 "proxy_http, "
				 "proxy_https, "
				 "proxy_ftp, "
				 "proxy_socks, "
				 "no_proxy, "
				 "pac) "
				 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
				 -1, &statement, NULL);
	if (rc != SQLITE_OK) {
		g_warning ("failed to prepare statement: %s", sqlite3_errmsg (tdb->priv->db));
		goto out;
	}

	/* bind data, so that the freeform proxy text cannot be used to inject SQL */
	sqlite3_bind_text (statement, 1, timespec, -1, SQLITE_STATIC);
	sqlite3_bind_int (statement, 2, uid);
	sqlite3_bind_text (statement, 3, session, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 4, proxy_http, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 5, proxy_https, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 6, proxy_ftp, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 7, proxy_socks, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 8, no_proxy, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 9, pac, -1, SQLITE_STATIC);

	/* execute statement */
	rc = sqlite3_step (statement);
	if (rc != SQLITE_DONE) {
		g_warning ("failed to execute statement: %s", sqlite3_errmsg (tdb->priv->db));
		goto out;
	}

	ret = TRUE;
out:
	if (statement != NULL)
		sqlite3_finalize (statement);
	g_free (timespec);
	g_free (proxy_http_tmp);
	return ret;
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
	signals [SIGNAL_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
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
	gchar *text;
	gboolean ret;

	g_return_if_fail (PK_IS_TRANSACTION_DB (tdb));

	tdb->priv = PK_TRANSACTION_DB_GET_PRIVATE (tdb);
	tdb->priv->db = NULL;
	tdb->priv->database_save_id = 0;

	g_debug ("trying to open database '%s'", PK_TRANSACTION_DB_FILE);
	rc = sqlite3_open (PK_TRANSACTION_DB_FILE, &tdb->priv->db);
	if (rc != SQLITE_OK) {
		g_error ("Can't open transaction database: %s\n", sqlite3_errmsg (tdb->priv->db));
		sqlite3_close (tdb->priv->db);
		return;
	}

	/* we don't need to keep doing fsync */
	sqlite3_exec (tdb->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);

	/* check transactions */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM transactions LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
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
		g_debug ("altering table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "ALTER TABLE transactions ADD COLUMN uid INTEGER DEFAULT 0;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE transactions ADD COLUMN cmdline TEXT;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	}

	/* check last_action (since 0.3.10) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM last_action LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("adding last action details: %s", error_msg);
		statement = "CREATE TABLE last_action (role TEXT PRIMARY KEY, timespec TEXT);";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	}

	/* check config (since 0.4.6) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM config LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("adding config: %s", error_msg);
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
			g_warning ("failed to get job id: %s\n", error_msg);
			sqlite3_free (error_msg);
		}
		g_debug ("job count is now at %i", tdb->priv->job_count);
	}

	/* session proxy saving (since 0.5.1) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM proxy LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("altering table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "ALTER TABLE proxy ADD COLUMN proxy_https TEXT;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE proxy ADD COLUMN proxy_socks TEXT;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE proxy ADD COLUMN no_proxy TEXT;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
		statement = "ALTER TABLE proxy ADD COLUMN pac TEXT;";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	}

	/* session no_proxy proxy */
	rc = sqlite3_exec (tdb->priv->db, "SELECT no_proxy FROM proxy LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("adding table proxy: %s", error_msg);
		statement = "CREATE TABLE proxy (created TEXT, proxy_http TEXT, proxy_ftp TEXT, uid INTEGER, session TEXT);";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	}

	/* transaction root (since 0.6.4) */
	rc = sqlite3_exec (tdb->priv->db, "SELECT * FROM root LIMIT 1", NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_debug ("adding table root: %s", error_msg);
		statement = "CREATE TABLE root (created TEXT, root TEXT, uid INTEGER, session TEXT);";
		sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	}
}

/**
 * pk_transaction_sqlite_root_cb:
 **/
static gint
pk_transaction_sqlite_root_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	gchar **root = (gchar **) data;
	gint i;

	g_return_val_if_fail (root != NULL, 0);

	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "root") == 0) {
			*root = g_strdup (argv[i]);
		} else {
			g_warning ("%s = %s\n", col_name[i], argv[i]);
		}
	}
	return 0;
}

/**
 * pk_transaction_db_get_root:
 * @tdb: the #PkTransactionDb instance
 * @uid: the user ID of the user
 * @session: the ConsoleKit session
 * @root: the install root
 *
 * Retrieves the root information from the database.
 *
 * Return value: %TRUE if we matched a root
 **/
gboolean
pk_transaction_db_get_root (PkTransactionDb *tdb, guint uid, const gchar *session,
			     gchar **root)
{
	gchar *error_msg = NULL;
	gchar *statement;
	gboolean ret = FALSE;
	gint rc;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (uid != G_MAXUINT, FALSE);
	g_return_val_if_fail (root != NULL, FALSE);

	/* get existing data */
	statement = g_strdup_printf ("SELECT root FROM root WHERE uid = '%i' AND session = '%s' LIMIT 1",
				     uid, session);
	rc = sqlite3_exec (tdb->priv->db, statement, pk_transaction_sqlite_root_cb, root, &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* nothing matched */
	if (*root == NULL) {
		g_debug ("no data");
		goto out;
	}

	/* success, even if we got no data */
	ret = TRUE;
out:
	g_free (statement);
	return ret;
}

/**
 * pk_transaction_db_set_root:
 * @tdb: the #PkTransactionDb instance
 * @uid: the user ID of the user
 * @session: the ConsoleKit session
 * @root: the HTTP root
 *
 * Saves the root install prefix to the database.
 *
 * Return value: %TRUE for success
 **/
gboolean
pk_transaction_db_set_root (PkTransactionDb *tdb, guint uid, const gchar *session,
			    const gchar *root)
{
	gchar *timespec = NULL;
	gchar *root_tmp = NULL;
	gboolean ret = FALSE;
	gint rc;
	sqlite3_stmt *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (uid != G_MAXUINT, FALSE);

	/* check for previous entries */
	ret = pk_transaction_db_get_root (tdb, uid, session, &root_tmp);
	if (ret) {
		g_debug ("updated root %s for uid:%i and session:%s", root, uid, session);

		/* prepare statement */
		rc = sqlite3_prepare_v2 (tdb->priv->db,
					 "UPDATE root SET root = ? WHERE uid = ? AND session = ?",
					 -1, &statement, NULL);
		if (rc != SQLITE_OK) {
			g_warning ("failed to prepare statement: %s", sqlite3_errmsg (tdb->priv->db));
			goto out;
		}

		/* bind data, so that the freeform root text cannot be used to inject SQL */
		sqlite3_bind_text (statement, 1, root, -1, SQLITE_STATIC);
		sqlite3_bind_int (statement, 2, uid);
		sqlite3_bind_text (statement, 3, session, -1, SQLITE_STATIC);

		/* execute statement */
		rc = sqlite3_step (statement);
		if (rc != SQLITE_DONE) {
			g_warning ("failed to execute statement: %s", sqlite3_errmsg (tdb->priv->db));
			goto out;
		}
		goto out;
	}

	/* insert new entry */
	timespec = pk_iso8601_present ();
	g_debug ("set root %s for uid:%i and session:%s", root, uid, session);

	/* prepare statement */
	rc = sqlite3_prepare_v2 (tdb->priv->db,
				 "INSERT INTO root (created, uid, session, root) VALUES (?, ?, ?, ?)",
				 -1, &statement, NULL);
	if (rc != SQLITE_OK) {
		g_warning ("failed to prepare statement: %s", sqlite3_errmsg (tdb->priv->db));
		goto out;
	}

	/* bind data, so that the freeform root text cannot be used to inject SQL */
	sqlite3_bind_text (statement, 1, timespec, -1, SQLITE_STATIC);
	sqlite3_bind_int (statement, 2, uid);
	sqlite3_bind_text (statement, 3, session, -1, SQLITE_STATIC);
	sqlite3_bind_text (statement, 4, root, -1, SQLITE_STATIC);

	/* execute statement */
	rc = sqlite3_step (statement);
	if (rc != SQLITE_DONE) {
		g_warning ("failed to execute statement: %s", sqlite3_errmsg (tdb->priv->db));
		goto out;
	}

	ret = TRUE;
out:
	if (statement != NULL)
		sqlite3_finalize (statement);
	g_free (timespec);
	g_free (root_tmp);
	return ret;
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

