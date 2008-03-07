/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#include <pk-package-id.h>
#include <pk-package-list.h>

#include <pk-debug.h>
#include <pk-common.h>
#include <pk-network.h>
#include <pk-package-list.h>
#include <pk-enum.h>

#include "pk-backend.h"
#include "pk-backend-internal.h"
#include "pk-runner.h"
#include "pk-engine.h"
#include "pk-transaction-db.h"
#include "pk-transaction-list.h"
#include "pk-inhibit.h"
#include "pk-marshal.h"
#include "pk-security.h"

static void     pk_engine_class_init	(PkEngineClass *klass);
static void     pk_engine_init		(PkEngine      *engine);
static void     pk_engine_finalize	(GObject       *object);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))
#define PK_ENGINE_UPDATES_CHANGED_TIMEOUT	100 /* ms */

struct PkEnginePrivate
{
	GTimer			*timer;
	PkTransactionList	*transaction_list;
	PkTransactionDb		*transaction_db;
	PkTransactionItem	*sync_item;
	PkPackageList		*updates_cache;
	PkBackend		*backend;
	PkInhibit		*inhibit;
	PkNetwork		*network;
	PkSecurity		*security;
	PkEnumList		*actions;
	PkEnumList		*groups;
	PkEnumList		*filters;
};

enum {
	PK_ENGINE_TRANSACTION_LIST_CHANGED,
	PK_ENGINE_STATUS_CHANGED,
	PK_ENGINE_PROGRESS_CHANGED,
	PK_ENGINE_PACKAGE,
	PK_ENGINE_TRANSACTION,
	PK_ENGINE_ERROR_CODE,
	PK_ENGINE_REQUIRE_RESTART,
	PK_ENGINE_MESSAGE,
	PK_ENGINE_UPDATES_CHANGED,
	PK_ENGINE_REPO_SIGNATURE_REQUIRED,
	PK_ENGINE_FINISHED,
	PK_ENGINE_UPDATE_DETAIL,
	PK_ENGINE_DESCRIPTION,
	PK_ENGINE_FILES,
	PK_ENGINE_ALLOW_CANCEL,
	PK_ENGINE_CALLER_ACTIVE_CHANGED,
	PK_ENGINE_LOCKED,
	PK_ENGINE_REPO_DETAIL,
	PK_ENGINE_LAST_SIGNAL
};

static guint	     signals [PK_ENGINE_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkEngine, pk_engine, G_TYPE_OBJECT)

/* prototype */
gboolean pk_engine_filter_check (const gchar *filter, GError **error);

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
			ENUM_ENTRY (PK_ENGINE_ERROR_INPUT_INVALID, "InputInvalid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_INVALID_STATE, "InvalidState"),
			ENUM_ENTRY (PK_ENGINE_ERROR_INITIALIZE_FAILED, "InitializeFailed"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("PkEngineError", values);
	}
	return etype;
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
 * pk_engine_inhibit_locked_cb:
 **/
static void
pk_engine_inhibit_locked_cb (PkInhibit *inhibit, gboolean is_locked, PkEngine *engine)
{
	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));
	pk_debug ("emitting locked %i", is_locked);
	g_signal_emit (engine, signals [PK_ENGINE_LOCKED], 0, is_locked);
}

/**
 * pk_engine_status_changed_cb:
 **/
