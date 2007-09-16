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

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-task-client.h"
#include "pk-task-common.h"
#include "pk-task-list.h"
#include "pk-task-monitor.h"
#include "pk-job-list.h"

static void     pk_task_list_class_init		(PkTaskListClass *klass);
static void     pk_task_list_init		(PkTaskList      *task_list);
static void     pk_task_list_finalize		(GObject           *object);

#define PK_TASK_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_LIST, PkTaskListPrivate))

struct PkTaskListPrivate
{
	GPtrArray		*task_list;
	PkJobList		*job_list;
};

typedef enum {
	PK_TASK_LIST_CHANGED,
	PK_TASK_LIST_FINISHED,
	PK_TASK_LIST_ERROR_CODE,
	PK_TASK_LIST_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_LIST_LAST_SIGNAL] = { 0, };

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

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	length = tlist->priv->task_list->len;
	g_print ("Tasks:\n");
	if (length == 0) {
		g_print ("[none]...\n");
		return TRUE;
	}
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		g_print ("%i %s %s", item->job, pk_role_enum_to_text (item->role), item->package_id);
	}
	g_print ("\n");
	return TRUE;
}

/**
 * pk_task_list_find_existing_job:
 **/
static PkTaskListItem *
pk_task_list_find_existing_job (PkTaskList *tlist, guint job)
{
	guint i;
	guint length;
	PkTaskListItem *item;

	length = tlist->priv->task_list->len;
	/* mark previous tasks as non-valid */
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		if (item->job == job) {
			return item;
		}
	}
	return NULL;
}

/**
 * pk_task_list_job_status_changed_cb:
 **/
static void
pk_task_list_job_status_changed_cb (PkTaskMonitor *tmonitor, PkStatusEnum status, PkTaskList *tlist)
{
	guint job;
	PkTaskListItem *item;

	g_return_if_fail (tlist != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (tlist));

	job = pk_task_monitor_get_job (tmonitor);
	pk_debug ("job %i is now %i", job, status);

	/* get correct item */
	item = pk_task_list_find_existing_job (tlist, job);
	item->status = status;

	pk_debug ("emit task-list-changed");
	g_signal_emit (tlist , signals [PK_TASK_LIST_CHANGED], 0);
}

/**
 * pk_task_list_job_finished_cb:
 **/
static void
pk_task_list_job_finished_cb (PkTaskMonitor *tmonitor, PkExitEnum exit, guint runtime, PkTaskList *tlist)
{
	guint job;
	PkTaskListItem *item;

	g_return_if_fail (tlist != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (tlist));

	job = pk_task_monitor_get_job (tmonitor);
	pk_debug ("job %i exited with %i", job, exit);

	/* get correct item */
	item = pk_task_list_find_existing_job (tlist, job);

	pk_debug ("emit task-list-finished %i, %s, %i", item->role, item->package_id, runtime);
	g_signal_emit (tlist , signals [PK_TASK_LIST_FINISHED], 0, item->role, item->package_id, runtime);
}

/**
 * pk_task_list_error_code_cb:
 **/
static void
pk_task_list_error_code_cb (PkTaskMonitor *tmonitor, PkErrorCodeEnum error_code, const gchar *details, PkTaskList *tlist)
{
	g_return_if_fail (tlist != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (tlist));

	pk_debug ("emit error-code %i, %s", error_code, details);
	g_signal_emit (tlist , signals [PK_TASK_LIST_ERROR_CODE], 0, error_code, details);
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
	guint job;
	guint length;
	GArray *array;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	/* get the latest job list */
	array = pk_job_list_get_latest (tlist->priv->job_list);

	/* mark previous tasks as non-valid */
	length = tlist->priv->task_list->len;
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		item->valid = FALSE;
	}

	/* copy tasks */
	length = array->len;
	for (i=0; i<length; i++) {
		job = g_array_index (array, guint, i);

		item = pk_task_list_find_existing_job (tlist, job);
		if (item == NULL) {
			pk_debug ("new job, have to create %i", job);
			item = g_new0 (PkTaskListItem, 1);
			item->job = job;
			item->monitor = pk_task_monitor_new ();
			g_signal_connect (item->monitor, "job-status-changed",
					  G_CALLBACK (pk_task_list_job_status_changed_cb), tlist);
			g_signal_connect (item->monitor, "finished",
					  G_CALLBACK (pk_task_list_job_finished_cb), tlist);
			g_signal_connect (item->monitor, "error-code",
					  G_CALLBACK (pk_task_list_error_code_cb), tlist);
			pk_task_monitor_set_job (item->monitor, job);
			pk_task_monitor_get_role (item->monitor, &item->role, &item->package_id);
			pk_task_monitor_get_status (item->monitor, &item->status);

			/* add to watched array */
			g_ptr_array_add (tlist->priv->task_list, item);
		}

		/* mark as present so we don't garbage collect it */
		item->valid = TRUE;
	}

	/* find and remove non-valid watches */
	for (i=0; i<tlist->priv->task_list->len; i++) {
		item = g_ptr_array_index (tlist->priv->task_list, i);
		if (item->valid == FALSE) {
			pk_debug ("remove %i", item->job);
			g_object_unref (item->monitor);
			g_free (item->package_id);
			g_ptr_array_remove (tlist->priv->task_list, item);
			g_free (item);
		}
	}

	return TRUE;
}

/**
 * pk_task_list_get_latest:
 *
 * DO NOT FREE THIS.
 **/
GPtrArray *
pk_task_list_get_latest (PkTaskList *tlist)
{
	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);
	return tlist->priv->task_list;
}

/**
 * pk_task_list_job_list_changed_cb:
 **/
static void
pk_task_list_job_list_changed_cb (PkJobList *jlist, PkTaskList *tlist)
{
	/* for now, just refresh all the jobs. a little inefficient me thinks */
	pk_task_list_refresh (tlist);
	pk_debug ("emit task-list-changed");
	g_signal_emit (tlist , signals [PK_TASK_LIST_CHANGED], 0);
}

/**
 * pk_task_list_class_init:
 **/
static void
pk_task_list_class_init (PkTaskListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_list_finalize;

	signals [PK_TASK_LIST_CHANGED] =
		g_signal_new ("task-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_TASK_LIST_FINISHED] =
		g_signal_new ("task-list-finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_UINT,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_TASK_LIST_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);

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
	tlist->priv->job_list = pk_job_list_new ();
	g_signal_connect (tlist->priv->job_list, "job-list-changed",
			  G_CALLBACK (pk_task_list_job_list_changed_cb), tlist);

	/* we maintain a local copy */
	tlist->priv->task_list = g_ptr_array_new ();

	/* force a refresh so we have valid data*/
	pk_task_list_get_latest (tlist);
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
		pk_debug ("remove %i", item->job);
		g_object_unref (item->monitor);
		g_free (item->package_id);
		g_ptr_array_remove (tlist->priv->task_list, item);
		g_free (item);
	}

	g_ptr_array_free (tlist->priv->task_list, TRUE);
	g_object_unref (tlist->priv->job_list);

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

