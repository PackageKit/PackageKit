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

#include "pk-cleanup.h"
#include "pk-shared.h"
#include "pk-transaction.h"
#include "pk-transaction-private.h"
#include "pk-scheduler.h"

static void     pk_scheduler_finalize	(GObject	*object);

#define PK_SCHEDULER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SCHEDULER, PkSchedulerPrivate))

/* the interval between each CST, in seconds */
#define PK_TRANSACTION_WEDGE_CHECK			10

/* How long the transaction should be queriable after it is finished */
#define PK_TRANSACTION_KEEP_FINISHED_TIMOUT		5 /* s */

/* how many times we should retry a locked transaction */
#define PK_SCHEDULER_MAX_LOCK_RETRIES			4

/* how long the transaction is valid before it's destroyed */
#define PK_SCHEDULER_CREATE_COMMIT_TIMEOUT		300 /* s */

/* maximum number of requests a given user is able to request and queue */
#define PK_SCHEDULER_SIMULTANEOUS_TRANSACTIONS_FOR_UID	500

struct PkSchedulerPrivate
{
	GPtrArray		*array;
	guint			 unwedge1_id;
	guint			 unwedge2_id;
	GKeyFile		*conf;
	GPtrArray		*plugins;
	PkBackend		*backend;
	GDBusNodeInfo		*introspection;
};

typedef struct {
	PkTransaction		*transaction;
	PkScheduler		*scheduler;
	gchar			*tid;
	guint			 remove_id;
	guint			 idle_id;
	guint			 commit_id;
	gulong			 finished_id;
	gulong			 state_changed_id;
	guint			 uid;
	guint			 tries;
} PkSchedulerItem;

enum {
	PK_SCHEDULER_CHANGED,
	PK_SCHEDULER_LAST_SIGNAL
};

static guint signals [PK_SCHEDULER_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkScheduler, pk_scheduler, G_TYPE_OBJECT)

/**
 * pk_scheduler_get_from_tid:
 **/
static PkSchedulerItem *
pk_scheduler_get_from_tid (PkScheduler *scheduler, const gchar *tid)
{
	guint i;
	GPtrArray *array;
	PkSchedulerItem *item;
	const gchar *tmptid;

	g_return_val_if_fail (scheduler != NULL, NULL);
	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), NULL);

	/* find the runner with the transaction ID */
	array = scheduler->priv->array;
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		tmptid = pk_transaction_get_tid (item->transaction);
		if (g_strcmp0 (tmptid, tid) == 0)
			return item;
	}
	return NULL;
}

/**
 * pk_scheduler_get_transaction:
 *
 * Return value: Do not unref.
 **/
PkTransaction *
pk_scheduler_get_transaction (PkScheduler *scheduler, const gchar *tid)
{
	PkSchedulerItem *item;
	item = pk_scheduler_get_from_tid (scheduler, tid);
	if (item == NULL)
		return NULL;
	return item->transaction;
}

/**
 * pk_scheduler_role_present:
 *
 * if there is a queued transaction with this role, useful to avoid having
 * multiple system updates queued
 **/
gboolean
pk_scheduler_role_present (PkScheduler *scheduler, PkRoleEnum role)
{
	guint i;
	GPtrArray *array;
	PkRoleEnum role_temp;
	PkSchedulerItem *item;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);

	/* check for existing transaction doing an update */
	array = scheduler->priv->array;
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
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
 * pk_scheduler_item_free:
 **/
static void
pk_scheduler_item_free (PkSchedulerItem *item)
{
	g_return_if_fail (item != NULL);
	if (item->finished_id != 0)
		g_signal_handler_disconnect (item->transaction, item->finished_id);
	if (item->state_changed_id != 0)
		g_signal_handler_disconnect (item->transaction, item->state_changed_id);
	g_object_unref (item->transaction);
	if (item->commit_id != 0)
		g_source_remove (item->commit_id);
	if (item->idle_id != 0)
		g_source_remove (item->idle_id);
	if (item->remove_id != 0)
		g_source_remove (item->remove_id);
	g_object_unref (item->scheduler);
	g_free (item->tid);
	g_free (item);
}

/**
 * pk_scheduler_remove_internal:
 **/
