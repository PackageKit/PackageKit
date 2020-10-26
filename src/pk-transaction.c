/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2014 Richard Hughes <richard@hughsie.com>
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
#include <syslog.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-offline-private.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>
#include <packagekit-glib2/pk-results.h>
#include <polkit/polkit.h>

#include "pk-backend.h"
#include "pk-dbus.h"
#include "pk-shared.h"
#include "pk-transaction-db.h"
#include "pk-transaction.h"
#include "pk-transaction-private.h"

#ifndef HAVE_POLKIT_0_114
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitDetails, g_object_unref)
#endif

static void     pk_transaction_finalize		(GObject	    *object);
static void     pk_transaction_dispose		(GObject	    *object);

static gchar *pk_transaction_get_content_type_for_file (const gchar *filename, GError **error);
static gboolean pk_transaction_is_supported_content_type (PkTransaction *transaction, const gchar *content_type);

#define PK_TRANSACTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION, PkTransactionPrivate))
#define PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT	100 /* ms */

/* when the UID is invalid or not known */
#define PK_TRANSACTION_UID_INVALID		G_MAXUINT

/* maximum number of items that can be resolved in one go */
#define PK_TRANSACTION_MAX_ITEMS_TO_RESOLVE	10000

/* maximum number of packages that can be processed in one go */
#define PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS	5200

struct PkTransactionPrivate
{
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	PkTransactionState	 state;
	guint			 percentage;
	guint			 elapsed_time;
	guint			 speed;
	guint			 download_size_remaining;
	gboolean		 finished;
	gboolean		 allow_cancel;
	gboolean		 waiting_for_auth;
	gboolean		 emit_eula_required;
	gboolean		 emit_signature_required;
	gboolean		 emit_media_change_required;
	gboolean		 caller_active;
	gboolean		 exclusive;
	guint			 uid;
	guint			 watch_id;
	PkBackend		*backend;
	PkBackendJob		*job;
	GKeyFile		*conf;
	PkDbus			*dbus;
	PolkitAuthority		*authority;
	PolkitSubject		*subject;
	GCancellable		*cancellable;
	gboolean		 skip_auth_check;

	/* needed for gui coldplugging */
	gchar			*last_package_id;
	gchar			*tid;
	gchar			*sender;
	gchar			*cmdline;
	PkResults		*results;
	PkTransactionDb		*transaction_db;

	/* cached */
	gboolean		 cached_force;
	gboolean		 cached_allow_deps;
	gboolean		 cached_autoremove;
	gboolean		 cached_enabled;
	PkBitfield		 cached_transaction_flags;
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
	PkUpgradeKindEnum	 cached_upgrade_kind;
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
	SIGNAL_STATE_CHANGED,
	SIGNAL_ALLOW_CANCEL_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkTransaction, pk_transaction, G_TYPE_OBJECT)

GQuark
pk_transaction_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("pk-transaction-error-quark");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_DENIED,
					     PK_DBUS_INTERFACE_TRANSACTION ".Denied");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NOT_RUNNING,
					     PK_DBUS_INTERFACE_TRANSACTION ".NotRunning");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NO_ROLE,
					     PK_DBUS_INTERFACE_TRANSACTION ".NoRole");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_CANNOT_CANCEL,
					     PK_DBUS_INTERFACE_TRANSACTION ".CannotCancel");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     PK_DBUS_INTERFACE_TRANSACTION ".NotSupported");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NO_SUCH_TRANSACTION,
					     PK_DBUS_INTERFACE_TRANSACTION ".NoSuchTransaction");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
					     PK_DBUS_INTERFACE_TRANSACTION ".NoSuchFile");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NO_SUCH_DIRECTORY,
					     PK_DBUS_INTERFACE_TRANSACTION ".NoSuchDirectory");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_TRANSACTION_EXISTS_WITH_ROLE,
					     PK_DBUS_INTERFACE_TRANSACTION ".TransactionExistsWithRole");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
					     PK_DBUS_INTERFACE_TRANSACTION ".RefusedByPolicy");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".PackageIdInvalid");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_SEARCH_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".SearchInvalid");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_SEARCH_PATH_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".PathInvalid");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_FILTER_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".FilterInvalid");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_INPUT_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".InputInvalid");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_INVALID_STATE,
					     PK_DBUS_INTERFACE_TRANSACTION ".InvalidState");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_INITIALIZE_FAILED,
					     PK_DBUS_INTERFACE_TRANSACTION ".InitializeFailed");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_COMMIT_FAILED,
					     PK_DBUS_INTERFACE_TRANSACTION ".CommitFailed");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_INVALID_PROVIDE,
					     PK_DBUS_INTERFACE_TRANSACTION ".InvalidProvide");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_PACK_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".PackInvalid");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_MIME_TYPE_NOT_SUPPORTED,
					     PK_DBUS_INTERFACE_TRANSACTION ".MimeTypeNotSupported");
		g_dbus_error_register_error (quark,
					     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
					     PK_DBUS_INTERFACE_TRANSACTION ".NumberOfPackagesInvalid");
	}
	return quark;
}

static guint
pk_transaction_get_runtime (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), 0);
	g_return_val_if_fail (transaction->priv->tid != NULL, 0);
	return pk_backend_job_get_runtime (transaction->priv->job);
}

gboolean
pk_transaction_get_background (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	return pk_backend_job_get_background (transaction->priv->job);
}

static gboolean
pk_transaction_finish_invalidate_caches (PkTransaction *transaction)
{
	PkTransactionPrivate *priv = transaction->priv;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	/* could the update list have changed? */
	if (pk_bitfield_contain (transaction->priv->cached_transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_SIMULATE))
		goto out;
	if (pk_bitfield_contain (transaction->priv->cached_transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD))
		goto out;
	if (priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    priv->role == PK_ROLE_ENUM_REPO_SET_DATA ||
	    priv->role == PK_ROLE_ENUM_REPO_REMOVE ||
	    priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {

		/* this needs to be done after a small delay */
		pk_backend_updates_changed_delay (priv->backend,
						  PK_TRANSACTION_UPDATES_CHANGED_TIMEOUT);
	}
out:
	return TRUE;
}

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

static void
pk_transaction_progress_changed_emit (PkTransaction *transaction,
				     guint percentage,
				     guint elapsed,
				     guint remaining)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	/* save so we can do GetProgress on a queued or finished transaction */
	transaction->priv->percentage = percentage;
	transaction->priv->elapsed_time = elapsed;

	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "Percentage",
					      g_variant_new_uint32 (percentage));
	pk_transaction_emit_property_changed (transaction,
					      "ElapsedTime",
					      g_variant_new_uint32 (elapsed));
	pk_transaction_emit_property_changed (transaction,
					      "RemainingTime",
					      g_variant_new_uint32 (remaining));
}

static void
pk_transaction_allow_cancel_emit (PkTransaction *transaction, gboolean allow_cancel)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	/* already set */
	if (transaction->priv->allow_cancel == allow_cancel)
		return;

	transaction->priv->allow_cancel = allow_cancel;

	/* proxy this up so we can change the system inhibit */
	g_signal_emit (transaction, signals[SIGNAL_ALLOW_CANCEL_CHANGED], 0, allow_cancel);

	/* emit */
	pk_transaction_emit_property_changed (transaction,
					      "AllowCancel",
					      g_variant_new_boolean (allow_cancel));
}

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
					      g_variant_new_uint32 (status));
}

static void
pk_transaction_finished_emit (PkTransaction *transaction,
			      PkExitEnum exit_enum,
			      guint time_ms)
{
	g_debug ("emitting finished '%s', %i",
		 pk_exit_enum_to_string (exit_enum),
		 time_ms);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Finished",
				       g_variant_new ("(uu)",
						      exit_enum,
						      time_ms),
				       NULL);

	/* For the transaction list */
	g_signal_emit (transaction, signals[SIGNAL_FINISHED], 0);
}

static void
pk_transaction_error_code_emit (PkTransaction *transaction,
				PkErrorEnum error_enum,
				const gchar *details)
{
	g_debug ("emitting error-code %s, '%s'",
		 pk_error_enum_to_string (error_enum),
		 details);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "ErrorCode",
				       g_variant_new ("(us)",
						      error_enum,
						      details),
				       NULL);
}

static void
pk_transaction_allow_cancel_cb (PkBackendJob *job,
				gboolean allow_cancel,
				PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("emitting allow-cancel %i", allow_cancel);
	pk_transaction_allow_cancel_emit (transaction, allow_cancel);
}

static void
pk_transaction_locked_changed_cb (PkBackendJob *job,
				gboolean locked,
				PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("backend job lock status changed: %i", locked);

	/* if backend cache is locked at some time, this transaction is running in exclusive mode */
	if (locked)
		pk_transaction_make_exclusive (transaction);
}

static void
pk_transaction_details_cb (PkBackendJob *job,
			   PkDetails *item,
			   PkTransaction *transaction)
{
	GVariantBuilder builder;
	PkGroupEnum group;
	const gchar *tmp;
	guint64 size;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_details (transaction->priv->results, item);

	/* emit */
	g_debug ("emitting details");
	g_variant_builder_init (&builder, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add (&builder, "{sv}", "package-id",
			       g_variant_new_string (pk_details_get_package_id (item)));
	group = pk_details_get_group (item);
	if (group != PK_GROUP_ENUM_UNKNOWN)
		g_variant_builder_add (&builder, "{sv}", "group",
				       g_variant_new_uint32 (group));
	tmp = pk_details_get_summary (item);
	if (tmp != NULL)
		g_variant_builder_add (&builder, "{sv}", "summary",
				       g_variant_new_string (tmp));
	tmp = pk_details_get_description (item);
	if (tmp != NULL)
		g_variant_builder_add (&builder, "{sv}", "description",
				       g_variant_new_string (tmp));
	tmp = pk_details_get_url (item);
	if (tmp != NULL)
		g_variant_builder_add (&builder, "{sv}", "url",
				       g_variant_new_string (tmp));
	tmp = pk_details_get_license (item);
	if (tmp != NULL)
		g_variant_builder_add (&builder, "{sv}", "license",
				       g_variant_new_string (tmp));
	size = pk_details_get_size (item);
	if (size != 0)
		g_variant_builder_add (&builder, "{sv}", "size",
				       g_variant_new_uint64 (size));

	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Details",
				       g_variant_new ("(a{sv})", &builder),
				       NULL);
}

static void
pk_transaction_error_code_cb (PkBackendJob *job,
			      PkError *item,
			      PkTransaction *transaction)
{
	PkErrorEnum code;
	g_autofree gchar *details = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* get data */
	g_object_get (item,
		      "code", &code,
		      "details", &details,
		      NULL);

	if (code == PK_ERROR_ENUM_UNKNOWN) {
		g_warning ("%s emitted 'unknown error'",
			   pk_role_enum_to_string (transaction->priv->role));
	}

	/* add to results */
	pk_results_set_error_code (transaction->priv->results, item);

	if (!transaction->priv->exclusive && code == PK_ERROR_ENUM_LOCK_REQUIRED) {
		/* the backend failed to get lock for this action, this means this transaction has to be run in exclusive mode */
		g_debug ("changing transaction to exclusive mode (after failing with lock-required)");
		transaction->priv->exclusive = TRUE;
	} else {
		/* emit, as it is not the internally-handled LOCK_REQUIRED code */
		pk_transaction_error_code_emit (transaction, code, details);
	}
}

static void
pk_transaction_files_cb (PkBackendJob *job,
			 PkFiles *item,
			 PkTransaction *transaction)
{
	guint i;
	g_autofree gchar *package_id = NULL;
	g_auto(GStrv) files = NULL;

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
		for (i = 0; files[i] != NULL; i++) {
			if (!g_str_has_prefix (files[i], transaction->priv->cached_directory)) {
				g_warning ("%s does not have the correct prefix (%s)",
					   files[i],
					   transaction->priv->cached_directory);
			}
		}
	}

	/* add to results */
	pk_results_add_files (transaction->priv->results, item);

	/* emit */
	g_debug ("emitting files %s", package_id);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "Files",
				       g_variant_new ("(s^as)",
						      package_id != NULL ? package_id : "",
						      files),
				       NULL);
}

static void
pk_transaction_category_cb (PkBackendJob *job,
			    PkCategory *item,
			    PkTransaction *transaction)
{
	g_autofree gchar *parent_id = NULL;
	g_autofree gchar *cat_id = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *icon = NULL;

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
						      parent_id != NULL ? parent_id : "",
						      cat_id,
						      name,
						      summary,
						      icon != NULL ? icon : ""),
				       NULL);
}

