/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
#include <sys/stat.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gio/gio.h>
#include <packagekit-glib/packagekit.h>
#ifdef USE_SECURITY_POLKIT
#include <polkit/polkit.h>
#endif

#include "egg-debug.h"
#include "egg-string.h"
#include "egg-dbus-monitor.h"

#include "pk-transaction.h"
#include "pk-transaction-list.h"
#include "pk-transaction-db.h"
#include "pk-marshal.h"
#include "pk-backend.h"
#include "pk-backend-internal.h"
#include "pk-inhibit.h"
#include "pk-conf.h"
#include "pk-shared.h"
#include "pk-cache.h"
#include "pk-notify.h"
#include "pk-transaction-extra.h"
#include "pk-syslog.h"

static void     pk_transaction_finalize		(GObject	    *object);
static void     pk_transaction_dispose		(GObject	    *object);

#define PK_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION, PkTransactionPrivate))
#define PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT	100 /* ms */

/* when the UID is invalid or not known */
#define PK_TRANSACTION_UID_INVALID		G_MAXUINT

static void pk_transaction_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkTransaction *transaction);
static void pk_transaction_progress_changed_cb (PkBackend *backend, guint percentage, guint subpercentage, guint elapsed, guint remaining, PkTransaction *transaction);

struct PkTransactionPrivate
{
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	guint			 percentage;
	guint			 subpercentage;
	guint			 elapsed;
	guint			 remaining;
	gboolean		 finished;
	gboolean		 running;
	gboolean		 has_been_run;
	gboolean		 allow_cancel;
	gboolean		 waiting_for_auth;
	gboolean		 emit_eula_required;
	gboolean		 emit_signature_required;
	gboolean		 emit_media_change_required;
	gchar			*locale;
	guint			 uid;
	EggDbusMonitor		*monitor;
	PkBackend		*backend;
	PkInhibit		*inhibit;
	PkCache			*cache;
	PkConf			*conf;
	PkNotify		*notify;
#ifdef USE_SECURITY_POLKIT
	PolkitAuthority		*authority;
	PolkitSubject		*subject;
	GCancellable		*cancellable;
#endif
	DBusGConnection		*connection;
	DBusGProxy		*proxy_pid;
	PkTransactionExtra		*transaction_extra;
	PkSyslog		*syslog;

	/* needed for gui coldplugging */
	gchar			*last_package_id;
	gchar			*tid;
	gchar			*sender;
	gchar			*cmdline;
	GPtrArray		*require_restart_list;
	PkPackageList		*package_list;
	PkTransactionList	*transaction_list;
	PkTransactionDb		*transaction_db;

	/* cached */
	gboolean		 cached_force;
	gboolean		 cached_allow_deps;
	gboolean		 cached_autoremove;
	gboolean		 cached_enabled;
	gboolean		 cached_only_trusted;
	gchar			*cached_package_id;
	gchar			**cached_package_ids;
	gchar			*cached_transaction_id;
	gchar			*cached_full_path;
	gchar			**cached_full_paths;
	PkBitfield		 cached_filters;
	gchar			*cached_search;
	gchar			*cached_repo_id;
	gchar			*cached_key_id;
	gchar			*cached_parameter;
	gchar			*cached_value;
	gchar			*cached_directory;
	gchar			*cached_cat_id;
	PkProvidesEnum		 cached_provides;

	guint			 signal_allow_cancel;
	guint			 signal_details;
	guint			 signal_error_code;
	guint			 signal_files;
	guint			 signal_distro_upgrade;
	guint			 signal_finished;
	guint			 signal_message;
	guint			 signal_package;
	guint			 signal_progress_changed;
	guint			 signal_repo_detail;
	guint			 signal_repo_signature_required;
	guint			 signal_eula_required;
	guint			 signal_media_change_required;
	guint			 signal_require_restart;
	guint			 signal_status_changed;
	guint			 signal_update_detail;
	guint			 signal_category;
};

enum {
	PK_TRANSACTION_ALLOW_CANCEL,
	PK_TRANSACTION_CALLER_ACTIVE_CHANGED,
	PK_TRANSACTION_DETAILS,
	PK_TRANSACTION_ERROR_CODE,
	PK_TRANSACTION_DISTRO_UPGRADE,
	PK_TRANSACTION_FILES,
	PK_TRANSACTION_FINISHED,
	PK_TRANSACTION_MESSAGE,
	PK_TRANSACTION_PACKAGE,
	PK_TRANSACTION_PROGRESS_CHANGED,
	PK_TRANSACTION_REPO_DETAIL,
	PK_TRANSACTION_REPO_SIGNATURE_REQUIRED,
	PK_TRANSACTION_EULA_REQUIRED,
	PK_TRANSACTION_MEDIA_CHANGE_REQUIRED,
	PK_TRANSACTION_REQUIRE_RESTART,
	PK_TRANSACTION_STATUS_CHANGED,
	PK_TRANSACTION_TRANSACTION,
	PK_TRANSACTION_UPDATE_DETAIL,
	PK_TRANSACTION_CATEGORY,
	PK_TRANSACTION_DESTROY,
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
	if (!quark)
		quark = g_quark_from_static_string ("pk_transaction_error");
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
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NO_SUCH_DIRECTORY, "NoSuchDirectory"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE, "TransactionExistsWithRole"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_REFUSED_BY_POLICY, "RefusedByPolicy"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID, "PackageIdInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_SEARCH_INVALID, "SearchInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_SEARCH_PATH_INVALID, "SearchPathInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_FILTER_INVALID, "FilterInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INPUT_INVALID, "InputInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INVALID_STATE, "InvalidState"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_INITIALIZE_FAILED, "InitializeFailed"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_COMMIT_FAILED, "CommitFailed"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_PACK_INVALID, "PackInvalid"),
			ENUM_ENTRY (PK_TRANSACTION_ERROR_MIME_TYPE_NOT_SUPPORTED, "MimeTypeNotSupported"),
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
static guint
pk_transaction_get_runtime (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), 0);
	g_return_val_if_fail (transaction->priv->tid != NULL, 0);
	return pk_backend_get_runtime (transaction->priv->backend);
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
 * pk_transaction_get_text:
 **/
static gchar *
pk_transaction_get_text (PkTransaction *transaction)
{
	PkPackageId *id;
	gchar *text = NULL;
	const gchar *data;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (transaction->priv->tid != NULL, NULL);

	if (transaction->priv->cached_package_id != NULL) {
		data = transaction->priv->cached_package_id;
		/* is a package id? */
		if (pk_package_id_check (data)) {
			id = pk_package_id_new_from_string (data);
			text = g_strdup (id->name);
			pk_package_id_free (id);
		} else {
			text = g_strdup (data);
		}
	} else if (transaction->priv->cached_package_ids != NULL) {
		data = transaction->priv->cached_package_ids[0];
		/* is a package id? */
		if (pk_package_id_check (data)) {
			/* FIXME: join all with ';' */
			id = pk_package_id_new_from_string (data);
			text = g_strdup (id->name);
			pk_package_id_free (id);
		} else {
			text = g_strdup (data);
		}
	} else if (transaction->priv->cached_search != NULL) {
		text = g_strdup (transaction->priv->cached_search);
	}

	return text;
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
		egg_warning ("could not get current tid from backend");
		return FALSE;
	}

	egg_debug ("invalidating caches");

	/* copy this into the cache if we are getting updates */
	if (transaction->priv->role == PK_ROLE_ENUM_GET_UPDATES)
		pk_cache_set_updates (transaction->priv->cache, transaction->priv->package_list);

	/* we unref the update cache if it exists */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)
		pk_cache_invalidate (transaction->priv->cache);

	/* this has to be done as different repos might have different updates */
	if (transaction->priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_SET_DATA)
		pk_cache_invalidate (transaction->priv->cache);

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
 * pk_transaction_progress_changed_emit:
 **/
static void
pk_transaction_progress_changed_emit (PkTransaction *transaction, guint percentage, guint subpercentage, guint elapsed, guint remaining)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	/* save so we can do GetProgress on a queued or finished transaction */
	transaction->priv->percentage = percentage;
	transaction->priv->subpercentage = subpercentage;
	transaction->priv->elapsed = elapsed;
	transaction->priv->remaining = remaining;

	egg_debug ("emitting percentage-changed %i, %i, %i, %i", percentage, subpercentage, elapsed, remaining);
	g_signal_emit (transaction, signals [PK_TRANSACTION_PROGRESS_CHANGED], 0, percentage, subpercentage, elapsed, remaining);
}

/**
 * pk_transaction_allow_cancel_emit:
 **/
static void
pk_transaction_allow_cancel_emit (PkTransaction *transaction, gboolean allow_cancel)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	transaction->priv->allow_cancel = allow_cancel;

	/* remove or add the hal inhibit */
	if (allow_cancel)
		pk_inhibit_remove (transaction->priv->inhibit, transaction);
	else
		pk_inhibit_add (transaction->priv->inhibit, transaction);

	egg_debug ("emitting allow-cancel %i", allow_cancel);
	g_signal_emit (transaction, signals [PK_TRANSACTION_ALLOW_CANCEL], 0, allow_cancel);
}

/**
 * pk_transaction_status_changed_emit:
 **/
static void
pk_transaction_status_changed_emit (PkTransaction *transaction, PkStatusEnum status)
{
	const gchar *status_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	transaction->priv->status = status;
	status_text = pk_status_enum_to_text (status);

	egg_debug ("emitting status-changed '%s'", status_text);
	g_signal_emit (transaction, signals [PK_TRANSACTION_STATUS_CHANGED], 0, status_text);
}

/**
 * pk_transaction_finished_emit:
 **/
static void
pk_transaction_finished_emit (PkTransaction *transaction, PkExitEnum exit_enum, guint time_ms)
{
	const gchar *exit_text;
	exit_text = pk_exit_enum_to_text (exit_enum);
	egg_debug ("emitting finished '%s', %i", exit_text, time_ms);
	g_signal_emit (transaction, signals [PK_TRANSACTION_FINISHED], 0, exit_text, time_ms);
}

/**
 * pk_transaction_error_code_emit:
 **/
