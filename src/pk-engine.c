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
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
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
#include "pk-engine.h"
#include "pk-marshal.h"

static void     pk_engine_class_init	(PkEngineClass *klass);
static void     pk_engine_init		(PkEngine      *engine);
static void     pk_engine_finalize	(GObject       *object);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))

struct PkEnginePrivate
{
	GPtrArray		*array;
};

enum {
	PK_ENGINE_JOB_LIST_CHANGED,
	PK_ENGINE_JOB_STATUS_CHANGED,
	PK_ENGINE_PERCENTAGE_CHANGED,
	PK_ENGINE_PACKAGE,
	PK_ENGINE_FINISHED,
	PK_ENGINE_DESCRIPTION,
	PK_ENGINE_LAST_SIGNAL
};

static guint	     signals [PK_ENGINE_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkEngine, pk_engine, G_TYPE_OBJECT)

/**
 * pk_engine_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
pk_engine_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("pk_engine_error");
	}
	return quark;
}

/**
 * pk_engine_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_engine_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_ENGINE_ERROR_DENIED, "PermissionDenied"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("PkEngineError", values);
	}
	return etype;
}

/**
 * pk_engine_create_job_list:
 **/
static GArray *
pk_engine_create_job_list (PkEngine *engine)
{
	guint i;
	guint length;
	guint job;
	PkTask *task;
	GArray *job_list;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create new list */
	job_list = g_array_new (FALSE, FALSE, sizeof (guint));

	/* find all the jobs in progress */
	length = engine->priv->array->len;
	for (i=0; i<length; i++) {
		task = (PkTask *) g_ptr_array_index (engine->priv->array, i);
		job = pk_task_get_job (task);
		job_list = g_array_append_val (job_list, job);
	}
	return job_list;
}

/**
 * pk_engine_job_list_changed:
 **/
static gboolean
pk_engine_job_list_changed (PkEngine *engine)
{
	GArray *job_list;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	job_list = pk_engine_create_job_list (engine);

	pk_debug ("emitting job-list-changed");
	g_signal_emit (engine, signals [PK_ENGINE_JOB_LIST_CHANGED], 0, job_list);
	return TRUE;
}

/**
 * pk_engine_job_status_changed_cb:
 **/
static void
pk_engine_job_status_changed_cb (PkTask *task, PkTaskStatus status, PkEngine *engine)
{
	guint job;
	const gchar *status_text;
	const gchar *package;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	job = pk_task_get_job (task);
	status_text = pk_task_status_to_text (status);
	package = "foo";

	pk_debug ("emitting job-status-changed job:%i, '%s', '%s'", job, status_text, package);
	g_signal_emit (engine, signals [PK_ENGINE_JOB_STATUS_CHANGED], 0, job, status_text, package);
}

/**
 * pk_engine_percentage_changed_cb:
 **/
static void
pk_engine_percentage_changed_cb (PkTask *task, guint percentage, PkEngine *engine)
{
	guint job;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	job = pk_task_get_job (task);
	pk_debug ("emitting percentage-changed job:%i %i", job, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_PERCENTAGE_CHANGED], 0, job, percentage);
}

/**
 * pk_engine_package_cb:
 **/
static void
pk_engine_package_cb (PkTask *task, const gchar *package, PkEngine *engine)
{
	guint job;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	job = pk_task_get_job (task);
	pk_debug ("emitting package job:%i %s", job, package);
	g_signal_emit (engine, signals [PK_ENGINE_PACKAGE], 0, job, package);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkTask *task, PkTaskExit exit, PkEngine *engine)
{
	guint job;
	const gchar *exit_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	job = pk_task_get_job (task);
	exit_text = pk_task_exit_to_text (exit);

	pk_debug ("emitting finished job:%i, '%s'", job, exit_text);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, job, exit_text);

	/* remove from array and unref */
	g_ptr_array_remove (engine->priv->array, task);
	g_object_unref (task);
	pk_debug ("removed task %p", task);
}

/**
 * pk_engine_new_task:
 **/
static PkTask *
pk_engine_new_task (PkEngine *engine)
{
	PkTask *task;
	static guint job = 0;

	/* increment the job number - we never repeat an id */
	job++;

	/* allocate a new task */
	task = pk_task_new ();
	pk_debug ("adding task %p", task);

	/* connect up signals */
	g_signal_connect (task, "job-status-changed",
			  G_CALLBACK (pk_engine_job_status_changed_cb), engine);
	g_signal_connect (task, "percentage-complete-changed",
			  G_CALLBACK (pk_engine_percentage_changed_cb), engine);
	g_signal_connect (task, "package",
			  G_CALLBACK (pk_engine_package_cb), engine);
	g_signal_connect (task, "finished",
			  G_CALLBACK (pk_engine_finished_cb), engine);

	/* set the job ID */
	pk_task_set_job (task, job);

	/* add to the array */
	g_ptr_array_add (engine->priv->array, task);

	/* emit a signal */
	pk_engine_job_list_changed (engine);

	return task;
}

