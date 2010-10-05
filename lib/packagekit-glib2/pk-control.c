/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-control
 * @short_description: For querying data about PackageKit
 *
 * A GObject to use for accessing PackageKit asynchronously.
 */

#include "config.h"

#include <string.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-version.h>

#include "egg-debug.h"

static void     pk_control_finalize	(GObject     *object);

#define PK_CONTROL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONTROL, PkControlPrivate))

#define PK_CONTROL_DBUS_METHOD_TIMEOUT		1500 /* ms */

/**
 * PkControlPrivate:
 *
 * Private #PkControl data
 **/
struct _PkControlPrivate
{
	DBusGProxyCall		*call_get_properties;
	GPtrArray		*calls;
	DBusGProxy		*proxy;
	DBusGProxy		*proxy_props;
	DBusGProxy		*proxy_dbus;
	DBusGConnection		*connection;
	guint			 version_major;
	guint			 version_minor;
	guint			 version_micro;
	gchar			*backend_name;
	gchar			*backend_description;
	gchar			*backend_author;
	PkBitfield		 roles;
	PkBitfield		 groups;
	PkBitfield		 filters;
	gchar			*mime_types;
	gboolean		 connected;
	gboolean		 locked;
	PkNetworkEnum		 network_state;
	gchar			*distro_id;
	guint			 transaction_list_changed_id;
	guint			 restart_schedule_id;
	guint			 updates_changed_id;
	guint			 repo_list_changed_id;
};

enum {
	SIGNAL_TRANSACTION_LIST_CHANGED,
	SIGNAL_RESTART_SCHEDULE,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_REPO_LIST_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_VERSION_MAJOR,
	PROP_VERSION_MINOR,
	PROP_VERSION_MICRO,
	PROP_BACKEND_NAME,
	PROP_BACKEND_DESCRIPTION,
	PROP_BACKEND_AUTHOR,
	PROP_ROLES,
	PROP_GROUPS,
	PROP_FILTERS,
	PROP_MIME_TYPES,
	PROP_LOCKED,
	PROP_NETWORK_STATE,
	PROP_CONNECTED,
	PROP_DISTRO_ID,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };
static gpointer pk_control_object = NULL;

G_DEFINE_TYPE (PkControl, pk_control, G_TYPE_OBJECT)

typedef struct {
	gboolean		 ret;
	gchar			*tid;
	gchar			**transaction_list;
	gchar			*daemon_state;
	guint			 time;
	gulong			 cancellable_id;
	DBusGProxyCall		*call;
	GCancellable		*cancellable;
	GSimpleAsyncResult	*res;
	PkAuthorizeEnum		 authorize;
	PkControl		*control;
	PkNetworkEnum		 network;
} PkControlState;

/**
 * pk_control_error_quark:
 *
 * We are a GObject that sets errors
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.5.2
 **/
GQuark
pk_control_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_control_error");
	return quark;
}

/**
 * pk_control_fixup_dbus_error:
 **/
static void
pk_control_fixup_dbus_error (GError *error)
{
	g_return_if_fail (error != NULL);

	/* hardcode domain */
	error->domain = PK_CONTROL_ERROR;

	/* find a better failure code */
	if (error->code == DBUS_GERROR_SPAWN_CHILD_EXITED)
		error->code = PK_CONTROL_ERROR_CANNOT_START_DAEMON;
	else
		error->code = PK_CONTROL_ERROR_FAILED;
}

/**
 * pk_control_cancellable_cancel_cb:
 **/
static void
pk_control_cancellable_cancel_cb (GCancellable *cancellable, PkControlState *state)
{
	/* dbus method is pending now, just cancel both */
	if (state->call != NULL) {
		dbus_g_proxy_cancel_call (state->control->priv->proxy, state->call);
		dbus_g_proxy_cancel_call (state->control->priv->proxy_props, state->call);
		egg_debug ("cancelling, ended DBus call: %p (%p)", state, state->call);
		state->call = NULL;
	}
}

/***************************************************************************************************/

/**
 * pk_control_get_tid_state_finish:
 **/
static void
pk_control_get_tid_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->tid != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdup (state->tid), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_free (state->tid);
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_tid_cb:
 **/
static void
pk_control_get_tid_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *tid = NULL;
	gboolean ret;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &tid,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_tid_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save results */
	state->tid = g_strdup (tid);

	/* we're done */
	pk_control_get_tid_state_finish (state, NULL);
out:
	g_free (tid);
}

/**
 * pk_control_call_destroy_cb:
 **/
static void
pk_control_call_destroy_cb (PkControlState *state)
{
	if (state->call != NULL)
		egg_warning ("%p was destroyed before it was cleared", state->call);
}

/**
 * pk_control_get_tid_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets a transacton ID from the daemon.
 *
 * Since: 0.5.2
 **/
void
pk_control_get_tid_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_tid_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_tid_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus method async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTid",
					       (DBusGProxyCallNotify) pk_control_get_tid_cb, state,
					       (GDestroyNotify) pk_control_call_destroy_cb, G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_get_tid_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the ID, or %NULL if unset, free with g_free()
 *
 * Since: 0.5.2
 **/
gchar *
pk_control_get_tid_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_tid_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_strdup (g_simple_async_result_get_op_res_gpointer (simple));
}

/***************************************************************************************************/

/**
 * pk_control_suggest_daemon_quit_state_finish:
 **/
static void
pk_control_suggest_daemon_quit_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_suggest_daemon_quit_cb:
 **/
