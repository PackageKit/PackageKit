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
#include <signal.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include "pk-debug.h"
#include "pk-transaction-id.h"
#include "pk-transaction-list.h"

static void     pk_transaction_list_class_init	(PkTransactionListClass *klass);
static void     pk_transaction_list_init	(PkTransactionList      *tlist);
static void     pk_transaction_list_finalize	(GObject        *object);

#define PK_TRANSACTION_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionListPrivate))

struct PkTransactionListPrivate
{
	GPtrArray		*array;
};

enum {
	PK_TRANSACTION_LIST_CHANGED,
	PK_TRANSACTION_LIST_LAST_SIGNAL
};

static guint signals [PK_TRANSACTION_LIST_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTransactionList, pk_transaction_list, G_TYPE_OBJECT)

/**
 * pk_transaction_list_role_present:
 *
 * if there is a queued transaction with this role, useful to avoid having
 * multiple system updates queued
 **/
gboolean
pk_transaction_list_role_present (PkTransactionList *tlist, PkRoleEnum role)
{
	guint i;
	guint length;
	PkRoleEnum role_temp;
	PkTransactionItem *item;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	/* check for existing transaction doing an update */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		pk_backend_get_role (item->backend, &role_temp, NULL);
		if (role_temp == role) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * pk_transaction_list_create:
 **/
PkTransactionItem *
pk_transaction_list_create (PkTransactionList *tlist)
{
	PkTransactionItem *item;

	g_return_val_if_fail (tlist != NULL, NULL);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* add to the array */
	item = g_new0 (PkTransactionItem, 1);
	item->committed = FALSE;
	item->running = FALSE;
	item->backend = NULL;
	item->tid = pk_transaction_id_generate ();
	g_ptr_array_add (tlist->priv->array, item);
	return item;
}

/**
 * pk_transaction_list_remove:
 **/
gboolean
pk_transaction_list_remove (PkTransactionList *tlist, PkTransactionItem *item)
{
	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	/* valid item */
	g_ptr_array_remove (tlist->priv->array, item);
	g_free (item->tid);
	g_free (item);

	/* we have changed what is running */
	pk_debug ("emmitting ::changed");
	g_signal_emit (tlist, signals [PK_TRANSACTION_LIST_CHANGED], 0);

	return TRUE;
}

/**
 * pk_transaction_list_backend_finished_cb:
 **/
static void
pk_transaction_list_backend_finished_cb (PkBackend *backend, PkExitEnum exit, PkTransactionList *tlist)
{
	guint i;
	guint length;
	PkTransactionItem *item;

	g_return_if_fail (tlist != NULL);
	g_return_if_fail (PK_IS_TRANSACTION_LIST (tlist));

	item = pk_transaction_list_get_from_backend (tlist, backend);
	if (item == NULL) {
		pk_error ("moo!");
	}
	pk_debug ("transaction %s completed, removing", item->tid);
	pk_transaction_list_remove (tlist, item);

	/* do the next transaction now if we have another queued */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (item->committed == TRUE && item->running == FALSE) {
			pk_debug ("running %s", item->tid);
			item->running = TRUE;
			pk_backend_run (item->backend);
			break;
		}
	}
}

/**
 * pk_transaction_list_number_running:
 **/
static guint
pk_transaction_list_number_running (PkTransactionList *tlist)
{
	guint i;
	guint count = 0;
	guint length;
	PkTransactionItem *item;

	g_return_val_if_fail (tlist != NULL, 0);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), 0);

	/* find all the transactions in progress */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (item->committed == TRUE && item->running == TRUE) {
			count++;
		}
	}
	return count;
}

/**
 * pk_transaction_list_commit:
 **/
gboolean
pk_transaction_list_commit (PkTransactionList *tlist, PkTransactionItem *item)
{
	PkRoleEnum role;
	gboolean search_okay = TRUE;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	pk_debug ("marking transaction %s as committed", item->tid);
	item->committed = TRUE;

	/* we will changed what is running */
	pk_debug ("emmitting ::changed");
	g_signal_emit (tlist, signals [PK_TRANSACTION_LIST_CHANGED], 0);

	/* connect up finished so we can start the next backend */
	g_signal_connect (item->backend, "finished",
			  G_CALLBACK (pk_transaction_list_backend_finished_cb), tlist);

	/* if we are refreshing the cache then nothing is sacred */
	if (pk_transaction_list_role_present (tlist, PK_ROLE_ENUM_REFRESH_CACHE) == TRUE) {
		search_okay = FALSE;
		/* TODO: other backends might be different, need to abstract */
	}

	/* if it's a query then just do the action (if safe) */
	if (search_okay == TRUE) {
		pk_backend_get_role (item->backend, &role, NULL);
		if (role == PK_ROLE_ENUM_SEARCH_NAME ||
		    role == PK_ROLE_ENUM_SEARCH_FILE ||
		    role == PK_ROLE_ENUM_SEARCH_GROUP ||
		    role == PK_ROLE_ENUM_SEARCH_DETAILS) {
			pk_debug ("running %s", item->tid);
			item->running = TRUE;
			pk_backend_run (item->backend);
			return TRUE;
		}
	}

	/* do the transaction now if we have no other in progress */
	if (pk_transaction_list_number_running (tlist) == 0) {
		pk_debug ("running %s", item->tid);
		item->running = TRUE;
		pk_backend_run (item->backend);
	}

	return TRUE;
}

