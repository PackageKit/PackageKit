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
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <packagekit-glib2/pk-common.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-conf.h"
#include "pk-transaction-list.h"
#include "org.freedesktop.PackageKit.Transaction.h"

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
};

typedef struct {
	gboolean		 committed;
	gboolean		 running;
	gboolean		 finished;
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
		/* we might have recently finished this, but not removed it */
		if (item->finished)
			continue;
		/* we might not have this set yet */
		if (item->transaction == NULL)
			continue;
		role_temp = pk_transaction_priv_get_role (item->transaction);
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
	egg_debug ("remove transaction %s, item %p", item->tid, item);
	ret = g_ptr_array_remove (tlist->priv->array, item);
	if (!ret) {
		egg_warning ("could not remove %p as not present in list", item);
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
		egg_warning ("could not get item");
		return FALSE;
	}
	if (item->finished) {
		egg_debug ("already finished, so waiting to timeout");
		return FALSE;
	}

	/* we could be being called externally, so stop the automated callback */
	if (item->remove_id != 0) {
		g_source_remove (item->remove_id);
		item->remove_id = 0;
	}

	/* check if we are running, or _just_ about to be run */
	if (item->running) {
		if (item->idle_id == 0) {
			egg_warning ("already running, but no idle_id");
			return FALSE;
		}
		/* just about to be run! */
		egg_debug ("cancelling the callback to the 'lost' transaction");
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
		egg_warning ("could not get item");
		return FALSE;
	}
	if (item->finished) {
		egg_debug ("already finished, so waiting to timeout");
		return FALSE;
	}
	egg_debug ("%s is now background: %i", tid, background);
	item->background = background;
	return TRUE;
}

/**
 * pk_transaction_list_remove_item_cb:
 **/
static gboolean
pk_transaction_list_remove_item_cb (PkTransactionItem *item)
{
	egg_debug ("transaction %s completed, removing", item->tid);
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

	egg_debug ("actually running %s", item->tid);
	ret = pk_transaction_run (item->transaction);
	if (!ret)
		egg_error ("failed to run transaction (fatal)");

	/* never try to idle add this again */
	item->idle_id = 0;
	return FALSE;
}

/**
 * pk_transaction_list_run_item:
 **/
static void
pk_transaction_list_run_item (PkTransactionList *tlist, PkTransactionItem *item)
{
	/* we set this here so that we don't try starting more than one */
	egg_debug ("schedule idle running %s", item->tid);
	item->running = TRUE;

	/* add this idle, so that we don't have a deep out-of-order callchain */
	item->idle_id = g_idle_add ((GSourceFunc) pk_transaction_list_run_idle_cb, item);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (item->idle_id, "[PkTransactionList] run");
#endif
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
		if (item->committed &&
		    !item->running &&
		    !item->finished &&
		    !item->background)
			goto out;
	}

	/* then try the other waiting transactions (background tasks) */
	for (i=0; i<array->len; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (array, i);
		if (item->committed &&
		    !item->running &&
		    !item->finished)
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
pk_transaction_list_transaction_finished_cb (PkTransaction *transaction, const gchar *exit_text, guint time_ms, PkTransactionList *tlist)
{
	guint timeout;
	PkTransactionItem *item;
	const gchar *tid;

	g_return_if_fail (PK_IS_TRANSACTION_LIST (tlist));

	tid = pk_transaction_get_tid (transaction);
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL)
		egg_error ("no transaction list item '%s' found!", tid);

	/* transaction is already finished? */
	if (item->finished) {
		egg_warning ("transaction %s finished twice!", item->tid);
		return;
	}

	/* we've been 'used' */
	if (item->commit_id != 0) {
		g_source_remove (item->commit_id);
		item->commit_id = 0;
	}

	egg_debug ("transaction %s completed, marking finished", item->tid);
	item->running = FALSE;
	item->finished = TRUE;

	/* if we worked from a cache, we might never have committed this object */
	item->committed = TRUE;

	/* we have changed what is running */
	egg_debug ("emmitting ::changed");
	g_signal_emit (tlist, signals [PK_TRANSACTION_LIST_CHANGED], 0);

	/* give the client a few seconds to still query the runner */
	timeout = pk_conf_get_int (tlist->priv->conf, "TransactionKeepFinishedTimeout");
	item->remove_id = g_timeout_add_seconds (timeout, (GSourceFunc) pk_transaction_list_remove_item_cb, item);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (item->remove_id, "[PkTransactionList] remove");
#endif

	/* do the next transaction now if we have another queued */
	item = pk_transaction_list_get_next_item (tlist);
	if (item != NULL) {
		egg_debug ("running %s as previous one finished", item->tid);
		pk_transaction_list_run_item (tlist, item);
	}
}

