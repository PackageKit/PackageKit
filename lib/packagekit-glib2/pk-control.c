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

#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-version.h>

#include "egg-debug.h"

static void     pk_control_finalize	(GObject     *object);

#define PK_CONTROL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONTROL, PkControlPrivate))

/**
 * PkControlPrivate:
 *
 * Private #PkControl data
 **/
struct _PkControlPrivate
{
	DBusGProxyCall		*call;
	GPtrArray		*calls;
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	gboolean		 version_major;
	gboolean		 version_minor;
	gboolean		 version_micro;
};

enum {
	SIGNAL_LOCKED,
	SIGNAL_LIST_CHANGED,
	SIGNAL_RESTART_SCHEDULE,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_REPO_LIST_CHANGED,
	SIGNAL_NETWORK_STATE_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_VERSION_MAJOR,
	PROP_VERSION_MINOR,
	PROP_VERSION_MICRO,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };
static gpointer pk_control_object = NULL;

G_DEFINE_TYPE (PkControl, pk_control, G_TYPE_OBJECT)

typedef struct {
	gboolean		 ret;
	gchar			**mime_types;
	gchar			*tid;
	gchar			**transaction_list;
	guint			 time;
	DBusGProxyCall		*call;
	GCancellable		*cancellable;
	GSimpleAsyncResult	*res;
	PkAuthorizeEnum		 authorize;
	PkBitfield		*bitfield;
	PkControl		*control;
	PkNetworkEnum		 network;
} PkControlState;

/**
 * pk_control_error_quark:
 *
 * We are a GObject that sets errors
 *
 * Return value: Our personal error quark.
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

/***************************************************************************************************/

/**
 * pk_control_get_tid_state_finish:
 **/
static void
pk_control_get_tid_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->tid != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdup (state->tid), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_free (state->tid);
	g_object_unref (state->res);
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

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &tid,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_tid_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save results */
	state->tid = g_strdup (tid);

	/* we're done */
	pk_control_get_tid_state_finish (state, error);
out:
	g_free (tid);
}

/**
 * pk_control_get_tid_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets a transacton ID from the daemon.
 **/
void
pk_control_get_tid_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_tid_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus method async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTid",
					       (DBusGProxyCallNotify) pk_control_get_tid_cb, state,
					       NULL, G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

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
 * Return value: the ID, or %NULL if unset
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

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/

/**
 * pk_control_get_mime_types_state_finish:
 **/
static void
pk_control_get_mime_types_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->mime_types != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdupv (state->mime_types), (GDestroyNotify) g_strfreev);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_strfreev (state->mime_types);
	g_object_unref (state->res);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_mime_types_cb:
 **/
static void
pk_control_get_mime_types_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *temp = NULL;
	gboolean ret;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &temp,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_mime_types_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	state->mime_types = g_strsplit (temp, ";", -1);

	/* we're done */
	pk_control_get_mime_types_state_finish (state, error);
out:
	g_free (temp);
}

/**
 * pk_control_get_mime_types_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * The MIME list is the supported package formats.
 **/
void
pk_control_get_mime_types_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_mime_types_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus get_mime_types async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetMimeTypes",
					       (DBusGProxyCallNotify) pk_control_get_mime_types_cb, state,
					       NULL, G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

	g_object_unref (res);
}

/**
 * pk_control_get_mime_types_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: an GStrv list of the formats the backend supports,
 * or %NULL if unknown
 **/
gchar **
pk_control_get_mime_types_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_mime_types_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/

/**
 * pk_control_set_proxy_state_finish:
 **/
