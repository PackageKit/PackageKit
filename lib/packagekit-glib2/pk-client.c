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
 * @short_description: TODO
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
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	/* TODO: add the other existing properties */
	PROP_ID,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

typedef struct {
	PkClient			*client;
	GCancellable			*cancellable;
	gchar				*tid;
	gchar				**packages;
	GSimpleAsyncResult		*res;
	DBusGProxyCall			*call;
	PkResults			*results;
	DBusGProxy			*proxy;
	PkBitfield			 filters;
	PkClientProgressCallback	 callback_progress;
	PkClientStatusCallback		 callback_status;
	PkClientPackageCallback		 callback_package;
	gpointer			 user_data;
	PkRoleEnum			 role;
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

	g_strfreev (state->packages);

	if (state->client != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);
	}

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

	/* do the callback for GUI programs */
	if (state->callback_package != NULL)
		state->callback_package (state->client, package_id, state->user_data);
}

/**
 * pk_client_progress_changed_cb:
 */
static void
pk_client_progress_changed_cb (DBusGProxy *proxy, guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkClientState *state)
{
	/* do the callback for GUI programs */
	if (state->callback_progress != NULL)
		state->callback_progress (state->client, percentage, state->user_data);
}

/**
 * pk_client_status_changed_cb:
 */
