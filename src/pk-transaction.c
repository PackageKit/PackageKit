/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <libgbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-enum.h>
#include <pk-debug.h>
#include <pk-package-list.h>

#include "pk-transaction.h"
#include "pk-transaction-list.h"
#include "pk-transaction-db.h"
#include "pk-marshal.h"
#include "pk-backend.h"
#include "pk-backend-internal.h"
#include "pk-inhibit.h"
#include "pk-cache.h"
#include "pk-notify.h"
#include "pk-security.h"

static void     pk_transaction_class_init	(PkTransactionClass *klass);
static void     pk_transaction_init		(PkTransaction      *transaction);
static void     pk_transaction_finalize		(GObject	    *object);

#define PK_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION, PkTransactionPrivate))
#define PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT	100 /* ms */

struct PkTransactionPrivate
{
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	gboolean		 finished;
	gboolean		 running;
	gboolean		 allow_cancel;
	gboolean		 emit_eula_required;
	gboolean		 emit_signature_required;
	LibGBus			*libgbus;
	PkBackend		*backend;
	PkInhibit		*inhibit;
	PkCache			*cache;
	PkNotify		*notify;
	PkSecurity		*security;

	/* needed for gui coldplugging */
	gchar			*last_package;
	gchar			*dbus_name;
	gchar			*tid;
	PkPackageList		*package_list;
	PkTransactionList	*transaction_list;
	PkTransactionDb		*transaction_db;

	/* cached */
	gboolean		 cached_force;
	gboolean		 cached_allow_deps;
	gboolean		 cached_autoremove;
	gboolean		 cached_enabled;
	gchar			*cached_package_id;
	gchar			**cached_package_ids;
	gchar			*cached_transaction_id;
	gchar			*cached_full_path;
	PkFilterEnum		 cached_filters;
	gchar			*cached_search;
	gchar			*cached_repo_id;
	gchar			*cached_key_id;
	gchar			*cached_parameter;
	gchar			*cached_value;
	PkProvidesEnum		 cached_provides;

	guint			 signal_allow_cancel;
	guint			 signal_description;
	guint			 signal_error_code;
	guint			 signal_files;
	guint			 signal_finished;
	guint			 signal_message;
	guint			 signal_package;
	guint			 signal_progress_changed;
	guint			 signal_repo_detail;
	guint			 signal_repo_signature_required;
	guint			 signal_eula_required;
	guint			 signal_require_restart;
	guint			 signal_status_changed;
	guint			 signal_update_detail;
};

enum {
	PK_TRANSACTION_ALLOW_CANCEL,
	PK_TRANSACTION_CALLER_ACTIVE_CHANGED,
	PK_TRANSACTION_DESCRIPTION,
	PK_TRANSACTION_ERROR_CODE,
	PK_TRANSACTION_FILES,
	PK_TRANSACTION_FINISHED,
	PK_TRANSACTION_MESSAGE,
	PK_TRANSACTION_PACKAGE,
	PK_TRANSACTION_PROGRESS_CHANGED,
	PK_TRANSACTION_REPO_DETAIL,
	PK_TRANSACTION_REPO_SIGNATURE_REQUIRED,
	PK_TRANSACTION_EULA_REQUIRED,
	PK_TRANSACTION_REQUIRE_RESTART,
	PK_TRANSACTION_STATUS_CHANGED,
	PK_TRANSACTION_TRANSACTION,
	PK_TRANSACTION_UPDATE_DETAIL,
	PK_TRANSACTION_LAST_SIGNAL
};

static guint	     signals [PK_TRANSACTION_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkTransaction, pk_transaction, G_TYPE_OBJECT)

/**
 * pk_transaction_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
pk_transaction_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("pk_transaction_error");
	}
	return quark;
}

/**
 * pk_transaction_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_transaction_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_TRANSACTION_ERROR_DENIED, "PermissionDenied"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NOT_RUNNING, "NotRunning"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NO_ROLE, "NoRole"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_CANNOT_CANCEL, "CannotCancel"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NOT_SUPPORTED, "NotSupported"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NO_SUCH_TRANSACTION, "NoSuchTransaction"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NO_SUCH_FILE, "NoSuchFile"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE, "TransactionExistsWithRole"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_REFUSED_BY_POLICY, "RefusedByPolicy"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID, "PackageIdInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_SEARCH_INVALID, "SearchInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_FILTER_INVALID, "FilterInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INPUT_INVALID, "InputInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INVALID_STATE, "InvalidState"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INITIALIZE_FAILED, "InitializeFailed"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_COMMIT_FAILED, "CommitFailed"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INVALID_PROVIDE, "InvalidProvide"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkTransactionError", values);
	}
	return etype;
}

/**
 * pk_transaction_get_runtime:
 *
 * Returns time running in ms
 */
guint
pk_transaction_get_runtime (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), 0);
	g_return_val_if_fail (transaction->priv->tid != NULL, 0);
	return pk_backend_get_runtime (transaction->priv->backend);
}

/**
 * pk_transaction_set_dbus_name:
 */
gboolean
pk_transaction_set_dbus_name (PkTransaction *transaction, const gchar *dbus_name)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);
	g_return_val_if_fail (dbus_name != NULL, FALSE);

	if (transaction->priv->dbus_name != NULL) {
		pk_warning ("you can't assign more than once!");
		return FALSE;
	}
	transaction->priv->dbus_name = g_strdup (dbus_name);
	pk_debug ("assigning %s to %p", dbus_name, transaction);
	libgbus_assign (transaction->priv->libgbus, LIBGBUS_SYSTEM, dbus_name);
	return TRUE;
}

/**
 * pk_transaction_set_role:
 * We should only set this when we are creating a manual cache
 **/
static gboolean
pk_transaction_set_role (PkTransaction *transaction, PkRoleEnum role)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	/* save this */
	transaction->priv->role = role;
	return TRUE;
}

/**
 * pk_transaction_get_package_list:
 **/
PkPackageList *
pk_transaction_get_package_list (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (transaction->priv->tid != NULL, NULL);

	return transaction->priv->package_list;
}

/**
 * pk_transaction_get_text:
 **/
const gchar *
pk_transaction_get_text (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (transaction->priv->tid != NULL, NULL);

	if (transaction->priv->cached_package_id != NULL) {
		return transaction->priv->cached_package_id;
	} else if (transaction->priv->cached_package_ids != NULL) {
		return transaction->priv->cached_package_ids[0];
	} else if (transaction->priv->cached_search != NULL) {
		return transaction->priv->cached_search;
	}

	return NULL;
}