/**
 * pk_transaction_list_get_array:
 **/
gchar **
pk_transaction_list_get_array (PkTransactionList *tlist)
{
	guint i;
	guint count = 0;
	guint length;
	gchar **array;
	PkTransactionItem *item;

	g_return_val_if_fail (tlist != NULL, NULL);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* find all the transactions in progress */
	length = tlist->priv->array->len;

	/* create new strv list */
	array = g_new0 (gchar *, length);

	pk_debug ("%i active transactions", length);
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		/* only return in the list if it worked */
		if (item->committed == TRUE) {
			array[count] = g_strdup (item->tid);
			count++;
		}
	}
	return array;
}

/**
 * pk_transaction_list_get_size:
 **/
guint
pk_transaction_list_get_size (PkTransactionList *tlist)
{
	g_return_val_if_fail (tlist != NULL, 0);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), 0);
	return tlist->priv->array->len;
}

/**
 * pk_transaction_list_get_from_tid:
 **/
PkTransactionItem *
pk_transaction_list_get_from_tid (PkTransactionList *tlist, const gchar *tid)
{
	guint i;
	guint length;
	PkTransactionItem *item;

	g_return_val_if_fail (tlist != NULL, NULL);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* find the backend with the transaction ID */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (pk_transaction_id_equal (item->tid, tid)) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_transaction_list_get_from_backend:
 **/
PkTransactionItem *
pk_transaction_list_get_from_backend (PkTransactionList *tlist, PkBackend *backend)
{
	guint i;
	guint length;
	PkTransactionItem *item;

	g_return_val_if_fail (tlist != NULL, NULL);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* find the backend with the transaction ID */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (item->backend == backend) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_transaction_list_class_init:
 * @klass: The PkTransactionListClass
 **/
static void
pk_transaction_list_class_init (PkTransactionListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_transaction_list_finalize;

	signals [PK_TRANSACTION_LIST_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkTransactionListPrivate));
}

/**
 * pk_transaction_list_init:
 * @tlist: This class instance
 **/
static void
pk_transaction_list_init (PkTransactionList *tlist)
{
	tlist->priv = PK_TRANSACTION_LIST_GET_PRIVATE (tlist);
	tlist->priv->array = g_ptr_array_new ();
}

/**
 * pk_transaction_list_finalize:
 * @object: The object to finalize
 **/
static void
pk_transaction_list_finalize (GObject *object)
{
	PkTransactionList *tlist;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TRANSACTION_LIST (object));

	tlist = PK_TRANSACTION_LIST (object);

	g_return_if_fail (tlist->priv != NULL);

	g_ptr_array_free (tlist->priv->array, TRUE);
	G_OBJECT_CLASS (pk_transaction_list_parent_class)->finalize (object);
}

/**
 * pk_transaction_list_new:
 *
 * Return value: a new PkTransactionList object.
 **/
PkTransactionList *
pk_transaction_list_new (void)
{
	PkTransactionList *tlist;
	tlist = g_object_new (PK_TYPE_TRANSACTION_LIST, NULL);
	return PK_TRANSACTION_LIST (tlist);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_transaction_list (LibSelfTest *test)
{
	PkTransactionList *tlist;
	gchar *tid;

	if (libst_start (test, "PkTransactionList", CLASS_AUTO) == FALSE) {
		return;
	}

	tlist = pk_transaction_list_new ();

	/************************************************************/
	libst_title (test, "make sure we get a valid tid");
	tid = pk_transaction_id_generate ();
	if (tid != NULL) {
		libst_success (test, "got tid %s", tid);
	} else {
		libst_failed (test, "failed to get tid");
	}
	g_free (tid);

	g_object_unref (tlist);

	libst_end (test);
}
#endif

