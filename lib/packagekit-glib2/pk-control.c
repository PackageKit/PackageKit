/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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
#include <gio/gio.h>

#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-version.h>

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
	GCancellable		*cancellable;
	GPtrArray		*calls;
	GDBusProxy		*proxy;
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
	guint			 watch_id;
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
	GCancellable		*call;
	GCancellable		*cancellable;
	GSimpleAsyncResult	*res;
	PkAuthorizeEnum		 authorize;
	PkControl		*control;
	PkNetworkEnum		 network;
	GVariant		*parameters;
	GDBusProxy		*proxy;
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
	if (error->code == 0)
		error->code = PK_CONTROL_ERROR_CANNOT_START_DAEMON;
	else
		error->code = PK_CONTROL_ERROR_FAILED;
}

/**
 * pk_control_set_property_value:
 **/
static void
pk_control_set_property_value (PkControl *control,
			       const gchar *key,
			       GVariant *value)
{
	const gchar *tmp_str;
	gboolean tmp_bool;
	guint tmp_uint;
	PkBitfield tmp_bitfield;

	if (g_strcmp0 (key, "VersionMajor") == 0) {
		tmp_uint = g_variant_get_uint32 (value);
		if (control->priv->version_major == tmp_uint)
			return;
		control->priv->version_major = tmp_uint;
		g_object_notify (G_OBJECT(control), "version-major");
		return;
	}
	if (g_strcmp0 (key, "VersionMinor") == 0) {
		tmp_uint = g_variant_get_uint32 (value);
		if (control->priv->version_minor == tmp_uint)
			return;
		control->priv->version_minor = tmp_uint;
		g_object_notify (G_OBJECT(control), "version-minor");
		return;
	}
	if (g_strcmp0 (key, "VersionMicro") == 0) {
		tmp_uint = g_variant_get_uint32 (value);
		if (control->priv->version_micro == tmp_uint)
			return;
		control->priv->version_micro = tmp_uint;
		g_object_notify (G_OBJECT(control), "version-micro");
		return;
	}
	if (g_strcmp0 (key, "BackendName") == 0) {
		tmp_str = g_variant_get_string (value, NULL);
		if (g_strcmp0 (control->priv->backend_name, tmp_str) == 0)
			return;
		g_free (control->priv->backend_name);
		control->priv->backend_name = g_strdup (tmp_str);
		g_object_notify (G_OBJECT(control), "backend-name");
		return;
	}
	if (g_strcmp0 (key, "BackendDescription") == 0) {
		tmp_str = g_variant_get_string (value, NULL);
		if (g_strcmp0 (control->priv->backend_description, tmp_str) == 0)
			return;
		g_free (control->priv->backend_description);
		control->priv->backend_description = g_strdup (tmp_str);
		g_object_notify (G_OBJECT(control), "backend-description");
		return;
	}
	if (g_strcmp0 (key, "BackendAuthor") == 0) {
		tmp_str = g_variant_get_string (value, NULL);
		if (g_strcmp0 (control->priv->backend_author, tmp_str) == 0)
			return;
		g_free (control->priv->backend_author);
		control->priv->backend_author = g_strdup (tmp_str);
		g_object_notify (G_OBJECT(control), "backend-author");
		return;
	}
	if (g_strcmp0 (key, "MimeTypes") == 0) {
		tmp_str = g_variant_get_string (value, NULL);
		if (g_strcmp0 (control->priv->mime_types, tmp_str) == 0)
			return;
		g_free (control->priv->mime_types);
		control->priv->mime_types = g_strdup (tmp_str);
		g_object_notify (G_OBJECT(control), "mime-types");
		return;
	}
	if (g_strcmp0 (key, "Roles") == 0) {
		tmp_bitfield = g_variant_get_uint64 (value);
		if (control->priv->roles == tmp_bitfield)
			return;
		control->priv->roles = tmp_bitfield;
		g_object_notify (G_OBJECT(control), "roles");
		return;
	}
	if (g_strcmp0 (key, "Groups") == 0) {
		tmp_bitfield = g_variant_get_uint64 (value);
		if (control->priv->groups == tmp_bitfield)
			return;
		control->priv->groups = tmp_bitfield;
		g_object_notify (G_OBJECT(control), "groups");
		return;
	}
	if (g_strcmp0 (key, "Filters") == 0) {
		tmp_bitfield = g_variant_get_uint64 (value);
		if (control->priv->filters == tmp_bitfield)
			return;
		control->priv->filters = tmp_bitfield;
		g_object_notify (G_OBJECT(control), "filters");
		return;
	}
	if (g_strcmp0 (key, "Locked") == 0) {
		tmp_bool = g_variant_get_boolean (value);
		if (control->priv->locked == tmp_bool)
			return;
		control->priv->locked = tmp_bool;
		g_object_notify (G_OBJECT(control), "locked");
		return;
	}
	if (g_strcmp0 (key, "NetworkState") == 0) {
		tmp_uint = g_variant_get_uint32 (value);
		if (control->priv->network_state == tmp_uint)
			return;
		control->priv->network_state = tmp_uint;
		g_object_notify (G_OBJECT(control), "network-state");
		return;
	}
	if (g_strcmp0 (key, "DistroId") == 0) {
		tmp_str = g_variant_get_string (value, NULL);
		/* we don't want distro specific results in 'make check' */
		if (g_getenv ("PK_SELF_TEST") != NULL)
			tmp_str = "selftest;11.91;i686";
		if (g_strcmp0 (control->priv->distro_id, tmp_str) == 0)
			return;
		g_free (control->priv->distro_id);
		control->priv->distro_id = g_strdup (tmp_str);
		g_object_notify (G_OBJECT(control), "distro-id");
		return;
	}
	g_warning ("unhandled property '%s'", key);
}

