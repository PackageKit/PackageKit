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

#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-task-common.h"

/**
 * pk_task_setup_signals:
 **/
gboolean
pk_task_setup_signals (GObjectClass *object_class, guint *signals)
{
	g_return_val_if_fail (object_class != NULL, FALSE);

	signals [PK_TASK_JOB_STATUS_CHANGED] =
		g_signal_new ("job-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, job_status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_PERCENTAGE_COMPLETE_CHANGED] =
		g_signal_new ("percentage-complete-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, percentage_complete_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_PACKAGES] =
		g_signal_new ("packages",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, packages),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals [PK_TASK_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, finished),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	return TRUE;
}

/**
 * pk_task_change_percentage_complete:
 **/
gboolean
pk_task_change_percentage_complete (PkTask *task, guint percentage)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	pk_debug ("emit percentage-complete-changed %i", percentage);
	g_signal_emit (task, task->signals [PK_TASK_PERCENTAGE_COMPLETE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * pk_task_change_job_status:
 **/
gboolean
pk_task_change_job_status (PkTask *task, PkTaskStatus status)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	task->status = status;
	pk_debug ("emiting job-status-changed %i", status);
	g_signal_emit (task, task->signals [PK_TASK_JOB_STATUS_CHANGED], 0, status);
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
	if (task->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	*status = task->status;
	return TRUE;
}

/**
 * pk_task_finished:
 **/
gboolean
pk_task_finished (PkTask *task, PkTaskExit exit)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	pk_debug ("emit finished %i", exit);
	g_signal_emit (task, task->signals [PK_TASK_FINISHED], 0, exit);
	return TRUE;
}

/**
 * pk_task_assign:
 **/
gboolean
pk_task_assign (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we already have an action */
	if (task->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	task->assigned = TRUE;
	return TRUE;
}

/**
 * pk_task_get_job:
 **/
guint
pk_task_get_job (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	return task->job;
}

/**
 * pk_task_set_job:
 **/
gboolean
pk_task_set_job (PkTask *task, guint job)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	pk_debug ("set job %p=%i", task, job);
	task->job = job;
	return TRUE;
}

/**
 * pk_task_clear:
 **/
gboolean
pk_task_clear (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	task->assigned = FALSE;
	task->status = PK_TASK_STATUS_INVALID;
	task->exit = PK_TASK_EXIT_UNKNOWN;
	task->job = 1;

	return TRUE;
}

/**
 * pk_task_status_to_text:
 **/
const gchar *
pk_task_status_to_text (PkTaskStatus status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_TASK_STATUS_SETUP:
		text = "setup";
		break;
	case PK_TASK_STATUS_QUERY:
		text = "query";
		break;
	case PK_TASK_STATUS_REMOVE:
		text = "remove";
		break;
	case PK_TASK_STATUS_DOWNLOAD:
		text = "download";
		break;
	case PK_TASK_STATUS_INSTALL:
		text = "install";
		break;
	case PK_TASK_STATUS_UPDATE:
		text = "update";
		break;
	case PK_TASK_STATUS_EXIT:
		text = "exit";
		break;
	default:
		text = "invalid";
	}
	return text;
}