static void
pk_transaction_error_code_emit (PkTransaction *transaction, PkErrorCodeEnum error_enum, const gchar *details)
{
	const gchar *text;
	text = pk_error_enum_to_text (error_enum);
	egg_debug ("emitting error-code %s, '%s'", text, details);
	g_signal_emit (transaction, signals [PK_TRANSACTION_ERROR_CODE], 0, text, details);
}

/**
 * pk_transaction_allow_cancel_cb:
 **/
static void
pk_transaction_allow_cancel_cb (PkBackend *backend, gboolean allow_cancel, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	pk_transaction_allow_cancel_emit (transaction, allow_cancel);
}

/**
 * pk_transaction_caller_active_changed_cb:
 **/
static void
pk_transaction_caller_active_changed_cb (EggDbusMonitor *egg_dbus_monitor, gboolean is_active, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	if (is_active == FALSE) {
		egg_debug ("client disconnected....");
		g_signal_emit (transaction, signals [PK_TRANSACTION_CALLER_ACTIVE_CHANGED], 0, FALSE);
	}
}

/**
 * pk_transaction_details_cb:
 **/
static void
pk_transaction_details_cb (PkBackend *backend, PkDetailsObj *obj, PkTransaction *transaction)
{
	const gchar *group_text;
	gchar *package_id;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	group_text = pk_group_enum_to_text (obj->group);
	package_id = pk_package_id_to_string (obj->id);
	g_signal_emit (transaction, signals [PK_TRANSACTION_DETAILS], 0, package_id,
		       obj->license, group_text, obj->description, obj->url, obj->size);
	g_free (package_id);
}

/**
 * pk_transaction_error_code_cb:
 **/
static void
pk_transaction_error_code_cb (PkBackend *backend, PkErrorCodeEnum code,
			      const gchar *details, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	if (code == PK_ERROR_ENUM_UNKNOWN) {
		pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s emitted 'unknown error' rather than a specific error "
				    "- this is a backend problem and should be fixed!", pk_role_enum_to_text (transaction->priv->role));
	}

	pk_transaction_error_code_emit (transaction, code, details);
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

	egg_debug ("emitting files %s, %s", package_id, filelist);
	g_signal_emit (transaction, signals [PK_TRANSACTION_FILES], 0, package_id, filelist);
}

/**
 * pk_transaction_category_cb:
 **/
static void
pk_transaction_category_cb (PkBackend *backend, const gchar *parent_id, const gchar *cat_id,
			 const gchar *name, const gchar *summary, const gchar *icon,
			 PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("emitting category %s, %s, %s, %s, %s ", parent_id, cat_id, name, summary, icon);
	g_signal_emit (transaction, signals [PK_TRANSACTION_CATEGORY], 0, parent_id, cat_id, name, summary, icon);
}

/**
 * pk_transaction_distro_upgrade_cb:
 **/
static void
pk_transaction_distro_upgrade_cb (PkBackend *backend, PkDistroUpgradeEnum type,
				  const gchar *name, const gchar *summary, PkTransaction *transaction)
{
	const gchar *type_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	type_text = pk_distro_upgrade_enum_to_text (type);
	egg_debug ("emitting distro-upgrade %s, %s, %s", type_text, name, summary);
	g_signal_emit (transaction, signals [PK_TRANSACTION_DISTRO_UPGRADE], 0, type_text, name, summary);
}

/**
 * pk_transaction_finished_cb:
 **/
static void
pk_transaction_finished_cb (PkBackend *backend, PkExitEnum exit_enum, PkTransaction *transaction)
{
	gboolean ret;
	guint time_ms;
	gchar *packages;
	gchar **package_ids;
	guint i, length;
	GPtrArray *list;
	const PkPackageObj *obj;
	gchar *package_id;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		egg_warning ("Already finished");
		return;
	}

	/* disconnect these straight away, as the PkTransaction object takes time to timeout */
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_details);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_error_code);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_files);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_distro_upgrade);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_finished);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_package);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_repo_detail);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_repo_signature_required);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_eula_required);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_media_change_required);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_update_detail);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_category);

	/* check for session restarts */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS &&
	    (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	     transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {

		/* check updated packages file lists and running processes */
		ret = pk_conf_get_bool (transaction->priv->conf, "UpdateCheckProcesses");
		if (ret) {

			/* filter on UPDATING */
			list = g_ptr_array_new ();
			length = pk_package_list_get_size (transaction->priv->package_list);
			for (i=0; i<length; i++) {
				obj = pk_package_list_get_obj (transaction->priv->package_list, i);
				if (obj->info == PK_INFO_ENUM_UPDATING) {
					/* we convert the package_id data to be 'installed' as this means
					 * we can use the local package database for GetFiles rather than
					 * downloading new remote metadata */
					package_id = pk_package_id_build (obj->id->name, obj->id->version, obj->id->arch, "installed");
					g_ptr_array_add (list, package_id);
				}
			}

			/* process file lists on these packages */
			if (list->len > 0) {
				package_ids = pk_package_ids_from_array (list);
				pk_transaction_extra_check_running_process (transaction->priv->transaction_extra, package_ids);
				g_strfreev (package_ids);
			}
			g_ptr_array_foreach (list, (GFunc) g_free, NULL);
			g_ptr_array_free (list, TRUE);
		}
	}

	/* rescan desktop files after install */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS &&
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {

		/* refresh the desktop icon cache */
		ret = pk_conf_get_bool (transaction->priv->conf, "ScanDesktopFiles");
		if (ret) {

			/* filter on INSTALLING | UPDATING */
			list = g_ptr_array_new ();
			length = pk_package_list_get_size (transaction->priv->package_list);
			for (i=0; i<length; i++) {
				obj = pk_package_list_get_obj (transaction->priv->package_list, i);
				if (obj->info == PK_INFO_ENUM_INSTALLING || obj->info == PK_INFO_ENUM_UPDATING) {
					/* we convert the package_id data to be 'installed' */
					package_id = pk_package_id_build (obj->id->name, obj->id->version, obj->id->arch, "installed");
					g_ptr_array_add (list, package_id);
				}
			}

			egg_debug ("processing %i packags for desktop files", PK_OBJ_LIST(list)->len);
			/* process file lists on these packages */
			if (list->len > 0) {
				package_ids = pk_package_ids_from_array (list);
				pk_transaction_extra_check_desktop_files (transaction->priv->transaction_extra, package_ids);
				g_strfreev (package_ids);
			}
			g_ptr_array_foreach (list, (GFunc) g_free, NULL);
			g_ptr_array_free (list, TRUE);
		}
	}

	/* signals we are not allowed to send from the second phase post transaction */
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_allow_cancel);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_message);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_status_changed);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_progress_changed);
	g_signal_handler_disconnect (transaction->priv->backend, transaction->priv->signal_require_restart);

	/* do some optional extra actions when we've finished refreshing the cache */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS &&
	    transaction->priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {

		/* generate the package list */
		ret = pk_conf_get_bool (transaction->priv->conf, "UpdatePackageList");
		if (ret)
			pk_transaction_extra_update_package_list (transaction->priv->transaction_extra);

		/* refresh the desktop icon cache */
		ret = pk_conf_get_bool (transaction->priv->conf, "ScanDesktopFiles");
		if (ret)
			pk_transaction_extra_import_desktop_files (transaction->priv->transaction_extra);

		/* clear the firmware requests directory */
		pk_transaction_extra_clear_firmware_requests (transaction->priv->transaction_extra);
	}

	/* if we did not send this, ensure the GUI has the right state */
	if (transaction->priv->allow_cancel)
		pk_transaction_allow_cancel_emit (transaction, FALSE);

	/* we should get no more from the backend with this tid */
	transaction->priv->finished = TRUE;

	/* mark not running */
	transaction->priv->running = FALSE;

	/* if we did ::repo-signature-required or ::eula-required, change the error code */
	if (transaction->priv->emit_signature_required)
		exit_enum = PK_EXIT_ENUM_KEY_REQUIRED;
	else if (transaction->priv->emit_eula_required)
		exit_enum = PK_EXIT_ENUM_EULA_REQUIRED;
	else if (transaction->priv->emit_media_change_required)
		exit_enum = PK_EXIT_ENUM_MEDIA_CHANGE_REQUIRED;

	/* invalidate some caches if we succeeded */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS)
		pk_transaction_finish_invalidate_caches (transaction);

	/* find the length of time we have been running */
	time_ms = pk_transaction_get_runtime (transaction);
	egg_debug ("backend was running for %i ms", time_ms);

	/* add to the database if we are going to log it */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		packages = pk_obj_list_to_string (PK_OBJ_LIST(transaction->priv->package_list));

		/* save to database */
		if (!egg_strzero (packages))
			pk_transaction_db_set_data (transaction->priv->transaction_db, transaction->priv->tid, packages);
		g_free (packages);

		/* report to syslog */
		length = PK_OBJ_LIST(transaction->priv->package_list)->len;
		for (i=0; i<length; i++) {
			obj = pk_package_list_get_obj (transaction->priv->package_list, i);
			if (obj->info == PK_INFO_ENUM_REMOVING ||
			    obj->info == PK_INFO_ENUM_INSTALLING ||
			    obj->info == PK_INFO_ENUM_UPDATING) {
				packages = pk_package_id_to_string (obj->id);
				pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "in %s for %s package %s was %s for uid %i",
					       transaction->priv->tid, pk_role_enum_to_text (transaction->priv->role),
					       packages, pk_info_enum_to_text (obj->info), transaction->priv->uid);
				g_free (packages);
			}
		}
	}

	/* the repo list will have changed */
	if (transaction->priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		pk_notify_repo_list_changed (transaction->priv->notify);
	}

	/* only reset the time if we succeeded */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS)
		pk_transaction_db_action_time_reset (transaction->priv->transaction_db, transaction->priv->role);

	/* did we finish okay? */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS)
		pk_transaction_db_set_finished (transaction->priv->transaction_db, transaction->priv->tid, TRUE, time_ms);
	else
		pk_transaction_db_set_finished (transaction->priv->transaction_db, transaction->priv->tid, FALSE, time_ms);

	/* remove any inhibit */
	pk_inhibit_remove (transaction->priv->inhibit, transaction);

	/* report to syslog */
	if (transaction->priv->uid != PK_TRANSACTION_UID_INVALID)
		pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "%s transaction %s from uid %i finished with %s after %ims",
			       pk_role_enum_to_text (transaction->priv->role), transaction->priv->tid,
			       transaction->priv->uid, pk_exit_enum_to_text (exit_enum), time_ms);
	else
		pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "%s transaction %s finished with %s after %ims",
			       pk_role_enum_to_text (transaction->priv->role), transaction->priv->tid, pk_exit_enum_to_text (exit_enum), time_ms);

	/* we emit last, as other backends will be running very soon after us, and we don't want to be notified */
	pk_transaction_finished_emit (transaction, exit_enum, time_ms);
}

