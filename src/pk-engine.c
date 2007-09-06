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
#include <polkit/polkit.h>
#include <polkit-dbus/polkit-dbus.h>
#include <pk-package-id.h>

#include <pk-debug.h>
#include "pk-task-utils.h"
#include "pk-backend-internal.h"
#include "pk-engine.h"
#include "pk-marshal.h"

static void     pk_engine_class_init	(PkEngineClass *klass);
static void     pk_engine_init		(PkEngine      *engine);
static void     pk_engine_finalize	(GObject       *object);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))
#define PK_ENGINE_JOB_LAST_COUNT_FILE		LOCALSTATEDIR "/run/PackageKit/job_count.dat"

struct PkEnginePrivate
{
	GPtrArray		*array;
	GTimer			*timer;
	guint			 job_count;
	PolKitContext		*pk_context;
	DBusConnection		*connection;
	gchar			*backend;
};

enum {
	PK_ENGINE_JOB_LIST_CHANGED,
	PK_ENGINE_JOB_STATUS_CHANGED,
	PK_ENGINE_PERCENTAGE_CHANGED,
	PK_ENGINE_SUB_PERCENTAGE_CHANGED,
	PK_ENGINE_NO_PERCENTAGE_UPDATES,
	PK_ENGINE_PACKAGE,
	PK_ENGINE_ERROR_CODE,
	PK_ENGINE_REQUIRE_RESTART,
	PK_ENGINE_FINISHED,
	PK_ENGINE_DESCRIPTION,
	PK_ENGINE_ALLOW_INTERRUPT,
	PK_ENGINE_LAST_SIGNAL
};

typedef struct {
	guint		 job;
	PkBackend		*task;
} PkEngineMap;

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
			ENUM_ENTRY (PK_ENGINE_ERROR_NOT_SUPPORTED, "NotSupported"),
			ENUM_ENTRY (PK_ENGINE_ERROR_NO_SUCH_JOB, "NoSuchJob"),
			ENUM_ENTRY (PK_ENGINE_ERROR_REFUSED_BY_POLICY, "RefusedByPolicy"),
			ENUM_ENTRY (PK_ENGINE_ERROR_PACKAGE_ID_INVALID, "PackageIdInvalid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_SEARCH_INVALID, "SearchInvalid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_FILTER_INVALID, "FilterInvalid"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("PkEngineError", values);
	}
	return etype;
}

/**
 * pk_engine_use_backend:
 **/
gboolean
pk_engine_use_backend (PkEngine *engine, const gchar *backend)
{
	pk_debug ("trying backend %s", backend);
	engine->priv->backend = g_strdup (backend);
	return TRUE;
}

/**
 * pk_engine_reset_timer:
 **/
static void
pk_engine_reset_timer (PkEngine *engine)
{
	pk_debug ("reset timer");
	g_timer_reset (engine->priv->timer);
}

/**
 * pk_engine_create_job_list:
 **/
static GArray *
pk_engine_create_job_list (PkEngine *engine)
{
	guint i;
	guint length;
	GArray *job_list;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create new list */
	job_list = g_array_new (FALSE, FALSE, sizeof (guint));

	/* find all the jobs in progress */
	length = engine->priv->array->len;
	for (i=0; i<length; i++) {
		map = (PkEngineMap *) g_ptr_array_index (engine->priv->array, i);
		job_list = g_array_append_val (job_list, map->job);
	}
	return job_list;
}

/**
 * pk_get_map_from_job:
 **/
static PkEngineMap *
pk_get_map_from_job (PkEngine *engine, guint job)
{
	guint i;
	guint length;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, NULL);
	g_return_val_if_fail (PK_IS_ENGINE (engine), NULL);

	/* find the task with the job ID */
	length = engine->priv->array->len;
	for (i=0; i<length; i++) {
		map = (PkEngineMap *) g_ptr_array_index (engine->priv->array, i);
		if (map->job == job) {
			return map;
		}
	}
	return NULL;
}