static void
pk_engine_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkEngine *engine)
{
	const gchar *status_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	status_text = pk_status_enum_to_text (status);

	pk_debug ("emitting status-changed tid:%s, '%s'", c_tid, status_text);
	g_signal_emit (engine, signals [PK_ENGINE_STATUS_CHANGED], 0, c_tid, status_text);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_progress_changed_cb:
 **/
static void
pk_engine_progress_changed_cb (PkBackend *backend, guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	pk_debug ("emitting percentage-changed tid:%s %i, %i, %i, %i",
		  c_tid, percentage, subpercentage, elapsed, remaining);
	g_signal_emit (engine, signals [PK_ENGINE_PROGRESS_CHANGED], 0,
		       c_tid, percentage, subpercentage, elapsed, remaining);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_package_cb:
 **/
static void
pk_engine_package_cb (PkBackend *backend, PkInfoEnum info, const gchar *package_id, const gchar *summary, PkEngine *engine)
{
	const gchar *info_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}

	info_text = pk_info_enum_to_text (info);
	pk_debug ("emitting package tid:%s info=%s %s, %s", c_tid, info_text, package_id, summary);
	g_signal_emit (engine, signals [PK_ENGINE_PACKAGE], 0, c_tid, info_text, package_id, summary);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_update_detail_cb:
 **/
static void
pk_engine_update_detail_cb (PkBackend *backend, const gchar *package_id,
			    const gchar *updates, const gchar *obsoletes,
			    const gchar *vendor_url, const gchar *bugzilla_url,
			    const gchar *cve_url, PkRestartEnum restart,
			    const gchar *update_text, PkEngine *engine)
{
	const gchar *c_tid;
	const gchar *restart_text;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	restart_text = pk_restart_enum_to_text (restart);
	pk_debug ("emitting package tid:%s value=%s, %s, %s, %s, %s, %s, %s, %s", c_tid,
		  package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart_text, update_text);
	g_signal_emit (engine, signals [PK_ENGINE_UPDATE_DETAIL], 0, c_tid,
		       package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart_text, update_text);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_updates_changed_cb:
 **/
static void
pk_engine_updates_changed_cb (PkBackend *backend, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	pk_debug ("emitting updates-changed tid:%s", c_tid);
	g_signal_emit (engine, signals [PK_ENGINE_UPDATES_CHANGED], 0, c_tid);
}

/**
 * pk_engine_repo_signature_required_cb:
 **/
static void
pk_engine_repo_signature_required_cb (PkBackend *backend, const gchar *repository_name, const gchar *key_url,
				      const gchar *key_userid, const gchar *key_id, const gchar *key_fingerprint,
				      const gchar *key_timestamp, PkSigTypeEnum type, PkEngine *engine)
{
	const gchar *type_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	type_text = pk_sig_type_enum_to_text (type);

	pk_debug ("emitting repo_signature_required tid:%s, %s, %s, %s, %s, %s, %s, %s",
		  c_tid, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
	g_signal_emit (engine, signals [PK_ENGINE_REPO_SIGNATURE_REQUIRED], 0,
		       c_tid, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
}

/**
 * pk_engine_error_code_cb:
 **/
static void
pk_engine_error_code_cb (PkBackend *backend, PkErrorCodeEnum code, const gchar *details, PkEngine *engine)
{
	const gchar *code_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	code_text = pk_error_enum_to_text (code);
	pk_debug ("emitting error-code tid:%s %s, '%s'", c_tid, code_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_ERROR_CODE], 0, c_tid, code_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_require_restart_cb:
 **/
static void
pk_engine_require_restart_cb (PkBackend *backend, PkRestartEnum restart, const gchar *details, PkEngine *engine)
{
	const gchar *restart_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	restart_text = pk_restart_enum_to_text (restart);
	pk_debug ("emitting require-restart tid:%s %s, '%s'", c_tid, restart_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_REQUIRE_RESTART], 0, c_tid, restart_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_message_cb:
 **/
static void
pk_engine_message_cb (PkBackend *backend, PkMessageEnum message, const gchar *details, PkEngine *engine)
{
	const gchar *message_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	message_text = pk_message_enum_to_text (message);
	pk_debug ("emitting message tid:%s %s, '%s'", c_tid, message_text, details);
	g_signal_emit (engine, signals [PK_ENGINE_MESSAGE], 0, c_tid, message_text, details);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_description_cb:
 **/
static void
pk_engine_description_cb (PkBackend *backend, const gchar *package_id, const gchar *license, PkGroupEnum group,
			  const gchar *detail, const gchar *url,
			  guint64 size, PkEngine *engine)
{
	const gchar *group_text;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}
	group_text = pk_group_enum_to_text (group);

	pk_debug ("emitting description tid:%s, %s, %s, %s, %s, %s, %ld",
		  c_tid, package_id, license, group_text, detail, url, (long int) size);
	g_signal_emit (engine, signals [PK_ENGINE_DESCRIPTION], 0,
		       c_tid, package_id, license, group_text, detail, url, size);
}

/**
 * pk_engine_files_cb:
 **/
static void
pk_engine_files_cb (PkBackend *backend, const gchar *package_id,
		    const gchar *filelist, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}

	pk_debug ("emitting files tid:%s, %s, %s",
		  c_tid, package_id, filelist);
	g_signal_emit (engine, signals [PK_ENGINE_FILES], 0,
		       c_tid, package_id, filelist);
}

/**
 * pk_engine_finished_updates_changed_cb:
 **/
static gboolean
pk_engine_finished_updates_changed_cb (gpointer data)
{
	const gchar *tid;
	PkEngine *engine = PK_ENGINE (data);

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	tid = (const gchar *) g_object_get_data (G_OBJECT (engine), "calling-tid");

	pk_debug ("emitting updates-changed tid:%s", tid);
	g_signal_emit (engine, signals [PK_ENGINE_UPDATES_CHANGED], 0, tid);
	return FALSE;
}

/**
 * pk_engine_finish_invalidate_caches:
 **/
static gboolean
pk_engine_finish_invalidate_caches (PkEngine *engine, PkTransactionItem *item, PkRoleEnum role)
{
	const gchar *c_tid;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return FALSE;
	}

	pk_debug ("invalidating caches");

	/* copy this into the cache if we are getting updates */
	if (role == PK_ROLE_ENUM_GET_UPDATES) {
		if (engine->priv->updates_cache != NULL) {
			pk_debug ("unreffing updates cache");
			g_object_unref (engine->priv->updates_cache);
		}
		engine->priv->updates_cache = pk_runner_get_package_list (item->runner);
		pk_debug ("reffing updates cache");
		g_object_ref (engine->priv->updates_cache);
		g_object_add_weak_pointer (G_OBJECT (engine->priv->updates_cache), (gpointer) &engine->priv->updates_cache);
	}

	/* we unref the update cache if it exists */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		if (engine->priv->updates_cache != NULL) {
			pk_debug ("unreffing updates cache as we have just finished an update");
			g_object_unref (engine->priv->updates_cache);
			engine->priv->updates_cache = NULL;
		}
	}

	/* this has to be done as different repos might have different updates */
	if (role == PK_ROLE_ENUM_REPO_ENABLE ||
	    role == PK_ROLE_ENUM_REPO_SET_DATA) {
		if (engine->priv->updates_cache != NULL) {
			pk_debug ("unreffing updates cache as we have just enabled/disabled a repo");
			g_object_unref (engine->priv->updates_cache);
			engine->priv->updates_cache = NULL;
		}
	}

	/* could the update list have changed? */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGE ||
	    role == PK_ROLE_ENUM_REPO_ENABLE ||
	    role == PK_ROLE_ENUM_REPO_SET_DATA ||
	    role == PK_ROLE_ENUM_REFRESH_CACHE) {
		/* this needs to be done after a small delay */
		g_object_set_data (G_OBJECT (engine), "calling-tid", g_strdup (c_tid));
		g_timeout_add (PK_ENGINE_UPDATES_CHANGED_TIMEOUT, pk_engine_finished_updates_changed_cb, engine);
	}
	return TRUE;
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkBackend *backend, PkExitEnum exit, PkEngine *engine)
{
	PkTransactionItem *item;
	PkRoleEnum role;
	const gchar *exit_text;
	guint time;
	gchar *packages;
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}

	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, c_tid);
	if (item == NULL) {
		pk_warning ("tid not found in transaction list");
		return;
	}

	/* we might not have this set yet */
	if (item->runner == NULL) {
		pk_warning ("Backend not set yet!");
		return;
	}

	/* get what the role was */
	role = pk_runner_get_role (item->runner);

	/* invalidate some caches if we succeeded*/
	if (exit == PK_EXIT_ENUM_SUCCESS) {
		pk_engine_finish_invalidate_caches (engine, item, role);
	}

	/* find the length of time we have been running */
	time = pk_runner_get_runtime (item->runner);

	/* add to the database if we are going to log it */
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGE ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		packages = pk_package_list_get_string (pk_runner_get_package_list (item->runner));
		if (pk_strzero (packages) == FALSE) {
			pk_transaction_db_set_data (engine->priv->transaction_db, c_tid, packages);
		}
		g_free (packages);
	}

	pk_debug ("backend was running for %i ms", time);
	pk_transaction_db_set_finished (engine->priv->transaction_db, c_tid, TRUE, time);

	/* only reset the time if we succeeded */
	if (exit == PK_EXIT_ENUM_SUCCESS) {
		pk_transaction_db_action_time_reset (engine->priv->transaction_db, role);
	}

	exit_text = pk_exit_enum_to_text (exit);
	pk_debug ("emitting finished transaction:%s, '%s', %i", c_tid, exit_text, time);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, c_tid, exit_text, time);

	/* daemon is busy */
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_allow_cancel_cb:
 **/
