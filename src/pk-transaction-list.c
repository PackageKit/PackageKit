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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * Transaction Commit Logic:
 *
 * State = COMMIT
 * Transaction.Run()
 * WHEN transaction finished:
 * 	IF error = LOCK_REQUIRED
 * 		IF number_of_tries > 4
 * 			Fail the transaction with CANNOT_GET_LOCK
 * 			Remove the transaction from the FIFO queue
 * 		ELSE
 * 			Reset transaction
 * 			Transaction.Exclusive = TRUE
 * 			number_of_tries++
 * 			Leave transaction in the FIFO queue
 *	ELSE
 * 		State = Finished
 * 		IF Transaction.Exclusive
 * 			Take the first PK_TRANSACTION_STATE_READY transaction which has Transaction.Exclusive == TRUE
 * 			from the list and run it. If there's none, just do nothing
 * 		ELSE
 * 			Do nothing
 * 		Transaction.Destroy()
**/

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
#include <packagekit-glib2/pk-common.h>

#include "pk-conf.h"
#include "pk-transaction.h"
#include "pk-transaction-private.h"
#include "pk-transaction-list.h"

static void     pk_transaction_list_finalize	(GObject        *object);

#define PK_TRANSACTION_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionListPrivate))

/* the interval between each CST, in seconds */
#define PK_TRANSACTION_WEDGE_CHECK			10

struct PkTransactionListPrivate
{
	GPtrArray		*array;
	guint			 unwedge1_id;
	guint			 unwedge2_id;
	PkConf			*conf;
	GPtrArray		*plugins;
	PkBackend		*backend;
};

typedef struct {
	PkTransaction		*transaction;
	PkTransactionList	*list;
	gchar			*tid;
	guint			 remove_id;
	guint			 idle_id;
	guint			 commit_id;
	gulong			 finished_id;
	guint			 uid;
	gboolean		 background;
} PkTransactionItem;

enum {
	PK_TRANSACTION_LIST_CHANGED,
	PK_TRANSACTION_LIST_LAST_SIGNAL
};

static guint signals [PK_TRANSACTION_LIST_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkTransactionList, pk_transaction_list, G_TYPE_OBJECT)
static gpointer pk_transaction_list_object = NULL;

/**
 * pk_transaction_list_get_from_tid:
 **/
static PkTransactionItem *
pk_transaction_list_get_from_tid (PkTransactionList *tlist, const gchar *tid)
{
	guint i;
	GPtrArray *array;
	PkTransactionItem *item;
	const gchar *tmptid;

	g_return_val_if_fail (tlist != NULL, NULL);
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* find the runner with the transaction ID */
	array = tlist->priv->array;
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		tmptid = pk_transaction_get_tid (item->transaction);
		if (g_strcmp0 (tmptid, tid) == 0)
			return item;
	}
	return NULL;
}

/**
 * pk_transaction_list_get_transaction:
 *
 * Return value: Do not unref.
 **/
PkTransaction *
pk_transaction_list_get_transaction (PkTransactionList *tlist, const gchar *tid)
{
	PkTransactionItem *item;
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL)
		return NULL;
	return item->transaction;
}

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
	GPtrArray *array;
	PkRoleEnum role_temp;
	PkTransactionItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	/* check for existing transaction doing an update */
	array = tlist->priv->array;
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		/* we might not have this set yet */
		if (item->transaction == NULL)
			continue;
		/* we might have recently finished this, but not removed it */
		if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_FINISHED)
			continue;
		role_temp = pk_transaction_get_role (item->transaction);
		if (role_temp == role)
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_transaction_list_item_free:
 **/
static void
pk_transaction_list_item_free (PkTransactionItem *item)
{
	g_return_if_fail (item != NULL);
	if (item->finished_id != 0)
		g_signal_handler_disconnect (item->transaction, item->finished_id);
	g_object_unref (item->transaction);
	if (item->commit_id != 0)
		g_source_remove (item->commit_id);
	if (item->idle_id != 0)
		g_source_remove (item->idle_id);
	if (item->remove_id != 0)
		g_source_remove (item->remove_id);
	g_object_unref (item->list);
	g_free (item->tid);
	g_free (item);
}