/**
 * pk_transaction_finish_invalidate_caches:
 **/
static gboolean
pk_transaction_finish_invalidate_caches (PkTransaction *transaction)
{
	const gchar *c_tid;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	c_tid = pk_backend_get_current_tid (transaction->priv->backend);
	if (c_tid == NULL) {
		pk_warning ("could not get current tid from backend");
		return FALSE;
	}

	pk_debug ("invalidating caches");

	/* copy this into the cache if we are getting updates */
	if (transaction->priv->role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_cache_set_updates (transaction->priv->cache, pk_transaction_get_package_list (transaction));
	}

	/* we unref the update cache if it exists */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		pk_cache_invalidate (transaction->priv->cache);
	}

	/* this has to be done as different repos might have different updates */
	if (transaction->priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		pk_cache_invalidate (transaction->priv->cache);
	}

	/* could the update list have changed? */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_SET_DATA ||
	    transaction->priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		/* this needs to be done after a small delay */
		pk_notify_wait_updates_changed (transaction->priv->notify,
						PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT);
	}
	return TRUE;
}

/**
 * pk_transaction_allow_cancel_cb:
 **/
static void
pk_transaction_allow_cancel_cb (PkBackend *backend, gboolean allow_cancel, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("AllowCancel now %i", allow_cancel);
	transaction->priv->allow_cancel = allow_cancel;

	pk_debug ("emitting allow-interrpt %i", allow_cancel);
	g_signal_emit (transaction, signals [PK_TRANSACTION_ALLOW_CANCEL], 0, allow_cancel);
}

/**
 * pk_transaction_caller_active_changed_cb:
 **/
static void
pk_transaction_caller_active_changed_cb (LibGBus *libgbus, gboolean is_active, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	if (is_active == FALSE) {
		pk_debug ("client disconnected....");
		g_signal_emit (transaction, signals [PK_TRANSACTION_CALLER_ACTIVE_CHANGED], 0, FALSE);
	}
}

/**
 * pk_transaction_description_cb:
 **/
static void
pk_transaction_description_cb (PkBackend *backend, const gchar *package_id, const gchar *license, PkGroupEnum group,
			       const gchar *detail, const gchar *url,
			       guint64 size, PkTransaction *transaction)
{
	const gchar *group_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	group_text = pk_group_enum_to_text (group);

	pk_debug ("emitting description %s, %s, %s, %s, %s, %ld",
		  package_id, license, group_text, detail, url, (long int) size);
	g_signal_emit (transaction, signals [PK_TRANSACTION_DESCRIPTION], 0,
		       package_id, license, group_text, detail, url, size);
}

/**
 * pk_transaction_error_code_cb:
 **/
static void
pk_transaction_error_code_cb (PkBackend *backend, PkErrorCodeEnum code,
			      const gchar *details, PkTransaction *transaction)
{
	const gchar *code_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	if (code == PK_ERROR_ENUM_UNKNOWN) {
		pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_DAEMON,
				    "backend emitted 'unknown error' rather than a specific error "
				    "- this is a backend problem and should be fixed!");
	}

	code_text = pk_error_enum_to_text (code);
	pk_debug ("emitting error-code %s, '%s'", code_text, details);
	g_signal_emit (transaction, signals [PK_TRANSACTION_ERROR_CODE], 0, code_text, details);
}

/**
 * pk_transaction_files_cb:
 **/
static void
pk_transaction_files_cb (PkBackend *backend, const gchar *package_id,
			 const gchar *filelist, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("emitting files %s, %s", package_id, filelist);
	g_signal_emit (transaction, signals [PK_TRANSACTION_FILES], 0, package_id, filelist);
}

/**
 * pk_transaction_finished_cb:
 **/
static void
pk_transaction_finished_cb (PkBackend *backend, PkExitEnum exit, PkTransaction *transaction)
{
	const gchar *exit_text;
	guint time;
	gchar *packages;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		pk_warning ("Already finished");
		return;
	}

	/* we should get no more from the backend with this tid */
	transaction->priv->finished = TRUE;

	/* mark not running */
	transaction->priv->running = FALSE;

	/* if we did ::repo-signature-required or ::eula-required, change the error code */
	if (transaction->priv->emit_signature_required) {
		exit = PK_EXIT_ENUM_KEY_REQUIRED;
	} else if (transaction->priv->emit_eula_required) {
		exit = PK_EXIT_ENUM_EULA_REQUIRED;
	}

	/* invalidate some caches if we succeeded*/
	if (exit == PK_EXIT_ENUM_SUCCESS) {
		pk_transaction_finish_invalidate_caches (transaction);
	}

	/* find the length of time we have been running */
	time = pk_transaction_get_runtime (transaction);
	pk_debug ("backend was running for %i ms", time);

	/* add to the database if we are going to log it */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    transaction->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		packages = pk_package_list_get_string (pk_transaction_get_package_list (transaction));
		if (pk_strzero (packages) == FALSE) {
			pk_transaction_db_set_data (transaction->priv->transaction_db, transaction->priv->tid, packages);
		}
		g_free (packages);
	}

	/* the repo list will have changed */
	if (transaction->priv->role == PK_ROLE_ENUM_SERVICE_PACK ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		pk_notify_repo_list_changed (transaction->priv->notify);
	}

	/* only reset the time if we succeeded */
	if (exit == PK_EXIT_ENUM_SUCCESS) {
		pk_transaction_db_action_time_reset (transaction->priv->transaction_db, transaction->priv->role);
	}

	/* did we finish okay? */
	if (exit == PK_EXIT_ENUM_SUCCESS) {
		pk_transaction_db_set_finished (transaction->priv->transaction_db, transaction->priv->tid, TRUE, time);
	} else {
		pk_transaction_db_set_finished (transaction->priv->transaction_db, transaction->priv->tid, FALSE, time);
	}

	/* disconnect these straight away, as the PkTransaction object takes time to timeout */
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_allow_cancel);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_description);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_error_code);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_files);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_finished);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_message);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_package);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_progress_changed);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_repo_detail);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_repo_signature_required);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_eula_required);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_require_restart);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_status_changed);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_update_detail);

	/* we emit last, as other backends will be running very soon after us, and we don't want to be notified */
	exit_text = pk_exit_enum_to_text (exit);
	pk_debug ("emitting finished '%s', %i", exit_text, time);
	g_signal_emit (transaction, signals [PK_TRANSACTION_FINISHED], 0, exit_text, time);
}