static void
pk_transaction_item_progress_cb (PkBackendJob *job,
				 PkItemProgress *item_progress,
				 PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* emit */
	g_debug ("emitting item-progress %s, %s: %u",
		 pk_item_progress_get_package_id (item_progress),
		 pk_status_enum_to_string (pk_item_progress_get_status (item_progress)),
		 pk_item_progress_get_percentage (item_progress));
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "ItemProgress",
				       g_variant_new ("(suu)",
						      pk_item_progress_get_package_id (item_progress),
						      pk_item_progress_get_status (item_progress),
						      pk_item_progress_get_percentage (item_progress)),
				       NULL);
}

static void
pk_transaction_distro_upgrade_cb (PkBackendJob *job,
				  PkDistroUpgrade *item,
				  PkTransaction *transaction)
{
	PkUpdateStateEnum state;
	g_autofree gchar *name = NULL;
	g_autofree gchar *summary = NULL;

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
	g_debug ("emitting distro-upgrade %s, %s, %s",
		 pk_distro_upgrade_enum_to_string (state),
		 name, summary);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "DistroUpgrade",
				       g_variant_new ("(uss)",
						      state,
						      name,
						      summary != NULL ? summary : ""),
				       NULL);
}

static gchar *
pk_transaction_package_list_to_string (GPtrArray *array)
{
	GString *string;
	PkPackage *pkg;
	guint i;

	string = g_string_new ("");
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		g_string_append_printf (string, "%s\t%s\t%s\n",
					pk_info_enum_to_string (pk_package_get_info (pkg)),
					pk_package_get_id (pkg),
					pk_package_get_summary (pkg));
	}

	/* remove trailing newline */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);
	return g_string_free (string, FALSE);
}

const gchar *
pk_transaction_state_to_string (PkTransactionState state)
{
	if (state == PK_TRANSACTION_STATE_NEW)
		return "new";
	if (state == PK_TRANSACTION_STATE_WAITING_FOR_AUTH)
		return "waiting-for-auth";
	if (state == PK_TRANSACTION_STATE_READY)
		return "ready";
	if (state == PK_TRANSACTION_STATE_RUNNING)
		return "running";
	if (state == PK_TRANSACTION_STATE_FINISHED)
		return "finished";
	if (state == PK_TRANSACTION_STATE_ERROR)
		return "error";
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
 * 3. 'ready'	     <--- when the transaction is ready to be run
 * 4. 'running'	   <--- where PkBackend gets used
 * 5. 'finished'
 *
 **/
void
pk_transaction_set_state (PkTransaction *transaction, PkTransactionState state)
{
	PkTransactionPrivate *priv = transaction->priv;

	/* check we're not going backwards */
	if (priv->state != PK_TRANSACTION_STATE_UNKNOWN &&
	    priv->state > state) {
		g_warning ("cannot set %s, as already %s",
			   pk_transaction_state_to_string (state),
			   pk_transaction_state_to_string (priv->state));
		return;
	}

	g_debug ("transaction now %s", pk_transaction_state_to_string (state));
	priv->state = state;
	g_signal_emit (transaction, signals[SIGNAL_STATE_CHANGED], 0, state);

	/* only save into the database for useful stuff */
	if (state == PK_TRANSACTION_STATE_READY &&
	    (priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	     priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	     priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {

		/* add to database */
		pk_transaction_db_add (priv->transaction_db, priv->tid);

		/* save role in the database */
		pk_transaction_db_set_role (priv->transaction_db, priv->tid, priv->role);

		/* save uid */
		pk_transaction_db_set_uid (priv->transaction_db, priv->tid, priv->uid);

		/* save cmdline in db */
		if (priv->cmdline != NULL)
			pk_transaction_db_set_cmdline (priv->transaction_db, priv->tid, priv->cmdline);

		/* report to syslog */
		syslog (LOG_DAEMON | LOG_DEBUG,
			"new %s transaction %s scheduled from uid %i",
			pk_role_enum_to_string (priv->role),
			priv->tid, priv->uid);
	}

	/* update GUI */
	if (state == PK_TRANSACTION_STATE_WAITING_FOR_AUTH) {
		pk_transaction_status_changed_emit (transaction,
						    PK_STATUS_ENUM_WAITING_FOR_AUTH);
		pk_transaction_progress_changed_emit (transaction,
						      PK_BACKEND_PERCENTAGE_INVALID,
						      0, 0);

	} else if (state == PK_TRANSACTION_STATE_READY) {
		pk_transaction_status_changed_emit (transaction,
						    PK_STATUS_ENUM_WAIT);
		pk_transaction_progress_changed_emit (transaction,
						      PK_BACKEND_PERCENTAGE_INVALID,
						      0, 0);
	}
}

PkTransactionState
pk_transaction_get_state (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), PK_TRANSACTION_STATE_UNKNOWN);

	return transaction->priv->state;
}

guint
pk_transaction_get_uid (PkTransaction *transaction)
{
	return transaction->priv->uid;
}

static void
pk_transaction_setup_mime_types (PkTransaction *transaction)
{
	guint i;
	g_auto(GStrv) mime_types = NULL;

	/* get list of mime types supported by backends */
	mime_types = pk_backend_get_mime_types (transaction->priv->backend);
	for (i = 0; mime_types[i] != NULL; i++) {
		g_ptr_array_add (transaction->priv->supported_content_types,
				 g_strdup (mime_types[i]));
	}
}

void
pk_transaction_set_backend (PkTransaction *transaction,
			    PkBackend *backend)
{
	/* save a reference */
	if (transaction->priv->backend != NULL)
		g_object_unref (transaction->priv->backend);
	transaction->priv->backend = g_object_ref (backend);

	/* setup supported mime types */
	pk_transaction_setup_mime_types (transaction);
}

/**
* pk_transaction_get_backend_job:
*
* Returns: (transfer none): Current PkBackendJob for this transaction
**/
PkBackendJob *
pk_transaction_get_backend_job (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	return transaction->priv->job;
}

/**
 * pk_transaction_is_finished_with_lock_required:
 **/
gboolean
pk_transaction_is_finished_with_lock_required (PkTransaction *transaction)
{
	g_autoptr(PkError) error_code = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	error_code = pk_results_get_error_code (transaction->priv->results);
	if (error_code != NULL &&
	    pk_error_get_code (error_code) == PK_ERROR_ENUM_LOCK_REQUIRED) {
		return TRUE;
	}
	return FALSE;
}

static void
pk_transaction_offline_invalidate_check (PkTransaction *transaction)
{
	PkPackage *pkg;
	const gchar *package_id;
	gchar **package_ids;
	guint i;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(GPtrArray) invalidated = NULL;

	/* get the existing prepared updates */
	sack = pk_offline_get_prepared_sack (NULL);
	if (sack == NULL)
		return;

	/* are there any requested packages that match in prepared-updates */
	package_ids = transaction->priv->cached_package_ids;
	for (i = 0; package_ids[i] != NULL; i++) {
		pkg = pk_package_sack_find_by_id_name_arch (sack, package_ids[i]);
		if (pkg != NULL) {
			g_debug ("%s modified %s, invalidating prepared-updates",
				 package_ids[i], pk_package_get_id (pkg));
			if (!pk_offline_auth_invalidate (&error)) {
				g_warning ("failed to invalidate: %s",
					   error->message);
			}
			g_object_unref (pkg);
			return;
		}
	}

	/* are there any changed deps that match a package in prepared-updates */
	invalidated = pk_results_get_package_array (transaction->priv->results);
	for (i = 0; i < invalidated->len; i++) {
		package_id = pk_package_get_id (g_ptr_array_index (invalidated, i));
		pkg = pk_package_sack_find_by_id_name_arch (sack, package_id);
		if (pkg != NULL) {
			g_debug ("%s modified %s, invalidating prepared-updates",
				 package_id, pk_package_get_id (pkg));
			if (!pk_offline_auth_invalidate (&error)) {
				g_warning ("failed to invalidate: %s",
					   error->message);
			}
			g_object_unref (pkg);
			return;
		}
	}
}

static void
pk_transaction_offline_finished (PkTransaction *transaction)
{
	PkBitfield transaction_flags;
	gchar **package_ids;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* if we're doing UpdatePackages[only-download] then update the
	 * prepared-updates file */
	transaction_flags = transaction->priv->cached_transaction_flags;
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES &&
	    pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		package_ids = transaction->priv->cached_package_ids;
		if (!pk_offline_auth_set_prepared_ids (package_ids, &error)) {
			g_warning ("failed to write offline update: %s",
				   error->message);
		}
		return;
	}

	/* if we're doing UpgradeSystem[only-download] then update the
	 * prepared-upgrade file */
	transaction_flags = transaction->priv->cached_transaction_flags;
	if (transaction->priv->role == PK_ROLE_ENUM_UPGRADE_SYSTEM &&
	    pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		const gchar *version = transaction->priv->cached_value;
		g_autofree gchar *name = NULL;

		name = pk_get_distro_name (&error);
		if (name == NULL) {
			g_warning ("failed to get distro name: %s",
				   error->message);
			return;
		}
		if (!pk_offline_auth_set_prepared_upgrade (name, version, &error)) {
			g_warning ("failed to write offline system upgrade state: %s",
				   error->message);
			return;
		}
		return;
	}

	switch (transaction->priv->role) {
	case PK_ROLE_ENUM_GET_UPDATES:
		/* if we do get-updates and there's no updates then remove
		 * prepared-updates so the UI doesn't display update & reboot */
		array = pk_results_get_package_array (transaction->priv->results);
		if (array->len == 0) {
			if (!pk_offline_auth_invalidate (&error)) {
				g_warning ("failed to invalidate: %s",
					   error->message);
			}
		}
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
	case PK_ROLE_ENUM_REPO_SET_DATA:
	case PK_ROLE_ENUM_REPO_ENABLE:
		/* delete the prepared updates file as it's not valid */
		if (!pk_offline_auth_invalidate (&error))
			g_warning ("failed to invalidate: %s", error->message);
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		/* delete the file if the action affected any package
		 * already listed in the prepared updates file */
		pk_transaction_offline_invalidate_check (transaction);
		break;
	default:
		break;
	}
}

