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

/**
 * SECTION:pk-task-list
 * @short_description: A nice way to keep a list of the jobs being processed
 *
 * These provide a good way to keep a list of the jobs being processed so we
 * can see what type of jobs and thier status easily.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <egg-debug.h>
#include <pk-client.h>
#include <pk-common.h>
#include <pk-task-list.h>
#include <pk-control.h>
#include <pk-connection.h>
#include <pk-marshal.h>

static void     pk_task_list_class_init		(PkTaskListClass *klass);
static void     pk_task_list_init		(PkTaskList      *task_list);
static void     pk_task_list_finalize		(GObject         *object);

#define PK_TASK_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_LIST, PkTaskListPrivate))

/**
 * PkTaskListPrivate:
 *
 * Private #PkTaskList data
 **/
struct _PkTaskListPrivate
{
	GPtrArray		*task_list;
	PkControl		*control;
	PkConnection		*connection;
};

typedef enum {
	PK_TASK_LIST_CHANGED,
	PK_TASK_LIST_STATUS_CHANGED,
	PK_TASK_LIST_MESSAGE,
	PK_TASK_LIST_FINISHED,
	PK_TASK_LIST_ERROR_CODE,
	PK_TASK_LIST_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_LIST_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkTaskList, pk_task_list, G_TYPE_OBJECT)

/**
 * pk_task_list_print:
 **/
gboolean
pk_task_list_print (PkTaskList *tlist)
{
	guint i;
	PkTaskListItem *item;
	guint length;

	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	length = tlist->priv->task_list->len;
	egg_debug ("Tasks:");
	if (length == 0) {
		egg_debug ("[none]...");
		return TRUE;
	}
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		egg_debug ("%s\t%s:%s %s", item->tid, pk_role_enum_to_text (item->role),
			 pk_status_enum_to_text (item->status), item->text);
	}
	return TRUE;
}

/**
 * pk_task_list_contains_role:
 **/
gboolean
pk_task_list_contains_role (PkTaskList *tlist, PkRoleEnum role)
{
	guint i;
	PkTaskListItem *item;
	guint length;

	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	length = tlist->priv->task_list->len;
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		if (item->role == role) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * pk_task_list_find_existing_tid:
 **/
static PkTaskListItem *
pk_task_list_find_existing_tid (PkTaskList *tlist, const gchar *tid)
{
	guint i;
	guint length;
	PkTaskListItem *item;

	length = tlist->priv->task_list->len;
	/* mark previous tasks as non-valid */
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		if (strcmp (item->tid, tid) == 0) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_task_list_status_changed_cb:
 **/
static void
pk_task_list_status_changed_cb (PkClient *client, PkStatusEnum status, PkTaskList *tlist)
{
	gchar *tid;
	PkTaskListItem *item;

	g_return_if_fail (PK_IS_TASK_LIST (tlist));

	tid = pk_client_get_tid (client);

	/* get correct item */
	item = pk_task_list_find_existing_tid (tlist, tid);
	item->status = status;

	egg_debug ("emit status-changed(%s) for %s", pk_status_enum_to_text (status), tid);
	g_signal_emit (tlist, signals [PK_TASK_LIST_STATUS_CHANGED], 0);
	g_free (tid);
}

/**
 * gpk_task_list_finished_cb:
 **/
static void
gpk_task_list_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, PkTaskList *tlist)
{
	g_return_if_fail (PK_IS_TASK_LIST (tlist));
	egg_debug ("emit finished");
	g_signal_emit (tlist, signals [PK_TASK_LIST_FINISHED], 0, client, exit, runtime);
}

/**
 * gpk_task_list_error_code_cb:
 **/
static void
gpk_task_list_error_code_cb (PkClient *client, PkErrorCodeEnum error_code, const gchar *details, PkTaskList *tlist)
{
	g_return_if_fail (PK_IS_TASK_LIST (tlist));
	egg_debug ("emit error-code");
	g_signal_emit (tlist, signals [PK_TASK_LIST_ERROR_CODE], 0, client, error_code, details);
}

/**
 * gpk_task_list_message_cb:
 **/
static void
gpk_task_list_message_cb (PkClient *client, PkMessageEnum message, const gchar *details, PkTaskList *tlist)
{
	g_return_if_fail (PK_IS_TASK_LIST (tlist));
	egg_debug ("emit message");
	g_signal_emit (tlist, signals [PK_TASK_LIST_MESSAGE], 0, client, message, details);
}

/**
 * pk_task_list_refresh:
 *
 * Not normally required, but force a refresh
 **/
gboolean
pk_task_list_refresh (PkTaskList *tlist)
{
	guint i;
	PkTaskListItem *item;
	guint length;
	const gchar *tid;
	const gchar **array;
	GError *error = NULL;
	gboolean ret;

	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	/* get the latest job list */
	array = pk_control_transaction_list_get (tlist->priv->control);

	/* mark previous tasks as non-valid */
	length = tlist->priv->task_list->len;
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		item->valid = FALSE;
	}

	/* copy tasks */
	length = g_strv_length ((gchar **) array);
	for (i=0; i<length; i++) {
		tid = array[i];

		item = pk_task_list_find_existing_tid (tlist, tid);
		if (item == NULL) {
			egg_debug ("new job, have to create %s", tid);
			item = g_new0 (PkTaskListItem, 1);
			item->tid = g_strdup (tid);
			item->monitor = pk_client_new ();
			g_signal_connect (item->monitor, "status-changed",
					  G_CALLBACK (pk_task_list_status_changed_cb), tlist);
			g_signal_connect (item->monitor, "finished",
					  G_CALLBACK (gpk_task_list_finished_cb), tlist);
			g_signal_connect (item->monitor, "error-code",
					  G_CALLBACK (gpk_task_list_error_code_cb), tlist);
			g_signal_connect (item->monitor, "message",
					  G_CALLBACK (gpk_task_list_message_cb), tlist);
			ret = pk_client_set_tid (item->monitor, tid, &error);
			if (!ret) {
				egg_error ("could not set tid: %s", error->message);
				g_error_free (error);
				break;
			}
			pk_client_get_role (item->monitor, &item->role, &item->text, NULL);
			pk_client_get_status (item->monitor, &item->status, NULL);

			/* add to watched array */
			g_ptr_array_add (tlist->priv->task_list, item);
		}

		/* mark as present so we don't garbage collect it */
		item->valid = TRUE;
	}

	/* find and remove non-valid watches */
	for (i=0; i<tlist->priv->task_list->len; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		if (!item->valid) {
			g_object_unref (item->monitor);
			g_ptr_array_remove (tlist->priv->task_list, item);
			g_free (item->tid);
			g_free (item->text);
			g_free (item);
		}
	}

	return TRUE;
}