static void
pk_control_set_proxy_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->mime_types != NULL) {
		g_simple_async_result_set_op_res_gboolean (state->res, state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_object_unref (state->res);
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

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to set proxy: %s", error->message);
		pk_control_set_proxy_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	state->ret = TRUE;

	/* we're done */
	pk_control_set_proxy_state_finish (state, error);
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
 **/
void
pk_control_set_proxy_async (PkControl *control, const gchar *proxy_http, const gchar *proxy_ftp, GCancellable *cancellable,
			    GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_set_proxy_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus set_proxy async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "SetProxy",
					       (DBusGProxyCallNotify) pk_control_set_proxy_cb, state, NULL,
					       G_TYPE_STRING, proxy_http,
					       G_TYPE_STRING, proxy_ftp,
					       G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

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
 * pk_control_bitfield_copy:
 **/
static PkBitfield *
pk_control_bitfield_copy (PkBitfield *value)
{
	PkBitfield *new;
	new = g_new0 (PkBitfield, 1);
	*new = *value;
	return new;
}

/**
 * pk_control_get_roles_state_finish:
 **/
static void
pk_control_get_roles_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->bitfield != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, pk_control_bitfield_copy (state->bitfield), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_free (state->bitfield);
	g_object_unref (state->res);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_roles_cb:
 **/
static void
pk_control_get_roles_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *roles = NULL;
	gboolean ret;
	PkBitfield bitfield;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &roles,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_roles_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	bitfield = pk_role_bitfield_from_text (roles);
	state->bitfield = pk_control_bitfield_copy (&bitfield);

	/* we're done */
	pk_control_get_roles_state_finish (state, error);
out:
	g_free (roles);
}

/**
 * pk_control_get_roles_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Get what methods the daemon can do with the current backend.
 **/
void
pk_control_get_roles_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_roles_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus get_roles async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetActions", /* not GetRoles, just get over it... */
					       (DBusGProxyCallNotify) pk_control_get_roles_cb, state,
					       NULL, G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

	g_object_unref (res);
}

/**
 * pk_control_get_roles_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: an enumerated list of the actions the backend supports, free with g_free()
 **/
PkBitfield *
pk_control_get_roles_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_roles_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/
/**
 * pk_control_get_filters_state_finish:
 **/
static void
pk_control_get_filters_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->bitfield != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, pk_control_bitfield_copy (state->bitfield), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_free (state->bitfield);
	g_object_unref (state->res);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_filters_cb:
 **/
static void
pk_control_get_filters_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *filters = NULL;
	gboolean ret;
	PkBitfield bitfield;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &filters,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_filters_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	bitfield = pk_filter_bitfield_from_text (filters);
	state->bitfield = pk_control_bitfield_copy (&bitfield);

	/* we're done */
	pk_control_get_filters_state_finish (state, error);
out:
	g_free (filters);
}

/**
 * pk_control_get_filters_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Filters are how the backend can specify what type of package is returned.
 **/
void
pk_control_get_filters_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_filters_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus get_filters async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetFilters",
					       (DBusGProxyCallNotify) pk_control_get_filters_cb, state,
					       NULL, G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

	g_object_unref (res);
}

/**
 * pk_control_get_filters_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: an enumerated list of the filters the backend supports, free with g_free()
 **/
PkBitfield *
pk_control_get_filters_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_filters_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/
/**
 * pk_control_get_groups_state_finish:
 **/
static void
pk_control_get_groups_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->bitfield != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, pk_control_bitfield_copy (state->bitfield), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_free (state->bitfield);
	g_object_unref (state->res);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_groups_cb:
 **/
static void
pk_control_get_groups_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gchar *groups = NULL;
	gboolean ret;
	PkBitfield bitfield;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &groups,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_groups_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	bitfield = pk_group_bitfield_from_text (groups);
	state->bitfield = pk_control_bitfield_copy (&bitfield);

	/* we're done */
	pk_control_get_groups_state_finish (state, error);
out:
	g_free (groups);
}

/**
 * pk_control_get_groups_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * The group list is enumerated so it can be localised and have deep
 * integration with desktops.
 * This method allows a frontend to only display the groups that are supported.
 **/
void
pk_control_get_groups_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_groups_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus get_groups async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetGroups",
					       (DBusGProxyCallNotify) pk_control_get_groups_cb, state,
					       NULL, G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

	g_object_unref (res);
}

/**
 * pk_control_get_groups_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: an enumerated list of the groups the backend supports, free with g_free()
 **/