/**
 * pk_transaction_message_cb:
 **/
static void
pk_transaction_message_cb (PkBackend *backend, PkMessageEnum message, const gchar *details, PkTransaction *transaction)
{
	const gchar *message_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

#ifndef PK_IS_DEVELOPER
	if (message == PK_MESSAGE_ENUM_DAEMON) {
		pk_warning ("ignoring message: %s", details);
		return;
	}
#endif

	message_text = pk_message_enum_to_text (message);
	pk_debug ("emitting message %s, '%s'", message_text, details);
	g_signal_emit (transaction, signals [PK_TRANSACTION_MESSAGE], 0, message_text, details);
}

/**
 * pk_transaction_package_cb:
 **/
static void
pk_transaction_package_cb (PkBackend *backend, PkInfoEnum info, const gchar *package_id,
			   const gchar *summary, PkTransaction *transaction)
{
	const gchar *info_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		pk_warning ("Already finished");
		return;
	}

	/* check the backend is doing the right thing */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		if (info == PK_INFO_ENUM_INSTALLED) {
			pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_DAEMON,
					    "backend emitted 'installed' rather than 'installing' "
					    "- you need to do the package *before* you do the action");
			return;
		}
	}

	/* add to package cache even if we already got a result */
	info_text = pk_info_enum_to_text (info);
	pk_package_list_add (transaction->priv->package_list, info, package_id, summary);
	pk_debug ("caching package info=%s %s, %s", info_text, package_id, summary);

	/* emit */
	pk_debug ("emitting package info=%s %s, %s", info_text, package_id, summary);
	g_signal_emit (transaction, signals [PK_TRANSACTION_PACKAGE], 0, info_text, package_id, summary);
}

/**
 * pk_transaction_progress_changed_cb:
 **/
static void
pk_transaction_progress_changed_cb (PkBackend *backend, guint percentage, guint subpercentage,
				    guint elapsed, guint remaining, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("emitting percentage-changed %i, %i, %i, %i",
		  percentage, subpercentage, elapsed, remaining);
	g_signal_emit (transaction, signals [PK_TRANSACTION_PROGRESS_CHANGED], 0,
		       percentage, subpercentage, elapsed, remaining);
}

/**
 * pk_transaction_repo_detail_cb:
 **/
static void
pk_transaction_repo_detail_cb (PkBackend *backend, const gchar *repo_id,
			       const gchar *description, gboolean enabled, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("emitting repo-detail %s, %s, %i", repo_id, description, enabled);
	g_signal_emit (transaction, signals [PK_TRANSACTION_REPO_DETAIL], 0, repo_id, description, enabled);
}

/**
 * pk_transaction_repo_signature_required_cb:
 **/
static void
pk_transaction_repo_signature_required_cb (PkBackend *backend, const gchar *package_id,
					   const gchar *repository_name, const gchar *key_url,
					   const gchar *key_userid, const gchar *key_id,
					   const gchar *key_fingerprint, const gchar *key_timestamp,
					   PkSigTypeEnum type, PkTransaction *transaction)
{
	const gchar *type_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	type_text = pk_sig_type_enum_to_text (type);

	pk_debug ("emitting repo_signature_required %s, %s, %s, %s, %s, %s, %s, %s",
		  package_id, repository_name, key_url, key_userid, key_id,
		  key_fingerprint, key_timestamp, type_text);
	g_signal_emit (transaction, signals [PK_TRANSACTION_REPO_SIGNATURE_REQUIRED], 0,
		       package_id, repository_name, key_url, key_userid, key_id,
		       key_fingerprint, key_timestamp, type_text);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_signature_required = TRUE;
}

/**
 * pk_transaction_eula_required_cb:
 **/
static void
pk_transaction_eula_required_cb (PkBackend *backend, const gchar *eula_id, const gchar *package_id,
				 const gchar *vendor_name, const gchar *license_agreement,
				 PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("emitting eula-required %s, %s, %s, %s",
		  eula_id, package_id, vendor_name, license_agreement);
	g_signal_emit (transaction, signals [PK_TRANSACTION_EULA_REQUIRED], 0,
		       eula_id, package_id, vendor_name, license_agreement);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_eula_required = TRUE;
}

/**
 * pk_transaction_require_restart_cb:
 **/
static void
pk_transaction_require_restart_cb (PkBackend *backend, PkRestartEnum restart, const gchar *details, PkTransaction *transaction)
{
	const gchar *restart_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	restart_text = pk_restart_enum_to_text (restart);
	pk_debug ("emitting require-restart %s, '%s'", restart_text, details);
	g_signal_emit (transaction, signals [PK_TRANSACTION_REQUIRE_RESTART], 0, restart_text, details);
}

/**
 * pk_transaction_status_changed_cb:
 **/
static void
pk_transaction_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkTransaction *transaction)
{
	const gchar *status_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		pk_warning ("Already finished, so can't proxy status %s", pk_status_enum_to_text (status));
		return;
	}

	transaction->priv->status = status;
	status_text = pk_status_enum_to_text (status);

	pk_debug ("emitting status-changed '%s'", status_text);
	g_signal_emit (transaction, signals [PK_TRANSACTION_STATUS_CHANGED], 0, status_text);
}

/**
 * pk_transaction_transaction_cb:
 **/
static void
pk_transaction_transaction_cb (PkTransactionDb *tdb, const gchar *old_tid, const gchar *timespec,
			       gboolean succeeded, PkRoleEnum role, guint duration,
			       const gchar *data, PkTransaction *transaction)
{
	const gchar *role_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	role_text = pk_role_enum_to_text (role);
	pk_debug ("emitting transaction %s, %s, %i, %s, %i, %s", old_tid, timespec, succeeded, role_text, duration, data);
	g_signal_emit (transaction, signals [PK_TRANSACTION_TRANSACTION], 0, old_tid, timespec, succeeded, role_text, duration, data);
}

/**
 * pk_transaction_update_detail_cb:
 **/
static void
pk_transaction_update_detail_cb (PkBackend *backend, const gchar *package_id,
				 const gchar *updates, const gchar *obsoletes,
				 const gchar *vendor_url, const gchar *bugzilla_url,
				 const gchar *cve_url, PkRestartEnum restart,
				 const gchar *update_text, PkTransaction *transaction)
{
	const gchar *restart_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	restart_text = pk_restart_enum_to_text (restart);
	pk_debug ("emitting package value=%s, %s, %s, %s, %s, %s, %s, %s",
		  package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart_text, update_text);
	g_signal_emit (transaction, signals [PK_TRANSACTION_UPDATE_DETAIL], 0,
		       package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart_text, update_text);
}