static void
pk_transaction_finished_cb (PkBackendJob *job, PkExitEnum exit_enum, PkTransaction *transaction)
{
	guint time_ms;
	guint i;
	PkPackage *item;
	PkInfoEnum info;
	PkBitfield transaction_flags;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* have we already been marked as finished? */
	if (transaction->priv->finished) {
		g_warning ("Already finished");
		return;
	}

	/* save this so we know if the cache is valid */
	pk_results_set_exit_code (transaction->priv->results, exit_enum);

	/* don't really finish the transaction if we only completed to wait for lock */
	if (pk_transaction_is_finished_with_lock_required (transaction)) {
		/* finish only for the transaction list */
		g_signal_emit (transaction, signals[SIGNAL_FINISHED], 0);
		return;
	}

	/* handle offline updates */
	transaction_flags = transaction->priv->cached_transaction_flags;
	if (exit_enum == PK_EXIT_ENUM_SUCCESS &&
	    !pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_transaction_offline_finished (transaction);
	}

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
	if (transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		g_autoptr(GPtrArray) array = NULL;
		g_autofree gchar *packages = NULL;

		array = pk_results_get_package_array (transaction->priv->results);

		/* save to database */
		packages = pk_transaction_package_list_to_string (array);
		if (!pk_strzero (packages))
			pk_transaction_db_set_data (transaction->priv->transaction_db, transaction->priv->tid, packages);

		/* report to syslog */
		for (i = 0; i < array->len; i++) {
			item = g_ptr_array_index (array, i);
			info = pk_package_get_info (item);
			if (info == PK_INFO_ENUM_REMOVING ||
			    info == PK_INFO_ENUM_INSTALLING ||
			    info == PK_INFO_ENUM_UPDATING) {
				syslog (LOG_DAEMON | LOG_DEBUG,
					"in %s for %s package %s was %s for uid %i",
					transaction->priv->tid,
					pk_role_enum_to_string (transaction->priv->role),
					pk_package_get_id (item),
					pk_info_enum_to_string (info),
					transaction->priv->uid);
			}
		}
	}

	/* the repo list will have changed */
	if (transaction->priv->role == PK_ROLE_ENUM_REPO_ENABLE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_REMOVE ||
	    transaction->priv->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		pk_backend_repo_list_changed (transaction->priv->backend);
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
	if (transaction->priv->uid != PK_TRANSACTION_UID_INVALID) {
		syslog (LOG_DAEMON | LOG_DEBUG,
			"%s transaction %s from uid %i finished with %s after %ims",
			pk_role_enum_to_string (transaction->priv->role),
			transaction->priv->tid,
			transaction->priv->uid,
			pk_exit_enum_to_string (exit_enum),
			time_ms);
	} else {
		syslog (LOG_DAEMON | LOG_DEBUG,
			"%s transaction %s finished with %s after %ims",
			pk_role_enum_to_string (transaction->priv->role),
			transaction->priv->tid,
			pk_exit_enum_to_string (exit_enum),
			time_ms);
	}

	/* this disconnects any pending signals */
	pk_backend_job_disconnect_vfuncs (transaction->priv->job);

	/* destroy the job */
	pk_backend_stop_job (transaction->priv->backend, transaction->priv->job);

	/* we emit last, as other backends will be running very soon after us, and we don't want to be notified */
	pk_transaction_finished_emit (transaction, exit_enum, time_ms);
}

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

	/* check the backend is doing the right thing */
	info = pk_package_get_info (item);
	if (transaction->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    transaction->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		if (info == PK_INFO_ENUM_INSTALLED) {
			role_text = pk_role_enum_to_string (transaction->priv->role);
			g_warning ("%s emitted 'installed' rather than 'installing'",
				   role_text);
			return;
		}
	}

	/* check we are respecting the filters */
	if (pk_bitfield_contain (transaction->priv->cached_filters,
				 PK_FILTER_ENUM_NOT_INSTALLED)) {
		if (info == PK_INFO_ENUM_INSTALLED) {
			role_text = pk_role_enum_to_string (transaction->priv->role);
			g_warning ("%s emitted package that was installed when "
				   "the ~installed filter is in place",
				   role_text);
			return;
		}
	}
	if (pk_bitfield_contain (transaction->priv->cached_filters,
				 PK_FILTER_ENUM_INSTALLED)) {
		if (info == PK_INFO_ENUM_AVAILABLE) {
			role_text = pk_role_enum_to_string (transaction->priv->role);
			g_warning ("%s emitted package that was ~installed when "
				   "the installed filter is in place",
				   role_text);
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
	if (transaction->priv->role != PK_ROLE_ENUM_GET_PACKAGES) {
		g_debug ("emit package %s, %s, %s",
			 pk_info_enum_to_string (info),
			 package_id,
			 summary);
	}
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

static void
pk_transaction_repo_detail_cb (PkBackend *backend,
			       PkRepoDetail *item,
			       PkTransaction *transaction)
{
	gboolean enabled;
	const gchar *repo_id;
	const gchar *description;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_repo_detail (transaction->priv->results, item);

	/* emit */
	repo_id = pk_repo_detail_get_id (item);
	description = pk_repo_detail_get_description (item);
	enabled = pk_repo_detail_get_enabled (item);
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
}

static void
pk_transaction_repo_signature_required_cb (PkBackend *backend,
					   PkRepoSignatureRequired *item,
					   PkTransaction *transaction)
{
	PkSigTypeEnum type;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *repository_name = NULL;
	g_autofree gchar *key_url = NULL;
	g_autofree gchar *key_userid = NULL;
	g_autofree gchar *key_id = NULL;
	g_autofree gchar *key_fingerprint = NULL;
	g_autofree gchar *key_timestamp = NULL;

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
	g_debug ("emitting repo_signature_required %s, %s, %s, %s, %s, %s, %s, %s",
		 package_id, repository_name, key_url, key_userid, key_id,
		 key_fingerprint, key_timestamp,
		 pk_sig_type_enum_to_string (type));
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "RepoSignatureRequired",
				       g_variant_new ("(sssssssu)",
						      package_id,
						      repository_name,
						      key_url != NULL ? key_url : "",
						      key_userid != NULL ? key_userid : "",
						      key_id != NULL ? key_id : "",
						      key_fingerprint != NULL ? key_fingerprint : "",
						      key_timestamp != NULL ? key_timestamp : "",
						      type),
				       NULL);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_signature_required = TRUE;
}

static void
pk_transaction_eula_required_cb (PkBackend *backend,
				 PkEulaRequired *item,
				 PkTransaction *transaction)
{
	const gchar *eula_id;
	const gchar *package_id;
	const gchar *vendor_name;
	const gchar *license_agreement;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_eula_required (transaction->priv->results, item);

	/* get data */
	eula_id = pk_eula_required_get_eula_id (item);
	package_id = pk_eula_required_get_package_id (item);
	vendor_name = pk_eula_required_get_vendor_name (item);
	license_agreement = pk_eula_required_get_license_agreement (item);

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
}

static void
pk_transaction_media_change_required_cb (PkBackend *backend,
					 PkMediaChangeRequired *item,
					 PkTransaction *transaction)
{
	PkMediaTypeEnum media_type;
	g_autofree gchar *media_id = NULL;
	g_autofree gchar *media_text = NULL;

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
	g_debug ("emitting media-change-required %s, %s, %s",
		 pk_media_type_enum_to_string (media_type),
		 media_id,
		 media_text);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "MediaChangeRequired",
				       g_variant_new ("(uss)",
						      media_type,
						      media_id,
						      media_text != NULL ? media_text : ""),
				       NULL);

	/* we should mark this transaction so that we finish with a special code */
	transaction->priv->emit_media_change_required = TRUE;
}

static void
pk_transaction_require_restart_cb (PkBackend *backend,
				   PkRequireRestart *item,
				   PkTransaction *transaction)
{
	PkRequireRestart *item_tmp;
	gboolean found = FALSE;
	guint i;
	PkRestartEnum restart;
	g_autofree gchar *package_id = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "restart", &restart,
		      NULL);

	/* filter out duplicates */
	array = pk_results_get_require_restart_array (transaction->priv->results);
	for (i = 0; i < array->len; i++) {
		g_autofree gchar *package_id_tmp = NULL;
		item_tmp = g_ptr_array_index (array, i);
		g_object_get (item_tmp,
			      "package-id", &package_id_tmp,
			      NULL);
		if (g_strcmp0 (package_id, package_id_tmp) == 0) {
			found = TRUE;
			break;
		}
	}

	/* ignore */
	if (found) {
		g_debug ("ignoring %s (%s) as already sent",
			 pk_restart_enum_to_string (restart),
			 package_id);
		return;
	}

	/* add to results */
	pk_results_add_require_restart (transaction->priv->results, item);

	/* emit */
	g_debug ("emitting require-restart %s, '%s'",
		 pk_restart_enum_to_string (restart),
		 package_id);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "RequireRestart",
				       g_variant_new ("(us)",
						      restart,
						      package_id),
				       NULL);
}