PkBitfield *
pk_control_get_groups_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_groups_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/

/**
 * pk_control_get_transaction_list_state_finish:
 **/
static void
pk_control_get_transaction_list_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->transaction_list != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdupv (state->transaction_list), (GDestroyNotify) g_strfreev);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_strfreev (state->transaction_list);
	g_object_unref (state->res);
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

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRV, &temp,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_transaction_list_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	state->transaction_list = g_strdupv (temp);

	/* we're done */
	pk_control_get_transaction_list_state_finish (state, error);
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
 **/
void
pk_control_get_transaction_list_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_transaction_list_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus get_transaction_list async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTransactionList",
					       (DBusGProxyCallNotify) pk_control_get_transaction_list_cb, state,
					       NULL, G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

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
 * Return value: A GStrv list of transaction ID's
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

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/

/**
 * pk_control_get_time_since_action_state_finish:
 **/
static void
pk_control_get_time_since_action_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->time != 0) {
		g_simple_async_result_set_op_res_gssize (state->res, state->time);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_object_unref (state->res);
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

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_UINT, &seconds,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_time_since_action_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	state->time = seconds;
	if (state->time == 0) {
		error = g_error_new (PK_CONTROL_ERROR, PK_CONTROL_ERROR_FAILED, "could not get time");
		pk_control_get_time_since_action_state_finish (state, error);
		goto out;
	}

	/* we're done */
	pk_control_get_time_since_action_state_finish (state, error);
out:
	return;
}

/**
 * pk_control_get_time_since_action_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * We may want to know how long it has been since we refreshed the cache or
 * retrieved the update list.
 **/
void
pk_control_get_time_since_action_async (PkControl *control, PkRoleEnum role, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	const gchar *role_text;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_time_since_action_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus get_time_since_action async */
	role_text = pk_role_enum_to_text (role);
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTimeSinceAction",
					       (DBusGProxyCallNotify) pk_control_get_time_since_action_cb, state, NULL,
					       G_TYPE_STRING, role_text,
					       G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

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
 * pk_control_get_network_state_state_finish:
 **/
static void
pk_control_get_network_state_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->network != PK_NETWORK_ENUM_UNKNOWN) {
		g_simple_async_result_set_op_res_gssize (state->res, state->network);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_object_unref (state->res);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_network_state_cb:
 **/
static void
pk_control_get_network_state_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
	GError *error = NULL;
	gboolean ret;
	gchar *network_state = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &network_state,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_get_network_state_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	state->network = pk_network_enum_from_text (network_state);
	if (state->network == PK_NETWORK_ENUM_UNKNOWN) {
		error = g_error_new (PK_CONTROL_ERROR, PK_CONTROL_ERROR_FAILED, "could not get state");
		pk_control_get_network_state_state_finish (state, error);
		goto out;
	}

	/* we're done */
	pk_control_get_network_state_state_finish (state, error);
out:
	g_free (network_state);
	return;
}

/**
 * pk_control_get_network_state_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets the network state.
 **/
void
pk_control_get_network_state_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_network_state_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->network = PK_NETWORK_ENUM_UNKNOWN;
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetNetworkState",
					       (DBusGProxyCallNotify) pk_control_get_network_state_cb, state, NULL,
					       G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

	g_object_unref (res);
}

/**
 * pk_control_get_network_state_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: an enumerated network state
 **/
PkNetworkEnum
pk_control_get_network_state_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_NETWORK_ENUM_UNKNOWN);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), PK_NETWORK_ENUM_UNKNOWN);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_network_state_async, PK_NETWORK_ENUM_UNKNOWN);

	if (g_simple_async_result_propagate_error (simple, error))
		return PK_NETWORK_ENUM_UNKNOWN;

	return (PkNetworkEnum) g_simple_async_result_get_op_res_gssize (simple);
}

/***************************************************************************************************/

/**
 * pk_control_can_authorize_state_finish:
 **/