/**
 * pk_transaction_list_remove_internal:
 **/
static gboolean
pk_transaction_list_remove_internal (PkTransactionList *tlist, PkTransactionItem *item)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* valid item */
	g_debug ("remove transaction %s, item %p", item->tid, item);
	ret = g_ptr_array_remove (tlist->priv->array, item);
	if (!ret) {
		g_warning ("could not remove %p as not present in list", item);
		return FALSE;
	}
	pk_transaction_list_item_free (item);

	return TRUE;
}

/**
 * pk_transaction_list_remove:
 **/
gboolean
pk_transaction_list_remove (PkTransactionList *tlist, const gchar *tid)
{
	PkTransactionItem *item;
	gboolean ret;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL) {
		g_warning ("could not get item");
		return FALSE;
	}
	if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_FINISHED) {
		g_debug ("already finished, so waiting to timeout");
		return FALSE;
	}

	/* we could be being called externally, so stop the automated callback */
	if (item->remove_id != 0) {
		g_source_remove (item->remove_id);
		item->remove_id = 0;
	}

	/* check if we are running, or _just_ about to be run */
	if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_RUNNING) {
		if (item->idle_id == 0) {
			g_warning ("already running, but no idle_id");
			return FALSE;
		}
		/* just about to be run! */
		g_debug ("cancelling the callback to the 'lost' transaction");
		g_source_remove (item->idle_id);
		item->idle_id = 0;
	}
	ret = pk_transaction_list_remove_internal (tlist, item);
	return ret;
}

/**
 * pk_transaction_list_set_background:
 **/
gboolean
pk_transaction_list_set_background (PkTransactionList *tlist, const gchar *tid, gboolean background)
{
	PkTransactionItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL) {
		g_warning ("could not get item");
		return FALSE;
	}
	if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_FINISHED) {
		g_debug ("already finished, so waiting to timeout");
		return FALSE;
	}
	g_debug ("%s is now background: %i", tid, background);
	item->background = background;
	return TRUE;
}

/**
 * pk_transaction_list_remove_item_cb:
 **/
static gboolean
pk_transaction_list_remove_item_cb (PkTransactionItem *item)
{
	g_debug ("transaction %s completed, removing", item->tid);
	pk_transaction_list_remove_internal (item->list, item);
	return FALSE;
}

/**
 * pk_transaction_list_run_idle_cb:
 **/
static gboolean
pk_transaction_list_run_idle_cb (PkTransactionItem *item)
{
	gboolean ret;
	GError *error = NULL;
	PkBackend *backend = NULL;
	PkRoleEnum role;

	g_debug ("actually running %s", item->tid);

	/* load a new backend if the master is busy */
	ret = pk_backend_get_is_finished (item->list->priv->backend);
	role = pk_backend_get_role (item->list->priv->backend);
	if (ret || role == PK_ROLE_ENUM_UNKNOWN) {
		pk_transaction_set_backend (item->transaction,
					    item->list->priv->backend);
	} else {
		g_warning ("Using a new backend instance which is "
			   "not supported at this stage or well tested");
		backend = pk_backend_new ();
		ret = pk_backend_load (backend, &error);
		if (!ret) {
			g_critical ("Failed to load second instance of PkBackend: %s",
				    error->message);
			g_error_free (error);
			goto out;
		}
		pk_transaction_set_backend (item->transaction, backend);
	}

	/* run the transaction */
	ret = pk_transaction_run (item->transaction);
	if (!ret)
		g_error ("failed to run transaction (fatal)");
out:
	/* never try to idle add this again */
	item->idle_id = 0;
	if (backend != NULL)
		g_object_unref (backend);
	return FALSE;
}

/**
 * pk_transaction_list_run_item:
 **/