/**
 * pk_transaction_list_no_commit_cb:
 **/
static gboolean
pk_transaction_list_no_commit_cb (PkTransactionItem *item)
{
	egg_warning ("ID %s was not committed in time!", item->tid);
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
pk_transaction_list_create (PkTransactionList *tlist, const gchar *tid, const gchar *sender, GError **error)
{
	guint count;
	guint max_count;
	guint timeout;
	gboolean ret = FALSE;
	PkTransactionItem *item;
	DBusGConnection *connection;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	/* already added? */
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item != NULL) {
		g_set_error (error, 1, 0, "already added %s to list", tid);
		egg_warning ("already added %s to list", tid);
		goto out;
	}

	/* add to the array */
	item = g_new0 (PkTransactionItem, 1);
	item->committed = FALSE;
	item->running = FALSE;
	item->finished = FALSE;
	item->transaction = NULL;
	item->background = FALSE;
	item->commit_id = 0;
	item->remove_id = 0;
	item->idle_id = 0;
	item->finished_id = 0;
	item->list = g_object_ref (tlist);
	item->tid = g_strdup (tid);

	/* get another connection */
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	if (connection == NULL)
		egg_error ("no connection");

	item->transaction = pk_transaction_new ();
	item->finished_id =
		g_signal_connect_after (item->transaction, "finished",
					G_CALLBACK (pk_transaction_list_transaction_finished_cb), tlist);

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

	/* get the uid for the transaction */
	g_object_get (item->transaction,
		      "uid", &item->uid,
		      NULL);

	/* find out the number of transactions this uid already has in progress */
	count = pk_transaction_list_get_number_transactions_for_uid (tlist, item->uid);
	egg_debug ("uid=%i, count=%i", item->uid, count);

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

	/* put on the bus */
	dbus_g_object_type_install_info (PK_TYPE_TRANSACTION, &dbus_glib_pk_transaction_object_info);
	dbus_g_connection_register_g_object (connection, item->tid, G_OBJECT (item->transaction));

	/* the client only has a finite amount of time to use the object, else it's destroyed */
	timeout = pk_conf_get_int (tlist->priv->conf, "TransactionCreateCommitTimeout");
	item->commit_id = g_timeout_add_seconds (timeout, (GSourceFunc) pk_transaction_list_no_commit_cb, item);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (item->commit_id, "[PkTransactionList] commit");
#endif

	egg_debug ("adding transaction %p, item %p", item->transaction, item);
	g_ptr_array_add (tlist->priv->array, item);
out:
	return ret;
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

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), 0);

	/* find all the transactions in progress */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (item->committed && item->running && !item->finished)
			count++;
	}
	return count;
}

/**
 * pk_transaction_list_commit:
 **/