static void
pk_control_can_authorize_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->authorize != PK_AUTHORIZE_ENUM_UNKNOWN) {
		g_simple_async_result_set_op_res_gssize (state->res, state->authorize);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_object_unref (state->res);
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

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &authorize_state,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed: %s", error->message);
		pk_control_can_authorize_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* save data */
	state->authorize = pk_authorize_type_enum_from_text (authorize_state);
	if (state->authorize == PK_AUTHORIZE_ENUM_UNKNOWN) {
		error = g_error_new (PK_CONTROL_ERROR, PK_CONTROL_ERROR_FAILED, "could not get state");
		pk_control_can_authorize_state_finish (state, error);
		goto out;
	}

	/* we're done */
	pk_control_can_authorize_state_finish (state, error);
out:
	g_free (authorize_state);
	return;
}

/**
 * pk_control_can_authorize_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * We may want to know before we run a method if we are going to be denied,
 * accepted or challenged for authentication.
 **/
void
pk_control_can_authorize_async (PkControl *control, const gchar *action_id, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_can_authorize_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);
	state->control = control;
	state->authorize = PK_AUTHORIZE_ENUM_UNKNOWN;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "CanAuthorize",
					       (DBusGProxyCallNotify) pk_control_can_authorize_cb, state, NULL,
					       G_TYPE_STRING, action_id,
					       G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);

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
 * pk_control_get_daemon_state:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The engine state debugging output
 *
 * Return value: a string of debugging data of unspecified format, unref wih g_free()
 **/
gchar *
pk_control_get_daemon_state (PkControl *control, GError **error)
{
	gboolean ret;
	gchar *state = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* call D-Bus sync */
	ret = dbus_g_proxy_call (control->priv->proxy, "GetDaemonState", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &state,
				 G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		if (error != NULL)
			pk_control_fixup_dbus_error (*error);
		goto out;
	}
out:
	return state;
}

/**
 * pk_control_get_backend_detail:
 * @control: a valid #PkControl instance
 * @name: the name of the backend
 * @author: the author of the backend
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The backend detail is useful for the pk-backend-status program, or for
 * automatic bugreports.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_control_get_backend_detail (PkControl *control, gchar **name, gchar **author, GError **error)
{
	gboolean ret;
	gchar *tname;
	gchar *tauthor;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* call D-Bus sync */
	ret = dbus_g_proxy_call (control->priv->proxy, "GetBackendDetail", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tname,
				 G_TYPE_STRING, &tauthor,
				 G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		if (error != NULL)
			pk_control_fixup_dbus_error (*error);
		goto out;
	}

	/* copy needed bits */
	if (name != NULL)
		*name = tname;
	else
		g_free (tname);
	/* copy needed bits */
	if (author != NULL)
		*author = tauthor;
	else
		g_free (tauthor);
out:
	return ret;
}

/**
 * pk_control_transaction_list_changed_cb:
 */
static void
pk_control_transaction_list_changed_cb (DBusGProxy *proxy, gchar **array, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emit transaction-list-changed");
	g_signal_emit (control, signals [SIGNAL_LIST_CHANGED], 0);
}

/**
 * pk_control_restart_schedule_cb:
 */
static void
pk_control_restart_schedule_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emitting restart-schedule");
	g_signal_emit (control, signals [SIGNAL_RESTART_SCHEDULE], 0);

}

/**
 * pk_control_updates_changed_cb:
 */
static void
pk_control_updates_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emitting updates-changed");
	g_signal_emit (control, signals [SIGNAL_UPDATES_CHANGED], 0);

}

/**
 * pk_control_repo_list_changed_cb:
 */
static void
pk_control_repo_list_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emitting repo-list-changed");
	g_signal_emit (control, signals [SIGNAL_REPO_LIST_CHANGED], 0);
}

/**
 * pk_control_network_state_changed_cb:
 */
static void
pk_control_network_state_changed_cb (DBusGProxy *proxy, const gchar *network_text, PkControl *control)
{
	PkNetworkEnum network;
	g_return_if_fail (PK_IS_CONTROL (control));

	network = pk_network_enum_from_text (network_text);
	egg_debug ("emitting network-state-changed: %s", network_text);
	g_signal_emit (control, signals [SIGNAL_NETWORK_STATE_CHANGED], 0, network);
}

