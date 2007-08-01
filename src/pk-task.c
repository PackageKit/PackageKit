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
#include <dbus/dbus-glib-lowlevel.h>

#include "pk-task.h"

static void     pk_task_class_init	(PkTaskClass *klass);
static void     pk_task_init		(PkTask      *task);
static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

struct PkTaskPrivate
{
	gboolean		 assigned;
	guint			 job;
	PkTaskStatus		 status;
	PkTaskExit		 exit;
};

enum {
	JOB_STATUS_CHANGED,
	PERCENTAGE_COMPLETE_CHANGED,
	PACKAGES,
	FINISHED,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTask, pk_task, G_TYPE_OBJECT)

/**
 * pk_task_change_percentage_complete:
 **/
static gboolean
pk_task_change_percentage_complete (PkTask *task, guint percentage)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	g_debug ("emit percentage-complete-changed %i", percentage);
	g_signal_emit (task, signals [PERCENTAGE_COMPLETE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * pk_task_change_percentage_complete:
 **/
static gboolean
pk_task_change_job_status (PkTask *task, PkTaskStatus status)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	task->priv->status = status;
	g_debug ("emit job-status-changed %i", status);
	g_signal_emit (task, signals [JOB_STATUS_CHANGED], 0, status);
	return TRUE;
}

/**
 * pk_task_finished:
 **/
static gboolean
pk_task_finished (PkTask *task, PkTaskExit exit)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	g_debug ("emit finished %i", exit);
	g_signal_emit (task, signals [FINISHED], 0, exit);
	return TRUE;
}

/**
 * pk_task_get_job:
 **/
guint
pk_task_get_job (PkTask *task)
{
	return task->priv->job;
}

/**
 * pk_task_get_updates:
 **/
gboolean
pk_task_get_updates (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	//dlopend_get_updates
if (0)	pk_task_change_percentage_complete (task, 0);
if (0)	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	return TRUE;
}

/**
 * pk_task_update_system:
 **/
gboolean
pk_task_update_system (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_UPDATE);

	//pk_module_update()

	return TRUE;
}

/**
 * pk_task_find_packages:
 **/
gboolean
pk_task_find_packages (PkTask *task, const gchar *search)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);

	return TRUE;
}

/**
 * pk_task_get_dependencies:
 **/
gboolean
pk_task_get_dependencies (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);

	return TRUE;
}

/**
 * pk_task_remove_packages:
 **/
gboolean
pk_task_remove_packages (PkTask *task, const gchar **packages)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_REMOVE);

	return TRUE;
}

/**
 * pk_task_remove_packages_with_dependencies:
 **/
gboolean
pk_task_remove_packages_with_dependencies (PkTask *task, const gchar **packages)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_REMOVE);

	return TRUE;
}

/**
 * pk_task_install_packages:
 **/
gboolean
pk_task_install_packages (PkTask *task, const gchar **packages)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	task->priv->assigned = TRUE;
	pk_task_change_job_status (task, PK_TASK_STATUS_INSTALL);

	//pk_backend_install (packages, FALSE);

	return TRUE;
}

/**
 * pk_task_get_job_status:
 **/
gboolean
pk_task_get_job_status (PkTask *task, PkTaskStatus *status)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we have an action */
	if (task->priv->assigned == FALSE) {
		g_warning ("Not assigned");
		return FALSE;
	}
	*status = task->priv->status;
	return TRUE;
}

/**
 * pk_task_cancel_job_try:
 **/
gboolean
pk_task_cancel_job_try (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we have an action */
	if (task->priv->assigned == FALSE) {
		g_warning ("Not assigned");
		return FALSE;
	}
	/* try to cancel action */
	if (task->priv->status != PK_TASK_STATUS_QUERY) {
		g_warning ("cannot cancel as not query");
		return FALSE;
	}

	/* close process */
	//pk_backend_cancel ();

	return TRUE;
}

/**
 * pk_task_class_init:
 * @klass: The PkTaskClass
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_finalize;

	signals [JOB_STATUS_CHANGED] =
		g_signal_new ("job-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, job_status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PERCENTAGE_COMPLETE_CHANGED] =
		g_signal_new ("percentage-complete-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, percentage_complete_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PACKAGES] =
		g_signal_new ("packages",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, packages),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, finished),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkTaskPrivate));
}

/**
 * pk_task_init:
 * @task: This class instance
 **/
static void
pk_task_init (PkTask *task)
{
	task->priv = PK_TASK_GET_PRIVATE (task);
	task->priv->assigned = FALSE;
	task->priv->status = PK_TASK_STATUS_INVALID;
	task->priv->exit = PK_TASK_EXIT_UNKNOWN;

	/* allocate a unique job number */
	task->priv->job = 1;
}

/**
 * pk_task_finalize:
 * @object: The object to finalize
 *
 * Finalise the task, by unref'ing all the depending modules.
 **/
static void
pk_task_finalize (GObject *object)
{
	PkTask *task;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK (object));

	task = PK_TASK (object);

	g_return_if_fail (task->priv != NULL);

	G_OBJECT_CLASS (pk_task_parent_class)->finalize (object);
}

/**
 * pk_task_new:
 *
 * Return value: a new PkTask object.
 **/
PkTask *
pk_task_new (void)
{
	PkTask *task;
	task = g_object_new (PK_TYPE_TASK, NULL);
	return PK_TASK (task);
}

