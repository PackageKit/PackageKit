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
#include <pk-task-common.h>
#include <pk-enum.h>

#include "pk-backend-internal.h"
#include "pk-engine.h"
#include "pk-job-list.h"
#include "pk-marshal.h"

static void     pk_engine_class_init	(PkEngineClass *klass);
static void     pk_engine_init		(PkEngine      *engine);
static void     pk_engine_finalize	(GObject       *object);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))

struct PkEnginePrivate
{
	GTimer			*timer;
	PolKitContext		*pk_context;
	DBusConnection		*connection;
	gchar			*backend;
	PkJobList		*job_list;
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
	PK_ENGINE_UPDATE_DETAIL,
	PK_ENGINE_DESCRIPTION,
	PK_ENGINE_ALLOW_INTERRUPT,
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
			ENUM_ENTRY (PK_ENGINE_ERROR_NOT_SUPPORTED, "NotSupported"),
			ENUM_ENTRY (PK_ENGINE_ERROR_NO_SUCH_JOB, "NoSuchJob"),
			ENUM_ENTRY (PK_ENGINE_ERROR_JOB_EXISTS_WITH_ROLE, "JobExistsWithRole"),
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
 * pk_engine_job_list_changed:
 **/
static gboolean
pk_engine_job_list_changed (PkEngine *engine)
{
	GArray *job_list;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	job_list = pk_job_list_get_array (engine->priv->job_list);

	pk_debug ("emitting job-list-changed");
	g_signal_emit (engine, signals [PK_ENGINE_JOB_LIST_CHANGED], 0, job_list);
	pk_engine_reset_timer (engine);
	return TRUE;
}

/**
 * pk_engine_job_status_changed_cb:
 **/
static void
pk_engine_job_status_changed_cb (PkTask *task, PkStatusEnum status, PkEngine *engine)
{
	PkJobListItem *item;
	const gchar *status_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
		status_text = pk_status_enum_to_text (status);

	pk_debug ("emitting job-status-changed job:%i, '%s'", item->job, status_text);
	g_signal_emit (engine, signals [PK_ENGINE_JOB_STATUS_CHANGED], 0, item->job, status_text);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_percentage_changed_cb:
 **/
static void
pk_engine_percentage_changed_cb (PkTask *task, guint percentage, PkEngine *engine)
{
	PkJobListItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting percentage-changed job:%i %i", item->job, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_PERCENTAGE_CHANGED], 0, item->job, percentage);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_sub_percentage_changed_cb:
 **/
static void
pk_engine_sub_percentage_changed_cb (PkTask *task, guint percentage, PkEngine *engine)
{
	PkJobListItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting sub-percentage-changed job:%i %i", item->job, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_SUB_PERCENTAGE_CHANGED], 0, item->job, percentage);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_no_percentage_updates_cb:
 **/
static void
pk_engine_no_percentage_updates_cb (PkTask *task, PkEngine *engine)
{
	PkJobListItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting no-percentage-updates job:%i", item->job);
	g_signal_emit (engine, signals [PK_ENGINE_NO_PERCENTAGE_UPDATES], 0, item->job);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_package_cb:
 **/
static void
pk_engine_package_cb (PkTask *task, guint value, const gchar *package_id, const gchar *summary, PkEngine *engine)
{
	PkJobListItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	pk_debug ("emitting package job:%i value=%i %s, %s", item->job, value, package_id, summary);
	g_signal_emit (engine, signals [PK_ENGINE_PACKAGE], 0, item->job, value, package_id, summary);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_error_code_cb:
 **/
static void
pk_engine_error_code_cb (PkTask *task, PkErrorCodeEnum code, const gchar *details, PkEngine *engine)
{
	PkJobListItem *item;
	const gchar *code_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	code_text = pk_error_enum_to_text (code);
	pk_debug ("emitting error-code job:%i %s, '%s'", item->job, code_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_ERROR_CODE], 0, item->job, code_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_require_restart_cb:
 **/
static void
pk_engine_require_restart_cb (PkTask *task, PkRestartEnum restart, const gchar *details, PkEngine *engine)
{
	PkJobListItem *item;
	const gchar *restart_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	restart_text = pk_restart_enum_to_text (restart);
	pk_debug ("emitting error-code job:%i %s, '%s'", item->job, restart_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_REQUIRE_RESTART], 0, item->job, restart_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_description_cb:
 **/
static void
pk_engine_description_cb (PkTask *task, const gchar *package_id, PkGroupEnum group,
			  const gchar *detail, const gchar *url, PkEngine *engine)
{
	PkJobListItem *item;
	const gchar *group_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	group_text = pk_group_enum_to_text (group);

	pk_debug ("emitting description job:%i, %s, %s, %s, %s", item->job, package_id, group_text, detail, url);
	g_signal_emit (engine, signals [PK_ENGINE_DESCRIPTION], 0, item->job, package_id, group_text, detail, url);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkTask *task, PkExitEnum exit, PkEngine *engine)
{
	PkJobListItem *item;
	const gchar *exit_text;
	gdouble time;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	exit_text = pk_exit_enum_to_text (exit);

	/* find the length of time we have been running */
	time = pk_backend_get_runtime (task);

	pk_debug ("task was running for %f seconds", time);

	pk_debug ("emitting finished job: %i, '%s', %i", item->job, exit_text, (guint) time);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, item->job, exit_text, (guint) time);

	/* remove from array and unref */
	pk_job_list_remove (engine->priv->job_list, task);

	g_object_unref (task);
	pk_debug ("removed task %p", task);
	pk_engine_job_list_changed (engine);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_allow_interrupt_cb:
 **/
static void
pk_engine_allow_interrupt_cb (PkTask *task, gboolean allow_kill, PkEngine *engine)
{
	PkJobListItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}

	pk_debug ("emitting allow-interrpt job:%i, %i", item->job, allow_kill);
	g_signal_emit (engine, signals [PK_ENGINE_ALLOW_INTERRUPT], 0, item->job, allow_kill);
}

/**
 * pk_engine_new_task:
 **/
static PkTask *
pk_engine_new_task (PkEngine *engine)
{
	PkTask *task;
	gboolean ret;

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

	pk_job_list_add (engine->priv->job_list, task);

	/* we don't add to the array or do the job-list-changed yet
	 * as this job might fail */
	return task;
}

/**
 * pk_engine_add_task:
 **/
static gboolean
pk_engine_add_task (PkEngine *engine, PkTask *task)
{
	/* commit, so it appears in the JobList */
	pk_job_list_commit (engine->priv->job_list, task);

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
	PkTask *task;
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_refresh_cache (task, force);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

	return TRUE;
}

/**
 * pk_engine_get_updates:
 **/
gboolean
pk_engine_get_updates (PkEngine *engine, guint *job, GError **error)
{
	gboolean ret;
	PkTask *task;
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_get_updates (task);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

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
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

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
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

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
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

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
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

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
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

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
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

	return TRUE;
}

/**
 * pk_engine_get_update_detail:
 **/
gboolean
pk_engine_get_update_detail (PkEngine *engine, const gchar *package_id,
		       guint *job, GError **error)
{
	gboolean ret;
	PkTask *task;
	PkJobListItem *item;

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
	ret = pk_backend_get_update_detail (task, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

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
	PkTask *task;
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_get_description (task, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		g_object_unref (task);
		return FALSE;
	}
	pk_engine_add_task (engine, task);
	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return FALSE;
	}
	*job = item->job;

	return TRUE;
}

/**
 * pk_engine_update_system:
 **/
void
pk_engine_update_system (PkEngine *engine,
			 DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	GError *error;
	PkTask *task;
	PkJobListItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.update", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* are we already performing an update? */
	if (pk_job_list_role_present (engine->priv->job_list, PK_ROLE_ENUM_SYSTEM_UPDATE) == TRUE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_JOB_EXISTS_WITH_ROLE,
				     "Already performing system update");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	ret = pk_backend_update_system (task);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, item->job);
}

/**
 * pk_engine_remove_package:
 **/
void
pk_engine_remove_package (PkEngine *engine, const gchar *package_id, gboolean allow_deps,
			  DBusGMethodInvocation *context, GError **dead_error)
{
	PkJobListItem *item;
	gboolean ret;
	PkTask *task;
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
				     "Operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, item->job);
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
	PkJobListItem *item;
	PkTask *task;
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
				     "Operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, item->job);
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
	PkJobListItem *item;
	PkTask *task;
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
				     "Operation not yet supported by backend");
		g_object_unref (task);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_task (engine, task);

	item = pk_job_list_get_item_from_task (engine->priv->job_list, task);
	if (item == NULL) {
		pk_warning ("could not find task");
		return;
	}
	dbus_g_method_return (context, item->job);
}

/**
 * pk_engine_get_job_list:
 **/
gboolean
pk_engine_get_job_list (PkEngine *engine, GArray **job_list, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	*job_list = pk_job_list_get_array (engine->priv->job_list);

	return TRUE;
}

/**
 * pk_engine_get_job_status:
 **/
gboolean
pk_engine_get_job_status (PkEngine *engine, guint job,
			  const gchar **status, GError **error)
{
	PkStatusEnum status_enum;
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_job_list_get_item_from_job (engine->priv->job_list, job);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_job_status (item->task, &status_enum);
	*status = g_strdup (pk_status_enum_to_text (status_enum));

	return TRUE;
}

/**
 * pk_engine_get_job_role:
 **/
gboolean
pk_engine_get_job_role (PkEngine *engine, guint job,
			const gchar **role, const gchar **package_id, GError **error)
{
	PkJobListItem *item;
	PkRoleEnum role_enum;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_job_list_get_item_from_job (engine->priv->job_list, job);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_job_role (item->task, &role_enum, package_id);
	*role = g_strdup (pk_role_enum_to_text (role_enum));

	return TRUE;
}

/**
 * pk_engine_get_percentage:
 **/
gboolean
pk_engine_get_percentage (PkEngine *engine, guint job, guint *percentage, GError **error)
{
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_job_list_get_item_from_job (engine->priv->job_list, job);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_percentage (item->task, percentage);
	return TRUE;
}

/**
 * pk_engine_get_sub_percentage:
 **/
gboolean
pk_engine_get_sub_percentage (PkEngine *engine, guint job, guint *percentage, GError **error)
{
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_job_list_get_item_from_job (engine->priv->job_list, job);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_sub_percentage (item->task, percentage);
	return TRUE;
}

/**
 * pk_engine_get_package:
 **/
gboolean
pk_engine_get_package (PkEngine *engine, guint job, gchar **package, GError **error)
{
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_job_list_get_item_from_job (engine->priv->job_list, job);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}
	pk_backend_get_package (item->task, package);
	return TRUE;
}

/**
 * pk_engine_cancel_job_try:
 **/
gboolean
pk_engine_cancel_job_try (PkEngine *engine, guint job, GError **error)
{
	gboolean ret;
	PkJobListItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_job_list_get_item_from_job (engine->priv->job_list, job);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_JOB,
			     "No job:%i", job);
		return FALSE;
	}

	ret = pk_backend_cancel_job_try (item->task);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
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
	PkTask *task;
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_backend_load (task, engine->priv->backend);
	elist = pk_backend_get_actions (task);
	*actions = pk_enum_list_to_string (elist);
	g_object_unref (task);
	g_object_unref (elist);

	return TRUE;
}

/**
 * pk_engine_get_groups:
 * @engine: This class instance
 **/
gboolean
pk_engine_get_groups (PkEngine *engine, gchar **groups, GError **error)
{
	PkTask *task;
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_backend_load (task, engine->priv->backend);
	elist = pk_backend_get_groups (task);
	*groups = pk_enum_list_to_string (elist);
	g_object_unref (task);
	g_object_unref (elist);

	return TRUE;
}

/**
 * pk_engine_get_filters:
 * @engine: This class instance
 **/
gboolean
pk_engine_get_filters (PkEngine *engine, gchar **filters, GError **error)
{
	PkTask *task;
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new task and start it */
	task = pk_engine_new_task (engine);
	pk_backend_load (task, engine->priv->backend);
	elist = pk_backend_get_filters (task);
	*filters = pk_enum_list_to_string (elist);
	g_object_unref (task);
	g_object_unref (elist);

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
	if (pk_job_list_get_size (engine->priv->job_list) != 0) {
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
	signals [PK_ENGINE_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 7, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
	engine->priv->job_list = pk_job_list_new ();
	engine->priv->timer = g_timer_new ();
	engine->priv->backend = NULL;

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

	/* compulsory gobjects */
	g_timer_destroy (engine->priv->timer);
	g_free (engine->priv->backend);
	polkit_context_unref (engine->priv->pk_context);
	g_object_unref (engine->priv->job_list);

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