/**
 * pk_task_list_get_size:
 **/
guint
pk_task_list_get_size (PkTaskList *tlist)
{
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), 0);
	return tlist->priv->task_list->len;
}

/**
 * pk_task_list_get_item:
 **/
PkTaskListItem *
pk_task_list_get_item (PkTaskList *tlist, guint item)
{
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), NULL);
	if (item >= tlist->priv->task_list->len) {
		egg_warning ("item too large!");
		return NULL;
	}
	return g_ptr_array_index (tlist->priv->task_list, item);
}

/**
 * pk_task_list_transaction_list_changed_cb:
 **/
static void
pk_task_list_transaction_list_changed_cb (PkControl *control, PkTaskList *tlist)
{
	g_return_if_fail (PK_IS_TASK_LIST (tlist));
	/* for now, just refresh all the jobs. a little inefficient me thinks */
	pk_task_list_refresh (tlist);
	egg_debug ("emit changed");
	g_signal_emit (tlist , signals [PK_TASK_LIST_CHANGED], 0);
}

/**
 * pk_task_list_connection_changed_cb:
 **/
static void
pk_task_list_connection_changed_cb (PkConnection *connection, gboolean connected, PkTaskList *tlist)
{
	g_return_if_fail (PK_IS_TASK_LIST (tlist));
	egg_debug ("connected=%i", connected);
	if (connected) {
		/* force a refresh so we have valid data*/
		pk_task_list_refresh (tlist);
	}
}

/**
 * pk_task_list_class_init:
 **/
