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

/**
 * SECTION:pk-client
 * @short_description: GObject class for PackageKit client access
 *
 * A nice GObject to use for accessing PackageKit asynchronously
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-marshal.h>

#include "egg-debug.h"

static void     pk_client_finalize	(GObject     *object);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct _PkClientPrivate
{
	DBusGConnection		*connection;
	PkControl		*control;
	PkRoleEnum		 role;
	PkStatusEnum		 status;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ROLE,
	PROP_STATUS,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

typedef struct {
	gboolean			 allow_deps;
	gboolean			 autoremove;
	gboolean			 enabled;
	gboolean			 force;
	gboolean			 only_trusted;
	gboolean			 recursive;
	gchar				*directory;
	gchar				*eula_id;
	gchar				**files;
	gchar				*key_id;
	gchar				*package_id;
	gchar				**package_ids;
	gchar				*parameter;
	gchar				*repo_id;
	gchar				*search;
	gchar				*tid;
	gchar				*value;
	gpointer			 progress_user_data;
	gpointer			 user_data;
	guint				 number;
	DBusGProxyCall			*call;
	DBusGProxy			*proxy;
	GCancellable			*cancellable;
	GSimpleAsyncResult		*res;
	PkBitfield			 filters;
	PkClient			*client;
	PkProgress			*progress;
	PkProgressCallback		 progress_callback;
	PkProvidesEnum			 provides;
	PkResults			*results;
	PkRoleEnum			 role;
	PkSigTypeEnum			 type;
} PkClientState;

static void pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClientState *state);
static void pk_client_disconnect_proxy (DBusGProxy *proxy, PkClientState *state);

/**
 * pk_client_state_finish:
 **/
static void
pk_client_state_finish (PkClientState *state, GError *error)
{
	PkClientPrivate *priv;
	priv = state->client->priv;

	g_free (state->directory);
	g_free (state->eula_id);
	g_free (state->key_id);
	g_free (state->package_id);
	g_free (state->parameter);
	g_free (state->repo_id);
	g_free (state->search);
	g_free (state->value);
	g_strfreev (state->files);
	g_strfreev (state->package_ids);
	g_object_unref (state->progress);

	if (state->client != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	if (state->proxy != NULL) {
		pk_client_disconnect_proxy (state->proxy, state);
		g_object_unref (G_OBJECT (state->proxy));
	}

	if (state->results != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_object_ref (state->results), g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	g_simple_async_result_complete_in_idle (state->res);
	g_object_unref (state->res);
	g_slice_free (PkClientState, state);
}

/**
 * pk_client_finished_cb:
 */
static void
pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClientState *state)
{
	GError *error = NULL;
	PkExitEnum exit_enum;

	egg_debug ("exit_text=%s", exit_text);

	/* yay */
	exit_enum = pk_exit_enum_from_text (exit_text);
	pk_results_set_exit_code (state->results, exit_enum);

	/* failed */
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		/* TODO: get error code and error message */
		error = g_error_new (1, 0, "Failed to run: %s", exit_text);
		pk_client_state_finish (state, error);
		return;
	}

	/* we're done */
	pk_client_state_finish (state, error);
}

/**
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
//	PkClient *client = PK_CLIENT (state->client);
	GError *error = NULL;
	gboolean ret;

	/* we've sent this async */
	egg_debug ("got reply to request");

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed: %s", error->message);
		pk_client_state_finish (state, error);
		return;
	}

	/* finished this call */
	state->call = NULL;

	/* wait for ::Finished() */
}

/**
 * pk_client_package_cb:
 */
static void
pk_client_package_cb (DBusGProxy *proxy, const gchar *info_text, const gchar *package_id, const gchar *summary, PkClientState *state)
{
	PkInfoEnum info_enum;
	g_return_if_fail (PK_IS_CLIENT (state->client));

	/* add to results */
	info_enum = pk_info_enum_from_text (info_text);
	pk_results_add_package (state->results, info_enum, package_id, summary);

	/* save progress */
	g_object_set (state->progress,
		      "package_id", package_id,
		      NULL);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PACKAGE_ID, state->progress_user_data);
}

/**
 * pk_client_progress_changed_cb:
 */
