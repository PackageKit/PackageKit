/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <gio/gio.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-service-pack.h>
#ifdef USE_SECURITY_POLKIT
#include <polkit/polkit.h>
#endif

#include "pk-backend.h"
#include "pk-cache.h"
#include "pk-conf.h"
#include "pk-dbus.h"
#include "pk-marshal.h"
#include "pk-notify.h"
#include "pk-plugin.h"
#include "pk-shared.h"
#include "pk-syslog.h"
#include "pk-transaction-db.h"
#include "pk-transaction.h"
#include "pk-transaction-private.h"
#include "pk-transaction-list.h"

static void     pk_transaction_finalize		(GObject	    *object);
static void     pk_transaction_dispose		(GObject	    *object);

#define PK_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION, PkTransactionPrivate))
#define PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT	100 /* ms */

/* when the UID is invalid or not known */
#define PK_TRANSACTION_UID_INVALID		G_MAXUINT

static void pk_transaction_status_changed_cb (PkBackend *backend, PkStatusEnum status, PkTransaction *transaction);

struct PkTransactionPrivate
{
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	PkTransactionState	 state;
	guint			 percentage;
	guint			 subpercentage;
	guint			 elapsed_time;
	guint			 remaining_time;
	guint			 speed;
	gboolean		 finished;
	gboolean		 allow_cancel;
	gboolean		 waiting_for_auth;
	gboolean		 emit_eula_required;
	gboolean		 emit_signature_required;
	gboolean		 emit_media_change_required;
	gboolean		 caller_active;
	PkHintEnum		 background;
	PkHintEnum		 interactive;
	gchar			*locale;
	gchar			*frontend_socket;
	guint			 cache_age;
	guint			 uid;
	guint			 watch_id;
	PkBackend		*backend;
	PkCache			*cache;
	PkConf			*conf;
	PkNotify		*notify;
	PkDbus			*dbus;
#ifdef USE_SECURITY_POLKIT
	PolkitAuthority		*authority;
	PolkitSubject		*subject;
	GCancellable		*cancellable;
#endif
	PkSyslog		*syslog;

	/* needed for gui coldplugging */
	gchar			*last_package_id;
	gchar			*tid;
	gchar			*sender;
	gchar			*cmdline;
	PkResults		*results;
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
	gchar			**cached_full_paths;
	PkBitfield		 cached_filters;
	gchar			**cached_values;
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
	guint			 signal_percentage;
	guint			 signal_subpercentage;
	guint			 signal_remaining;
	guint			 signal_repo_detail;
	guint			 signal_repo_signature_required;
	guint			 signal_eula_required;
	guint			 signal_media_change_required;
	guint			 signal_require_restart;
	guint			 signal_status_changed;
	guint			 signal_update_detail;
	guint			 signal_category;
	guint			 signal_speed;
	guint			 signal_item_progress;
	GPtrArray		*plugins;
	GPtrArray		*supported_content_types;
	guint			 registration_id;
	GDBusConnection		*connection;
	GDBusNodeInfo		*introspection;
};

typedef enum {
	PK_TRANSACTION_ERROR_DENIED,
	PK_TRANSACTION_ERROR_NOT_RUNNING,
	PK_TRANSACTION_ERROR_NO_ROLE,
	PK_TRANSACTION_ERROR_CANNOT_CANCEL,
	PK_TRANSACTION_ERROR_NOT_SUPPORTED,
	PK_TRANSACTION_ERROR_NO_SUCH_TRANSACTION,
	PK_TRANSACTION_ERROR_NO_SUCH_FILE,
	PK_TRANSACTION_ERROR_NO_SUCH_DIRECTORY,
	PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
	PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
	PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
	PK_TRANSACTION_ERROR_SEARCH_INVALID,
	PK_TRANSACTION_ERROR_SEARCH_PATH_INVALID,
	PK_TRANSACTION_ERROR_FILTER_INVALID,
	PK_TRANSACTION_ERROR_INPUT_INVALID,
	PK_TRANSACTION_ERROR_INVALID_STATE,
	PK_TRANSACTION_ERROR_INITIALIZE_FAILED,
	PK_TRANSACTION_ERROR_COMMIT_FAILED,
	PK_TRANSACTION_ERROR_INVALID_PROVIDE,
	PK_TRANSACTION_ERROR_PACK_INVALID,
	PK_TRANSACTION_ERROR_MIME_TYPE_NOT_SUPPORTED,
	PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
	PK_TRANSACTION_ERROR_LAST
} PkTransactionError;

enum {
	SIGNAL_FINISHED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

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
			ENUM_ENTRY (PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID, "NumberOfPackagesInvalid"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkTransactionError", values);
	}
	return etype;
}

/**
 * pk_transaction_set_plugins:
 */
void
pk_transaction_set_plugins (PkTransaction *transaction,
			    GPtrArray *plugins)
{
	transaction->priv->plugins = g_ptr_array_ref (plugins);
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
 * pk_transaction_finish_invalidate_caches:
 **/
static gboolean
pk_transaction_finish_invalidate_caches (PkTransaction *transaction)
{
	gchar *transaction_id;
	PkTransactionPrivate *priv = transaction->priv;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	g_object_get (priv->backend,
		      "transaction-id", &transaction_id,
		      NULL);
	if (transaction_id == NULL) {
		g_warning ("could not get current tid from backend");
		return FALSE;
	}

	/* copy this into the cache */
	pk_cache_set_results (priv->cache, priv->role, priv->results);

	/* could the update list have changed? */
	if (priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    priv->role == PK_ROLE_ENUM_REPO_SET_DATA ||
	    priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {

		/* the cached list is no longer valid */
		g_debug ("invalidating caches");
		pk_cache_invalidate (priv->cache);

		/* this needs to be done after a small delay */
		pk_notify_wait_updates_changed (priv->notify,
						PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT);
	}
	g_free (transaction_id);
	return TRUE;
}

/**
 * pk_transaction_emit_property_changed:
 **/
static void
pk_transaction_emit_property_changed (PkTransaction *transaction,
				      const gchar *property_name,
				      GVariant *property_value)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* build the dict */
	g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder,
			       "{sv}",
			       property_name,
			       property_value);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       g_variant_new ("(sa{sv}as)",
				       PK_DBUS_INTERFACE_TRANSACTION,
				       &builder,
				       &invalidated_builder),
				       NULL);
}

/**
 * pk_transaction_emit_changed:
 **/
static void
pk_transaction_emit_changed (PkTransaction *transaction)
{
	g_debug ("emitting changed");
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Changed",
				       NULL,
				       NULL);
}

/**
 * pk_transaction_progress_changed_emit:
 **/
static void
pk_transaction_progress_changed_emit (PkTransaction *transaction,
				     guint percentage,
				     guint subpercentage,
				     guint elapsed,
				     guint remaining)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	/* save so we can do GetProgress on a queued or finished transaction */
	transaction->priv->percentage = percentage;
	transaction->priv->subpercentage = subpercentage;
	transaction->priv->elapsed_time = elapsed;
	transaction->priv->remaining_time = remaining;

	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "Percentage",
					      g_variant_new_uint32 (percentage));
	pk_transaction_emit_property_changed (transaction,
					      "Subpercentage",
					      g_variant_new_uint32 (subpercentage));
	pk_transaction_emit_property_changed (transaction,
					      "ElapsedTime",
					      g_variant_new_uint32 (elapsed));
	pk_transaction_emit_property_changed (transaction,
					      "RemainingTime",
					      g_variant_new_uint32 (remaining));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_allow_cancel_emit:
 **/
static void
pk_transaction_allow_cancel_emit (PkTransaction *transaction, gboolean allow_cancel)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	/* already set */
	if (transaction->priv->allow_cancel == allow_cancel)
		return;

	transaction->priv->allow_cancel = allow_cancel;

	/* TODO: have master property on main interface */

	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "AllowCancel",
					      g_variant_new_boolean (allow_cancel));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_status_changed_emit:
 **/
static void
pk_transaction_status_changed_emit (PkTransaction *transaction, PkStatusEnum status)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* already set */
	if (transaction->priv->status == status)
		return;

	transaction->priv->status = status;

	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "Status",
					      g_variant_new_string (pk_status_enum_to_string (status)));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_finished_emit:
 **/
static void
pk_transaction_finished_emit (PkTransaction *transaction,
			      PkExitEnum exit_enum,
			      guint time_ms)
{
	const gchar *exit_text;
	exit_text = pk_exit_enum_to_string (exit_enum);
	g_debug ("emitting finished '%s', %i", exit_text, time_ms);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Finished",
				       g_variant_new ("(su)",
						      exit_text,
						      time_ms),
				       NULL);

	/* For the transaction list */
	g_signal_emit (transaction, signals[SIGNAL_FINISHED], 0);
}

/**
 * pk_transaction_error_code_emit:
 **/
static void
pk_transaction_error_code_emit (PkTransaction *transaction,
				PkErrorEnum error_enum,
				const gchar *details)
{
	const gchar *text;
	text = pk_error_enum_to_string (error_enum);
	g_debug ("emitting error-code %s, '%s'", text, details);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "ErrorCode",
				       g_variant_new ("(ss)",
						      text,
						      details),
				       NULL);
}

/**
 * pk_transaction_allow_cancel_cb:
 **/
static void
pk_transaction_allow_cancel_cb (PkBackend *backend,
				gboolean allow_cancel,
				PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("emitting allow-cancel %i", allow_cancel);
	pk_transaction_allow_cancel_emit (transaction, allow_cancel);
}

/**
 * pk_transaction_details_cb:
 **/
static void
pk_transaction_details_cb (PkBackend *backend,
			   PkDetails *item,
			   PkTransaction *transaction)
{
	const gchar *group_text;
	gchar *package_id;
	gchar *description;
	gchar *license;
	gchar *url;
	guint64 size;
	PkGroupEnum group;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_details (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "group", &group,
		      "description", &description,
		      "license", &license,
		      "url", &url,
		      "size", &size,
		      NULL);

	/* emit */
	group_text = pk_group_enum_to_string (group);
	g_debug ("emitting details");
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Details",
				       g_variant_new ("(ssssst)",
						      package_id,
						      license != NULL ? license : "",
						      group_text,
						      description != NULL ? description : "",
						      url != NULL ? url : "",
						      size),
				       NULL);

	g_free (package_id);
	g_free (description);
	g_free (license);
	g_free (url);
}

/**
 * pk_transaction_error_code_cb:
 **/
static void
pk_transaction_error_code_cb (PkBackend *backend,
			      PkError *item,
			      PkTransaction *transaction)
{
	gchar *details;
	PkErrorEnum code;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* get data */
	g_object_get (item,
		      "code", &code,
		      "details", &details,
		      NULL);

	if (code == PK_ERROR_ENUM_UNKNOWN) {
		pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s emitted 'unknown error' rather than a specific error "
				    "- this is a backend problem and should be fixed!", pk_role_enum_to_string (transaction->priv->role));
	}

	/* add to results */
	pk_results_set_error_code (transaction->priv->results, item);

	/* emit */
	pk_transaction_error_code_emit (transaction, code, details);

	g_free (details);
}

/**
 * pk_transaction_files_cb:
 **/
static void
pk_transaction_files_cb (PkBackend *backend,
			 PkFiles *item,
			 PkTransaction *transaction)
{
	gchar *filelist = NULL;
	guint i;
	gchar *package_id;
	gchar **files;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "files", &files,
		      NULL);

	/* ensure the files have the correct prefix */
	if (transaction->priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    transaction->priv->cached_directory != NULL) {
		for (i=0; files[i] != NULL; i++) {
			if (!g_str_has_prefix (files[i], transaction->priv->cached_directory)) {
				pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
						    "%s does not have the correct prefix (%s)",
						    files[i], transaction->priv->cached_directory);
			}
		}
	}

	/* add to results */
	pk_results_add_files (transaction->priv->results, item);

	/* emit */
	filelist = g_strjoinv (";", files);
	g_debug ("emitting files %s, %s", package_id, filelist);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Files",
				       g_variant_new ("(ss)",
						      package_id,
						      filelist),
				       NULL);
	g_free (filelist);
	g_free (package_id);
	g_strfreev (files);
}

/**
 * pk_transaction_category_cb:
 **/
static void
pk_transaction_category_cb (PkBackend *backend,
			    PkCategory *item,
			    PkTransaction *transaction)
{
	gchar *parent_id;
	gchar *cat_id;
	gchar *name;
	gchar *summary;
	gchar *icon;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_category (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "parent-id", &parent_id,
		      "cat-id", &cat_id,
		      "name", &name,
		      "summary", &summary,
		      "icon", &icon,
		      NULL);

	/* emit */
	g_debug ("emitting category %s, %s, %s, %s, %s ", parent_id, cat_id, name, summary, icon);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Category",
				       g_variant_new ("(sssss)",
						      parent_id,
						      cat_id,
						      name,
						      summary,
						      icon != NULL ? icon : ""),
				       NULL);
	g_free (parent_id);
	g_free (cat_id);
	g_free (name);
	g_free (summary);
	g_free (icon);
}

/**
 * pk_transaction_item_progress_cb:
 **/
static void
pk_transaction_item_progress_cb (PkBackend *backend,
				 const gchar *package_id,
				 guint percentage,
				 PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* emit */
	g_debug ("emitting item-progress %s, %u", package_id, percentage);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "ItemProgress",
				       g_variant_new ("(su)",
						      package_id,
						      percentage),
				       NULL);
}

/**
 * pk_transaction_distro_upgrade_cb:
 **/
static void
pk_transaction_distro_upgrade_cb (PkBackend *backend,
				  PkDistroUpgrade *item,
				  PkTransaction *transaction)
{
	const gchar *type_text;
	gchar *name;
	gchar *summary;
	PkUpdateStateEnum state;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_distro_upgrade (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "state", &state,
		      "name", &name,
		      "summary", &summary,
		      NULL);

	/* emit */
	type_text = pk_distro_upgrade_enum_to_string (state);
	g_debug ("emitting distro-upgrade %s, %s, %s",
		 type_text, name, summary);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "DistroUpgrade",
				       g_variant_new ("(sss)",
						      type_text,
						      name,
						      summary != NULL ? summary : ""),
				       NULL);

	g_free (name);
	g_free (summary);
}