static gboolean
pk_scheduler_remove_internal (PkScheduler *scheduler, PkSchedulerItem *item)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	/* valid item */
	ret = g_ptr_array_remove (scheduler->priv->array, item);
	if (!ret) {
		g_warning ("could not remove %p as not present in list", item);
		return FALSE;
	}
	pk_scheduler_item_free (item);

	return TRUE;
}

/**
 * pk_scheduler_remove:
 **/
gboolean
pk_scheduler_remove (PkScheduler *scheduler, const gchar *tid)
{
	PkSchedulerItem *item;
	gboolean ret;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	item = pk_scheduler_get_from_tid (scheduler, tid);
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
	ret = pk_scheduler_remove_internal (scheduler, item);
	return ret;
}

/**
 * pk_scheduler_remove_item_cb:
 **/
static gboolean
pk_scheduler_remove_item_cb (gpointer user_data)
{
	PkSchedulerItem *item = (PkSchedulerItem *) user_data;
	g_debug ("transaction %s completed, removing", item->tid);
	pk_scheduler_remove_internal (item->scheduler, item);
	return FALSE;
}

/**
 * pk_scheduler_run_idle_cb:
 **/
static gboolean
pk_scheduler_run_idle_cb (PkSchedulerItem *item)
{
	gboolean ret;

	/* run the transaction */
	pk_transaction_set_backend (item->transaction,
				    item->scheduler->priv->backend);
	ret = pk_transaction_run (item->transaction);
	if (!ret)
		g_error ("failed to run transaction (fatal)");

	/* never try to idle add this again */
	item->idle_id = 0;
	return FALSE;
}

/**
 * pk_scheduler_run_item:
 **/
static void
pk_scheduler_run_item (PkScheduler *scheduler, PkSchedulerItem *item)
{
	/* we set this here so that we don't try starting more than one */
	pk_transaction_set_state (item->transaction, PK_TRANSACTION_STATE_RUNNING);

	/* add this idle, so that we don't have a deep out-of-order callchain */
	item->idle_id = g_idle_add ((GSourceFunc) pk_scheduler_run_idle_cb, item);
	g_source_set_name_by_id (item->idle_id, "[PkScheduler] run");
}

/**
 * pk_scheduler_get_active_transactions:
 *
 **/
static GPtrArray *
pk_scheduler_get_active_transactions (PkScheduler *scheduler)
{
	guint i;
	GPtrArray *array;
	GPtrArray *res;
	PkSchedulerItem *item;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), NULL);

	/* create array to store the results */
	res = g_ptr_array_new ();

	/* find the runner with the transaction ID */
	array = scheduler->priv->array;
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		if (pk_transaction_get_state (item->transaction) == PK_TRANSACTION_STATE_RUNNING)
			g_ptr_array_add (res, item);
	}

	return res;
}

/**
 * pk_scheduler_get_exclusive_running:
 *
 * Return value: Greater than zero if any of the transactions in progress is
 * exclusive (no other exclusive transaction can be run in parallel).
 **/
static guint
pk_scheduler_get_exclusive_running (PkScheduler *scheduler)
{
	PkSchedulerItem *item = NULL;
	guint exclusive_running = 0;
	guint i;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);

	/* anything running? */
	array = pk_scheduler_get_active_transactions (scheduler);
	if (array->len == 0)
		return 0;

	/* check if we have any running locked (exclusive) transaction */
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);

		/* check if a transaction is running in exclusive */
		if (pk_transaction_is_exclusive (item->transaction)) {
			/* should never be more that one, but we count them for sanity checks */
			exclusive_running++;
		}
	}
	return exclusive_running;
}

/**
 * pk_scheduler_get_background_running:
 *
 * Return value: %TRUE if we have running background transactions
 **/
static gboolean
pk_scheduler_get_background_running (PkScheduler *scheduler)
{
	PkSchedulerItem *item = NULL;
	guint i;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);

	/* anything running? */
	array = pk_scheduler_get_active_transactions (scheduler);
	if (array->len == 0)
		return FALSE;

	/* check if we have any running background transaction */
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		if (pk_transaction_get_background (item->transaction))
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_scheduler_get_next_item:
 **/