static void
pk_transaction_list_run_item (PkTransactionList *tlist, PkTransactionItem *item)
{
	/* we set this here so that we don't try starting more than one */
	g_debug ("schedule idle running %s", item->tid);
	pk_transaction_set_state (item->transaction, PK_TRANSACTION_STATE_RUNNING);

	/* add this idle, so that we don't have a deep out-of-order callchain */
	item->idle_id = g_idle_add ((GSourceFunc) pk_transaction_list_run_idle_cb, item);
	g_source_set_name_by_id (item->idle_id, "[PkTransactionList] run");
}

/**
 * pk_transaction_list_get_active_transactions:
 *
 **/
static GPtrArray *
pk_transaction_list_get_active_transactions (PkTransactionList *tlist)
{
	guint i;
	GPtrArray *array;
	GPtrArray *res;
	PkTransactionItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* create array to store the results */
	res = g_ptr_array_new ();

	/* find the runner with the transaction ID */
	array = tlist->priv->array;
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_RUNNING)
			g_ptr_array_add (res, item);
	}

	return res;
}

/**
 * pk_transaction_list_get_next_item:
 **/
static PkTransactionItem *
pk_transaction_list_get_next_item (PkTransactionList *tlist)
{
	PkTransactionItem *item = NULL;
	GPtrArray *array;
	guint i;

	array = tlist->priv->array;

	/* first try the waiting non-background transactions */
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_READY &&
		    !item->background)
			goto out;
	}

	/* then try the other waiting transactions (background tasks) */
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_READY)
			goto out;
	}

	/* nothing to run */
	item = NULL;
out:
	return item;
}

/**
 * pk_transaction_list_transaction_finished_cb:
 **/
static void
pk_transaction_list_transaction_finished_cb (PkTransaction *transaction,
					     PkTransactionList *tlist)
{
	gboolean ret;
	guint timeout;
	PkTransactionItem *item;
	PkTransactionState state;
	const gchar *tid;

	g_return_if_fail (PK_IS_TRANSACTION_LIST (tlist));

	tid = pk_transaction_get_tid (transaction);
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL)
		g_error ("no transaction list item '%s' found!", tid);

	/* transaction is already finished? */
	state = pk_transaction_get_state (item->transaction);
	if (state == PK_TRANSACTION_STATE_FINISHED) {
		g_warning ("transaction %s finished twice!", item->tid);
		return;
	}

	/* we've been 'used' */
	if (item->commit_id != 0) {
		g_source_remove (item->commit_id);
		item->commit_id = 0;
	}

	g_debug ("transaction %s completed, marking finished", item->tid);
	ret = pk_transaction_set_state (item->transaction, PK_TRANSACTION_STATE_FINISHED);
	if (!ret) {
		g_warning ("transaction could not be set finished!");
		return;
	}

	/* we have changed what is running */
	g_debug ("emmitting ::changed");
	g_signal_emit (tlist, signals [PK_TRANSACTION_LIST_CHANGED], 0);

	/* give the client a few seconds to still query the runner */
	timeout = pk_conf_get_int (tlist->priv->conf, "TransactionKeepFinishedTimeout");
	item->remove_id = g_timeout_add_seconds (timeout, (GSourceFunc) pk_transaction_list_remove_item_cb, item);
	g_source_set_name_by_id (item->remove_id, "[PkTransactionList] remove");

	/* do the next transaction now if we have another queued */
	item = pk_transaction_list_get_next_item (tlist);
	if (item != NULL) {
		g_debug ("running %s as previous one finished", item->tid);
		pk_transaction_list_run_item (tlist, item);
	}
}

/**
 * pk_transaction_list_no_commit_cb:
 **/
static gboolean
pk_transaction_list_no_commit_cb (PkTransactionItem *item)
{
	g_warning ("ID %s was not committed in time!", item->tid);
	pk_transaction_list_remove_internal (item->list, item);

	/* never repeat */
	return FALSE;
}