/**
 * pk_control_locked_cb:
 */
static void
pk_control_locked_cb (DBusGProxy *proxy, gboolean is_locked, PkControl *control)
{
	egg_debug ("emit locked %i", is_locked);
	g_signal_emit (control , signals [SIGNAL_LOCKED], 0, is_locked);
}

/**
 * pk_control_set_properties_collect_cb:
 **/
static void
pk_control_set_properties_collect_cb (const char *key, const GValue *value, PkControl *control)
{
	if (g_strcmp0 (key, "version-major") == 0)
		control->priv->version_major = g_value_get_uint (value);
	else if (g_strcmp0 (key, "version-minor") == 0)
		control->priv->version_minor = g_value_get_uint (value);
	else if (g_strcmp0 (key, "version-micro") == 0)
		control->priv->version_micro = g_value_get_uint (value);
	else
		egg_warning ("unhandled property '%s'", key);
}

/**
 * pk_control_set_properties_cb:
 **/
static void
pk_control_set_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControl *control)
{
	GError *error = NULL;
	gboolean ret;
	GHashTable *hash;

	/* finished call */
	control->priv->call = NULL;

	/* we've sent this async */
	egg_debug ("got reply to request");

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
				     &hash,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		egg_warning ("failed to get properties: %s", error->message);
		return;
	}

	/* process results */
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_control_set_properties_collect_cb, control);
		g_hash_table_unref (hash);
	}
	g_object_unref (proxy);
}

/**
 * pk_control_set_properties:
 **/