static void
pk_control_suggest_daemon_quit_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *tid = NULL;
	gboolean ret;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to suggest quit: %s", error->message);
		pk_control_suggest_daemon_quit_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->ret = TRUE;

	/* we're done */
	pk_control_suggest_daemon_quit_state_finish (state, NULL);
out:
	g_free (tid);
}

/**
 * pk_control_suggest_daemon_quit_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Suggests to the daemon that it should quit as soon as possible.
 *
 * Since: 0.6.2
 **/
void
pk_control_suggest_daemon_quit_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_suggest_daemon_quit_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_suggest_daemon_quit_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus method async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "SuggestDaemonQuit",
					       (DBusGProxyCallNotify) pk_control_suggest_daemon_quit_cb, state,
					       NULL, G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_suggest_daemon_quit_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if the suggestion was sent
 *
 * Since: 0.6.2
 **/
gboolean
pk_control_suggest_daemon_quit_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_suggest_daemon_quit_async, FALSE);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/***************************************************************************************************/

/**
 * pk_control_get_daemon_state_state_finish:
 **/
static void
pk_control_get_daemon_state_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->daemon_state != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdup (state->daemon_state), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_free (state->daemon_state);
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_daemon_state_cb:
 **/
static void
pk_control_get_daemon_state_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *daemon_state = NULL;
	gboolean ret;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &daemon_state,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_daemon_state_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save results */
	state->daemon_state = g_strdup (daemon_state);

	/* we're done */
	pk_control_get_daemon_state_state_finish (state, NULL);
out:
	g_free (daemon_state);
}

/**
 * pk_control_get_daemon_state_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets the debugging state from the daemon.
 *
 * Since: 0.5.2
 **/
void
pk_control_get_daemon_state_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_daemon_state_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_daemon_state_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus method async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetDaemonState",
					       (DBusGProxyCallNotify) pk_control_get_daemon_state_cb, state,
					       NULL, G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_get_daemon_state_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the ID, or %NULL if unset, free with g_free()
 *
 * Since: 0.5.2
 **/
gchar *
pk_control_get_daemon_state_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_daemon_state_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_strdup (g_simple_async_result_get_op_res_gpointer (simple));
}

/***************************************************************************************************/

/**
 * pk_control_set_proxy_state_finish:
 **/
static void
pk_control_set_proxy_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_set_proxy_cb:
 **/
static void
pk_control_set_proxy_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *tid = NULL;
	gboolean ret;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to set proxy: %s", error->message);
		pk_control_set_proxy_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->ret = TRUE;

	/* we're done */
	pk_control_set_proxy_state_finish (state, NULL);
out:
	g_free (tid);
}

/**
 * pk_control_set_proxy_async:
 * @control: a valid #PkControl instance
 * @proxy_http: a HTTP proxy string such as "username:password@server.lan:8080"
 * @proxy_ftp: a FTP proxy string such as "server.lan:8080"
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Set a proxy on the PK daemon
 *
 * Since: 0.5.2
 **/
void
pk_control_set_proxy_async (PkControl *control, const gchar *proxy_http, const gchar *proxy_ftp, GCancellable *cancellable,
			    GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_set_proxy_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_set_proxy_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus set_proxy async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "SetProxy",
					       (DBusGProxyCallNotify) pk_control_set_proxy_cb, state, NULL,
					       G_TYPE_STRING, proxy_http,
					       G_TYPE_STRING, proxy_ftp,
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_set_proxy_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if we set the proxy successfully
 *
 * Since: 0.5.2
 **/
gboolean
pk_control_set_proxy_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_set_proxy_async, FALSE);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/***************************************************************************************************/

/**
 * pk_control_set_root_state_finish:
 **/
static void
pk_control_set_root_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_set_root_cb:
 **/
static void
pk_control_set_root_cb (DBusGProxy *root, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *tid = NULL;
	gboolean ret;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (root, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to set root: %s", error->message);
		pk_control_set_root_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->ret = TRUE;

	/* we're done */
	pk_control_set_root_state_finish (state, NULL);
out:
	g_free (tid);
}

/**
 * pk_control_set_root_async:
 * @control: a valid #PkControl instance
 * @root: an install root string such as "/mnt/ltsp"
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Set the install root for the backend used by PackageKit
 *
 * Since: 0.6.4
 **/
void
pk_control_set_root_async (PkControl *control, const gchar *root, GCancellable *cancellable,
			   GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_set_root_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_set_root_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus set_root async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "SetRoot",
					       (DBusGProxyCallNotify) pk_control_set_root_cb, state, NULL,
					       G_TYPE_STRING, root,
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_set_root_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if we set the root successfully
 *
 * Since: 0.6.4
 **/
gboolean
pk_control_set_root_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_set_root_async, FALSE);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/***************************************************************************************************/

/**
 * pk_control_get_transaction_list_state_finish:
 **/
static void
pk_control_get_transaction_list_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->transaction_list != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdupv (state->transaction_list), (GDestroyNotify) g_strfreev);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_strfreev (state->transaction_list);
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_transaction_list_cb:
 **/
static void
pk_control_get_transaction_list_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar **temp = NULL;
	gboolean ret;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRV, &temp,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_transaction_list_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->transaction_list = g_strdupv (temp);

	/* we're done */
	pk_control_get_transaction_list_state_finish (state, NULL);
out:
	g_strfreev (temp);
}

/**
 * pk_control_get_transaction_list_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets the transactions currently running in the daemon.
 *
 * Since: 0.5.2
 **/