static PkSchedulerItem *
pk_scheduler_get_next_item (PkScheduler *scheduler)
{
	PkSchedulerItem *item = NULL;
	GPtrArray *array;
	guint i;
	PkTransactionState state;
	gboolean exclusive_running;

	array = scheduler->priv->array;

	/* check for running exclusive transaction */
	exclusive_running = pk_scheduler_get_exclusive_running (scheduler) > 0;

	/* first try the waiting non-background transactions */
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		state = pk_transaction_get_state (item->transaction);

		if ((state == PK_TRANSACTION_STATE_READY) && (!pk_transaction_get_background (item->transaction))) {
			/* check if we can run the transaction now or if we need to wait for lock release */
			if (pk_transaction_is_exclusive (item->transaction)) {
				if (!exclusive_running)
					goto out;
			} else {
				goto out;
			}
		}
	}

	/* then try the other waiting transactions (background tasks) */
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		state = pk_transaction_get_state (item->transaction);

		if (state == PK_TRANSACTION_STATE_READY) {
			/* check if we can run the transaction now or if we need to wait for lock release */
			if (pk_transaction_is_exclusive (item->transaction)) {
				if (!exclusive_running)
					goto out;
			} else {
				goto out;
			}
		}
	}

	/* nothing to run */
	item = NULL;
out:
	return item;
}

/**
 * pk_scheduler_commit:
 **/
static void
pk_scheduler_commit (PkScheduler *scheduler, const gchar *tid)
{
	PkSchedulerItem *item;

	g_return_if_fail (PK_IS_SCHEDULER (scheduler));
	g_return_if_fail (tid != NULL);

	item = pk_scheduler_get_from_tid (scheduler, tid);
	if (item == NULL) {
		g_warning ("could not get transaction: %s", tid);
		return;
	}

	/* treat all transactions as exclusive if backend does not support parallelization */
	if (!pk_backend_supports_parallelization (scheduler->priv->backend))
		pk_transaction_make_exclusive (item->transaction);

	/* we've been 'used' */
	if (item->commit_id != 0) {
		g_source_remove (item->commit_id);
		item->commit_id = 0;
	}

	/* we will changed what is running */
	g_signal_emit (scheduler, signals [PK_SCHEDULER_CHANGED], 0);

	/* is one of the current running transactions background, and this new
	 * transaction foreground? */
	if (!pk_transaction_get_background (item->transaction) &&
	    pk_scheduler_get_background_running (scheduler)) {
		g_debug ("cancelling running background transactions and instead running %s",
			item->tid);
		pk_scheduler_cancel_background (scheduler);
	}

	/* do the transaction now, if possible */
	if (pk_transaction_is_exclusive (item->transaction) == FALSE ||
	    pk_scheduler_get_exclusive_running (scheduler) == 0)
		pk_scheduler_run_item (scheduler, item);
}

/**
 * pk_scheduler_transaction_state_changed_cb:
 **/
static void
pk_scheduler_transaction_state_changed_cb (PkTransaction *transaction,
					   PkTransactionState state,
					   PkScheduler *scheduler)
{
	/* release the ID as we are returning an error */
	if (state == PK_TRANSACTION_STATE_ERROR) {
		pk_scheduler_remove (scheduler, pk_transaction_get_tid (transaction));
		return;
	}
	if (state == PK_TRANSACTION_STATE_READY) {
		pk_scheduler_commit (scheduler, pk_transaction_get_tid (transaction));
		return;
	}
}

/**
 * pk_scheduler_transaction_finished_cb:
 **/