/**
 * pk_transaction_message_cb:
 **/
static void
pk_transaction_message_cb (PkBackend *backend, PkMessageEnum message, const gchar *details, PkTransaction *transaction)
{
	const gchar *message_text;
	gboolean developer_mode;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* if not running in developer mode, then skip these types */
	developer_mode = pk_conf_get_bool (transaction->priv->conf, "DeveloperMode");
	if (!developer_mode &&
	    (message == PK_MESSAGE_ENUM_BACKEND_ERROR ||
	     message == PK_MESSAGE_ENUM_DAEMON_ERROR)) {
		egg_warning ("ignoring message: %s", details);
		return;
	}

	message_text = pk_message_enum_to_text (message);
	egg_debug ("emitting message %s, '%s'", message_text, details);
	g_signal_emit (transaction, signals [PK_TRANSACTION_MESSAGE], 0, message_text, details);
}

/**
 * pk_transaction_package_cb:
 **/
static void
pk_transaction_package_cb (PkBackend *backend, const PkPackageObj *obj, PkTransaction *transaction)
{
	const gchar *info_text;
	const gchar *role_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		egg_warning ("Already finished");
		return;
	}

	/* we need this in warnings */
	role_text = pk_role_enum_to_text (transaction->priv->role);

	/* check the backend is doing the right thing */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		if (obj->info == PK_INFO_ENUM_INSTALLED) {
			pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "%s emitted 'installed' rather than 'installing' "
					    "- you need to do the package *before* you do the action", role_text);
			return;
		}
	}

	/* check we are respecting the filters */
	if (pk_bitfield_contain (transaction->priv->cached_filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		if (obj->info == PK_INFO_ENUM_INSTALLED) {
			pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "%s emitted package that was installed when "
					    "the ~installed filter is in place", role_text);
			return;
		}
	}
	if (pk_bitfield_contain (transaction->priv->cached_filters, PK_FILTER_ENUM_INSTALLED)) {
		if (obj->info == PK_INFO_ENUM_AVAILABLE) {
			pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "%s emitted package that was ~installed when "
					    "the installed filter is in place", role_text);
			return;
		}
	}

	/* add to package cache even if we already got a result */
	if (obj->info != PK_INFO_ENUM_FINISHED)
		pk_obj_list_add (PK_OBJ_LIST(transaction->priv->package_list), obj);

	/* emit */
	g_free (transaction->priv->last_package_id);
	transaction->priv->last_package_id = pk_package_id_to_string (obj->id);
	info_text = pk_info_enum_to_text (obj->info);
	g_signal_emit (transaction, signals [PK_TRANSACTION_PACKAGE], 0, info_text,
		       transaction->priv->last_package_id, obj->summary);
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

	pk_transaction_progress_changed_emit (transaction, percentage, subpercentage, elapsed, remaining);
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

	egg_debug ("emitting repo-detail %s, %s, %i", repo_id, description, enabled);
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

	egg_debug ("emitting repo_signature_required %s, %s, %s, %s, %s, %s, %s, %s",
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

	egg_debug ("emitting eula-required %s, %s, %s, %s",
		  eula_id, package_id, vendor_name, license_agreement);
	g_signal_emit (transaction, signals [PK_TRANSACTION_EULA_REQUIRED], 0,
		       eula_id, package_id, vendor_name, license_agreement);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_eula_required = TRUE;
}

/**
 * pk_transaction_media_change_required_cb:
 **/
static void
pk_transaction_media_change_required_cb (PkBackend *backend,
					 PkMediaTypeEnum media_type,
					 const gchar *media_id,
					 const gchar *media_text,
					 PkTransaction *transaction)
{
	const gchar *media_type_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	media_type_text = pk_media_type_enum_to_text (media_type);

	egg_debug ("emitting media-change-required %s, %s, %s",
		   media_type_text, media_id, media_text);
	g_signal_emit (transaction, signals [PK_TRANSACTION_MEDIA_CHANGE_REQUIRED], 0,
		       media_type_text, media_id, media_text);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_media_change_required = TRUE;
}

/**
 * pk_transaction_require_restart_cb:
 **/
static void
pk_transaction_require_restart_cb (PkBackend *backend, PkRestartEnum restart, const gchar *package_id, PkTransaction *transaction)
{
	const gchar *restart_text;
	const gchar *package_id_tmp;
	GPtrArray *list;
	gboolean found = FALSE;
	guint i;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	restart_text = pk_restart_enum_to_text (restart);

	/* filter out duplicates */
	list = transaction->priv->require_restart_list;
	for (i=0; i<list->len; i++) {
		package_id_tmp = g_ptr_array_index (list, i);
		if (g_strcmp0 (package_id, package_id_tmp) == 0) {
			found = TRUE;
			break;
		}
	}

	/* ignore */
	if (found) {
		egg_debug ("ignoring %s (%s) as already sent", restart_text, package_id);
		return;
	}

	/* add to duplicate list */
	g_ptr_array_add (list, g_strdup (package_id));

	egg_debug ("emitting require-restart %s, '%s'", restart_text, package_id);
	g_signal_emit (transaction, signals [PK_TRANSACTION_REQUIRE_RESTART], 0, restart_text, package_id);
}

/**
 * pk_transaction_status_changed_cb:
 **/
static void
pk_transaction_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		egg_warning ("Already finished, so can't proxy status %s", pk_status_enum_to_text (status));
		return;
	}

	pk_transaction_status_changed_emit (transaction, status);
}

/**
 * pk_transaction_transaction_cb:
 **/
static void
pk_transaction_transaction_cb (PkTransactionDb *tdb, const gchar *old_tid, const gchar *timespec,
			       gboolean succeeded, PkRoleEnum role, guint duration,
			       const gchar *data, guint uid, const gchar *cmdline, PkTransaction *transaction)
{
	const gchar *role_text;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	role_text = pk_role_enum_to_text (role);
	egg_debug ("emitting transaction %s, %s, %i, %s, %i, %s, %i, %s", old_tid, timespec, succeeded, role_text, duration, data, uid, cmdline);
	g_signal_emit (transaction, signals [PK_TRANSACTION_TRANSACTION], 0, old_tid, timespec, succeeded, role_text, duration, data, uid, cmdline);
}

/**
 * pk_transaction_update_detail_cb:
 **/
static void
pk_transaction_update_detail_cb (PkBackend *backend, const PkUpdateDetailObj *detail, PkTransaction *transaction)
{
	const gchar *restart_text;
	const gchar *state_text;
	gchar *package_id;
	gchar *issued;
	gchar *updated;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	restart_text = pk_restart_enum_to_text (detail->restart);
	package_id = pk_package_id_to_string (detail->id);
	state_text = pk_update_state_enum_to_text (detail->state);
	issued = pk_iso8601_from_date (detail->issued);
	updated = pk_iso8601_from_date (detail->updated);

	g_signal_emit (transaction, signals [PK_TRANSACTION_UPDATE_DETAIL], 0,
		       package_id, detail->updates, detail->obsoletes, detail->vendor_url,
		       detail->bugzilla_url, detail->cve_url, restart_text, detail->update_text,
		       detail->changelog, state_text, issued, updated);

	g_free (issued);
	g_free (updated);
	g_free (package_id);
}

/**
 * pk_transaction_pre_transaction_checks:
 *
 * TODO: also check if package in InstallPackages is in the update lists
 *       and do the same check if this is so.
 */
static gboolean
pk_transaction_pre_transaction_checks (PkTransaction *transaction)
{
	PkPackageList *updates;
	const PkPackageObj *obj;
	guint i;
	guint length;
	gboolean ret = FALSE;
	gchar *package_id;
	gchar **package_ids = NULL;
	GPtrArray *list = NULL;

	/* only do this for update actions */
	if (transaction->priv->role != PK_ROLE_ENUM_UPDATE_SYSTEM &&
	    transaction->priv->role != PK_ROLE_ENUM_UPDATE_PACKAGES) {
		egg_debug ("doing nothing, as not update");
		goto out;
	}

	/* do we want to enable this codepath? */
	ret = pk_conf_get_bool (transaction->priv->conf, "CheckSharedLibrariesInUse");
	if (!ret) {
		egg_warning ("not checking for library restarts");
		goto out;
	}

	/* do we have a cache */
	updates = pk_cache_get_updates (transaction->priv->cache);
	if (updates == NULL) {
		egg_warning ("no updates cache");
		goto out;
	}

	/* find security update packages */
	list = g_ptr_array_new ();
	length = pk_package_list_get_size (updates);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (updates, i);
		if (obj->info == PK_INFO_ENUM_SECURITY) {
			package_id = pk_package_id_to_string (obj->id);
			egg_debug ("security update: %s", package_id);
			g_ptr_array_add (list, package_id);
		}
	}

	/* find files in security updates */
	package_ids = pk_package_ids_from_array (list);
	ret = pk_transaction_extra_check_library_restart (transaction->priv->transaction_extra, package_ids);
out:
	g_strfreev (package_ids);
	if (list != NULL) {
		g_ptr_array_foreach (list, (GFunc) g_free, NULL);
		g_ptr_array_free (list, TRUE);
	}
	return ret;
}

/**
 * pk_transaction_set_running:
 */