static void
pk_transaction_status_changed_cb (PkBackendJob *job,
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

static void
pk_transaction_update_detail_cb (PkBackend *backend,
				 PkUpdateDetail *item,
				 PkTransaction *transaction)
{
	const gchar *changelog;
	const gchar *issued;
	const gchar *package_id;
	const gchar *updated;
	const gchar *update_text;
	gchar **bugzilla_urls;
	gchar **cve_urls;
	gchar *empty[] = { NULL };
	gchar **obsoletes;
	gchar **updates;
	gchar **vendor_urls;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	/* add to results */
	pk_results_add_update_detail (transaction->priv->results, item);

	/* emit */
	package_id = pk_update_detail_get_package_id (item);
	updates = pk_update_detail_get_updates (item);
	obsoletes = pk_update_detail_get_obsoletes (item);
	vendor_urls = pk_update_detail_get_vendor_urls (item);
	bugzilla_urls = pk_update_detail_get_bugzilla_urls (item);
	cve_urls = pk_update_detail_get_cve_urls (item);
	update_text = pk_update_detail_get_update_text (item);
	changelog = pk_update_detail_get_changelog (item);
	issued = pk_update_detail_get_issued (item);
	updated = pk_update_detail_get_updated (item);
	g_debug ("emitting update-detail for %s", package_id);
	g_dbus_connection_emit_signal (transaction->priv->connection,
				       NULL,
				       transaction->priv->tid,
				       PK_DBUS_INTERFACE_TRANSACTION,
				       "UpdateDetail",
				       g_variant_new ("(s^as^as^as^as^asussuss)",
						      package_id,
						      updates != NULL ? updates : empty,
						      obsoletes != NULL ? obsoletes : empty,
						      vendor_urls != NULL ? vendor_urls : empty,
						      bugzilla_urls != NULL ? bugzilla_urls : empty,
						      cve_urls != NULL ? cve_urls : empty,
						      pk_update_detail_get_restart (item),
						      update_text != NULL ? update_text : "",
						      changelog != NULL ? changelog : "",
						      pk_update_detail_get_state (item),
						      issued != NULL ? issued : "",
						      updated != NULL ? updated : ""),
				       NULL);
}

static gboolean
pk_transaction_set_session_state (PkTransaction *transaction,
				  GError **error)
{
	gboolean ret;
	g_autofree gchar *session = NULL;
	g_autofree gchar *proxy_http = NULL;
	g_autofree gchar *proxy_https = NULL;
	g_autofree gchar *proxy_ftp = NULL;
	g_autofree gchar *proxy_socks = NULL;
	g_autofree gchar *no_proxy = NULL;
	g_autofree gchar *pac = NULL;
	g_autofree gchar *cmdline = NULL;
	PkTransactionPrivate *priv = transaction->priv;

	/* get session */
	if (!pk_dbus_connect (priv->dbus, error))
		return FALSE;
	session = pk_dbus_get_session (priv->dbus, priv->sender);
	if (session == NULL) {
		g_set_error_literal (error, 1, 0, "failed to get the session");
		return FALSE;
	}

	/* get from database */
	ret = pk_transaction_db_get_proxy (priv->transaction_db,
					   priv->uid,
					   session,
					   &proxy_http,
					   &proxy_https,
					   &proxy_ftp,
					   &proxy_socks,
					   &no_proxy,
					   &pac);
	if (!ret) {
		g_set_error_literal (error, 1, 0,
				     "failed to get the proxy from the database");
		return FALSE;
	}

	/* try to set the new proxy */
	pk_backend_job_set_proxy (priv->job,
				  proxy_http,
				  proxy_https,
				  proxy_ftp,
				  proxy_socks,
				  no_proxy,
				  pac);

	/* try to set the new uid and cmdline */
	cmdline = g_strdup_printf ("PackageKit: %s",
				   pk_role_enum_to_string (priv->role));
	pk_backend_job_set_uid (priv->job, priv->uid);
	pk_backend_job_set_cmdline (priv->job, cmdline);
	return TRUE;
}

static void
pk_transaction_speed_cb (PkBackendJob *job,
			 guint speed,
			 PkTransaction *transaction)
{
	/* emit */
	transaction->priv->speed = speed;
	pk_transaction_emit_property_changed (transaction,
					      "Speed",
					      g_variant_new_uint32 (speed));
}

static void
pk_transaction_download_size_remaining_cb (PkBackendJob *job,
					   guint64 *download_size_remaining,
					   PkTransaction *transaction)
{
	/* emit */
	transaction->priv->download_size_remaining = *download_size_remaining;
	pk_transaction_emit_property_changed (transaction,
					      "DownloadSizeRemaining",
					      g_variant_new_uint64 (*download_size_remaining));
}

static void
pk_transaction_percentage_cb (PkBackendJob *job,
			      guint percentage,
			      PkTransaction *transaction)
{
	/* emit */
	transaction->priv->percentage = percentage;
	pk_transaction_emit_property_changed (transaction,
					      "Percentage",
					      g_variant_new_uint32 (percentage));
}

gboolean
pk_transaction_run (PkTransaction *transaction)
{
	GError *error = NULL;
	PkExitEnum exit_status;
	PkTransactionPrivate *priv = PK_TRANSACTION_GET_PRIVATE (transaction);

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (priv->tid != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->backend != NULL, FALSE);

	/* we are no longer waiting, we are setting up */
	pk_transaction_status_changed_emit (transaction, PK_STATUS_ENUM_SETUP);

	/* set proxy */
	if (!pk_transaction_set_session_state (transaction, &error)) {
		g_debug ("failed to set the session state (non-fatal): %s",
			 error->message);
		g_clear_error (&error);
	}

	/* already cancelled? */
	if (pk_backend_job_get_exit_code (priv->job) == PK_EXIT_ENUM_CANCELLED) {
		exit_status = pk_backend_job_get_exit_code (priv->job);
		pk_transaction_finished_emit (transaction, exit_status, 0);
		return TRUE;
	}

	/* run the job */
	pk_backend_start_job (priv->backend, priv->job);

	/* is an error code set? */
	if (pk_backend_job_get_is_error_set (priv->job)) {
		exit_status = pk_backend_job_get_exit_code (priv->job);
		pk_transaction_finished_emit (transaction, exit_status, 0);
		/* do not fail the transaction */
	}

	/* check if we should skip this transaction */
	if (pk_backend_job_get_exit_code (priv->job) == PK_EXIT_ENUM_SKIP_TRANSACTION) {
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_SUCCESS, 0);
		/* do not fail the transaction */
	}

	/* set the role */
	pk_backend_job_set_role (priv->job, priv->role);
	g_debug ("setting role for %s to %s",
		 priv->tid,
		 pk_role_enum_to_string (priv->role));

	/* reset after the pre-transaction checks */
	pk_backend_job_set_percentage (priv->job, PK_BACKEND_PERCENTAGE_INVALID);

	/* connect signal to receive backend lock changes */
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_LOCKED_CHANGED,
				  (PkBackendJobVFunc) pk_transaction_locked_changed_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_ALLOW_CANCEL,
				  (PkBackendJobVFunc) pk_transaction_allow_cancel_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_DETAILS,
				  (PkBackendJobVFunc) pk_transaction_details_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_ERROR_CODE,
				  (PkBackendJobVFunc) pk_transaction_error_code_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_FILES,
				  (PkBackendJobVFunc) pk_transaction_files_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_DISTRO_UPGRADE,
				  (PkBackendJobVFunc) pk_transaction_distro_upgrade_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  (PkBackendJobVFunc) pk_transaction_finished_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_PACKAGE,
				  (PkBackendJobVFunc) pk_transaction_package_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_ITEM_PROGRESS,
				  (PkBackendJobVFunc) pk_transaction_item_progress_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_PERCENTAGE,
				  (PkBackendJobVFunc) pk_transaction_percentage_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_SPEED,
				  (PkBackendJobVFunc) pk_transaction_speed_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_DOWNLOAD_SIZE_REMAINING,
				  (PkBackendJobVFunc) pk_transaction_download_size_remaining_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_REPO_DETAIL,
				  (PkBackendJobVFunc) pk_transaction_repo_detail_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED,
				  (PkBackendJobVFunc) pk_transaction_repo_signature_required_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_EULA_REQUIRED,
				  (PkBackendJobVFunc) pk_transaction_eula_required_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED,
				  (PkBackendJobVFunc) pk_transaction_media_change_required_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_REQUIRE_RESTART,
				  (PkBackendJobVFunc) pk_transaction_require_restart_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_STATUS_CHANGED,
				  (PkBackendJobVFunc) pk_transaction_status_changed_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_UPDATE_DETAIL,
				  (PkBackendJobVFunc) pk_transaction_update_detail_cb,
				  transaction);
	pk_backend_job_set_vfunc (priv->job,
				  PK_BACKEND_SIGNAL_CATEGORY,
				  (PkBackendJobVFunc) pk_transaction_category_cb,
				  transaction);

	/* do the correct action with the cached parameters */
	switch (priv->role) {
	case PK_ROLE_ENUM_DEPENDS_ON:
		pk_backend_depends_on (priv->backend,
					priv->job,
					priv->cached_filters,
					priv->cached_package_ids,
					priv->cached_force);
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		pk_backend_get_update_detail (priv->backend,
					      priv->job,
					      priv->cached_package_ids);
		break;
	case PK_ROLE_ENUM_RESOLVE:
		pk_backend_resolve (priv->backend,
				    priv->job,
				    priv->cached_filters,
				    priv->cached_package_ids);
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		pk_backend_download_packages (priv->backend,
					      priv->job,
					      priv->cached_package_ids,
					      priv->cached_directory);
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
		pk_backend_get_details (priv->backend,
					priv->job,
					priv->cached_package_ids);
		break;
	case PK_ROLE_ENUM_GET_DETAILS_LOCAL:
		pk_backend_get_details_local (priv->backend,
					      priv->job,
					      priv->cached_full_paths);
		break;
	case PK_ROLE_ENUM_GET_FILES_LOCAL:
		pk_backend_get_files_local (priv->backend,
					    priv->job,
					    priv->cached_full_paths);
		break;
	case PK_ROLE_ENUM_GET_DISTRO_UPGRADES:
		pk_backend_get_distro_upgrades (priv->backend,
						priv->job);
		break;
	case PK_ROLE_ENUM_GET_FILES:
		pk_backend_get_files (priv->backend,
				      priv->job,
				      priv->cached_package_ids);
		break;
	case PK_ROLE_ENUM_REQUIRED_BY:
		pk_backend_required_by (priv->backend,
					 priv->job,
					 priv->cached_filters,
					 priv->cached_package_ids,
					 priv->cached_force);
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		pk_backend_what_provides (priv->backend,
					  priv->job,
					  priv->cached_filters,
					  priv->cached_values);
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		pk_backend_get_updates (priv->backend,
					priv->job,
					priv->cached_filters);
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
		pk_backend_get_packages (priv->backend,
					 priv->job,
					 priv->cached_filters);
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		pk_backend_search_details (priv->backend,
					   priv->job,
					   priv->cached_filters,
					   priv->cached_values);
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		pk_backend_search_files (priv->backend,
					 priv->job,
					 priv->cached_filters,
					 priv->cached_values);
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		pk_backend_search_groups (priv->backend,
					  priv->job,
					  priv->cached_filters,
					  priv->cached_values);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		pk_backend_search_names (priv->backend,
					 priv->job,
					 priv->cached_filters,
					 priv->cached_values);
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		pk_backend_install_packages (priv->backend,
					     priv->job,
					     priv->cached_transaction_flags,
					     priv->cached_package_ids);
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		pk_backend_install_files (priv->backend,
					  priv->job,
					  priv->cached_transaction_flags,
					  priv->cached_full_paths);
		break;
	case PK_ROLE_ENUM_INSTALL_SIGNATURE:
		pk_backend_install_signature (priv->backend,
					      priv->job,
					      PK_SIGTYPE_ENUM_GPG,
					      priv->cached_key_id,
					      priv->cached_package_id);
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		pk_backend_refresh_cache (priv->backend,
					  priv->job,
					  priv->cached_force);
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		pk_backend_remove_packages (priv->backend,
					    priv->job,
					    priv->cached_transaction_flags,
					    priv->cached_package_ids,
					    priv->cached_allow_deps,
					    priv->cached_autoremove);
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		pk_backend_update_packages (priv->backend,
					    priv->job,
					    priv->cached_transaction_flags,
					    priv->cached_package_ids);
		break;
	case PK_ROLE_ENUM_GET_CATEGORIES:
		pk_backend_get_categories (priv->backend,
					   priv->job);
		break;
	case PK_ROLE_ENUM_GET_REPO_LIST:
		pk_backend_get_repo_list (priv->backend,
					  priv->job,
					  priv->cached_filters);
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
		pk_backend_repo_enable (priv->backend,
					priv->job,
					priv->cached_repo_id,
					priv->cached_enabled);
		break;
	case PK_ROLE_ENUM_REPO_SET_DATA:
		pk_backend_repo_set_data (priv->backend,
					  priv->job,
					  priv->cached_repo_id,
					  priv->cached_parameter,
					  priv->cached_value);
		break;
	case PK_ROLE_ENUM_REPO_REMOVE:
		pk_backend_repo_remove (priv->backend,
					priv->job,
					priv->cached_transaction_flags,
					priv->cached_repo_id,
					priv->cached_autoremove);
		break;
	case PK_ROLE_ENUM_UPGRADE_SYSTEM:
		pk_backend_upgrade_system (priv->backend,
					   priv->job,
					   priv->cached_transaction_flags,
					   priv->cached_value,
					   priv->cached_upgrade_kind);
		break;
	case PK_ROLE_ENUM_REPAIR_SYSTEM:
		pk_backend_repair_system (priv->backend,
					  priv->job,
					  priv->cached_transaction_flags);
		break;
	case PK_ROLE_ENUM_IMPORT_PUBKEY:
		pk_backend_import_pubkey (priv->backend,
					  priv->job,
					  priv->cached_value);
		break;
	case PK_ROLE_ENUM_REMOVE_PUBKEY:
		pk_backend_remove_pubkey (priv->backend,
					  priv->job,
					  priv->cached_value);
		break;
	/* handled in the engine without a transaction */
	case PK_ROLE_ENUM_CANCEL:
	case PK_ROLE_ENUM_GET_OLD_TRANSACTIONS:
	case PK_ROLE_ENUM_ACCEPT_EULA:
		g_warning ("role %s should be handled by engine",
			   pk_role_enum_to_string (priv->role));
		break;
	default:
		g_error ("failed to run as role not assigned");
		return FALSE;
		break;
	}
	return TRUE;
}

const gchar *
pk_transaction_get_tid (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), NULL);
	g_return_val_if_fail (transaction->priv->tid != NULL, NULL);

	return transaction->priv->tid;
}

gboolean
pk_transaction_is_exclusive (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);

	return transaction->priv->exclusive;
}

void
pk_transaction_make_exclusive (PkTransaction *transaction)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	g_debug ("changing transaction to exclusive mode");

	transaction->priv->exclusive = TRUE;
}

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
}

gboolean
pk_transaction_set_sender (PkTransaction *transaction, const gchar *sender)
{
	PkTransactionPrivate *priv = transaction->priv;
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	g_return_val_if_fail (sender != NULL, FALSE);
	g_return_val_if_fail (transaction->priv->sender == NULL, FALSE);

	g_debug ("setting sender to %s", sender);
	priv->sender = g_strdup (sender);

	priv->watch_id =
		g_bus_watch_name (G_BUS_TYPE_SYSTEM,
				  sender,
				  G_BUS_NAME_WATCHER_FLAGS_NONE,
				  NULL,
				  pk_transaction_vanished_cb,
				  transaction,
				  NULL);

	/* we get the UID for all callers as we need to know when to cancel */
	priv->subject = polkit_system_bus_name_new (sender);
	if (!pk_dbus_connect (priv->dbus, &error)) {
		g_warning ("cannot get UID: %s", error->message);
		return FALSE;
	}
	priv->uid = pk_dbus_get_uid (priv->dbus, sender);

	/* only get when it's going to be saved into the database */
	if (priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		priv->cmdline = pk_dbus_get_cmdline (priv->dbus, sender);
	}

	return TRUE;
}

static gboolean
pk_transaction_finished_idle_cb (PkTransaction *transaction)
{
	pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_SUCCESS, 0);
	return FALSE;
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
	guint length;

	/* maximum size is 1024 */
	length = pk_strlen (text, 1024);
	if (length == 0) {
		g_set_error_literal (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_INPUT_INVALID,
				     "Invalid input passed to daemon: zero length string");
		return FALSE;
	}
	if (length > 1024) {
		g_set_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID,
			     "Invalid input passed to daemon: input too long: %u", length);
		return FALSE;
	}

	/* just check for valid UTF-8 */
	if (!g_utf8_validate (text, -1, NULL)) {
		g_set_error (error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_INPUT_INVALID,
			     "Invalid input passed to daemon: %s", text);
		return FALSE;
	}
	return TRUE;
}