static void
pk_scheduler_transaction_finished_cb (PkTransaction *transaction,
					     PkScheduler *scheduler)
{
	PkSchedulerItem *item;
	PkTransactionState state;
	PkBackendJob *job;
	const gchar *tid;

	g_return_if_fail (PK_IS_SCHEDULER (scheduler));

	tid = pk_transaction_get_tid (transaction);
	item = pk_scheduler_get_from_tid (scheduler, tid);
	if (item == NULL)
		g_error ("no transaction list item '%s' found!", tid);

	/* transaction is already finished? */
	state = pk_transaction_get_state (item->transaction);
	if (state == PK_TRANSACTION_STATE_FINISHED) {
		g_warning ("transaction %s finished twice!", item->tid);
		return;
	}

	if (pk_transaction_is_finished_with_lock_required (item->transaction)) {
		pk_transaction_reset_after_lock_error (item->transaction);

		/* increase the number of tries */
		item->tries++;

		g_debug ("transaction finished and requires lock now, attempt %i", item->tries);

		if (item->tries > PK_SCHEDULER_MAX_LOCK_RETRIES) {
			/* fail the transaction */
			job = pk_transaction_get_backend_job (item->transaction);

			/* we finally failed completely to get a package manager lock */
			pk_backend_job_error_code (job, PK_ERROR_ENUM_CANNOT_GET_LOCK,
						   "Unable to lock package database! There is probably another application using it already.");

			/* now really finish & fail the transaction */
			pk_backend_job_finished (job);
			return;
		}
	} else {
		/* we've been 'used' */
		if (item->commit_id != 0) {
			g_source_remove (item->commit_id);
			item->commit_id = 0;
		}
		pk_transaction_set_state (item->transaction, PK_TRANSACTION_STATE_FINISHED);

		/* give the client a few seconds to still query the runner */
		item->remove_id = g_timeout_add_seconds (PK_TRANSACTION_KEEP_FINISHED_TIMOUT,
							 pk_scheduler_remove_item_cb,
							 item);
		g_source_set_name_by_id (item->remove_id, "[PkScheduler] remove");
	}

	/* try to run the next transaction, if possible */
	item = pk_scheduler_get_next_item (scheduler);
	if (item != NULL) {
		g_debug ("running %s as previous one finished", item->tid);
		pk_scheduler_run_item (scheduler, item);
	}

	/* we have changed what is running */
	g_signal_emit (scheduler, signals [PK_SCHEDULER_CHANGED], 0);
}

/**
 * pk_scheduler_no_commit_cb:
 **/
static gboolean
pk_scheduler_no_commit_cb (gpointer user_data)
{
	PkSchedulerItem *item = (PkSchedulerItem *) user_data;
	g_warning ("ID %s was not committed in time!", item->tid);
	pk_scheduler_remove_internal (item->scheduler, item);

	/* never repeat */
	return FALSE;
}

/**
 * pk_scheduler_get_number_transactions_for_uid:
 *
 * Find all the transactions that are pending from this uid.
 **/
static guint
pk_scheduler_get_number_transactions_for_uid (PkScheduler *scheduler, guint uid)
{
	guint i;
	GPtrArray *array;
	PkSchedulerItem *item;
	guint count = 0;

	/* find all the transactions in progress */
	array = scheduler->priv->array;
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		if (item->uid == uid)
			count++;
	}
	return count;
}

/**
 * pk_scheduler_create:
 **/
gboolean
pk_scheduler_create (PkScheduler *scheduler,
			    const gchar *tid,
			    const gchar *sender,
			    GError **error)
{
	guint count;
	gboolean ret = FALSE;
	PkSchedulerItem *item;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	/* already added? */
	item = pk_scheduler_get_from_tid (scheduler, tid);
	if (item != NULL) {
		g_set_error (error, 1, 0, "already added %s to list", tid);
		return FALSE;
	}

	/* add to the array */
	item = g_new0 (PkSchedulerItem, 1);
	item->scheduler = g_object_ref (scheduler);
	item->tid = g_strdup (tid);
	item->transaction = pk_transaction_new (scheduler->priv->conf,
						scheduler->priv->introspection);
	item->finished_id =
		g_signal_connect_after (item->transaction, "finished",
					G_CALLBACK (pk_scheduler_transaction_finished_cb),
					scheduler);
	item->state_changed_id =
		g_signal_connect_after (item->transaction, "state-changed",
					G_CALLBACK (pk_scheduler_transaction_state_changed_cb),
					scheduler);

	/* set plugins */
	if (scheduler->priv->plugins != NULL) {
		pk_transaction_set_plugins (item->transaction,
					    scheduler->priv->plugins);
	}

	/* set transaction state */
	pk_transaction_set_state (item->transaction, PK_TRANSACTION_STATE_NEW);

	/* set the TID on the transaction */
	ret = pk_transaction_set_tid (item->transaction, item->tid);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to set TID: %s", tid);
		return FALSE;
	}

	/* set the DBUS sender on the transaction */
	ret = pk_transaction_set_sender (item->transaction, sender);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to set sender: %s", tid);
		return FALSE;
	}

	/* set the master PkBackend really early (i.e. before
	 * pk_transaction_run is called) as transactions may want to check
	 * to see if roles are possible before accepting actions */
	if (scheduler->priv->backend != NULL) {
		pk_transaction_set_backend (item->transaction,
					    scheduler->priv->backend);
	}

	/* get the uid for the transaction */
	item->uid = pk_transaction_get_uid (item->transaction);

	/* find out the number of transactions this uid already has in progress */
	count = pk_scheduler_get_number_transactions_for_uid (scheduler, item->uid);

	/* would this take us over the maximum number of requests allowed */
	if (count > PK_SCHEDULER_SIMULTANEOUS_TRANSACTIONS_FOR_UID) {
		g_set_error (error, 1, 0,
			     "failed to allocate %s as uid %i already has "
			     "%i transactions in progress",
			     tid, item->uid, count);
		/* free transaction, as it's never going to be added */
		pk_scheduler_item_free (item);
		return FALSE;
	}

	/* the client only has a finite amount of time to use the object, else it's destroyed */
	item->commit_id = g_timeout_add_seconds (PK_SCHEDULER_CREATE_COMMIT_TIMEOUT,
						 pk_scheduler_no_commit_cb,
						 item);
	g_source_set_name_by_id (item->commit_id, "[PkScheduler] commit");

	g_debug ("adding transaction %p", item->transaction);
	g_ptr_array_add (scheduler->priv->array, item);
	return TRUE;
}