/**
 * pk_control_properties_changed_cb:
 **/
static void
pk_control_properties_changed_cb (GDBusProxy *proxy,
				  GVariant *changed_properties,
				  const gchar* const  *invalidated_properties,
				  gpointer user_data)
{
	const gchar *key;
	GVariantIter *iter;
	GVariant *value;
	PkControl *control = PK_CONTROL (user_data);

	if (g_variant_n_children (changed_properties) > 0) {
		g_variant_get (changed_properties,
				"a{sv}",
				&iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value))
			pk_control_set_property_value (control, key, value);
		g_variant_iter_free (iter);
	}
}

/**
 * pk_control_signal_cb:
 **/
static void
pk_control_signal_cb (GDBusProxy *proxy,
		      const gchar *sender_name,
		      const gchar *signal_name,
		      GVariant *parameters,
		      gpointer user_data)
{
	const gchar **ids_tmp = NULL;
	gchar **ids = NULL;
	PkControl *control = PK_CONTROL (user_data);

	if (g_strcmp0 (signal_name, "TransactionListChanged") == 0) {
		g_variant_get (parameters, "(^a&s)", &ids_tmp);
		if (ids_tmp == NULL) {
			ids = g_new0 (gchar *, 1);
		} else {
			ids = g_strdupv ((gchar **) ids_tmp);
		}
		g_debug ("emit transaction-list-changed");
		g_signal_emit (control,
			       signals[SIGNAL_TRANSACTION_LIST_CHANGED], 0,
			       ids);
	}
	if (g_strcmp0 (signal_name, "UpdatesChanged") == 0) {
		g_debug ("emit updates-changed");
		g_signal_emit (control, signals[SIGNAL_UPDATES_CHANGED], 0);
		goto out;
	}
	if (g_strcmp0 (signal_name, "RepoListChanged") == 0) {
		g_debug ("emit repo-list-changed");
		g_signal_emit (control, signals[SIGNAL_REPO_LIST_CHANGED], 0);
		goto out;
	}
	if (g_strcmp0 (signal_name, "Changed") == 0) {
		/* we don't need to do anything here */
		goto out;
	}
	if (g_strcmp0 (signal_name, "RestartSchedule") == 0) {
		g_debug ("emit restart-schedule");
		g_signal_emit (control, signals[SIGNAL_RESTART_SCHEDULE], 0);
		goto out;
	}
out:
	g_free (ids_tmp);
	g_strfreev (ids);
}

/**
 * pk_control_proxy_connect:
 **/