/**
 * pk_transaction_set_running:
 */
G_GNUC_WARN_UNUSED_RESULT static gboolean
pk_transaction_set_running (PkTransaction *transaction)
{
	PkBackendDesc *desc;
	PkTransactionPrivate *priv = PK_TRANSACTION_GET_PRIVATE (transaction);
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	/* prepare for use; the transaction list ensures this is safe */
	pk_backend_reset (transaction->priv->backend);

	/* assign */
	pk_backend_set_current_tid (priv->backend, priv->tid);

	/* set the role */
	pk_backend_set_role (priv->backend, priv->role);

	/* we are no longer waiting, we are setting up */
	pk_backend_set_status (priv->backend, PK_STATUS_ENUM_SETUP);

	/* connect up the signals */
	transaction->priv->signal_allow_cancel =
		g_signal_connect (transaction->priv->backend, "allow-cancel",
				  G_CALLBACK (pk_transaction_allow_cancel_cb), transaction);
	transaction->priv->signal_description =
		g_signal_connect (transaction->priv->backend, "description",
				  G_CALLBACK (pk_transaction_description_cb), transaction);
	transaction->priv->signal_error_code =
		g_signal_connect (transaction->priv->backend, "error-code",
				  G_CALLBACK (pk_transaction_error_code_cb), transaction);
	transaction->priv->signal_files =
		g_signal_connect (transaction->priv->backend, "files",
				  G_CALLBACK (pk_transaction_files_cb), transaction);
	transaction->priv->signal_finished =
		g_signal_connect (transaction->priv->backend, "finished",
				  G_CALLBACK (pk_transaction_finished_cb), transaction);
	transaction->priv->signal_message =
		g_signal_connect (transaction->priv->backend, "message",
				  G_CALLBACK (pk_transaction_message_cb), transaction);
	transaction->priv->signal_package =
		g_signal_connect (transaction->priv->backend, "package",
				  G_CALLBACK (pk_transaction_package_cb), transaction);
	transaction->priv->signal_progress_changed =
		g_signal_connect (transaction->priv->backend, "progress-changed",
				  G_CALLBACK (pk_transaction_progress_changed_cb), transaction);
	transaction->priv->signal_repo_detail =
		g_signal_connect (transaction->priv->backend, "repo-detail",
				  G_CALLBACK (pk_transaction_repo_detail_cb), transaction);
	transaction->priv->signal_repo_signature_required =
		g_signal_connect (transaction->priv->backend, "repo-signature-required",
				  G_CALLBACK (pk_transaction_repo_signature_required_cb), transaction);
	transaction->priv->signal_eula_required =
		g_signal_connect (transaction->priv->backend, "eula-required",
				  G_CALLBACK (pk_transaction_eula_required_cb), transaction);
	transaction->priv->signal_require_restart =
		g_signal_connect (transaction->priv->backend, "require-restart",
				  G_CALLBACK (pk_transaction_require_restart_cb), transaction);
	transaction->priv->signal_status_changed =
		g_signal_connect (transaction->priv->backend, "status-changed",
				  G_CALLBACK (pk_transaction_status_changed_cb), transaction);
	transaction->priv->signal_update_detail =
		g_signal_connect (transaction->priv->backend, "update-detail",
				  G_CALLBACK (pk_transaction_update_detail_cb), transaction);

	/* mark running */
	transaction->priv->running = TRUE;

	/* lets reduce pointer dereferences... */
	desc = priv->backend->desc;

	/* do the correct action with the cached parameters */
	if (priv->role == PK_ROLE_ENUM_GET_DEPENDS) {
		desc->get_depends (priv->backend, priv->cached_filters, priv->cached_package_id, priv->cached_force);
	} else if (priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		desc->get_update_detail (priv->backend, priv->cached_package_id);
	} else if (priv->role == PK_ROLE_ENUM_RESOLVE) {
		desc->resolve (priv->backend, priv->cached_filters, priv->cached_package_id);
	} else if (priv->role == PK_ROLE_ENUM_ROLLBACK) {
		desc->rollback (priv->backend, priv->cached_transaction_id);
	} else if (priv->role == PK_ROLE_ENUM_GET_DESCRIPTION) {
		desc->get_description (priv->backend, priv->cached_package_id);
	} else if (priv->role == PK_ROLE_ENUM_GET_FILES) {
		desc->get_files (priv->backend, priv->cached_package_id);
	} else if (priv->role == PK_ROLE_ENUM_GET_REQUIRES) {
		desc->get_requires (priv->backend, priv->cached_filters, priv->cached_package_id, priv->cached_force);
	} else if (priv->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		desc->what_provides (priv->backend, priv->cached_filters, priv->cached_provides, priv->cached_search);
	} else if (priv->role == PK_ROLE_ENUM_GET_UPDATES) {
		desc->get_updates (priv->backend, priv->cached_filters);
	} else if (priv->role == PK_ROLE_ENUM_GET_PACKAGES) {
		desc->get_packages (priv->backend, priv->cached_filters);
	} else if (priv->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		desc->search_details (priv->backend, priv->cached_filters, priv->cached_search);
	} else if (priv->role == PK_ROLE_ENUM_SEARCH_FILE) {
		desc->search_file (priv->backend,priv->cached_filters,priv->cached_search);
	} else if (priv->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		desc->search_group (priv->backend, priv->cached_filters, priv->cached_search);
	} else if (priv->role == PK_ROLE_ENUM_SEARCH_NAME) {
		desc->search_name (priv->backend,priv->cached_filters,priv->cached_search);
	} else if (priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		desc->install_package (priv->backend, priv->cached_package_id);
	} else if (priv->role == PK_ROLE_ENUM_INSTALL_FILE) {
		desc->install_file (priv->backend, priv->cached_full_path);
	} else if (priv->role == PK_ROLE_ENUM_INSTALL_SIGNATURE) {
		desc->install_signature (priv->backend, PK_SIGTYPE_ENUM_GPG, priv->cached_key_id, priv->cached_package_id);
	} else if (priv->role == PK_ROLE_ENUM_SERVICE_PACK) {
		desc->service_pack (priv->backend, priv->cached_full_path, priv->cached_enabled);
	} else if (priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		desc->refresh_cache (priv->backend,  priv->cached_force);
	} else if (priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		desc->remove_package (priv->backend, priv->cached_package_id, priv->cached_allow_deps, priv->cached_autoremove);
	} else if (priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		desc->update_packages (priv->backend, priv->cached_package_ids);
	} else if (priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		desc->update_system (priv->backend);
	} else if (priv->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		desc->get_repo_list (priv->backend, priv->cached_filters);
	} else if (priv->role == PK_ROLE_ENUM_REPO_ENABLE) {
		desc->repo_enable (priv->backend, priv->cached_repo_id, priv->cached_enabled);
	} else if (priv->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		desc->repo_set_data (priv->backend, priv->cached_repo_id, priv->cached_parameter, priv->cached_value);
	} else {
		pk_error ("failed to run as role not assigned");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_run:
 */
gboolean
pk_transaction_run (PkTransaction *transaction)
{
	gboolean ret;
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	ret = pk_transaction_set_running (transaction);
	if (ret) {
		/* we start inhibited, it's up to the backed to
		 * release early if a shutdown is possible */
		pk_inhibit_add (transaction->priv->inhibit, transaction);
	}
	return ret;
}

/**
 * pk_transaction_get_tid:
 */
const gchar *
pk_transaction_get_tid (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (transaction->priv->tid != NULL, NULL);

	return transaction->priv->tid;
}

/**
 * pk_transaction_set_tid:
 */
gboolean
pk_transaction_set_tid (PkTransaction *transaction, const gchar *tid)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->tid == NULL, FALSE);

	if (transaction->priv->tid != NULL) {
		pk_warning ("changing a tid -- why?");
	}
	g_free (transaction->priv->tid);
	transaction->priv->tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_transaction_commit:
 **/
G_GNUC_WARN_UNUSED_RESULT static gboolean
pk_transaction_commit (PkTransaction *transaction)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	/* commit, so it appears in the JobList */
	ret = pk_transaction_list_commit (transaction->priv->transaction_list, transaction);
	if (!ret) {
		pk_warning ("failed to commit (job not run?)");
		return FALSE;
	}

	/* only save into the database for useful stuff */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		/* add to database */
		pk_transaction_db_add (transaction->priv->transaction_db, transaction->priv->tid);

		/* save role in the database */
		pk_transaction_db_set_role (transaction->priv->transaction_db, transaction->priv->tid, transaction->priv->role);
	}
	return TRUE;
}

