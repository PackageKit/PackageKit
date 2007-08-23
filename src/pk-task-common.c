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
#include <stdlib.h>

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-spawn.h"
#include "pk-task-common.h"
#include "pk-marshal.h"

/**
 * pk_task_filter_package_name:
 **/
gboolean
pk_task_filter_package_name (PkTask *task, const gchar *package)
{
	if (strstr (package, "-debuginfo") != NULL) {
		return FALSE;
	}
	if (strstr (package, "-dbg") != NULL) {
		return FALSE;
	}
	if (strstr (package, "-devel") != NULL) {
		return FALSE;
	}
	if (strstr (package, "-dev") != NULL) {
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
	signals [PK_TASK_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
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
 * pk_task_parse_common_output:
 *
 * If you are editing this function creating a new backend,
 * then you are probably doing something wrong.
 **/
static gboolean
pk_task_parse_common_output (PkTask *task, const gchar *line)
{
	gchar **sections;
	guint size;
	guint value = 0;
	gchar *command;
	gboolean ret = TRUE;
	gboolean okay;

	/* check if output line */
	if (line == NULL || strstr (line, "\t") == NULL)
		return FALSE;

	/* split by tab */
	sections = g_strsplit (line, "\t", 0);
	command = sections[0];

	/* get size */
	for (size=0; sections[size]; size++);

	if (strcmp (command, "package") == 0) {
		if (size != 4) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		okay = pk_task_filter_package_name (task, sections[2]);
		if (okay == TRUE) {
			if (pk_task_check_package_id (sections[2]) == TRUE) {
				value = atoi(sections[1]);
				pk_debug ("value=%i, package='%s' shortdesc='%s'", value, sections[2], sections[3]);
				pk_task_package (task, value, sections[2], sections[3]);
			} else {
				pk_warning ("invalid package_id");
			}
		}
	} else if (strcmp (command, "description") == 0) {
		if (size != 5) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_task_description (task, sections[1], sections[2], sections[3], sections[4]);
	} else {
		pk_warning ("invalid command '%s'", command);
	}		
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_task_parse_common_error:
 *
 * If you are editing this function creating a new backend,
 * then you are probably doing something wrong.
 **/
static gboolean
pk_task_parse_common_error (PkTask *task, const gchar *line)
{
	gchar **sections;
	guint size;
	guint percentage;
	gchar *command;
	PkTaskErrorCode error_enum;
	PkTaskStatus status_enum;
	PkTaskRestart restart_enum;
	gboolean ret = TRUE;

	/* check if output line */
	if (line == NULL || strstr (line, "\t") == NULL)
		return FALSE;
  
	/* split by tab */
	sections = g_strsplit (line, "\t", 0);
	command = sections[0];

	/* get size */
	for (size=0; sections[size]; size++);

	if (strcmp (command, "percentage") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		percentage = atoi(sections[1]);
		pk_task_change_percentage (task, percentage);
	} else if (strcmp (command, "subpercentage") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		percentage = atoi(sections[1]);
		pk_warning ("Ignoring sub-percentage %i", percentage);
	} else if (strcmp (command, "error") == 0) {
		if (size != 3) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		error_enum = pk_task_error_code_from_text (sections[1]);
		pk_task_error_code (task, error_enum, sections[2]);
	} else if (strcmp (command, "requirerestart") == 0) {
		if (size != 3) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		restart_enum = pk_task_restart_from_text (sections[1]);
		pk_task_require_restart (task, restart_enum, sections[2]);
	} else if (strcmp (command, "data") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_task_set_data (task, sections[1]);
	} else if (strcmp (command, "status") == 0) {
		if (size != 2) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		status_enum = pk_task_status_from_text (sections[1]);
		pk_task_change_job_status (task, status_enum);
	} else if (strcmp (command, "no-percentage-updates") == 0) {
		if (size != 1) {
			g_error ("invalid command '%s'", command);
			ret = FALSE;
			goto out;
		}
		pk_task_no_percentage_updates (task);
	} else {
		pk_warning ("invalid command '%s'", command);
	}		
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_task_spawn_finished_cb:
 **/
static void
pk_task_spawn_finished_cb (PkSpawn *spawn, gint exitcode, PkTask *task)
{
	pk_debug ("unref'ing spawn %p, exit code %i", spawn, exitcode);
	g_object_unref (spawn);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
}

/**
 * pk_task_spawn_stdout_cb:
 **/
static void
pk_task_spawn_stdout_cb (PkSpawn *spawn, const gchar *line, PkTask *task)
{
	pk_debug ("stdout from %p = '%s'", spawn, line);
	pk_task_parse_common_output (task, line);
}

/**
 * pk_task_spawn_stderr_cb:
 **/
static void
pk_task_spawn_stderr_cb (PkSpawn *spawn, const gchar *line, PkTask *task)
{
	pk_debug ("stderr from %p = '%s'", spawn, line);
	pk_task_parse_common_error (task, line);
}

/**
 * pk_task_spawn_helper:
 **/
gboolean
pk_task_spawn_helper (PkTask *task, const gchar *script)
{
	PkSpawn *spawn;
	gboolean ret;
	gchar *filename;

	filename = g_build_filename (DATADIR, "PackageKit", "helpers", script, NULL);
	spawn = pk_spawn_new ();
	g_signal_connect (spawn, "finished",
			  G_CALLBACK (pk_task_spawn_finished_cb), task);
	g_signal_connect (spawn, "stdout",
			  G_CALLBACK (pk_task_spawn_stdout_cb), task);
	g_signal_connect (spawn, "stderr",
			  G_CALLBACK (pk_task_spawn_stderr_cb), task);
	ret = pk_spawn_command (spawn, filename);
	if (ret == FALSE) {
		g_warning ("spawn failed: '%s'", filename);
		g_object_unref (spawn);
		pk_task_error_code (task, PK_TASK_ERROR_CODE_INTERNAL_ERROR, "spawn failed");
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	}
	g_free (filename);
	return ret;
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
 * pk_task_require_restart:
 **/
gboolean
pk_task_require_restart (PkTask *task, PkTaskRestart restart, const gchar *details)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	pk_debug ("emit require-restart %i, %s", restart, details);
	g_signal_emit (task, task->signals [PK_TASK_REQUIRE_RESTART], 0, restart, details);

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
	task->status = PK_TASK_STATUS_UNKNOWN;
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

/**
 * pk_task_set_data:
 *
 * Need to g_free
 **/
gboolean
pk_task_set_data (PkTask *task, const gchar *data)
{
	g_free (task->package);
	task->package = g_strdup (data);
	return TRUE;
}

