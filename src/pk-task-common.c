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
#include <string.h>

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-task-common.h"
#include "pk-marshal.h"

/**
 * pk_task_filter_package_name:
 **/
gboolean
pk_task_filter_package_name (PkTask *task, gchar *package)
{
	if (strstr (package, "-debuginfo") != NULL) {
		return FALSE;
	}
	if (strstr (package, "-devel") != NULL) {
		return FALSE;
	}
	/* todo, check if package depends on any gtk/qt toolkit */
	return TRUE;
}

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
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-complete-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TASK_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	return TRUE;
}

/**
 * pk_task_change_percentage:
 **/
gboolean
pk_task_change_percentage (PkTask *task, guint percentage)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);
	pk_debug ("emit percentage-complete-changed %i", percentage);
	g_signal_emit (task, task->signals [PK_TASK_PERCENTAGE_CHANGED], 0, percentage);
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
 * pk_task_package:
 **/
gboolean
pk_task_package (PkTask *task, guint value, const gchar *package, const gchar *summary)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	pk_debug ("emit package %i, %s, %s", value, package, summary);
	g_signal_emit (task, task->signals [PK_TASK_PACKAGE], 0, value, package, summary);

	return TRUE;
}

/**
 * pk_task_description:
 **/
gboolean
pk_task_description (PkTask *task, const gchar *package, const gchar *version,
		     const gchar *description, const gchar *url)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	pk_debug ("emit description %s, %s, %s, %s", package, version, description, url);
	g_signal_emit (task, task->signals [PK_TASK_DESCRIPTION], 0, package, version, description, url);

	return TRUE;
}

/**
 * pk_task_error_code:
 **/
gboolean
pk_task_error_code (PkTask *task, PkTaskErrorCode code, const gchar *details)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	pk_debug ("emit error-code %i, %s", code, details);
	g_signal_emit (task, task->signals [PK_TASK_ERROR_CODE], 0, code, details);

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
 * pk_task_finished_idle:
 **/
static gboolean
pk_task_finished_idle (gpointer data)
{
	PkTask *task = (PkTask *) data;
	pk_debug ("emit finished %i", task->exit);
	g_signal_emit (task, task->signals [PK_TASK_FINISHED], 0, task->exit);
	return FALSE;
}

/**
 * pk_task_finished:
 **/
gboolean
pk_task_finished (PkTask *task, PkTaskExit exit)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* we have to run this idle as the command may finish before the job
	 * has been sent to the client. I love async... */
	pk_debug ("adding finished %p to idle loop", task);
	task->exit = exit;
	pk_task_change_job_status (task, PK_TASK_STATUS_EXIT);
	g_idle_add (pk_task_finished_idle, task);
	return TRUE;
}

/**
 * pk_task_no_percentage_updates:
 **/
gboolean
pk_task_no_percentage_updates (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	pk_debug ("emit no-percentage-updates");
	g_signal_emit (task, task->signals [PK_TASK_NO_PERCENTAGE_UPDATES], 0);
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
	task->package = NULL;

	return TRUE;
}

/**
 * pk_task_get_data:
 *
 * Need to g_free
 **/
gchar *
pk_task_get_data (PkTask *task)
{
	return g_strdup (task->package);
}