static void
pk_control_set_properties (PkControl *control)
{
	DBusGProxy *proxy;

	/* connect to the correct path for properties */
	proxy = dbus_g_proxy_new_for_name (control->priv->connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.DBus.Properties");
	if (proxy == NULL) {
		egg_warning ("Couldn't connect to proxy");
		return;
	}

	/* does an async call, so properties may not be set until some time after the object is setup */
	control->priv->call = dbus_g_proxy_begin_call (proxy, "GetAll",
						       (DBusGProxyCallNotify) pk_control_set_properties_cb, control, NULL,
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
		egg_debug ("cancel in flight call: %p", state->call);
		dbus_g_proxy_cancel_call (control->priv->proxy, state->call);
	}

	return TRUE;
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
	 */
	pspec = g_param_spec_uint ("version-major", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MAJOR, pspec);

	/**
	 * PkControl:version-minor:
	 */
	pspec = g_param_spec_uint ("version-minor", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MINOR, pspec);

	/**
	 * PkControl:version-micro:
	 */
	pspec = g_param_spec_uint ("version-micro", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MICRO, pspec);

	/**
	 * PkControl::updates-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::updates-changed signal is emitted when the update list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals [SIGNAL_UPDATES_CHANGED] =
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
	signals [SIGNAL_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, repo_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::network-state-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::network-state-changed signal is emitted when the network has changed speed or
	 * connections state.
	 **/
	signals [SIGNAL_NETWORK_STATE_CHANGED] =
		g_signal_new ("network-state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, network_state_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	/**
	 * PkControl::restart-schedule:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::restart_schedule signal is emitted when the packagekitd service
	 * has been restarted because it has been upgraded.
	 * Client programs should reload themselves when it is convenient to
	 * do so, as old client tools may not be compatable with the new daemon.
	 **/
	signals [SIGNAL_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, restart_schedule),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::transaction-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::transaction-list-changed signal is emitted when the list
	 * of transactions handled by the daemon is changed.
	 **/
	signals [SIGNAL_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, transaction_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::locked:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::locked signal is emitted when the backend instance has been
	 * locked by PackageKit.
	 * This may mean that other native package tools will not work.
	 **/
	signals [SIGNAL_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, locked),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

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
	control->priv->call = NULL;
	control->priv->calls = g_ptr_array_new ();

	/* check dbus connections, exit if not valid */
	control->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* we maintain a local copy */
	control->priv->version_major = 0;
	control->priv->version_minor = 0;
	control->priv->version_micro = 0;

	/* get a connection to the engine object */
	control->priv->proxy = dbus_g_proxy_new_for_name (control->priv->connection,
							  PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL)
		egg_error ("Cannot connect to PackageKit.");

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (control->priv->proxy, INT_MAX);

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

	dbus_g_proxy_add_signal (control->priv->proxy, "NetworkStateChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "NetworkStateChanged",
				     G_CALLBACK (pk_control_network_state_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RestartSchedule", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RestartSchedule",
				     G_CALLBACK (pk_control_restart_schedule_cb), control, NULL);

	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (control->priv->proxy, "Locked", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "Locked",
				     G_CALLBACK (pk_control_locked_cb), control, NULL);

	/* get properties async if they exist */
if (0)	pk_control_set_properties (control);
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

	/* if we have a request in flight, cancel it */
	if (control->priv->call != NULL) {
		egg_warning ("cancel in flight call");
		dbus_g_proxy_cancel_call (control->priv->proxy, control->priv->call);
	}

	/* ensure we cancel any in-flight DBus calls */
	pk_control_cancel_all_dbus_methods (control);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "Locked",
				        G_CALLBACK (pk_control_locked_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "TransactionListChanged",
				        G_CALLBACK (pk_control_transaction_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_control_updates_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RepoListChanged",
				        G_CALLBACK (pk_control_repo_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "NetworkStateChanged",
				        G_CALLBACK (pk_control_network_state_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RestartSchedule",
				        G_CALLBACK (pk_control_restart_schedule_cb), control);

	g_object_unref (G_OBJECT (priv->proxy));
	g_ptr_array_unref (control->priv->calls);

	G_OBJECT_CLASS (pk_control_parent_class)->finalize (object);
}

/**
 * pk_control_new:
 *
 * Return value: a new PkControl object.
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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_control_test_get_tid_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	const gchar *tid;

	/* get the result */
	tid = pk_control_get_tid_finish (control, res, &error);
	if (tid == NULL) {
		egg_test_failed (test, "failed to get transaction: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_debug ("tid = %s", tid);
	egg_test_loop_quit (test);
}

static void
pk_control_test_get_mime_types_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gchar **types;
	guint len;

	/* get the result */
	types = pk_control_get_mime_types_finish (control, res, &error);
	if (types == NULL) {
		egg_test_failed (test, "failed to get mime types: %s", error->message);
		g_error_free (error);
		return;
	}

	/* check size */
	len = g_strv_length (types);
	if (len != 2) {
		egg_test_failed (test, "length incorrect: %i", len);
		return;
	}

	/* check value */
	if (g_strcmp0 (types[0], "application/x-rpm") != 0) {
		egg_test_failed (test, "data incorrect: %s", types[0]);
		return;
	}

	egg_test_loop_quit (test);
}

static void
pk_control_test_get_roles_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	PkBitfield *roles;
	guint len;
	gchar *text;

	/* get the result */
	roles = pk_control_get_roles_finish (control, res, &error);
	if (roles == NULL) {
		egg_test_failed (test, "failed to get roles: %s", error->message);
		g_error_free (error);
		return;
	}

	/* check value */
	text = pk_role_bitfield_to_text (*roles);
	if (g_strcmp0 (text, "cancel;get-depends;get-details;get-files;get-packages;get-repo-list;"
			     "get-requires;get-update-detail;get-updates;install-files;install-packages;"
			     "refresh-cache;remove-packages;repo-enable;repo-set-data;resolve;rollback;"
			     "search-details;search-file;search-group;search-name;update-packages;update-system;"
			     "what-provides;download-packages;get-distro-upgrades;simulate-install-packages;"
			     "simulate-remove-packages;simulate-update-packages") != 0) {
		egg_test_failed (test, "data incorrect: %s", text);
		return;
	}

	g_free (text);
	egg_test_loop_quit (test);
}

static void
pk_control_test_get_filters_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	PkBitfield *filters;
	guint len;
	gchar *text;

	/* get the result */
	filters = pk_control_get_filters_finish (control, res, &error);
	if (filters == NULL) {
		egg_test_failed (test, "failed to get filters: %s", error->message);
		g_error_free (error);
		return;
	}

	/* check value */
	text = pk_filter_bitfield_to_text (*filters);
	if (g_strcmp0 (text, "installed;devel;gui") != 0) {
		egg_test_failed (test, "data incorrect: %s", text);
		return;
	}

	g_free (text);
	egg_test_loop_quit (test);
}

static void
pk_control_test_get_groups_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	PkBitfield *groups;
	guint len;
	gchar *text;

	/* get the result */
	groups = pk_control_get_groups_finish (control, res, &error);
	if (groups == NULL) {
		egg_test_failed (test, "failed to get groups: %s", error->message);
		g_error_free (error);
		return;
	}

	/* check value */
	text = pk_group_bitfield_to_text (*groups);
	if (g_strcmp0 (text, "accessibility;games;system") != 0) {
		egg_test_failed (test, "data incorrect: %s", text);
		return;
	}

	g_free (text);
	egg_test_loop_quit (test);
}

static void
pk_control_test_get_time_since_action_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	guint seconds;

	/* get the result */
	seconds = pk_control_get_time_since_action_finish (control, res, &error);
	if (seconds == 0) {
		egg_test_failed (test, "failed to get time: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_test_loop_quit (test);
}

static void
pk_control_test_get_network_state_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	PkNetworkEnum network;

	/* get the result */
	network = pk_control_get_network_state_finish (control, res, &error);
	if (network == PK_NETWORK_ENUM_UNKNOWN) {
		egg_test_failed (test, "failed to get network state: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_test_loop_quit (test);
}

static void
pk_control_test_can_authorize_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	PkAuthorizeEnum auth;

	/* get the result */
	auth = pk_control_can_authorize_finish (control, res, &error);
	if (auth == PK_AUTHORIZE_ENUM_UNKNOWN) {
		egg_test_failed (test, "failed to get auth: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_test_loop_quit (test);
}

void
pk_control_test (EggTest *test)
{
	PkControl *control;
	guint version;

	if (!egg_test_start (test, "PkControl"))
		return;

	/************************************************************/
	egg_test_title (test, "get control");
	control = pk_control_new ();
	egg_test_assert (test, control != NULL);

	/************************************************************/
	egg_test_title (test, "get TID async");
	pk_control_get_tid_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_tid_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got tid in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get mime-types async");
	pk_control_get_mime_types_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_mime_types_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got mime types in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get roles async");
	pk_control_get_roles_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_roles_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got roles in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get filters async");
	pk_control_get_filters_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_filters_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got filters in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get groups async");
	pk_control_get_groups_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_groups_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got groups in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get time since async");
	pk_control_get_time_since_action_async (control, PK_ROLE_ENUM_GET_UPDATES, NULL, (GAsyncReadyCallback) pk_control_test_get_time_since_action_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got get time since in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get network state async");
	pk_control_get_network_state_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_network_state_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "get network state in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "get auth state async");
	pk_control_can_authorize_async (control, "org.freedesktop.packagekit.system-update", NULL,
					(GAsyncReadyCallback) pk_control_test_can_authorize_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "get auth state in %i", egg_test_elapsed (test));

#if 0
	/************************************************************/
	egg_test_title (test, "version major");
	g_object_get (control, "version-major", &version, NULL);
	egg_test_assert (test, (version == PK_MAJOR_VERSION));

	/************************************************************/
	egg_test_title (test, "version minor");
	g_object_get (control, "version-minor", &version, NULL);
	egg_test_assert (test, (version == PK_MINOR_VERSION));

	/************************************************************/
	egg_test_title (test, "version micro");
	g_object_get (control, "version-micro", &version, NULL);
	egg_test_assert (test, (version == PK_MICRO_VERSION));
#endif

	g_object_unref (control);
out:
	egg_test_end (test);
}
#endif