gboolean
pk_transaction_list_commit (PkTransactionList *tlist, const gchar *tid)
{
	PkTransactionItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item == NULL) {
		egg_warning ("could not get transaction: %s", tid);
		return FALSE;
	}

	/* check we're not this again */
	if (item->committed) {
		egg_warning ("already committed");
		return FALSE;
	}

	egg_debug ("marking transaction %s as committed", item->tid);
	item->committed = TRUE;

	/* we've been 'used' */
	if (item->commit_id != 0) {
		g_source_remove (item->commit_id);
		item->commit_id = 0;
	}

	/* we will changed what is running */
	egg_debug ("emitting ::changed");
	g_signal_emit (tlist, signals [PK_TRANSACTION_LIST_CHANGED], 0);

	/* do the transaction now if we have no other in progress */
	if (pk_transaction_list_number_running (tlist) == 0) {
		egg_debug ("running %s as no others in progress", item->tid);
		pk_transaction_list_run_item (tlist, item);
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
	guint length;
	GPtrArray *parray;
	gchar **array;
	PkTransactionItem *item;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);

	/* use a temp array, as not all are in progress */
	parray = g_ptr_array_new_with_free_func (g_free);

	/* find all the transactions in progress */
	length = tlist->priv->array->len;
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		/* only return in the list if its committed and not finished */
		if (item->committed && !item->finished)
			g_ptr_array_add (parray, g_strdup (item->tid));
	}
	egg_debug ("%i transactions in list, %i committed but not finished", length, parray->len);
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
	guint wrong = 0;
	guint no_commit = 0;
	PkRoleEnum role;
	PkTransactionItem *item;
	GString *string;

	length = tlist->priv->array->len;
	string = g_string_new ("State:\n");
	if (length == 0)
		goto out;

	/* iterate tasks */
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (item->running)
			running++;
		if (item->committed && !item->finished && !item->running)
			waiting++;
		if (!item->committed && !item->finished && !item->running)
			no_commit++;
		if (!item->committed && item->finished)
			wrong++;
		if (item->running && item->finished)
			wrong++;
		role = pk_transaction_priv_get_role (item->transaction);
		g_string_append_printf (string, "%0i\t%s\t%s\trunning[%i] committed[%i] finished[%i] background[%i]\n", i,
					pk_role_enum_to_string (role), item->tid, item->running,
					item->committed, item->finished, item->background);
	}

	/* wrong flags */
	if (wrong != 0)
		g_string_append_printf (string, "ERROR: %i have inconsistent flags\n", wrong);

	/* more than one running */
	if (running > 1)
		g_string_append_printf (string, "ERROR: %i are running\n", running);

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
	egg_debug ("%s", state);
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
	guint wrong = 0;
	guint no_commit = 0;
	guint length;
	guint unknown_role = 0;
	PkTransactionItem *item;
	PkRoleEnum role;

	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), 0);

	/* find all the transactions */
	length = tlist->priv->array->len;
	if (length == 0)
		goto out;

	/* get state */
	egg_debug ("checking consistency as length %i", length);
	for (i=0; i<length; i++) {
		item = (PkTransactionItem *) g_ptr_array_index (tlist->priv->array, i);
		if (item->running)
			running++;
		if (item->committed && !item->finished && !item->running)
			waiting++;
		if (!item->committed && !item->finished && !item->running)
			no_commit++;
		if (!item->committed && item->finished)
			wrong++;
		if (item->running && item->finished)
			wrong++;
		role = pk_transaction_priv_get_role (item->transaction);
		if (role == PK_ROLE_ENUM_UNKNOWN)
			unknown_role++;
	}

	/* debug */
	pk_transaction_list_print (tlist);

	/* wrong flags */
	if (wrong != 0) {
		egg_warning ("%i have inconsistent flags", wrong);
		ret = FALSE;
	}

	/* role not set */
	if (unknown_role != 0)
		egg_debug ("%i have an unknown role (GetTid then nothing?)", unknown_role);

	/* some are not committed */
	if (no_commit != 0)
		egg_debug ("%i have not been committed and may be pending auth", no_commit);

	/* more than one running */
	if (running > 1) {
		egg_warning ("%i are running", running);
		ret = FALSE;
	}

	/* nothing running */
	if (waiting == length) {
		egg_warning ("everything is waiting!");
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

	egg_debug ("checking consistency a second time, as the first was not valid");
	ret = pk_transaction_list_is_consistent (tlist);
	if (ret) {
		egg_debug ("panic over");
		goto out;
	}

	/* dump all the state we know */
	egg_warning ("dumping data:");
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
		egg_warning ("list is consistent, scheduling another check");
		tlist->priv->unwedge2_id = g_timeout_add (500, (GSourceFunc) pk_transaction_list_wedge_check2, tlist);
#if GLIB_CHECK_VERSION(2,25,8)
		g_source_set_name_by_id (tlist->priv->unwedge2_id, "[PkTransactionList] wedge-check");
#endif
	}

	/* always repeat */
	return TRUE;
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
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (tlist->priv->unwedge1_id, "[PkTransactionList] wedge-check (main)");
#endif
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include "pk-backend-internal.h"
#include "pk-cache.h"
#include "pk-transaction-db.h"