/**
 * pk_scheduler_get_locked:
 *
 * Return value: %TRUE if any of the transactions in progress are
 * locking a database or resource and cannot be cancelled.
 **/
gboolean
pk_scheduler_get_locked (PkScheduler *scheduler)
{
	PkBackendJob *job;
	PkSchedulerItem *item;
	guint i;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);

	/* anything running? */
	array = pk_scheduler_get_active_transactions (scheduler);
	if (array->len == 0)
		return FALSE;

	/* check if any backend in running transaction is locked at time */
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);

		job = pk_transaction_get_backend_job (item->transaction);
		if (job == NULL)
			continue;
		if (pk_backend_job_get_locked (job))
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_scheduler_cancel_background:
 **/
void
pk_scheduler_cancel_background (PkScheduler *scheduler)
{
	guint i;
	GPtrArray *array;
	PkSchedulerItem *item;
	PkTransactionState state;

	g_return_if_fail (PK_IS_SCHEDULER (scheduler));

	/* cancel all running background transactions */
	array = scheduler->priv->array;
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state != PK_TRANSACTION_STATE_RUNNING)
			continue;
		if (!pk_transaction_get_background (item->transaction))
			continue;
		g_debug ("cancelling running background transaction %s",
			 item->tid);
		pk_transaction_cancel_bg (item->transaction);
	}
}

/**
 * pk_scheduler_cancel_queued:
 **/
void
pk_scheduler_cancel_queued (PkScheduler *scheduler)
{
	guint i;
	GPtrArray *array;
	PkSchedulerItem *item;
	PkTransactionState state;

	g_return_if_fail (PK_IS_SCHEDULER (scheduler));

	/* clear any pending transactions */
	array = scheduler->priv->array;
	for (i = 0; i < array->len; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state >= PK_TRANSACTION_STATE_RUNNING)
			continue;
		g_debug ("cancelling pending transaction %s", item->tid);
		pk_transaction_cancel_bg (item->transaction);
	}
}

/**
 * pk_scheduler_get_array:
 **/
gchar **
pk_scheduler_get_array (PkScheduler *scheduler)
{
	guint i;
	guint length;
	PkSchedulerItem *item;
	PkTransactionState state;
	_cleanup_ptrarray_unref_ GPtrArray *parray = NULL;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), NULL);

	/* use a temp array, as not all are in progress */
	parray = g_ptr_array_new_with_free_func (g_free);

	/* find all the transactions in progress */
	length = scheduler->priv->array->len;
	for (i = 0; i < length; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (scheduler->priv->array, i);
		/* only return in the list if its committed and not finished */
		state = pk_transaction_get_state (item->transaction);
		if (state == PK_TRANSACTION_STATE_READY ||
		    state == PK_TRANSACTION_STATE_READY ||
		    state == PK_TRANSACTION_STATE_RUNNING)
			g_ptr_array_add (parray, g_strdup (item->tid));
	}
	g_debug ("%i transactions in list, %i committed but not finished",
		 length, parray->len);
	return pk_ptr_array_to_strv (parray);
}

/**
 * pk_scheduler_get_size:
 **/
guint
pk_scheduler_get_size (PkScheduler *scheduler)
{
	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), 0);
	return scheduler->priv->array->len;
}

/**
 * pk_scheduler_get_state:
 **/
