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

//	command = g_strdup_printf ("/usr/bin/apt-cache search %s", search);

#include <string.h>

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-task-common.h"
#include "pk-spawn.h"

static void     pk_task_class_init	(PkTaskClass *klass);
static void     pk_task_init		(PkTask      *task);
static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

struct PkTaskPrivate
{
	gboolean		 whatever_you_want;
	guint			 progress_percentage;
};

static guint signals [PK_TASK_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTask, pk_task, G_TYPE_OBJECT)

/**
 * pk_task_get_updates:
 **/
gboolean
pk_task_get_updates (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_refresh_cache:
 **/
gboolean
pk_task_refresh_cache (PkTask *task, gboolean force)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_update_system:
 **/
gboolean
pk_task_update_system (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

#if 0

/**
 * pk_task_parse_data:
 **/
static void
pk_task_parse_data (PkTask *task, const gchar *line)
{
	char **sections;
	gboolean okay;

	/* check if output line */
	if (strstr (line, " - ") == NULL)
		return;		
	sections = g_strsplit (line, " - ", 0);
	okay = pk_task_filter_package_name (NULL, sections[0]);
	if (okay == TRUE) {
		pk_debug ("package='%s' shortdesc='%s'", sections[0], sections[1]);
		pk_task_package (task, TRUE, sections[0], sections[1]);
	}
	g_strfreev (sections);
}

/**
 * pk_spawn_finished_cb:
 **/
static void
pk_spawn_finished_cb (PkSpawn *spawn, gint exitcode, PkTask *task)
{
	pk_debug ("unref'ing spawn %p, exit code %i", spawn, exitcode);
	g_object_unref (spawn);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
}

/**
 * pk_spawn_stdout_cb:
 **/
static void
pk_spawn_stdout_cb (PkSpawn *spawn, const gchar *line, PkTask *task)
{
	pk_debug ("stdout from %p = '%s'", spawn, line);
	pk_task_parse_data (task, line);
}

/**
 * pk_spawn_stderr_cb:
 **/
static void
pk_spawn_stderr_cb (PkSpawn *spawn, const gchar *line, PkTask *task)
{
	pk_debug ("stderr from %p = '%s'", spawn, line);
}

#endif

/**
 * pk_task_find_packages:
 **/
gboolean
pk_task_find_packages (PkTask *task, const gchar *search, guint depth, gboolean installed, gboolean available)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_get_deps:
 **/
gboolean
pk_task_get_deps (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_remove_package:
 **/
gboolean
pk_task_remove_package (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_remove_package_with_deps:
 **/
gboolean
pk_task_remove_package_with_deps (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_install_package:
 **/
gboolean
pk_task_install_package (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
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
	if (task->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}

	/* not implimented yet */
	return FALSE;
}

/**
 * pk_task_class_init:
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_finalize;
	pk_task_setup_signals (object_class, signals);
	g_type_class_add_private (klass, sizeof (PkTaskPrivate));
}

/**
 * pk_task_init:
 **/
static void
pk_task_init (PkTask *task)
{
	task->priv = PK_TASK_GET_PRIVATE (task);
	task->signals = signals;
	pk_task_clear (task);
}

/**
 * pk_task_finalize:
 **/
static void
pk_task_finalize (GObject *object)
{
	PkTask *task;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK (object));
	task = PK_TASK (object);
	g_return_if_fail (task->priv != NULL);
	g_free (task->package);
	G_OBJECT_CLASS (pk_task_parent_class)->finalize (object);
}

/**
 * pk_task_new:
 **/
PkTask *
pk_task_new (void)
{
	PkTask *task;
	task = g_object_new (PK_TYPE_TASK, NULL);
	return PK_TASK (task);
}