static void
pk_engine_allow_cancel_cb (PkBackend *backend, gboolean allow_cancel, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}

	pk_debug ("emitting allow-interrpt tid:%s, %i", c_tid, allow_cancel);
	g_signal_emit (engine, signals [PK_ENGINE_ALLOW_CANCEL], 0, c_tid, allow_cancel);
}

/**
 * pk_engine_caller_active_changed_cb:
 **/
static void
pk_engine_caller_active_changed_cb (PkRunner *runner, gboolean is_active, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_runner_get_tid (runner);
	if (c_tid == NULL) {
		/* we might get this if we emulated a backend */
		pk_debug ("could not get current tid from runner");
		return;
	}

	pk_debug ("emitting caller-active-changed tid:%s, %i", c_tid, is_active);
	g_signal_emit (engine, signals [PK_ENGINE_CALLER_ACTIVE_CHANGED], 0, c_tid, is_active);
}

/**
 * pk_engine_change_transaction_data_cb:
 **/
static void
pk_engine_change_transaction_data_cb (PkBackend *backend, gchar *data, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}

	/* change the database */
	pk_warning ("TODO: change the c_tid and resave to database");
}

/**
 * pk_engine_repo_detail_cb:
 **/
static void
pk_engine_repo_detail_cb (PkBackend *backend, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkEngine *engine)
{
	const gchar *c_tid;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	c_tid = pk_backend_get_current_tid (engine->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return;
	}

	pk_debug ("emitting repo-detail tid:%s, %s, %s, %i", c_tid, repo_id, description, enabled);
	g_signal_emit (engine, signals [PK_ENGINE_REPO_DETAIL], 0, c_tid, repo_id, description, enabled);
}