static void
pk_control_proxy_connect (PkControlState *state)
{
	gchar **props = NULL;
	guint i;
	GVariant *value_tmp;

	/* coldplug properties */
	props = g_dbus_proxy_get_cached_property_names (state->proxy);
	for (i = 0; props != NULL && props[i] != NULL; i++) {
		value_tmp = g_dbus_proxy_get_cached_property (state->proxy,
							      props[i]);
		pk_control_set_property_value (state->control,
					       props[i],
					       value_tmp);
		g_variant_unref (value_tmp);
	}

	/* connect up signals */
	g_signal_connect (state->proxy, "g-properties-changed",
			  G_CALLBACK (pk_control_properties_changed_cb),
			  state->control);
	g_signal_connect (state->proxy, "g-signal",
			  G_CALLBACK (pk_control_signal_cb),
			  state->control);

	/* if we have no generic system wide proxy, then use this */
	if (state->control->priv->proxy == NULL)
		state->control->priv->proxy = g_object_ref (state->proxy);

	g_strfreev (props);
}

/**********************************************************************/

/**
 * pk_control_get_tid_state_finish:
 **/
static void
pk_control_get_tid_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->tid != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_strdup (state->tid),
							   g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_free (state->tid);
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_tid_cb:
 **/
static void
pk_control_get_tid_cb (GObject *source_object,
		       GAsyncResult *res,
		       gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		pk_control_get_tid_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save results */
	g_variant_get (value, "(o)", &state->tid);

	/* we're done */
	pk_control_get_tid_state_finish (state, NULL);
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_get_tid_internal:
 **/
static void
pk_control_get_tid_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "CreateTransaction",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_get_tid_cb,
			   state);
}

/**
 * pk_control_get_tid_proxy_cb:
 **/
