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
static void     pk_task_finalize	(GObject	 *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

struct PkTaskPrivate
{
	gboolean		 low_power;
};

enum {
	JOB_LIST_CHANGED,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTask, pk_task, G_TYPE_OBJECT)

/**
 * pk_task_get_updates:
 **/
gboolean
pk_task_get_updates (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

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

	return TRUE;
}

/**
 * pk_task_get_job_status:
 **/
gboolean
pk_task_get_job_status (PkTask *task, const gchar **status, const gchar **package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

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

	return TRUE;
}

//		g_signal_emit (task, signals [JOB_LIST_CHANGED], 0, FALSE);

/**
 * pk_task_class_init:
 * @klass: The PkTaskClass
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_finalize;

	signals [JOB_LIST_CHANGED] =
		g_signal_new ("job-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTaskClass, job_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkTaskPrivate));
}

/**
 * pk_task_init:
 * @task: This class instance
 **/
static void
pk_task_init (PkTask *task)
{
	DBusGConnection *connection;
	GError *error = NULL;

	task->priv = PK_TASK_GET_PRIVATE (task);
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
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

