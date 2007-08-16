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

/* DUMMY TARGET
 *
 * Upgrade takes 10 seconds and gives out 10% percentage points
 * Get updates is instant and sends back a single package "kernel"
 * Install of anything takes 20 seconds and spends 10 secs downloading and 10 secs installing with 1% percentage points
 * Removal is instant
 * Removal of dbus fails for deps
 * Removal of evince passes with no deps
 * Search takes two seconds, returns 5 packages with no percentage updates
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

	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

gboolean
pk_task_update_system_timeout (gpointer data)
{
	PkTask *task = (PkTask *) data;
	if (task->priv->progress_percentage == 100) {
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
		return FALSE;
	}
	task->priv->progress_percentage += 10;
	pk_task_change_percentage (task, task->priv->progress_percentage);
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

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_change_job_status (task, PK_TASK_STATUS_UPDATE);
	task->priv->progress_percentage = 0;
	g_timeout_add (1000, pk_task_update_system_timeout, task);

	return TRUE;
}

/**
 * pk_task_find_packages_timeout:
 **/
gboolean
pk_task_find_packages_timeout (gpointer data)
{
	PkTask *task = (PkTask *) data;
	pk_task_package (task, 1, "evince", "Document viewer");
	pk_task_package (task, 1, "evince-gnome", "Document viewer GNOME UI");
	pk_task_package (task, 0, "evince-tools", "Document viewer tools");
	pk_task_package (task, 0, "evince-debuginfo", "Document viewer debuginfo");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return FALSE;
}

/**
 * pk_task_parse_data:
 **/
static void
pk_task_parse_data (PkTask *task, const gchar *line)
{
	char **sections;
	gboolean okay;
	guint value = FALSE;

	/* check if output line */
	if (strstr (line, "\t") == NULL)
		return;		
	sections = g_strsplit (line, "\t", 0);
	if (strcmp (sections[0], "yes") == 0) {
		value = TRUE;
	}
	okay = pk_task_filter_package_name (NULL, sections[1]);
	if (okay == TRUE) {
		pk_debug ("value=%i, package='%s' shortdesc='%s'", value, sections[1], sections[2]);
		pk_task_package (task, value, sections[1], sections[2]);
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

/**
 * pk_task_find_packages:
 **/
gboolean
pk_task_find_packages (PkTask *task, const gchar *search)
{
	PkSpawn *spawn;
	gchar *command;
	gboolean ret;

	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	task->package = strdup (search);
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_no_percentage_updates (task);

//	command = g_strdup_printf ("/usr/bin/apt-cache search %s", search);
	command = g_strdup_printf ("/home/hughsie/Code/PackageKit/helpers/yum-search-simple.py %s", search);
	spawn = pk_spawn_new ();
	g_signal_connect (spawn, "finished",
			  G_CALLBACK (pk_spawn_finished_cb), task);
	g_signal_connect (spawn, "stdout",
			  G_CALLBACK (pk_spawn_stdout_cb), task);
	g_signal_connect (spawn, "stderr",
			  G_CALLBACK (pk_spawn_stderr_cb), task);
	ret = pk_spawn_command (spawn, command);
	if (ret == FALSE) {
		g_warning ("spawn failed: '%s'", command);
		g_object_unref (spawn);
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	}

if (0)	g_timeout_add (2000, pk_task_find_packages_timeout, task);
	g_free (command);
	return TRUE;
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

	task->package = strdup (package);
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	return TRUE;
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

	pk_task_change_job_status (task, PK_TASK_STATUS_REMOVE);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	return TRUE;
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

	pk_task_change_job_status (task, PK_TASK_STATUS_REMOVE);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	return TRUE;
}

static gboolean
pk_task_install_timeout (gpointer data)
{
	PkTask *task = (PkTask *) data;
	if (task->priv->progress_percentage == 100) {
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
		return FALSE;
	}
	if (task->priv->progress_percentage == 50) {
		pk_task_change_job_status (task, PK_TASK_STATUS_INSTALL);
	}
	task->priv->progress_percentage += 10;
	pk_task_change_percentage (task, task->priv->progress_percentage);
	return TRUE;
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

	pk_task_change_job_status (task, PK_TASK_STATUS_DOWNLOAD);
	task->priv->progress_percentage = 0;
	g_timeout_add (1000, pk_task_install_timeout, task);
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
	if (task->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	/* try to cancel action */
	if (task->status != PK_TASK_STATUS_QUERY) {
		pk_warning ("cannot cancel as not query");
		return FALSE;
	}

	/* close process */
	//pk_backend_cancel ();

	return TRUE;
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