/**
 * pk_transaction_package_list_to_string:
 **/
static gchar *
pk_transaction_package_list_to_string (GPtrArray *array)
{
	guint i;
	PkPackage *item;
	GString *string;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		g_string_append_printf (string, "%s\t%s\t%s\n",
					pk_info_enum_to_string (info),
					package_id, summary);
		g_free (package_id);
		g_free (summary);
	}

	/* remove trailing newline */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);
	return g_string_free (string, FALSE);
}

/**
 * pk_transaction_state_to_string:
 **/
const gchar *
pk_transaction_state_to_string (PkTransactionState state)
{
	if (state == PK_TRANSACTION_STATE_NEW)
		return "new";
	if (state == PK_TRANSACTION_STATE_WAITING_FOR_AUTH)
		return "waiting-for-auth";
	if (state == PK_TRANSACTION_STATE_COMMITTED)
		return "committed";
	if (state == PK_TRANSACTION_STATE_READY)
		return "ready";
	if (state == PK_TRANSACTION_STATE_RUNNING)
		return "running";
	if (state == PK_TRANSACTION_STATE_FINISHED)
		return "finished";
	return NULL;
}

/**
 * pk_transaction_set_state:
 *
 * A transaction can have only one state at any time as it is processed.
 * Typically, these states will be:
 *
 * 1. 'new'
 * 2. 'waiting for auth'  <--- waiting for PolicyKit (optional)
 * 3. 'committed'         <--- when the client sets the role
 * 4. 'ready'             <--- when the transaction is ready to be run
 * 5. 'running'           <--- where PkBackend gets used
 * 6. 'finished'
 *
 **/
gboolean
pk_transaction_set_state (PkTransaction *transaction, PkTransactionState state)
{
	gboolean ret = TRUE;

	/* check we're not going backwards */
	if (transaction->priv->state != PK_TRANSACTION_STATE_UNKNOWN &&
	    transaction->priv->state > state) {
		g_warning ("cannot set %s, as already %s",
			   pk_transaction_state_to_string (state),
			   pk_transaction_state_to_string (transaction->priv->state));
		ret = FALSE;
		goto out;
	}

	g_debug ("transaction now %s", pk_transaction_state_to_string (state));
	transaction->priv->state = state;

	/* update GUI */
	if (state == PK_TRANSACTION_STATE_WAITING_FOR_AUTH) {
		pk_transaction_status_changed_emit (transaction,
						    PK_STATUS_ENUM_WAITING_FOR_AUTH);
		pk_transaction_progress_changed_emit (transaction,
						      PK_BACKEND_PERCENTAGE_INVALID,
						      PK_BACKEND_PERCENTAGE_INVALID,
						      0, 0);

	} else if (state == PK_TRANSACTION_STATE_READY) {
		pk_transaction_status_changed_emit (transaction,
						    PK_STATUS_ENUM_WAIT);
		pk_transaction_progress_changed_emit (transaction,
						      PK_BACKEND_PERCENTAGE_INVALID,
						      PK_BACKEND_PERCENTAGE_INVALID,
						      0, 0);
	}

	/* we have no actions to perform here, so go straight to running */
	if (state == PK_TRANSACTION_STATE_COMMITTED) {
		/* TODO: do some things before we change states */
		ret = pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
	}
out:
	return ret;
}

/**
 * pk_transaction_get_state:
 **/
PkTransactionState
pk_transaction_get_state (PkTransaction *transaction)
{
	return transaction->priv->state;
}

/**
 * pk_transaction_get_uid:
 **/
guint
pk_transaction_get_uid (PkTransaction *transaction)
{
	return transaction->priv->uid;
}

/**
 * pk_transaction_plugin_phase:
 **/
static void
pk_transaction_plugin_phase (PkTransaction *transaction,
			     PkPluginPhase phase)
{
	guint i;
	const gchar *function = NULL;
	gboolean ret;
	gboolean ran_one = FALSE;
	PkBitfield backend_signals = PK_BACKEND_SIGNAL_LAST;
	PkPluginTransactionFunc plugin_func = NULL;
	PkPlugin *plugin;

	switch (phase) {
	case PK_PLUGIN_PHASE_TRANSACTION_RUN:
		function = "pk_plugin_transaction_run";
		backend_signals = PK_TRANSACTION_NO_BACKEND_SIGNALS;
		break;
	case PK_PLUGIN_PHASE_TRANSACTION_CONTENT_TYPES:
		function = "pk_plugin_transaction_content_types";
		backend_signals = PK_TRANSACTION_NO_BACKEND_SIGNALS;
		break;
	case PK_PLUGIN_PHASE_TRANSACTION_STARTED:
		function = "pk_plugin_transaction_started";
		backend_signals = PK_TRANSACTION_ALL_BACKEND_SIGNALS;
		break;
	case PK_PLUGIN_PHASE_TRANSACTION_FINISHED_START:
		function = "pk_plugin_transaction_finished_start";
		backend_signals = PK_TRANSACTION_ALL_BACKEND_SIGNALS;
		break;
	case PK_PLUGIN_PHASE_TRANSACTION_FINISHED_RESULTS:
		function = "pk_plugin_transaction_finished_results";
		backend_signals = pk_bitfield_from_enums (
			PK_BACKEND_SIGNAL_ALLOW_CANCEL,
			PK_BACKEND_SIGNAL_MESSAGE,
			PK_BACKEND_SIGNAL_NOTIFY_PERCENTAGE,
			PK_BACKEND_SIGNAL_NOTIFY_SUBPERCENTAGE,
			PK_BACKEND_SIGNAL_NOTIFY_REMAINING,
			PK_BACKEND_SIGNAL_REQUIRE_RESTART,
			PK_BACKEND_SIGNAL_STATUS_CHANGED,
			PK_BACKEND_SIGNAL_ITEM_PROGRESS,
			-1);
		break;
	case PK_PLUGIN_PHASE_TRANSACTION_FINISHED_END:
		function = "pk_plugin_transaction_finished_end";
		backend_signals = PK_TRANSACTION_NO_BACKEND_SIGNALS;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_assert (function != NULL);
	if (transaction->priv->plugins == NULL)
		goto out;

	/* run each plugin */
	for (i=0; i<transaction->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (transaction->priv->plugins, i);
		ret = g_module_symbol (plugin->module,
				       function,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;

		ran_one = TRUE;
		g_debug ("run %s on %s",
			 function,
			 g_module_name (plugin->module));
		pk_transaction_set_signals (transaction, backend_signals);
		plugin_func (plugin, transaction);
	}
out:
	/* set this to a know state in case the plugin misbehaves */
	pk_transaction_set_signals (transaction, backend_signals);
	if (!ran_one)
		g_debug ("no plugins provided %s", function);
}

/**
 * pk_transaction_get_conf:
 *
 * Returns: (transfer none): PkConf of this transaction
 **/
PkConf *
pk_transaction_get_conf (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	return transaction->priv->conf;
}

/**
 * pk_transaction_get_results:
 *
 * Returns: (transfer none): Results of the transaction
 **/
PkResults *
pk_transaction_get_results (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	return transaction->priv->results;
}

/**
 * pk_transaction_get_package_ids:
 *
 * Returns: (transfer none): Cached package-ids
 **/
gchar **
pk_transaction_get_package_ids (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	return transaction->priv->cached_package_ids;
}

/**
 * pk_transaction_set_package_ids:
 **/
void
pk_transaction_set_package_ids (PkTransaction *transaction,
			        gchar **package_ids)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_strfreev (transaction->priv->cached_package_ids);
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
}

/**
 * pk_transaction_get_values:
 *
 * Returns: (transfer none): Cached values
 **/
gchar **
pk_transaction_get_values (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	return transaction->priv->cached_values;
}

/**
 * pk_transaction_get_full_paths:
 *
 * Returns: (transfer none): Cached paths
 **/
gchar **
pk_transaction_get_full_paths (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	return transaction->priv->cached_full_paths;
}

/**
 * pk_transaction_set_full_paths:
 **/
void
pk_transaction_set_full_paths (PkTransaction *transaction,
			       gchar **full_paths)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_strfreev (transaction->priv->cached_full_paths);
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
}

/**
 * pk_transaction_finished_cb:
 **/
static void
pk_transaction_finished_cb (PkBackend *backend, PkExitEnum exit_enum, PkTransaction *transaction)
{
	guint time_ms;
	gchar *packages;
	guint i;
	GPtrArray *array;
	PkPackage *item;
	gchar *package_id;
	PkInfoEnum info;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		g_warning ("Already finished");
		return;
	}

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_FINISHED_START);

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_FINISHED_RESULTS);

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_FINISHED_END);

	/* save this so we know if the cache is valid */
	pk_results_set_exit_code (transaction->priv->results, exit_enum);

	/* if we did not send this, ensure the GUI has the right state */
	if (transaction->priv->allow_cancel)
		pk_transaction_allow_cancel_emit (transaction, FALSE);

	/* we should get no more from the backend with this tid */
	transaction->priv->finished = TRUE;

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
	g_debug ("backend was running for %i ms", time_ms);

	/* add to the database if we are going to log it */
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {

		/* get results */
		array = pk_results_get_package_array (transaction->priv->results);

		/* save to database */
		packages = pk_transaction_package_list_to_string (array);
		if (!pk_strzero (packages))
			pk_transaction_db_set_data (transaction->priv->transaction_db, transaction->priv->tid, packages);

		/* report to syslog */
		for (i=0; i<array->len; i++) {
			item = g_ptr_array_index (array, i);
			g_object_get (item,
				      "info", &info,
				      "package-id", &package_id,
				      NULL);
			if (info == PK_INFO_ENUM_REMOVING ||
			    info == PK_INFO_ENUM_INSTALLING ||
			    info == PK_INFO_ENUM_UPDATING) {
				pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "in %s for %s package %s was %s for uid %i",
					       transaction->priv->tid, pk_role_enum_to_string (transaction->priv->role),
					       package_id, pk_info_enum_to_string (info), transaction->priv->uid);
			}
			g_free (package_id);
		}
		g_free (packages);
		g_ptr_array_unref (array);
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
	//TODO: on main interface

	/* report to syslog */
	if (transaction->priv->uid != PK_TRANSACTION_UID_INVALID)
		pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "%s transaction %s from uid %i finished with %s after %ims",
			       pk_role_enum_to_string (transaction->priv->role), transaction->priv->tid,
			       transaction->priv->uid, pk_exit_enum_to_string (exit_enum), time_ms);
	else
		pk_syslog_add (transaction->priv->syslog, PK_SYSLOG_TYPE_INFO, "%s transaction %s finished with %s after %ims",
			       pk_role_enum_to_string (transaction->priv->role), transaction->priv->tid, pk_exit_enum_to_string (exit_enum), time_ms);

	/* we emit last, as other backends will be running very soon after us, and we don't want to be notified */
	pk_transaction_finished_emit (transaction, exit_enum, time_ms);
}

/**
 * pk_transaction_message_cb:
 **/
static void
pk_transaction_message_cb (PkBackend *backend,
			   PkMessage *item,
			   PkTransaction *transaction)
{
	const gchar *message_text;
	gboolean developer_mode;
	gchar *details;
	PkMessageEnum type;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* get data */
	g_object_get (item,
		      "type", &type,
		      "details", &details,
		      NULL);

	/* if not running in developer mode, then skip these types */
	developer_mode = pk_conf_get_bool (transaction->priv->conf, "DeveloperMode");
	if (!developer_mode &&
	    (type == PK_MESSAGE_ENUM_BACKEND_ERROR ||
	     type == PK_MESSAGE_ENUM_DAEMON_ERROR)) {
		g_debug ("ignoring message (turn on DeveloperMode): %s", details);
		return;
	}

	/* add to results */
	pk_results_add_message (transaction->priv->results, item);

	/* emit */
	message_text = pk_message_enum_to_string (type);
	g_debug ("emitting message %s, '%s'", message_text, details);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Message",
				       g_variant_new ("(ss)",
						      message_text,
						      details),
				       NULL);
	g_free (details);
}

/**
 * pk_transaction_package_cb:
 **/
static void
pk_transaction_package_cb (PkBackend *backend,
			   PkPackage *item,
			   PkTransaction *transaction)
{
	const gchar *role_text;
	PkInfoEnum info;
	const gchar *package_id;
	const gchar *summary = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		g_warning ("Already finished");
		return;
	}

	/* get data */

	/* check the backend is doing the right thing */
	info = pk_package_get_info (item);
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		if (info == PK_INFO_ENUM_INSTALLED) {
			role_text = pk_role_enum_to_string (transaction->priv->role);
			pk_backend_message (transaction->priv->backend,
					    PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "%s emitted 'installed' rather than 'installing' "
					    "- you need to do the package *before* you do the action", role_text);
			return;
		}
	}

	/* check we are respecting the filters */
	if (pk_bitfield_contain (transaction->priv->cached_filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		if (info == PK_INFO_ENUM_INSTALLED) {
			role_text = pk_role_enum_to_string (transaction->priv->role);
			pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "%s emitted package that was installed when "
					    "the ~installed filter is in place", role_text);
			return;
		}
	}
	if (pk_bitfield_contain (transaction->priv->cached_filters, PK_FILTER_ENUM_INSTALLED)) {
		if (info == PK_INFO_ENUM_AVAILABLE) {
			role_text = pk_role_enum_to_string (transaction->priv->role);
			pk_backend_message (transaction->priv->backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
					    "%s emitted package that was ~installed when "
					    "the installed filter is in place", role_text);
			return;
		}
	}

	/* add to results even if we already got a result */
	if (info != PK_INFO_ENUM_FINISHED)
		pk_results_add_package (transaction->priv->results, item);

	/* emit */
	package_id = pk_package_get_id (item);
	g_free (transaction->priv->last_package_id);
	transaction->priv->last_package_id = g_strdup (package_id);
	summary = pk_package_get_summary (item);
	g_debug ("emit package %s, %s, %s",
		 pk_info_enum_to_string (info),
		 package_id,
		 summary);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Package",
				       g_variant_new ("(uss)",
						      info,
						      package_id,
						      summary ? summary : ""),
				       NULL);
}