static void
pk_control_get_tid_proxy_cb (GObject *source_object,
			     GAsyncResult *res,
			     gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_get_tid_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_get_tid_internal (state);
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
pk_control_get_tid_async (PkControl *control,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_get_tid_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_tid_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_get_tid_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_get_tid_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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
pk_control_get_tid_finish (PkControl *control,
			   GAsyncResult *res,
			   GError **error)
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

/**********************************************************************/


/**
 * pk_control_suggest_daemon_quit_state_finish:
 **/
static void
pk_control_suggest_daemon_quit_state_finish (PkControlState *state,
					     const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res,
							   state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_suggest_daemon_quit_cb:
 **/
static void
pk_control_suggest_daemon_quit_cb (GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		pk_control_suggest_daemon_quit_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->ret = TRUE;

	/* we're done */
	pk_control_suggest_daemon_quit_state_finish (state, NULL);
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_suggest_daemon_quit_internal:
 **/
static void
pk_control_suggest_daemon_quit_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "SuggestDaemonQuit",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_suggest_daemon_quit_cb,
			   state);
}

/**
 * pk_control_suggest_daemon_quit_proxy_cb:
 **/
static void
pk_control_suggest_daemon_quit_proxy_cb (GObject *source_object,
					 GAsyncResult *res,
					 gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_suggest_daemon_quit_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_suggest_daemon_quit_internal (state);
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
pk_control_suggest_daemon_quit_async (PkControl *control,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_suggest_daemon_quit_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_suggest_daemon_quit_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_suggest_daemon_quit_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_suggest_daemon_quit_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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

/**********************************************************************/


/**
 * pk_control_get_daemon_state_state_finish:
 **/
static void
pk_control_get_daemon_state_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->daemon_state != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_strdup (state->daemon_state), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_free (state->daemon_state);
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_daemon_state_cb:
 **/
static void
pk_control_get_daemon_state_cb (GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		pk_control_get_daemon_state_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save results */
	g_variant_get (value, "(s)", &state->daemon_state);

	/* we're done */
	pk_control_get_daemon_state_state_finish (state, NULL);
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_get_daemon_state_internal:
 **/
static void
pk_control_get_daemon_state_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "GetDaemonState",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_get_daemon_state_cb,
			   state);
}

/**
 * pk_control_get_daemon_state_proxy_cb:
 **/
static void
pk_control_get_daemon_state_proxy_cb (GObject *source_object,
			     GAsyncResult *res,
			     gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_get_daemon_state_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_get_daemon_state_internal (state);
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
pk_control_get_daemon_state_async (PkControl *control,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_get_daemon_state_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_daemon_state_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_get_daemon_state_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_get_daemon_state_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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
pk_control_get_daemon_state_finish (PkControl *control,
				    GAsyncResult *res,
				    GError **error)
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

/**********************************************************************/


/**
 * pk_control_set_proxy_state_finish:
 **/
static void
pk_control_set_proxy_state_finish (PkControlState *state,
				   const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res,
							   state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_variant_unref (state->parameters);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_set_proxy_cb:
 **/
static void
pk_control_set_proxy_cb (GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		g_warning ("failed to set proxy: %s", error->message);
		pk_control_set_proxy_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	state->ret = TRUE;

	/* we're done */
	pk_control_set_proxy_state_finish (state, NULL);
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_set_proxy_internal:
 **/
static void
pk_control_set_proxy_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "SetProxy",
			   state->parameters,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_set_proxy_cb,
			   state);
}

/**
 * pk_control_set_proxy_proxy_cb:
 **/
static void
pk_control_set_proxy_proxy_cb (GObject *source_object,
			     GAsyncResult *res,
			     gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_set_proxy_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_set_proxy_internal (state);
}

/**
 * pk_control_set_proxy2_async:
 * @control: a valid #PkControl instance
 * @proxy_http: a HTTP proxy string such as "username:password@server.lan:8080", or %NULL
 * @proxy_https: a HTTPS proxy string such as "username:password@server.lan:8080", or %NULL
 * @proxy_ftp: a FTP proxy string such as "server.lan:8080", or %NULL
 * @proxy_socks: a SOCKS proxy string such as "server.lan:8080", or %NULL
 * @no_proxy: a list of download IPs that shouldn't go through the proxy, or %NULL
 * @pac: a PAC string, or %NULL
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Set a proxy on the PK daemon
 *
 * Since: 0.6.13
 **/
void
pk_control_set_proxy2_async (PkControl *control,
			     const gchar *proxy_http,
			     const gchar *proxy_https,
			     const gchar *proxy_ftp,
			     const gchar *proxy_socks,
			     const gchar *no_proxy,
			     const gchar *pac,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_set_proxy_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	state->parameters = g_variant_new ("(ssssss)",
					   proxy_http ? proxy_http : "",
					   proxy_https ? proxy_https : "",
					   proxy_ftp ? proxy_ftp : "",
					   proxy_socks ? proxy_socks : "",
					   no_proxy ? no_proxy : "",
					   pac ? pac : "");
	g_variant_ref_sink (state->parameters);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_set_proxy_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_set_proxy_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_set_proxy_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
out:
	g_object_unref (res);
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
 * NOTE: This is just provided for backwards compatibility.
 * Clients should really be using pk_control_set_proxy2_async().
 *
 * Since: 0.5.2
 **/
void
pk_control_set_proxy_async (PkControl *control,
			    const gchar *proxy_http,
			    const gchar *proxy_ftp,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	pk_control_set_proxy2_async (control,
				     proxy_http,
				     NULL,
				     proxy_ftp,
				     NULL,
				     NULL,
				     NULL,
				     cancellable,
				     callback,
				     user_data);
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
pk_control_set_proxy_finish (PkControl *control,
			     GAsyncResult *res,
			     GError **error)
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

/**********************************************************************/

/**
 * pk_control_get_transaction_list_state_finish:
 **/
static void
pk_control_get_transaction_list_state_finish (PkControlState *state,
					      const GError *error)
{
	/* get result */
	if (state->transaction_list != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_strdupv (state->transaction_list),
							   (GDestroyNotify) g_strfreev);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_strfreev (state->transaction_list);
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_transaction_list_cb:
 **/
static void
pk_control_get_transaction_list_cb (GObject *source_object,
				    GAsyncResult *res,
				    gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	const gchar **tlist_tmp = NULL;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		pk_control_get_transaction_list_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* unwrap data */
	g_variant_get (value, "(^a&s)", &tlist_tmp);
	if (tlist_tmp == NULL) {
		state->transaction_list = g_new0 (gchar *, 1);
	} else {
		state->transaction_list = g_strdupv ((gchar **)tlist_tmp);
	}
	g_assert (state->transaction_list != NULL);

	/* we're done */
	pk_control_get_transaction_list_state_finish (state, NULL);
out:
	g_free (tlist_tmp);
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_get_transaction_list_internal:
 **/
static void
pk_control_get_transaction_list_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "GetTransactionList",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_get_transaction_list_cb,
			   state);
}

/**
 * pk_control_get_transaction_list_proxy_cb:
 **/
static void
pk_control_get_transaction_list_proxy_cb (GObject *source_object,
					  GAsyncResult *res,
					  gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_get_transaction_list_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_get_transaction_list_internal (state);
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
pk_control_get_transaction_list_async (PkControl *control,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_get_transaction_list_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_transaction_list_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_get_transaction_list_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_get_transaction_list_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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
pk_control_get_transaction_list_finish (PkControl *control,
					GAsyncResult *res,
					GError **error)
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

/**********************************************************************/


/**
 * pk_control_get_time_since_action_state_finish:
 **/
static void
pk_control_get_time_since_action_state_finish (PkControlState *state,
					       const GError *error)
{
	/* get result */
	if (state->time != 0) {
		g_simple_async_result_set_op_res_gssize (state->res, state->time);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_variant_unref (state->parameters);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_time_since_action_cb:
 **/
static void
pk_control_get_time_since_action_cb (GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	GVariant *value;
	GError *error = NULL;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	g_variant_get (value, "(u)", &state->time);
	if (state->time == 0) {
		error = g_error_new (PK_CONTROL_ERROR, PK_CONTROL_ERROR_FAILED, "could not get time");
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* we're done */
	pk_control_get_time_since_action_state_finish (state, NULL);
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_get_time_since_action_internal:
 **/
static void
pk_control_get_time_since_action_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "GetTimeSinceAction",
			   state->parameters,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_get_time_since_action_cb,
			   state);
}

/**
 * pk_control_get_time_since_action_proxy_cb:
 **/
static void
pk_control_get_time_since_action_proxy_cb (GObject *source_object,
					   GAsyncResult *res,
					   gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_get_time_since_action_internal (state);
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
pk_control_get_time_since_action_async (PkControl *control,
					PkRoleEnum role,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_get_time_since_action_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	state->parameters = g_variant_new ("(s)", pk_role_enum_to_string (role));
	g_variant_ref_sink (state->parameters);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_time_since_action_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_get_time_since_action_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_get_time_since_action_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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
pk_control_get_time_since_action_finish (PkControl *control,
					 GAsyncResult *res,
					 GError **error)
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

/**********************************************************************/


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

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_variant_unref (state->parameters);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_can_authorize_cb:
 **/
static void
pk_control_can_authorize_cb (GObject *source_object,
			     GAsyncResult *res,
			     gpointer user_data)
{
	GError *error = NULL;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkControlState *state = (PkControlState *) user_data;
	GVariant *value;
	const gchar *authorize_state = NULL;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_control_fixup_dbus_error (error);
		pk_control_can_authorize_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* save data */
	g_variant_get (value, "(&s)", &authorize_state);
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
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_control_can_authorize_internal:
 **/
static void
pk_control_can_authorize_internal (PkControlState *state)
{
	g_dbus_proxy_call (state->control->priv->proxy,
			   "CanAuthorize",
			   state->parameters,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CONTROL_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_control_can_authorize_cb,
			   state);
}

/**
 * pk_control_can_authorize_proxy_cb:
 **/
static void
pk_control_can_authorize_proxy_cb (GObject *source_object,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_can_authorize_state_finish (state, error);
		g_error_free (error);
		return;
	}
	pk_control_proxy_connect (state);
	pk_control_can_authorize_internal (state);
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
pk_control_can_authorize_async (PkControl *control,
				const gchar *action_id,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_can_authorize_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	state->parameters = g_variant_new ("(s)", action_id);
	g_variant_ref_sink (state->parameters);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_can_authorize_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	state->authorize = PK_AUTHORIZE_ENUM_UNKNOWN;

	/* skip straight to the D-Bus method if already connection */
	if (control->priv->proxy != NULL) {
		pk_control_can_authorize_internal (state);
	} else {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
					  G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  PK_DBUS_SERVICE,
					  PK_DBUS_PATH,
					  PK_DBUS_INTERFACE,
					  control->priv->cancellable,
					  pk_control_can_authorize_proxy_cb,
					  state);
	}

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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

/**********************************************************************/


/**
 * pk_control_get_properties_state_finish:
 **/
static void
pk_control_get_properties_state_finish (PkControlState *state, const GError *error)
{
	/* get result */
	if (state->ret) {
		g_simple_async_result_set_op_res_gboolean (state->res,
							   state->ret);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	g_ptr_array_remove (state->control->priv->calls, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable,
					  state->cancellable_id);
		g_object_unref (state->cancellable);
	}
	g_object_unref (state->res);
	g_object_unref (state->control);
	if (state->proxy != NULL)
		g_object_unref (state->proxy);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_properties_cb:
 **/
static void
pk_control_get_properties_cb (GObject *source_object,
			      GAsyncResult *res,
			      gpointer user_data)
{
	GError *error = NULL;
	PkControlState *state = (PkControlState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_control_get_properties_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* connect up proxy */
	pk_control_proxy_connect (state);

	/* save data */
	state->ret = TRUE;

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
pk_control_get_properties_async (PkControl *control,
				 GCancellable *cancellable,
				 GAsyncReadyCallback callback,
				 gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkControlState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (control),
					 callback,
					 user_data,
					 pk_control_get_properties_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->control = g_object_ref (control);
	if (cancellable != NULL)
		state->cancellable = g_object_ref (cancellable);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_control_get_properties_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* already done */
	if (control->priv->proxy != NULL) {
		state->ret = TRUE;
		pk_control_get_properties_state_finish (state, NULL);
		goto out;
	}

	/* get a connection to the main interface */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_NONE,
				  NULL,
				  PK_DBUS_SERVICE,
				  PK_DBUS_PATH,
				  PK_DBUS_INTERFACE,
				  control->priv->cancellable,
				  pk_control_get_properties_cb,
				  state);

	/* track state */
	g_ptr_array_add (control->priv->calls, state);
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

/**********************************************************************/

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
 * pk_control_name_appeared_cb:
 **/
static void
pk_control_name_appeared_cb (GDBusConnection *connection,
			     const gchar *name,
			     const gchar *name_owner,
			     gpointer user_data)
{
	PkControl *control = PK_CONTROL (user_data);
	control->priv->connected = TRUE;
	g_debug ("notify::connected");
	g_object_notify (G_OBJECT(control), "connected");
}

/**
 * pk_control_name_vanished_cb:
 **/
static void
pk_control_name_vanished_cb (GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	PkControl *control = PK_CONTROL (user_data);
	control->priv->connected = FALSE;
	g_debug ("notify::connected");
	g_object_notify (G_OBJECT(control), "connected");
}

/**
 * pk_control_init:
 * @control: This class instance
 **/
static void
pk_control_init (PkControl *control)
{
	control->priv = PK_CONTROL_GET_PRIVATE (control);
	control->priv->network_state = PK_NETWORK_ENUM_UNKNOWN;
	control->priv->version_major = G_MAXUINT;
	control->priv->version_minor = G_MAXUINT;
	control->priv->version_micro = G_MAXUINT;
	control->priv->cancellable = g_cancellable_new ();
	control->priv->calls = g_ptr_array_new ();
	control->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
						    PK_DBUS_SERVICE,
						    G_BUS_NAME_WATCHER_FLAGS_NONE,
						    pk_control_name_appeared_cb,
						    pk_control_name_vanished_cb,
						    control,
						    NULL);
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
	g_cancellable_cancel (priv->cancellable);
	g_bus_unwatch_name (priv->watch_id);

	/* remove pending sources */
	if (priv->transaction_list_changed_id != 0)
		g_source_remove (priv->transaction_list_changed_id);
	if (priv->restart_schedule_id != 0)
		g_source_remove (priv->restart_schedule_id);
	if (priv->updates_changed_id != 0)
		g_source_remove (priv->updates_changed_id);
	if (priv->repo_list_changed_id != 0)
		g_source_remove (priv->repo_list_changed_id);
	if (priv->proxy != NULL) {
		g_signal_handlers_disconnect_by_func (priv->proxy,
						      G_CALLBACK (pk_control_properties_changed_cb),
						      control);
		g_signal_handlers_disconnect_by_func (priv->proxy,
						      G_CALLBACK (pk_control_signal_cb),
						      control);
		g_object_unref (priv->proxy);
	}

	g_free (priv->backend_name);
	g_free (priv->backend_description);
	g_free (priv->backend_author);
	g_free (priv->mime_types);
	g_free (priv->distro_id);
	g_ptr_array_unref (priv->calls);

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