static gboolean
pk_transaction_search_check_item (const gchar *values, GError **error)
{
	guint size;

	/* limit to a 1k chunk */
	if (values == NULL) {
		g_set_error_literal (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Search is null. This isn't supposed to happen...");
		return FALSE;
	}
	size = pk_strlen (values, 1024);
	if (size == 0) {
		g_set_error_literal (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Search string zero length");
		return FALSE;
	}
	if (strstr (values, "*") != NULL) {
		g_set_error_literal (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing '*'");
		return FALSE;
	}
	if (strstr (values, "?") != NULL) {
		g_set_error_literal (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing '?'");
		return FALSE;
	}
	if (size == 1024) {
		g_set_error_literal (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "The search string length is too large");
		return FALSE;
	}
	return pk_transaction_strvalidate (values, error);
}

static gboolean
pk_transaction_search_check (gchar **values, GError **error)
{
	guint i;

	/* check each parameter */
	for (i = 0; values[i] != NULL; i++) {
		if (!pk_transaction_search_check_item (values[i], error))
			return FALSE;
	}
	return TRUE;
}

struct AuthorizeActionsData {
	PkTransaction *transaction;
	PkRoleEnum role;
	/** Array of policy actions to authorize. They will are processed sequentially,
	 * which can result in several chained callbacks. */
	GPtrArray *actions;
};

static gboolean
pk_transaction_authorize_actions (PkTransaction *transaction,
				  PkRoleEnum role,
				  GPtrArray *actions);

/**
 * pk_transaction_authorize_actions_finished_cb:
 *
 * A callback processing the result of action's authorization done by
 * polkit daemon. If the action was authorized, it pops another
 * from *data->actions* and schedules it for authorization. This continues
 * until an action is denied or all of them are authorized.
 **/
static void
pk_transaction_authorize_actions_finished_cb (GObject *source_object,
					      GAsyncResult *res,
					      struct AuthorizeActionsData *data)
{
	const gchar *action_id = NULL;
	PkTransactionPrivate *priv = data->transaction->priv;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitAuthorizationResult) result = NULL;
	g_assert (data->actions && data->actions->len > 0);

	/* get the first action */
	action_id = g_ptr_array_index (data->actions, 0);

	/* finish the call */
	result = polkit_authority_check_authorization_finish (priv->authority, res, &error);

	/* failed because the request was cancelled */
	if (g_cancellable_is_cancelled (priv->cancellable)) {
		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		priv->waiting_for_auth = FALSE;
		pk_transaction_status_changed_emit (data->transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_error_code_emit (data->transaction, PK_ERROR_ENUM_NOT_AUTHORIZED,
						"The authentication was cancelled due to a timeout.");
		pk_transaction_finished_emit (data->transaction, PK_EXIT_ENUM_FAILED, 0);
		goto out;
	}

	/* failed, maybe polkit is messed up? */
	if (result == NULL) {
		g_autofree gchar *message = NULL;
		priv->waiting_for_auth = FALSE;
		g_warning ("failed to check for auth: %s", error->message);

		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		pk_transaction_status_changed_emit (data->transaction, PK_STATUS_ENUM_FINISHED);
		message = g_strdup_printf ("Failed to check for authentication: %s", error->message);
		pk_transaction_error_code_emit (data->transaction,
						PK_ERROR_ENUM_NOT_AUTHORIZED,
						message);
		pk_transaction_finished_emit (data->transaction, PK_EXIT_ENUM_FAILED, 0);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		priv->waiting_for_auth = FALSE;
		/* emit an ::StatusChanged, ::ErrorCode() and then ::Finished() */
		pk_transaction_status_changed_emit (data->transaction, PK_STATUS_ENUM_FINISHED);
		pk_transaction_error_code_emit (data->transaction, PK_ERROR_ENUM_NOT_AUTHORIZED,
						"Failed to obtain authentication.");
		pk_transaction_finished_emit (data->transaction, PK_EXIT_ENUM_FAILED, 0);
		syslog (LOG_AUTH | LOG_NOTICE, "uid %i failed to obtain auth", priv->uid);
		goto out;
	}

	if (data->actions->len <= 1) {
		/* authentication finished successfully */
		priv->waiting_for_auth = FALSE;
		pk_transaction_set_state (data->transaction, PK_TRANSACTION_STATE_READY);
		/* log success too */
		syslog (LOG_AUTH | LOG_INFO,
			"uid %i obtained auth for %s",
			priv->uid, action_id);
	} else {
		/* process the rest of actions */
		g_ptr_array_remove_index (data->actions, 0);
		pk_transaction_authorize_actions (data->transaction, data->role, data->actions);
	}

out:
	g_object_unref (data->transaction);
	g_ptr_array_unref (data->actions);
	g_free (data);
}

/**
 * pk_transaction_authorize_actions:
 *
 * Param actions is an array of policy actions that shall be authorized. They
 * will be processed one-by-one until one action is denied or all of them are
 * authorized. Each action results in one asynchronous function call to polkit
 * daemon.
 *
 * Return value: %TRUE if no authorization is required or the first action
 *		is scheduled for processing.
 */
static gboolean
pk_transaction_authorize_actions (PkTransaction *transaction,
				  PkRoleEnum role,
				  GPtrArray *actions)
{
	const gchar *action_id = NULL;
	g_autoptr(PolkitDetails) details = NULL;
	g_autofree gchar *package_ids = NULL;
	GString *string = NULL;
	PkTransactionPrivate *priv = transaction->priv;
	const gchar *text = NULL;
	struct AuthorizeActionsData *data = NULL;

	if (actions->len <= 0) {
		g_debug ("No authentication required");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
		return TRUE;
	}
	action_id = g_ptr_array_index (actions, 0);

	/* log */
	syslog (LOG_AUTH | LOG_INFO,
		"uid %i is trying to obtain %s auth (only_trusted:%i)",
		priv->uid,
		action_id,
		pk_bitfield_contain (priv->cached_transaction_flags,
					PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED));

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
	if ((role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) &&
	    !pk_bitfield_contain (priv->cached_transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {

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

	data = g_new (struct AuthorizeActionsData, 1);
	data->transaction = g_object_ref (transaction);
	data->role = role;
	data->actions = g_ptr_array_ref (actions);

	/* create if required */
	if (priv->authority == NULL) {
		g_autoptr(GError) error = NULL;
		priv->authority = polkit_authority_get_sync (NULL, &error);
		if (priv->authority == NULL) {
			g_warning ("failed to get polkit authority: %s", error->message);
			return FALSE;
		}
	}

	g_debug ("authorizing action %s", action_id);
	/* do authorization async */
	polkit_authority_check_authorization (priv->authority,
					      priv->subject,
					      action_id,
					      details,
					      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
					      priv->cancellable,
					      (GAsyncReadyCallback) pk_transaction_authorize_actions_finished_cb,
					      data);
	return TRUE;
}

/**
 * pk_transaction_role_to_actions:
 *
 * Produces a list of policy actions needing authorization for given role
 * and transaction flags.
 *
 * Return value: array of policy action ids
 **/
static GPtrArray *
pk_transaction_role_to_actions (PkRoleEnum role, guint64 transaction_flags)
{
	const gchar *policy = NULL;
	GPtrArray *result = NULL;
	gboolean check_install_untrusted = FALSE;

	result = g_ptr_array_new_with_free_func (g_free);
	if (result == NULL)
		return result;

	if ((role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	     role == PK_ROLE_ENUM_INSTALL_FILES ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES) &&
	    !pk_bitfield_contain (transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
		g_ptr_array_add (result, g_strdup ("org.freedesktop.packagekit.package-install-untrusted"));
		check_install_untrusted = TRUE;
	}

	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES &&
	    pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ALLOW_REINSTALL)) {
		g_ptr_array_add (result, g_strdup ("org.freedesktop.packagekit.package-reinstall"));
	}

	if ((role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES) &&
	    pk_bitfield_contain (transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ALLOW_DOWNGRADE)) {
		g_ptr_array_add (result, g_strdup ("org.freedesktop.packagekit.package-downgrade"));
	} else if (!check_install_untrusted) {
		switch (role) {
		case PK_ROLE_ENUM_UPDATE_PACKAGES:
			policy = "org.freedesktop.packagekit.system-update";
			break;
		case PK_ROLE_ENUM_INSTALL_SIGNATURE:
			policy = "org.freedesktop.packagekit.system-trust-signing-key";
			break;
		case PK_ROLE_ENUM_REPO_ENABLE:
		case PK_ROLE_ENUM_REPO_SET_DATA:
		case PK_ROLE_ENUM_REPO_REMOVE:
			policy = "org.freedesktop.packagekit.system-sources-configure";
			break;
		case PK_ROLE_ENUM_REFRESH_CACHE:
			policy = "org.freedesktop.packagekit.system-sources-refresh";
			break;
		case PK_ROLE_ENUM_REMOVE_PACKAGES:
			policy = "org.freedesktop.packagekit.package-remove";
			break;
		case PK_ROLE_ENUM_INSTALL_PACKAGES:
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
		if (policy != NULL)
			g_ptr_array_add (result, g_strdup (policy));
	}

	return result;
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
pk_transaction_obtain_authorization (PkTransaction *transaction,
				     PkRoleEnum role,
				     GError **error)
{
	g_autoptr(GPtrArray) actions = NULL;
	PkTransactionPrivate *priv = transaction->priv;
	g_autofree gchar *package_ids = NULL;
	g_autoptr(PolkitDetails) details = NULL;
	g_autoptr(GString) string = NULL;

	g_return_val_if_fail (priv->sender != NULL, FALSE);

	/* we don't need to authenticate at all to just download
	 * packages or if we're running unit tests */
	if (pk_bitfield_contain (transaction->priv->cached_transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD) ||
			pk_bitfield_contain (transaction->priv->cached_transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE) ||
			priv->skip_auth_check == TRUE) {
		g_debug ("No authentication required");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
		return TRUE;
	}

	/* we should always have subject */
	if (priv->subject == NULL) {
		g_set_error (error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
			     "subject %s not found", priv->sender);
		return FALSE;
	}

	actions = pk_transaction_role_to_actions (role, priv->cached_transaction_flags);
	if (actions == NULL)
		return FALSE;

	return pk_transaction_authorize_actions (transaction, role, actions);
}

/**
 * pk_transaction_skip_auth_checks:
 *
 * Skip authorization checks.
 * NOTE: This is *only* for testing, do never
 * use it somewhere else!
 **/
void
pk_transaction_skip_auth_checks (PkTransaction *transaction, gboolean skip_checks)
{
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	transaction->priv->skip_auth_check = skip_checks;
}

PkRoleEnum
pk_transaction_get_role (PkTransaction *transaction)
{
	g_return_val_if_fail (PK_IS_TRANSACTION (transaction), FALSE);
	return transaction->priv->role;
}

static void
pk_transaction_set_role (PkTransaction *transaction, PkRoleEnum role)
{
	transaction->priv->role = role;

	/* always set transaction exclusive for some actions (improves performance) */
	if (role == PK_ROLE_ENUM_INSTALL_FILES ||
	    role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_UPGRADE_SYSTEM ||
	    role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		pk_transaction_make_exclusive (transaction);
	}

	pk_transaction_emit_property_changed (transaction,
					      "Role",
					      g_variant_new_uint32 (role));
}

static void
pk_transaction_dbus_return (GDBusMethodInvocation *context, const GError *error)
{
	/* not set inside the test suite */
	if (context == NULL) {
		if (error != NULL)
			g_warning ("context null, and error: %s", error->message);
		return;
	}
	if (error != NULL)
		g_dbus_method_invocation_return_gerror (context, error);
	else
		g_dbus_method_invocation_return_value (context, NULL);
}

static void
pk_transaction_accept_eula (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	guint idle_id;
	const gchar *eula_id = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
		       &eula_id);

	/* check for sanity */
	ret = pk_transaction_strvalidate (eula_id, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_ACCEPT_EULA);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_ACCEPT_EULA,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	g_debug ("AcceptEula method called: %s", eula_id);
	pk_backend_accept_eula (transaction->priv->backend, eula_id);

	/* we are done */
	idle_id = g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);
	g_source_set_name_by_id (idle_id, "[PkTransaction] finished from accept");
out:
	pk_transaction_dbus_return (context, error);
}

void
pk_transaction_cancel_bg (PkTransaction *transaction)
{
	g_debug ("CancelBg method called on %s", transaction->priv->tid);

	/* transaction is already finished */
	if (transaction->priv->state == PK_TRANSACTION_STATE_FINISHED)
		return;

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_CANCEL)) {
		g_warning ("Cancel not supported by backend");
		return;
	}

	/* if it's never been run, just remove this transaction from the list */
	if (transaction->priv->state <= PK_TRANSACTION_STATE_READY) {
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_CANCELLED, 0);
		return;
	}

	/* set the state, as cancelling might take a few seconds */
	pk_backend_job_set_status (transaction->priv->job, PK_STATUS_ENUM_CANCEL);

	/* we don't want to cancel twice */
	pk_backend_job_set_allow_cancel (transaction->priv->job, FALSE);

	/* we need ::finished to not return success or failed */
	pk_backend_job_set_exit_code (transaction->priv->job, PK_EXIT_ENUM_CANCELLED_PRIORITY);

	/* actually run the method */
	pk_backend_cancel (transaction->priv->backend, transaction->priv->job);
}

static void
pk_transaction_cancel (PkTransaction *transaction,
		       GVariant *params,
		       GDBusMethodInvocation *context)
{
	gboolean ret;
	const gchar *sender;
	guint uid;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("Cancel method called on %s", transaction->priv->tid);

	/* transaction is already finished */
	if (transaction->priv->state == PK_TRANSACTION_STATE_FINISHED) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_RUNNING,
			     "Transaction is already finished");
		goto out;
	}

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_CANCEL)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
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
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NO_ROLE, "No role");
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
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_CANNOT_CANCEL,
			     "No context from caller to get UID from");
		goto out;
	}

	/* get the UID of the caller */
	if (!pk_dbus_connect (transaction->priv->dbus, &error))
		goto out;
	uid = pk_dbus_get_uid (transaction->priv->dbus, sender);
	if (uid == PK_TRANSACTION_UID_INVALID) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_INVALID_STATE,
			     "unable to get uid of caller");
		goto out;
	}

	/* check the caller uid with the originator uid */
	if (transaction->priv->uid != uid) {
		g_debug ("uid does not match (%i vs. %i)", transaction->priv->uid, uid);
		ret = pk_transaction_obtain_authorization (transaction,
							   PK_ROLE_ENUM_CANCEL,
							   &error);
		if (!ret)
			goto out;
	}