void
pk_control_get_transaction_list_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_transaction_list_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_transaction_list_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus get_transaction_list async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTransactionList",
					       (DBusGProxyCallNotify) pk_control_get_transaction_list_cb, state,
					       NULL, G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_get_transaction_list_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: (transfer full): A GStrv list of transaction ID's, free with g_strfreev()
 *
 * Since: 0.5.2
 **/
gchar **
pk_control_get_transaction_list_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_transaction_list_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_strdupv (g_simple_async_result_get_op_res_gpointer (simple));
}

/***************************************************************************************************/

/**
 * pk_control_get_time_since_action_state_finish:
 **/
static void
pk_control_get_time_since_action_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->time != 0) {
		g_simple_async_result_set_op_res_gssize (state->res, state->time);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_time_since_action_cb:
 **/
static void
pk_control_get_time_since_action_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gboolean ret;
	guint seconds;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_UINT, &seconds,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->time = seconds;
	if (state->time == 0) {
		error = g_error_new (PK_CONTROL_ERROR, PK_CONTROL_ERROR_FAILED, "could not get time");
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we're done */
	pk_control_get_time_since_action_state_finish (state, NULL);
out:
	return;
}

/**
 * pk_control_get_time_since_action_async:
 * @control: a valid #PkControl instance
 * @role: the role enum, e.g. %PK_ROLE_ENUM_GET_UPDATES
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * We may want to know how long it has been since we refreshed the cache or
 * retrieved the update list.
 *
 * Since: 0.5.2
 **/
void
pk_control_get_time_since_action_async (PkControl *control, PkRoleEnum role, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;
	const gchar *role_text;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_time_since_action_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus get_time_since_action async */
	role_text = pk_role_enum_to_string (role);
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTimeSinceAction",
					       (DBusGProxyCallNotify) pk_control_get_time_since_action_cb, state, NULL,
					       G_TYPE_STRING, role_text,
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_get_time_since_action_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if the daemon serviced the request
 *
 * Since: 0.5.2
 **/
guint
pk_control_get_time_since_action_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), 0);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), 0);
	g_return_val_if_fail (error == NULL || *error == NULL, 0);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_time_since_action_async, 0);

	if (g_simple_async_result_propagate_error (simple, error))
		return 0;

	return (guint) g_simple_async_result_get_op_res_gssize (simple);
}

/***************************************************************************************************/

/**
 * pk_control_can_authorize_state_finish:
 **/
static void
pk_control_can_authorize_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->authorize != PK_AUTHORIZE_ENUM_UNKNOWN) {
		g_simple_async_result_set_op_res_gssize (state->res, state->authorize);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_can_authorize_cb:
 **/
static void
pk_control_can_authorize_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gboolean ret;
	gchar *authorize_state = NULL;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &authorize_state,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_can_authorize_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->authorize = pk_authorize_type_enum_from_string (authorize_state);
	if (state->authorize == PK_AUTHORIZE_ENUM_UNKNOWN) {
		error = g_error_new (PK_CONTROL_ERROR, PK_CONTROL_ERROR_FAILED, "could not get state");
		pk_control_can_authorize_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we're done */
	pk_control_can_authorize_state_finish (state, NULL);
out:
	g_free (authorize_state);
	return;
}

/**
 * pk_control_can_authorize_async:
 * @control: a valid #PkControl instance
 * @action_id: The action ID, for instance "org.freedesktop.PackageKit.install-untrusted"
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * We may want to know before we run a method if we are going to be denied,
 * accepted or challenged for authentication.
 *
 * Since: 0.5.2
 **/
void
pk_control_can_authorize_async (PkControl *control, const gchar *action_id, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_can_authorize_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_can_authorize_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->authorize = PK_AUTHORIZE_ENUM_UNKNOWN;

	/* call D-Bus async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "CanAuthorize",
					       (DBusGProxyCallNotify) pk_control_can_authorize_cb, state, NULL,
					       G_TYPE_STRING, action_id,
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_can_authorize_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the %PkAuthorizeEnum or %PK_AUTHORIZE_ENUM_UNKNOWN if the method failed
 *
 * Since: 0.5.2
 **/
PkAuthorizeEnum
pk_control_can_authorize_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_AUTHORIZE_ENUM_UNKNOWN);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), PK_AUTHORIZE_ENUM_UNKNOWN);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_can_authorize_async, PK_AUTHORIZE_ENUM_UNKNOWN);

	if (g_simple_async_result_propagate_error (simple, error))
		return PK_AUTHORIZE_ENUM_UNKNOWN;

	return (PkAuthorizeEnum) g_simple_async_result_get_op_res_gssize (simple);
}

/***************************************************************************************************/

/**
 * pk_control_get_properties_state_finish:
 **/