static void
pk_client_progress_changed_cb (DBusGProxy *proxy, guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkClientState *state)
{
	gint percentage_new;
	gint subpercentage_new;

	/* convert to signed */
	percentage_new = (gint) percentage;
	subpercentage_new = (gint) subpercentage;

	/* daemon is odd, and says that unknown is 101 */
	if (percentage_new == 101)
		percentage_new = -1;
	if (subpercentage_new == 101)
		subpercentage_new = -1;

	/* save progress */
	g_object_set (state->progress,
		      "percentage", percentage_new,
		      "subpercentage", subpercentage_new,
		      NULL);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL) {
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_SUBPERCENTAGE, state->progress_user_data);
	}
}

/**
 * pk_client_status_changed_cb:
 */
static void
pk_client_status_changed_cb (DBusGProxy *proxy, const gchar *status_text, PkClientState *state)
{
	PkStatusEnum status_enum;

	/* convert from text */
	status_enum = pk_status_enum_from_text (status_text);

	/* save cached value */
	state->client->priv->status = status_enum;

	/* save progress */
	g_object_set (state->progress,
		      "status", status_enum,
		      NULL);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);
}

/**
 * pk_client_allow_cancel_cb:
 */
static void
pk_client_allow_cancel_cb (DBusGProxy *proxy, gboolean allow_cancel, PkClientState *state)
{
	/* save progress */
	g_object_set (state->progress,
		      "allow-cancel", allow_cancel,
		      NULL);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_ALLOW_CANCEL, state->progress_user_data);
}

/**
 * pk_client_caller_active_changed_cb:
 */
static void
pk_client_caller_active_changed_cb (DBusGProxy *proxy, gboolean is_active, PkClientState *state)
{
	/* save progress */
	g_object_set (state->progress,
		      "caller-active", is_active,
		      NULL);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_CALLER_ACTIVE, state->progress_user_data);
}

/**
 * pk_client_details_cb:
 */
static void
pk_client_details_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *license,
		      const gchar *group_text, const gchar *description, const gchar *url,
		      guint64 size, PkClientState *state)
{
	PkGroupEnum group_enum;
	group_enum = pk_group_enum_from_text (group_text);
	pk_results_add_details (state->results, package_id, license, group_enum, description, url, size);
}

/**
 * pk_client_update_detail_cb:
 */
static void
pk_client_update_detail_cb (DBusGProxy  *proxy, const gchar *package_id, const gchar *updates,
			    const gchar *obsoletes, const gchar *vendor_url, const gchar *bugzilla_url,
			    const gchar *cve_url, const gchar *restart_text, const gchar *update_text,
			    const gchar *changelog, const gchar *state_text, const gchar *issued_text,
			    const gchar *updated_text, PkClientState *state)
{
	GDate *issued;
	GDate *updated;
	PkUpdateStateEnum state_enum;
	PkRestartEnum restart_enum;

	restart_enum = pk_restart_enum_from_text (restart_text);
	state_enum = pk_update_state_enum_from_text (state_text);
	issued = pk_iso8601_to_date (issued_text);
	updated = pk_iso8601_to_date (updated_text);

	pk_results_add_update_detail (state->results, package_id, updates, obsoletes, vendor_url,
				      bugzilla_url, cve_url, restart_enum, update_text, changelog,
				      state_enum, issued, updated);

	if (issued != NULL)
		g_date_free (issued);
	if (updated != NULL)
		g_date_free (updated);
}

/**
 * pk_client_transaction_cb:
 */
static void
pk_client_transaction_cb (DBusGProxy *proxy, const gchar *old_tid, const gchar *timespec,
			  gboolean succeeded, const gchar *role_text, guint duration,
			  const gchar *data, guint uid, const gchar *cmdline, PkClientState *state)
{
	PkRoleEnum role_enum;
	role_enum = pk_role_enum_from_text (role_text);
	pk_results_add_transaction (state->results, old_tid, timespec, succeeded, role_enum, duration, data, uid, cmdline);
}

/**
 * pk_client_distro_upgrade_cb:
 */
static void
pk_client_distro_upgrade_cb (DBusGProxy *proxy, const gchar *type_text, const gchar *name,
			     const gchar *summary, PkClientState *state)
{
	PkUpdateStateEnum type_enum;
	type_enum = pk_update_state_enum_from_text (type_text);
	pk_results_add_distro_upgrade (state->results, type_enum, name, summary);
}