skip_uid:
	/* if it's never been run, just remove this transaction from the list */
	if (transaction->priv->state <= PK_TRANSACTION_STATE_READY) {
		g_autofree gchar *msg = NULL;
		msg = g_strdup_printf ("%s was cancelled and was never run",
				       transaction->priv->tid);
		pk_transaction_error_code_emit (transaction,
						PK_ERROR_ENUM_TRANSACTION_CANCELLED,
						msg);
		pk_transaction_finished_emit (transaction, PK_EXIT_ENUM_CANCELLED, 0);
		goto out;
	}

	/* set the state, as cancelling might take a few seconds */
	pk_backend_job_set_status (transaction->priv->job, PK_STATUS_ENUM_CANCEL);

	/* we don't want to cancel twice */
	pk_backend_job_set_allow_cancel (transaction->priv->job, FALSE);

	/* we need ::finished to not return success or failed */
	pk_backend_job_set_exit_code (transaction->priv->job, PK_EXIT_ENUM_CANCELLED);

	/* actually run the method */
	pk_backend_cancel (transaction->priv->backend, transaction->priv->job);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_download_packages (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	gint retval;
	guint length;
	gboolean store_in_cache;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;

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
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "DownloadPackages not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid", package_ids_temp);
		goto out;
	}

	/* create cache directory */
	if (!store_in_cache) {
		directory = g_build_filename (LOCALSTATEDIR, "cache", "PackageKit",
					     "downloads", transaction->priv->tid, NULL);
		/* rwxrwxr-x */
		retval = g_mkdir_with_parents (directory, 0775);
		if (retval != 0) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_DENIED,
				     "cannot create %s", directory);
			goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_directory = g_strdup (directory);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_DOWNLOAD_PACKAGES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_categories (PkTransaction *transaction,
			       GVariant *params,
			       GDBusMethodInvocation *context)
{
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("GetCategories method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_CATEGORIES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetCategories not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_CATEGORIES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_depends_on (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	gchar *package_ids_temp;
	guint length;
	PkBitfield filter;
	gboolean recursive;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&sb)",
		       &filter,
		       &package_ids,
		       &recursive);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("DependsOn method called: %s (recursive %i)", package_ids_temp, recursive);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_DEPENDS_ON)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "DependsOn not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_force = recursive;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_DEPENDS_ON);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_details (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	guint length;
	g_autofree gchar **package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *package_ids_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetDetails method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_DETAILS)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetDetails not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid",
			     package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DETAILS);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_details_local (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error_local = NULL;
	GError *error = NULL;
	guint i;
	guint length;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *files_temp = NULL;
	g_autofree gchar **full_paths = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)", &full_paths);

	files_temp = pk_package_ids_to_string (full_paths);
	g_debug ("GetDetailsLocal method called: %s", files_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_DETAILS_LOCAL)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetDetailsLocal not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (full_paths);
	if (length == 0) {
		g_set_error_literal (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "No filenames listed");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many files to process (%i/%i)", length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check all files exists and are valid */
	length = g_strv_length (full_paths);
	for (i = 0; i < length; i++) {

		/* exists */
		ret = g_file_test (full_paths[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}

		/* get content type */
		content_type = pk_transaction_get_content_type_for_file (full_paths[i],
								         &error_local);
		if (content_type == NULL) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}

		/* supported content type? */
		ret = pk_transaction_is_supported_content_type (transaction, content_type);
		if (!ret) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DETAILS_LOCAL);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_files_local (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error_local = NULL;
	guint i;
	guint length;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar *files_temp = NULL;
	g_autofree gchar **full_paths = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)", &full_paths);

	files_temp = pk_package_ids_to_string (full_paths);
	g_debug ("GetFilesLocal method called: %s", files_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_FILES_LOCAL)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetFilesLocal not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (full_paths);
	if (length == 0) {
		g_set_error_literal (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
				     "No filenames listed");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many files to process (%i/%i)", length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check all files exists and are valid */
	length = g_strv_length (full_paths);
	for (i = 0; i < length; i++) {

		/* exists */
		ret = g_file_test (full_paths[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}

		/* get content type */
		content_type = pk_transaction_get_content_type_for_file (full_paths[i],
								         &error_local);
		if (content_type == NULL) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}

		/* supported content type? */
		ret = pk_transaction_is_supported_content_type (transaction, content_type);
		if (!ret) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_FILES_LOCAL);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_distro_upgrades (PkTransaction *transaction,
				    GVariant *params,
				    GDBusMethodInvocation *context)
{
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_debug ("GetDistroUpgrades method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetDistroUpgrades not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_files (PkTransaction *transaction,
			  GVariant *params,
			  GDBusMethodInvocation *context)
{
	gboolean ret;
	guint length;
	g_autoptr(GError) error = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetFiles method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_FILES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetFiles not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_FILES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_packages (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	PkBitfield filter;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t)",
		       &filter);

	g_debug ("GetPackages method called: %" G_GUINT64_FORMAT, filter);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_PACKAGES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetPackages not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_PACKAGES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_old_transactions (PkTransaction *transaction,
				     GVariant *params,
				     GDBusMethodInvocation *context)
{
	const gchar *cmdline;
	const gchar *data;
	const gchar *modified;
	const gchar *tid;
	gboolean succeeded;
	GList *l;
	GList *transactions = NULL;
	guint duration;
	guint idle_id;
	guint number;
	guint uid;
	PkRoleEnum role;
	PkTransactionPast *item;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(u)",
		       &number);

	g_debug ("GetOldTransactions method called");

	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_OLD_TRANSACTIONS);
	transactions = pk_transaction_db_get_list (transaction->priv->transaction_db, number);
	for (l = transactions; l != NULL; l = l->next) {
		item = PK_TRANSACTION_PAST (l->data);

		/* add to results */
		pk_results_add_transaction (transaction->priv->results, item);

		/* get data */
		role = pk_transaction_past_get_role (item);
		tid = pk_transaction_past_get_id (item);
		modified = pk_transaction_past_get_timespec (item);
		succeeded = pk_transaction_past_get_succeeded (item);
		duration = pk_transaction_past_get_duration (item);
		data = pk_transaction_past_get_data (item);
		uid = pk_transaction_past_get_uid (item);
		cmdline = pk_transaction_past_get_cmdline (item);

		/* emit */
		g_debug ("adding transaction %s, %s, %i, %s, %i, %s, %i, %s",
			 tid, modified, succeeded,
			 pk_role_enum_to_string (role),
			 duration, data, uid, cmdline);
		g_dbus_connection_emit_signal (transaction->priv->connection,
					       NULL,
					       transaction->priv->tid,
					       PK_DBUS_INTERFACE_TRANSACTION,
					       "Transaction",
					       g_variant_new ("(osbuusus)",
							      tid,
							      modified,
							      succeeded,
							      role,
							      duration,
							      data != NULL ? data : "",
							      uid,
							      cmdline != NULL ? cmdline : ""),
					       NULL);
	}
	g_list_free_full (transactions, (GDestroyNotify) g_object_unref);

	idle_id = g_idle_add ((GSourceFunc) pk_transaction_finished_idle_cb, transaction);
	g_source_set_name_by_id (idle_id, "[PkTransaction] finished from get-old-transactions");

	pk_transaction_dbus_return (context, NULL);
}

static void
pk_transaction_get_repo_list (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	PkBitfield filter;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t)",
		       &filter);

	g_debug ("GetRepoList method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_REPO_LIST)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetRepoList not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_REPO_LIST);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_required_by (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	guint length;
	PkBitfield filter;
	gboolean recursive;
	g_autoptr(GError) error = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&sb)",
		       &filter,
		       &package_ids,
		       &recursive);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("RequiredBy method called: %s (recursive %i)", package_ids_temp, recursive);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REQUIRED_BY)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RequiredBy not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_force = recursive;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REQUIRED_BY);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_get_update_detail (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error = NULL;
	guint length;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)",
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	g_debug ("GetUpdateDetail method called: %s", package_ids_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_UPDATE_DETAIL)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetUpdateDetail not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid",
			     package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

void
pk_transaction_get_updates (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	PkBitfield filter;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t)",
		       &filter);

	g_debug ("GetUpdates method called");

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_GET_UPDATES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "GetUpdates not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_GET_UPDATES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static gchar *
pk_transaction_get_content_type_for_file (const gchar *filename, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* get file info synchronously */
	file = g_file_new_for_path (filename);
	info = g_file_query_info (file, "standard::content-type",
				  G_FILE_QUERY_INFO_NONE, NULL, &error_local);
	if (info == NULL) {
		g_set_error (error, 1, 0,
			     "failed to get file attributes for %s: %s",
			     filename, error_local->message);
		return NULL;
	}

	/* get content type as string */
	return g_file_info_get_attribute_as_string (info, "standard::content-type");
}