gchar *
pk_scheduler_get_state (PkScheduler *scheduler)
{
	guint i;
	guint length;
	guint running = 0;
	guint waiting = 0;
	guint no_commit = 0;
	PkRoleEnum role;
	PkSchedulerItem *item;
	PkTransactionState state;
	GString *string;

	length = scheduler->priv->array->len;
	string = g_string_new ("State:\n");
	if (length == 0)
		goto out;

	/* iterate tasks */
	for (i = 0; i < length; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (scheduler->priv->array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state == PK_TRANSACTION_STATE_RUNNING)
			running++;
		if (state == PK_TRANSACTION_STATE_READY)
			waiting++;
		if (state == PK_TRANSACTION_STATE_NEW)
			no_commit++;

		role = pk_transaction_get_role (item->transaction);
		g_string_append_printf (string, "%0i\t%s\t%s\tstate[%s] "
					"exclusive[%i] background[%i]\n", i,
					pk_role_enum_to_string (role), item->tid,
					pk_transaction_state_to_string (state),
					pk_transaction_is_exclusive (item->transaction),
					pk_transaction_get_background (item->transaction));
	}

	/* nothing running */
	if (waiting == length)
		g_string_append_printf (string, "WARNING: everything is waiting!\n");
out:
	return g_string_free (string, FALSE);
}

/**
 * pk_scheduler_print:
 **/
static void
pk_scheduler_print (PkScheduler *scheduler)
{
	_cleanup_free_ gchar *state = NULL;
	state = pk_scheduler_get_state (scheduler);
	g_debug ("%s", state);
}

/**
 * pk_scheduler_is_consistent:
 *
 * This checks the list for consistency so we don't ever deadlock the daemon
 * even if the backends are spectacularly shit
 **/
static gboolean
pk_scheduler_is_consistent (PkScheduler *scheduler)
{
	guint i;
	gboolean ret = TRUE;
	guint running = 0;
	guint running_exclusive = 0;
	guint waiting = 0;
	guint no_commit = 0;
	guint length;
	guint unknown_role = 0;
	PkSchedulerItem *item;
	PkTransactionState state;
	PkRoleEnum role;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), 0);

	/* find all the transactions */
	length = scheduler->priv->array->len;
	if (length == 0)
		return TRUE;

	/* get state */
	for (i = 0; i < length; i++) {
		item = (PkSchedulerItem *) g_ptr_array_index (scheduler->priv->array, i);
		state = pk_transaction_get_state (item->transaction);
		if (state == PK_TRANSACTION_STATE_RUNNING)
			running++;
		if (state == PK_TRANSACTION_STATE_READY)
			waiting++;
		if (state == PK_TRANSACTION_STATE_READY)
			waiting++;
		if (state == PK_TRANSACTION_STATE_NEW)
			no_commit++;
		role = pk_transaction_get_role (item->transaction);
		if (role == PK_ROLE_ENUM_UNKNOWN)
			unknown_role++;
	}

	/* role not set */
	if (unknown_role != 0) {
		pk_scheduler_print (scheduler);
		g_debug ("%i have an unknown role (CreateTransaction then nothing?)", unknown_role);
	}

	/* some are not committed */
	if (no_commit != 0) {
		pk_scheduler_print (scheduler);
		g_debug ("%i have not been committed and may be pending auth", no_commit);
	}

	/* more than one running */
	if (running > 0) {
		pk_scheduler_print (scheduler);
		g_debug ("%i are running", running);
	}

	/* more than one exclusive transactions running? */
	running_exclusive = pk_scheduler_get_exclusive_running (scheduler);
	if (running_exclusive > 1) {
		pk_scheduler_print (scheduler);
		g_warning ("%i exclusive transactions running", running_exclusive);
		ret = FALSE;
	}

	/* nothing running */
	if (waiting == length) {
		pk_scheduler_print (scheduler);
		g_warning ("everything is waiting!");
		ret = FALSE;
	}
	return ret;
}

/**
 * pk_scheduler_wedge_check2:
 **/
static gboolean
pk_scheduler_wedge_check2 (PkScheduler *scheduler)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);

	g_debug ("checking consistency a second time");
	ret = pk_scheduler_is_consistent (scheduler);
	if (ret) {
		g_debug ("panic over");
		return FALSE;
	}

	/* dump all the state we know */
	g_warning ("dumping data:");
	pk_scheduler_print (scheduler);

	/* never repeat */
	return FALSE;
}