G_GNUC_WARN_UNUSED_RESULT static gboolean
pk_transaction_set_running (PkTransaction *transaction)
{
	gboolean ret;
	PkBackendDesc *desc;
	PkStore *store;
	PkTransactionPrivate *priv = PK_TRANSACTION_GET_PRIVATE (transaction);
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	/* reset the require-restart list */
	g_ptr_array_foreach (transaction->priv->require_restart_list, (GFunc) g_free, NULL);
	g_ptr_array_set_size (transaction->priv->require_restart_list, 0);

	/* prepare for use; the transaction list ensures this is safe */
	pk_backend_reset (transaction->priv->backend);

	/* assign */
	pk_backend_set_current_tid (priv->backend, priv->tid);

	/* if we didn't set a locale for this transaction, we would reuse the
	 * last set locale in the backend, or NULL if it was not ever set.
	 * in this case use the C locale */
	if (priv->locale == NULL)
		pk_backend_set_locale (priv->backend, "C");
	else
		pk_backend_set_locale (priv->backend, priv->locale);

	/* set the role */
	pk_backend_set_role (priv->backend, priv->role);
	egg_debug ("setting role for %s to %s", priv->tid, pk_role_enum_to_text (priv->role));

	/* do any pre transaction checks */
	ret = pk_transaction_pre_transaction_checks (transaction);

	/* might have to reset again if we used the backend */
	if (ret)
		pk_backend_reset (transaction->priv->backend);

	/* connect up the signals */
	transaction->priv->signal_allow_cancel =
		g_signal_connect (transaction->priv->backend, "allow-cancel",
				  G_CALLBACK (pk_transaction_allow_cancel_cb), transaction);
	transaction->priv->signal_details =
		g_signal_connect (transaction->priv->backend, "details",
				  G_CALLBACK (pk_transaction_details_cb), transaction);
	transaction->priv->signal_error_code =
		g_signal_connect (transaction->priv->backend, "error-code",
				  G_CALLBACK (pk_transaction_error_code_cb), transaction);
	transaction->priv->signal_files =
		g_signal_connect (transaction->priv->backend, "files",
				  G_CALLBACK (pk_transaction_files_cb), transaction);
	transaction->priv->signal_distro_upgrade =
		g_signal_connect (transaction->priv->backend, "distro-upgrade",
				  G_CALLBACK (pk_transaction_distro_upgrade_cb), transaction);
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
	transaction->priv->signal_media_change_required =
		g_signal_connect (transaction->priv->backend, "media-change-required",
				  G_CALLBACK (pk_transaction_media_change_required_cb), transaction);
	transaction->priv->signal_require_restart =
		g_signal_connect (transaction->priv->backend, "require-restart",
				  G_CALLBACK (pk_transaction_require_restart_cb), transaction);
	transaction->priv->signal_status_changed =
		g_signal_connect (transaction->priv->backend, "status-changed",
				  G_CALLBACK (pk_transaction_status_changed_cb), transaction);
	transaction->priv->signal_update_detail =
		g_signal_connect (transaction->priv->backend, "update-detail",
				  G_CALLBACK (pk_transaction_update_detail_cb), transaction);
	transaction->priv->signal_category =
		g_signal_connect (transaction->priv->backend, "category",
				  G_CALLBACK (pk_transaction_category_cb), transaction);

	/* mark running */
	transaction->priv->running = TRUE;
	transaction->priv->has_been_run = TRUE;
	transaction->priv->allow_cancel = FALSE;

	/* we are no longer waiting, we are setting up */
	pk_backend_set_status (priv->backend, PK_STATUS_ENUM_SETUP);

	/* set all possible arguments for backend */
	store = pk_backend_get_store (priv->backend);
	pk_store_set_bool (store, "force", priv->cached_force);
	pk_store_set_bool (store, "allow_deps", priv->cached_allow_deps);
	pk_store_set_bool (store, "autoremove", priv->cached_autoremove);
	pk_store_set_bool (store, "enabled", priv->cached_enabled);
	pk_store_set_bool (store, "only_trusted", priv->cached_only_trusted);
	pk_store_set_uint (store, "filters", priv->cached_filters);
	pk_store_set_uint (store, "provides", priv->cached_provides);
	pk_store_set_strv (store, "package_ids", priv->cached_package_ids);
	pk_store_set_strv (store, "full_paths", priv->cached_full_paths);
	pk_store_set_string (store, "package_id", priv->cached_package_id);
	pk_store_set_string (store, "transaction_id", priv->cached_transaction_id);
	pk_store_set_string (store, "full_path", priv->cached_full_path);
	pk_store_set_string (store, "search", priv->cached_search);
	pk_store_set_string (store, "repo_id", priv->cached_repo_id);
	pk_store_set_string (store, "key_id", priv->cached_key_id);
	pk_store_set_string (store, "parameter", priv->cached_parameter);
	pk_store_set_string (store, "value", priv->cached_value);
	pk_store_set_string (store, "directory", priv->cached_directory);

	/* lets reduce pointer dereferences... */
	desc = priv->backend->desc;

	/* do the correct action with the cached parameters */
	if (priv->role == PK_ROLE_ENUM_GET_DEPENDS)
		desc->get_depends (priv->backend, priv->cached_filters, priv->cached_package_ids, priv->cached_force);
	else if (priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		desc->get_update_detail (priv->backend, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_RESOLVE)
		desc->resolve (priv->backend, priv->cached_filters, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_ROLLBACK)
		desc->rollback (priv->backend, priv->cached_transaction_id);
	else if (priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES)
		desc->download_packages (priv->backend, priv->cached_package_ids, priv->cached_directory);
	else if (priv->role == PK_ROLE_ENUM_GET_DETAILS)
		desc->get_details (priv->backend, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES)
		desc->get_distro_upgrades (priv->backend);
	else if (priv->role == PK_ROLE_ENUM_GET_FILES)
		desc->get_files (priv->backend, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_GET_REQUIRES)
		desc->get_requires (priv->backend, priv->cached_filters, priv->cached_package_ids, priv->cached_force);
	else if (priv->role == PK_ROLE_ENUM_WHAT_PROVIDES)
		desc->what_provides (priv->backend, priv->cached_filters, priv->cached_provides, priv->cached_search);
	else if (priv->role == PK_ROLE_ENUM_GET_UPDATES)
		desc->get_updates (priv->backend, priv->cached_filters);
	else if (priv->role == PK_ROLE_ENUM_GET_PACKAGES)
		desc->get_packages (priv->backend, priv->cached_filters);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_DETAILS)
		desc->search_details (priv->backend, priv->cached_filters, priv->cached_search);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_FILE)
		desc->search_file (priv->backend, priv->cached_filters, priv->cached_search);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_GROUP)
		desc->search_group (priv->backend, priv->cached_filters, priv->cached_search);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_NAME)
		desc->search_name (priv->backend,priv->cached_filters,priv->cached_search);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES)
		desc->install_packages (priv->backend, priv->cached_only_trusted, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_FILES)
		desc->install_files (priv->backend, priv->cached_only_trusted, priv->cached_full_paths);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_SIGNATURE)
		desc->install_signature (priv->backend, PK_SIGTYPE_ENUM_GPG, priv->cached_key_id, priv->cached_package_id);
	else if (priv->role == PK_ROLE_ENUM_REFRESH_CACHE)
		desc->refresh_cache (priv->backend,  priv->cached_force);
	else if (priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES)
		desc->remove_packages (priv->backend, priv->cached_package_ids, priv->cached_allow_deps, priv->cached_autoremove);
	else if (priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)
		desc->update_packages (priv->backend, priv->cached_only_trusted, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM)
		desc->update_system (priv->backend, priv->cached_only_trusted);
	else if (priv->role == PK_ROLE_ENUM_GET_CATEGORIES)
		desc->get_categories (priv->backend);
	else if (priv->role == PK_ROLE_ENUM_GET_REPO_LIST)
		desc->get_repo_list (priv->backend, priv->cached_filters);
	else if (priv->role == PK_ROLE_ENUM_REPO_ENABLE)
		desc->repo_enable (priv->backend, priv->cached_repo_id, priv->cached_enabled);
	else if (priv->role == PK_ROLE_ENUM_REPO_SET_DATA)
		desc->repo_set_data (priv->backend, priv->cached_repo_id, priv->cached_parameter, priv->cached_value);
	else {
		egg_error ("failed to run as role not assigned");
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
		egg_warning ("changing a tid -- why?");
		return FALSE;
	}
	g_free (transaction->priv->tid);
	transaction->priv->tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_transaction_get_uid:
 **/
static guint
pk_transaction_get_uid (PkTransaction *transaction, const gchar *sender)
{
	guint uid;
	DBusError error;
	DBusConnection *con;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), G_MAXUINT);
	g_return_val_if_fail (sender != NULL, G_MAXUINT);

	dbus_error_init (&error);
	con = dbus_g_connection_get_connection (transaction->priv->connection);
	uid = dbus_bus_get_unix_user (con, sender, &error);
	if (dbus_error_is_set (&error)) {
		egg_warning ("Could not get uid for connection: %s %s", error.name, error.message);
		uid = G_MAXUINT;
		goto out;
	}
out:
	return uid;
}

#ifdef USE_SECURITY_POLKIT
/**
 * pk_transaction_get_pid:
 **/
static guint
pk_transaction_get_pid (PkTransaction *transaction, PolkitSubject *subject)
{
	guint pid = G_MAXUINT;
	gboolean ret;
	gchar *sender = NULL;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), G_MAXUINT);
	g_return_val_if_fail (transaction->priv->proxy_pid != NULL, G_MAXUINT);
	g_return_val_if_fail (subject != NULL, G_MAXUINT);

	/* this comes back as 'system-bus-name::1.127' */
	sender = polkit_subject_to_string (subject);

	/* get pid from DBus (quite slow) */
	ret = dbus_g_proxy_call (transaction->priv->proxy_pid, "GetConnectionUnixProcessID", &error,
				 G_TYPE_STRING, sender+16,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &pid,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to get pid: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (sender);
	return pid;
}

/**
 * pk_transaction_get_cmdline:
 **/
static gchar *
pk_transaction_get_cmdline (PkTransaction *transaction, PolkitSubject *subject)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;
	guint pid;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (subject != NULL, NULL);

	/* get pid */
	pid = pk_transaction_get_pid (transaction, subject);
	if (pid == G_MAXUINT) {
		egg_warning ("failed to get PID");
		goto out;
	}

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get cmdline: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (filename);
	return cmdline;
}
#endif