static gboolean
pk_transaction_is_supported_content_type (PkTransaction *transaction,
					  const gchar *content_type)
{
	const gchar *tmp;
	GPtrArray *array = transaction->priv->supported_content_types;
	guint i;

	/* can we support this one? */
	for (i = 0; i < array->len; i++) {
		tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (tmp, content_type) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
pk_transaction_install_files (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	GError *error_local = NULL;
	guint length;
	guint i;
	PkBitfield transaction_flags;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *content_type = NULL;
	g_autofree gchar **full_paths = NULL;
	g_autofree gchar *full_paths_temp = NULL;
	g_autofree gchar *transaction_flags_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &full_paths);

	full_paths_temp = pk_package_ids_to_string (full_paths);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("InstallFiles method called: %s (transaction_flags: %s)",
		 full_paths_temp, transaction_flags_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_INSTALL_FILES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "InstallFiles not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check all files exists and are valid */
	length = g_strv_length (full_paths);
	for (i = 0; i < length; i++) {
		/* exists */
		ret = g_file_test (full_paths[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
				goto out;
		}

		/* get content type */
		content_type = pk_transaction_get_content_type_for_file (full_paths[i], &error_local);
		if (content_type == NULL) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
				goto out;
		}

		/* supported content type? */
		ret = pk_transaction_is_supported_content_type (transaction, content_type);
		if (!ret) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NO_SUCH_FILE,
				     "File %s is not found or unsupported", full_paths[i]);
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
				goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_flags = transaction_flags;
	transaction->priv->cached_full_paths = g_strdupv (full_paths);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_FILES);

	/* this changed */
	pk_transaction_emit_property_changed (transaction,
					      "TransactionFlags",
					      g_variant_new_uint64 (transaction_flags));

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_INSTALL_FILES,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

void
pk_transaction_install_packages (PkTransaction *transaction,
				 GVariant *params,
				 GDBusMethodInvocation *context)
{
	gboolean ret;
	guint length;
	PkBitfield transaction_flags;
	g_autoptr(GError) error = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;
	g_autofree gchar *transaction_flags_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("InstallPackages method called: %s (transaction_flags: %s)",
		 package_ids_temp, transaction_flags_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_INSTALL_PACKAGES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "InstallPackages not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_flags = transaction_flags;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_PACKAGES);

	/* this changed */
	pk_transaction_emit_property_changed (transaction,
					      "TransactionFlags",
					      g_variant_new_uint64 (transaction_flags));

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_INSTALL_PACKAGES,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_install_signature (PkTransaction *transaction,
				  GVariant *params,
				  GDBusMethodInvocation *context)
{
	gboolean ret;
	const gchar *key_id;
	const gchar *package_id;
	PkSigTypeEnum sig_type;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(u&s&s)",
		       &sig_type,
		       &key_id,
		       &package_id);

	g_debug ("InstallSignature method called: %s, %s, %s",
		 pk_sig_type_enum_to_string (sig_type),
		 key_id,
		 package_id);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_INSTALL_SIGNATURE)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "InstallSignature not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (key_id, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_id (';;;repo-id' is used for the repo key) */
	ret = pk_package_id_check (package_id);
	if (!ret && !g_str_has_prefix (package_id, ";;;")) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id '%s' is not valid", package_id);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_package_id = g_strdup (package_id);
	transaction->priv->cached_key_id = g_strdup (key_id);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_INSTALL_SIGNATURE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_INSTALL_SIGNATURE,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_refresh_cache (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	gboolean force;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(b)",
		       &force);

	g_debug ("RefreshCache method called: %i", force);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REFRESH_CACHE)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RefreshCache not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_force = force;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REFRESH_CACHE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_REFRESH_CACHE,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_remove_packages (PkTransaction *transaction,
				GVariant *params,
				GDBusMethodInvocation *context)
{
	gboolean ret;
	guint length;
	gboolean allow_deps;
	gboolean autoremove;
	PkBitfield transaction_flags;
	g_autoptr(GError) error = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;
	g_autofree gchar *transaction_flags_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&sbb)",
		       &transaction_flags,
		       &package_ids,
		       &allow_deps,
		       &autoremove);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("RemovePackages method called: %s, %i, %i (transaction_flags: %s)",
		 package_ids_temp, allow_deps, autoremove, transaction_flags_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REMOVE_PACKAGES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RemovePackages not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid", package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_flags = transaction_flags;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	transaction->priv->cached_allow_deps = allow_deps;
	transaction->priv->cached_autoremove = autoremove;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REMOVE_PACKAGES);

	/* this changed */
	pk_transaction_emit_property_changed (transaction,
					      "TransactionFlags",
					      g_variant_new_uint64 (transaction_flags));

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_REMOVE_PACKAGES,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_repo_enable (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	gboolean ret;
	const gchar *repo_id;
	gboolean enabled;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&sb)",
		       &repo_id,
		       &enabled);

	g_debug ("RepoEnable method called: %s, %i", repo_id, enabled);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REPO_ENABLE)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RepoEnable not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (repo_id, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_enabled = enabled;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_ENABLE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_REPO_ENABLE,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_repo_set_data (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	const gchar *repo_id;
	const gchar *parameter;
	const gchar *value;
	g_autoptr(GError) error = NULL;

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
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RepoSetData not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (repo_id, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_parameter = g_strdup (parameter);
	transaction->priv->cached_value = g_strdup (value);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_SET_DATA);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_REPO_SET_DATA,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_repo_remove (PkTransaction *transaction,
			    GVariant *params,
			    GDBusMethodInvocation *context)
{
	PkBitfield transaction_flags;
	const gchar *repo_id;
	gboolean autoremove;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *tmp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t&sb)",
		       &transaction_flags,
		       &repo_id,
		       &autoremove);

	tmp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("RepoRemove method called: %s, %s, %i",
		 tmp, repo_id, autoremove);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REPO_REMOVE)) {
		g_set_error_literal (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "RepoSetData not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for sanity */
	ret = pk_transaction_strvalidate (repo_id, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_repo_id = g_strdup (repo_id);
	transaction->priv->cached_transaction_flags = transaction_flags;
	transaction->priv->cached_autoremove = autoremove;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPO_REMOVE);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_REPO_REMOVE,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_resolve (PkTransaction *transaction,
			GVariant *params,
			GDBusMethodInvocation *context)
{
	gboolean ret;
	guint i;
	guint length;
	PkBitfield filter;
	g_autoptr(GError) error = NULL;
	g_autofree gchar **packages = NULL;
	g_autofree gchar *packages_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &filter,
		       &packages);

	packages_temp = pk_package_ids_to_string (packages);
	g_debug ("Resolve method called: %" G_GUINT64_FORMAT ", %s",
		 filter, packages_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_RESOLVE)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "Resolve not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (packages);
	if (length == 0) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_INPUT_INVALID,
			     "Too few items to process");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
	if (length > PK_TRANSACTION_MAX_ITEMS_TO_RESOLVE) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_INPUT_INVALID,
			     "Too many items to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_ITEMS_TO_RESOLVE);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check each package for sanity */
	for (i = 0; i < length; i++) {
		ret = pk_transaction_strvalidate (packages[i], &error);
		if (!ret) {
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_package_ids = g_strdupv (packages);
	transaction->priv->cached_filters = filter;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_RESOLVE);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

void
pk_transaction_search_details (PkTransaction *transaction,
			       GVariant *params,
			       GDBusMethodInvocation *context)
{
	gboolean ret;
	PkBitfield filter;
	g_autofree gchar **values = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchDetails method called: %" G_GUINT64_FORMAT ", %s",
		 filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_DETAILS)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "SearchDetails not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_DETAILS);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_search_files (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	guint i;
	PkBitfield filter;
	g_autofree gchar **values = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchFiles method called: %" G_GUINT64_FORMAT ", %s",
		 filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_FILE)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "SearchFiles not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* when not an absolute path, disallow slashes in search */
	for (i = 0; values[i] != NULL; i++) {
		if (values[i][0] != '/' && strstr (values[i], "/") != NULL) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_PATH_INVALID,
				     "Invalid search path");
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_FILE);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_search_groups (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	guint i;
	PkBitfield filter;
	g_autofree gchar **values = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchGroups method called: %" G_GUINT64_FORMAT ", %s",
		 filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_GROUP)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "SearchGroups not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* do not allow spaces */
	for (i = 0; values[i] != NULL; i++) {
		if (strstr (values[i], " ") != NULL) {
			g_set_error (&error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_SEARCH_INVALID,
				     "Invalid search containing spaces");
			pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
		}
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_GROUP);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

void
pk_transaction_search_names (PkTransaction *transaction,
			     GVariant *params,
			     GDBusMethodInvocation *context)
{
	gboolean ret;
	PkBitfield filter;
	g_autofree gchar **values = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &filter,
		       &values);

	g_debug ("SearchNames method called: %"  G_GUINT64_FORMAT ", %s",
		 filter, values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_SEARCH_NAME)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "SearchNames not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_SEARCH_NAME);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static gboolean
pk_transaction_set_hint (PkTransaction *transaction,
			 const gchar *key,
			 const gchar *value,
			 GError **error)
{
	PkTransactionPrivate *priv = transaction->priv;

	/* locale=en_GB.utf8 */
	if (g_strcmp0 (key, "locale") == 0) {
		pk_backend_job_set_locale (priv->job, value);
		return TRUE;
	}

	/* frontend_socket=/tmp/socket.3456 */
	if (g_strcmp0 (key, "frontend-socket") == 0) {

		/* nothing provided */
		if (value == NULL || value[0] == '\0') {
			g_set_error_literal (error,
					     PK_TRANSACTION_ERROR,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Could not set frontend-socket to nothing");
			return FALSE;
		}

		/* nothing provided */
		if (value[0] != '/') {
			g_set_error_literal (error,
					     PK_TRANSACTION_ERROR,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "frontend-socket has to be an absolute path");
			return FALSE;
		}

		/* socket does not exist */
		if (!g_file_test (value, G_FILE_TEST_EXISTS)) {
			g_set_error_literal (error,
					     PK_TRANSACTION_ERROR,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "frontend-socket does not exist");
			return FALSE;
		}

		/* success */
		pk_backend_job_set_frontend_socket (priv->job, value);
		return TRUE;
	}

	/* background=true */
	if (g_strcmp0 (key, "background") == 0) {
		if (g_strcmp0 (value, "true") == 0) {
			pk_backend_job_set_background (priv->job, TRUE);
		} else if (g_strcmp0 (value, "false") == 0) {
			pk_backend_job_set_background (priv->job, FALSE);
		} else {
			g_set_error (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				      "background hint expects true or false, not %s", value);
			return FALSE;
		}
		return TRUE;
	}

	/* interactive=true */
	if (g_strcmp0 (key, "interactive") == 0) {
		if (g_strcmp0 (value, "true") == 0) {
			pk_backend_job_set_interactive (priv->job, TRUE);
		} else if (g_strcmp0 (value, "false") == 0) {
			pk_backend_job_set_interactive (priv->job, FALSE);
		} else {
			g_set_error (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				      "interactive hint expects true or false, not %s", value);
			return FALSE;
		}
		return TRUE;
	}

	/* cache-age=<time-in-seconds> */
	if (g_strcmp0 (key, "cache-age") == 0) {
		guint cache_age;
		if (!pk_strtouint (value, &cache_age)) {
			g_set_error (error,
				     PK_TRANSACTION_ERROR,
				     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
				     "cannot parse cache age value %s", value);
			return FALSE;
		}
		if (cache_age == 0) {
			g_set_error_literal (error,
					     PK_TRANSACTION_ERROR,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "cannot set a cache age of zero");
			return FALSE;
		}
		pk_backend_job_set_cache_age (priv->job, cache_age);
		return TRUE;
	}

	/* to preserve forwards and backwards compatibility, we ignore
	 * extra options here */
	g_warning ("unknown option: %s with value %s", key, value);
	return TRUE;
}

static void
pk_transaction_set_hints (PkTransaction *transaction,
			  GVariant *params,
			  GDBusMethodInvocation *context)
{
	gboolean ret;
	guint i;
	g_autofree gchar **hints = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dbg = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(^a&s)", &hints);
	dbg = g_strjoinv (", ", (gchar**) hints);
	g_debug ("SetHints method called: %s", dbg);

	/* parse */
	for (i = 0; hints[i] != NULL; i++) {
		g_auto(GStrv) sections = NULL;
		sections = g_strsplit (hints[i], "=", 2);
		if (g_strv_length (sections) == 2) {
			ret = pk_transaction_set_hint (transaction,
						       sections[0],
						       sections[1],
						       &error);
			if (!ret)
				goto out;
		} else {
			g_set_error (&error, PK_TRANSACTION_ERROR,
					     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
					     "Could not parse hint '%s'", hints[i]);
			goto out;
		}
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_update_packages (PkTransaction *transaction,
				GVariant *params,
				GDBusMethodInvocation *context)
{
	gboolean ret;
	guint length;
	PkBitfield transaction_flags;
	g_autoptr(GError) error = NULL;
	g_autofree gchar **package_ids = NULL;
	g_autofree gchar *package_ids_temp = NULL;
	g_autofree gchar *transaction_flags_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &transaction_flags,
		       &package_ids);

	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("UpdatePackages method called: %s (transaction_flags: %s)",
		 package_ids_temp, transaction_flags_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "UpdatePackages not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check for length sanity */
	length = g_strv_length (package_ids);
	if (length > PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NUMBER_OF_PACKAGES_INVALID,
			     "Too many packages to process (%i/%i)",
			     length, PK_TRANSACTION_MAX_PACKAGES_TO_PROCESS);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check package_ids */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_PACKAGE_ID_INVALID,
			     "The package id's '%s' are not valid",
			     package_ids_temp);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_flags = transaction_flags;
	transaction->priv->cached_package_ids = g_strdupv (package_ids);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPDATE_PACKAGES);

	/* this changed */
	pk_transaction_emit_property_changed (transaction,
					      "TransactionFlags",
					      g_variant_new_uint64 (transaction_flags));

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_UPDATE_PACKAGES,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_what_provides (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	PkBitfield filter;
	g_autofree gchar **values = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t^a&s)",
		       &filter,
		       &values);

	g_debug ("WhatProvides method called: %s",
		 values[0]);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_WHAT_PROVIDES)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "WhatProvides not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check the search term */
	ret = pk_transaction_search_check (values, &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_filters = filter;
	transaction->priv->cached_values = g_strdupv (values);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_WHAT_PROVIDES);
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_upgrade_system (PkTransaction *transaction,
			       GVariant *params,
			       GDBusMethodInvocation *context)
{
	gboolean ret;
	PkBitfield transaction_flags;
	PkUpgradeKindEnum upgrade_kind;
	const gchar *distro_id;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *transaction_flags_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t&su)",
		       &transaction_flags,
		       &distro_id,
		       &upgrade_kind);

	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("UpgradeSystem method called: %s: %s  (transaction_flags: %s)",
		 distro_id,
		 pk_upgrade_kind_enum_to_string (upgrade_kind),
		 transaction_flags_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_UPGRADE_SYSTEM)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "UpgradeSystem not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_flags = transaction_flags;
	transaction->priv->cached_value = g_strdup (distro_id);
	transaction->priv->cached_upgrade_kind = upgrade_kind;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_UPGRADE_SYSTEM);

	/* this changed */
	pk_transaction_emit_property_changed (transaction,
					      "TransactionFlags",
					      g_variant_new_uint64 (transaction_flags));

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_UPGRADE_SYSTEM,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_repair_system (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	PkBitfield transaction_flags;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *transaction_flags_temp = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(t)", &transaction_flags);

	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	g_debug ("RepairSystem method called  (transaction_flags: %s)",
		 transaction_flags_temp);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
					PK_ROLE_ENUM_REPAIR_SYSTEM)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RepairSystem not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_transaction_flags = transaction_flags;
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REPAIR_SYSTEM);

	/* this changed */
	pk_transaction_emit_property_changed (transaction,
					      "TransactionFlags",
					      g_variant_new_uint64 (transaction_flags));

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
						   PK_ROLE_ENUM_REPAIR_SYSTEM,
						   &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_import_pubkey (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	const gchar *key_path = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
			 &key_path);

	g_debug ("ImportPubkey method called: %s",
	   key_path);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
			    	PK_ROLE_ENUM_IMPORT_PUBKEY)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "ImportPubkey not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* check the files exists */
	ret = g_file_test (key_path, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_set_error (&error,
			        PK_TRANSACTION_ERROR,
			        PK_TRANSACTION_ERROR_NO_SUCH_FILE,
			        "No such file %s", key_path);
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
			goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_value = g_strdup (key_path);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_IMPORT_PUBKEY);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
			               PK_ROLE_ENUM_IMPORT_PUBKEY,
			               &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static void