/**
 * pk_transaction_list_get_number_transactions_for_uid:
 *
 * Find all the transactions that are pending from this uid.
 **/
static guint
pk_transaction_list_get_number_transactions_for_uid (PkTransactionList *tlist, guint uid)
{
	guint i;
	GPtrArray *array;
	PkTransactionItem *item;
	guint count = 0;

	/* find all the transactions in progress */
	array = tlist->priv->array;
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		if (item->uid == uid)
			count++;
	}
	return count;
}

/**
 * pk_transaction_list_create:
 **/
gboolean
pk_transaction_list_create (PkTransactionList *tlist,
			    const gchar *tid,
			    const gchar *sender,
			    GError **error)
{
	guint count;
	guint max_count;
	guint timeout;
	gboolean ret = FALSE;
	PkTransactionItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	/* already added? */
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item != NULL) {
		g_set_error (error, 1, 0, "already added %s to list", tid);
		goto out;
	}

	/* add to the array */
	item = g_new0 (PkTransactionItem, 1);
	item->list = g_object_ref (tlist);
	item->tid = g_strdup (tid);
	item->transaction = pk_transaction_new ();
	item->finished_id =
		g_signal_connect_after (item->transaction, "finished",
					G_CALLBACK (pk_transaction_list_transaction_finished_cb), tlist);

	/* set plugins */
	if (tlist->priv->plugins != NULL) {
		pk_transaction_set_plugins (item->transaction,
					    tlist->priv->plugins);
	}

	/* set transaction state */
	ret = pk_transaction_set_state (item->transaction, PK_TRANSACTION_STATE_NEW);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to set transaction state 'new': %s", tid);
		goto out;
	}

	/* set the TID on the transaction */
	ret = pk_transaction_set_tid (item->transaction, item->tid);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to set TID: %s", tid);
		goto out;
	}

	/* set the DBUS sender on the transaction */
	ret = pk_transaction_set_sender (item->transaction, sender);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to set sender: %s", tid);
		goto out;
	}

	/* set the master PkBackend really early (i.e. before
	 * pk_transaction_run is called) as transactions may want to check
	 * to see if roles are possible before accepting actions */
	pk_transaction_set_backend (item->transaction,
				    tlist->priv->backend);

	/* get the uid for the transaction */
	item->uid = pk_transaction_get_uid (item->transaction);

	/* find out the number of transactions this uid already has in progress */
	count = pk_transaction_list_get_number_transactions_for_uid (tlist, item->uid);
	g_debug ("uid=%i, count=%i", item->uid, count);

	/* would this take us over the maximum number of requests allowed */
	max_count = pk_conf_get_int (tlist->priv->conf, "SimultaneousTransactionsForUid");
	if (count > max_count) {
		g_set_error (error, 1, 0, "failed to allocate %s as uid %i already has %i transactions in progress", tid, item->uid, count);

		/* free transaction, as it's never going to be added */
		pk_transaction_list_item_free (item);

		/* failure */
		ret = FALSE;
		goto out;
	}

	/* the client only has a finite amount of time to use the object, else it's destroyed */
	timeout = pk_conf_get_int (tlist->priv->conf, "TransactionCreateCommitTimeout");
	item->commit_id = g_timeout_add_seconds (timeout, (GSourceFunc) pk_transaction_list_no_commit_cb, item);
	g_source_set_name_by_id (item->commit_id, "[PkTransactionList] commit");

	g_debug ("adding transaction %p, item %p", item->transaction, item);
	g_ptr_array_add (tlist->priv->array, item);
out:
	return ret;
}

/**
 * pk_transaction_list_get_locked:
 *
 * Return value: %TRUE if any of the transactions in progress are
 * locking a database or resource and cannot be cancelled.
 **/
