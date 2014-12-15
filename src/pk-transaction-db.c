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
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-common.h>

#include "pk-shared.h"

#include "pk-cleanup.h"
#include "pk-transaction-db.h"

static void     pk_transaction_db_finalize	(GObject        *object);

#define PK_TRANSACTION_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbPrivate))

struct PkTransactionDbPrivate
{
	gboolean		 loaded;
	sqlite3			*db;
	guint			 job_count;
	guint			 database_save_id;
};

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
pk_transaction_db_add_transaction_cb (void *data,
				      gint argc,
				      gchar **argv,
				      gchar **col_name)
{
	PkTransactionPast *item;
	GList **list = (GList **) data;
	gint i;
	gchar *col;
	gchar *value;
	guint temp;
	gboolean ret;

	item = pk_transaction_past_new ();
	for (i = 0; i < argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (g_strcmp0 (col, "succeeded") == 0) {
			ret = pk_strtouint (value, &temp);
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
			ret = pk_strtouint (value, &temp);
			if (ret)
				g_object_set (item, "uid", temp, NULL);
		} else if (g_strcmp0 (col, "duration") == 0 && value != NULL) {
			ret = pk_strtouint (value, &temp);
			if (!ret) {
				g_warning ("failed to parse duration: %s", value);
			} else if (temp > 60 * 60 * 12 * 1000) {
				g_warning ("insane duration: %i", temp);
			} else {
				g_object_set (item, "duration", temp, NULL);
			}
		} else {
			g_warning ("%s = %s", col, value);
		}
	}

	/* add to start of the list */
	*list = g_list_prepend (*list, item);

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

	rc = sqlite3_exec (tdb->priv->db, sql, NULL, tdb, &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s", error_msg);
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

	for (i = 0; i < argc; i++) {
		col = col_name[i];
		value = argv[i];
		if (g_strcmp0 (col, "timespec") == 0)
			*timespec = g_strdup (value);
		else
			g_warning ("%s = %s", col, value);
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
	_cleanup_free_ gchar *statement = NULL;
	_cleanup_free_ gchar *timespec = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), 0);
	g_return_val_if_fail (tdb->priv->db != NULL, 0);

	role_text = pk_role_enum_to_string (role);

	statement = g_strdup_printf ("SELECT timespec FROM last_action WHERE role = '%s'", role_text);
	rc = sqlite3_exec (tdb->priv->db, statement,
			   pk_time_action_sqlite_callback, &timespec, &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return G_MAXUINT;
	}
	if (timespec == NULL)
		return G_MAXUINT;

	/* work out the difference */
	return pk_transaction_db_iso8601_difference (timespec);
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
	guint since;
	_cleanup_free_ gchar *statement = NULL;
	_cleanup_free_ gchar *timespec = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (tdb->priv->db != NULL, FALSE);

	timespec = pk_iso8601_present ();
	role_text = pk_role_enum_to_string (role);

	/* get the previous entry */
	since = pk_transaction_db_action_time_since (tdb, role);
	if (since == G_MAXUINT) {
		statement = g_strdup_printf ("INSERT INTO last_action (role, timespec) VALUES ('%s', '%s')", role_text, timespec);
	} else {
		statement = g_strdup_printf ("UPDATE last_action SET timespec = '%s' WHERE role = '%s'", timespec, role_text);
	}

	/* update or insert the entry */
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_db_get_list:
 **/
GList *
pk_transaction_db_get_list (PkTransactionDb *tdb, guint limit)
{
	gchar *error_msg = NULL;
	gint rc;
	GList *list = NULL;
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), NULL);

	if (limit == 0) {
		statement = g_strdup ("SELECT transaction_id, timespec, succeeded, duration, role, data, uid, cmdline "
				      "FROM transactions ORDER BY timespec DESC");
	} else {
		statement = g_strdup_printf ("SELECT transaction_id, timespec, succeeded, duration, role, data, uid, cmdline "
					     "FROM transactions ORDER BY timespec DESC LIMIT %i", limit);
	}
	rc = sqlite3_exec (tdb->priv->db,
			   statement,
			   pk_transaction_db_add_transaction_cb,
			   &list,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
out:
	return list;
}