/**
 * pk_scheduler_wedge_check1:
 **/
static gboolean
pk_scheduler_wedge_check1 (PkScheduler *scheduler)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_SCHEDULER (scheduler), FALSE);

	ret = pk_scheduler_is_consistent (scheduler);
	if (!ret) {
		/* we have to do this twice, as we might idle add inbetween a transition */
		g_warning ("list is consistent, scheduling another check");
		scheduler->priv->unwedge2_id = g_timeout_add (500, (GSourceFunc) pk_scheduler_wedge_check2, scheduler);
		g_source_set_name_by_id (scheduler->priv->unwedge2_id, "[PkScheduler] wedge-check");
	}

	/* always repeat */
	return TRUE;
}

/**
 * pk_scheduler_set_plugins:
 */
void
pk_scheduler_set_plugins (PkScheduler *scheduler,
				 GPtrArray *plugins)
{
	g_return_if_fail (PK_IS_SCHEDULER (scheduler));
	scheduler->priv->plugins = g_ptr_array_ref (plugins);
}

/**
 * pk_scheduler_set_backend:
 *
 * Note: this is the master PkBackend that is used when the transaction
 * list is processing one transaction at a time.
 * When parallel transactions are used, then another PkBackend will
 * be instantiated if this PkBackend is busy.
 */
void
pk_scheduler_set_backend (PkScheduler *scheduler,
				 PkBackend *backend)
{
	g_return_if_fail (PK_IS_SCHEDULER (scheduler));
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (scheduler->priv->backend == NULL);
	scheduler->priv->backend = g_object_ref (backend);
}

/**
 * pk_scheduler_class_init:
 * @klass: The PkSchedulerClass
 **/
static void
pk_scheduler_class_init (PkSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_scheduler_finalize;

	signals [PK_SCHEDULER_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkSchedulerPrivate));
}

/**
 * pk_scheduler_init:
 * @scheduler: This class instance
 **/
static void
pk_scheduler_init (PkScheduler *scheduler)
{
	scheduler->priv = PK_SCHEDULER_GET_PRIVATE (scheduler);
	scheduler->priv->array = g_ptr_array_new ();
	scheduler->priv->introspection = pk_load_introspection (PK_DBUS_INTERFACE_TRANSACTION ".xml",
							    NULL);
	scheduler->priv->unwedge2_id = 0;
	scheduler->priv->unwedge1_id = g_timeout_add_seconds (PK_TRANSACTION_WEDGE_CHECK,
							  (GSourceFunc) pk_scheduler_wedge_check1, scheduler);
	g_source_set_name_by_id (scheduler->priv->unwedge1_id, "[PkScheduler] wedge-check (main)");
}

/**
 * pk_scheduler_finalize:
 * @object: The object to finalize
 **/
static void
pk_scheduler_finalize (GObject *object)
{
	PkScheduler *scheduler;

	g_return_if_fail (PK_IS_SCHEDULER (object));

	scheduler = PK_SCHEDULER (object);

	g_return_if_fail (scheduler->priv != NULL);

	if (scheduler->priv->unwedge1_id != 0)
		g_source_remove (scheduler->priv->unwedge1_id);
	if (scheduler->priv->unwedge2_id != 0)
		g_source_remove (scheduler->priv->unwedge2_id);

	g_ptr_array_foreach (scheduler->priv->array, (GFunc) pk_scheduler_item_free, NULL);
	g_ptr_array_free (scheduler->priv->array, TRUE);
	g_dbus_node_info_unref (scheduler->priv->introspection);
	g_key_file_unref (scheduler->priv->conf);
	if (scheduler->priv->plugins != NULL)
		g_ptr_array_unref (scheduler->priv->plugins);
	if (scheduler->priv->backend != NULL)
		g_object_unref (scheduler->priv->backend);

	G_OBJECT_CLASS (pk_scheduler_parent_class)->finalize (object);
}

/**
 * pk_scheduler_new:
 *
 * Return value: a new PkScheduler object.
 **/
PkScheduler *
pk_scheduler_new (GKeyFile *conf)
{
	PkScheduler *scheduler = PK_SCHEDULER (g_object_new (PK_TYPE_SCHEDULER, NULL));
	scheduler->priv->conf = g_key_file_ref (conf);
	return scheduler;
}