/**
 * pk_transaction_repo_detail_cb:
 **/
static void
pk_transaction_repo_detail_cb (PkBackend *backend,
			       PkRepoDetail *item,
			       PkTransaction *transaction)
{
	gchar *repo_id;
	gchar *description;
	gboolean enabled;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_repo_detail (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "repo-id", &repo_id,
		      "description", &description,
		      "enabled", &enabled,
		      NULL);

	/* emit */
	g_debug ("emitting repo-detail %s, %s, %i", repo_id, description, enabled);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "RepoDetail",
				       g_variant_new ("(ssb)",
						      repo_id,
						      description != NULL ? description : "",
						      enabled),
				       NULL);
	g_free (repo_id);
	g_free (description);
}

/**
 * pk_transaction_repo_signature_required_cb:
 **/
static void
pk_transaction_repo_signature_required_cb (PkBackend *backend,
					   PkRepoSignatureRequired *item,
					   PkTransaction *transaction)
{
	const gchar *type_text;
	gchar *package_id;
	gchar *repository_name;
	gchar *key_url;
	gchar *key_userid;
	gchar *key_id;
	gchar *key_fingerprint;
	gchar *key_timestamp;
	PkSigTypeEnum type;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_repo_signature_required (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "type", &type,
		      "package-id", &package_id,
		      "repository-name", &repository_name,
		      "key-url", &key_url,
		      "key-userid", &key_userid,
		      "key-id", &key_id,
		      "key-fingerprint", &key_fingerprint,
		      "key-timestamp", &key_timestamp,
		      NULL);

	/* emit */
	type_text = pk_sig_type_enum_to_string (type);
	g_debug ("emitting repo_signature_required %s, %s, %s, %s, %s, %s, %s, %s",
		 package_id, repository_name, key_url, key_userid, key_id,
		 key_fingerprint, key_timestamp, type_text);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "RepoSignatureRequired",
				       g_variant_new ("(ssssssss)",
						      package_id,
						      repository_name,
						      key_url != NULL ? key_url : "",
						      key_userid != NULL ? key_userid : "",
						      key_id != NULL ? key_id : "",
						      key_fingerprint != NULL ? key_fingerprint : "",
						      key_timestamp != NULL ? key_timestamp : "",
						      type_text),
				       NULL);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_signature_required = TRUE;

	g_free (package_id);
	g_free (repository_name);
	g_free (key_url);
	g_free (key_userid);
	g_free (key_id);
	g_free (key_fingerprint);
	g_free (key_timestamp);
}

/**
 * pk_transaction_eula_required_cb:
 **/
static void
pk_transaction_eula_required_cb (PkBackend *backend,
				 PkEulaRequired *item,
				 PkTransaction *transaction)
{
	gchar *eula_id;
	gchar *package_id;
	gchar *vendor_name;
	gchar *license_agreement;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_eula_required (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "eula-id", &eula_id,
		      "package-id", &package_id,
		      "vendor-name", &vendor_name,
		      "license-agreement", &license_agreement,
		      NULL);

	/* emit */
	g_debug ("emitting eula-required %s, %s, %s, %s",
		   eula_id, package_id, vendor_name, license_agreement);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "EulaRequired",
				       g_variant_new ("(ssss)",
						      eula_id,
						      package_id,
						      vendor_name != NULL ? vendor_name : "",
						      license_agreement != NULL ? license_agreement : ""),
				       NULL);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_eula_required = TRUE;

	g_free (eula_id);
	g_free (package_id);
	g_free (vendor_name);
	g_free (license_agreement);
}

/**
 * pk_transaction_media_change_required_cb:
 **/
static void
pk_transaction_media_change_required_cb (PkBackend *backend,
					 PkMediaChangeRequired *item,
					 PkTransaction *transaction)
{
	const gchar *media_type_text;
	gchar *media_id;
	gchar *media_text;
	PkMediaTypeEnum media_type;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_media_change_required (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "media-type", &media_type,
		      "media-id", &media_id,
		      "media-text", &media_text,
		      NULL);

	/* emit */
	media_type_text = pk_media_type_enum_to_string (media_type);
	g_debug ("emitting media-change-required %s, %s, %s",
		   media_type_text, media_id, media_text);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "MediaChangeRequired",
				       g_variant_new ("(sss)",
						      media_type_text,
						      media_id,
						      media_text != NULL ? media_text : ""),
				       NULL);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_media_change_required = TRUE;

	g_free (media_id);
	g_free (media_text);
}

/**
 * pk_transaction_require_restart_cb:
 **/
static void
pk_transaction_require_restart_cb (PkBackend *backend,
				   PkRequireRestart *item,
				   PkTransaction *transaction)
{
	const gchar *restart_text;
	PkRequireRestart *item_tmp;
	GPtrArray *array;
	gboolean found = FALSE;
	guint i;
	gchar *package_id;
	gchar *package_id_tmp;
	PkRestartEnum restart;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "restart", &restart,
		      NULL);

	/* filter out duplicates */
	array = pk_results_get_require_restart_array (transaction->priv->results);
	for (i=0; i<array->len; i++) {
		item_tmp = g_ptr_array_index (array, i);
		g_object_get (item_tmp,
			      "package-id", &package_id_tmp,
			      NULL);
		if (g_strcmp0 (package_id, package_id_tmp) == 0) {
			g_free (package_id_tmp);
			found = TRUE;
			break;
		}
		g_free (package_id_tmp);
	}
	g_ptr_array_unref (array);

	/* ignore */
	restart_text = pk_restart_enum_to_string (restart);
	if (found) {
		g_debug ("ignoring %s (%s) as already sent", restart_text, package_id);
		return;
	}

	/* add to results */
	pk_results_add_require_restart (transaction->priv->results, item);

	/* emit */
	g_debug ("emitting require-restart %s, '%s'", restart_text, package_id);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "RequireRestart",
				       g_variant_new ("(ss)",
						      restart_text,
						      package_id),
				       NULL);
	g_free (package_id);
}

/**
 * pk_transaction_status_changed_cb:
 **/
static void
pk_transaction_status_changed_cb (PkBackend *backend,
				  PkStatusEnum status,
				  PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* don't proxy this on the bus, just for use internal */
	if (status == PK_STATUS_ENUM_WAIT)
		return;

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		g_warning ("Already finished, so can't proxy status %s",
			   pk_status_enum_to_string (status));
		return;
	}

	pk_transaction_status_changed_emit (transaction, status);
}

/**
 * pk_transaction_transaction_cb:
 **/
static void
pk_transaction_transaction_cb (PkTransactionDb *tdb,
			       PkTransactionPast *item,
			       PkTransaction *transaction)
{
	const gchar *role_text;
	gchar *tid;
	gchar *timespec;
	gchar *data;
	gchar *cmdline;
	guint duration;
	guint uid;
	gboolean succeeded;
	PkRoleEnum role;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_transaction (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "role", &role,
		      "tid", &tid,
		      "timespec", &timespec,
		      "succeeded", &succeeded,
		      "duration", &duration,
		      "data", &data,
		      "uid", &uid,
		      "cmdline", &cmdline,
		      NULL);

	/* emit */
	role_text = pk_role_enum_to_string (role);
	g_debug ("emitting transaction %s, %s, %i, %s, %i, %s, %i, %s",
		   tid, timespec, succeeded, role_text,
		   duration, data, uid, cmdline);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Transaction",
				       g_variant_new ("(ssbsusus)",
						      tid,
						      timespec,
						      succeeded,
						      role_text,
						      duration,
						      data != NULL ? data : "",
						      uid,
						      cmdline != NULL ? cmdline : ""),
				       NULL);
	g_free (tid);
	g_free (timespec);
	g_free (data);
	g_free (cmdline);
}

/**
 * pk_transaction_update_detail_cb:
 **/
static void
pk_transaction_update_detail_cb (PkBackend *backend,
				 PkUpdateDetail *item,
				 PkTransaction *transaction)
{
	const gchar *state_text;
	const gchar *restart_text;
	gchar *package_id;
	gchar *updates;
	gchar *obsoletes;
	gchar *vendor_url;
	gchar *bugzilla_url;
	gchar *cve_url;
	gchar *update_text;
	gchar *changelog;
	gchar *issued;
	gchar *updated;
	PkRestartEnum restart;
	PkUpdateStateEnum state;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_update_detail (transaction->priv->results, item);

	/* get data */
	g_object_get (item,
		      "restart", &restart,
		      "state", &state,
		      "package-id", &package_id,
		      "updates", &updates,
		      "obsoletes", &obsoletes,
		      "vendor-url", &vendor_url,
		      "bugzilla-url", &bugzilla_url,
		      "cve-url", &cve_url,
		      "update-text", &update_text,
		      "changelog", &changelog,
		      "issued", &issued,
		      "updated", &updated,
		      NULL);

	/* emit */
	g_debug ("emitting update-detail");
	restart_text = pk_restart_enum_to_string (restart);
	state_text = pk_update_state_enum_to_string (state);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "UpdateDetail",
				       g_variant_new ("(ssssssssssss)",
						      package_id,
						      updates != NULL ? updates : "",
						      obsoletes != NULL ? obsoletes : "",
						      vendor_url != NULL ? vendor_url : "",
						      bugzilla_url != NULL ? bugzilla_url : "",
						      cve_url != NULL ? cve_url : "",
						      restart_text,
						      update_text != NULL ? update_text : "",
						      changelog != NULL ? changelog : "",
						      state_text,
						      issued != NULL ? issued : "",
						      updated != NULL ? updated : ""),
				       NULL);

	g_free (package_id);
	g_free (updates);
	g_free (obsoletes);
	g_free (vendor_url);
	g_free (bugzilla_url);
	g_free (cve_url);
	g_free (update_text);
	g_free (changelog);
	g_free (issued);
	g_free (updated);
}

/**
 * pk_transaction_set_session_state:
 */
static gboolean
pk_transaction_set_session_state (PkTransaction *transaction,
				  GError **error)
{
	gboolean ret = FALSE;
	gchar *session = NULL;
	gchar *proxy_http = NULL;
	gchar *proxy_https = NULL;
	gchar *proxy_ftp = NULL;
	gchar *proxy_socks = NULL;
	gchar *no_proxy = NULL;
	gchar *pac = NULL;
	gchar *root = NULL;
	gchar *cmdline = NULL;
	PkTransactionPrivate *priv = transaction->priv;

	/* get session */
	session = pk_dbus_get_session (priv->dbus, priv->sender);
	if (session == NULL) {
		g_set_error_literal (error, 1, 0, "failed to get the session");
		goto out;
	}

	/* get from database */
	ret = pk_transaction_db_get_proxy (priv->transaction_db, priv->uid, session,
					   &proxy_http,
					   &proxy_https,
					   &proxy_ftp,
					   &proxy_socks,
					   &no_proxy,
					   &pac);
	if (!ret) {
		g_set_error_literal (error, 1, 0,
				     "failed to get the proxy from the database");
		goto out;
	}

	/* try to set the new proxy */
	ret = pk_backend_set_proxy (priv->backend,
				    proxy_http,
				    proxy_https,
				    proxy_ftp,
				    proxy_socks,
				    no_proxy,
				    pac);
	if (!ret) {
		g_set_error_literal (error, 1, 0, "failed to set the proxy");
		goto out;
	}

	/* get from database */
	ret = pk_transaction_db_get_root (priv->transaction_db, priv->uid, session, &root);
	if (!ret) {
		g_set_error_literal (error, 1, 0, "failed to get the root from the database");
		goto out;
	}

	/* try to set the new proxy */
	ret = pk_backend_set_root (priv->backend, root);
	if (!ret) {
		g_set_error_literal (error, 1, 0, "failed to set the root");
		goto out;
	}
	g_debug ("using http_proxy=%s, ftp_proxy=%s, root=%s for %i:%s",
		   proxy_http, proxy_ftp, root, priv->uid, session);

	/* try to set the new uid and cmdline */
	cmdline = g_strdup_printf ("PackageKit: %s",
				   pk_role_enum_to_string (priv->role));
	pk_backend_set_uid (priv->backend, priv->uid);
	pk_backend_set_cmdline (priv->backend, cmdline);
out:
	g_free (cmdline);
	g_free (proxy_http);
	g_free (proxy_https);
	g_free (proxy_ftp);
	g_free (proxy_socks);
	g_free (no_proxy);
	g_free (pac);
	g_free (session);
	return ret;
}

/**
 * pk_transaction_speed_cb:
 **/
static void
pk_transaction_speed_cb (GObject *object,
			 GParamSpec *pspec,
			 PkTransaction *transaction)
{
	g_object_get (object,
		      "speed", &transaction->priv->speed,
		      NULL);
	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "Speed",
					      g_variant_new_uint32 (transaction->priv->speed));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_percentage_cb:
 **/
static void
pk_transaction_percentage_cb (GObject *object,
			      GParamSpec *pspec,
			      PkTransaction *transaction)
{
	g_object_get (object,
		      "percentage", &transaction->priv->percentage,
		      NULL);
	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "Percentage",
					      g_variant_new_uint32 (transaction->priv->percentage));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_subpercentage_cb:
 **/
static void
pk_transaction_subpercentage_cb (GObject *object,
			         GParamSpec *pspec,
			         PkTransaction *transaction)
{
	g_object_get (object,
		      "subpercentage", &transaction->priv->subpercentage,
		      NULL);
	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "Subpercentage",
					      g_variant_new_uint32 (transaction->priv->subpercentage));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_remaining_cb:
 **/