static void
pk_control_get_properties_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);
	if (state->call != NULL)
		egg_warning ("state array remove %p (%p)", state, state->call);
	else
		egg_debug ("state array remove %p", state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_set_version_major:
 **/
static void
pk_control_set_version_major (PkControl *control, guint version_major)
{
	/* is the same as before */
	if (control->priv->version_major == version_major)
		return;
	control->priv->version_major = version_major;

	/* notify we're changed */
	egg_debug ("notify::version-major");
	g_object_notify (G_OBJECT(control), "version-major");
}

/**
 * pk_control_set_version_minor:
 **/
static void
pk_control_set_version_minor (PkControl *control, guint version_minor)
{
	/* is the same as before */
	if (control->priv->version_minor == version_minor)
		return;
	control->priv->version_minor = version_minor;

	/* notify we're changed */
	egg_debug ("notify::version-minor");
	g_object_notify (G_OBJECT(control), "version-minor");
}

/**
 * pk_control_set_version_micro:
 **/
static void
pk_control_set_version_micro (PkControl *control, guint version_micro)
{
	/* is the same as before */
	if (control->priv->version_micro == version_micro)
		return;
	control->priv->version_micro = version_micro;

	/* notify we're changed */
	egg_debug ("notify::version-micro");
	g_object_notify (G_OBJECT(control), "version-micro");
}

/**
 * pk_control_set_locked:
 **/
static void
pk_control_set_locked (PkControl *control, gboolean locked)
{
	/* is the same as before */
	if (control->priv->locked == locked)
		return;
	control->priv->locked = locked;

	/* notify we're changed */
	egg_debug ("notify::locked");
	g_object_notify (G_OBJECT(control), "locked");
}

/**
 * pk_control_set_backend_name:
 **/
static void
pk_control_set_backend_name (PkControl *control, const gchar *backend_name)
{
	/* is the same as before */
	if (g_strcmp0 (control->priv->backend_name, backend_name) == 0)
		return;
	g_free (control->priv->backend_name);
	control->priv->backend_name = g_strdup (backend_name);

	/* notify we're changed */
	egg_debug ("notify::backend-name");
	g_object_notify (G_OBJECT(control), "backend-name");
}

/**
 * pk_control_set_backend_author:
 **/
static void
pk_control_set_backend_author (PkControl *control, const gchar *backend_author)
{
	/* is the same as before */
	if (g_strcmp0 (control->priv->backend_author, backend_author) == 0)
		return;
	g_free (control->priv->backend_author);
	control->priv->backend_author = g_strdup (backend_author);

	/* notify we're changed */
	egg_debug ("notify::backend-author");
	g_object_notify (G_OBJECT(control), "backend-author");
}

/**
 * pk_control_set_backend_description:
 **/
static void
pk_control_set_backend_description (PkControl *control, const gchar *backend_description)
{
	/* is the same as before */
	if (g_strcmp0 (control->priv->backend_description, backend_description) == 0)
		return;
	g_free (control->priv->backend_description);
	control->priv->backend_description = g_strdup (backend_description);

	/* notify we're changed */
	egg_debug ("notify::backend-description");
	g_object_notify (G_OBJECT(control), "backend-description");
}

/**
 * pk_control_set_mime_types:
 **/
static void
pk_control_set_mime_types (PkControl *control, const gchar *mime_types)
{
	/* is the same as before */
	if (g_strcmp0 (control->priv->mime_types, mime_types) == 0)
		return;
	g_free (control->priv->mime_types);
	control->priv->mime_types = g_strdup (mime_types);

	/* notify we're changed */
	egg_debug ("notify::mime-types");
	g_object_notify (G_OBJECT(control), "mime-types");
}

/**
 * pk_control_set_roles:
 **/
static void
pk_control_set_roles (PkControl *control, PkBitfield roles)
{
	/* is the same as before */
	if (control->priv->roles == roles)
		return;
	control->priv->roles = roles;

	/* notify we're changed */
	egg_debug ("notify::roles");
	g_object_notify (G_OBJECT(control), "roles");
}

/**
 * pk_control_set_groups:
 **/
static void
pk_control_set_groups (PkControl *control, PkBitfield groups)
{
	/* is the same as before */
	if (control->priv->groups == groups)
		return;
	control->priv->groups = groups;

	/* notify we're changed */
	egg_debug ("notify::groups");
	g_object_notify (G_OBJECT(control), "groups");
}

/**
 * pk_control_set_filters:
 **/
static void
pk_control_set_filters (PkControl *control, PkBitfield filters)
{
	/* is the same as before */
	if (control->priv->filters == filters)
		return;
	control->priv->filters = filters;

	/* notify we're changed */
	egg_debug ("notify::filters");
	g_object_notify (G_OBJECT(control), "filters");
}

/**
 * pk_control_set_network_state:
 **/
static void
pk_control_set_network_state (PkControl *control, PkNetworkEnum network_state)
{
	/* is the same as before */
	if (control->priv->network_state == network_state)
		return;
	control->priv->network_state = network_state;

	/* notify we're changed */
	egg_debug ("notify::network-state");
	g_object_notify (G_OBJECT(control), "network-state");
}

/**
 * pk_control_set_distro_id:
 **/
static void
pk_control_set_distro_id (PkControl *control, const gchar *distro_id)
{
	/* is the same as before */
	if (g_strcmp0 (control->priv->distro_id, distro_id) == 0)
		return;
	g_free (control->priv->distro_id);
	control->priv->distro_id = g_strdup (distro_id);

	/* notify we're changed */
	egg_debug ("notify::distro-id");
	g_object_notify (G_OBJECT(control), "distro-id");
}

/**
 * pk_control_get_properties_collect_cb:
 **/
static void
pk_control_get_properties_collect_cb (const char *key, const GValue *value, PkControl *control)
{
	if (g_strcmp0 (key, "VersionMajor") == 0) {
		pk_control_set_version_major (control, g_value_get_uint (value));
	} else if (g_strcmp0 (key, "VersionMinor") == 0) {
		pk_control_set_version_minor (control, g_value_get_uint (value));
	} else if (g_strcmp0 (key, "VersionMicro") == 0) {
		pk_control_set_version_micro (control, g_value_get_uint (value));
	} else if (g_strcmp0 (key, "BackendName") == 0) {
		pk_control_set_backend_name (control, g_value_get_string (value));
	} else if (g_strcmp0 (key, "BackendDescription") == 0) {
		pk_control_set_backend_description (control, g_value_get_string (value));
	} else if (g_strcmp0 (key, "BackendAuthor") == 0) {
		pk_control_set_backend_author (control, g_value_get_string (value));
	} else if (g_strcmp0 (key, "MimeTypes") == 0) {
		pk_control_set_mime_types (control, g_value_get_string (value));
	} else if (g_strcmp0 (key, "Roles") == 0) {
		pk_control_set_roles (control, pk_role_bitfield_from_string (g_value_get_string (value)));
	} else if (g_strcmp0 (key, "Groups") == 0) {
		pk_control_set_groups (control, pk_group_bitfield_from_string (g_value_get_string (value)));
	} else if (g_strcmp0 (key, "Filters") == 0) {
		pk_control_set_filters (control, pk_filter_bitfield_from_string (g_value_get_string (value)));
	} else if (g_strcmp0 (key, "Locked") == 0) {
		pk_control_set_locked (control, g_value_get_boolean (value));
	} else if (g_strcmp0 (key, "NetworkState") == 0) {
		pk_control_set_network_state (control, pk_network_enum_from_string (g_value_get_string (value)));
	} else if (g_strcmp0 (key, "DistroId") == 0) {
		pk_control_set_distro_id (control, g_value_get_string (value));
	} else {
		egg_warning ("unhandled property '%s'", key);
	}
}

/**
 * pk_control_get_properties_cb:
 **/
static void
pk_control_get_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gboolean ret;
	GHashTable *hash;

	/* finished this call */
	state->call = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to get properties: %s", error->message);
		pk_control_get_properties_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->ret = TRUE;

	/* process results */
	if (hash != NULL) {
		g_object_freeze_notify (G_OBJECT(state->control));
		g_hash_table_foreach (hash, (GHFunc) pk_control_get_properties_collect_cb, state->control);
		g_hash_table_unref (hash);
		g_object_thaw_notify (G_OBJECT(state->control));
	}

	/* we're done */
	pk_control_get_properties_state_finish (state, NULL);
out:
	return;
}