/**
 * pk_engine_item_commit:
 **/
static gboolean
pk_engine_item_commit (PkEngine *engine, PkTransactionItem *item)
{
	PkRoleEnum role;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* we might not have this set yet */
	if (item->runner == NULL) {
		pk_warning ("Backend not set yet!");
		return FALSE;
	}

	/* commit, so it appears in the JobList */
	pk_transaction_list_commit (engine->priv->transaction_list, item);

	/* only save into the database for useful stuff */
	role = pk_runner_get_role (item->runner);
	if (role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
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
 * pk_engine_item_delete:
 *
 * Use this function when a function failed, and we just want to get rid
 * of all references to it.
 **/
gboolean
pk_engine_item_delete (PkEngine *engine, PkTransactionItem *item)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("removing backend %p as it failed", item->runner);
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

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetTid method called");

	item = pk_transaction_list_create (engine->priv->transaction_list);
	pk_debug ("sending tid: '%s'", item->tid);
	*tid =  g_strdup (item->tid);
	return TRUE;
}

/**
 * pk_engine_action_is_allowed:
 *
 * Only valid from an async caller, which is fine, as we won't prompt the user
 * when not async.
 **/
static gboolean
pk_engine_action_is_allowed (PkEngine *engine, const gchar *dbus_sender,
			     PkRoleEnum role, GError **error)
{
	gboolean ret;
	gchar *error_detail;

	/* could we actually do this, even with the right permissions? */
	ret = pk_enum_list_contains (engine->priv->actions, role);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "%s not supported", pk_role_enum_to_text (role));
		return FALSE;
	}

	/* use security model to get auth */
	ret = pk_security_action_is_allowed (engine->priv->security, dbus_sender, role, &error_detail);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_REFUSED_BY_POLICY, error_detail);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_runner_new:
 **/
static PkRunner *
pk_engine_runner_new (PkEngine *engine)
{
	PkRunner *runner;

	g_return_val_if_fail (engine != NULL, NULL);
	g_return_val_if_fail (PK_IS_ENGINE (engine), NULL);

	runner = pk_runner_new ();
	g_signal_connect (runner, "caller-active-changed",
			  G_CALLBACK (pk_engine_caller_active_changed_cb), engine);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	return runner;
}

/**
 * pk_engine_refresh_cache:
 **/
void
pk_engine_refresh_cache (PkEngine *engine, const gchar *tid, gboolean force, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("RefreshCache method called: %s, %i", tid, force);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INITIALIZE_FAILED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_REFRESH_CACHE, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	/* we unref the update cache if it exists */
	if (engine->priv->updates_cache != NULL) {
		pk_debug ("unreffing updates cache");
		g_object_unref (engine->priv->updates_cache);
		engine->priv->updates_cache = NULL;
	}

	ret = pk_runner_refresh_cache (item->runner, force);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_updates:
 **/
void
pk_engine_get_updates (PkEngine *engine, const gchar *tid, const gchar *filter, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetUpdates method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_updates (item->runner, filter);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* try and reuse cache */
	if (engine->priv->updates_cache != NULL) {
		PkPackageItem *package;
		const gchar *info_text;
		const gchar *exit_text;
		guint i;
		guint length;

		length = pk_package_list_get_size (engine->priv->updates_cache);
		pk_debug ("we have cached data (%i) we should use!", length);

		/* emulate the backend */
		pk_runner_set_role (item->runner, PK_ROLE_ENUM_GET_UPDATES);
		for (i=0; i<length; i++) {
			package = pk_package_list_get_item (engine->priv->updates_cache, i);
			info_text = pk_info_enum_to_text (package->info);
			g_signal_emit (engine, signals [PK_ENGINE_PACKAGE], 0, tid, info_text, package->package_id, package->summary);
		}

		/* we are done */
		exit_text = pk_exit_enum_to_text (PK_EXIT_ENUM_SUCCESS);
		pk_debug ("emitting finished transaction:%s, '%s'", tid, exit_text);
		g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, tid, exit_text, 0);

		pk_engine_item_delete (engine, item);
		dbus_g_method_return (context);
		return;
	}

	/* only commit if we haven't emulated */
	pk_engine_item_commit (engine, item);

	dbus_g_method_return (context);
}

/**
 * pk_engine_search_check:
 **/