static void
pk_transaction_remaining_cb (GObject *object,
			     GParamSpec *pspec,
			     PkTransaction *transaction)
{
	g_object_get (object,
		      "remaining", &transaction->priv->remaining_time,
		      NULL);
	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "RemainingTime",
					      g_variant_new_uint32 (transaction->priv->remaining_time));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_set_signals:
 *
 * Connect selected signals in backend_signals to Pkransaction,
 * disconnect everthing else not mentioned there.
 **/
void
pk_transaction_set_signals (PkTransaction *transaction, PkBitfield backend_signals)
{
	PkTransactionPrivate *priv = PK_TRANSACTION_GET_PRIVATE (transaction);

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_ALLOW_CANCEL)) {
		if (priv->signal_allow_cancel == 0)
			priv->signal_allow_cancel =
				g_signal_connect (priv->backend, "allow-cancel",
						G_CALLBACK (pk_transaction_allow_cancel_cb), transaction);
	} else {
		if (priv->signal_allow_cancel > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_allow_cancel);
			priv->signal_allow_cancel = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_DETAILS)) {
		pk_backend_set_vfunc (priv->backend,
				      PK_BACKEND_SIGNAL_DETAILS,
				      (PkBackendVFunc) pk_transaction_details_cb,
				      transaction);
	} else {
		pk_backend_set_vfunc (priv->backend,
				      PK_BACKEND_SIGNAL_DETAILS,
				      NULL,
				      transaction);
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_ERROR_CODE)) {
		if (priv->signal_error_code == 0)
			priv->signal_error_code =
				g_signal_connect (priv->backend, "error-code",
						G_CALLBACK (pk_transaction_error_code_cb), transaction);
	} else {
		if (priv->signal_error_code > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_error_code);
			priv->signal_error_code = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_FILES)) {
		if (priv->signal_files == 0)
			priv->signal_files =
				g_signal_connect (priv->backend, "files",
						G_CALLBACK (pk_transaction_files_cb), transaction);
	} else {
		if (priv->signal_files > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_files);
			priv->signal_files = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_DISTRO_UPGRADE)) {
		if (priv->signal_distro_upgrade == 0)
			priv->signal_distro_upgrade =
				g_signal_connect (priv->backend, "distro-upgrade",
						G_CALLBACK (pk_transaction_distro_upgrade_cb), transaction);
	} else {
		if (priv->signal_distro_upgrade > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_distro_upgrade);
			priv->signal_distro_upgrade = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_FINISHED)) {
		if (priv->signal_finished == 0)
			priv->signal_finished =
				g_signal_connect (priv->backend, "finished",
						G_CALLBACK (pk_transaction_finished_cb), transaction);
	} else {
		if (priv->signal_finished > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_finished);
			priv->signal_finished = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_MESSAGE)) {
		if (priv->signal_message == 0)
			priv->signal_message =
				g_signal_connect (priv->backend, "message",
						G_CALLBACK (pk_transaction_message_cb), transaction);
	} else {
		if (priv->signal_message > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_message);
			priv->signal_message = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_PACKAGE)) {
		if (priv->signal_package == 0)
			priv->signal_package =
				g_signal_connect (priv->backend, "package",
						G_CALLBACK (pk_transaction_package_cb), transaction);
	} else {
		if (priv->signal_package > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_package);
			priv->signal_package = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_ITEM_PROGRESS)) {
		if (priv->signal_item_progress == 0)
			priv->signal_item_progress =
				g_signal_connect (priv->backend, "item-progress",
						G_CALLBACK (pk_transaction_item_progress_cb), transaction);
	} else {
		if (priv->signal_item_progress > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_item_progress);
			priv->signal_item_progress = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_NOTIFY_PERCENTAGE)) {
		if (priv->signal_percentage == 0)
			priv->signal_percentage =
				g_signal_connect (priv->backend, "notify::percentage",
						G_CALLBACK (pk_transaction_percentage_cb), transaction);
	} else {
		if (priv->signal_percentage > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_percentage);
			priv->signal_percentage = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_NOTIFY_SUBPERCENTAGE)) {
		if (priv->signal_subpercentage == 0)
			priv->signal_subpercentage =
				g_signal_connect (priv->backend, "notify::subpercentage",
						G_CALLBACK (pk_transaction_subpercentage_cb), transaction);
	} else {
		if (priv->signal_subpercentage > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_subpercentage);
			priv->signal_subpercentage = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_NOTIFY_REMAINING)) {
		if (priv->signal_remaining == 0)
			priv->signal_remaining =
				g_signal_connect (priv->backend, "notify::remaining",
						G_CALLBACK (pk_transaction_remaining_cb), transaction);
	} else {
		if (priv->signal_remaining > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_remaining);
			priv->signal_remaining = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_NOTIFY_SPEED)) {
		if (priv->signal_speed == 0)
			priv->signal_speed =
				g_signal_connect (priv->backend, "notify::speed",
						G_CALLBACK (pk_transaction_speed_cb), transaction);
	} else {
		if (priv->signal_speed > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_speed);
			priv->signal_speed = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_REPO_DETAIL)) {
		if (priv->signal_repo_detail == 0)
			priv->signal_repo_detail =
				g_signal_connect (priv->backend, "repo-detail",
						G_CALLBACK (pk_transaction_repo_detail_cb), transaction);
	} else {
		if (priv->signal_repo_detail > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_repo_detail);
			priv->signal_repo_detail = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED)) {
		if (priv->signal_repo_signature_required == 0)
			priv->signal_repo_signature_required =
				g_signal_connect (priv->backend, "repo-signature-required",
						G_CALLBACK (pk_transaction_repo_signature_required_cb), transaction);
	} else {
		if (priv->signal_repo_signature_required > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_repo_signature_required);
			priv->signal_repo_signature_required = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_EULA_REQUIRED)) {
		if (priv->signal_eula_required == 0)
			priv->signal_eula_required =
				g_signal_connect (priv->backend, "eula-required",
						G_CALLBACK (pk_transaction_eula_required_cb), transaction);
	} else {
		if (priv->signal_eula_required > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_eula_required);
			priv->signal_eula_required = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED)) {
		if (priv->signal_media_change_required == 0)
			priv->signal_media_change_required =
				g_signal_connect (priv->backend, "media-change-required",
						G_CALLBACK (pk_transaction_media_change_required_cb), transaction);
	} else {
		if (priv->signal_media_change_required > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_media_change_required);
			priv->signal_media_change_required = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_REQUIRE_RESTART)) {
		if (priv->signal_require_restart == 0)
			priv->signal_require_restart =
				g_signal_connect (priv->backend, "require-restart",
						G_CALLBACK (pk_transaction_require_restart_cb), transaction);
	} else {
		if (priv->signal_require_restart > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_require_restart);
			priv->signal_require_restart = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_STATUS_CHANGED)) {
		if (priv->signal_status_changed == 0)
			priv->signal_status_changed =
				g_signal_connect (priv->backend, "status-changed",
						G_CALLBACK (pk_transaction_status_changed_cb), transaction);
	} else {
		if (priv->signal_status_changed > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_status_changed);
			priv->signal_status_changed = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_UPDATE_DETAIL)) {
		if (priv->signal_update_detail == 0)
			priv->signal_update_detail =
				g_signal_connect (priv->backend, "update-detail",
						G_CALLBACK (pk_transaction_update_detail_cb), transaction);
	} else {
		if (priv->signal_update_detail > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_update_detail);
			priv->signal_update_detail = 0;
		}
	}

	if (pk_bitfield_contain (backend_signals, PK_BACKEND_SIGNAL_CATEGORY)) {
		if (priv->signal_category == 0)
			priv->signal_category =
				g_signal_connect (priv->backend, "category",
						G_CALLBACK (pk_transaction_category_cb), transaction);
	} else {
		if (priv->signal_category > 0) {
			g_signal_handler_disconnect (priv->backend,
					priv->signal_category);
			priv->signal_category = 0;
		}
	}
}

/**
 * pk_transaction_run:
 */
gboolean
pk_transaction_run (PkTransaction *transaction)
{
	gboolean ret;
	GError *error = NULL;
	PkExitEnum exit_status;
	PkTransactionPrivate *priv = PK_TRANSACTION_GET_PRIVATE (transaction);

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (priv->tid != NULL, FALSE);

	/* prepare for use; the transaction list ensures this is safe */
	pk_backend_reset (priv->backend);

	/* assign */
	g_object_set (priv->backend,
		      "background", priv->background,
		      "interactive", priv->interactive,
		      "transaction-id", priv->tid,
		      NULL);

	/* if we didn't set a locale for this transaction, we would reuse the
	 * last set locale in the backend, or NULL if it was not ever set.
	 * in this case use the C locale */
	if (priv->locale == NULL)
		pk_backend_set_locale (priv->backend, "C");
	else
		pk_backend_set_locale (priv->backend, priv->locale);

	/* set the frontend socket if it exists */
	pk_backend_set_frontend_socket (priv->backend, priv->frontend_socket);

	/* set the cache-age */
	if (priv->cache_age > 0)
		pk_backend_set_cache_age (priv->backend, priv->cache_age);

	/* set proxy */
	ret = pk_transaction_set_session_state (transaction, &error);
	if (!ret) {
		ret = TRUE;
		g_debug ("failed to set the session state (non-fatal): %s",
			 error->message);
		g_clear_error (&error);
	}

	/* we are no longer waiting, we are setting up */
	pk_backend_set_status (priv->backend, PK_STATUS_ENUM_SETUP);
	pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_SETUP);

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_RUN);

	/* is an error code set? */
	if (pk_backend_get_is_error_set (priv->backend)) {
		exit_status = pk_backend_get_exit_code (priv->backend);
		pk_transaction_finished_emit (transaction, exit_status, 0);

		/* do not fail the transaction */
		ret = TRUE;
		goto out;
	}

	/* check if we should skip this transaction */
	if (pk_backend_get_exit_code (priv->backend) == PK_EXIT_ENUM_SKIP_TRANSACTION) {
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_SUCCESS, 0);

		/* do not fail the transaction */
		ret = TRUE;
		goto out;
	}

	/* might have to reset again if we used the backend */
	pk_backend_reset (priv->backend);

	/* set the role */
	pk_backend_set_role (priv->backend, priv->role);
	g_debug ("setting role for %s to %s",
		 priv->tid,
		 pk_role_enum_to_string (priv->role));

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_STARTED);

	/* check again if we should skip this transaction */
	exit_status = pk_backend_get_exit_code (priv->backend);
	if (exit_status == PK_EXIT_ENUM_SKIP_TRANSACTION) {
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_SUCCESS, 0);

		/* do not fail the transaction */
		ret = TRUE;
		goto out;
	}

	/* did the plugin finish or abort the transaction? */
	if (exit_status != PK_EXIT_ENUM_UNKNOWN)  {
		pk_transaction_finished_emit (transaction, exit_status, 0);
		ret = TRUE;
		goto out;
	}

	/* mark running */
	priv->allow_cancel = FALSE;

	/* reset after the pre-transaction checks */
	pk_backend_set_percentage (priv->backend, PK_BACKEND_PERCENTAGE_INVALID);

	/* do the correct action with the cached parameters */
	if (priv->role == PK_ROLE_ENUM_GET_DEPENDS)
		pk_backend_get_depends (priv->backend, priv->cached_filters, priv->cached_package_ids, priv->cached_force);
	else if (priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		pk_backend_get_update_detail (priv->backend, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_RESOLVE)
		pk_backend_resolve (priv->backend, priv->cached_filters, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_ROLLBACK)
		pk_backend_rollback (priv->backend, priv->cached_transaction_id);
	else if (priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES)
		pk_backend_download_packages (priv->backend, priv->cached_package_ids, priv->cached_directory);
	else if (priv->role == PK_ROLE_ENUM_GET_DETAILS)
		pk_backend_get_details (priv->backend, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES)
		pk_backend_get_distro_upgrades (priv->backend);
	else if (priv->role == PK_ROLE_ENUM_GET_FILES)
		pk_backend_get_files (priv->backend, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_GET_REQUIRES)
		pk_backend_get_requires (priv->backend, priv->cached_filters, priv->cached_package_ids, priv->cached_force);
	else if (priv->role == PK_ROLE_ENUM_WHAT_PROVIDES)
		pk_backend_what_provides (priv->backend, priv->cached_filters, priv->cached_provides, priv->cached_values);
	else if (priv->role == PK_ROLE_ENUM_GET_UPDATES)
		pk_backend_get_updates (priv->backend, priv->cached_filters);
	else if (priv->role == PK_ROLE_ENUM_GET_PACKAGES)
		pk_backend_get_packages (priv->backend, priv->cached_filters);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_DETAILS)
		pk_backend_search_details (priv->backend, priv->cached_filters, priv->cached_values);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_FILE)
		pk_backend_search_files (priv->backend, priv->cached_filters, priv->cached_values);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_GROUP)
		pk_backend_search_groups (priv->backend, priv->cached_filters, priv->cached_values);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_NAME)
		pk_backend_search_names (priv->backend,priv->cached_filters,priv->cached_values);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES)
		pk_backend_install_packages (priv->backend, priv->cached_only_trusted, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_FILES)
		pk_backend_install_files (priv->backend, priv->cached_only_trusted, priv->cached_full_paths);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_SIGNATURE)
		pk_backend_install_signature (priv->backend, PK_SIGTYPE_ENUM_GPG, priv->cached_key_id, priv->cached_package_id);
	else if (priv->role == PK_ROLE_ENUM_REFRESH_CACHE)
		pk_backend_refresh_cache (priv->backend,  priv->cached_force);
	else if (priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES)
		pk_backend_remove_packages (priv->backend, priv->cached_package_ids, priv->cached_allow_deps, priv->cached_autoremove);
	else if (priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)
		pk_backend_update_packages (priv->backend, priv->cached_only_trusted, priv->cached_package_ids);
	else if (priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM)
		pk_backend_update_system (priv->backend, priv->cached_only_trusted);
	else if (priv->role == PK_ROLE_ENUM_GET_CATEGORIES)
		pk_backend_get_categories (priv->backend);
	else if (priv->role == PK_ROLE_ENUM_GET_REPO_LIST)
		pk_backend_get_repo_list (priv->backend, priv->cached_filters);
	else if (priv->role == PK_ROLE_ENUM_REPO_ENABLE)
		pk_backend_repo_enable (priv->backend, priv->cached_repo_id, priv->cached_enabled);
	else if (priv->role == PK_ROLE_ENUM_REPO_SET_DATA)
		pk_backend_repo_set_data (priv->backend, priv->cached_repo_id, priv->cached_parameter, priv->cached_value);
	else if (priv->role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES)
		pk_backend_simulate_install_files (priv->backend, priv->cached_full_paths);
	else if (priv->role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES) {
		pk_backend_simulate_install_packages (priv->backend, priv->cached_package_ids);
	} else if (priv->role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES) {
		pk_backend_simulate_remove_packages (priv->backend, priv->cached_package_ids, priv->cached_autoremove);
	} else if (priv->role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
		pk_backend_simulate_update_packages (priv->backend, priv->cached_package_ids);
	} else if (priv->role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		pk_backend_upgrade_system (priv->backend, priv->cached_value, priv->cached_provides);
	} else if (priv->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		pk_backend_repair_system (priv->backend, priv->cached_only_trusted);
	} else if (priv->role == PK_ROLE_ENUM_SIMULATE_REPAIR_SYSTEM) {
		pk_backend_simulate_repair_system (priv->backend);
	} else {
		g_error ("failed to run as role not assigned");
		ret = FALSE;
	}