/**
 * pk_control_get_properties_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets global properties from the daemon.
 *
 * Since: 0.5.2
 **/
void
pk_control_get_properties_async (PkControl *control, GCancellable *cancellable,
				 GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_properties_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_control_cancellable_cancel_cb), state, NULL);
	}

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_properties_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* call D-Bus get_properties async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy_props, "GetAll",
					       (DBusGProxyCallNotify) pk_control_get_properties_cb, state, NULL,
					       G_TYPE_STRING, "org.freedesktop.PackageKit",
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
	egg_debug ("state array add %p (%p)", state, state->call);
out:
	g_object_unref (res);
}

/**
 * pk_control_get_properties_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: %TRUE if we set the proxy successfully
 *
 * Since: 0.5.2
 **/
gboolean
pk_control_get_properties_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_properties_async, FALSE);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (simple);
}

/***************************************************************************************************/

typedef struct {
	PkControl	*control;
	gchar		**transaction_ids;
} PkIdleSignalStore;

/**
 * pk_control_idle_signal_store_free:
 */
static void
pk_control_idle_signal_store_free (PkIdleSignalStore *store)
{
	g_strfreev (store->transaction_ids);
	g_object_unref (store->control);
	g_free (store);
}

/**
 * pk_control_transaction_list_changed_idle_cb:
 */
static gboolean
pk_control_transaction_list_changed_idle_cb (PkIdleSignalStore *store)
{
	egg_debug ("emit transaction-list-changed");
	g_signal_emit (store->control, signals[SIGNAL_TRANSACTION_LIST_CHANGED], 0, store->transaction_ids);
	store->control->priv->transaction_list_changed_id = 0;
	pk_control_idle_signal_store_free (store);
	return FALSE;
}

/**
 * pk_control_transaction_list_changed_cb:
 */
static void
pk_control_transaction_list_changed_cb (DBusGProxy *proxy, gchar **transaction_ids, PkControl *control)
{
	PkIdleSignalStore *store;

	g_return_if_fail (PK_IS_CONTROL (control));

	/* already pending */
	if (control->priv->transaction_list_changed_id != 0) {
		egg_debug ("already pending, so ignoring");
		return;
	}

	/* create store object */
	store = g_new0 (PkIdleSignalStore, 1);
	store->control = g_object_ref (control);
	store->transaction_ids = g_strdupv (transaction_ids);

	/* we have to do this idle as the transaction list will change when not yet finished */
	egg_debug ("emit transaction-list-changed (when idle)");
	control->priv->transaction_list_changed_id =
		g_idle_add ((GSourceFunc) pk_control_transaction_list_changed_idle_cb, store);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (control->priv->transaction_list_changed_id,
				 "[PkControl] transaction-list-changed");
#endif
}

/**
 * pk_control_restart_schedule_idle_cb:
 */
static gboolean
pk_control_restart_schedule_idle_cb (PkIdleSignalStore *store)
{
	egg_debug ("emit transaction-list-changed");
	g_signal_emit (store->control, signals[SIGNAL_RESTART_SCHEDULE], 0);
	store->control->priv->restart_schedule_id = 0;
	pk_control_idle_signal_store_free (store);
	return FALSE;
}

/**
 * pk_control_restart_schedule_cb:
 */