gboolean
pk_transaction_list_get_locked (PkTransactionList *tlist)
{
	PkBackend *backend;
	PkTransactionItem *item;
	GPtrArray *array;
	guint i;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	/* anything running? */
	array = pk_transaction_list_get_active_transactions (tlist);
	if (array->len == 0)
		goto out;

	/* check if any backend in running transaction is locked at time */
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);

		backend = pk_transaction_get_backend (item->transaction);
		ret = pk_backend_get_locked (backend);
		if (ret)
			goto out;
	}

out:
	g_ptr_array_free (array, TRUE);
	return ret;
}

/**
 * pk_transaction_list_cancel_background:
 **/
void
pk_transaction_list_cancel_background (PkTransactionList *tlist)
{
	guint i;
	GPtrArray *array;
	PkTransactionItem *item;
	PkTransactionState state;

	g_return_if_fail (PK_IS_TRANSACTION_LIST (tlist));

	/* clear any pending transactions */
	array = tlist->priv->array;
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state >= PK_TRANSACTION_STATE_RUNNING)
			continue;
		g_debug ("cancelling pending transaction %s",
			 item->tid);
		pk_transaction_cancel_bg (item->transaction);
	}

	/* cancel any running transactions */
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state != PK_TRANSACTION_STATE_RUNNING)
			continue;
		if (!item->background)
			continue;
		g_debug ("cancelling running background transaction %s",
			 item->tid);
		pk_transaction_cancel_bg (item->transaction);
	}
}

/**
 * pk_transaction_list_commit:
 **/
gboolean
pk_transaction_list_commit (PkTransactionList *tlist, const gchar *tid)
{
	gboolean ret;
	PkTransactionItem *item;
	PkTransactionItem *item_active;
	GPtrArray *array;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL) {
		g_warning ("could not get transaction: %s", tid);
		return FALSE;
	}

	/* check we're not this again */
	if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_COMMITTED) {
		g_warning ("already committed");
		return FALSE;
	}

	g_debug ("marking transaction %s as committed", item->tid);
	ret = pk_transaction_set_state (item->transaction,
					PK_TRANSACTION_STATE_COMMITTED);
	if (!ret) {
		g_warning ("could not mark as committed");
		return FALSE;
	}

	/* we've been 'used' */
	if (item->commit_id != 0) {
		g_source_remove (item->commit_id);
		item->commit_id = 0;
	}

	/* we will changed what is running */
	g_debug ("emitting ::changed");
	g_signal_emit (tlist, signals [PK_TRANSACTION_LIST_CHANGED], 0);

	/* do the transaction now if we have no other in progress */
	/* FIXME: Hack to keep this function working with get_active_transactions, adjustment to parallel transactions
	 * is done in the para_processing branch right now. */
	array = pk_transaction_list_get_active_transactions (tlist);
	if (array->len == 0) {
		g_debug ("running %s as no others in progress", item->tid);
		pk_transaction_list_run_item (tlist, item);
		goto out;
	}
	item_active = (PkTransactionItem *) g_ptr_array_index (array, 0);

	/* is the current running transaction backtround, and this new
	 * transaction foreground? */
	ret = pk_conf_get_bool (tlist->priv->conf,
				"TransactionCreateCommitTimeout");
	if (!ret)
		goto out;
	if (!item->background && item_active->background) {
		g_debug ("cancelling running background transaction %s "
			 "and instead running %s",
			 item_active->tid, item->tid);
		pk_transaction_cancel_bg (item_active->transaction);
		goto out;
	}
out:
	if (array != NULL)
		g_ptr_array_free (array, TRUE);

	return TRUE;
}

/**
 * pk_transaction_list_get_array:
 **/
gchar **
pk_transaction_list_get_array (PkTransactionList *tlist)
{
	guint i;
	guint length;
	GPtrArray *parray;
	gchar **array;
	PkTransactionItem *item;
	PkTransactionState state;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* use a temp array, as not all are in progress */
	parray = g_ptr_array_new_with_free_func (g_free);

	/* find all the transactions in progress */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		/* only return in the list if its committed and not finished */
		state = pk_transaction_get_state (item->transaction);
		if (state == PK_TRANSACTION_STATE_COMMITTED ||
		    state == PK_TRANSACTION_STATE_READY ||
		    state == PK_TRANSACTION_STATE_RUNNING)
			g_ptr_array_add (parray, g_strdup (item->tid));
	}
	g_debug ("%i transactions in list, %i committed but not finished", length, parray->len);
	array = pk_ptr_array_to_strv (parray);
	g_ptr_array_unref (parray);

	return array;
}