static void
pk_client_status_changed_cb (DBusGProxy *proxy, const gchar *status_text, PkClientState *state)
{
	PkStatusEnum status_enum;

	/* do the callback for GUI programs */
	if (state->callback_status != NULL) {
		status_enum = pk_status_enum_from_text (status_text);
		state->callback_status (state->client, status_enum, state->user_data);
	}
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
#if 0
	dbus_g_proxy_connect_signal (proxy, "Transaction",
				     G_CALLBACK (pk_client_transaction_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_client_update_detail_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "DistroUpgrade",
				     G_CALLBACK (pk_client_distro_upgrade_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Details",
				     G_CALLBACK (pk_client_details_cb), state, NULL);
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
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_client_require_restart_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Message",
				     G_CALLBACK (pk_client_message_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "CallerActiveChanged",
				     G_CALLBACK (pk_client_caller_active_changed_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "AllowCancel",
				     G_CALLBACK (pk_client_allow_cancel_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Category",
				     G_CALLBACK (pk_client_category_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "MediaChangeRequired",
				     G_CALLBACK (pk_client_media_change_required_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Destroy",
				     G_CALLBACK (pk_client_destroy_cb), state, NULL);
#endif
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
#if 0
	dbus_g_proxy_disconnect_signal (proxy, "Transaction",
					G_CALLBACK (pk_client_transaction_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "DistroUpgrade",
					G_CALLBACK (pk_client_distro_upgrade_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Details",
					G_CALLBACK (pk_client_details_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Files",
					G_CALLBACK (pk_client_files_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "RepoSignatureRequired",
					G_CALLBACK (pk_client_repo_signature_required_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "EulaRequired",
					G_CALLBACK (pk_client_eula_required_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "ErrorCode",
					G_CALLBACK (pk_client_error_code_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "RequireRestart",
					G_CALLBACK (pk_client_require_restart_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Message",
					G_CALLBACK (pk_client_message_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "CallerActiveChanged",
					G_CALLBACK (pk_client_caller_active_changed_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "AllowCancel",
					G_CALLBACK (pk_client_allow_cancel_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Destroy",
					G_CALLBACK (pk_client_destroy_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "MediaChangeRequired",
					G_CALLBACK (pk_client_media_change_required_cb), state);
#endif
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
	gchar *filters_text = NULL;

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

	/* setup the proxies ready for use */
	pk_client_connect_proxy (state->proxy, state);

	/* do this async, although this should be pretty fast anyway */
	if (state->role == PK_ROLE_ENUM_RESOLVE) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "Resolve",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRV, state->packages,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetDetails",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->packages,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetUpdateDetail",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->packages,
						       G_TYPE_INVALID);
	} else {
		g_assert_not_reached ();
	}

	/* we've sent this async */
	egg_debug ("sent request");

	/* we'll have results from now on */
	state->results = pk_results_new ();

	/* deallocate temp state */
	g_free (filters_text);
}

/*****************************************************************************************************************************/

/**
 * pk_client_resolve_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @callback_progress: the function to run when the progress changes
 * @callback_status: the function to run when the status changes
 * @callback_package: the function to run when the package changes
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * TODO
 **/
void
pk_client_resolve_async (PkClient *client, PkBitfield filters, gchar **packages, GCancellable *cancellable,
			 PkClientProgressCallback callback_progress, PkClientStatusCallback callback_status,
			 PkClientPackageCallback callback_package, GAsyncReadyCallback callback_ready, gpointer user_data)
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
	state->results = NULL;
	state->proxy = NULL;
	state->call = NULL;
	state->filters = filters;
	state->packages = g_strdupv (packages);
	state->callback_progress = callback_progress;
	state->callback_status = callback_status;
	state->callback_package = callback_package;
	state->user_data = user_data;
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_resolve_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the ID, or %NULL if unset
 **/
PkResults *
pk_client_resolve_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_client_resolve_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/*****************************************************************************************************************************/

/**
 * pk_client_get_details_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @callback_progress: the function to run when the progress changes
 * @callback_status: the function to run when the status changes
 * @callback_package: the function to run when the package changes
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * TODO
 **/
void
pk_client_get_details_async (PkClient *client, gchar **packages, GCancellable *cancellable,
			     PkClientProgressCallback callback_progress, PkClientStatusCallback callback_status,
			     PkClientPackageCallback callback_package, GAsyncReadyCallback callback_ready, gpointer user_data)
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
	state->results = NULL;
	state->proxy = NULL;
	state->call = NULL;
	state->packages = g_strdupv (packages);
	state->callback_progress = callback_progress;
	state->callback_status = callback_status;
	state->callback_package = callback_package;
	state->user_data = user_data;
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_details_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the ID, or %NULL if unset
 **/
PkResults *
pk_client_get_details_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_client_get_details_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/*****************************************************************************************************************************/

/**
 * pk_client_get_update_detail_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @callback_progress: the function to run when the progress changes
 * @callback_status: the function to run when the status changes
 * @callback_package: the function to run when the package changes
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * TODO
 **/
void
pk_client_get_update_detail_async (PkClient *client, gchar **packages, GCancellable *cancellable,
				   PkClientProgressCallback callback_progress, PkClientStatusCallback callback_status,
				   PkClientPackageCallback callback_package, GAsyncReadyCallback callback_ready, gpointer user_data)
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
	state->results = NULL;
	state->proxy = NULL;
	state->call = NULL;
	state->packages = g_strdupv (packages);
	state->callback_progress = callback_progress;
	state->callback_status = callback_status;
	state->callback_package = callback_package;
	state->user_data = user_data;
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_update_detail_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the ID, or %NULL if unset
 **/
PkResults *
pk_client_get_update_detail_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_client_get_update_detail_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/**
 * pk_client_get_property:
 **/
static void
pk_client_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
//	PkClient *client = PK_CLIENT (object);
//	PkClientPrivate *priv = client->priv;

	switch (prop_id) {
//	case PROP_ID:
//		g_value_set_string (value, priv->id);
//		break;
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
//	PkClient *client = PK_CLIENT (object);
//	PkClientPrivate *priv = client->priv;

	switch (prop_id) {
//	case PROP_INFO:
//		priv->info = g_value_get_uint (value);
//		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_class_init:
 * @klass: The PkClientClass
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
	 * PkClient:id:
	 */
	pspec = g_param_spec_string ("id", NULL,
				     "The full client_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

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
 * @client: This class instance
 **/
static void
pk_client_init (PkClient *client)
{
	GError *error = NULL;
	client->priv = PK_CLIENT_GET_PRIVATE (client);

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
 * @object: The object to finalize
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
 * Return value: a new PkClient object.
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
	results = pk_client_resolve_finish (client, res, &error);
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

static guint _progress_cb = 0;
static guint _status_cb = 0;
static guint _package_cb = 0;

void
pk_client_test_progress_cb (PkClient *client, gint percentage, EggTest *test)
{
	egg_debug ("progress now %i", percentage);
	_progress_cb++;
}

void
pk_client_test_status_cb (PkClient *client, PkStatusEnum status, EggTest *test)
{
	egg_debug ("status now %s", pk_status_enum_to_text (status));
	_status_cb++;
}

void
pk_client_test_package_cb (PkClient *client, const gchar *package_id, EggTest *test)
{
	egg_debug ("package now %s", package_id);
	_package_cb++;
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
				 (PkClientProgressCallback) pk_client_test_progress_cb,
				 (PkClientStatusCallback) pk_client_test_status_cb,
				 (PkClientPackageCallback) pk_client_test_package_cb,
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

	/************************************************************/
	egg_test_title (test, "got package updates");
	if (_package_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _package_cb);

	g_object_unref (client);

	egg_test_end (test);
}
#endif