/**
 * pk_transaction_set_sender:
 */
gboolean
pk_transaction_set_sender (PkTransaction *transaction, const gchar *sender)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (sender != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->sender == NULL, FALSE);

	egg_debug ("setting sender to %s", sender);
	transaction->priv->sender = g_strdup (sender);
	egg_dbus_monitor_assign (transaction->priv->monitor, EGG_DBUS_MONITOR_SYSTEM, sender);

	/* we get the UID for all callers as we need to know when to cancel */
#ifdef USE_SECURITY_POLKIT
	transaction->priv->subject = polkit_system_bus_name_new (sender);
	transaction->priv->cmdline = pk_transaction_get_cmdline (transaction, transaction->priv->subject);
#endif
	transaction->priv->uid = pk_transaction_get_uid (transaction, sender);

	return TRUE;
}

/**
 * pk_transaction_release_tid:
 **/
static gboolean
pk_transaction_release_tid (PkTransaction *transaction)
{
	gboolean ret;
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	/* release the ID as we are returning an error */
	ret = pk_transaction_list_remove (transaction->priv->transaction_list,
					  transaction->priv->tid);
	return ret;
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
	ret = pk_transaction_list_commit (transaction->priv->transaction_list,
					  transaction->priv->tid);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		egg_warning ("failed to commit (job not run?)");
		return FALSE;
	}

	/* only save into the database for useful stuff */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {

		/* add to database */
		pk_transaction_db_add (transaction->priv->transaction_db, transaction->priv->tid);

		/* save role in the database */
		pk_transaction_db_set_role (transaction->priv->transaction_db, transaction->priv->tid, transaction->priv->role);

		/* save uid */
		pk_transaction_db_set_uid (transaction->priv->transaction_db, transaction->priv->tid, transaction->priv->uid);

#ifdef USE_SECURITY_POLKIT
		/* save cmdline in db */
		if (transaction->priv->cmdline != NULL)
			pk_transaction_db_set_cmdline (transaction->priv->transaction_db, transaction->priv->tid, transaction->priv->cmdline);
#endif

		/* report to syslog */
		pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "new %s transaction %s scheduled from uid %i",
			       pk_role_enum_to_text (transaction->priv->role), transaction->priv->tid, transaction->priv->uid);
	}
	return TRUE;
}

/**
 * pk_transaction_finished_idle_cb:
 **/
static gboolean
pk_transaction_finished_idle_cb (PkTransaction *transaction)
{
	pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_SUCCESS, 0);
	return FALSE;
}

/**
 * pk_transaction_search_check:
 **/
static gboolean
pk_transaction_search_check (const gchar *search, GError **error)
{
	guint size;
	gboolean ret;

	/* limit to a 1k chunk */
	size = egg_strlen (search, 1024);

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
	if (size == 1024) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "The search string length is too large");
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
static gboolean
pk_transaction_filter_check (const gchar *filter, GError **error)
{
	gchar **sections;
	guint i;
	guint length;
	gboolean ret = FALSE;

	g_return_val_if_fail (error != NULL, FALSE);

	/* is zero? */
	if (egg_strzero (filter)) {
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
		if (egg_strzero (sections[i])) {
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

#ifdef USE_SECURITY_POLKIT
/**
 * pk_transaction_action_obtain_authorization:
 **/
static void
pk_transaction_action_obtain_authorization_finished_cb (GObject *source_object, GAsyncResult *res, PkTransaction *transaction)
{
	PolkitAuthorizationResult *result;
	gboolean ret;
	GError *error = NULL;

	/* finish the call */
	result = polkit_authority_check_authorization_finish (transaction->priv->authority, res, &error);
	transaction->priv->waiting_for_auth = FALSE;

	/* failed */
	if (result == NULL) {
		egg_warning ("failed to check for auth: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {

		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_error_code_emit (transaction, PK_ERROR_ENUM_NOT_AUTHORIZED, "failed to obtain auth");
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_FAILED, 0);

		pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_AUTH, "uid %i failed to obtain auth", transaction->priv->uid);
		goto out;
	}

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		egg_warning ("Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* log success too */
	pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_AUTH, "uid %i obtained auth", transaction->priv->uid);
out:
	if (result != NULL)
		g_object_unref (result);
	return;
}

/**
 * pk_transaction_role_to_action_only_trusted:
 **/
static const gchar *
pk_transaction_role_to_action_only_trusted (PkRoleEnum role)
{
	const gchar *policy = NULL;

	switch (role) {
		case PK_ROLE_ENUM_UPDATE_PACKAGES:
		case PK_ROLE_ENUM_UPDATE_SYSTEM:
			policy = "org.freedesktop.packagekit.system-update";
			break;
		case PK_ROLE_ENUM_INSTALL_SIGNATURE:
			policy = "org.freedesktop.packagekit.system-trust-signing-key";
			break;
		case PK_ROLE_ENUM_ROLLBACK:
			policy = "org.freedesktop.packagekit.system-rollback";
			break;
		case PK_ROLE_ENUM_REPO_ENABLE:
		case PK_ROLE_ENUM_REPO_SET_DATA:
			policy = "org.freedesktop.packagekit.system-sources-configure";
			break;
		case PK_ROLE_ENUM_REFRESH_CACHE:
			policy = "org.freedesktop.packagekit.system-sources-refresh";
			break;
		case PK_ROLE_ENUM_REMOVE_PACKAGES:
			policy = "org.freedesktop.packagekit.package-remove";
			break;
		case PK_ROLE_ENUM_INSTALL_PACKAGES:
			policy = "org.freedesktop.packagekit.package-install";
			break;
		case PK_ROLE_ENUM_INSTALL_FILES:
			policy = "org.freedesktop.packagekit.package-install";
			break;
		case PK_ROLE_ENUM_ACCEPT_EULA:
			policy = "org.freedesktop.packagekit.package-eula-accept";
			break;
		case PK_ROLE_ENUM_CANCEL:
			policy = "org.freedesktop.packagekit.cancel-foreign";
			break;
		default:
			break;
	}
	return policy;
}

/**
 * pk_transaction_role_to_action_allow_untrusted:
 **/
static const gchar *
pk_transaction_role_to_action_allow_untrusted (PkRoleEnum role)
{
	const gchar *policy = NULL;

	switch (role) {
		case PK_ROLE_ENUM_INSTALL_PACKAGES:
		case PK_ROLE_ENUM_INSTALL_FILES:
		case PK_ROLE_ENUM_UPDATE_PACKAGES:
		case PK_ROLE_ENUM_UPDATE_SYSTEM:
			policy = "org.freedesktop.packagekit.package-install-untrusted";
			break;
		default:
			policy = pk_transaction_role_to_action_only_trusted (role);
	}
	return policy;
}

/**
 * pk_transaction_obtain_authorization:
 *
 * Only valid from an async caller, which is fine, as we won't prompt the user
 * when not async.
 *
 * Because checking for authentication might have to respond to user input, this
 * is treated as async. As such, the transaction should only be added to the
 * transaction list when authorised, and not before.
 **/
static gboolean
pk_transaction_obtain_authorization (PkTransaction *transaction, gboolean only_trusted, PkRoleEnum role, GError **error)
{
	PolkitDetails *details;
	const gchar *action_id;
	gboolean ret = FALSE;

	g_return_val_if_fail (transaction->priv->sender != NULL, FALSE);

	/* we should always have subject */
	if (transaction->priv->subject == NULL) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
				      "subject %s not found", transaction->priv->sender);
		goto out;
	}

	/* map the roles to policykit rules */
	if (only_trusted)
		action_id = pk_transaction_role_to_action_only_trusted (role);
	else
		action_id = pk_transaction_role_to_action_allow_untrusted (role);
	if (action_id == NULL) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_REFUSED_BY_POLICY, "policykit type required for '%s'", pk_role_enum_to_text (role));
		goto out;
	}

	/* log */
	pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_AUTH, "uid %i is trying to obtain %s auth (only_trusted:%i)", transaction->priv->uid, action_id, only_trusted);

	/* emit status for GUIs */
	pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_WAITING_FOR_AUTH);

	/* check subject */
	transaction->priv->waiting_for_auth = TRUE;

	/* insert details about the authorization */
	details = polkit_details_new ();
	polkit_details_insert (details, "role", pk_role_enum_to_text (transaction->priv->role));
	polkit_details_insert (details, "only-trusted", transaction->priv->cached_only_trusted ? "true" : "false");
	if (transaction->priv->cmdline != NULL)
		polkit_details_insert (details, "cmdline", transaction->priv->cmdline);

	/* do authorization async */
	polkit_authority_check_authorization (transaction->priv->authority,
					      transaction->priv->subject,
					      action_id,
					      details,
					      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
					      transaction->priv->cancellable,
					      (GAsyncReadyCallback) pk_transaction_action_obtain_authorization_finished_cb,
					      transaction);

	/* check_authorization ref's this */
	g_object_unref (details);

	/* assume success, as this is async */
	ret = TRUE;
out:
	return ret;
}

#else
/**
 * pk_transaction_obtain_authorization:
 **/
static gboolean
pk_transaction_obtain_authorization (PkTransaction *transaction, gboolean only_trusted, PkRoleEnum role, GError **error)
{
	gboolean ret;

	egg_warning ("*** THERE IS NO SECURITY MODEL BEING USED!!! ***");

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		egg_warning ("Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
	}

	return ret;
}
#endif

/**
 * pk_transaction_priv_get_role:
 **/
PkRoleEnum
pk_transaction_priv_get_role (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	return transaction->priv->role;
}

/**
 * pk_transaction_verify_sender:
 *
 * Verify caller of this method matches the one that got the Tid
 **/