/**
 * pk_transaction_search_check:
 **/
static gboolean
pk_transaction_search_check (const gchar *search, GError **error)
{
	guint size;
	gboolean ret;

	/* ITS4: ignore, not used for allocation, and checked */
	size = strlen (search);

	if (search == NULL) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Search is null. This isn't supposed to happen...");
		return FALSE;
	}
	if (size == 0) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Search string zero length");
		return FALSE;
	}
	if (size < 2) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "The search string length is too small");
		return FALSE;
	}
	if (size > 1024) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "The search string length is too large");
		return FALSE;
	}
	if (strstr (search, "*") != NULL) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing '*'");
		return FALSE;
	}
	if (strstr (search, "?") != NULL) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing '?'");
		return FALSE;
	}
	ret = pk_strvalidate (search);
	if (!ret) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid search term");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_filter_check:
 **/
gboolean
pk_transaction_filter_check (const gchar *filter, GError **error)
{
	gchar **sections;
	guint i;
	guint length;
	gboolean ret = FALSE;

	g_return_val_if_fail (error != NULL, FALSE);

	/* is zero? */
	if (pk_strzero (filter)) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "filter zero length");
		return FALSE;
	}

	/* check for invalid input */
	ret = pk_strvalidate (filter);
	if (!ret) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid filter term: %s", filter);
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (filter, ";", 0);
	length = g_strv_length (sections);
	for (i=0; i<length; i++) {
		/* only one wrong part is enough to fail the filter */
		if (pk_strzero (sections[i])) {
			*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
					     "Single empty section of filter: %s", filter);
			goto out;
		}
		if (pk_filter_enum_from_text (sections[i]) == PK_FILTER_ENUM_UNKNOWN) {
			*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
					     "Unknown filter part: %s", sections[i]);
			goto out;
		}
	}
	ret = TRUE;
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_transaction_action_is_allowed:
 *
 * Only valid from an async caller, which is fine, as we won't prompt the user
 * when not async.
 **/
static gboolean
pk_transaction_action_is_allowed (PkTransaction *transaction, const gchar *dbus_sender,
				  PkRoleEnum role, GError **error)
{
	gboolean ret;
	gchar *error_detail;

	/* use security model to get auth */
	ret = pk_security_action_is_allowed (transaction->priv->security, dbus_sender, role, &error_detail);
	if (!ret) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_REFUSED_BY_POLICY, "%s", error_detail);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_priv_get_role:
 *
 * Only valid from an async caller, which is fine, as we won't prompt the user
 * when not async.
 **/
PkRoleEnum
pk_transaction_priv_get_role (PkTransaction *transaction)
{
	g_return_val_if_fail (transaction != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	return transaction->priv->role;
}

/**
 * pk_transaction_cancel:
 **/
gboolean
pk_transaction_cancel (PkTransaction *transaction, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("Cancel method called");
	/* check to see if we are trying to cancel a non-running task */
	if (!transaction->priv->running) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_RUNNING,
			     "cancelling a non-running transaction");
		return FALSE;
	}

	/* not implemented yet */
	if (transaction->priv->backend->desc->cancel == NULL) {
		pk_debug ("Not implemented yet: Cancel");
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		return FALSE;
	}

	/* check to see if we have an action */
	if (transaction->priv->role == PK_ROLE_ENUM_UNKNOWN) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_ROLE, "No role");
		return FALSE;
	}

	/* check if it's safe to kill */
	if (transaction->priv->allow_cancel == FALSE) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_CANNOT_CANCEL,
			     "Tried to cancel a transaction that is not safe to kill");
		return FALSE;
	}

	/* set the state, as cancelling might take a few seconds */
	pk_backend_set_status (transaction->priv->backend, PK_STATUS_ENUM_CANCEL);

	/* we don't want to cancel twice */
	pk_backend_set_allow_cancel (transaction->priv->backend, FALSE);

	/* we need ::finished to not return success or failed */
	pk_backend_set_exit_code (transaction->priv->backend, PK_EXIT_ENUM_CANCELLED);

	/* actually run the method */
	transaction->priv->backend->desc->cancel (transaction->priv->backend);
	return TRUE;
}