out:
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
 * pk_transaction_vanished_cb:
 **/
static void
pk_transaction_vanished_cb (GDBusConnection *connection,
			    const gchar *name,
			    gpointer user_data)
{
	PkTransaction *transaction = PK_TRANSACTION (user_data);

	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	transaction->priv->caller_active = FALSE;

	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "CallerActive",
					      g_variant_new_boolean (transaction->priv->caller_active));
	pk_transaction_emit_changed (transaction);
}

/**
 * pk_transaction_set_sender:
 */
gboolean
pk_transaction_set_sender (PkTransaction *transaction, const gchar *sender)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (sender != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->sender == NULL, FALSE);

	g_debug ("setting sender to %s", sender);
	transaction->priv->sender = g_strdup (sender);

	transaction->priv->watch_id =
		g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				  sender,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  NULL,
				  pk_transaction_vanished_cb,
				  transaction,
				  NULL);

	/* we get the UID for all callers as we need to know when to cancel */
#ifdef USE_SECURITY_POLKIT
	transaction->priv->subject = polkit_system_bus_name_new (sender);
	transaction->priv->cmdline = pk_dbus_get_cmdline (transaction->priv->dbus, sender);
#endif
	transaction->priv->uid = pk_dbus_get_uid (transaction->priv->dbus, sender);

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
	PkTransactionPrivate *priv = transaction->priv;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (priv->tid != NULL, FALSE);

	/* set the idle really early as this affects scheduling */
	if (priv->background == PK_HINT_ENUM_TRUE ||
	    priv->background == PK_HINT_ENUM_FALSE) {
		pk_transaction_list_set_background (priv->transaction_list,
					      priv->tid,
					      priv->background);
	}

	/* commit, so it appears in the JobList */
	ret = pk_transaction_list_commit (priv->transaction_list,
					  priv->tid);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		g_warning ("failed to commit (job not run?)");
		return FALSE;
	}

	/* only save into the database for useful stuff */
	if (priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {

		/* add to database */
		pk_transaction_db_add (priv->transaction_db, priv->tid);

		/* save role in the database */
		pk_transaction_db_set_role (priv->transaction_db, priv->tid, priv->role);

		/* save uid */
		pk_transaction_db_set_uid (priv->transaction_db, priv->tid, priv->uid);

#ifdef USE_SECURITY_POLKIT
		/* save cmdline in db */
		if (priv->cmdline != NULL)
			pk_transaction_db_set_cmdline (priv->transaction_db, priv->tid, priv->cmdline);
#endif

		/* report to syslog */
		pk_syslog_add (priv->syslog, PK_SYSLOG_TYPE_INFO, "new %s transaction %s scheduled from uid %i",
			       pk_role_enum_to_string (priv->role), priv->tid, priv->uid);
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
 * pk_transaction_strvalidate_char:
 * @item: A single char to test
 *
 * Tests a char to see if it may be dangerous.
 *
 * Return value: %TRUE if the char is valid
 **/
static gboolean
pk_transaction_strvalidate_char (gchar item)
{
	switch (item) {
	case '$':
	case '`':
	case '\'':
	case '"':
	case '^':
	case '[':
	case ']':
	case '{':
	case '}':
	case '\\':
	case '<':
	case '>':
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_transaction_strvalidate:
 * @text: The text to check for validity
 *
 * Tests a string to see if it may be dangerous or invalid.
 *
 * Return value: %TRUE if the string is valid
 **/
gboolean
pk_transaction_strvalidate (const gchar *text, GError **error)
{
	guint i;
	guint length;

	/* maximum size is 1024 */
	length = pk_strlen (text, 1024);
	if (length > 1024) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
			     "Invalid input passed to daemon: input too long: %u", length);
		return FALSE;
	}

	for (i=0; i<length; i++) {
		if (pk_transaction_strvalidate_char (text[i]) == FALSE) {
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon: char '%c' in text!", text[i]);
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * pk_transaction_search_check_item:
 **/
static gboolean
pk_transaction_search_check_item (const gchar *values, GError **error)
{
	guint size;
	gboolean ret;

	/* limit to a 1k chunk */
	size = pk_strlen (values, 1024);

	if (values == NULL) {
		g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Search is null. This isn't supposed to happen...");
		return FALSE;
	}
	if (size == 0) {
		g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Search string zero length");
		return FALSE;
	}
	if (strstr (values, "*") != NULL) {
		g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing '*'");
		return FALSE;
	}
	if (strstr (values, "?") != NULL) {
		g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing '?'");
		return FALSE;
	}
	if (size == 1024) {
		g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "The search string length is too large");
		return FALSE;
	}
	ret = pk_transaction_strvalidate (values, error);
	if (!ret)
		return FALSE;
	return TRUE;
}

/**
 * pk_transaction_search_check:
 **/
static gboolean
pk_transaction_search_check (gchar **values, GError **error)
{
	guint i;
	gboolean ret = TRUE;

	/* check each parameter */
	for (i=0; values[i] != NULL; i++) {
		ret = pk_transaction_search_check_item (values[i], error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * pk_transaction_filter_check:
 **/
gboolean
pk_transaction_filter_check (const gchar *filter, GError **error)
{
	gchar **sections = NULL;
	guint i;
	guint length;
	gboolean ret = FALSE;

	g_return_val_if_fail (error != NULL, FALSE);

	/* is zero? */
	if (pk_strzero (filter)) {
		g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "filter zero length");
		goto out;
	}

	/* check for invalid input */
	ret = pk_transaction_strvalidate (filter, error);
	if (!ret)
		goto out;

	/* split by delimeter ';' */
	sections = g_strsplit (filter, ";", 0);
	length = g_strv_length (sections);
	for (i=0; i<length; i++) {
		/* only one wrong part is enough to fail the filter */
		if (pk_strzero (sections[i])) {
			ret = FALSE;
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Single empty section of filter: %s", filter);
			goto out;
		}
		if (pk_filter_enum_from_string (sections[i]) == PK_FILTER_ENUM_UNKNOWN) {
			ret = FALSE;
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Unknown filter part: %s", sections[i]);
			goto out;
		}
	}
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
	gchar *message;
	GError *error = NULL;
	PkTransactionPrivate *priv = transaction->priv;

	/* finish the call */
	result = polkit_authority_check_authorization_finish (priv->authority, res, &error);
	priv->waiting_for_auth = FALSE;

	/* failed because the request was cancelled */
	ret = g_cancellable_is_cancelled (priv->cancellable);
	if (ret) {
		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_error_code_emit (transaction, PK_ERROR_ENUM_NOT_AUTHORIZED, "The authentication was cancelled due to a timeout.");
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_FAILED, 0);
		goto out;
	}

	/* failed, maybe polkit is messed up? */
	if (result == NULL) {
		g_warning ("failed to check for auth: %s", error->message);

		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);
		message = g_strdup_printf ("Failed to check for authentication: %s", error->message);
		pk_transaction_error_code_emit (transaction, PK_ERROR_ENUM_NOT_AUTHORIZED, message);
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_FAILED, 0);
		g_error_free (error);
		g_free (message);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {

		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_error_code_emit (transaction, PK_ERROR_ENUM_NOT_AUTHORIZED,
						"Failed to obtain authentication.");
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_FAILED, 0);

		pk_syslog_add (priv->syslog, PK_SYSLOG_TYPE_AUTH, "uid %i failed to obtain auth", priv->uid);
		goto out;
	}

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		g_warning ("Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* log success too */
	pk_syslog_add (priv->syslog, PK_SYSLOG_TYPE_AUTH, "uid %i obtained auth", priv->uid);
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
		case PK_ROLE_ENUM_UPGRADE_SYSTEM:
			policy = "org.freedesktop.packagekit.upgrade-system";
			break;
		case PK_ROLE_ENUM_REPAIR_SYSTEM:
			policy = "org.freedesktop.packagekit.repair-system";
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
	const gchar *text;
	gboolean ret = FALSE;
	gchar *package_ids = NULL;
	GString *string = NULL;
	PkTransactionPrivate *priv = transaction->priv;

	g_return_val_if_fail (priv->sender != NULL, FALSE);

	/* we should always have subject */
	if (priv->subject == NULL) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
				      "subject %s not found", priv->sender);
		goto out;
	}

	/* map the roles to policykit rules */
	if (only_trusted)
		action_id = pk_transaction_role_to_action_only_trusted (role);
	else
		action_id = pk_transaction_role_to_action_allow_untrusted (role);
	if (action_id == NULL) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_REFUSED_BY_POLICY, "policykit type required for '%s'", pk_role_enum_to_string (role));
		goto out;
	}

	/* log */
	pk_syslog_add (priv->syslog, PK_SYSLOG_TYPE_AUTH, "uid %i is trying to obtain %s auth (only_trusted:%i)", priv->uid, action_id, only_trusted);

	/* set transaction state */
	pk_transaction_set_state (transaction,
				  PK_TRANSACTION_STATE_WAITING_FOR_AUTH);

	/* check subject */
	priv->waiting_for_auth = TRUE;

	/* insert details about the authorization */
	details = polkit_details_new ();

	/* do we have package details? */
	if (priv->cached_package_id != NULL)
		package_ids = g_strdup (priv->cached_package_id);
	else if (priv->cached_package_ids != NULL)
		package_ids = pk_package_ids_to_string (priv->cached_package_ids);

	/* save optional stuff */
	if (package_ids != NULL)
		polkit_details_insert (details, "package_ids", package_ids);
	if (priv->cmdline != NULL)
		polkit_details_insert (details, "cmdline", priv->cmdline);

	/* do not use the default icon and wording for some roles */
	if (!priv->cached_only_trusted) {

		/* don't use the friendly PackageKit icon as this is
		 * might be a ricky authorisation */
		polkit_details_insert (details, "polkit.icon_name", "emblem-important");

		string = g_string_new ("");

		/* TRANSLATORS: is not GPG signed */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("The software is not from a trusted source.")));
		g_string_append (string, "\n");

		/* UpdatePackages */
		if (priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {

			/* TRANSLATORS: user has to trust provider -- I know, this sucks */
			text = g_dngettext (GETTEXT_PACKAGE,
					    N_("Do not update this package unless you are sure it is safe to do so."),
					    N_("Do not update these packages unless you are sure it is safe to do so."),
					    g_strv_length (priv->cached_package_ids));
			g_string_append (string, text);
		}

		/* InstallPackages */
		if (priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {

			/* TRANSLATORS: user has to trust provider -- I know, this sucks */
			text = g_dngettext (GETTEXT_PACKAGE,
					    N_("Do not install this package unless you are sure it is safe to do so."),
					    N_("Do not install these packages unless you are sure it is safe to do so."),
					    g_strv_length (priv->cached_package_ids));
			g_string_append (string, text);
		}
		if (string->len > 0) {
			polkit_details_insert (details, "polkit.gettext_domain", GETTEXT_PACKAGE);
			polkit_details_insert (details, "polkit.message", string->str);
		}
	}

	/* do authorization async */
	polkit_authority_check_authorization (priv->authority,
					      priv->subject,
					      action_id,
					      details,
					      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
					      priv->cancellable,
					      (GAsyncReadyCallback) pk_transaction_action_obtain_authorization_finished_cb,
					      transaction);

	/* check_authorization ref's this */
	g_object_unref (details);

	/* assume success, as this is async */
	ret = TRUE;
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	g_free (package_ids);
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

	g_warning ("*** THERE IS NO SECURITY MODEL BEING USED!!! ***");

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		g_warning ("Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
	}

	return ret;
}
#endif

/**
 * pk_transaction_get_role:
 **/
PkRoleEnum
pk_transaction_get_role (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	return transaction->priv->role;
}

/**
 * pk_transaction_dbus_return:
 **/
static void
pk_transaction_dbus_return (GDBusMethodInvocation *context, GError *error)
{
	/* not set inside the test suite */
	if (context == NULL) {
		if (error != NULL) {
			g_warning ("context null, and error: %s", error->message);
			g_error_free (error);
		}
		return;
	}
	if (error != NULL)
		g_dbus_method_invocation_return_gerror (context, error);
	else
		g_dbus_method_invocation_return_value (context, NULL);
}

/**
 * pk_transaction_accept_eula:
 *
 * This should be called when a eula_id needs to be added into an internal db.
 **/
