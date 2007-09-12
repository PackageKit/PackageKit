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

#include "pk-debug.h"
#include "pk-transaction-db.h"
#include "pk-marshal.h"

static void     pk_transaction_db_class_init	(PkTransactionDbClass *klass);
static void     pk_transaction_db_init		(PkTransactionDb      *tdb);
static void     pk_transaction_db_finalize	(GObject        *object);

#define PK_TRANSACTION_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_DB, PkTransactionDbPrivate))
#define PK_TRANSACTION_DB_FILE		DATABASEDIR "/transactions.db"

struct PkTransactionDbPrivate
{
	sqlite3			*db;
};

G_DEFINE_TYPE (PkTransactionDb, pk_transaction_db, G_TYPE_OBJECT)

/**
 * pk_transaction_db_add:
 **/
gboolean
pk_transaction_db_add (PkTransactionDb *tdb, const gchar *tid)
{
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
	g_type_class_add_private (klass, sizeof (PkTransactionDbPrivate));
}

/**
 * pk_transaction_sqlite_callback:
 **/
static gint
pk_transaction_sqlite_callback (void *data, gint argc, gchar **argv, gchar **col_name)
{
//	PkTransactionDb *tdb = PK_TRANSACTION_DB (data);
	gint i;
	for (i=0; i<argc; i++) {
		g_print ("%s = %s\n", col_name[i], argv[i]);
	}
	g_print ("\n");
	return 0;
}

/**
 * pk_transaction_db_init:
 **/
static void
pk_transaction_db_init (PkTransactionDb *tdb)
{
	const gchar *statement;
	gchar *error_msg = NULL;
	gint rc;

	tdb->priv = PK_TRANSACTION_DB_GET_PRIVATE (tdb);
	pk_debug ("trying to open database '%s'", PK_TRANSACTION_DB_FILE);
	rc = sqlite3_open (PK_TRANSACTION_DB_FILE, &tdb->priv->db);
	if (rc) {
		pk_warning ("Can't open database: %s\n", sqlite3_errmsg (tdb->priv->db));
		sqlite3_close (tdb->priv->db);
		return;
	}

	statement = "SELECT time date FROM transactions WHERE transaction_id = \"13;acaef\"";
	rc = sqlite3_exec (tdb->priv->db, statement, pk_transaction_sqlite_callback, 0, &error_msg);
	if (rc != SQLITE_OK) {
		pk_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
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
	g_return_if_fail (object != NULL);
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