static void
pk_control_restart_schedule_cb (DBusGProxy *proxy, PkControl *control)
{
	PkIdleSignalStore *store;

	g_return_if_fail (PK_IS_CONTROL (control));

	/* already pending */
	if (control->priv->restart_schedule_id != 0) {
		egg_debug ("already pending, so ignoring");
		return;
	}

	/* create store object */
	store = g_new0 (PkIdleSignalStore, 1);
	store->control = g_object_ref (control);

	/* we have to do this idle as the transaction list will change when not yet finished */
	egg_debug ("emit restart-schedule (when idle)");
	store->control->priv->restart_schedule_id =
		g_idle_add ((GSourceFunc) pk_control_restart_schedule_idle_cb, store);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (store->control->priv->restart_schedule_id,
				 "[PkControl] restart-schedule");
#endif
}

/**
 * pk_control_updates_changed_idle_cb:
 */
static gboolean
pk_control_updates_changed_idle_cb (PkIdleSignalStore *store)
{
	egg_debug ("emit transaction-list-changed");
	g_signal_emit (store->control, signals[SIGNAL_UPDATES_CHANGED], 0);
	store->control->priv->updates_changed_id = 0;
	pk_control_idle_signal_store_free (store);
	return FALSE;
}

/**
 * pk_control_updates_changed_cb:
 */
static void
pk_control_updates_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	PkIdleSignalStore *store;

	g_return_if_fail (PK_IS_CONTROL (control));

	/* already pending */
	if (control->priv->updates_changed_id != 0) {
		egg_debug ("already pending, so ignoring");
		return;
	}

	/* create store object */
	store = g_new0 (PkIdleSignalStore, 1);
	store->control = g_object_ref (control);

	/* we have to do this idle as the transaction list will change when not yet finished */
	egg_debug ("emit updates-changed (when idle)");
	control->priv->updates_changed_id =
		g_idle_add ((GSourceFunc) pk_control_updates_changed_idle_cb, store);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (control->priv->updates_changed_id,
				 "[PkControl] updates-changed");
#endif
}

/**
 * pk_control_repo_list_changed_idle_cb:
 */
static gboolean
pk_control_repo_list_changed_idle_cb (PkIdleSignalStore *store)
{
	egg_debug ("emit transaction-list-changed");
	g_signal_emit (store->control, signals[SIGNAL_REPO_LIST_CHANGED], 0);
	store->control->priv->repo_list_changed_id = 0;
	pk_control_idle_signal_store_free (store);
	return FALSE;
}

/**
 * pk_control_repo_list_changed_cb:
 */
static void
pk_control_repo_list_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	PkIdleSignalStore *store;

	g_return_if_fail (PK_IS_CONTROL (control));

	/* already pending */
	if (control->priv->repo_list_changed_id != 0) {
		egg_debug ("already pending, so ignoring");
		return;
	}

	/* create store object */
	store = g_new0 (PkIdleSignalStore, 1);
	store->control = g_object_ref (control);

	/* we have to do this idle as the transaction list will change when not yet finished */
	egg_debug ("emit repo-list-changed (when idle)");
	control->priv->repo_list_changed_id =
		g_idle_add ((GSourceFunc) pk_control_repo_list_changed_idle_cb, store);
#if GLIB_CHECK_VERSION(2,25,8)
	g_source_set_name_by_id (control->priv->repo_list_changed_id,
				 "[PkControl] repo-list-changed");
#endif
}

/**
 * pk_control_changed_get_properties_cb:
 **/
static void
pk_control_changed_get_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControl *control)
{
	GError *error = NULL;
	gboolean ret;
	GHashTable *hash;

	/* finished this call */
	control->priv->call_get_properties = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to get properties: %s", error->message);
		goto out;
	}

	/* process results */
	if (hash != NULL) {
		g_object_freeze_notify (G_OBJECT(control));
		g_hash_table_foreach (hash, (GHFunc) pk_control_get_properties_collect_cb, control);
		g_hash_table_unref (hash);
		g_object_thaw_notify (G_OBJECT(control));
	}
out:
	return;
}

/**
 * pk_control_changed_cb:
 */
static void
pk_control_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	/* already getting properties */
	if (control->priv->call_get_properties != NULL) {
		egg_warning ("already getting properties, will ignore");
		return;
	}

	/* call D-Bus get_properties async */
	egg_debug ("properties changed, so getting new list");

	/* TODO: idle? */
	control->priv->call_get_properties =
		dbus_g_proxy_begin_call (control->priv->proxy_props, "GetAll",
					 (DBusGProxyCallNotify) pk_control_changed_get_properties_cb, control, NULL,
					 G_TYPE_STRING, "org.freedesktop.PackageKit",
					 G_TYPE_INVALID);
}

/**
 * pk_control_cancel_all_dbus_methods:
 **/
static gboolean
pk_control_cancel_all_dbus_methods (PkControl *control)
{
	const PkControlState *state;
	guint i;
	GPtrArray *array;

	/* just cancel the call */
	array = control->priv->calls;
	for (i=0; i<array->len; i++) {
		state = g_ptr_array_index (array, i);
		if (state->call == NULL)
			continue;
		egg_debug ("cancel in flight call: %p (%p)", state, state->call);
		dbus_g_proxy_cancel_call (control->priv->proxy, state->call);
	}

	return TRUE;
}

/**
 * pk_control_name_owner_changed_cb:
 **/