static void
pk_transaction_accept_eula (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	guint idle_id;
	const gchar *eula_id = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_ACCEPT_EULA);

	g_variant_get (params, "(&s)",
		       &eula_id);

	/* check for sanity */
	ret = pk_transaction_strvalidate (eula_id, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_ACCEPT_EULA,
						   &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	g_debug ("AcceptEula method called: %s", eula_id);
	ret = pk_backend_accept_eula (transaction->priv->backend, eula_id);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "EULA failed to be added");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* we are done */
	idle_id = g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);
	g_source_set_name_by_id (idle_id, "[PkTransaction] finished from accept");
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_cancel_bg:
 **/
void
pk_transaction_cancel_bg (PkTransaction *transaction)
{
	g_debug ("CancelBg method called on %s", transaction->priv->tid);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_CANCEL)) {
		g_warning ("Cancel not supported by backend");
		goto out;
	}

	/* if it's never been run, just remove this transaction from the list */
	if (transaction->priv->state <= PK_TRANSACTION_STATE_READY) {
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
	pk_backend_set_exit_code (transaction->priv->backend, PK_EXIT_ENUM_CANCELLED_PRIORITY);

	/* actually run the method */
	pk_backend_cancel (transaction->priv->backend);
out:
	return;
}

/**
 * pk_transaction_cancel:
 **/
static void
pk_transaction_cancel (PkTransaction *transaction,
		       GVariant *params,
		       GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *sender;
	guint uid;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("Cancel method called on %s", transaction->priv->tid);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_CANCEL)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Cancel not supported by backend");
		goto out;
	}

	/* if it's finished, cancelling will have no action regardless of uid */
	if (transaction->priv->finished) {
		g_debug ("No point trying to cancel a finished transaction, ignoring");

		/* return from async with success */
		pk_transaction_dbus_return (context, NULL);
		goto out;
	}

	/* check to see if we have an action */
	if (transaction->priv->role == PK_ROLE_ENUM_UNKNOWN) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_ROLE, "No role");
		goto out;
	}

	/* first, check the sender -- if it's the same we don't need to check the uid */
	sender = g_dbus_method_invocation_get_sender (context);
	ret = (g_strcmp0 (transaction->priv->sender, sender) == 0);
	if (ret) {
		g_debug ("same sender, no need to check uid");
		goto skip_uid;
	}

	/* check if we saved the uid */
	if (transaction->priv->uid == PK_TRANSACTION_UID_INVALID) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_CANNOT_CANCEL,
				     "No context from caller to get UID from");
		goto out;
	}

	/* get the UID of the caller */
	uid = pk_dbus_get_uid (transaction->priv->dbus, sender);
	if (uid == PK_TRANSACTION_UID_INVALID) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_STATE, "unable to get uid of caller");
		goto out;
	}

	/* check the caller uid with the originator uid */
	if (transaction->priv->uid != uid) {
		g_debug ("uid does not match (%i vs. %i)", transaction->priv->uid, uid);
		ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_CANCEL, &error);
		if (!ret) {
				goto out;
		}
	}

skip_uid:
	/* if it's never been run, just remove this transaction from the list */
	if (transaction->priv->state <= PK_TRANSACTION_STATE_READY) {
		pk_transaction_progress_changed_emit (transaction, 100, 100, 0, 0);
		pk_transaction_allow_cancel_emit (transaction, FALSE);
		pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_CANCELLED, 0);
		pk_transaction_release_tid (transaction);

		/* return from async with success */
		pk_transaction_dbus_return (context, NULL);
		goto out;
	}

	/* set the state, as cancelling might take a few seconds */
	pk_backend_set_status (transaction->priv->backend, PK_STATUS_ENUM_CANCEL);

	/* we don't want to cancel twice */
	pk_backend_set_allow_cancel (transaction->priv->backend, FALSE);

	/* we need ::finished to not return success or failed */
	pk_backend_set_exit_code (transaction->priv->backend, PK_EXIT_ENUM_CANCELLED);

	/* actually run the method */
	pk_backend_cancel (transaction->priv->backend);
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_download_packages:
 **/
static void
pk_transaction_download_packages (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	gchar *directory = NULL;
	gint retval;
	guint length;
	guint max_length;
	gboolean store_in_cache;
	gchar **package_ids = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b^a&s)",
		       &store_in_cache,
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("DownloadPackages method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_DOWNLOAD_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "DownloadPackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		goto out;
	}

	/* create cache directory */
	if (!store_in_cache) {
		directory = g_build_filename (LOCALSTATEDIR, "cache", "PackageKit",
					     "downloads", transaction->priv->tid, NULL);
		/* rwxrwxr-x */
		retval = g_mkdir (directory, 0775);
		if (retval != 0) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_DENIED,
					     "cannot create %s", directory);
			goto out;
		}
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
		goto out;
	}
out:
	g_free (package_ids_temp);
	g_free (directory);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_categories:
 **/
static void
pk_transaction_get_categories (PkTransaction *transaction,
			       GVariant *params,
			       GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("GetCategories method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_CATEGORIES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetCategories not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_CATEGORIES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_depends:
 **/
static void
pk_transaction_get_depends (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	const gchar *filter;
	gchar **package_ids;
	gboolean recursive;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&sb)",
		       &filter,
		       &package_ids,
		       &recursive);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetDepends method called: %s (recursive %i)", package_ids_temp, recursive);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_DEPENDS)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetDepends not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_force = recursive;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DEPENDS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_details:
 **/
static void
pk_transaction_get_details (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetDetails method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_DETAILS)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetDetails not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_string (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
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
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_distro_upgrades:
 **/
static void
pk_transaction_get_distro_upgrades (PkTransaction *transaction,
				    GVariant *params,
				    GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("GetDistroUpgrades method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetDistroUpgrades not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_files:
 **/
static void
pk_transaction_get_files (PkTransaction *transaction,
			  GVariant *params,
			  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetFiles method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_FILES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetFiles not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
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
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_packages:
 **/
static void
pk_transaction_get_packages (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *filter;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
		       &filter);

	g_debug ("GetPackages method called: %s", filter);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetPackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_old_transactions:
 **/
static void
pk_transaction_get_old_transactions (PkTransaction *transaction,
				     GVariant *params,
				     GDBusMethodInvocation *context)
{
	guint idle_id;
	guint number;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(u)",
		       &number);

	g_debug ("GetOldTransactions method called");

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_OLD_TRANSACTIONS);
	pk_transaction_db_get_list (transaction->priv->transaction_db, number);
	idle_id = g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);
	g_source_set_name_by_id (idle_id, "[PkTransaction] finished from get-old-transactions");

	pk_transaction_dbus_return (context, NULL);
}

/**
 * pk_transaction_get_repo_list:
 **/
static void
pk_transaction_get_repo_list (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *filter;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
		       &filter);

	g_debug ("GetRepoList method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_REPO_LIST)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetRepoList not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REPO_LIST);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_requires:
 **/
static void
pk_transaction_get_requires (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	const gchar *filter;
	gchar **package_ids;
	gboolean recursive;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&sb)",
		       &filter,
		       &package_ids,
		       &recursive);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetRequires method called: %s (recursive %i)", package_ids_temp, recursive);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_REQUIRES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetRequires not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_force = recursive;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REQUIRES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_get_update_detail:
 **/
static void
pk_transaction_get_update_detail (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetUpdateDetail method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_UPDATE_DETAIL)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetUpdateDetail not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
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
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_try_emit_cache:
 **/
static gboolean
pk_transaction_try_emit_cache (PkTransaction *transaction)
{
	PkResults *results;
	gboolean ret = FALSE;
	GPtrArray *package_array = NULL;
	GPtrArray *message_array = NULL;
	PkPackage *package;
	PkMessage *message;
	PkExitEnum exit_enum;
	guint i;
	guint idle_id;

	/* not allowed to use a cache */
	ret = pk_conf_get_bool (transaction->priv->conf, "UseUpdateCache");
	if (!ret)
		goto out;

	/* get results */
	results = pk_cache_get_results (transaction->priv->cache, transaction->priv->role);
	if (results == NULL)
		goto out;

	/* failed last time */
	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_warning ("failed last time with: %s", pk_exit_enum_to_string (exit_enum));
		goto out;
	}

	g_debug ("we have cached data we should use");

	/* packages */
	package_array = pk_results_get_package_array (results);
	for (i=0; i<package_array->len; i++) {
		package = g_ptr_array_index (package_array, i);
		g_dbus_connection_emit_signal (transaction->priv->connection,
					       NULL,
					       transaction->priv->tid,
					       PK_DBUS_INTERFACE_TRANSACTION,
					       "Package",
					       g_variant_new ("(sss)",
							      pk_info_enum_to_string (pk_package_get_info (package)),
							      pk_package_get_id (package),
							      pk_package_get_summary (package)),
					       NULL);
	}

	/* messages */
	message_array = pk_results_get_message_array (results);
	for (i=0; i<message_array->len; i++) {
		message = g_ptr_array_index (message_array, i);
		g_dbus_connection_emit_signal (transaction->priv->connection,
					       NULL,
					       transaction->priv->tid,
					       PK_DBUS_INTERFACE_TRANSACTION,
					       "Message",
					       g_variant_new ("(ss)",
							      pk_message_enum_to_string (pk_message_get_kind (message)),
							      pk_message_get_details (message)),
					       NULL);
	}

	/* success */
	ret = TRUE;

	/* set finished */
	pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_FINISHED);

	/* we are done */
	idle_id = g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);
	g_source_set_name_by_id (idle_id, "[PkTransaction] try-emit-cache");
out:
	if (package_array != NULL)
		g_ptr_array_unref (package_array);
	if (message_array != NULL)
		g_ptr_array_unref (message_array);
	return ret;
}

/**
 * pk_transaction_get_updates:
 **/
void
pk_transaction_get_updates (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *filter;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
		       &filter);

	g_debug ("GetUpdates method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_UPDATES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "GetUpdates not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATES);

	/* try and reuse cache */
	ret = pk_transaction_try_emit_cache (transaction);
	if (ret)
		goto out;

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
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
		g_set_error (error, 1, 0, "failed to get file attributes for %s: %s", filename, error_local->message);
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
pk_transaction_is_supported_content_type (PkTransaction *transaction,
					  const gchar *content_type)
{
	const gchar *mime_type_tmp;
	gboolean ret = FALSE;
	GPtrArray *array = transaction->priv->supported_content_types;
	guint i;

	/* can we support this one? */
	for (i=0; i<array->len; i++) {
		mime_type_tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (mime_type_tmp, content_type) == 0) {
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * pk_transaction_install_files:
 **/
static void
pk_transaction_install_files (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gchar *full_paths_temp;
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	PkServicePack *service_pack;
	gchar *content_type = NULL;
	guint length;
	guint i;
	gboolean only_trusted;
	gchar **full_paths;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b^a&s)",
		       &only_trusted,
		       &full_paths);

	full_paths_temp = pk_package_ids_to_string (full_paths);
	g_debug ("InstallFiles method called: %s (only_trusted %i)", full_paths_temp, only_trusted);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_INSTALL_FILES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "InstallFiles not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_CONTENT_TYPES);

	/* check all files exists and are valid */
	length = g_strv_length (full_paths);

	for (i=0; i<length; i++) {
		/* exists */
		ret = g_file_test (full_paths[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_SUCH_FILE,
					     "No such file %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
				goto out;
		}

		/* get content type */
		content_type = pk_transaction_get_content_type_for_file (full_paths[i], &error_local);
		if (content_type == NULL) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Failed to get content type for file %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
				goto out;
		}

		/* supported content type? */
		ret = pk_transaction_is_supported_content_type (transaction, content_type);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_MIME_TYPE_NOT_SUPPORTED,
					     "MIME type '%s' not supported %s", content_type, full_paths[i]);
			pk_transaction_release_tid (transaction);
				goto out;
		}

		/* valid */
		if (g_str_has_suffix (full_paths[i], ".servicepack")) {
			service_pack = pk_service_pack_new ();
			ret = pk_service_pack_check_valid (service_pack, full_paths[i], &error_local);
			g_object_unref (service_pack);
			if (!ret) {
				error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACK_INVALID, "%s", error_local->message);
				pk_transaction_release_tid (transaction);
						g_error_free (error_local);
				goto out;
			}
		}
	}

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_INSTALL_FILES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_FILES);
out:
	g_free (full_paths_temp);
	g_free (content_type);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_install_packages:
 **/
static void
pk_transaction_install_packages (PkTransaction *transaction,
				 GVariant *params,
				 GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gboolean only_trusted;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b^a&s)",
		       &only_trusted,
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("InstallPackages method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_INSTALL_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "InstallPackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_PACKAGES);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_INSTALL_PACKAGES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_install_signature:
 **/
static void
pk_transaction_install_signature (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *sig_type;
	const gchar *key_id;
	const gchar *package_id;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s&s&s)",
		       &sig_type,
		       &key_id,
		       &package_id);

	g_debug ("InstallSignature method called: %s, %s", key_id, package_id);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_INSTALL_SIGNATURE)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "InstallSignature not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (key_id, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_id (';;;repo-id' is used for the repo key) */
	ret = pk_package_id_check (package_id);
	if (!ret && !g_str_has_prefix (package_id, ";;;")) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id '%s' is not valid", package_id);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->cached_key_id = g_strdup (key_id);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_SIGNATURE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_INSTALL_SIGNATURE,
						   &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_refresh_cache:
 **/
static void
pk_transaction_refresh_cache (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gboolean force;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b)",
		       &force);

	g_debug ("RefreshCache method called: %i", force);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REFRESH_CACHE)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RefreshCache not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* we unref the update cache if it exists */
	pk_cache_invalidate (transaction->priv->cache);

	/* save so we can run later */
	transaction->priv->cached_force = force;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REFRESH_CACHE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_REFRESH_CACHE,
						   &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_remove_packages:
 **/