/**
 * pk_engine_get_updates:
 **/
gboolean
pk_engine_get_updates (PkEngine *engine, guint *job, GError **error)
{
	PkTask *task;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_get_updates (task);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_update_system:
 **/
gboolean
pk_engine_update_system (PkEngine *engine, guint *job, GError **error)
{
	guint i;
	guint length;
	PkTaskStatus status;
	PkTask *task;
	gboolean ret;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check for existing job doing an update */
	length = engine->priv->array->len;
	for (i=0; i<length; i++) {
		task = (PkTask *) g_ptr_array_index (engine->priv->array, i);
		ret = pk_task_get_job_status (task, &status);
		if (ret == TRUE && status == PK_TASK_STATUS_UPDATE) {
			g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_DENIED,
				     "system update already in progress");
			return FALSE;
		}
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_update_system (task);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_find_packages:
 **/
gboolean
pk_engine_find_packages (PkEngine *engine, const gchar *search,
			 guint *job, GError **error)
{
	PkTask *task;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_find_packages (task, search);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_get_deps:
 **/
gboolean
pk_engine_get_deps (PkEngine *engine, const gchar *package,
		    guint *job, GError **error)
{
	PkTask *task;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_get_deps (task, package);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_remove_package:
 **/
gboolean
pk_engine_remove_package (PkEngine *engine, const gchar *package,
			  guint *job, GError **error)
{
	PkTask *task;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_remove_package (task, package);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_remove_package_with_deps:
 **/
gboolean
pk_engine_remove_package_with_deps (PkEngine *engine, const gchar *package,
					     guint *job, GError **error)
{
	PkTask *task;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_remove_package_with_deps (task, package);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_install_package:
 **/
gboolean
pk_engine_install_package (PkEngine *engine, const gchar *package,
			   guint *job, GError **error)
{
	PkTask *task;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_task_install_package (task, package);
	*job = pk_task_get_job (task);

	return TRUE;
}

/**
 * pk_engine_get_job_list:
 **/
gboolean
pk_engine_get_job_list (PkEngine *engine, GArray **job_list, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	*job_list = pk_engine_create_job_list (engine);

	return TRUE;
}

/**
 * pk_engine_get_job_status:
 **/
gboolean
pk_engine_get_job_status (PkEngine *engine, guint job,
			  const gchar **status, const gchar **package, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	return TRUE;
}

/**
 * pk_engine_cancel_job_try:
 **/
gboolean
pk_engine_cancel_job_try (PkEngine *engine, guint job, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	return TRUE;
}

//		g_signal_emit (engine, signals [JOB_LIST_CHANGED], 0, FALSE);

/**
 * pk_engine_class_init:
 * @klass: The PkEngineClass
 **/
static void
pk_engine_class_init (PkEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_engine_finalize;

	/* set up signal that emits 'au' */
	signals [PK_ENGINE_JOB_LIST_CHANGED] =
		g_signal_new ("job-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, dbus_g_type_get_collection ("GArray", G_TYPE_UINT));
	signals [PK_ENGINE_JOB_STATUS_CHANGED] =
		g_signal_new ("job-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_ENGINE_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_ENGINE_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkEnginePrivate));
}

/**
 * pk_engine_init:
 * @engine: This class instance
 **/
static void
pk_engine_init (PkEngine *engine)
{
	engine->priv = PK_ENGINE_GET_PRIVATE (engine);
	engine->priv->array = g_ptr_array_new ();
}

/**
 * pk_engine_finalize:
 * @object: The object to finalize
 **/
static void
pk_engine_finalize (GObject *object)
{
	PkEngine *engine;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_ENGINE (object));

	engine = PK_ENGINE (object);

	g_return_if_fail (engine->priv != NULL);

	/* compulsory gobjects */
	g_ptr_array_free (engine->priv->array, TRUE);

	G_OBJECT_CLASS (pk_engine_parent_class)->finalize (object);
}

/**
 * pk_engine_new:
 *
 * Return value: a new PkEngine object.
 **/
PkEngine *
pk_engine_new (void)
{
	PkEngine *engine;
	engine = g_object_new (PK_TYPE_ENGINE, NULL);
	return PK_ENGINE (engine);
}