/**
 * pk_get_map_from_task:
 **/
static PkEngineMap *
pk_get_map_from_task (PkEngine *engine, PkBackend *task)
{
	guint i;
	guint length;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, NULL);
	g_return_val_if_fail (PK_IS_ENGINE (engine), NULL);

	/* find the task with the job ID */
	length = engine->priv->array->len;
	for (i=0; i<length; i++) {
		map = (PkEngineMap *) g_ptr_array_index (engine->priv->array, i);
		if (map->task == task) {
			return map;
		}
	}
	return NULL;
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
	pk_engine_reset_timer (engine);
	return TRUE;
}

/**
 * pk_engine_job_status_changed_cb:
 **/
static void
pk_engine_job_status_changed_cb (PkBackend *task, PkTaskStatus status, PkEngine *engine)
{
	PkEngineMap *map;
	const gchar *status_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
		status_text = pk_task_status_to_text (status);

	pk_debug ("emitting job-status-changed job:%i, '%s'", map->job, status_text);
	g_signal_emit (engine, signals [PK_ENGINE_JOB_STATUS_CHANGED], 0, map->job, status_text);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_percentage_changed_cb:
 **/
static void
pk_engine_percentage_changed_cb (PkBackend *task, guint percentage, PkEngine *engine)
{
	PkEngineMap *map;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting percentage-changed job:%i %i", map->job, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_PERCENTAGE_CHANGED], 0, map->job, percentage);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_sub_percentage_changed_cb:
 **/
static void
pk_engine_sub_percentage_changed_cb (PkBackend *task, guint percentage, PkEngine *engine)
{
	PkEngineMap *map;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting sub-percentage-changed job:%i %i", map->job, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_SUB_PERCENTAGE_CHANGED], 0, map->job, percentage);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_no_percentage_updates_cb:
 **/
static void
pk_engine_no_percentage_updates_cb (PkBackend *task, PkEngine *engine)
{
	PkEngineMap *map;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting no-percentage-updates job:%i", map->job);
	g_signal_emit (engine, signals [PK_ENGINE_NO_PERCENTAGE_UPDATES], 0, map->job);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_package_cb:
 **/
static void
pk_engine_package_cb (PkBackend *task, guint value, const gchar *package_id, const gchar *summary, PkEngine *engine)
{
	PkEngineMap *map;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting package job:%i value=%i %s, %s", map->job, value, package_id, summary);
	g_signal_emit (engine, signals [PK_ENGINE_PACKAGE], 0, map->job, value, package_id, summary);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_error_code_cb:
 **/
static void
pk_engine_error_code_cb (PkBackend *task, PkTaskErrorCode code, const gchar *details, PkEngine *engine)
{
	PkEngineMap *map;
	const gchar *code_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	code_text = pk_task_error_code_to_text (code);
	pk_debug ("emitting error-code job:%i %s, '%s'", map->job, code_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_ERROR_CODE], 0, map->job, code_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_require_restart_cb:
 **/
static void
pk_engine_require_restart_cb (PkBackend *task, PkTaskRestart restart, const gchar *details, PkEngine *engine)
{
	PkEngineMap *map;
	const gchar *restart_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	restart_text = pk_task_restart_to_text (restart);
	pk_debug ("emitting error-code job:%i %s, '%s'", map->job, restart_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_REQUIRE_RESTART], 0, map->job, restart_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_description_cb:
 **/
static void
pk_engine_description_cb (PkBackend *task, const gchar *package_id, PkTaskGroup group,
			  const gchar *detail, const gchar *url, PkEngine *engine)
{
	PkEngineMap *map;
	const gchar *group_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	group_text = pk_task_group_to_text (group);

	pk_debug ("emitting description job:%i, %s, %s, %s, %s", map->job, package_id, group_text, detail, url);
	g_signal_emit (engine, signals [PK_ENGINE_DESCRIPTION], 0, map->job, package_id, group_text, detail, url);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkBackend *task, PkTaskExit exit, PkEngine *engine)
{
	PkEngineMap *map;
	const gchar *exit_text;
	gdouble time;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	exit_text = pk_task_exit_to_text (exit);

	/* find the length of time we have been running */
	time = pk_backend_get_runtime (task);

	pk_debug ("task was running for %f seconds", time);

	pk_debug ("emitting finished job: %i, '%s', %i", map->job, exit_text, (guint) time);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, map->job, exit_text, (guint) time);

	/* remove from array and unref */
	g_ptr_array_remove (engine->priv->array, map);
	g_free (map);

	g_object_unref (task);
	pk_debug ("removed task %p", task);
	pk_engine_job_list_changed (engine);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_allow_interrupt_cb:
 **/
static void
pk_engine_allow_interrupt_cb (PkBackend *task, gboolean allow_kill, PkEngine *engine)
{
	PkEngineMap *map;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}

	pk_debug ("emitting allow-interrpt job:%i, %i", map->job, allow_kill);
	g_signal_emit (engine, signals [PK_ENGINE_ALLOW_INTERRUPT], 0, map->job, allow_kill);
}

/**
 * pk_engine_load_job_count:
 **/
static gboolean
pk_engine_load_job_count (PkEngine *engine)
{
	gboolean ret;
	gchar *contents;
	ret = g_file_get_contents (PK_ENGINE_JOB_LAST_COUNT_FILE, &contents, NULL, NULL);
	if (ret == FALSE) {
		pk_warning ("failed to get last job");
		return FALSE;
	}
	engine->priv->job_count = atoi (contents);
	pk_debug ("job=%i", engine->priv->job_count);
	g_free (contents);
	return TRUE;
}

/**
 * pk_engine_save_job_count:
 **/
static gboolean
pk_engine_save_job_count (PkEngine *engine)
{
	gboolean ret;
	gchar *contents;

	pk_debug ("saving %i", engine->priv->job_count);
	contents = g_strdup_printf ("%i", engine->priv->job_count);
	ret = g_file_set_contents (PK_ENGINE_JOB_LAST_COUNT_FILE, contents, -1, NULL);
	g_free (contents);
	if (ret == FALSE) {
		pk_warning ("failed to set last job");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_new_task:
 **/
static PkBackend *
pk_engine_new_task (PkEngine *engine)
{
	PkBackend *task;
	gboolean ret;

	/* increment the job number - we never repeat an id */
	engine->priv->job_count++;

	/* allocate a new task */
	task = pk_backend_new ();
	ret = pk_backend_load (task, engine->priv->backend);
	if (ret == FALSE) {
		pk_error ("Cannot use backend '%s'", engine->priv->backend);
	}
	pk_debug ("adding task %p", task);

	/* connect up signals */
	g_signal_connect (task, "job-status-changed",
			  G_CALLBACK (pk_engine_job_status_changed_cb), engine);
	g_signal_connect (task, "percentage-changed",
			  G_CALLBACK (pk_engine_percentage_changed_cb), engine);
	g_signal_connect (task, "sub-percentage-changed",
			  G_CALLBACK (pk_engine_sub_percentage_changed_cb), engine);
	g_signal_connect (task, "no-percentage-updates",
			  G_CALLBACK (pk_engine_no_percentage_updates_cb), engine);
	g_signal_connect (task, "package",
			  G_CALLBACK (pk_engine_package_cb), engine);
	g_signal_connect (task, "error-code",
			  G_CALLBACK (pk_engine_error_code_cb), engine);
	g_signal_connect (task, "require-restart",
			  G_CALLBACK (pk_engine_require_restart_cb), engine);
	g_signal_connect (task, "finished",
			  G_CALLBACK (pk_engine_finished_cb), engine);
	g_signal_connect (task, "description",
			  G_CALLBACK (pk_engine_description_cb), engine);
	g_signal_connect (task, "allow-interrupt",
			  G_CALLBACK (pk_engine_allow_interrupt_cb), engine);

	/* initialise some stuff */
	pk_engine_reset_timer (engine);

	/* we don't add to the array or do the job-list-changed yet
	 * as this job might fail */
	return task;
}

/**
 * pk_engine_add_task:
 **/
static gboolean
pk_engine_add_task (PkEngine *engine, PkBackend *task)
{
	PkEngineMap *map;

	/* add to the array */
	map = g_new0 (PkEngineMap, 1);
	map->task = task;
	map->job = engine->priv->job_count;
	g_ptr_array_add (engine->priv->array, map);

	/* in an ideal world we don't need this, but do it in case the daemon is ctrl-c;d */
	pk_engine_save_job_count (engine);

	/* emit a signal */
	pk_engine_job_list_changed (engine);

	return TRUE;
}

/**
 * pk_engine_can_do_action:
 **/
static PolKitResult
pk_engine_can_do_action (PkEngine *engine, const gchar *dbus_name, const gchar *action)
{
	PolKitResult pk_result;
	PolKitAction *pk_action;
	PolKitCaller *pk_caller;
	DBusError dbus_error;

	/* set action */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, action);

	/* set caller */
	pk_debug ("using caller %s", dbus_name);
	dbus_error_init (&dbus_error);
	pk_caller = polkit_caller_new_from_dbus_name (engine->priv->connection, dbus_name, &dbus_error);
	if (pk_caller == NULL) {
		if (dbus_error_is_set (&dbus_error)) {
			pk_error ("error: polkit_caller_new_from_dbus_name(): %s: %s\n",
				  dbus_error.name, dbus_error.message);
		}
	}

	pk_result = polkit_context_can_caller_do_action (engine->priv->pk_context, pk_action, pk_caller);
	pk_debug ("PolicyKit result = '%s'", polkit_result_to_string_representation (pk_result));

	polkit_action_unref (pk_action);
	polkit_caller_unref (pk_caller);

	return pk_result;
}

/**
 * pk_engine_action_is_allowed:
 **/
static gboolean
pk_engine_action_is_allowed (PkEngine *engine, DBusGMethodInvocation *context, const gchar *action, GError **error)
{
	PolKitResult pk_result;
	const gchar *dbus_name;

#ifdef IGNORE_POLKIT
	return TRUE;
#endif

	/* get the dbus sender */
	dbus_name = dbus_g_method_get_sender (context);
	pk_result = pk_engine_can_do_action (engine, dbus_name, action);
	if (pk_result != POLKIT_RESULT_YES) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_REFUSED_BY_POLICY,
				     "%s %s", action, polkit_result_to_string_representation (pk_result));
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_refresh_cache:
 **/
gboolean
pk_engine_refresh_cache (PkEngine *engine, gboolean force, guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_refresh_cache (task, force);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_get_updates:
 **/
gboolean
pk_engine_get_updates (PkEngine *engine, guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_get_updates (task);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_search_check:
 **/
gboolean
pk_engine_search_check (const gchar *search, GError **error)
{
	if (search == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
			     "Search is null. This isn't supposed to happen...");
		return FALSE;
	}
	if (strlen (search) == 0) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
			     "Search string zero length");
		return FALSE;
	}
	if (strlen (search) < 2) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
			     "The search string length is too small");
		return FALSE;
	}
	if (strstr (search, "*") != NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
			     "Invalid search containing '*'");
		return FALSE;
	}
	if (strstr (search, "?") != NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
			     "Invalid search containing '?'");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_filter_check:
 **/
gboolean
pk_engine_filter_check (const gchar *filter, GError **error)
{
	gboolean ret;

	/* check for invalid filter */
	ret = pk_task_filter_check (filter);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_FILTER_INVALID,
			     "Filter '%s' is invalid", filter);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_search_name:
 **/
gboolean
pk_engine_search_name (PkEngine *engine, const gchar *filter, const gchar *search,
		       guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check the search term */
	ret = pk_engine_search_check (search, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_search_name (task, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_search_details:
 **/
gboolean
pk_engine_search_details (PkEngine *engine, const gchar *filter, const gchar *search,
			  guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check the search term */
	ret = pk_engine_search_check (search, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_search_details (task, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_search_group:
 **/
gboolean
pk_engine_search_group (PkEngine *engine, const gchar *filter, const gchar *search,
			guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check the search term */
	ret = pk_engine_search_check (search, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_search_group (task, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_search_file:
 **/
gboolean
pk_engine_search_file (PkEngine *engine, const gchar *filter, const gchar *search,
		       guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check the search term */
	ret = pk_engine_search_check (search, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, error);
	if (ret == FALSE) {
		return FALSE;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_search_file (task, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_get_depends:
 **/
gboolean
pk_engine_get_depends (PkEngine *engine, const gchar *package_id,
		    guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				      "The package id '%s' is not valid", package_id);
		return FALSE;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_get_depends (task, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_get_requires:
 **/
gboolean
pk_engine_get_requires (PkEngine *engine, const gchar *package_id,
		        guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				      "The package id '%s' is not valid", package_id);
		return FALSE;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_get_requires (task, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_get_description:
 **/
gboolean
pk_engine_get_description (PkEngine *engine, const gchar *package_id,
			   guint *job, GError **error)
{
	gboolean ret;
	PkBackend *task;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_get_description (task, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = map->job;

	return TRUE;
}

/**
 * pk_engine_update_system:
 **/
void
pk_engine_update_system (PkEngine *engine,
			 DBusGMethodInvocation *context, GError **dead_error)
{
	guint i;
	guint length;
	PkTaskRole role;
	gboolean ret;
	GError *error;
	PkBackend *task;
	PkEngineMap *map;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.update", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for existing job doing an update */
	length = engine->priv->array->len;
	for (i=0; i<length; i++) {
		map = (PkEngineMap *) g_ptr_array_index (engine->priv->array, i);
		ret = pk_backend_get_job_role (map->task, &role, NULL);
		if (ret == TRUE && role == PK_TASK_ROLE_SYSTEM_UPDATE) {
			error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_DENIED,
					     "operation not yet supported by backend");
			dbus_g_method_return_error (context, error);
			return;
		}
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_update_system (task);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, map->job);
}

/**
 * pk_engine_remove_package:
 **/
void
pk_engine_remove_package (PkEngine *engine, const gchar *package_id, gboolean allow_deps,
			  DBusGMethodInvocation *context, GError **dead_error)
{
	PkEngineMap *map;
	gboolean ret;
	PkBackend *task;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.remove", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_remove_package (task, package_id, allow_deps);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, map->job);
}

/**
 * pk_engine_install_package:
 *
 * This is async, so we have to treat it a bit carefully
 **/
void
pk_engine_install_package (PkEngine *engine, const gchar *package_id,
			   DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	PkEngineMap *map;
	PkBackend *task;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.install", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_install_package (task, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, map->job);
}

/**
 * pk_engine_update_package:
 *
 * This is async, so we have to treat it a bit carefully
 **/
void
pk_engine_update_package (PkEngine *engine, const gchar *package_id,
			   DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	PkEngineMap *map;
	PkBackend *task;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.update", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_update_package (task, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	map = pk_get_map_from_task (engine, task);
	if (map == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, map->job);
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
			  const gchar **status, GError **error)
{
	PkTaskStatus status_enum;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	map = pk_get_map_from_job (engine, job);
	if (map == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_job_status (map->task, &status_enum);
	*status = g_strdup (pk_task_status_to_text (status_enum));

	return TRUE;
}

/**
 * pk_engine_get_job_role:
 **/
gboolean
pk_engine_get_job_role (PkEngine *engine, guint job,
			const gchar **role, const gchar **package_id, GError **error)
{
	PkEngineMap *map;
	PkTaskRole role_enum;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	map = pk_get_map_from_job (engine, job);
	if (map == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_job_role (map->task, &role_enum, package_id);
	*role = g_strdup (pk_task_role_to_text (role_enum));

	return TRUE;
}

/**
 * pk_engine_cancel_job_try:
 **/
gboolean
pk_engine_cancel_job_try (PkEngine *engine, guint job, GError **error)
{
	gboolean ret;
	PkEngineMap *map;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	map = pk_get_map_from_job (engine, job);
	if (map == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}

	ret = pk_backend_cancel_job_try (map->task);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "operation not yet supported by backend");
		return FALSE;
	}

	return TRUE;
}


/**
 * pk_engine_get_actions:
 * @engine: This class instance
 **/
gboolean
pk_engine_get_actions (PkEngine *engine, gchar **actions, GError **error)
{
	PkBackend *task;
	PkActionList *alist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	alist = pk_backend_get_actions (task);
	g_object_unref (task);
	*actions = pk_util_action_to_string (alist);

	return TRUE;
}

/**
 * pk_engine_get_seconds_idle:
 * @engine: This class instance
 **/
guint
pk_engine_get_seconds_idle (PkEngine *engine)
{
	guint idle;

	g_return_val_if_fail (engine != NULL, 0);
	g_return_val_if_fail (PK_IS_ENGINE (engine), 0);

	/* check for jobs running - a job that takes a *long* time might not
	 * give sufficient percentage updates to not be marked as idle */
	if (engine->priv->array->len != 0) {
		pk_debug ("engine idle zero as jobs in progress");
		return 0;
	}

	idle = (guint) g_timer_elapsed (engine->priv->timer, NULL);
	pk_debug ("engine idle=%i", idle);
	return idle;
}

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
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_ENGINE_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_ENGINE_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_ENGINE_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_ENGINE_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 5, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_UINT,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_ENGINE_ALLOW_INTERRUPT] =
		g_signal_new ("allow-interrupt",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_BOOL,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkEnginePrivate));
}

/**
 * pk_engine_init:
 * @engine: This class instance
 **/
static void
pk_engine_init (PkEngine *engine)
{
	DBusError dbus_error;
	polkit_bool_t retval;
	PolKitError *pk_error;

	engine->priv = PK_ENGINE_GET_PRIVATE (engine);
	engine->priv->array = g_ptr_array_new ();
	engine->priv->timer = g_timer_new ();
	engine->priv->backend = NULL;

	engine->priv->job_count = pk_engine_load_job_count (engine);

	/* get a connection to the bus */
	dbus_error_init (&dbus_error);
	engine->priv->connection = dbus_bus_get (DBUS_BUS_SYSTEM, &dbus_error);
	if (engine->priv->connection == NULL) {
		pk_error ("failed to get system connection %s: %s\n", dbus_error.name, dbus_error.message);
	}

	/* get PolicyKit context */
	engine->priv->pk_context = polkit_context_new ();
	pk_error = NULL;
	retval = polkit_context_init (engine->priv->pk_context, &pk_error);
	if (retval == FALSE) {
		pk_error ("Could not init PolicyKit context: %s", polkit_error_get_error_message (pk_error));
		polkit_error_free (pk_error);
	}
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

	/* save last job id so we don't ever repeat */
	pk_engine_save_job_count (engine);

	/* compulsory gobjects */
	g_ptr_array_free (engine->priv->array, TRUE);
	g_timer_destroy (engine->priv->timer);
	g_free (engine->priv->backend);
	polkit_context_unref (engine->priv->pk_context);

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