static void
pk_transaction_remove_packages (PkTransaction *transaction,
				GVariant *params,
				GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;
	gboolean allow_deps;
	gboolean autoremove;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&sbb)",
		       &package_ids,
		       &allow_deps,
		       &autoremove);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("RemovePackages method called: %s, %i, %i",
		 package_ids_temp, allow_deps, autoremove);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REMOVE_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RemovePackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_allow_deps = allow_deps;
	transaction->priv->cached_autoremove = autoremove;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REMOVE_PACKAGES);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_REMOVE_PACKAGES,
						   &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_repo_enable:
 **/
static void
pk_transaction_repo_enable (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *repo_id;
	gboolean enabled;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&sb)",
		       &repo_id,
		       &enabled);

	g_debug ("RepoEnable method called: %s, %i", repo_id, enabled);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REPO_ENABLE)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RepoEnable not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (repo_id, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_enabled = enabled;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_ENABLE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_REPO_ENABLE, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_repo_set_data:
 **/
static void
pk_transaction_repo_set_data (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s&s&s)",
		       &repo_id,
		       &parameter,
		       &value);

	g_debug ("RepoSetData method called: %s, %s, %s",
		 repo_id, parameter, value);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REPO_SET_DATA)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RepoSetData not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (repo_id, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_parameter = g_strdup (parameter);
	transaction->priv->cached_value = g_strdup (value);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_SET_DATA);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_REPO_SET_DATA,
						   &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_resolve:
 **/
static void
pk_transaction_resolve (PkTransaction *transaction,
			GVariant *params,
			GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *packages_temp;
	guint i;
	guint length;
	guint max_length;
	const gchar *filter;
	gchar **packages;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&s)",
		       &filter,
		       &packages);

	packages_temp = pk_package_ids_to_string (packages);
	g_debug ("Resolve method called: %s, %s", filter, packages_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_RESOLVE)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Resolve not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (packages);
	if (length == 0) {
		error = g_error_new (PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Too few items to process");
		pk_transaction_release_tid (transaction);
		goto out;
	}
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumItemsToResolve");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Too many items to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check each package for sanity */
	for (i=0; i<length; i++) {
		ret = pk_transaction_strvalidate (packages[i], &error);
		if (!ret) {
			pk_transaction_release_tid (transaction);
				return;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (packages);
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_RESOLVE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (packages_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_rollback:
 **/
static void
pk_transaction_rollback (PkTransaction *transaction,
			 GVariant *params,
			 GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *transaction_id;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
		       &transaction_id);

	g_debug ("Rollback method called: %s", transaction_id);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_ROLLBACK)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Rollback not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (transaction_id, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_id = g_strdup (transaction_id);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_ROLLBACK);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_ROLLBACK, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_search_details:
 **/
void
pk_transaction_search_details (PkTransaction *transaction,
			       GVariant *params,
			       GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *filter;
	gchar **values;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchDetails method called: %s, %s", filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_DETAILS)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchDetails not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_DETAILS);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_search_files:
 **/
static void
pk_transaction_search_files (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	guint i;
	const gchar *filter;
	gchar **values;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchFiles method called: %s, %s", filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_FILE)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchFiles not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* when not an absolute path, disallow slashes in search */
	for (i=0; values[i] != NULL; i++) {
		if (values[i][0] != '/' && strstr (values[i], "/") != NULL) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_PATH_INVALID,
					     "Invalid search path");
			pk_transaction_release_tid (transaction);
				return;
		}
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_FILE);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_search_groups:
 **/
static void
pk_transaction_search_groups (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	guint i;
	const gchar *filter;
	gchar **values;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchGroups method called: %s, %s", filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_GROUP)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchGroups not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* do not allow spaces */
	for (i=0; values[i] != NULL; i++) {
		if (strstr (values[i], " ") != NULL) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_SEARCH_INVALID,
					     "Invalid search containing spaces");
			pk_transaction_release_tid (transaction);
				return;
		}
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_GROUP);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_search_name:
 **/
void
pk_transaction_search_names (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	const gchar *filter;
	gchar **values;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchNames method called: %s, %s", filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_NAME)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SearchNames not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_NAME);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_set_hint:
 *
 * Only return FALSE on error, not invalid parameter name
 */
static gboolean
pk_transaction_set_hint (PkTransaction *transaction,
			 const gchar *key,
			 const gchar *value,
			 GError **error)
{
	gboolean ret = TRUE;
	PkTransactionPrivate *priv = transaction->priv;

	/* locale=en_GB.utf8 */
	if (g_strcmp0 (key, "locale") == 0) {

		/* already set */
		if (priv->locale != NULL) {
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					      "Already set locale to %s", priv->locale);
			ret = FALSE;
			goto out;
		}

		/* success */
		priv->locale = g_strdup (value);
		goto out;
	}

	/* frontend_socket=/tmp/socket.3456 */
	if (g_strcmp0 (key, "frontend-socket") == 0) {

		/* already set */
		if (priv->frontend_socket != NULL) {
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "Already set frontend-socket to %s", priv->frontend_socket);
			ret = FALSE;
			goto out;
		}

		/* nothing provided */
		if (value == NULL || value[0] == '\0') {
			g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Could not set frontend-socket to nothing");
			ret = FALSE;
			goto out;
		}

		/* nothing provided */
		if (value[0] != '/') {
			g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "frontend-socket has to be an absolute path");
			ret = FALSE;
			goto out;
		}

		/* socket does not exist */
		if (!g_file_test (value, G_FILE_TEST_EXISTS)) {
			g_set_error_literal (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "frontend-socket does not exist");
			ret = FALSE;
			goto out;
		}

		/* success */
		priv->frontend_socket = g_strdup (value);
		goto out;
	}

	/* background=true */
	if (g_strcmp0 (key, "background") == 0) {
		priv->background = pk_hint_enum_from_string (value);
		if (priv->background == PK_HINT_ENUM_INVALID) {
			priv->background = PK_HINT_ENUM_UNSET;
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					      "background hint expects true or false, not %s", value);
			ret = FALSE;
		}
		goto out;
	}

	/* interactive=true */
	if (g_strcmp0 (key, "interactive") == 0) {
		priv->interactive = pk_hint_enum_from_string (value);
		if (priv->interactive == PK_HINT_ENUM_INVALID) {
			priv->interactive = PK_HINT_ENUM_UNSET;
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					      "interactive hint expects true or false, not %s", value);
			ret = FALSE;
		}
		goto out;
	}

	/* cache-age=<time-in-seconds> */
	if (g_strcmp0 (key, "cache-age") == 0) {
		ret = pk_strtouint (value, &priv->cache_age);
		if (!ret) {
			priv->cache_age = G_MAXUINT;
			g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "cannot parse cache age value %s", value);
			ret = FALSE;
		}
		if (priv->cache_age == 0) {
			priv->cache_age = G_MAXUINT;
			g_set_error_literal (error, PK_TRANSACTION_ERROR,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "cannot set a cache age of zero");
			ret = FALSE;
		}
		goto out;
	}

	/* to preserve forwards and backwards compatibility, we ignore extra options here */
	g_warning ("unknown option: %s with value %s", key, value);
out:
	return ret;
}

/**
 * pk_transaction_set_hints:
 */
static void
pk_transaction_set_hints (PkTransaction *transaction,
			  GVariant *params,
			  GDBusMethodInvocation *context)
{
	GError *error = NULL;
	gboolean ret;
	guint i;
	gchar **sections;
	gchar *dbg;
	const gchar **hints = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &hints);

	dbg = g_strjoinv (", ", (gchar**) hints);
	g_debug ("SetHints method called: %s", dbg);

	/* parse */
	for (i=0; hints[i] != NULL; i++) {
		sections = g_strsplit (hints[i], "=", 2);
		if (g_strv_length (sections) == 2) {
			ret = pk_transaction_set_hint (transaction, sections[0], sections[1], &error);
		} else {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Could not parse hint '%s'", hints[i]);
			ret = FALSE;
		}
		g_strfreev (sections);

		/* we failed, so abort current list */
		if (!ret)
			goto out;
	}
out:
	g_free (dbg);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_simulate_install_files:
 **/
static void
pk_transaction_simulate_install_files (PkTransaction *transaction,
				       GVariant *params,
				       GDBusMethodInvocation *context)
{
	gchar *full_paths_temp;
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	PkServicePack *service_pack;
	gchar *content_type;
	guint length;
	guint i;
	gchar **full_paths;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &full_paths);

	full_paths_temp = pk_package_ids_to_string (full_paths);
	g_debug ("SimulateInstallFiles method called: %s", full_paths_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SIMULATE_INSTALL_FILES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SimulateInstallFiles not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* run the plugins */
	pk_transaction_plugin_phase (transaction,
				     PK_PLUGIN_PHASE_TRANSACTION_CONTENT_TYPES);

	/* check all files exists and are valid */
	length = g_strv_length (full_paths);

	for (i=0; i<length; i++) {
		/* exists */
		ret = g_file_test (full_paths[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NO_SUCH_FILE,
					     "No such file %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
			goto out;
		}

		/* get content type */
		content_type = pk_transaction_get_content_type_for_file (full_paths[i], &error_local);
		if (content_type == NULL) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Failed to get content type for file %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
			goto out;
		}

		/* supported content type? */
		ret = pk_transaction_is_supported_content_type (transaction, content_type);
		g_free (content_type);
		if (!ret) {
			error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_MIME_TYPE_NOT_SUPPORTED,
					     "MIME type not supported %s", full_paths[i]);
			pk_transaction_release_tid (transaction);
			goto out;
		}

		/* valid */
		if (g_str_has_suffix (full_paths[i], ".servicepack")) {
			service_pack = pk_service_pack_new ();
			ret = pk_service_pack_check_valid (service_pack, full_paths[i], &error_local);
			g_object_unref (service_pack);
			if (!ret) {
				error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACK_INVALID, "%s", error_local->message);
				pk_transaction_release_tid (transaction);
				g_error_free (error_local);
				goto out;
			}
		}
	}

	/* save so we can run later */
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SIMULATE_INSTALL_FILES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (full_paths_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_simulate_install_packages:
 **/
static void
pk_transaction_simulate_install_packages (PkTransaction *transaction,
					  GVariant *params,
					  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	g_debug ("SimulateInstallPackages method called: %s", package_ids[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SimulateInstallPackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_string (package_ids);
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_simulate_remove_packages:
 **/
static void
pk_transaction_simulate_remove_packages (PkTransaction *transaction,
					 GVariant *params,
					 GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;
	gboolean autoremove;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&sb)",
		       &package_ids,
		       &autoremove);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("SimulateRemovePackages method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SimulateRemovePackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_autoremove = autoremove;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_simulate_update_packages:
 **/
static void
pk_transaction_simulate_update_packages (PkTransaction *transaction,
					 GVariant *params,
					 GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("SimulateUpdatePackages method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "SimulateUpdatePackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_update_packages:
 **/
static void
pk_transaction_update_packages (PkTransaction *transaction,
				GVariant *params,
				GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gchar *package_ids_temp;
	guint length;
	guint max_length;
	gboolean only_trusted;
	gchar **package_ids;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b^a&s)",
		       &only_trusted,
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("UpdatePackages method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "UpdatePackages not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	max_length = pk_conf_get_int (transaction->priv->conf, "MaximumPackagesToProcess");
	if (length > max_length) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "Too many packages to process (%i/%i)", length, max_length);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
				     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_PACKAGES);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_UPDATE_PACKAGES, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	g_free (package_ids_temp);
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_update_system:
 **/
static void
pk_transaction_update_system (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gboolean only_trusted;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b)",
		       &only_trusted);

	g_debug ("UpdateSystem method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "UpdateSystem not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* are we already performing an update? */
	if (pk_transaction_list_role_present (transaction->priv->transaction_list, PK_ROLE_ENUM_UPDATE_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
				     "Already performing system update");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	transaction->priv->cached_only_trusted = only_trusted;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_SYSTEM);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction, only_trusted, PK_ROLE_ENUM_UPDATE_SYSTEM, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_what_provides:
 **/
static void
pk_transaction_what_provides (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	PkProvidesEnum provides;
	GError *error = NULL;
	const gchar *filter;
	const gchar *type;
	gchar **values;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s&s^a&s)",
		       &filter,
		       &type,
		       &values);

	g_debug ("WhatProvides method called: %s, %s", type, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_WHAT_PROVIDES)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "WhatProvides not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check the filter */
	ret = pk_transaction_filter_check (filter, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check provides */
	provides = pk_provides_enum_from_string (type);
	if (provides == PK_PROVIDES_ENUM_UNKNOWN) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_PROVIDE,
				     "provide type '%s' not found", type);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = pk_filter_bitfield_from_string (filter);
	transaction->priv->cached_values = g_strdupv (values);
	transaction->priv->cached_provides = provides;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_WHAT_PROVIDES);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
				     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_upgrade_system:
 **/
static void
pk_transaction_upgrade_system (PkTransaction *transaction,
			       GVariant *params,
			       GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	PkUpgradeKindEnum upgrade_kind;
	const gchar *distro_id;
	const gchar *upgrade_kind_str;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s*s)",
		       &distro_id,
		       &upgrade_kind_str);

	g_debug ("UpgradeSystem method called: %s (%s)",
		 distro_id, upgrade_kind_str);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_UPGRADE_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "UpgradeSystem not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* check upgrade kind */
	upgrade_kind = pk_upgrade_kind_enum_from_string (upgrade_kind_str);
	if (upgrade_kind == PK_UPGRADE_KIND_ENUM_UNKNOWN) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INVALID_PROVIDE,
				     "upgrade kind '%s' not found", upgrade_kind_str);
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_value = g_strdup (distro_id);
	transaction->priv->cached_provides = upgrade_kind;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPGRADE_SYSTEM);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   FALSE,
						   PK_ROLE_ENUM_UPGRADE_SYSTEM, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * pk_transaction_simulate_repair_system:
 **/
static void
pk_transaction_simulate_repair_system (PkTransaction *transaction,
                                       GVariant *params,
                                       GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("SimulateRepairSystem method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
	                                PK_ROLE_ENUM_SIMULATE_REPAIR_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
		                     "SimulateRepairSystem not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SIMULATE_REPAIR_SYSTEM);

	/* try to commit this */
	ret = pk_transaction_commit (transaction);
	if (!ret) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_COMMIT_FAILED,
		                     "Could not commit to a transaction object");
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}