static gboolean
pk_transaction_verify_sender (PkTransaction *transaction, DBusGMethodInvocation *context, GError **error)
{
	gboolean ret = TRUE;
	gchar *sender = NULL;

	g_return_val_if_fail (transaction->priv->sender != NULL, FALSE);

	/* not set inside the test suite */
	if (context == NULL)
		goto out;

	/* check is the same as the sender that did GetTid */
	sender = dbus_g_method_get_sender (context);
	ret = egg_strequal (transaction->priv->sender, sender);
	if (!ret) {
		*error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
				      "sender does not match (%s vs %s)", sender, transaction->priv->sender);
		goto out;
	}
out:
	g_free (sender);
	return ret;
}

/**
 * pk_transaction_dbus_return_error:
 **/
static void
pk_transaction_dbus_return_error (DBusGMethodInvocation *context, GError *error)
{
	/* not set inside the test suite */
	if (context == NULL) {
		egg_warning ("context null, and error: %s", error->message);
		g_error_free (error);
		return;
	}
	dbus_g_method_return_error (context, error);
}

/**
 * pk_transaction_dbus_return:
 **/
static void
pk_transaction_dbus_return (DBusGMethodInvocation *context)
{
	/* not set inside the test suite */
	if (context == NULL)
		return;
	dbus_g_method_return (context);
}

/**
 * pk_transaction_accept_eula:
 *
 * This should be called when a eula_id needs to be added into an internal db.
 **/
void
pk_transaction_accept_eula (PkTransaction *transaction, const gchar *eula_id, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (eula_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_ACCEPT_EULA, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	egg_debug ("AcceptEula method called: %s", eula_id);
	ret = pk_backend_accept_eula (transaction->priv->backend, eula_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "EULA failed to be added");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* we are done */
	g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_cancel:
 **/
void
pk_transaction_cancel (PkTransaction *transaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *sender;
	guint uid;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("Cancel method called on %s", transaction->priv->tid);

	/* not implemented yet */
	if (transaction->priv->backend->desc->cancel == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Cancel not yet supported by backend");
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* if it's finished, cancelling will have no action regardless of uid */
	if (transaction->priv->finished) {
		egg_debug ("No point trying to cancel a finished transaction, ignoring");
		goto out;
	}

	/* check to see if we have an action */
	if (transaction->priv->role == PK_ROLE_ENUM_UNKNOWN) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_ROLE, "No role");
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if it's safe to kill */
	if (!transaction->priv->allow_cancel) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_CANNOT_CANCEL,
				     "Tried to cancel %s (%s) that is not safe to kill",
				     transaction->priv->tid,
				     pk_role_enum_to_text (transaction->priv->role));
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if we saved the uid */
	if (transaction->priv->uid == PK_TRANSACTION_UID_INVALID) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_CANNOT_CANCEL,
				     "No context from caller to get UID from");
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* get the UID of the caller */
	sender = dbus_g_method_get_sender (context);
	uid = pk_transaction_get_uid (transaction, sender);
	g_free (sender);

	/* check we got a valid value */
	if (uid == PK_TRANSACTION_UID_INVALID) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_STATE, "unable to get uid of caller");
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the caller uid with the originator uid */
	if (transaction->priv->uid != uid) {
		egg_debug ("uid does not match (%i vs. %i)", transaction->priv->uid, uid);
		ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_CANCEL, &error);
		if (!ret) {
			pk_transaction_dbus_return_error (context, error);
			return;
		}
	}

	/* if it's never been run, just remove this transaction from the list */
	if (!transaction->priv->has_been_run) {
		pk_transaction_progress_changed_emit (transaction, 100, 100, 0, 0);
		pk_transaction_allow_cancel_emit (transaction, FALSE);
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_CANCELLED, 0);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* set the state, as cancelling might take a few seconds */
	pk_backend_set_status (transaction->priv->backend, PK_STATUS_ENUM_CANCEL);

	/* we don't want to cancel twice */
	pk_backend_set_allow_cancel (transaction->priv->backend, FALSE);

	/* we need ::finished to not return success or failed */
	pk_backend_set_exit_code (transaction->priv->backend, PK_EXIT_ENUM_CANCELLED);

	/* actually run the method */
	transaction->priv->backend->desc->cancel (transaction->priv->backend);

out:
	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_download_packages:
 **/
void
pk_transaction_download_packages (PkTransaction *transaction, gchar **package_ids, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;
	gchar *directory = NULL;
	gint retval;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("DownloadPackages method called: %s", package_ids[0]);

	/* not implemented yet */
	if (transaction->priv->backend->desc->download_packages == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "DownloadPackages not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		goto out;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_dbus_return_error (context, error);
		goto out;
	}

	/* create cache directory */
	directory = g_build_filename (LOCALSTATEDIR, "cache", "PackageKit",
				     "downloads", transaction->priv->tid, NULL);
	/* rwxrwxr-x */
	retval = g_mkdir (directory, 0775);
	if (retval != 0) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_DENIED,
				     "cannot create %s", directory);
		pk_transaction_dbus_return_error (context, error);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_directory = g_strdup (directory);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_DOWNLOAD_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		goto out;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
out:
	g_free (directory);
}

/**
 * pk_transaction_get_allow_cancel:
 **/
gboolean
pk_transaction_get_allow_cancel (PkTransaction *transaction, gboolean *allow_cancel, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	/* we do not need to get the context and check the uid */
	egg_debug ("GetAllowCancel method called");
	*allow_cancel = transaction->priv->allow_cancel;
	return TRUE;
}

/**
 * pk_transaction_get_categories:
 **/
void
pk_transaction_get_categories (PkTransaction *transaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("GetCategories method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_categories == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetCategories not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* are we already performing an update? */
	if (pk_transaction_list_role_present (transaction->priv->transaction_list, PK_ROLE_ENUM_GET_CATEGORIES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
				     "Already performing get categories");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_CATEGORIES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	dbus_g_method_return (context);
}

/**
 * pk_transaction_get_depends:
 **/
void
pk_transaction_get_depends (PkTransaction *transaction, const gchar *filter, gchar **package_ids,
			    gboolean recursive, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("GetDepends method called: %s (recursive %i)", package_ids_temp, recursive);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_depends == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetDepends not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_force = recursive;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DEPENDS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_get_details:
 **/
void
pk_transaction_get_details (PkTransaction *transaction, gchar **package_ids, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("GetDetails method called: %s", package_ids_temp);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_details == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetDetails not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DETAILS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_get_distro_upgrades:
 **/
void
pk_transaction_get_distro_upgrades (PkTransaction *transaction, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("GetDistroUpgrades method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_distro_upgrades == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetDistroUpgrades not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	if (context != NULL) {
		/* not set inside the test suite */
		dbus_g_method_return (context);
	}
}

/**
 * pk_transaction_get_files:
 **/
void
pk_transaction_get_files (PkTransaction *transaction, gchar **package_ids, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("GetFiles method called: %s", package_ids_temp);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_files == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetFiles not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_FILES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	egg_debug ("GetPackages method called: %s", filter);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_packages == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetPackages not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_get_old_transactions:
 **/
gboolean
pk_transaction_get_old_transactions (PkTransaction *transaction, guint number, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	egg_debug ("GetOldTransactions method called");

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_OLD_TRANSACTIONS);
	pk_transaction_db_get_list (transaction->priv->transaction_db, number);
	g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);

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

	egg_debug ("GetPackageLast method called");

	if (transaction->priv->last_package_id == NULL) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_STATE,
			     "No package data available");
		return FALSE;
	}
	*package_id = g_strdup (transaction->priv->last_package_id);
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
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	egg_debug ("GetProgress method called, using cached values");
	*percentage = transaction->priv->percentage;
	*subpercentage = transaction->priv->subpercentage;
	*elapsed = transaction->priv->elapsed;
	*remaining = transaction->priv->remaining;

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

	egg_debug ("GetRepoList method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_repo_list == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetRepoList not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REPO_LIST);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_get_requires:
 **/
void
pk_transaction_get_requires (PkTransaction *transaction, const gchar *filter, gchar **package_ids,
			     gboolean recursive, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("GetRequires method called: %s (recursive %i)", package_ids_temp, recursive);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_requires == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetRequires not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_force = recursive;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REQUIRES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_get_role:
 **/
gboolean
pk_transaction_get_role (PkTransaction *transaction,
			 const gchar **role, const gchar **text, GError **error)
{
	gchar *text_temp;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	egg_debug ("GetRole method called");

	/* we might not have this set yet */
	if (transaction->priv->tid == NULL) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_SUCH_TRANSACTION, "Role not set");
		return FALSE;
	}

	text_temp = pk_transaction_get_text (transaction);
	*role = g_strdup (pk_role_enum_to_text (transaction->priv->role));
	*text = g_strdup (text_temp);
	g_free (text_temp);
	return TRUE;
}

/**
 * pk_transaction_get_status:
 **/
gboolean
pk_transaction_get_status (PkTransaction *transaction, const gchar **status, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	egg_debug ("GetStatus method called");

	*status = g_strdup (pk_status_enum_to_text (transaction->priv->status));
	return TRUE;
}

/**
 * pk_transaction_get_update_detail:
 **/
void
pk_transaction_get_update_detail (PkTransaction *transaction, gchar **package_ids,
				  DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("GetUpdateDetail method called: %s", package_ids_temp);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_update_detail == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetUpdateDetail not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATE_DETAIL);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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
	gchar *package_id;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("GetUpdates method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->get_updates == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetUpdates not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATES);

	/* try and reuse cache */
	updates_cache = pk_cache_get_updates (transaction->priv->cache);
	if (updates_cache != NULL) {
		const PkPackageObj *obj;
		const gchar *info_text;
		guint i;
		guint length;

		length = pk_package_list_get_size (updates_cache);
		egg_debug ("we have cached data (%i) we should use!", length);

		/* emulate the backend */
		for (i=0; i<length; i++) {
			obj = pk_package_list_get_obj (updates_cache, i);
			info_text = pk_info_enum_to_text (obj->info);
			package_id = pk_package_id_to_string (obj->id);
			g_signal_emit (transaction, signals [PK_TRANSACTION_PACKAGE], 0,
				       info_text, package_id, obj->summary);
			g_free (package_id);
		}

		/* set finished */
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);

		/* we are done */
		g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);

		/* not set inside the test suite */
		if (context != NULL)
			dbus_g_method_return (context);
		return;
	}

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_get_content_type_for_file:
 **/
static gchar *
pk_transaction_get_content_type_for_file (const gchar *filename, GError **error)
{
	GError *error_local = NULL;
	GFile *file;
	GFileInfo *info;
	gchar *content_type = NULL;

	/* get file info synchronously */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file, "standard::content-type", G_FILE_QUERY_INFO_NONE, NULL, &error_local);
	if (info == NULL) {
		*error = g_error_new (1, 0, "failed to get file attributes for %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get content type as string */
	content_type = g_file_info_get_attribute_as_string (info, "standard::content-type");
out:
	if (info != NULL)
		g_object_unref (info);
	g_object_unref (file);
	return content_type;
}

/**
 * pk_transaction_is_supported_content_type:
 **/
static gboolean
pk_transaction_is_supported_content_type (PkTransaction *transaction, const gchar *content_type)
{
	guint i;
	gboolean ret = FALSE;
	gchar *mime_types_str;
	gchar **mime_types;

	/* get list of mime types supported by backends */
	mime_types_str = pk_backend_get_mime_types (transaction->priv->backend);
	mime_types = g_strsplit (mime_types_str, ";", -1);

	/* can we support this one? */
	for (i=0; mime_types[i] != NULL; i++) {
		if (g_strcmp0 (mime_types[i], content_type) == 0) {
			ret = TRUE;
			break;
		}
	}

	g_free (mime_types_str);
	g_strfreev (mime_types);
	return ret;
}

/**
 * pk_transaction_install_files:
 **/
void
pk_transaction_install_files (PkTransaction *transaction, gboolean only_trusted,
			      gchar **full_paths, DBusGMethodInvocation *context)
{
	gchar *full_paths_temp;
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	PkServicePack *service_pack;
	gchar *content_type;
	guint length;
	guint i;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	full_paths_temp = pk_package_ids_to_text (full_paths);
	egg_debug ("InstallFiles method called: %s (only_trusted %i)", full_paths_temp, only_trusted);
	g_free (full_paths_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->install_files == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "InstallFiles not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check all files exists and are valid */
	length = g_strv_length (full_paths);

	for (i=0; i<length; i++) {
		/* exists */
		ret = g_file_test (full_paths[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_SUCH_FILE,
					     "No such file %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
			pk_transaction_dbus_return_error (context, error);
			return;
		}

		/* get content type */
		content_type = pk_transaction_get_content_type_for_file (full_paths[i], &error_local);
		if (content_type == NULL) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Failed to get content type for file %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
			pk_transaction_dbus_return_error (context, error);
			return;
		}

		/* supported content type? */
		ret = pk_transaction_is_supported_content_type (transaction, content_type);
		g_free (content_type);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_MIME_TYPE_NOT_SUPPORTED,
					     "MIME type not supported %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
			pk_transaction_dbus_return_error (context, error);
			return;
		}

		/* valid */
		if (g_str_has_suffix (full_paths[i], ".servicepack")) {
			service_pack = pk_service_pack_new ();
			pk_service_pack_set_filename (service_pack, full_paths[i]);
			ret = pk_service_pack_check_valid (service_pack, &error_local);
			g_object_unref (service_pack);
			if (!ret) {
				error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACK_INVALID, "%s", error_local->message);
				pk_transaction_release_tid (transaction);
				pk_transaction_dbus_return_error (context, error);
				g_error_free (error_local);
				return;
			}
		}
	}

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_INSTALL_FILES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_FILES);

	/* return from async with success */
	pk_transaction_dbus_return (context);
	return;
}