static PkTransactionDb *db = NULL;

/**
 * pk_transaction_list_test_finished_cb:
 **/
static void
pk_transaction_list_test_finished_cb (PkTransaction *transaction, const gchar *exit_text, guint time, EggTest *test)
{
	egg_test_loop_quit (test);
}

/**
 * pk_transaction_list_test_delay_cb:
 **/
static void
pk_transaction_list_test_delay_cb (EggTest *test)
{
	egg_debug ("quitting loop");
	egg_test_loop_quit (test);
}

/**
 * pk_transaction_list_test_get_item:
 **/
static PkTransactionItem *
pk_transaction_list_test_get_item (PkTransactionList *tlist)
{
	PkTransactionItem *item;
	gchar *tid;

	/* get tid */
	tid = pk_transaction_db_generate_id (db);

	/* create PkTransaction instance */
	pk_transaction_list_create (tlist, tid, ":0", NULL);
	item = pk_transaction_list_get_from_tid (tlist, tid);
	g_free (tid);

	/* return object */
	return item;
}

void
pk_transaction_list_test (EggTest *test)
{
	PkTransactionList *tlist;
	PkCache *cache;
	gboolean ret;
	gchar *tid;
	guint size;
	gchar **array;
	PkTransactionItem *item;
	PkTransactionItem *item1;
	PkTransactionItem *item2;
	PkTransactionItem *item3;

	if (!egg_test_start (test, "PkTransactionList"))
		return;

	/* remove the self check file */
#if PK_BUILD_LOCAL
	ret = g_file_test ("./transactions.db", G_FILE_TEST_EXISTS);
	if (ret) {
		egg_test_title (test, "remove old local database");
		egg_warning ("Removing %s", "./transactions.db");
		size = g_unlink ("./transactions.db");
		egg_test_assert (test, (size == 0));
	}
#endif

	/* we get a cache object to reproduce the engine having it ref'd */
	cache = pk_cache_new ();
	db = pk_transaction_db_new ();

	/************************************************************/
	egg_test_title (test, "get a transaction list object");
	tlist = pk_transaction_list_new ();
	egg_test_assert (test, tlist != NULL);

	/************************************************************/
	egg_test_title (test, "make sure we get a valid tid");
	tid = pk_transaction_db_generate_id (db);
	if (tid != NULL)
		egg_test_success (test, "got tid %s", tid);
	else
		egg_test_failed (test, "failed to get tid");

	/************************************************************/
	egg_test_title (test, "create a transaction object");
	ret = pk_transaction_list_create (tlist, tid, ":0", NULL);
	if (ret)
		egg_test_success (test, "created transaction %s", tid);
	else
		egg_test_failed (test, "failed to create transaction");

	/************************************************************/
	egg_test_title (test, "make sure we get the right object back");
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item != NULL &&
	    (g_strcmp0 (item->tid, tid) == 0) &&
	    item->transaction != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "could not find in db");

	/************************************************************/
	egg_test_title (test, "make sure item has correct flags");
	if (item->running == FALSE && item->committed == FALSE && item->finished == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item->running, item->committed, item->finished);

	/************************************************************/
	egg_test_title (test, "get size one we have in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "add again the same tid (should fail)");
	ret = pk_transaction_list_create (tlist, tid, ":0", NULL);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "added the same tid twice");

	/************************************************************/
	egg_test_title (test, "remove without ever committing");
	ret = pk_transaction_list_remove (tlist, tid);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to remove");

	/************************************************************/
	egg_test_title (test, "get size none we have in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/* get a new tid */
	g_free (tid);
	tid = pk_transaction_db_generate_id (db);

	/************************************************************/
	egg_test_title (test, "create another item");
	ret = pk_transaction_list_create (tlist, tid, ":0", NULL);
	if (ret)
		egg_test_success (test, "created transaction %s", tid);
	else
		egg_test_failed (test, "failed to create transaction");

	/************************************************************/
	PkBackend *backend;
	backend = pk_backend_new ();
	egg_test_title (test, "try to load a valid backend");
	ret = pk_backend_set_name (backend, "dummy");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "lock an valid backend");
	ret = pk_backend_lock (backend);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to lock");

	/************************************************************/
	egg_test_title (test, "get from db");
	item = pk_transaction_list_get_from_tid (tlist, tid);
	if (item != NULL &&
	    (g_strcmp0 (item->tid, tid) == 0) &&
	    item->transaction != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "could not find in db");

	g_signal_connect (item->transaction, "finished",
			  G_CALLBACK (pk_transaction_list_test_finished_cb), test);

	/* this tests the run-on-commit action */
	pk_transaction_get_updates (item->transaction, "none", NULL);

	/************************************************************/
	egg_test_title (test, "make sure item has correct flags");
	if (item->running == TRUE && item->committed == TRUE && item->finished == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item->running, item->committed, item->finished);

	/************************************************************/
	egg_test_title (test, "get present role");
	ret = pk_transaction_list_role_present (tlist, PK_ROLE_ENUM_GET_UPDATES);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not get role");

	/************************************************************/
	egg_test_title (test, "get non-present role");
	ret = pk_transaction_list_role_present (tlist, PK_ROLE_ENUM_SEARCH_NAME);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got missing role");

	/************************************************************/
	egg_test_title (test, "get size we have in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/* wait for Finished */
	egg_test_loop_wait (test, 2000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "get size one we have in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (none)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "remove already removed");
	ret = pk_transaction_list_remove (tlist, tid);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "tried to remove");

	/* wait for Cleanup */
	g_timeout_add_seconds (5, (GSourceFunc) pk_transaction_list_test_delay_cb, test);
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "make sure queue empty");
	size = pk_transaction_list_get_size (tlist);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	g_free (tid);

	/************************************************************
	 ***************  Get updates from cache    *****************
	 ************************************************************/
	item = pk_transaction_list_test_get_item (tlist);
	g_signal_connect (item->transaction, "finished",
			  G_CALLBACK (pk_transaction_list_test_finished_cb), test);

	pk_transaction_get_updates (item->transaction, "none", NULL);

	/* wait for cached results*/
	egg_test_loop_wait (test, 1000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "make sure item has correct flags");
	if (item->running == FALSE && item->committed == TRUE && item->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item->running, item->committed, item->finished);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (none, as cached)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "get size we have in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/* wait for Cleanup */
	g_timeout_add_seconds (5, (GSourceFunc) pk_transaction_list_test_delay_cb, test);
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (none, as cached)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "get size we have in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************
	 ****************  Chained transactions    ******************
	 ************************************************************/

	/* create three instances in list */
	item1 = pk_transaction_list_test_get_item (tlist);
	item2 = pk_transaction_list_test_get_item (tlist);
	item3 = pk_transaction_list_test_get_item (tlist);

	/************************************************************/
	egg_test_title (test, "get all items in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) committed");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	g_signal_connect (item1->transaction, "finished",
			  G_CALLBACK (pk_transaction_list_test_finished_cb), test);
	g_signal_connect (item2->transaction, "finished",
			  G_CALLBACK (pk_transaction_list_test_finished_cb), test);
	g_signal_connect (item3->transaction, "finished",
			  G_CALLBACK (pk_transaction_list_test_finished_cb), test);

	/* this starts one action */
	array = g_strsplit ("dave", " ", -1);
	pk_transaction_search_details (item1->transaction, "none", array, NULL);
	g_strfreev (array);

	/* this should be chained after the first action completes */
	array = g_strsplit ("power", " ", -1);
	pk_transaction_search_names (item2->transaction, "none", array, NULL);
	g_strfreev (array);

	/* this starts be chained after the second action completes */
	array = g_strsplit ("paul", " ", -1);
	pk_transaction_search_details (item3->transaction, "none", array, NULL);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (all)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/* wait for first action */
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "get all items in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) (two, first one finished)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "make sure item1 has correct flags");
	if (item1->running == FALSE && item1->committed == TRUE && item1->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item1->running, item1->committed, item1->finished);

	/************************************************************/
	egg_test_title (test, "make sure item2 has correct flags");
	if (item2->running == TRUE && item2->committed == TRUE && item2->finished == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item2->running, item2->committed, item2->finished);

	/************************************************************/
	egg_test_title (test, "make sure item3 has correct flags");
	if (item3->running == FALSE && item3->committed == TRUE && item3->finished == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item3->running, item3->committed, item3->finished);

	/* wait for second action */
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "get all items in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (one)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "make sure item1 has correct flags");
	if (item1->running == FALSE && item1->committed == TRUE && item1->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item1->running, item1->committed, item1->finished);

	/************************************************************/
	egg_test_title (test, "make sure item2 has correct flags");
	if (item2->running == FALSE && item2->committed == TRUE && item2->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item2->running, item2->committed, item2->finished);

	/************************************************************/
	egg_test_title (test, "make sure item3 has correct flags");
	if (item3->running == TRUE && item3->committed == TRUE && item3->finished == FALSE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item3->running, item3->committed, item3->finished);

	/* wait for third action */
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "get all items in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (none)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "make sure item1 has correct flags");
	if (item1->running == FALSE && item1->committed == TRUE && item1->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item1->running, item1->committed, item1->finished);

	/************************************************************/
	egg_test_title (test, "make sure item2 has correct flags");
	if (item2->running == FALSE && item2->committed == TRUE && item2->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item2->running, item2->committed, item2->finished);

	/************************************************************/
	egg_test_title (test, "make sure item3 has correct flags");
	if (item3->running == FALSE && item3->committed == TRUE && item3->finished == TRUE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "wrong flags: running[%i] committed[%i] finished[%i]",
				 item3->running, item3->committed, item3->finished);

	/* wait for Cleanup */
	g_timeout_add_seconds (5, (GSourceFunc) pk_transaction_list_test_delay_cb, test);
	egg_test_loop_wait (test, 10000);
	egg_test_loop_check (test);

	/************************************************************/
	egg_test_title (test, "get both items in queue");
	size = pk_transaction_list_get_size (tlist);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);

	/************************************************************/
	egg_test_title (test, "get transactions (committed, not finished) in progress (neither - again)");
	array = pk_transaction_list_get_array (tlist);
	size = g_strv_length (array);
	if (size == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "size %i", size);
	g_strfreev (array);

	g_object_unref (tlist);
	g_object_unref (backend);
	g_object_unref (cache);
	g_object_unref (db);

	egg_test_end (test);
}
#endif