/**
 * pk_transaction_get_allow_cancel:
 **/
gboolean
pk_transaction_get_allow_cancel (PkTransaction *transaction, gboolean *allow_cancel, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("GetAllowCancel method called");
	*allow_cancel = transaction->priv->allow_cancel;
	return TRUE;
}

/**
 * pk_transaction_get_depends:
 **/
void
pk_transaction_get_depends (PkTransaction *transaction, const gchar *filter, const gchar *package_id,
			    gboolean recursive, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetDepends method called: %s, %i", package_id, recursive);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_depends == NULL) {
		pk_debug ("Not implemented yet: GetDepends");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->cached_force = recursive;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DEPENDS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_description:
 **/
void
pk_transaction_get_description (PkTransaction *transaction, const gchar *package_id,
				DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetDescription method called: %s", package_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_description == NULL) {
		pk_debug ("Not implemented yet: GetDescription");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DESCRIPTION);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_files:
 **/
void
pk_transaction_get_files (PkTransaction *transaction, const gchar *package_id,
			  DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetFiles method called: %s", package_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_files == NULL) {
		pk_debug ("Not implemented yet: GetFiles");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_FILES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_packages:
 **/
void
pk_transaction_get_packages (PkTransaction *transaction, const gchar *filter, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetPackages method called: %s", filter);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_packages == NULL) {
		pk_debug ("Not implemented yet: GetPackages");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_old_transactions:
 **/
gboolean
pk_transaction_get_old_transactions (PkTransaction *transaction, guint number, GError **error)
{
	const gchar *exit_text;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("GetOldTransactions method called");

	pk_transaction_db_get_list (transaction->priv->transaction_db, number);

	exit_text = pk_exit_enum_to_text (PK_EXIT_ENUM_SUCCESS);
	pk_debug ("emitting finished transaction '%s', %i", exit_text, 0);
	g_signal_emit (transaction, signals [PK_TRANSACTION_FINISHED], 0, exit_text, 0);

	return TRUE;
}

/**
 * pk_transaction_get_package_last:
 **/
gboolean
pk_transaction_get_package_last (PkTransaction *transaction, gchar **package_id, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("GetPackageLast method called");

	if (transaction->priv->last_package == NULL) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_STATE,
			     "No package data available");
		return FALSE;
	}
	*package_id = g_strdup (transaction->priv->last_package);
	return TRUE;
}

/**
 * pk_transaction_get_progress:
 **/
gboolean
pk_transaction_get_progress (PkTransaction *transaction,
			     guint *percentage, guint *subpercentage,
			     guint *elapsed, guint *remaining, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("GetProgress method called");

	ret = pk_backend_get_progress (transaction->priv->backend, percentage, subpercentage, elapsed, remaining);
	if (!ret) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_STATE,
			     "No progress data available");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_get_repo_list:
 **/
void
pk_transaction_get_repo_list (PkTransaction *transaction, const gchar *filter, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetRepoList method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_repo_list == NULL) {
		pk_debug ("Not implemented yet: GetRepoList");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REPO_LIST);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_requires:
 **/
void
pk_transaction_get_requires (PkTransaction *transaction, const gchar *filter, const gchar *package_id,
			gboolean recursive, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetRequires method called: %s, %i", package_id, recursive);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_requires == NULL) {
		pk_debug ("Not implemented yet: GetRequires");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->cached_force = recursive;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REQUIRES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_role:
 **/
gboolean
pk_transaction_get_role (PkTransaction *transaction,
			 const gchar **role, const gchar **package_id, GError **error)
{
	const gchar *text;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	pk_debug ("GetRole method called");

	/* we might not have this set yet */
	if (transaction->priv->tid == NULL) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_SUCH_TRANSACTION, "Role not set");
		return FALSE;
	}

	text = pk_transaction_get_text (transaction);
	*role = g_strdup (pk_role_enum_to_text (transaction->priv->role));
	*package_id = g_strdup (text);
	return TRUE;
}

/**
 * pk_transaction_get_status:
 **/
gboolean
pk_transaction_get_status (PkTransaction *transaction,
			   const gchar **status, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("GetStatus method called");

	*status = g_strdup (pk_status_enum_to_text (transaction->priv->status));
	return TRUE;
}

/**
 * pk_transaction_get_update_detail:
 **/
void
pk_transaction_get_update_detail (PkTransaction *transaction, const gchar *package_id,
			     DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetUpdateDetail method called: %s", package_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_update_detail == NULL) {
		pk_debug ("Not implemented yet: GetUpdateDetail");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATE_DETAIL);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_updates:
 **/