/**
 * pk_transaction_db_add:
 **/
gboolean
pk_transaction_db_add (PkTransactionDb *tdb, const gchar *tid)
{
	_cleanup_free_ gchar *timespec = NULL;
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	timespec = pk_iso8601_present ();
	statement = g_strdup_printf ("INSERT INTO transactions (transaction_id, timespec) VALUES ('%s', '%s')", tid, timespec);
	pk_transaction_db_sql_statement (tdb, statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_role:
 **/
gboolean
pk_transaction_db_set_role (PkTransactionDb *tdb, const gchar *tid, PkRoleEnum role)
{
	const gchar *role_text;
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	role_text = pk_role_enum_to_string (role);
	statement = g_strdup_printf ("UPDATE transactions SET role = '%s' WHERE transaction_id = '%s'", role_text, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_uid:
 **/
gboolean
pk_transaction_db_set_uid (PkTransactionDb *tdb, const gchar *tid, guint uid)
{
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = g_strdup_printf ("UPDATE transactions SET uid = '%i' WHERE transaction_id = '%s'", uid, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_cmdline:
 **/
gboolean
pk_transaction_db_set_cmdline (PkTransactionDb *tdb, const gchar *tid, const gchar *cmdline)
{
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = g_strdup_printf ("UPDATE transactions SET cmdline = '%s' WHERE transaction_id = '%s'", cmdline, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_data:
 **/
gboolean
pk_transaction_db_set_data (PkTransactionDb *tdb, const gchar *tid, const gchar *data)
{
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	/* TODO: we have to be careful of SQL injection attacks */
	statement = g_strdup_printf ("UPDATE transactions SET data = \"%s\" WHERE transaction_id = '%s'",
				     data, tid);
	pk_transaction_db_sql_statement (tdb, statement);
	return TRUE;
}

/**
 * pk_transaction_db_set_finished:
 * @runtime: time in ms
 *
 **/
gboolean
pk_transaction_db_set_finished (PkTransactionDb *tdb, const gchar *tid, gboolean success, guint runtime)
{
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	statement = g_strdup_printf ("UPDATE transactions SET succeeded = %i, duration = %i WHERE transaction_id = '%s'",
				     success, runtime, tid);
	pk_transaction_db_sql_statement (tdb, statement);
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
		return 0;
	}
	pk_strtouint (argv[0], &tdb->priv->job_count);
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
	for (i = 0; i < length; i++) {
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
	gchar *error_msg = NULL;
	gint rc;
	_cleanup_free_ gchar *statement = NULL;

	/* not loaded! */
	if (tdb->priv->db == NULL) {
		g_warning ("PkTransactionDb not loaded");
		goto out;
	}

	/* force fsync as we don't want to repeat this number */
	sqlite3_exec (tdb->priv->db, "PRAGMA synchronous=ON", NULL, NULL, NULL);

	/* save the job count */
	statement = g_strdup_printf ("UPDATE config SET value = '%i' WHERE key = 'job_count'",
				     tdb->priv->job_count);
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("failed to set job id: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* turn off fsync */
	sqlite3_exec (tdb->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
out:
	tdb->priv->database_save_id = 0;
	return FALSE;
}

/**
 * pk_transaction_db_generate_id:
 **/
gchar *
pk_transaction_db_generate_id (PkTransactionDb *tdb)
{
	gchar *tid = NULL;
	_cleanup_free_ gchar *rand_str = NULL;

	/* increment */
	tdb->priv->job_count++;
	g_debug ("job count now %i", tdb->priv->job_count);

	/* we don't need to wait for the database write, just do this the
	 * next time we are idle (but ensure we do this on shutdown) */
	if (tdb->priv->database_save_id == 0) {
		tdb->priv->database_save_id =
			g_idle_add_full (G_PRIORITY_LOW, (GSourceFunc)
					 pk_transaction_db_defer_write_job_count_cb, tdb, NULL);
		g_source_set_name_by_id (tdb->priv->database_save_id, "[PkTransactionDb] save");
	}

	/* make the tid */
	rand_str = pk_transaction_db_get_random_hex_string (8);
	tid = g_strdup_printf ("/%i_%s", tdb->priv->job_count, rand_str);
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

	for (i = 0; i < argc; i++) {
		if (g_strcmp0 (col_name[i], "proxy_http") == 0) {
			item->proxy_http = g_strdup (argv[i]);
			item->set = TRUE;
		} else if (g_strcmp0 (col_name[i], "proxy_https") == 0) {
			item->proxy_https = g_strdup (argv[i]);
			item->set = TRUE;
		} else if (g_strcmp0 (col_name[i], "proxy_ftp") == 0) {
			item->proxy_ftp = g_strdup (argv[i]);
			item->set = TRUE;
		} else if (g_strcmp0 (col_name[i], "proxy_socks") == 0) {
			item->proxy_socks = g_strdup (argv[i]);
			item->set = TRUE;
		} else if (g_strcmp0 (col_name[i], "no_proxy") == 0) {
			item->no_proxy = g_strdup (argv[i]);
			item->set = TRUE;
		} else if (g_strcmp0 (col_name[i], "pac") == 0) {
			item->pac = g_strdup (argv[i]);
			item->set = TRUE;
		} else {
			g_warning ("%s = %s", col_name[i], argv[i]);
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
	gboolean ret = FALSE;
	gint rc;
	PkTransactionDbProxyItem *item;
	_cleanup_free_ gchar *statement = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);
	g_return_val_if_fail (uid != G_MAXUINT, FALSE);

	/* get existing data */
	item = g_new0 (PkTransactionDbProxyItem, 1);
	statement = g_strdup_printf ("SELECT proxy_http, proxy_https, proxy_ftp, proxy_socks, no_proxy, pac FROM proxy WHERE uid = '%i' AND session = '%s' LIMIT 1",
				     uid, session);
	rc = sqlite3_exec (tdb->priv->db, statement,
			   pk_transaction_sqlite_proxy_cb,
			   item,
			   &error_msg);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	/* nothing matched */
	if (!item->set)
		goto out;

	/* success, even if we got no data */
	ret = TRUE;

	/* copy data */
	if (proxy_http != NULL)
		*proxy_http = g_strdup (item->proxy_http);
	if (proxy_https != NULL)
		*proxy_https = g_strdup (item->proxy_https);
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
	gboolean ret = FALSE;
	gint rc;
	sqlite3_stmt *statement = NULL;
	_cleanup_free_ gchar *timespec = NULL;
	_cleanup_free_ gchar *proxy_http_tmp = NULL;

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
					 "proxy_https = ?, "
					 "proxy_ftp = ?, "
					 "proxy_socks = ?, "
					 "no_proxy = ?, "
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
	g_type_class_add_private (klass, sizeof (PkTransactionDbPrivate));
}

/**
 * pk_transaction_db_ensure_file_directory:
 **/
static void
pk_transaction_db_ensure_file_directory (const gchar *path)
{
	_cleanup_free_ gchar *parent = NULL;
	parent = g_path_get_dirname (path);
	if (g_mkdir_with_parents (parent, 0755) == -1)
		g_warning ("%s", g_strerror (errno));
}

/**
 * pk_transaction_db_execute:
 **/
static gboolean
pk_transaction_db_execute (PkTransactionDb *tdb,
			   const gchar *statement,
			   GError **error)
{
	gboolean ret = TRUE;
	gint rc;

	/* wrap this up */
	rc = sqlite3_exec (tdb->priv->db, statement, NULL, NULL, NULL);
	if (rc != SQLITE_OK) {
		ret = FALSE;
		g_set_error (error,
			     1, 0,
			     "Failed to execute statement '%s': %s",
			     statement,
			     sqlite3_errmsg (tdb->priv->db));
	}
	return ret;
}

/**
 * pk_transaction_db_load:
 **/
gboolean
pk_transaction_db_load (PkTransactionDb *tdb, GError **error)
{
	const gchar *statement;
	gchar *error_msg = NULL;
	gchar *text;
	GError *error_local = NULL;
	gint rc;

	g_return_val_if_fail (PK_IS_TRANSACTION_DB (tdb), FALSE);

	/* already loaded */
	if (tdb->priv->loaded)
		return TRUE;

	g_debug ("trying to open database '%s'", PK_DB_DIR "/transactions.db");
	pk_transaction_db_ensure_file_directory (PK_DB_DIR "/transactions.db");
	rc = sqlite3_open (PK_DB_DIR "/transactions.db", &tdb->priv->db);
	if (rc != SQLITE_OK) {
		g_set_error (error,
			     1, 0,
			     "Can't open transaction database: %s",
			     sqlite3_errmsg (tdb->priv->db));
		sqlite3_close (tdb->priv->db);
		return FALSE;
	}

	/* we don't need to keep doing fsync */
	if (!pk_transaction_db_execute (tdb, "PRAGMA synchronous=OFF", error))
		return FALSE;

	/* check transactions */
	if (!pk_transaction_db_execute (tdb, "SELECT * FROM transactions LIMIT 1", &error_local)) {
		g_debug ("creating table to repair: %s", error_local->message);
		g_clear_error (&error_local);
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
		if (!pk_transaction_db_execute (tdb, statement, error))
			return FALSE;
	}

	/* check last_action (since 0.3.10) */
	if (!pk_transaction_db_execute (tdb, "SELECT * FROM last_action LIMIT 1", &error_local)) {
		g_debug ("adding last action details: %s", error_local->message);
		g_clear_error (&error_local);
		statement = "CREATE TABLE last_action (role TEXT PRIMARY KEY, timespec TEXT);";
		if (!pk_transaction_db_execute (tdb, statement, error))
			return FALSE;
	}

	/* check config (since 0.4.6) */
	if (!pk_transaction_db_execute (tdb, "SELECT * FROM config LIMIT 1", &error_local)) {
		g_debug ("adding config: %s", error_local->message);
		g_clear_error (&error_local);

		statement = "CREATE TABLE config (key TEXT PRIMARY KEY, value TEXT);";
		if (!pk_transaction_db_execute (tdb, statement, error))
			return FALSE;

		/* save job id */
		text = g_strdup_printf ("INSERT INTO config (key, value) VALUES ('job_count', '%i')", 1);
		if (!pk_transaction_db_execute (tdb, text, error))
			return FALSE;
		g_free (text);
	} else {
		/* get the job count */
		statement = "SELECT value FROM config WHERE key = 'job_count'";
		rc = sqlite3_exec (tdb->priv->db, statement, pk_transaction_sqlite_job_id_cb, tdb, &error_msg);
		if (rc != SQLITE_OK) {
			g_set_error (error, 1, 0,
				     "failed to get job id: %s", error_msg);
			sqlite3_free (error_msg);
			return FALSE;
		}
		g_debug ("job count is now at %i", tdb->priv->job_count);
	}

	/* session proxy saving (since 0.5.1) */
	if (!pk_transaction_db_execute (tdb, "SELECT * FROM proxy LIMIT 1", &error_local)) {
		g_debug ("adding table proxy: %s", error_local->message);
		g_clear_error (&error_local);
		statement = "CREATE TABLE proxy (created TEXT, proxy_http TEXT, proxy_https TEXT, proxy_ftp TEXT, proxy_socks TEXT, no_proxy TEXT, pac TEXT, uid INTEGER, session TEXT);";
		if (!pk_transaction_db_execute (tdb, statement, error))
			return FALSE;
	}

	/* try to set correct permissions */
	g_chmod (PK_DB_DIR "/transactions.db", 0644);

	/* success */
	tdb->priv->loaded = TRUE;
	return TRUE;
}

/**
 * pk_transaction_db_init:
 **/
static void
pk_transaction_db_init (PkTransactionDb *tdb)
{
	tdb->priv = PK_TRANSACTION_DB_GET_PRIVATE (tdb);
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