/**
 * pk_transaction_list_get_size:
 **/
guint
pk_transaction_list_get_size (PkTransactionList *tlist)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), 0);
	return tlist->priv->array->len;
}

/**
 * pk_transaction_list_get_state:
 **/
gchar *
pk_transaction_list_get_state (PkTransactionList *tlist)
{
	guint i;
	guint length;
	guint running = 0;
	guint waiting = 0;
	guint no_commit = 0;
	PkRoleEnum role;
	PkTransactionItem *item;
	PkTransactionState state;
	GString *string;

	length = tlist->priv->array->len;
	string = g_string_new ("State:\n");
	if (length == 0)
		goto out;

	/* iterate tasks */
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state == PK_TRANSACTION_STATE_RUNNING)
			running++;
		if (state == PK_TRANSACTION_STATE_COMMITTED)
			waiting++;
		if (state == PK_TRANSACTION_STATE_READY)
			waiting++;
		if (state == PK_TRANSACTION_STATE_NEW)
			no_commit++;
		role = pk_transaction_get_role (item->transaction);
		g_string_append_printf (string, "%0i\t%s\t%s\tstate[%s] background[%i]\n", i,
					pk_role_enum_to_string (role), item->tid,
					pk_transaction_state_to_string (state),
					item->background);
	}

	/* nothing running */
	if (waiting == length)
		g_string_append_printf (string, "WARNING: everything is waiting!\n");
out:
	return g_string_free (string, FALSE);
}

/**
 * pk_transaction_list_print:
 **/
static void
pk_transaction_list_print (PkTransactionList *tlist)
{
	gchar *state;
	state = pk_transaction_list_get_state (tlist);
	g_debug ("%s", state);
	g_free (state);
}

/**
 * pk_transaction_list_is_consistent:
 *
 * This checks the list for consistency so we don't ever deadlock the daemon
 * even if the backends are spectacularly shit
 **/
static gboolean
pk_transaction_list_is_consistent (PkTransactionList *tlist)
{
	guint i;
	gboolean ret = TRUE;
	guint running = 0;
	guint waiting = 0;
	guint no_commit = 0;
	guint length;
	guint unknown_role = 0;
	PkTransactionItem *item;
	PkTransactionState state;
	PkRoleEnum role;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), 0);

	/* find all the transactions */
	length = tlist->priv->array->len;
	if (length == 0)
		goto out;

	/* get state */
	g_debug ("checking consistency as length %i", length);
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state == PK_TRANSACTION_STATE_RUNNING)
			running++;
		if (state == PK_TRANSACTION_STATE_COMMITTED)
			waiting++;
		if (state == PK_TRANSACTION_STATE_READY)
			waiting++;
		if (state == PK_TRANSACTION_STATE_NEW)
			no_commit++;
		role = pk_transaction_get_role (item->transaction);
		if (role == PK_ROLE_ENUM_UNKNOWN)
			unknown_role++;
	}

	/* debug */
	pk_transaction_list_print (tlist);

	/* role not set */
	if (unknown_role != 0)
		g_debug ("%i have an unknown role (CreateTransaction then nothing?)", unknown_role);

	/* some are not committed */
	if (no_commit != 0)
		g_debug ("%i have not been committed and may be pending auth", no_commit);

	/* more than one running */
	if (running > 0) {
		g_debug ("%i are running", running);
	}

	/* nothing running */
	if (waiting == length) {
		g_warning ("everything is waiting!");
		ret = FALSE;
	}
out:
	return ret;
}

/**
 * pk_transaction_list_wedge_check2:
 **/