static void
pk_task_list_class_init (PkTaskListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_list_finalize;

	/**
	 * PkTaskList::changed:
	 * @tlist: the #PkTaskList instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the transaction list has changed
	 **/
	signals [PK_TASK_LIST_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskListClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkTaskList::status-changed:
	 * @tlist: the #PkTaskList instance that emitted the signal
	 *
	 * The ::status-changed signal is emitted when one of the status' of the transaction list
	 * clients has changed
	 **/
	signals [PK_TASK_LIST_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskListClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkTaskList::message:
	 * @tlist: the #PkTaskList instance that emitted the signal
	 * @client: the #PkClient instance that caused the signal
	 * @message: the PkMessageEnum type of the message, e.g. %PK_MESSAGE_ENUM_BROKEN_MIRROR
	 * @details: the non-localised message details
	 *
	 * The ::message signal is emitted when the transaction wants to tell
	 * the user something.
	 **/
	signals [PK_TASK_LIST_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskListClass, message),
			      NULL, NULL, pk_marshal_VOID__POINTER_UINT_STRING,
			      G_TYPE_NONE, 3, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_STRING);
	/**
	 * PkTaskList::finished:
	 * @tlist: the #PkTaskList instance that emitted the signal
	 * @client: the #PkClient instance that caused the signal
	 * @exit: the #PkExitEnum status value, e.g. PK_EXIT_ENUM_SUCCESS
	 * @runtime: the time in seconds the transaction has been running
	 *
	 * The ::finished signal is emitted when the transaction is complete.
	 **/
	signals [PK_TASK_LIST_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskListClass, finished),
			      NULL, NULL, pk_marshal_VOID__POINTER_UINT_UINT,
			      G_TYPE_NONE, 3, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_UINT);
	/**
	 * PkClient::error-code:
	 * @tlist: the #PkTaskList instance that emitted the signal
	 * @client: the #PkClient instance that caused the signal
	 * @code: the #PkErrorCodeEnum of the error, e.g. PK_ERROR_ENUM_DEP_RESOLUTION_FAILED
	 * @details: the non-locaised details about the error
	 *
	 * The ::error-code signal is emitted when the transaction wants to
	 * convey an error in the transaction.
	 *
	 * This can only happen once in a transaction.
	 **/
	signals [PK_TASK_LIST_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskListClass, error_code),
			      NULL, NULL, pk_marshal_VOID__POINTER_UINT_STRING,
			      G_TYPE_NONE, 3, G_TYPE_POINTER, G_TYPE_UINT, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (PkTaskListPrivate));
}

/**
 * pk_task_list_init:
 **/
static void
pk_task_list_init (PkTaskList *tlist)
{
	tlist->priv = PK_TASK_LIST_GET_PRIVATE (tlist);

	/* get the changing job list */
	tlist->priv->control = pk_control_new ();
	g_signal_connect (tlist->priv->control, "transaction-list-changed",
			  G_CALLBACK (pk_task_list_transaction_list_changed_cb), tlist);

	/* we maintain a local copy */
	tlist->priv->task_list = g_ptr_array_new ();

	tlist->priv->connection = pk_connection_new ();
	g_signal_connect (tlist->priv->connection, "connection-changed",
			  G_CALLBACK (pk_task_list_connection_changed_cb), tlist);

	/* force a refresh so we have valid data*/
	pk_task_list_refresh (tlist);
}

/**
 * pk_task_list_finalize:
 **/
static void
pk_task_list_finalize (GObject *object)
{
	guint i;
	PkTaskListItem *item;
	PkTaskList *tlist;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (object));
	tlist = PK_TASK_LIST (object);
	g_return_if_fail (tlist->priv != NULL);

	/* remove all watches */
	for (i=0; i<tlist->priv->task_list->len; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		g_object_unref (item->monitor);
		g_free (item->text);
		g_ptr_array_remove (tlist->priv->task_list, item);
		g_free (item);
	}

	g_ptr_array_free (tlist->priv->task_list, TRUE);
	g_object_unref (tlist->priv->control);

	G_OBJECT_CLASS (pk_task_list_parent_class)->finalize (object);
}

/**
 * pk_task_list_new:
 **/
PkTaskList *
pk_task_list_new (void)
{
	PkTaskList *tlist;
	tlist = g_object_new (PK_TYPE_TASK_LIST, NULL);
	return PK_TASK_LIST (tlist);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

static gboolean finished = FALSE;

static void
egg_test_task_list_finished_cb (PkTaskList *tlist, PkClient *client, PkExitEnum exit, guint runtime, EggTest *test)
{
	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (PK_IS_TASK_LIST (tlist));
	finished = TRUE;
	egg_test_loop_quit (test);
}

void
egg_test_task_list (EggTest *test)
{
	PkTaskList *tlist;
	PkClient *client;
	gboolean ret;
	GError *error = NULL;

	if (!egg_test_start (test, "PkTaskList"))
		return;

	/************************************************************/
	egg_test_title (test, "get client");
	tlist = pk_task_list_new ();
	if (tlist != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	g_signal_connect (tlist, "finished",
			  G_CALLBACK (egg_test_task_list_finished_cb), test);

	/************************************************************/
	egg_test_title (test, "search for power");
	client = pk_client_new ();
	ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", &error);
	if (!ret) {
		egg_test_failed (test, "failed: %s", error->message);
		g_error_free (error);
	}
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "we finished?");
	if (finished)
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "not finished");
	}

	g_object_unref (tlist);
	g_object_unref (client);

	egg_test_end (test);
}
#endif

