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
#include "pk-transaction-db.h"
#include "pk-transaction-list.h"
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
	PkTransactionList	*transaction_list;
	PkTransactionDb		*transaction_db;
	PkTransactionItem	*sync_item;
};

enum {
	PK_ENGINE_TRANSACTION_LIST_CHANGED,
	PK_ENGINE_TRANSACTION_STATUS_CHANGED,
	PK_ENGINE_PERCENTAGE_CHANGED,
	PK_ENGINE_SUB_PERCENTAGE_CHANGED,
	PK_ENGINE_NO_PERCENTAGE_UPDATES,
	PK_ENGINE_PACKAGE,
	PK_ENGINE_TRANSACTION,
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
			ENUM_ENTRY (PK_ENGINE_ERROR_NO_SUCH_TRANSACTION, "NoSuchTransaction"),
			ENUM_ENTRY (PK_ENGINE_ERROR_NO_SUCH_FILE, "NoSuchFile"),
			ENUM_ENTRY (PK_ENGINE_ERROR_TRANSACTION_EXISTS_WITH_ROLE, "TransactionExistsWithRole"),
			ENUM_ENTRY (PK_ENGINE_ERROR_REFUSED_BY_POLICY, "RefusedByPolicy"),
			ENUM_ENTRY (PK_ENGINE_ERROR_PACKAGE_ID_INVALID, "PackageIdInvalid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_SEARCH_INVALID, "SearchInvalid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_FILTER_INVALID, "FilterInvalid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_INVALID_STATE, "InvalidState"),
			ENUM_ENTRY (PK_ENGINE_ERROR_INITIALIZE_FAILED, "InitializeFailed"),
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
 * pk_engine_transaction_list_changed_cb:
 **/
static void
pk_engine_transaction_list_changed_cb (PkTransactionList *tlist, PkEngine *engine)
{
	gchar **transaction_list;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);

	pk_debug ("emitting transaction-list-changed");
	g_signal_emit (engine, signals [PK_ENGINE_TRANSACTION_LIST_CHANGED], 0, transaction_list);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_transaction_status_changed_cb:
 **/
static void
pk_engine_transaction_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkEngine *engine)
{
	PkTransactionItem *item;
	const gchar *status_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	status_text = pk_status_enum_to_text (status);

	pk_debug ("emitting transaction-status-changed tid:%s, '%s'", item->tid, status_text);
	g_signal_emit (engine, signals [PK_ENGINE_TRANSACTION_STATUS_CHANGED], 0, item->tid, status_text);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_percentage_changed_cb:
 **/
static void
pk_engine_percentage_changed_cb (PkBackend *backend, guint percentage, PkEngine *engine)
{
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	pk_debug ("emitting percentage-changed tid:%s %i", item->tid, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_PERCENTAGE_CHANGED], 0, item->tid, percentage);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_sub_percentage_changed_cb:
 **/
static void
pk_engine_sub_percentage_changed_cb (PkBackend *backend, guint percentage, PkEngine *engine)
{
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	pk_debug ("emitting sub-percentage-changed tid:%s %i", item->tid, percentage);
	g_signal_emit (engine, signals [PK_ENGINE_SUB_PERCENTAGE_CHANGED], 0, item->tid, percentage);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_no_percentage_updates_cb:
 **/
static void
pk_engine_no_percentage_updates_cb (PkBackend *backend, PkEngine *engine)
{
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	pk_debug ("emitting no-percentage-updates tid:%s", item->tid);
	g_signal_emit (engine, signals [PK_ENGINE_NO_PERCENTAGE_UPDATES], 0, item->tid);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_package_cb:
 **/
static void
pk_engine_package_cb (PkBackend *backend, guint value, const gchar *package_id, const gchar *summary, PkEngine *engine)
{
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	pk_debug ("emitting package tid:%s value=%i %s, %s", item->tid, value, package_id, summary);
	g_signal_emit (engine, signals [PK_ENGINE_PACKAGE], 0, item->tid, value, package_id, summary);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_update_detail_cb:
 **/
static void
pk_engine_update_detail_cb (PkBackend *backend, const gchar *package_id,
			    const gchar *updates, const gchar *obsoletes,
			    const gchar *url, const gchar *restart,
			    const gchar *update_text, PkEngine *engine)
{
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	pk_debug ("emitting package tid:%s value=%s, %s, %s, %s, %s, %s", item->tid,
		  package_id, updates, obsoletes, url, restart, update_text);
	g_signal_emit (engine, signals [PK_ENGINE_UPDATE_DETAIL], 0, item->tid,
		       package_id, updates, obsoletes, url, restart, update_text);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_error_code_cb:
 **/
static void
pk_engine_error_code_cb (PkBackend *backend, PkErrorCodeEnum code, const gchar *details, PkEngine *engine)
{
	PkTransactionItem *item;
	const gchar *code_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	code_text = pk_error_enum_to_text (code);
	pk_debug ("emitting error-code tid:%s %s, '%s'", item->tid, code_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_ERROR_CODE], 0, item->tid, code_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_require_restart_cb:
 **/
static void
pk_engine_require_restart_cb (PkBackend *backend, PkRestartEnum restart, const gchar *details, PkEngine *engine)
{
	PkTransactionItem *item;
	const gchar *restart_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	restart_text = pk_restart_enum_to_text (restart);
	pk_debug ("emitting error-code tid:%s %s, '%s'", item->tid, restart_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_REQUIRE_RESTART], 0, item->tid, restart_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_description_cb:
 **/
static void
pk_engine_description_cb (PkBackend *backend, const gchar *package_id, const gchar *licence, PkGroupEnum group,
			  const gchar *detail, const gchar *url, PkEngine *engine)
{
	PkTransactionItem *item;
	const gchar *group_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	group_text = pk_group_enum_to_text (group);

	pk_debug ("emitting description tid:%s, %s, %s, %s, %s, %s", item->tid, package_id, licence, group_text, detail, url);
	g_signal_emit (engine, signals [PK_ENGINE_DESCRIPTION], 0, item->tid, package_id, licence, group_text, detail, url);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkBackend *backend, PkExitEnum exit, PkEngine *engine)
{
	PkTransactionItem *item;
	const gchar *exit_text;
	gdouble time;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}
	exit_text = pk_exit_enum_to_text (exit);

	/* find the length of time we have been running */
	time = pk_backend_get_runtime (backend);

	pk_debug ("backend was running for %f seconds", time);
	pk_transaction_db_set_finished (engine->priv->transaction_db, item->tid, TRUE, time);

	pk_debug ("emitting finished transaction:%s, '%s', %i", item->tid, exit_text, (guint) time);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, item->tid, exit_text, (guint) time);

	/* unref */
	g_object_unref (backend);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_allow_interrupt_cb:
 **/
static void
pk_engine_allow_interrupt_cb (PkBackend *backend, gboolean allow_kill, PkEngine *engine)
{
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	item = pk_transaction_list_get_item_from_backend (engine->priv->transaction_list, backend);
	if (item == NULL) {
		pk_warning ("could not find backend");
		return;
	}

	pk_debug ("emitting allow-interrpt tid:%s, %i", item->tid, allow_kill);
	g_signal_emit (engine, signals [PK_ENGINE_ALLOW_INTERRUPT], 0, item->tid, allow_kill);
}

/**
 * pk_engine_new_backend:
 **/
static PkBackend *
pk_engine_new_backend (PkEngine *engine)
{
	PkBackend *backend;
	gboolean ret;

	g_return_val_if_fail (engine != NULL, NULL);
	g_return_val_if_fail (PK_IS_ENGINE (engine), NULL);

	/* allocate a new backend */
	backend = pk_backend_new ();
	ret = pk_backend_load (backend, engine->priv->backend);
	if (ret == FALSE) {
		pk_warning ("Cannot use backend '%s'", engine->priv->backend);
		return NULL;
	}
	pk_debug ("adding backend %p", backend);

	/* connect up signals */
	g_signal_connect (backend, "transaction-status-changed",
			  G_CALLBACK (pk_engine_transaction_status_changed_cb), engine);
	g_signal_connect (backend, "percentage-changed",
			  G_CALLBACK (pk_engine_percentage_changed_cb), engine);
	g_signal_connect (backend, "sub-percentage-changed",
			  G_CALLBACK (pk_engine_sub_percentage_changed_cb), engine);
	g_signal_connect (backend, "no-percentage-updates",
			  G_CALLBACK (pk_engine_no_percentage_updates_cb), engine);
	g_signal_connect (backend, "package",
			  G_CALLBACK (pk_engine_package_cb), engine);
	g_signal_connect (backend, "update-detail",
			  G_CALLBACK (pk_engine_update_detail_cb), engine);
	g_signal_connect (backend, "error-code",
			  G_CALLBACK (pk_engine_error_code_cb), engine);
	g_signal_connect (backend, "require-restart",
			  G_CALLBACK (pk_engine_require_restart_cb), engine);
	g_signal_connect (backend, "finished",
			  G_CALLBACK (pk_engine_finished_cb), engine);
	g_signal_connect (backend, "description",
			  G_CALLBACK (pk_engine_description_cb), engine);
	g_signal_connect (backend, "allow-interrupt",
			  G_CALLBACK (pk_engine_allow_interrupt_cb), engine);

	/* initialise some stuff */
	pk_engine_reset_timer (engine);

	pk_transaction_list_add (engine->priv->transaction_list, backend);

	/* we don't add to the array or do the transaction-list-changed yet
	 * as this transaction might fail */
	return backend;
}

/**
 * pk_engine_add_backend:
 **/
static gboolean
pk_engine_add_backend (PkEngine *engine, PkTransactionItem *item)
{
	PkRoleEnum role;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* commit, so it appears in the JobList */
	pk_transaction_list_commit (engine->priv->transaction_list, item->backend);

	/* only save into the database for useful stuff */
	pk_backend_get_role (item->backend, &role, NULL);
	if (role == PK_ROLE_ENUM_REFRESH_CACHE ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGE ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		/* add to database */
		pk_transaction_db_add (engine->priv->transaction_db, item->tid);

		/* save role in the database */
		pk_transaction_db_set_role (engine->priv->transaction_db, item->tid, role);
	}
	return TRUE;
}

/**
 * pk_engine_delete_backend:
 *
 * Use this function when a function failed, and we just want to get rid
 * of all references to it.
 **/
gboolean
pk_engine_delete_backend (PkEngine *engine, PkTransactionItem *item)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("removing backend %p as it failed", item->backend);
	pk_transaction_list_remove (engine->priv->transaction_list, item);

	/* we don't do g_object_unref (backend) here as it is done in the
	   ::finished handler */
	return TRUE;
}

/**
 * pk_engine_get_tid:
 **/
gboolean
pk_engine_get_tid (PkEngine *engine, gchar **tid, GError **error)
{
	PkTransactionItem *item;
	item = pk_transaction_list_add (engine->priv->transaction_list, NULL);
	*tid =  g_strdup (item->tid);
	return FALSE;
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
pk_engine_refresh_cache (PkEngine *engine, const gchar *tid, gboolean force, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_refresh_cache (item->backend, force);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}

	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_get_updates:
 **/
gboolean
pk_engine_get_updates (PkEngine *engine, const gchar *tid, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_get_updates (item->backend);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
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
pk_engine_search_name (PkEngine *engine, const gchar *tid, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_search_name (item->backend, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_search_details:
 **/
gboolean
pk_engine_search_details (PkEngine *engine, const gchar *tid, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_search_details (item->backend, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_search_group:
 **/
gboolean
pk_engine_search_group (PkEngine *engine, const gchar *tid, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_search_group (item->backend, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_search_file:
 **/
gboolean
pk_engine_search_file (PkEngine *engine, const gchar *tid, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_search_file (item->backend, filter, search);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_resolve:
 **/
gboolean
pk_engine_resolve (PkEngine *engine, const gchar *tid, const gchar *package, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_resolve (item->backend, package);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_get_depends:
 **/
gboolean
pk_engine_get_depends (PkEngine *engine, const gchar *tid, const gchar *package_id, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				      "The package id '%s' is not valid", package_id);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_get_depends (item->backend, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_get_requires:
 **/
gboolean
pk_engine_get_requires (PkEngine *engine, const gchar *tid, const gchar *package_id, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				      "The package id '%s' is not valid", package_id);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_get_requires (item->backend, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_get_update_detail:
 **/
gboolean
pk_engine_get_update_detail (PkEngine *engine, const gchar *tid, const gchar *package_id, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_PACKAGE_ID_INVALID,
				      "The package id '%s' is not valid", package_id);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_get_update_detail (item->backend, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_get_description:
 **/
gboolean
pk_engine_get_description (PkEngine *engine, const gchar *tid, const gchar *package_id, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "transaction_id '%s' not found", tid);
		return FALSE;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	ret = pk_backend_get_description (item->backend, package_id);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		return FALSE;
	}
	pk_engine_add_backend (engine, item);
	return TRUE;
}

/**
 * pk_engine_update_system:
 **/
void
pk_engine_update_system (PkEngine *engine, const gchar *tid, DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	GError *error;
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.update", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* are we already performing an update? */
	if (pk_transaction_list_role_present (engine->priv->transaction_list, PK_ROLE_ENUM_UPDATE_SYSTEM) == TRUE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
				     "Already performing system update");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	ret = pk_backend_update_system (item->backend);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_backend (engine, item);
}

/**
 * pk_engine_remove_package:
 **/
void
pk_engine_remove_package (PkEngine *engine, const gchar *tid, const gchar *package_id, gboolean allow_deps,
			  DBusGMethodInvocation *context, GError **dead_error)
{
	PkTransactionItem *item;
	gboolean ret;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	ret = pk_backend_remove_package (item->backend, package_id, allow_deps);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_backend (engine, item);
}

/**
 * pk_engine_install_package:
 *
 * This is async, so we have to treat it a bit carefully
 **/
void
pk_engine_install_package (PkEngine *engine, const gchar *tid, const gchar *package_id,
			   DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	ret = pk_backend_install_package (item->backend, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_backend (engine, item);
}

/**
 * pk_engine_install_file:
 *
 * This is async, so we have to treat it a bit carefully
 **/
void
pk_engine_install_file (PkEngine *engine, const gchar *tid, const gchar *full_path,
			DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check file exists */
	ret = g_file_test (full_path, G_FILE_TEST_EXISTS);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_FILE,
				     "No such file '%s'", full_path);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check with PolicyKit if the action is allowed from this client - if not, set an error */
	ret = pk_engine_action_is_allowed (engine, context, "org.freedesktop.packagekit.localinstall", &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	ret = pk_backend_install_file (item->backend, full_path);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_backend (engine, item);
}

/**
 * pk_engine_update_package:
 *
 * This is async, so we have to treat it a bit carefully
 **/
void
pk_engine_update_package (PkEngine *engine, const gchar *tid, const gchar *package_id,
			   DBusGMethodInvocation *context, GError **dead_error)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

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

	/* create a new backend */
	item->backend = pk_engine_new_backend (engine);
	if (item->backend == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	ret = pk_backend_update_package (item->backend, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_delete_backend (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_add_backend (engine, item);
}

/**
 * pk_engine_get_transaction_list:
 **/
gboolean
pk_engine_get_transaction_list (PkEngine *engine, gchar ***transaction_list, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("getting transaction list");
	*transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);

	return TRUE;
}

/**
 * pk_engine_get_status:
 **/
gboolean
pk_engine_get_status (PkEngine *engine, const gchar *tid,
		      const gchar **status, GError **error)
{
	PkStatusEnum status_enum;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	pk_backend_get_status (item->backend, &status_enum);
	*status = g_strdup (pk_status_enum_to_text (status_enum));

	return TRUE;
}

/**
 * pk_engine_get_role:
 **/
gboolean
pk_engine_get_role (PkEngine *engine, const gchar *tid,
		    const gchar **role, const gchar **package_id, GError **error)
{
	PkTransactionItem *item;
	PkRoleEnum role_enum;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	pk_backend_get_role (item->backend, &role_enum, package_id);
	*role = g_strdup (pk_role_enum_to_text (role_enum));

	return TRUE;
}

/**
 * pk_engine_get_percentage:
 **/
gboolean
pk_engine_get_percentage (PkEngine *engine, const gchar *tid, guint *percentage, GError **error)
{
	PkTransactionItem *item;
	gboolean ret;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	ret = pk_backend_get_percentage (item->backend, percentage);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INVALID_STATE,
			     "No percentage data available");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_get_sub_percentage:
 **/
gboolean
pk_engine_get_sub_percentage (PkEngine *engine, const gchar *tid, guint *percentage, GError **error)
{
	PkTransactionItem *item;
	gboolean ret;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	ret = pk_backend_get_sub_percentage (item->backend, percentage);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INVALID_STATE,
			     "No sub-percentage data available");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_get_package:
 **/
gboolean
pk_engine_get_package (PkEngine *engine, const gchar *tid, gchar **package, GError **error)
{
	PkTransactionItem *item;
	gboolean ret;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	ret = pk_backend_get_package (item->backend, package);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INVALID_STATE,
			     "No package data available");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_get_old_transactions:
 **/
gboolean
pk_engine_get_old_transactions (PkEngine *engine, const gchar *tid, guint number, GError **error)
{
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	item = pk_transaction_list_add (engine->priv->transaction_list, NULL);
	engine->priv->sync_item = item;
	pk_transaction_db_get_list (engine->priv->transaction_db, number);
//	pk_engine_finished_cb ();

	pk_debug ("emitting finished transaction:%s, '%s', %i", item->tid, "", 0);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, item->tid, "", 0);

	pk_transaction_list_remove (engine->priv->transaction_list, item);

	return TRUE;
//xxx
}

/**
 * pk_engine_cancel:
 **/
gboolean
pk_engine_cancel (PkEngine *engine, const gchar *tid, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_item_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}

	ret = pk_backend_cancel (item->backend);
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
	PkBackend *backend;
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new backend */
	backend = pk_engine_new_backend (engine);
	if (backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	elist = pk_backend_get_actions (backend);
	*actions = pk_enum_list_to_string (elist);
	g_object_unref (backend);
	g_object_unref (elist);

	return TRUE;
}


/**
 * pk_engine_get_backend_detail:
 * @engine: This class instance
 **/
gboolean
pk_engine_get_backend_detail (PkEngine *engine, gchar **name, gchar **author, gchar **version, GError **error)
{
	PkBackend *backend;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new backend */
	backend = pk_engine_new_backend (engine);
	if (backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	pk_backend_get_backend_detail (backend, name, author, version);
	g_object_unref (backend);

	return TRUE;
}

/**
 * pk_engine_get_groups:
 * @engine: This class instance
 **/
gboolean
pk_engine_get_groups (PkEngine *engine, gchar **groups, GError **error)
{
	PkBackend *backend;
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new backend */
	backend = pk_engine_new_backend (engine);
	if (backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	elist = pk_backend_get_groups (backend);
	*groups = pk_enum_list_to_string (elist);
	g_object_unref (backend);
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
	PkBackend *backend;
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* create a new backend */
	backend = pk_engine_new_backend (engine);
	if (backend == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
			     "Backend '%s' could not be initialized", engine->priv->backend);
		return FALSE;
	}

	elist = pk_backend_get_filters (backend);
	*filters = pk_enum_list_to_string (elist);
	g_object_unref (backend);
	g_object_unref (elist);

	return TRUE;
}

/**
 * pk_engine_transaction_cb:
 **/
static void
pk_engine_transaction_cb (PkTransactionDb *tdb, const gchar *old_tid, const gchar *timespec,
			  gboolean succeeded, const gchar *role, guint duration, PkEngine *engine)
{
	const gchar *tid = engine->priv->sync_item->tid;
	pk_debug ("emitting transaction %s, %s, %s, %i, %s, %i", tid, old_tid, timespec, succeeded, role, duration);
	g_signal_emit (engine, signals [PK_ENGINE_TRANSACTION], 0, tid, old_tid, timespec, succeeded, role, duration);
}

/**
 * pk_engine_get_seconds_idle:
 * @engine: This class instance
 **/
guint
pk_engine_get_seconds_idle (PkEngine *engine)
{
	guint idle;
	guint size;

	g_return_val_if_fail (engine != NULL, 0);
	g_return_val_if_fail (PK_IS_ENGINE (engine), 0);

	/* check for transactions running - a transaction that takes a *long* time might not
	 * give sufficient percentage updates to not be marked as idle */
	size = pk_transaction_list_get_size (engine->priv->transaction_list);
	if (size != 0) {
		pk_debug ("engine idle zero as %i transactions in progress", size);
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
	signals [PK_ENGINE_TRANSACTION_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);
	signals [PK_ENGINE_TRANSACTION_STATUS_CHANGED] =
		g_signal_new ("transaction-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_ENGINE_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_ENGINE_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [PK_ENGINE_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_ENGINE_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_ALLOW_INTERRUPT] =
		g_signal_new ("allow-interrupt",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_BOOL,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);
	signals [PK_ENGINE_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_BOOL_STRING_UINT,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT);

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
	engine->priv->timer = g_timer_new ();
	engine->priv->backend = NULL;

	engine->priv->transaction_list = pk_transaction_list_new ();
	g_signal_connect (engine->priv->transaction_list, "changed",
			  G_CALLBACK (pk_engine_transaction_list_changed_cb), engine);

	/* we use a trasaction db to store old transactions and to do rollbacks */
	engine->priv->transaction_db = pk_transaction_db_new ();
	g_signal_connect (engine->priv->transaction_db, "transaction",
			  G_CALLBACK (pk_engine_transaction_cb), engine);

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
	g_object_unref (engine->priv->transaction_list);
	g_object_unref (engine->priv->transaction_db);

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