static gboolean
pk_transaction_list_wedge_check2 (PkTransactionList *tlist)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	g_debug ("checking consistency a second time, as the first was not valid");
	ret = pk_transaction_list_is_consistent (tlist);
	if (ret) {
		g_debug ("panic over");
		goto out;
	}

	/* dump all the state we know */
	g_warning ("dumping data:");
	pk_transaction_list_print (tlist);
out:
	/* never repeat */
	return FALSE;
}

/**
 * pk_transaction_list_wedge_check1:
 **/
static gboolean
pk_transaction_list_wedge_check1 (PkTransactionList *tlist)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);

	ret = pk_transaction_list_is_consistent (tlist);
	if (!ret) {
		/* we have to do this twice, as we might idle add inbetween a transition */
		g_warning ("list is consistent, scheduling another check");
		tlist->priv->unwedge2_id = g_timeout_add (500, (GSourceFunc) pk_transaction_list_wedge_check2, tlist);
		g_source_set_name_by_id (tlist->priv->unwedge2_id, "[PkTransactionList] wedge-check");
	}

	/* always repeat */
	return TRUE;
}

/**
 * pk_transaction_list_set_plugins:
 */
void
pk_transaction_list_set_plugins (PkTransactionList *tlist,
				 GPtrArray *plugins)
{
	g_return_if_fail (PK_IS_TRANSACTION_LIST (tlist));
	tlist->priv->plugins = g_ptr_array_ref (plugins);
}

/**
 * pk_transaction_list_set_backend:
 *
 * Note: this is the master PkBackend that is used when the transaction
 * list is processing one transaction at a time.
 * When parallel transactions are used, then another PkBackend will
 * be instantiated if this PkBackend is busy.
 */
void
pk_transaction_list_set_backend (PkTransactionList *tlist,
				 PkBackend *backend)
{
	g_return_if_fail (PK_IS_TRANSACTION_LIST (tlist));
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (tlist->priv->backend == NULL);
	tlist->priv->backend = g_object_ref (backend);
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
	tlist->priv->conf = pk_conf_new ();
	tlist->priv->array = g_ptr_array_new ();
	tlist->priv->unwedge2_id = 0;
	tlist->priv->unwedge1_id = g_timeout_add_seconds (PK_TRANSACTION_WEDGE_CHECK,
							  (GSourceFunc) pk_transaction_list_wedge_check1, tlist);
	g_source_set_name_by_id (tlist->priv->unwedge1_id, "[PkTransactionList] wedge-check (main)");
}

/**
 * pk_transaction_list_finalize:
 * @object: The object to finalize
 **/
static void
pk_transaction_list_finalize (GObject *object)
{
	PkTransactionList *tlist;

	g_return_if_fail (PK_IS_TRANSACTION_LIST (object));

	tlist = PK_TRANSACTION_LIST (object);

	g_return_if_fail (tlist->priv != NULL);

	if (tlist->priv->unwedge1_id != 0)
		g_source_remove (tlist->priv->unwedge1_id);
	if (tlist->priv->unwedge2_id != 0)
		g_source_remove (tlist->priv->unwedge2_id);

	g_ptr_array_foreach (tlist->priv->array, (GFunc) pk_transaction_list_item_free, NULL);
	g_ptr_array_free (tlist->priv->array, TRUE);
	g_object_unref (tlist->priv->conf);
	if (tlist->priv->plugins != NULL)
		g_ptr_array_unref (tlist->priv->plugins);
	if (tlist->priv->backend != NULL)
		g_object_unref (tlist->priv->backend);

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
	if (pk_transaction_list_object != NULL) {
		g_object_ref (pk_transaction_list_object);
	} else {
		pk_transaction_list_object = g_object_new (PK_TYPE_TRANSACTION_LIST, NULL);
		g_object_add_weak_pointer (pk_transaction_list_object, &pk_transaction_list_object);
	}
	return PK_TRANSACTION_LIST (pk_transaction_list_object);
}