/**
 * pk_transaction_repair_system:
 **/
static void
pk_transaction_repair_system (PkTransaction *transaction,
                              GVariant *params,
                              GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	gboolean only_trusted;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b)", &only_trusted);

	g_debug ("RepairSystem method called (only trusted %i)", only_trusted);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
	                                PK_ROLE_ENUM_REPAIR_SYSTEM)) {
		error = g_error_new (PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_NOT_SUPPORTED,
		                     "RepairSystem not supported by backend");
		pk_transaction_release_tid (transaction);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_only_trusted = only_trusted;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPAIR_SYSTEM);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
	                                           only_trusted,
	                                           PK_ROLE_ENUM_REPAIR_SYSTEM, &error);
	if (!ret) {
		pk_transaction_release_tid (transaction);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

/**
 * _g_variant_new_maybe_string:
 **/
static GVariant *
_g_variant_new_maybe_string (const gchar *value)
{
	if (value == NULL)
		return g_variant_new_string ("");
	return g_variant_new_string (value);
}

/**
 * pk_transaction_get_property:
 **/
static GVariant *
pk_transaction_get_property (GDBusConnection *connection_, const gchar *sender,
			     const gchar *object_path, const gchar *interface_name,
			     const gchar *property_name, GError **error,
			     gpointer user_data)
{
	GVariant *retval = NULL;
	PkTransaction *transaction = PK_TRANSACTION (user_data);
	PkTransactionPrivate *priv = transaction->priv;

	if (g_strcmp0 (property_name, "Role") == 0) {
		retval = g_variant_new_string (pk_role_enum_to_string (priv->role));
		goto out;
	}
	if (g_strcmp0 (property_name, "Status") == 0) {
		retval = g_variant_new_string (pk_status_enum_to_string (priv->status));
		goto out;
	}
	if (g_strcmp0 (property_name, "LastPackage") == 0) {
		retval = _g_variant_new_maybe_string (priv->last_package_id);
		goto out;
	}
	if (g_strcmp0 (property_name, "Uid") == 0) {
		retval = g_variant_new_uint32 (priv->uid);
		goto out;
	}
	if (g_strcmp0 (property_name, "Percentage") == 0) {
		retval = g_variant_new_uint32 (transaction->priv->percentage);
		goto out;
	}
	if (g_strcmp0 (property_name, "Subpercentage") == 0) {
		retval = g_variant_new_uint32 (priv->subpercentage);
		goto out;
	}
	if (g_strcmp0 (property_name, "AllowCancel") == 0) {
		retval = g_variant_new_boolean (priv->allow_cancel);
		goto out;
	}
	if (g_strcmp0 (property_name, "CallerActive") == 0) {
		retval = g_variant_new_boolean (priv->caller_active);
		goto out;
	}
	if (g_strcmp0 (property_name, "ElapsedTime") == 0) {
		retval = g_variant_new_uint32 (priv->elapsed_time);
		goto out;
	}
	if (g_strcmp0 (property_name, "RemainingTime") == 0) {
		retval = g_variant_new_uint32 (priv->remaining_time);
		goto out;
	}
	if (g_strcmp0 (property_name, "Speed") == 0) {
		retval = g_variant_new_uint32 (priv->speed);
		goto out;
	}
out:
	return retval;
}

/**
 * pk_transaction_method_call:
 **/
static void
pk_transaction_method_call (GDBusConnection *connection_, const gchar *sender,
			    const gchar *object_path, const gchar *interface_name,
			    const gchar *method_name, GVariant *parameters,
			    GDBusMethodInvocation *invocation, gpointer user_data)
{
	PkTransaction *transaction = PK_TRANSACTION (user_data);
	gboolean ret = TRUE;

	g_return_if_fail (transaction->priv->sender != NULL);

	/* check is the same as the sender that did GetTid */
	ret = (g_strcmp0 (transaction->priv->sender, sender) == 0);
	if (!ret) {
		g_dbus_method_invocation_return_error (invocation,
						       PK_TRANSACTION_ERROR,
						       PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
						       "sender does not match (%s vs %s)",
						       sender,
						       transaction->priv->sender);
		goto out;
	}

	if (g_strcmp0 (method_name, "SetHints") == 0) {
		pk_transaction_set_hints (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "AcceptEula") == 0) {
		pk_transaction_accept_eula (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "Cancel") == 0) {
		pk_transaction_cancel (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "DownloadPackages") == 0) {
		pk_transaction_download_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetCategories") == 0) {
		pk_transaction_get_categories (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetDepends") == 0) {
		pk_transaction_get_depends (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetDetails") == 0) {
		pk_transaction_get_details (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetFiles") == 0) {
		pk_transaction_get_files (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetOldTransactions") == 0) {
		pk_transaction_get_old_transactions (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetPackages") == 0) {
		pk_transaction_get_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetRepoList") == 0) {
		pk_transaction_get_repo_list (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetRequires") == 0) {
		pk_transaction_get_requires (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetUpdateDetail") == 0) {
		pk_transaction_get_update_detail (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetUpdates") == 0) {
		pk_transaction_get_updates (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetDistroUpgrades") == 0) {
		pk_transaction_get_distro_upgrades (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "InstallFiles") == 0) {
		pk_transaction_install_files (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "InstallPackages") == 0) {
		pk_transaction_install_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "InstallSignature") == 0) {
		pk_transaction_install_signature (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "RefreshCache") == 0) {
		pk_transaction_refresh_cache (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "RemovePackages") == 0) {
		pk_transaction_remove_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "RepoEnable") == 0) {
		pk_transaction_repo_enable (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "RepoSetData") == 0) {
		pk_transaction_repo_set_data (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "Resolve") == 0) {
		pk_transaction_resolve (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "Rollback") == 0) {
		pk_transaction_rollback (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SearchDetails") == 0) {
		pk_transaction_search_details (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SearchFiles") == 0) {
		pk_transaction_search_files (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SearchGroups") == 0) {
		pk_transaction_search_groups (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SearchNames") == 0) {
		pk_transaction_search_names (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SimulateInstallFiles") == 0) {
		pk_transaction_simulate_install_files (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SimulateInstallPackages") == 0) {
		pk_transaction_simulate_install_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SimulateRemovePackages") == 0) {
		pk_transaction_simulate_remove_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SimulateUpdatePackages") == 0) {
		pk_transaction_simulate_update_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "UpdatePackages") == 0) {
		pk_transaction_update_packages (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "UpdateSystem") == 0) {
		pk_transaction_update_system (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "WhatProvides") == 0) {
		pk_transaction_what_provides (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "UpgradeSystem") == 0) {
		pk_transaction_upgrade_system (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "RepairSystem") == 0) {
		pk_transaction_repair_system (transaction, parameters, invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SimulateRepairSystem") == 0) {
		pk_transaction_simulate_repair_system (transaction, parameters, invocation);
		goto out;
	}
out:
	return;
}

/**
 * pk_transaction_set_tid:
 */
gboolean
pk_transaction_set_tid (PkTransaction *transaction, const gchar *tid)
{
	static const GDBusInterfaceVTable interface_vtable = {
		pk_transaction_method_call,
		pk_transaction_get_property,
		NULL
	};

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->tid == NULL, FALSE);

	transaction->priv->tid = g_strdup (tid);

	/* register org.freedesktop.PackageKit.Transaction */
	transaction->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
	g_assert (transaction->priv->connection != NULL);
	transaction->priv->registration_id =
		g_dbus_connection_register_object (transaction->priv->connection,
						   tid,
						   transaction->priv->introspection->interfaces[0],
						   &interface_vtable,
						   transaction,  /* user_data */
						   NULL,  /* user_data_free_func */
						   NULL); /* GError** */
	g_assert (transaction->priv->registration_id > 0);
	return TRUE;
}

/**
 * pk_transaction_add_supported_content_type:
 *
 * Designed to be used by plugins.
 **/
void
pk_transaction_add_supported_content_type (PkTransaction *transaction,
					const gchar *mime_type)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_debug ("added supported content type of %s", mime_type);
	g_ptr_array_add (transaction->priv->supported_content_types,
			 g_strdup (mime_type));
}

/**
 * pk_transaction_setup_mime_types:
 **/
static void
pk_transaction_setup_mime_types (PkTransaction *transaction)
{
	guint i;
	gchar *mime_types_str;
	gchar **mime_types;

	/* get list of mime types supported by backends */
	mime_types_str = pk_backend_get_mime_types (transaction->priv->backend);
	mime_types = g_strsplit (mime_types_str, ";", -1);
	for (i=0; mime_types[i] != NULL; i++) {
		g_ptr_array_add (transaction->priv->supported_content_types,
				 g_strdup (mime_types[i]));
	}

	g_free (mime_types_str);
	g_strfreev (mime_types);
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

	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

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
	transaction->priv->allow_cancel = TRUE;
	transaction->priv->caller_active = TRUE;
	transaction->priv->cached_only_trusted = TRUE;
	transaction->priv->cached_filters = PK_FILTER_ENUM_NONE;
	transaction->priv->uid = PK_TRANSACTION_UID_INVALID;
	transaction->priv->role = PK_ROLE_ENUM_UNKNOWN;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	transaction->priv->percentage = PK_BACKEND_PERCENTAGE_INVALID;
	transaction->priv->subpercentage = PK_BACKEND_PERCENTAGE_INVALID;
	transaction->priv->background = PK_HINT_ENUM_UNSET;
	transaction->priv->state = PK_TRANSACTION_STATE_UNKNOWN;
	transaction->priv->backend = pk_backend_new ();
	transaction->priv->cache = pk_cache_new ();
	transaction->priv->conf = pk_conf_new ();
	transaction->priv->notify = pk_notify_new ();
	transaction->priv->transaction_list = pk_transaction_list_new ();
	transaction->priv->syslog = pk_syslog_new ();
	transaction->priv->dbus = pk_dbus_new ();
	transaction->priv->results = pk_results_new ();
	transaction->priv->supported_content_types = g_ptr_array_new_with_free_func (g_free);
#ifdef USE_SECURITY_POLKIT
	transaction->priv->authority = polkit_authority_get_sync (NULL, &error);
	if (transaction->priv->authority == NULL) {
		g_error ("failed to get pokit authority: %s", error->message);
		g_error_free (error);
	}
	transaction->priv->cancellable = g_cancellable_new ();
#endif

	transaction->priv->transaction_db = pk_transaction_db_new ();
	g_signal_connect (transaction->priv->transaction_db, "transaction",
			  G_CALLBACK (pk_transaction_transaction_cb), transaction);

	/* load introspection from file */
	transaction->priv->introspection = pk_load_introspection (DATADIR "/dbus-1/interfaces/"
								  PK_DBUS_INTERFACE_TRANSACTION ".xml",
								  &error);
	if (transaction->priv->introspection == NULL) {
		g_error ("PkEngine: failed to load transaction introspection: %s",
			 error->message);
		g_error_free (error);
	}

	/* setup supported mime types */
	pk_transaction_setup_mime_types (transaction);
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

	/* were we waiting for the client to authorise */
	if (transaction->priv->waiting_for_auth) {
#ifdef USE_SECURITY_POLKIT
		g_cancellable_cancel (transaction->priv->cancellable);
#endif
		/* emit an ::ErrorCode() and then ::Finished() */
		pk_transaction_error_code_emit (transaction, PK_ERROR_ENUM_NOT_AUTHORIZED, "client did not authorize action");
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_FAILED, 0);
	}

	if (transaction->priv->registration_id > 0) {
		g_dbus_connection_unregister_object (transaction->priv->connection,
						     transaction->priv->registration_id);
		transaction->priv->registration_id = 0;
	}

	/* send signal to clients that we are about to be destroyed */
	if (transaction->priv->connection != NULL) {
		g_debug ("emitting destroy %s", transaction->priv->tid);
		g_dbus_connection_emit_signal (transaction->priv->connection,
					       NULL,
					       transaction->priv->tid,
					       PK_DBUS_INTERFACE_TRANSACTION,
					       "Destroy",
					       NULL,
					       NULL);
	}

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
	if (transaction->priv->watch_id > 0)
		g_bus_unwatch_name (transaction->priv->watch_id);
	g_free (transaction->priv->last_package_id);
	g_free (transaction->priv->locale);
	g_free (transaction->priv->frontend_socket);
	g_free (transaction->priv->cached_package_id);
	g_free (transaction->priv->cached_key_id);
	g_strfreev (transaction->priv->cached_package_ids);
	g_free (transaction->priv->cached_transaction_id);
	g_free (transaction->priv->cached_directory);
	g_strfreev (transaction->priv->cached_values);
	g_free (transaction->priv->cached_repo_id);
	g_free (transaction->priv->cached_parameter);
	g_free (transaction->priv->cached_value);
	g_free (transaction->priv->tid);
	g_free (transaction->priv->sender);
	g_free (transaction->priv->cmdline);

	if (transaction->priv->connection != NULL)
		g_object_unref (transaction->priv->connection);
	if (transaction->priv->introspection != NULL)
		g_dbus_node_info_unref (transaction->priv->introspection);

	g_object_unref (transaction->priv->conf);
	g_object_unref (transaction->priv->dbus);
	g_object_unref (transaction->priv->cache);
	g_object_unref (transaction->priv->backend);
	g_object_unref (transaction->priv->transaction_list);
	g_object_unref (transaction->priv->transaction_db);
	g_object_unref (transaction->priv->notify);
	g_object_unref (transaction->priv->syslog);
	g_object_unref (transaction->priv->results);
#ifdef USE_SECURITY_POLKIT
//	g_object_unref (transaction->priv->authority);
	g_object_unref (transaction->priv->cancellable);
#endif
	if (transaction->priv->plugins != NULL)
		g_ptr_array_unref (transaction->priv->plugins);

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