/**
 * pk_transaction_install_packages:
 **/
void
pk_transaction_install_packages (PkTransaction *transaction, gboolean only_trusted,
				 gchar **package_ids, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("InstallPackages method called: %s", package_ids_temp);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->install_packages == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "InstallPackages not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_PACKAGES);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_INSTALL_PACKAGES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("InstallSignature method called: %s, %s", key_id, package_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->install_signature == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "InstallSignature not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (key_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_id (';;;repo-id' is used for the repo key) */
	ret = pk_package_id_check (package_id);
	if (!ret && !g_str_has_prefix (package_id, ";;;")) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->cached_key_id = g_strdup (key_id);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_SIGNATURE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_INSTALL_SIGNATURE, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_is_caller_active:
 **/
gboolean
pk_transaction_is_caller_active (PkTransaction *transaction, gboolean *is_active, GError **error)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (transaction->priv->tid != NULL, FALSE);

	egg_debug ("is caller active");

	*is_active = egg_dbus_monitor_is_connected (transaction->priv->monitor);
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

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("RefreshCache method called: %i", force);

	/* not implemented yet */
	if (transaction->priv->backend->desc->refresh_cache == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RefreshCache not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* we unref the update cache if it exists */
	pk_cache_invalidate (transaction->priv->cache);

	/* save so we can run later */
	transaction->priv->cached_force = force;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REFRESH_CACHE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_REFRESH_CACHE, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_remove_packages:
 **/
void
pk_transaction_remove_packages (PkTransaction *transaction, gchar **package_ids,
				gboolean allow_deps, gboolean autoremove,
				DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("RemovePackages method called: %s, %i, %i", package_ids_temp, allow_deps, autoremove);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->remove_packages == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RemovePackages not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_allow_deps = allow_deps;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REMOVE_PACKAGES);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_REMOVE_PACKAGES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("RepoEnable method called: %s, %i", repo_id, enabled);

	/* not implemented yet */
	if (transaction->priv->backend->desc->repo_enable == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RepoEnable not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (repo_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_enabled = enabled;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_ENABLE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_REPO_ENABLE, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("RepoSetData method called: %s, %s, %s", repo_id, parameter, value);

	/* not implemented yet */
	if (transaction->priv->backend->desc->repo_set_data == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RepoSetData not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (repo_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_parameter = g_strdup (parameter);
	transaction->priv->cached_value = g_strdup (value);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_SET_DATA);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_REPO_SET_DATA, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_resolve:
 **/
void
pk_transaction_resolve (PkTransaction *transaction, const gchar *filter,
			gchar **packages, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *packages_temp;
	guint i;
	guint length;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	packages_temp = pk_package_ids_to_text (packages);
	egg_debug ("Resolve method called: %s, %s", filter, packages_temp);
	g_free (packages_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->resolve == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Resolve not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check for sanity */
	length = g_strv_length (packages);
	for (i=0; i<length; i++) {
		ret = pk_strvalidate (packages[i]);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
					     "Invalid input passed to daemon");
			pk_transaction_release_tid (transaction);
			pk_transaction_dbus_return_error (context, error);
			return;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (packages);
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_RESOLVE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("Rollback method called: %s", transaction_id);

	/* not implemented yet */
	if (transaction->priv->backend->desc->rollback == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Rollback not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check for sanity */
	ret = pk_strvalidate (transaction_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_id = g_strdup (transaction_id);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_ROLLBACK);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, FALSE, PK_ROLE_ENUM_ROLLBACK, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	egg_debug ("SearchDetails method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_details == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchDetails not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_DETAILS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	egg_debug ("SearchFile method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_file == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchFile not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* when not an absolute path, disallow slashes in search */
	if (search[0] != '/' && strstr (search, "/") != NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_PATH_INVALID,
				     "Invalid search path");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_FILE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	egg_debug ("SearchGroup method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_group == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchGroup not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* do not allow spaces */
	if (strstr (search, " ") != NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing spaces");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_GROUP);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	egg_debug ("SearchName method called: %s, %s", filter, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->search_name == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchName not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_NAME);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_set_locale:
 */
void
pk_transaction_set_locale (PkTransaction *transaction, const gchar *code, DBusGMethodInvocation *context)
{
	GError *error;
	gboolean ret;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* already set? */
	if (transaction->priv->locale != NULL) {
		egg_warning ("Already set locale");
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Already set locale to %s", transaction->priv->locale);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can pass to the backend */
	transaction->priv->locale = g_strdup (code);

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_update_packages:
 **/
void
pk_transaction_update_packages (PkTransaction *transaction, gboolean only_trusted, gchar **package_ids, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	gchar *package_ids_temp;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	package_ids_temp = pk_package_ids_to_text (package_ids);
	egg_debug ("UpdatePackages method called: %s", package_ids_temp);
	g_free (package_ids_temp);

	/* not implemented yet */
	if (transaction->priv->backend->desc->update_packages == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "UpdatePackages not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_PACKAGES);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_UPDATE_PACKAGES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_update_system:
 **/
void
pk_transaction_update_system (PkTransaction *transaction, gboolean only_trusted, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	egg_debug ("UpdateSystem method called");

	/* not implemented yet */
	if (transaction->priv->backend->desc->update_system == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "UpdateSystem not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* are we already performing an update? */
	if (pk_transaction_list_role_present (transaction->priv->transaction_list, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
				     "Already performing system update");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	transaction->priv->cached_only_trusted = only_trusted;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_SYSTEM);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_UPDATE_SYSTEM, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
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

	egg_debug ("WhatProvides method called: %s, %s", type, search);

	/* not implemented yet */
	if (transaction->priv->backend->desc->what_provides == NULL) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "WhatProvides not yet supported by backend");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check if the sender is the same */
	ret = pk_transaction_verify_sender (transaction, context, &error);
	if (!ret) {
		/* don't release tid */
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the search term */
	ret = pk_transaction_search_check (search, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* check provides */
	provides = pk_provides_enum_from_text (type);
	if (provides == PK_PROVIDES_ENUM_UNKNOWN) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_PROVIDE,
				     "provide type '%s' not found", type);
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_text (filter);
	transaction->priv->cached_search = g_strdup (search);
	transaction->priv->cached_provides = provides;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_WHAT_PROVIDES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		pk_transaction_dbus_return_error (context, error);
		return;
	}

	/* return from async with success */
	pk_transaction_dbus_return (context);
}

/**
 * pk_transaction_class_init:
 * @klass: The PkTransactionClass
 **/
static void
pk_transaction_class_init (PkTransactionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = pk_transaction_dispose;
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
	signals [PK_TRANSACTION_DETAILS] =
		g_signal_new ("details",
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
	signals [PK_TRANSACTION_CATEGORY] =
		g_signal_new ("category",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_DISTRO_UPGRADE] =
		g_signal_new ("distro-upgrade",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
	signals [PK_TRANSACTION_MEDIA_CHANGE_REQUIRED] =
		g_signal_new ("media-change-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_STRING_UINT_STRING_UINT_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT,
			      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TRANSACTION_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 12, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TRANSACTION_DESTROY] =
		g_signal_new ("destroy",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkTransactionPrivate));
}

/**
 * pk_transaction_init:
 * @transaction: This class instance
 **/
static void
pk_transaction_init (PkTransaction *transaction)
{
	GError *error = NULL;

	transaction->priv = PK_TRANSACTION_GET_PRIVATE (transaction);
	transaction->priv->finished = FALSE;
	transaction->priv->running = FALSE;
	transaction->priv->has_been_run = FALSE;
	transaction->priv->waiting_for_auth = FALSE;
	transaction->priv->allow_cancel = TRUE;
	transaction->priv->emit_eula_required = FALSE;
	transaction->priv->emit_signature_required = FALSE;
	transaction->priv->emit_media_change_required = FALSE;
	transaction->priv->cached_enabled = FALSE;
	transaction->priv->cached_only_trusted = TRUE;
	transaction->priv->cached_key_id = NULL;
	transaction->priv->cached_package_id = NULL;
	transaction->priv->cached_package_ids = NULL;
	transaction->priv->cached_transaction_id = NULL;
	transaction->priv->cached_full_path = NULL;
	transaction->priv->cached_full_paths = NULL;
	transaction->priv->cached_filters = PK_FILTER_ENUM_NONE;
	transaction->priv->cached_search = NULL;
	transaction->priv->cached_repo_id = NULL;
	transaction->priv->cached_parameter = NULL;
	transaction->priv->cached_value = NULL;
	transaction->priv->last_package_id = NULL;
	transaction->priv->tid = NULL;
	transaction->priv->sender = NULL;
	transaction->priv->locale = NULL;
#ifdef USE_SECURITY_POLKIT
	transaction->priv->subject = NULL;
#endif
	transaction->priv->cmdline = NULL;
	transaction->priv->uid = PK_TRANSACTION_UID_INVALID;
	transaction->priv->role = PK_ROLE_ENUM_UNKNOWN;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	transaction->priv->percentage = PK_BACKEND_PERCENTAGE_INVALID;
	transaction->priv->subpercentage = PK_BACKEND_PERCENTAGE_INVALID;
	transaction->priv->elapsed = 0;
	transaction->priv->remaining = 0;
	transaction->priv->require_restart_list = g_ptr_array_new ();
	transaction->priv->backend = pk_backend_new ();
	transaction->priv->cache = pk_cache_new ();
	transaction->priv->conf = pk_conf_new ();
	transaction->priv->notify = pk_notify_new ();
	transaction->priv->inhibit = pk_inhibit_new ();
	transaction->priv->package_list = pk_package_list_new ();
	transaction->priv->transaction_list = pk_transaction_list_new ();
	transaction->priv->syslog = pk_syslog_new ();
#ifdef USE_SECURITY_POLKIT
	transaction->priv->authority = polkit_authority_get ();
	transaction->priv->cancellable = g_cancellable_new ();
#endif

	transaction->priv->transaction_extra = pk_transaction_extra_new ();
	g_signal_connect (transaction->priv->transaction_extra, "status-changed",
			  G_CALLBACK (pk_transaction_status_changed_cb), transaction);
	g_signal_connect (transaction->priv->transaction_extra, "progress-changed",
			  G_CALLBACK (pk_transaction_progress_changed_cb), transaction);
	g_signal_connect (transaction->priv->transaction_extra, "require-restart",
			  G_CALLBACK (pk_transaction_require_restart_cb), transaction);

	transaction->priv->transaction_db = pk_transaction_db_new ();
	g_signal_connect (transaction->priv->transaction_db, "transaction",
			  G_CALLBACK (pk_transaction_transaction_cb), transaction);

	transaction->priv->monitor = egg_dbus_monitor_new ();
	g_signal_connect (transaction->priv->monitor, "connection-changed",
			  G_CALLBACK (pk_transaction_caller_active_changed_cb), transaction);

	/* connect to DBus so we can get the pid */
	transaction->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	transaction->priv->proxy_pid = dbus_g_proxy_new_for_name_owner (transaction->priv->connection,
									"org.freedesktop.DBus",
									"/org/freedesktop/DBus/Bus",
									"org.freedesktop.DBus", &error);
	if (transaction->priv->proxy_pid == NULL) {
		egg_warning ("cannot connect to DBus: %s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_transaction_dispose:
 **/
static void
pk_transaction_dispose (GObject *object)
{
	PkTransaction *transaction;

	g_return_if_fail (PK_IS_TRANSACTION (object));

	transaction = PK_TRANSACTION (object);

	/* remove any inhibit, it's okay to call this function when it's not needed */
	pk_inhibit_remove (transaction->priv->inhibit, transaction);

	/* were we waiting for the client to authorise */
	if (transaction->priv->waiting_for_auth) {
#ifdef USE_SECURITY_POLKIT
		g_cancellable_cancel (transaction->priv->cancellable);
#endif
		/* emit an ::ErrorCode() and then ::Finished() */
		pk_transaction_error_code_emit (transaction, PK_ERROR_ENUM_NOT_AUTHORIZED, "client did not authorize action");
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_FAILED, 0);
	}

	/* send signal to clients that we are about to be destroyed */
	egg_debug ("emitting destroy %s", transaction->priv->tid);
	g_signal_emit (transaction, signals [PK_TRANSACTION_DESTROY], 0);

	G_OBJECT_CLASS (pk_transaction_parent_class)->dispose (object);
}

/**
 * pk_transaction_finalize:
 **/
static void
pk_transaction_finalize (GObject *object)
{
	PkTransaction *transaction;

	g_return_if_fail (PK_IS_TRANSACTION (object));

	transaction = PK_TRANSACTION (object);

#ifdef USE_SECURITY_POLKIT
	if (transaction->priv->subject != NULL)
		g_object_unref (transaction->priv->subject);
#endif

	g_ptr_array_foreach (transaction->priv->require_restart_list, (GFunc) g_free, NULL);
	g_ptr_array_free (transaction->priv->require_restart_list, TRUE);

	g_free (transaction->priv->last_package_id);
	g_free (transaction->priv->locale);
	g_free (transaction->priv->cached_package_id);
	g_free (transaction->priv->cached_key_id);
	g_strfreev (transaction->priv->cached_package_ids);
	g_free (transaction->priv->cached_transaction_id);
	g_free (transaction->priv->cached_directory);
	g_free (transaction->priv->cached_search);
	g_free (transaction->priv->cached_repo_id);
	g_free (transaction->priv->cached_parameter);
	g_free (transaction->priv->cached_value);
	g_free (transaction->priv->tid);
	g_free (transaction->priv->sender);
	g_free (transaction->priv->cmdline);

	g_object_unref (transaction->priv->conf);
	g_object_unref (transaction->priv->cache);
	g_object_unref (transaction->priv->inhibit);
	g_object_unref (transaction->priv->backend);
	g_object_unref (transaction->priv->monitor);
	g_object_unref (transaction->priv->package_list);
	g_object_unref (transaction->priv->transaction_list);
	g_object_unref (transaction->priv->transaction_db);
	g_object_unref (transaction->priv->proxy_pid);
	g_object_unref (transaction->priv->notify);
	g_object_unref (transaction->priv->syslog);
	g_object_unref (transaction->priv->transaction_extra);
#ifdef USE_SECURITY_POLKIT
//	g_object_unref (transaction->priv->authority);
	g_object_unref (transaction->priv->cancellable);
#endif

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
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_transaction (EggTest *test)
{
	PkTransaction *transaction = NULL;
	gboolean ret;
	const gchar *temp;
	GError *error = NULL;
#ifdef USE_SECURITY_POLKIT
	const gchar *action;
#endif

	if (!egg_test_start (test, "PkTransaction"))
		return;

	/************************************************************/
	egg_test_title (test, "get PkTransaction object");
	transaction = pk_transaction_new ();
	egg_test_assert (test, transaction != NULL);

	/************************************************************
	 ****************         MAP ROLES        ******************
	 ************************************************************/
#ifdef USE_SECURITY_POLKIT
	egg_test_title (test, "map valid role to action");
	action = pk_transaction_role_to_action_only_trusted (PK_ROLE_ENUM_UPDATE_PACKAGES);
	if (egg_strequal (action, "org.freedesktop.packagekit.system-update"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not get correct action '%s'", action);

	/************************************************************/
	egg_test_title (test, "map invalid role to action");
	action = pk_transaction_role_to_action_only_trusted (PK_ROLE_ENUM_SEARCH_NAME);
	if (action == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "did not get correct action '%s'", action);
#endif

	/************************************************************
	 ****************          FILTERS         ******************
	 ************************************************************/
	temp = NULL;
	egg_test_title (test, "test a fail filter (null)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, !ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "";
	egg_test_title (test, "test a fail filter ()");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, !ret);
	g_clear_error (&error);

	/************************************************************/
	temp = ";";
	egg_test_title (test, "test a fail filter (;)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, !ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "moo";
	egg_test_title (test, "test a fail filter (invalid)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, !ret);

	g_clear_error (&error);

	/************************************************************/
	temp = "moo;foo";
	egg_test_title (test, "test a fail filter (invalid, multiple)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, !ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "gui;;";
	egg_test_title (test, "test a fail filter (valid then zero length)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, !ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "none";
	egg_test_title (test, "test a pass filter (none)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "gui";
	egg_test_title (test, "test a pass filter (single)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "devel;~gui";
	egg_test_title (test, "test a pass filter (multiple)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, ret);
	g_clear_error (&error);

	/************************************************************/
	temp = "~gui;~installed";
	egg_test_title (test, "test a pass filter (multiple2)");
	ret = pk_transaction_filter_check (temp, &error);
	egg_test_assert (test, ret);
	g_clear_error (&error);

	g_object_unref (transaction);

	egg_test_end (test);
}
#endif