/**
 * pk_client_require_restart_cb:
 */
static void
pk_client_require_restart_cb (DBusGProxy  *proxy, const gchar *restart_text, const gchar *package_id, PkClientState *state)
{
	PkRestartEnum restart_enum;
	restart_enum = pk_restart_enum_from_text (restart_text);
	pk_results_add_require_restart (state->results, restart_enum, package_id);
}

/**
 * pk_client_category_cb:
 */
static void
pk_client_category_cb (DBusGProxy  *proxy, const gchar *parent_id, const gchar *cat_id,
		       const gchar *name, const gchar *summary, const gchar *icon, PkClientState *state)
{
	pk_results_add_category (state->results, parent_id, cat_id, name, summary, icon);
}

/**
 * pk_client_files_cb:
 */
static void
pk_client_files_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *filelist, PkClientState *state)
{
	gchar **files;
	files = g_strsplit (filelist, ";", -1);
	pk_results_add_files (state->results, package_id, files);
	g_strfreev (files);
}

/**
 * pk_client_repo_signature_required_cb:
 **/
static void
pk_client_repo_signature_required_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *repository_name,
				      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				      const gchar *key_fingerprint, const gchar *key_timestamp,
				      const gchar *type_text, PkClientState *state)
{
	PkSigTypeEnum type_enum;
	type_enum = pk_sig_type_enum_from_text (type_text);
	pk_results_add_repo_signature_required (state->results, package_id, repository_name, key_url, key_userid,
						key_id, key_fingerprint, key_timestamp, type_enum);
}

/**
 * pk_client_eula_required_cb:
 **/
static void
pk_client_eula_required_cb (DBusGProxy *proxy, const gchar *eula_id, const gchar *package_id,
			    const gchar *vendor_name, const gchar *license_agreement, PkClientState *state)
{
	pk_results_add_eula_required (state->results, eula_id, package_id, vendor_name, license_agreement);
}

/**
 * pk_client_media_change_required_cb:
 **/
static void
pk_client_media_change_required_cb (DBusGProxy *proxy, const gchar *media_type_text,
				    const gchar *media_id, const gchar *media_text, PkClientState *state)
{
	PkMediaTypeEnum media_type_enum;
	media_type_enum = pk_media_type_enum_from_text (media_type_text);
	pk_results_add_media_change_required (state->results, media_type_enum, media_id, media_text);
}

/**
 * pk_client_repo_detail_cb:
 **/
static void
pk_client_repo_detail_cb (DBusGProxy *proxy, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkClientState *state)
{
	pk_results_add_repo_detail (state->results, repo_id, description, enabled);
}

/**
 * pk_client_error_code_cb:
 */
static void
pk_client_error_code_cb (DBusGProxy *proxy, const gchar *code_text, const gchar *details, PkClientState *state)
{
	PkErrorCodeEnum code_enum;
	code_enum = pk_error_enum_from_text (code_text);
	pk_results_add_error_code (state->results, code_enum, details);
}

/**
 * pk_client_message_cb:
 */
static void
pk_client_message_cb (DBusGProxy  *proxy, const gchar *message_text, const gchar *details, PkClientState *state)
{
	PkMessageEnum message_enum;
	message_enum = pk_message_enum_from_text (message_text);
	pk_results_add_message (state->results, message_enum, details);
}

/**
 * pk_client_connect_proxy:
 **/