static void
pk_control_name_owner_changed_cb (DBusGProxy *proxy, const gchar *name, const gchar *prev, const gchar *new, PkControl *control)
{
	guint new_len;
	guint prev_len;

	g_return_if_fail (PK_IS_CONTROL (control));

	if (control->priv->proxy_dbus == NULL)
		return;

	/* not us */
	if (g_strcmp0 (name, PK_DBUS_SERVICE) != 0)
		return;

	/* ITS4: ignore, not used for allocation */
	new_len = strlen (new);
	/* ITS4: ignore, not used for allocation */
	prev_len = strlen (prev);

	/* something --> nothing */
	if (prev_len != 0 && new_len == 0) {
		control->priv->connected = FALSE;
		egg_debug ("notify::connected");
		g_object_notify (G_OBJECT(control), "connected");
		return;
	}

	/* nothing --> something */
	if (prev_len == 0 && new_len != 0) {
		control->priv->connected = TRUE;
		egg_debug ("notify::connected");
		g_object_notify (G_OBJECT(control), "connected");
		return;
	}
}

/**
 * pk_control_get_property:
 **/
static void
pk_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkControl *control = PK_CONTROL (object);
	PkControlPrivate *priv = control->priv;

	switch (prop_id) {
	case PROP_VERSION_MAJOR:
		g_value_set_uint (value, priv->version_major);
		break;
	case PROP_VERSION_MINOR:
		g_value_set_uint (value, priv->version_minor);
		break;
	case PROP_VERSION_MICRO:
		g_value_set_uint (value, priv->version_micro);
		break;
	case PROP_BACKEND_NAME:
		g_value_set_string (value, priv->backend_name);
		break;
	case PROP_BACKEND_DESCRIPTION:
		g_value_set_string (value, priv->backend_description);
		break;
	case PROP_BACKEND_AUTHOR:
		g_value_set_string (value, priv->backend_author);
		break;
	case PROP_ROLES:
		g_value_set_uint64 (value, priv->roles);
		break;
	case PROP_GROUPS:
		g_value_set_uint64 (value, priv->groups);
		break;
	case PROP_FILTERS:
		g_value_set_uint64 (value, priv->filters);
		break;
	case PROP_MIME_TYPES:
		g_value_set_string (value, priv->mime_types);
		break;
	case PROP_LOCKED:
		g_value_set_boolean (value, priv->locked);
		break;
	case PROP_NETWORK_STATE:
		g_value_set_uint (value, priv->network_state);
		break;
	case PROP_DISTRO_ID:
		g_value_set_string (value, priv->distro_id);
		break;
	case PROP_CONNECTED:
		g_value_set_boolean (value, priv->connected);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_control_set_property:
 **/
static void
pk_control_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_control_class_init:
 * @klass: The PkControlClass
 **/
static void
pk_control_class_init (PkControlClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_control_get_property;
	object_class->set_property = pk_control_set_property;
	object_class->finalize = pk_control_finalize;

	/**
	 * PkControl:version-major:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("version-major", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MAJOR, pspec);

	/**
	 * PkControl:version-minor:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("version-minor", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MINOR, pspec);

	/**
	 * PkControl:version-micro:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint ("version-micro", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MICRO, pspec);

	/**
	 * PkControl:backend-name:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_string ("backend-name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKEND_NAME, pspec);

	/**
	 * PkControl:backend-description:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_string ("backend-description", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKEND_DESCRIPTION, pspec);

	/**
	 * PkControl:backend-author:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_string ("backend-author", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKEND_AUTHOR, pspec);

	/**
	 * PkControl:roles:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint64 ("roles", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLES, pspec);

	/**
	 * PkControl:groups:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint64 ("groups", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_GROUPS, pspec);

	/**
	 * PkControl:filters:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_uint64 ("filters", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILTERS, pspec);

	/**
	 * PkControl:mime-types:
	 *
	 * Since: 0.5.2
	 */
	pspec = g_param_spec_string ("mime-types", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MIME_TYPES, pspec);

	/**
	 * PkControl:locked:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_boolean ("locked", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LOCKED, pspec);

	/**
	 * PkControl:network-state:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_uint ("network-state", NULL, NULL,
				   0, G_MAXUINT, PK_NETWORK_ENUM_LAST,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_NETWORK_STATE, pspec);

	/**
	 * PkControl:distro-id:
	 *
	 * Since: 0.5.5
	 */
	pspec = g_param_spec_string ("distro-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DISTRO_ID, pspec);

	/**
	 * PkControl:connected:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_boolean ("connected", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CONNECTED, pspec);

	/**
	 * PkControl::updates-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::updates-changed signal is emitted when the update list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals[SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::repo-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::repo-list-changed signal is emitted when the repo list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals[SIGNAL_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, repo_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::restart-schedule:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::restart_schedule signal is emitted when the packagekitd service
	 * has been restarted because it has been upgraded.
	 * Client programs should reload themselves when it is convenient to
	 * do so, as old client tools may not be compatable with the new daemon.
	 **/
	signals[SIGNAL_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, restart_schedule),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::transaction-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 * @transaction_ids: an #GStrv array of transaction ID's
	 *
	 * The ::transaction-list-changed signal is emitted when the list
	 * of transactions handled by the daemon is changed.
	 **/
	signals[SIGNAL_TRANSACTION_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, transaction_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	g_type_class_add_private (klass, sizeof (PkControlPrivate));
}

/**
 * pk_control_init:
 * @control: This class instance
 **/
static void
pk_control_init (PkControl *control)
{
	GError *error = NULL;

	control->priv = PK_CONTROL_GET_PRIVATE (control);
	control->priv->mime_types = NULL;
	control->priv->backend_name = NULL;
	control->priv->backend_description = NULL;
	control->priv->backend_author = NULL;
	control->priv->locked = FALSE;
	control->priv->connected = FALSE;
	control->priv->transaction_list_changed_id = 0;
	control->priv->restart_schedule_id = 0;
	control->priv->updates_changed_id = 0;
	control->priv->repo_list_changed_id = 0;
	control->priv->network_state = PK_NETWORK_ENUM_UNKNOWN;
	control->priv->distro_id = NULL;
	control->priv->calls = g_ptr_array_new ();

	/* check dbus connections, exit if not valid */
	control->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* we maintain a local copy */
	control->priv->version_major = G_MAXUINT;
	control->priv->version_minor = G_MAXUINT;
	control->priv->version_micro = G_MAXUINT;

	/* get a connection to the main interface */
	control->priv->proxy = dbus_g_proxy_new_for_name (control->priv->connection,
							  PK_DBUS_SERVICE, PK_DBUS_PATH,
							  PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL)
		egg_error ("Cannot connect to PackageKit.");

	/* get a connection to collect properties */
	control->priv->proxy_props = dbus_g_proxy_new_for_name (control->priv->connection,
								PK_DBUS_SERVICE, PK_DBUS_PATH,
								"org.freedesktop.DBus.Properties");
	if (control->priv->proxy_props == NULL)
		egg_error ("Cannot connect to PackageKit.");

	/* get a connection to watch NameOwnerChanged */
	control->priv->proxy_dbus = dbus_g_proxy_new_for_name_owner (control->priv->connection,
								     DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
								     DBUS_INTERFACE_DBUS, &error);
	if (control->priv->proxy_dbus == NULL) {
		egg_error ("Cannot connect to DBUS: %s", error->message);
		g_error_free (error);
	}

	/* connect to NameOwnerChanged */
	dbus_g_proxy_add_signal (control->priv->proxy_dbus, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy_dbus, "NameOwnerChanged",
				     G_CALLBACK (pk_control_name_owner_changed_cb),
				     control, NULL);

	/* timeout after a few ms, all all these methods should not take long */
	dbus_g_proxy_set_default_timeout (control->priv->proxy, PK_CONTROL_DBUS_METHOD_TIMEOUT);
	dbus_g_proxy_set_default_timeout (control->priv->proxy_props, PK_CONTROL_DBUS_METHOD_TIMEOUT);
	dbus_g_proxy_set_default_timeout (control->priv->proxy_dbus, PK_CONTROL_DBUS_METHOD_TIMEOUT);

	dbus_g_proxy_add_signal (control->priv->proxy, "TransactionListChanged",
				 G_TYPE_STRV, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "TransactionListChanged",
				     G_CALLBACK(pk_control_transaction_list_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "UpdatesChanged", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "UpdatesChanged",
				     G_CALLBACK (pk_control_updates_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RepoListChanged", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RepoListChanged",
				     G_CALLBACK (pk_control_repo_list_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "Changed", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "Changed",
				     G_CALLBACK (pk_control_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RestartSchedule", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RestartSchedule",
				     G_CALLBACK (pk_control_restart_schedule_cb), control, NULL);
}

/**
 * pk_control_finalize:
 * @object: The object to finalize
 **/
static void
pk_control_finalize (GObject *object)
{
	PkControl *control = PK_CONTROL (object);
	PkControlPrivate *priv = control->priv;

	/* ensure we cancel any in-flight DBus calls */
	pk_control_cancel_all_dbus_methods (control);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "TransactionListChanged",
				        G_CALLBACK (pk_control_transaction_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_control_updates_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RepoListChanged",
				        G_CALLBACK (pk_control_repo_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "Changed",
				        G_CALLBACK (pk_control_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RestartSchedule",
				        G_CALLBACK (pk_control_restart_schedule_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy_dbus, "NameOwnerChanged",
				        G_CALLBACK (pk_control_name_owner_changed_cb), control);

	/* remove pending sources */
	if (control->priv->transaction_list_changed_id != 0)
		g_source_remove (control->priv->transaction_list_changed_id);
	if (control->priv->restart_schedule_id != 0)
		g_source_remove (control->priv->restart_schedule_id);
	if (control->priv->updates_changed_id != 0)
		g_source_remove (control->priv->updates_changed_id);
	if (control->priv->repo_list_changed_id != 0)
		g_source_remove (control->priv->repo_list_changed_id);

	g_free (priv->backend_name);
	g_free (priv->backend_description);
	g_free (priv->backend_author);
	g_free (priv->mime_types);
	g_free (priv->distro_id);
	g_object_unref (G_OBJECT (priv->proxy));
	g_object_unref (G_OBJECT (priv->proxy_props));
	g_object_unref (G_OBJECT (priv->proxy_dbus));
	g_ptr_array_unref (control->priv->calls);

	G_OBJECT_CLASS (pk_control_parent_class)->finalize (object);
}

/**
 * pk_control_new:
 *
 * Return value: a new PkControl object.
 *
 * Since: 0.5.2
 **/
PkControl *
pk_control_new (void)
{
	if (pk_control_object != NULL) {
		g_object_ref (pk_control_object);
	} else {
		pk_control_object = g_object_new (PK_TYPE_CONTROL, NULL);
		g_object_add_weak_pointer (pk_control_object, &pk_control_object);
	}
	return PK_CONTROL (pk_control_object);
}