gboolean
pk_engine_search_check (const gchar *search, GError **error)
{
	guint size;
	gboolean ret;

	/* ITS4: ignore, not used for allocation, and checked */
	size = strlen (search);

	if (search == NULL) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
				     "Search is null. This isn't supposed to happen...");
		return FALSE;
	}
	if (size == 0) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
				     "Search string zero length");
		return FALSE;
	}
	if (size < 2) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
				     "The search string length is too small");
		return FALSE;
	}
	if (size > 1024) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
				     "The search string length is too large");
		return FALSE;
	}
	if (strstr (search, "*") != NULL) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
				     "Invalid search containing '*'");
		return FALSE;
	}
	if (strstr (search, "?") != NULL) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_SEARCH_INVALID,
				     "Invalid search containing '?'");
		return FALSE;
	}
	ret = pk_strvalidate (search);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid search term");
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

	/* check for invalid input */
	ret = pk_strvalidate (filter);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid filter term");
		return FALSE;
	}

	/* check for invalid filter */
	ret = pk_filter_check (filter);
	if (ret == FALSE) {
		*error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_FILTER_INVALID,
				     "Filter '%s' is invalid", filter);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_search_name:
 **/
void
pk_engine_search_name (PkEngine *engine, const gchar *tid, const gchar *filter,
		       const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("SearchName method called: %s, %s, %s", tid, filter, search);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_engine_search_check (search, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_search_name (item->runner, filter, search);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_search_details:
 **/
void
pk_engine_search_details (PkEngine *engine, const gchar *tid, const gchar *filter,
			  const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("SearchDetails method called: %s, %s, %s", tid, filter, search);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_engine_search_check (search, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_search_details (item->runner, filter, search);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_search_group:
 **/
void
pk_engine_search_group (PkEngine *engine, const gchar *tid, const gchar *filter,
			const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("SearchGroup method called: %s, %s, %s", tid, filter, search);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_engine_search_check (search, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_search_group (item->runner, filter, search);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_search_file:
 **/
void
pk_engine_search_file (PkEngine *engine, const gchar *tid, const gchar *filter,
		       const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("SearchFile method called: %s, %s, %s", tid, filter, search);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_engine_search_check (search, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_search_file (item->runner, filter, search);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_resolve:
 **/
void
pk_engine_resolve (PkEngine *engine, const gchar *tid, const gchar *filter,
		   const gchar *package, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("Resolve method called: %s, %s, %s", tid, filter, package);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_engine_filter_check (filter, &error);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_resolve (item->runner, filter, package);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_depends:
 **/
void
pk_engine_get_depends (PkEngine *engine, const gchar *tid, const gchar *package_id,
		       gboolean recursive, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetDepends method called: %s, %s, %i", tid, package_id, recursive);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_depends (item->runner, package_id, recursive);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_requires:
 **/
void
pk_engine_get_requires (PkEngine *engine, const gchar *tid, const gchar *package_id,
			gboolean recursive, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetRequires method called: %s, %s, %i", tid, package_id, recursive);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_requires (item->runner, package_id, recursive);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_update_detail:
 **/
void
pk_engine_get_update_detail (PkEngine *engine, const gchar *tid, const gchar *package_id,
			     DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetUpdateDetail method called: %s, %s", tid, package_id);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_update_detail (item->runner, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_description:
 **/
void
pk_engine_get_description (PkEngine *engine, const gchar *tid, const gchar *package_id,
			   DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetDescription method called: %s, %s", tid, package_id);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_description (item->runner, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_files:
 **/
void
pk_engine_get_files (PkEngine *engine, const gchar *tid, const gchar *package_id,
		     DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetFiles method called: %s, %s", tid, package_id);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_files (item->runner, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_update_system:
 **/
void
pk_engine_update_system (PkEngine *engine, const gchar *tid, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	PkTransactionItem *item;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("UpdateSystem method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_UPDATE_SYSTEM, &error);
	g_free (sender);
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

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_update_system (item->runner);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_remove_package:
 **/
void
pk_engine_remove_package (PkEngine *engine, const gchar *tid, const gchar *package_id, gboolean allow_deps, gboolean autoremove,
			  DBusGMethodInvocation *context)
{
	PkTransactionItem *item;
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("RemovePackage method called: %s, %s, %i, %i", tid, package_id, allow_deps, autoremove);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_REMOVE_PACKAGE, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_remove_package (item->runner, package_id, allow_deps, autoremove);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_install_package:
 **/
void
pk_engine_install_package (PkEngine *engine, const gchar *tid, const gchar *package_id,
			   DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("method called: %s, %s", tid, package_id);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_INSTALL_PACKAGE, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_install_package (item->runner, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_install_file:
 **/
void
pk_engine_install_file (PkEngine *engine, const gchar *tid, const gchar *full_path,
			DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("InstallFile method called: %s, %s", tid, full_path);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
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

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_INSTALL_FILE, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_install_file (item->runner, full_path);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_service_pack:
 **/
void
pk_engine_service_pack (PkEngine *engine, const gchar *tid, const gchar *location,
			DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("ServicePack method called: %s, %s", tid, location);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_SERVICE_PACK, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_service_pack (item->runner, location);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_rollback:
 **/
void
pk_engine_rollback (PkEngine *engine, const gchar *tid, const gchar *transaction_id,
		    DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("Rollback method called: %s, %s", tid, transaction_id);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (transaction_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_ROLLBACK, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_rollback (item->runner, transaction_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_update_package:
 **/
void
pk_engine_update_package (PkEngine *engine, const gchar *tid, const gchar *package_id, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkTransactionItem *item;
	GError *error;
	gchar *sender;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("UpdatePackage method called: %s, %s", tid, package_id);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
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

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_UPDATE_PACKAGE, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_update_package (item->runner, package_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_repo_list:
 **/
void
pk_engine_get_repo_list (PkEngine *engine, const gchar *tid, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("GetRepoList method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_get_repo_list (item->runner);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_repo_enable:
 **/
void
pk_engine_repo_enable (PkEngine *engine, const gchar *tid, const gchar *repo_id, gboolean enabled,
		       DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("RepoEnable method called: %s, %s, %i", tid, repo_id, enabled);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (repo_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_REPO_ENABLE, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}


	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_repo_enable (item->runner, repo_id, enabled);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_repo_set_data:
 **/
void
pk_engine_repo_set_data (PkEngine *engine, const gchar *tid, const gchar *repo_id,
			 const gchar *parameter, const gchar *value,
		         DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;
	PkTransactionItem *item;

	g_return_if_fail (engine != NULL);
	g_return_if_fail (PK_IS_ENGINE (engine));

	pk_debug ("RepoSetData method called: %s, %s, %s, %s", tid, repo_id, parameter, value);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "transaction_id '%s' not found", tid);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (repo_id);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_engine_action_is_allowed (engine, sender, PK_ROLE_ENUM_REPO_SET_DATA, &error);
	g_free (sender);
	if (ret == FALSE) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* create a new runner object */
	item->runner = pk_engine_runner_new (engine);

	/* set the dbus name, so we can get the disconnect */
	pk_runner_set_dbus_name (item->runner, dbus_g_method_get_sender (context));

	ret = pk_runner_repo_set_data (item->runner, repo_id, parameter, value);
	if (ret == FALSE) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_engine_item_delete (engine, item);
		dbus_g_method_return_error (context, error);
		return;
	}
	pk_engine_item_commit (engine, item);
	dbus_g_method_return (context);
}

/**
 * pk_engine_get_transaction_list:
 **/
gboolean
pk_engine_get_transaction_list (PkEngine *engine, gchar ***transaction_list, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetTransactionList method called");
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

	pk_debug ("GetStatus method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	status_enum = pk_runner_get_status (item->runner);
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
	PkRoleEnum role_enum;
	PkTransactionItem *item;
	const gchar *text;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetRole method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}

	/* we might not have this set yet */
	if (item->runner == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "Backend not set with tid:%s", tid);
		return FALSE;
	}

	role_enum = pk_runner_get_role (item->runner);
	text = pk_runner_get_text (item->runner);
	*role = g_strdup (pk_role_enum_to_text (role_enum));
	*package_id = g_strdup (text);

	return TRUE;
}

/**
 * pk_engine_get_progress:
 **/
gboolean
pk_engine_get_progress (PkEngine *engine, const gchar *tid,
			guint *percentage, guint *subpercentage,
			guint *elapsed, guint *remaining, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetProgress method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	ret = pk_backend_get_progress (engine->priv->backend, percentage, subpercentage, elapsed, remaining);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INVALID_STATE,
			     "No progress data available");
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
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetPackage method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	ret = pk_runner_get_package (item->runner, package);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INVALID_STATE,
			     "No package data available");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_engine_get_allow_cancel:
 **/
gboolean
pk_engine_get_allow_cancel (PkEngine *engine, const gchar *tid, gboolean *allow_cancel, GError **error)
{
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetAllowCancel method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	*allow_cancel = pk_runner_get_allow_cancel (item->runner);
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

	pk_debug ("GetOldTransactions method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}
	engine->priv->sync_item = item;

	pk_transaction_db_get_list (engine->priv->transaction_db, number);
	pk_debug ("emitting finished transaction:%s, '%s', %i", tid, "", 0);
	g_signal_emit (engine, signals [PK_ENGINE_FINISHED], 0, tid, "", 0);
	pk_transaction_list_remove (engine->priv->transaction_list, item);
	return TRUE;
}

/**
 * pk_engine_cancel:
 **/
gboolean
pk_engine_cancel (PkEngine *engine, const gchar *tid, GError **error)
{
	gboolean ret;
	gchar *error_text = NULL;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("Cancel method called: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}

	/* check to see if we are trying to cancel a non-running task */
	if (item->running == FALSE) {
		pk_debug ("cancelling the non-running item %p", item);
		pk_engine_item_delete (engine, item);
		return TRUE;
	}

	/* try to cancel the transaction */
	ret = pk_runner_cancel (item->runner, &error_text);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED, error_text);
		g_free (error_text);
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_engine_state_has_changed:
 *
 * This should be called when tools like pup, pirut and yum-cli
 * have finished their transaction, and the update cache may not be valid.
 **/
gboolean
pk_engine_state_has_changed (PkEngine *engine, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	if (engine->priv->updates_cache != NULL) {
		pk_debug ("unreffing updates cache as state may have changed");
		g_object_unref (engine->priv->updates_cache);
		engine->priv->updates_cache = NULL;
	}
	pk_debug ("emitting updates-changed tid:%s", "unknown");
	g_signal_emit (engine, signals [PK_ENGINE_UPDATES_CHANGED], 0, "unknown");
	return TRUE;
}

/**
 * pk_engine_is_caller_active:
 **/
gboolean
pk_engine_is_caller_active (PkEngine *engine, const gchar *tid, gboolean *is_active, GError **error)
{
	gboolean ret;
	PkTransactionItem *item;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("is caller active: %s", tid);

	/* find pre-requested transaction id */
	item = pk_transaction_list_get_from_tid (engine->priv->transaction_list, tid);
	if (item == NULL) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NO_SUCH_TRANSACTION,
			     "No tid:%s", tid);
		return FALSE;
	}

	/* is the caller still active? */
	ret = pk_runner_is_caller_active (item->runner, is_active);
	if (ret == FALSE) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_NOT_SUPPORTED, "We don't know if the caller is still there");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_engine_get_actions:
 **/
gboolean
pk_engine_get_actions (PkEngine *engine, gchar **actions, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);
	*actions = pk_enum_list_to_string (engine->priv->actions);
	return TRUE;
}

/**
 * pk_engine_get_groups:
 **/
gboolean
pk_engine_get_groups (PkEngine *engine, gchar **groups, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);
	*groups = pk_enum_list_to_string (engine->priv->groups);
	return TRUE;
}

/**
 * pk_engine_get_filters:
 **/
gboolean
pk_engine_get_filters (PkEngine *engine, gchar **filters, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);
	*filters = pk_enum_list_to_string (engine->priv->filters);
	return TRUE;
}

/**
 * pk_engine_get_backend_detail:
 **/
gboolean
pk_engine_get_backend_detail (PkEngine *engine, gchar **name, gchar **author, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetBackendDetail method called");
	pk_backend_get_backend_detail (engine->priv->backend, name, author);
	return TRUE;
}

/**
 * pk_engine_get_time_since_action:
 *
 * @seconds: Number of seconds since the role was called, or zero is unknown
 **/
gboolean
pk_engine_get_time_since_action	(PkEngine *engine, const gchar *role_text, guint *seconds, GError **error)
{
	PkRoleEnum role;

	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	role = pk_role_enum_from_text (role_text);
	*seconds = pk_transaction_db_action_time_since (engine->priv->transaction_db, role);

	return TRUE;
}

/**
 * pk_engine_transaction_cb:
 **/
static void
pk_engine_transaction_cb (PkTransactionDb *tdb, const gchar *old_tid, const gchar *timespec,
			  gboolean succeeded, PkRoleEnum role, guint duration,
			  const gchar *data, PkEngine *engine)
{
	PkTransactionItem *item;
	const gchar *role_text;

	item = engine->priv->sync_item;
	if (item == NULL) {
		pk_warning ("no sync tid");
		return;
	}
	role_text = pk_role_enum_to_text (role);
	pk_debug ("emitting transaction %s, %s, %s, %i, %s, %i, %s", item->tid, old_tid, timespec, succeeded, role_text, duration, data);
	g_signal_emit (engine, signals [PK_ENGINE_TRANSACTION], 0, item->tid, old_tid, timespec, succeeded, role_text, duration, data);
}

/**
 * pk_engine_get_seconds_idle:
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
	signals [PK_ENGINE_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_ENGINE_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
	signals [PK_ENGINE_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [PK_ENGINE_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_UINT64,
			      G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64);
	signals [PK_ENGINE_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_ENGINE_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 9, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_ENGINE_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_BOOL,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);
	signals [PK_ENGINE_CALLER_ACTIVE_CHANGED] =
		g_signal_new ("caller-active-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_BOOL,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);
	signals [PK_ENGINE_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_ENGINE_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_BOOL_STRING_UINT_STRING,
			      G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_ENGINE_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_BOOL,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkEnginePrivate));
}

/**
 * pk_engine_init:
 **/
static void
pk_engine_init (PkEngine *engine)
{
	PkRunner *runner;
	gboolean ret;

	engine->priv = PK_ENGINE_GET_PRIVATE (engine);

	/* setup the backend backend */
	engine->priv->backend = pk_backend_new ();

	/* connect up signals */
	g_signal_connect (engine->priv->backend, "status-changed",
			  G_CALLBACK (pk_engine_status_changed_cb), engine);
	g_signal_connect (engine->priv->backend, "progress-changed",
			  G_CALLBACK (pk_engine_progress_changed_cb), engine);
	g_signal_connect (engine->priv->backend, "package",
			  G_CALLBACK (pk_engine_package_cb), engine);
	g_signal_connect (engine->priv->backend, "update-detail",
			  G_CALLBACK (pk_engine_update_detail_cb), engine);
	g_signal_connect (engine->priv->backend, "error-code",
			  G_CALLBACK (pk_engine_error_code_cb), engine);
	g_signal_connect (engine->priv->backend, "updates-changed",
			  G_CALLBACK (pk_engine_updates_changed_cb), engine);
	g_signal_connect (engine->priv->backend, "repo-signature-required",
			  G_CALLBACK (pk_engine_repo_signature_required_cb), engine);
	g_signal_connect (engine->priv->backend, "require-restart",
			  G_CALLBACK (pk_engine_require_restart_cb), engine);
	g_signal_connect (engine->priv->backend, "message",
			  G_CALLBACK (pk_engine_message_cb), engine);
	g_signal_connect (engine->priv->backend, "finished",
			  G_CALLBACK (pk_engine_finished_cb), engine);
	g_signal_connect (engine->priv->backend, "description",
			  G_CALLBACK (pk_engine_description_cb), engine);
	g_signal_connect (engine->priv->backend, "files",
			  G_CALLBACK (pk_engine_files_cb), engine);
	g_signal_connect (engine->priv->backend, "allow-cancel",
			  G_CALLBACK (pk_engine_allow_cancel_cb), engine);
	g_signal_connect (engine->priv->backend, "change-transaction-data",
			  G_CALLBACK (pk_engine_change_transaction_data_cb), engine);
	g_signal_connect (engine->priv->backend, "repo-detail",
			  G_CALLBACK (pk_engine_repo_detail_cb), engine);

	/* lock database */
	ret = pk_backend_lock (engine->priv->backend);
	if (ret == FALSE) {
		pk_error ("could not lock backend, you need to restart the daemon");
	}

	/* we dont need this, just don't keep creating and destroying it */
	engine->priv->network = pk_network_new ();

	/* create a new backend so we can get the static stuff */
	runner = pk_runner_new ();
	engine->priv->actions = pk_runner_get_actions (runner);
	engine->priv->groups = pk_runner_get_groups (runner);
	engine->priv->filters = pk_runner_get_filters (runner);
	g_object_unref (runner);

	engine->priv->timer = g_timer_new ();

	/* we save a cache of the latest update lists sowe can do cached responses */
	engine->priv->updates_cache = NULL;

	/* we need an auth framework */
	engine->priv->security = pk_security_new ();

	engine->priv->transaction_list = pk_transaction_list_new ();
	g_signal_connect (engine->priv->transaction_list, "changed",
			  G_CALLBACK (pk_engine_transaction_list_changed_cb), engine);

	engine->priv->inhibit = pk_inhibit_new ();
	g_signal_connect (engine->priv->inhibit, "locked",
			  G_CALLBACK (pk_engine_inhibit_locked_cb), engine);

	/* we use a trasaction db to store old transactions and to do rollbacks */
	engine->priv->transaction_db = pk_transaction_db_new ();
	g_signal_connect (engine->priv->transaction_db, "transaction",
			  G_CALLBACK (pk_engine_transaction_cb), engine);
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
	pk_debug ("engine finalise");

	/* compulsory gobjects */
	g_timer_destroy (engine->priv->timer);
	g_object_unref (engine->priv->inhibit);
	g_object_unref (engine->priv->transaction_list);
	g_object_unref (engine->priv->transaction_db);
	g_object_unref (engine->priv->network);
	g_object_unref (engine->priv->security);
	g_object_unref (engine->priv->backend);

	/* optional gobjects */
	if (engine->priv->actions != NULL) {
		g_object_unref (engine->priv->actions);
	}
	if (engine->priv->groups != NULL) {
		g_object_unref (engine->priv->groups);
	}
	if (engine->priv->filters != NULL) {
		g_object_unref (engine->priv->filters);
	}
	if (engine->priv->updates_cache != NULL) {
		g_object_unref (engine->priv->updates_cache);
	}

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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_engine (LibSelfTest *test)
{
	PkEngine *engine;
	PkBackend *backend;

	if (libst_start (test, "PkEngine", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get a backend instance");
	backend = pk_backend_new ();
	if (backend != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/* set the type, as we have no pk-main doing this for us */
	pk_backend_set_name (backend, "dummy");

	/************************************************************/
	libst_title (test, "get an engine instance");
	engine = pk_engine_new ();
	if (engine != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (backend);
	g_object_unref (engine);

	libst_end (test);
}
#endif