static void
pk_client_connect_proxy (DBusGProxy *proxy, PkClientState *state)
{
	/* add the signal types */
	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "ProgressChanged",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "StatusChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Transaction",
				 G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "UpdateDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "DistroUpgrade",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Details",
				 G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
				 G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Files", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RepoSignatureRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "EulaRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RepoDetail", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "ErrorCode", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RequireRestart", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Message", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "CallerActiveChanged", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "AllowCancel", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Destroy", G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Category", G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "MediaChangeRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* connect up the signals */
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_client_package_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "StatusChanged",
				     G_CALLBACK (pk_client_status_changed_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "ProgressChanged",
				     G_CALLBACK (pk_client_progress_changed_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Details",
				     G_CALLBACK (pk_client_details_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_client_update_detail_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Transaction",
				     G_CALLBACK (pk_client_transaction_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "DistroUpgrade",
				     G_CALLBACK (pk_client_distro_upgrade_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_client_require_restart_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Category",
				     G_CALLBACK (pk_client_category_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "AllowCancel",
				     G_CALLBACK (pk_client_allow_cancel_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "CallerActiveChanged",
				     G_CALLBACK (pk_client_caller_active_changed_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Files",
				     G_CALLBACK (pk_client_files_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "RepoSignatureRequired",
				     G_CALLBACK (pk_client_repo_signature_required_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "EulaRequired",
				     G_CALLBACK (pk_client_eula_required_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "RepoDetail",
				     G_CALLBACK (pk_client_repo_detail_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_client_error_code_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Message",
				     G_CALLBACK (pk_client_message_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "MediaChangeRequired",
				     G_CALLBACK (pk_client_media_change_required_cb), state, NULL);
}

/**
 * pk_client_disconnect_proxy:
 **/
static void
pk_client_disconnect_proxy (DBusGProxy *proxy, PkClientState *state)
{
	dbus_g_proxy_disconnect_signal (proxy, "Finished",
					G_CALLBACK (pk_client_finished_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Package",
					G_CALLBACK (pk_client_package_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "ProgressChanged",
					G_CALLBACK (pk_client_progress_changed_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "StatusChanged",
					G_CALLBACK (pk_client_status_changed_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Details",
					G_CALLBACK (pk_client_details_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "UpdateDetail",
					G_CALLBACK (pk_client_update_detail_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Transaction",
					G_CALLBACK (pk_client_transaction_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "DistroUpgrade",
					G_CALLBACK (pk_client_distro_upgrade_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "RequireRestart",
					G_CALLBACK (pk_client_require_restart_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "AllowCancel",
					G_CALLBACK (pk_client_allow_cancel_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "CallerActiveChanged",
					G_CALLBACK (pk_client_caller_active_changed_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Files",
					G_CALLBACK (pk_client_files_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "RepoSignatureRequired",
					G_CALLBACK (pk_client_repo_signature_required_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "EulaRequired",
					G_CALLBACK (pk_client_eula_required_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "ErrorCode",
					G_CALLBACK (pk_client_error_code_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Message",
					G_CALLBACK (pk_client_message_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "MediaChangeRequired",
					G_CALLBACK (pk_client_media_change_required_cb), state);
}

/**
 * pk_client_set_locale_cb:
 **/
static void
pk_client_set_locale_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	gchar *filters_text = NULL;
	const gchar *enum_text;
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to set locale: %s", error->message);
		pk_client_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* setup the proxies ready for use */
	pk_client_connect_proxy (state->proxy, state);

	/* do this async, although this should be pretty fast anyway */
	if (state->role == PK_ROLE_ENUM_RESOLVE) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "Resolve",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_NAME) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchName",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchDetails",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchGroup",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchFile",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetDetails",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetUpdateDetail",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_OLD_TRANSACTIONS) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetOldTransactions",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "DownloadPackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_STRING, state->directory,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATES) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetUpdates",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "UpdateSystem",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_BOOLEAN, state->only_trusted,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_DEPENDS) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetDepends",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_BOOLEAN, state->recursive,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_PACKAGES) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetPackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_REQUIRES) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetRequires",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_BOOLEAN, state->recursive,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		enum_text = pk_provides_enum_to_text (state->provides);
		state->call = dbus_g_proxy_begin_call (state->proxy, "WhatProvides",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRING, enum_text,
						       G_TYPE_STRING, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetDistroUpgrades",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_FILES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetFiles",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_CATEGORIES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetCategories",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "RemovePackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_BOOLEAN, state->allow_deps,
						       G_TYPE_BOOLEAN, state->autoremove,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "RefreshCache",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_BOOLEAN, state->force,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "InstallPackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_BOOLEAN, state->only_trusted,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_SIGNATURE) {
		enum_text = pk_sig_type_enum_to_text (state->type);
		state->call = dbus_g_proxy_begin_call (state->proxy, "InstallSignature",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, enum_text,
						       G_TYPE_STRING, state->key_id,
						       G_TYPE_STRING, state->package_id,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "UpdatePackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_BOOLEAN, state->only_trusted,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "InstallFiles",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_BOOLEAN, state->only_trusted,
						       G_TYPE_STRV, state->files,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_ACCEPT_EULA) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "AcceptEula",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, state->eula_id,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetRepoList",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_REPO_ENABLE) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "RepoEnable",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, state->repo_id,
						       G_TYPE_BOOLEAN, state->enabled,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "RepoSetData",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, state->repo_id,
						       G_TYPE_STRING, state->parameter,
						       G_TYPE_STRING, state->value,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "SimulateInstallFiles",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->files,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "SimulateInstallPackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "SimulateRemovePackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "SimulateUpdatePackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
	} else {
		g_assert_not_reached ();
	}

	/* we've sent this async */
	egg_debug ("sent request");

	/* we'll have results from now on */
	state->results = pk_results_new ();
out:
	g_free (filters_text);
	return;
}

/**
 * pk_client_get_tid_cb:
 **/
static void
pk_client_get_tid_cb (GObject *object, GAsyncResult *res, PkClientState *state)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	const gchar *tid = NULL;
	const gchar *locale;

	tid = pk_control_get_tid_finish (control, res, &error);
	if (tid == NULL) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		return;
	}

	egg_debug ("tid = %s", tid);
	state->tid = g_strdup (tid);

	/* get a connection to the tranaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", tid);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	/* set locale */
	locale = (const gchar *) setlocale (LC_ALL, NULL);
	state->call = dbus_g_proxy_begin_call (state->proxy, "SetLocale",
					       (DBusGProxyCallNotify) pk_client_set_locale_cb, state, NULL,
					       G_TYPE_STRING, locale,
					       G_TYPE_INVALID);

	/* we've sent this async */
	egg_debug ("sent locale request");
}

/**
 * pk_client_generic_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the #PkResults, or %NULL
 **/
PkResults *
pk_client_generic_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/**
 * pk_client_resolve_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_resolve_async (PkClient *client, PkBitfield filters, gchar **packages, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data,
			 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_resolve_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_RESOLVE;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->package_ids = g_strdupv (packages);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_name_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_search_name_async (PkClient *client, PkBitfield filters, const gchar *search, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_name_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_NAME;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_details_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_search_details_async (PkClient *client, PkBitfield filters, const gchar *search, GCancellable *cancellable,
			        PkProgressCallback progress_callback, gpointer progress_user_data,
			        GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_details_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_group_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_search_group_async (PkClient *client, PkBitfield filters, const gchar *search, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_group_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_GROUP;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_file_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_search_file_async (PkClient *client, PkBitfield filters, const gchar *search, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_file_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_FILE;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_details_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_details_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_details_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DETAILS;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_update_detail_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_update_detail_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_update_detail_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_download_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_download_packages_async (PkClient *client, gchar **package_ids, const gchar *directory, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_download_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_updates_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_updates_async (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_updates_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_UPDATES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_old_transactions_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_old_transactions_async (PkClient *client, guint number, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_old_transactions_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_OLD_TRANSACTIONS;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->number = number;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_update_system_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_update_system_async (PkClient *client, gboolean only_trusted, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_update_system_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UPDATE_SYSTEM;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->only_trusted = only_trusted;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_get_depends_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_depends_async (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_depends_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DEPENDS;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->recursive = recursive;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_get_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_packages_async (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}




/**
 * pk_client_get_requires_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_requires_async (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_requires_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_REQUIRES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->recursive = recursive;
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_what_provides_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_what_provides_async (PkClient *client, PkBitfield filters, PkProvidesEnum provides, const gchar *search, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_what_provides_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_WHAT_PROVIDES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->provides = provides;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_get_distro_upgrades_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_distro_upgrades_async (PkClient *client, GCancellable *cancellable,
				     PkProgressCallback progress_callback, gpointer progress_user_data,
				     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_distro_upgrades_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DISTRO_UPGRADES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_get_files_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_files_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_FILES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_get_categories_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_categories_async (PkClient *client, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_categories_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_CATEGORIES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_remove_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_remove_packages_async (PkClient *client, gchar **package_ids, gboolean allow_deps, gboolean autoremove, GCancellable *cancellable,
				 PkProgressCallback progress_callback, gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_refresh_cache_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_refresh_cache_async (PkClient *client, gboolean force, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_refresh_cache_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REFRESH_CACHE;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->force = force;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_install_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_install_packages_async (PkClient *client, gboolean only_trusted, gchar **package_ids, GCancellable *cancellable,
				  PkProgressCallback progress_callback, gpointer progress_user_data,
				  GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->only_trusted = only_trusted;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_install_signature_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_install_signature_async (PkClient *client, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_signature_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_SIGNATURE;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->type = type;
	state->key_id = g_strdup (key_id);
	state->package_id = g_strdup (package_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_update_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_update_packages_async (PkClient *client, gboolean only_trusted, gchar **package_ids, GCancellable *cancellable,
				 PkProgressCallback progress_callback, gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->only_trusted = only_trusted;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_install_files_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_install_files_async (PkClient *client, gboolean only_trusted, gchar **files, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->only_trusted = only_trusted;
	state->files = g_strdupv (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_accept_eula_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_accept_eula_async (PkClient *client, const gchar *eula_id, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_accept_eula_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_ACCEPT_EULA;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->eula_id = g_strdup (eula_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_get_repo_list_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_get_repo_list_async (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_repo_list_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_REPO_LIST;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_repo_enable_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_repo_enable_async (PkClient *client, const gchar *repo_id, gboolean enabled, GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data, GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_repo_enable_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REPO_ENABLE;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->enabled = enabled;
	state->repo_id = g_strdup (repo_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_repo_set_data_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_repo_set_data_async (PkClient *client, const gchar *repo_id, const gchar *parameter, const gchar *value, GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data, GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_repo_set_data_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REPO_SET_DATA;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->repo_id = g_strdup (repo_id);
	state->parameter = g_strdup (parameter);
	state->value = g_strdup (value);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_simulate_install_files_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_simulate_install_files_async (PkClient *client, gchar **files, GCancellable *cancellable,
					PkProgressCallback progress_callback, gpointer progress_user_data,
					GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->files = g_strdupv (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_simulate_install_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_simulate_install_packages_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
					   PkProgressCallback progress_callback, gpointer progress_user_data,
					   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_simulate_remove_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_simulate_remove_packages_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
					  PkProgressCallback progress_callback, gpointer progress_user_data,
					  GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_simulate_update_packages_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * TODO
 **/
void
pk_client_simulate_update_packages_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
					  PkProgressCallback progress_callback, gpointer progress_user_data,
					  GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_property:
 **/
static void
pk_client_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = client->priv;

	switch (prop_id) {
	case PROP_ROLE:
		g_value_set_uint (value, priv->role);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_set_property:
 **/
static void
pk_client_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_class_init:
 **/
static void
pk_client_class_init (PkClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_client_get_property;
	object_class->set_property = pk_client_set_property;
	object_class->finalize = pk_client_finalize;

	/**
	 * PkClient:role:
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkClient:status:
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkClient::changed:
	 * @client: the #PkClient instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the client data may have changed.
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkClientPrivate));
}

/**
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	GError *error = NULL;
	client->priv = PK_CLIENT_GET_PRIVATE (client);

	client->priv->status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;

	/* check dbus connections, exit if not valid */
	client->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* use a control object */
	client->priv->control = pk_control_new ();

	/* DistroUpgrade, MediaChangeRequired */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* ProgressChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT_UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT,
					   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);

	/* AllowCancel */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* StatusChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INVALID);

	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	/* ErrorCode, RequireRestart, Message */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* CallerActiveChanged */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* Details */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_UINT64,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
					   G_TYPE_INVALID);

	/* EulaRequired */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Files */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_INVALID);

	/* RepoSignatureRequired */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Package */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_INVALID);

	/* RepoDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_BOOL,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* UpdateDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* Transaction */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_BOOL_STRING_UINT_STRING_UINT_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
					   G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	/* Category */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
}

/**
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = client->priv;

	g_object_unref (priv->control);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 *
 * PkClient is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new %PkClient instance
 **/
PkClient *
pk_client_new (void)
{
	PkClient *client;
	client = g_object_new (PK_TYPE_CLIENT, NULL);
	return PK_CLIENT (client);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_client_test_resolve_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	const PkResultItemPackage *item;
	guint i;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		return;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));

	packages = pk_results_get_package_array (results);
	if (packages == NULL)
		egg_test_failed (test, "no packages!");

	/* list, just for shits and giggles */
	for (i=0; i<packages->len; i++) {
		item = g_ptr_array_index (packages, i);
		egg_debug ("%s\t%s\t%s", pk_info_enum_to_text (item->info_enum), item->package_id, item->summary);
	}

	if (packages->len != 2)
		egg_test_failed (test, "invalid number of packages: %i", packages->len);

	g_ptr_array_unref (packages);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
	egg_test_loop_quit (test);
}

static void
pk_client_test_get_details_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *details;
	const PkResultItemDetails *item;
	guint i;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		return;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));

	details = pk_results_get_details_array (results);
	if (details == NULL)
		egg_test_failed (test, "no details!");

	/* list, just for shits and giggles */
	for (i=0; i<details->len; i++) {
		item = g_ptr_array_index (details, i);
		egg_debug ("%s\t%s\t%s", item->package_id, item->url, item->description);
	}

	if (details->len != 1)
		egg_test_failed (test, "invalid number of details: %i", details->len);

	g_ptr_array_unref (details);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
	egg_test_loop_quit (test);
}

static void
pk_client_test_get_updates_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	PkPackageSack *sack;
	guint size;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		return;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));

	sack = pk_results_get_package_sack (results);
	if (sack == NULL)
		egg_test_failed (test, "no details!");

	/* check size */
	size = pk_package_sack_get_size (sack);
	if (size != 3)
		egg_test_failed (test, "invalid number of updates: %i", size);

	g_object_unref (sack);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
	egg_test_loop_quit (test);
}

static guint _progress_cb = 0;
static guint _status_cb = 0;
static guint _package_cb = 0;
static guint _allow_cancel_cb = 0;

void
pk_client_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
{
	if (type == PK_PROGRESS_TYPE_PACKAGE_ID)
		_package_cb++;
	if (type == PK_PROGRESS_TYPE_PERCENTAGE)
		_progress_cb++;
	if (type == PK_PROGRESS_TYPE_SUBPERCENTAGE)
		_progress_cb++;
	if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL)
		_allow_cancel_cb++;
	if (type == PK_PROGRESS_TYPE_STATUS)
		_status_cb++;

//	egg_debug ("percentage now %i", percentage);
}

void
pk_client_test (EggTest *test)
{
	PkClient *client;
	gchar **package_ids;

	if (!egg_test_start (test, "PkClient"))
		return;

	/************************************************************/
	egg_test_title (test, "get client");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);

	/************************************************************/
	egg_test_title (test, "resolve package");
	package_ids = g_strsplit ("glib2;2.14.0;i386;fedora,powertop", ",", -1);
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, NULL,
				 (PkProgressCallback) pk_client_test_progress_cb, test,
				 (GAsyncReadyCallback) pk_client_test_resolve_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "got progress updates");
	if (_progress_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _progress_cb);

	/************************************************************/
	egg_test_title (test, "got status updates");
	if (_status_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _status_cb);

	/* reset */
	_progress_cb = 0;
	_status_cb = 0;
//	_package_cb = 0;

	/************************************************************/
	egg_test_title (test, "get details about package");
	package_ids = g_strsplit ("powertop;1.8-1.fc8;i386;fedora", ",", -1);
	pk_client_get_details_async (client, package_ids, NULL,
				     (PkProgressCallback) pk_client_test_progress_cb, test,
				     (GAsyncReadyCallback) pk_client_test_get_details_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "got progress updates");
	if (_progress_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _progress_cb);

	/************************************************************/
	egg_test_title (test, "got status updates");
	if (_status_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _status_cb);

	/* reset */
	_progress_cb = 0;
	_status_cb = 0;
//	_package_cb = 0;

	/************************************************************/
	egg_test_title (test, "get updates");
	pk_client_get_updates_async (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), NULL,
				     (PkProgressCallback) pk_client_test_progress_cb, test,
				     (GAsyncReadyCallback) pk_client_test_get_updates_cb, test);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "got updates in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "got status updates");
	if (_status_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _status_cb);

	g_object_unref (client);

	egg_test_end (test);
}
#endif