pk_transaction_remove_pubkey (PkTransaction *transaction,
			      GVariant *params,
			      GDBusMethodInvocation *context)
{
	gboolean ret;
	const gchar *key_id = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_TRANSACTION (transaction));
	g_return_if_fail (transaction->priv->tid != NULL);

	g_variant_get (params, "(&s)",
			 &key_id);

	g_debug ("RemovePubkey method called: %s",
	   key_id);

	/* not implemented yet */
	if (!pk_backend_is_implemented (transaction->priv->backend,
			        PK_ROLE_ENUM_REMOVE_PUBKEY)) {
		g_set_error (&error,
			     PK_TRANSACTION_ERROR,
			     PK_TRANSACTION_ERROR_NOT_SUPPORTED,
			     "RemovePubkey not supported by backend");
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}

	/* save so we can run later */
	transaction->priv->cached_value = g_strdup (key_id);
	pk_transaction_set_role (transaction, PK_ROLE_ENUM_REMOVE_PUBKEY);

	/* try to get authorization */
	ret = pk_transaction_obtain_authorization (transaction,
			               PK_ROLE_ENUM_REMOVE_PUBKEY,
			               &error);
	if (!ret) {
		pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_ERROR);
		goto out;
	}
out:
	pk_transaction_dbus_return (context, error);
}

static GVariant *
_g_variant_new_maybe_string (const gchar *value)
{
	if (value == NULL)
		return g_variant_new_string ("");
	return g_variant_new_string (value);
}

static GVariant *
pk_transaction_get_property (GDBusConnection *connection_, const gchar *sender,
			     const gchar *object_path, const gchar *interface_name,
			     const gchar *property_name, GError **error,
			     gpointer user_data)
{
	PkTransaction *transaction = PK_TRANSACTION (user_data);
	PkTransactionPrivate *priv = transaction->priv;

	if (g_strcmp0 (property_name, "Role") == 0)
		return g_variant_new_uint32 (priv->role);
	if (g_strcmp0 (property_name, "Status") == 0)
		return g_variant_new_uint32 (priv->status);
	if (g_strcmp0 (property_name, "LastPackage") == 0)
		return _g_variant_new_maybe_string (priv->last_package_id);
	if (g_strcmp0 (property_name, "Uid") == 0)
		return g_variant_new_uint32 (priv->uid);
	if (g_strcmp0 (property_name, "Percentage") == 0)
		return g_variant_new_uint32 (transaction->priv->percentage);
	if (g_strcmp0 (property_name, "AllowCancel") == 0)
		return g_variant_new_boolean (priv->allow_cancel);
	if (g_strcmp0 (property_name, "CallerActive") == 0)
		return g_variant_new_boolean (priv->caller_active);
	if (g_strcmp0 (property_name, "ElapsedTime") == 0)
		return g_variant_new_uint32 (priv->elapsed_time);
	if (g_strcmp0 (property_name, "Speed") == 0)
		return g_variant_new_uint32 (priv->speed);
	if (g_strcmp0 (property_name, "DownloadSizeRemaining") == 0)
		return g_variant_new_uint64 (priv->download_size_remaining);
	if (g_strcmp0 (property_name, "TransactionFlags") == 0)
		return g_variant_new_uint64 (priv->cached_transaction_flags);
	return NULL;
}

static void
pk_transaction_method_call (GDBusConnection *connection_, const gchar *sender,
			    const gchar *object_path, const gchar *interface_name,
			    const gchar *method_name, GVariant *parameters,
			    GDBusMethodInvocation *invocation, gpointer user_data)
{
	PkTransaction *transaction = PK_TRANSACTION (user_data);

	g_return_if_fail (transaction->priv->sender != NULL);

	/* check is the same as the sender that did CreateTransaction */
	if (g_strcmp0 (transaction->priv->sender, sender) != 0) {
		g_dbus_method_invocation_return_error (invocation,
						       PK_TRANSACTION_ERROR,
						       PK_TRANSACTION_ERROR_REFUSED_BY_POLICY,
						       "sender does not match (%s vs %s)",
						       sender,
						       transaction->priv->sender);
		return;
	}
	if (g_strcmp0 (method_name, "SetHints") == 0) {
		pk_transaction_set_hints (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "AcceptEula") == 0) {
		pk_transaction_accept_eula (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "Cancel") == 0) {
		pk_transaction_cancel (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "DownloadPackages") == 0) {
		pk_transaction_download_packages (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetCategories") == 0) {
		pk_transaction_get_categories (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "DependsOn") == 0) {
		pk_transaction_depends_on (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetDetails") == 0) {
		pk_transaction_get_details (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetDetailsLocal") == 0) {
		pk_transaction_get_details_local (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetFilesLocal") == 0) {
		pk_transaction_get_files_local (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetFiles") == 0) {
		pk_transaction_get_files (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetOldTransactions") == 0) {
		pk_transaction_get_old_transactions (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetPackages") == 0) {
		pk_transaction_get_packages (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetRepoList") == 0) {
		pk_transaction_get_repo_list (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RequiredBy") == 0) {
		pk_transaction_required_by (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetUpdateDetail") == 0) {
		pk_transaction_get_update_detail (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetUpdates") == 0) {
		pk_transaction_get_updates (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "GetDistroUpgrades") == 0) {
		pk_transaction_get_distro_upgrades (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "InstallFiles") == 0) {
		pk_transaction_install_files (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "InstallPackages") == 0) {
		pk_transaction_install_packages (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "InstallSignature") == 0) {
		pk_transaction_install_signature (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RefreshCache") == 0) {
		pk_transaction_refresh_cache (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RemovePackages") == 0) {
		pk_transaction_remove_packages (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RepoEnable") == 0) {
		pk_transaction_repo_enable (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RepoSetData") == 0) {
		pk_transaction_repo_set_data (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RepoRemove") == 0) {
		pk_transaction_repo_remove (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "Resolve") == 0) {
		pk_transaction_resolve (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "SearchDetails") == 0) {
		pk_transaction_search_details (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "SearchFiles") == 0) {
		pk_transaction_search_files (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "SearchGroups") == 0) {
		pk_transaction_search_groups (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "SearchNames") == 0) {
		pk_transaction_search_names (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "UpdatePackages") == 0) {
		pk_transaction_update_packages (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "WhatProvides") == 0) {
		pk_transaction_what_provides (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "UpgradeSystem") == 0) {
		pk_transaction_upgrade_system (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RepairSystem") == 0) {
		pk_transaction_repair_system (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "ImportPubkey") == 0) {
		pk_transaction_import_pubkey (transaction, parameters, invocation);
		return;
	}
	if (g_strcmp0 (method_name, "RemovePubkey") == 0) {
		pk_transaction_remove_pubkey (transaction, parameters, invocation);
		return;
	}

	/* nothing matched */
	g_dbus_method_invocation_return_error (invocation,
					       PK_TRANSACTION_ERROR,
					       PK_TRANSACTION_ERROR_INVALID_STATE,
					       "method from %s not recognised",
					       sender);
}

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

void
pk_transaction_reset_after_lock_error (PkTransaction *transaction)
{
	PkTransactionPrivate *priv = PK_TRANSACTION_GET_PRIVATE (transaction);
	g_return_if_fail (PK_IS_TRANSACTION (transaction));

	/* clear results */
	g_object_unref (priv->results);
	priv->results = pk_results_new ();

	/* reset transaction state */
	/* first set state manually, otherwise set_state will refuse to switch to an earlier stage */
	priv->state = PK_TRANSACTION_STATE_READY;
	pk_transaction_set_state (transaction, PK_TRANSACTION_STATE_READY);

	g_debug ("transaction has been reset after lock-required issue.");
}

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
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_ALLOW_CANCEL_CHANGED] =
		g_signal_new ("allow-cancel-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkTransactionPrivate));
}

static void
pk_transaction_init (PkTransaction *transaction)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	transaction->priv = PK_TRANSACTION_GET_PRIVATE (transaction);
	transaction->priv->allow_cancel = TRUE;
	transaction->priv->caller_active = TRUE;
	transaction->priv->cached_transaction_flags = PK_TRANSACTION_FLAG_ENUM_NONE;
	transaction->priv->cached_filters = PK_FILTER_ENUM_NONE;
	transaction->priv->uid = PK_TRANSACTION_UID_INVALID;
	transaction->priv->role = PK_ROLE_ENUM_UNKNOWN;
	transaction->priv->status = PK_STATUS_ENUM_WAIT;
	transaction->priv->percentage = PK_BACKEND_PERCENTAGE_INVALID;
	transaction->priv->state = PK_TRANSACTION_STATE_UNKNOWN;
	transaction->priv->dbus = pk_dbus_new ();
	transaction->priv->results = pk_results_new ();
	transaction->priv->supported_content_types = g_ptr_array_new_with_free_func (g_free);
	transaction->priv->cancellable = g_cancellable_new ();

	transaction->priv->transaction_db = pk_transaction_db_new ();
	ret = pk_transaction_db_load (transaction->priv->transaction_db, &error);
	if (!ret)
		g_error ("PkEngine: failed to load transaction db: %s", error->message);
}

static void
pk_transaction_dispose (GObject *object)
{
	PkTransaction *transaction;

	g_return_if_fail (PK_IS_TRANSACTION (object));

	transaction = PK_TRANSACTION (object);

	/* were we waiting for the client to authorise */
	if (transaction->priv->waiting_for_auth) {
		g_cancellable_cancel (transaction->priv->cancellable);
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

static void
pk_transaction_finalize (GObject *object)
{
	PkTransaction *transaction;

	g_return_if_fail (PK_IS_TRANSACTION (object));

	transaction = PK_TRANSACTION (object);

	if (transaction->priv->subject != NULL)
		g_object_unref (transaction->priv->subject);
	if (transaction->priv->watch_id > 0)
		g_bus_unwatch_name (transaction->priv->watch_id);
	g_free (transaction->priv->last_package_id);
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
	g_ptr_array_unref (transaction->priv->supported_content_types);

	if (transaction->priv->connection != NULL)
		g_object_unref (transaction->priv->connection);
	if (transaction->priv->introspection != NULL)
		g_dbus_node_info_unref (transaction->priv->introspection);

	g_key_file_unref (transaction->priv->conf);
	g_object_unref (transaction->priv->dbus);
	if (transaction->priv->backend != NULL)
		g_object_unref (transaction->priv->backend);
	g_object_unref (transaction->priv->job);
	g_object_unref (transaction->priv->transaction_db);
	g_object_unref (transaction->priv->results);
	if (transaction->priv->authority != NULL)
		g_object_unref (transaction->priv->authority);
	g_object_unref (transaction->priv->cancellable);

	G_OBJECT_CLASS (pk_transaction_parent_class)->finalize (object);
}

PkTransaction *
pk_transaction_new (GKeyFile *conf, GDBusNodeInfo *introspection)
{
	PkTransaction *transaction;
	transaction = g_object_new (PK_TYPE_TRANSACTION, NULL);
	transaction->priv->conf = g_key_file_ref (conf);
	transaction->priv->job = pk_backend_job_new (conf);
	transaction->priv->introspection = g_dbus_node_info_ref (introspection);
	return PK_TRANSACTION (transaction);
}