void
pk_transaction_get_updates (PkTransaction *transaction, const gchar *filter, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	PkPackageList *updates_cache;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("GetUpdates method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_updates == NULL) {
		pk_debug ("Not implemented yet: GetUpdates");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* try and reuse cache */
	updates_cache = pk_cache_get_updates (transaction->priv->cache);
	if (updates_cache != NULL) {
		PkPackageItem *package;
		const gchar *info_text;
		const gchar *exit_text;
		guint i;
		guint length;

		length = pk_package_list_get_size (updates_cache);
		pk_debug ("we have cached data (%i) we should use!", length);

		/* emulate the backend */
		pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATES);
		for (i=0; i<length; i++) {
			package = pk_package_list_get_item (updates_cache, i);
			info_text = pk_info_enum_to_text (package->info);
			g_signal_emit (transaction, signals [PK_TRANSACTION_PACKAGE], 0,
				       info_text, package->package_id, package->summary);
		}

		/* we are done */
		exit_text = pk_exit_enum_to_text (PK_EXIT_ENUM_SUCCESS);
		pk_debug ("emitting finished '%s'", exit_text);
		g_signal_emit (transaction, signals [PK_TRANSACTION_FINISHED], 0, exit_text, 0);

		dbus_g_method_return (context);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_install_file:
 **/
void
pk_transaction_install_file (PkTransaction *transaction, const gchar *full_path,
			     DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("InstallFile method called: %s", full_path);

	/* not implemented yet */
	if (transaction->priv->backend->desc->install_file == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check file exists */
	ret = g_file_test (full_path, G_FILE_TEST_EXISTS);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "No such file '%s'", full_path);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_INSTALL_FILE, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_full_path = g_strdup (full_path);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_FILE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_install_package:
 **/
void
pk_transaction_install_package (PkTransaction *transaction, const gchar *package_id,
				DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("method called: %s", package_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->install_package == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_INSTALL_PACKAGE, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_PACKAGE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_install_signature:
 **/
void
pk_transaction_install_signature (PkTransaction *transaction, const gchar *sig_type,
				  const gchar *key_id, const gchar *package_id,
				  DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("method called: %s, %s", key_id, package_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->install_signature == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (key_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_INSTALL_SIGNATURE, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->cached_key_id = g_strdup (key_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_SIGNATURE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_is_caller_active:
 **/
gboolean
pk_transaction_is_caller_active (PkTransaction *transaction, gboolean *is_active, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	pk_debug ("is caller active");

	*is_active = libgbus_is_connected (transaction->priv->libgbus);
	return TRUE;
}

/**
 * pk_transaction_refresh_cache:
 **/
void
pk_transaction_refresh_cache (PkTransaction *transaction, gboolean force, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("RefreshCache method called: %i", force);

	/* not implemented yet */
	if (transaction->priv->backend->desc->refresh_cache == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_REFRESH_CACHE, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* we unref the update cache if it exists */
	pk_cache_invalidate (transaction->priv->cache);

	/* save so we can run later */
	transaction->priv->cached_force = force;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REFRESH_CACHE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_remove_package:
 **/
void
pk_transaction_remove_package (PkTransaction *transaction, const gchar *package_id,
			       gboolean allow_deps, gboolean autoremove,
			       DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("RemovePackage method called: %s, %i, %i", package_id, allow_deps, autoremove);

	/* not implemented yet */
	if (transaction->priv->backend->desc->remove_package == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_REMOVE_PACKAGE, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_allow_deps = allow_deps;
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REMOVE_PACKAGE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_repo_enable:
 **/
void
pk_transaction_repo_enable (PkTransaction *transaction, const gchar *repo_id, gboolean enabled,
			    DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("RepoEnable method called: %s, %i", repo_id, enabled);

	/* not implemented yet */
	if (transaction->priv->backend->desc->repo_enable == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (repo_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_REPO_ENABLE, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}


	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_enabled = enabled;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_ENABLE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_repo_set_data:
 **/
void
pk_transaction_repo_set_data (PkTransaction *transaction, const gchar *repo_id,
			      const gchar *parameter, const gchar *value,
		              DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("RepoSetData method called: %s, %s, %s", repo_id, parameter, value);

	/* not implemented yet */
	if (transaction->priv->backend->desc->repo_set_data == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (repo_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_REPO_SET_DATA, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_parameter = g_strdup (parameter);
	transaction->priv->cached_value = g_strdup (value);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_SET_DATA);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_resolve:
 **/
void
pk_transaction_resolve (PkTransaction *transaction, const gchar *filter,
			const gchar *package, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("Resolve method called: %s, %s", filter, package);

	/* not implemented yet */
	if (transaction->priv->backend->desc->resolve == NULL) {
		pk_debug ("Not implemented yet: Resolve");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (package);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package);
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_RESOLVE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_rollback:
 **/
void
pk_transaction_rollback (PkTransaction *transaction, const gchar *transaction_id,
			 DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("Rollback method called: %s", transaction_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->rollback == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (transaction_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_ROLLBACK, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_transaction_id = g_strdup (transaction_id);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_ROLLBACK);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_search_details:
 **/
void
pk_transaction_search_details (PkTransaction *transaction, const gchar *filter,
			       const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("SearchDetails method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_details == NULL) {
		pk_debug ("Not implemented yet: SearchDetails");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_DETAILS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_search_file:
 **/
void
pk_transaction_search_file (PkTransaction *transaction, const gchar *filter,
		   	    const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("SearchFile method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_file == NULL) {
		pk_debug ("Not implemented yet: SearchFile");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_FILE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_search_group:
 **/
void
pk_transaction_search_group (PkTransaction *transaction, const gchar *filter,
			     const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("SearchGroup method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_group == NULL) {
		pk_debug ("Not implemented yet: SearchGroup");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_GROUP);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_search_name:
 **/
void
pk_transaction_search_name (PkTransaction *transaction, const gchar *filter,
		  	    const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("SearchName method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_name == NULL) {
		pk_debug ("Not implemented yet: SearchName");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_NAME);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_service_pack:
 */
gboolean
pk_transaction_service_pack (PkTransaction *transaction, const gchar *location, gboolean enabled)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	/* not implemented yet */
	if (transaction->priv->backend->desc->service_pack == NULL) {
		pk_debug ("Not implemented yet: ServicePack");
		return FALSE;
	}
	/* save so we can run later */
	transaction->priv->cached_enabled = enabled;
	transaction->priv->cached_full_path = g_strdup (location);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SERVICE_PACK);
	return TRUE;
}

/**
 * pk_transaction_update_packages:
 **/
void
pk_transaction_update_packages (PkTransaction *transaction, gchar **package_ids, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;
	gchar *package_id_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("UpdatePackages method called: %s", package_ids[0]);

	/* not implemented yet */
	if (transaction->priv->backend->desc->update_packages == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check package_id */
	ret = pk_package_ids_check (package_ids);
	if (ret == FALSE) {
		package_id_temp = pk_package_ids_to_text (package_ids, ", ");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_id_temp);
		g_free (package_id_temp);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_UPDATE_PACKAGES, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_update_system:
 **/
void
pk_transaction_update_system (PkTransaction *transaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *sender;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("UpdateSystem method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->update_system == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check if the action is allowed from this client - if not, set an error */
	sender = dbus_g_method_get_sender (context);
	ret = pk_transaction_action_is_allowed (transaction, sender, PK_ROLE_ENUM_UPDATE_SYSTEM, &error);
	g_free (sender);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	/* are we already performing an update? */
	if (pk_transaction_list_role_present (transaction->priv->transaction_list, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
				     "Already performing system update");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_SYSTEM);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_what_provides:
 **/
void
pk_transaction_what_provides (PkTransaction *transaction, const gchar *filter, const gchar *type,
			 const gchar *search, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkProvidesEnum provides;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_debug ("WhatProvides method called: %s, %s", type, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->what_provides == NULL) {
		pk_debug ("Not implemented yet: WhatProvides");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Operation not yet supported by backend");
		dbus_g_method_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		dbus_g_method_return_error (context, error);
		return;
	}

	provides = pk_role_enum_from_text (type);
	if (provides == PK_PROVIDES_ENUM_UNKNOWN) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_PROVIDE,
				     "provide type '%s' not found", type);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* set the dbus name, so we can get the disconnect */
	pk_transaction_set_dbus_name (transaction, dbus_g_method_get_sender (context));

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_enums_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	transaction->priv->cached_provides = provides;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_WHAT_PROVIDES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_list_remove (transaction->priv->transaction_list, transaction);
		dbus_g_method_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_class_init:
 * @klass: The PkTransactionClass
 **/
static void
pk_transaction_class_init (PkTransactionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_transaction_finalize;

	signals [PK_TRANSACTION_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_TRANSACTION_CALLER_ACTIVE_CHANGED] =
		g_signal_new ("caller-active-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_TRANSACTION_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_UINT64,
			      G_TYPE_NONE, 6, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64);
	signals [PK_TRANSACTION_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_TRANSACTION_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_TRANSACTION_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL,
			      G_TYPE_NONE, 3, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_BOOLEAN);
	signals [PK_TRANSACTION_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_EULA_REQUIRED] =
		g_signal_new ("eula-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [PK_TRANSACTION_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_STRING_UINT_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TRANSACTION_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (PkTransactionPrivate));
}

/**
 * pk_transaction_init:
 * @transaction: This class instance
 **/
static void
pk_transaction_init (PkTransaction *transaction)
{
	transaction->priv = PK_TRANSACTION_GET_PRIVATE (transaction);
	transaction->priv->finished = FALSE;
	transaction->priv->running = FALSE;
	transaction->priv->allow_cancel = FALSE;
	transaction->priv->emit_eula_required = FALSE;
	transaction->priv->emit_signature_required = FALSE;
	transaction->priv->dbus_name = NULL;
	transaction->priv->cached_enabled = FALSE;
	transaction->priv->cached_key_id = NULL;
	transaction->priv->cached_package_id = NULL;
	transaction->priv->cached_package_ids = NULL;
	transaction->priv->cached_transaction_id = NULL;
	transaction->priv->cached_full_path = NULL;
	transaction->priv->cached_filters = PK_FILTER_ENUM_NONE;
	transaction->priv->cached_search = NULL;
	transaction->priv->cached_repo_id = NULL;
	transaction->priv->cached_parameter = NULL;
	transaction->priv->cached_value = NULL;
	transaction->priv->last_package = NULL;
	transaction->priv->tid = NULL;
	transaction->priv->role = PK_ROLE_ENUM_UNKNOWN;

	transaction->priv->backend = pk_backend_new ();
	transaction->priv->security = pk_security_new ();
	transaction->priv->cache = pk_cache_new ();
	transaction->priv->notify = pk_notify_new ();
	transaction->priv->inhibit = pk_inhibit_new ();
	transaction->priv->package_list = pk_package_list_new ();
	transaction->priv->transaction_list = pk_transaction_list_new ();
	transaction->priv->transaction_db = pk_transaction_db_new ();
	g_signal_connect (transaction->priv->transaction_db, "transaction",
			  G_CALLBACK (pk_transaction_transaction_cb), transaction);

	transaction->priv->libgbus = libgbus_new ();
	g_signal_connect (transaction->priv->libgbus, "connection-changed",
			  G_CALLBACK (pk_transaction_caller_active_changed_cb), transaction);
}

/**
 * pk_transaction_finalize:
 * @object: The object to finalize
 **/
static void
pk_transaction_finalize (GObject *object)
{
	PkTransaction *transaction;

	g_return_if_fail (PK_IS_TRANSACTION (object));

	transaction = PK_TRANSACTION (object);
	g_return_if_fail (transaction->priv != NULL);

	g_free (transaction->priv->last_package);
	g_free (transaction->priv->dbus_name);
	g_free (transaction->priv->cached_package_id);
	g_free (transaction->priv->cached_key_id);
	g_strfreev (transaction->priv->cached_package_ids);
	g_free (transaction->priv->cached_transaction_id);
	g_free (transaction->priv->cached_search);
	g_free (transaction->priv->cached_repo_id);
	g_free (transaction->priv->cached_parameter);
	g_free (transaction->priv->cached_value);
	g_free (transaction->priv->tid);

	/* remove any inhibit, it's okay to call this function when it's not needed */
	pk_inhibit_remove (transaction->priv->inhibit, transaction);
	g_object_unref (transaction->priv->cache);
	g_object_unref (transaction->priv->inhibit);
	g_object_unref (transaction->priv->backend);
	g_object_unref (transaction->priv->libgbus);
	g_object_unref (transaction->priv->package_list);
	g_object_unref (transaction->priv->transaction_list);
	g_object_unref (transaction->priv->transaction_db);
	g_object_unref (transaction->priv->security);
	g_object_unref (transaction->priv->notify);

	G_OBJECT_CLASS (pk_transaction_parent_class)->finalize (object);
}

/**
 * pk_transaction_new:
 *
 * Return value: a new PkTransaction object.
 **/
PkTransaction *
pk_transaction_new (void)
{
	PkTransaction *transaction;
	transaction = g_object_new (PK_TYPE_TRANSACTION, NULL);
	return PK_TRANSACTION (transaction);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_transaction (LibSelfTest *test)
{
	PkTransaction *transaction = NULL;
	gboolean ret;
	const gchar *temp;
	GError *error = NULL;

	if (libst_start (test, "PkTransaction", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get PkTransaction object");
	transaction = pk_transaction_new ();
	if (transaction != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************
	 ****************          FILTERS         ******************
	 ************************************************************/
	temp = NULL;
	libst_title (test, "test a fail filter (null)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "";
	libst_title (test, "test a fail filter ()");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = ";";
	libst_title (test, "test a fail filter (;)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "moo";
	libst_title (test, "test a fail filter (invalid)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "moo;foo";
	libst_title (test, "test a fail filter (invalid, multiple)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "gui;;";
	libst_title (test, "test a fail filter (valid then zero length)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "none";
	libst_title (test, "test a pass filter (none)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "gui";
	libst_title (test, "test a pass filter (single)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "devel;~gui";
	libst_title (test, "test a pass filter (multiple)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}
	g_clear_error (&error);

	/************************************************************/
	temp = "~gui;~installed";
	libst_title (test, "test a pass filter (multiple2)");
	ret = pk_transaction_filter_check (temp, &error);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}
	g_clear_error (&error);

	g_object_unref (transaction);

	libst_end (test);
}
#endif

