/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2024-2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * PkClient:
 *
 * For creating new transactions
 *
 * A GObject to use for accessing PackageKit asynchronously. If you're
 * using #PkClient to install, remove, or update packages, be prepared that
 * the eula, gpg and trusted callbacks need to be rescheduled manually, as in
 * https://packagekit.freedesktop.org/doc/concepts.html#transactions
 */

#include "config.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "pk-client.h"
#include "pk-client-helper-private.h"
#include "pk-common.h"
#include "pk-common-private.h"
#include "pk-control.h"
#include "pk-enum.h"
#include "pk-package-id.h"
#include "pk-package-ids.h"
#include "pk-progress-private.h"

static void pk_client_finalize (GObject *object);

#define PK_CLIENT_DBUS_METHOD_TIMEOUT G_MAXINT /* ms */

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct _PkClientPrivate
{
	GDBusConnection *connection;
	GPtrArray *calls; /* (element-type PkClientState) (owned) */
	PkControl *control;
	gchar *locale;
	gboolean background;
	gboolean interactive;
	guint cache_age;
};

enum {
	PROP_0,
	PROP_LOCALE,
	PROP_BACKGROUND,
	PROP_INTERACTIVE,
	PROP_IDLE,
	PROP_CACHE_AGE,
	PROP_LAST
};

enum {
	SIGNAL_FRONTEND_SOCKET,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

static GParamSpec *obj_properties[PROP_LAST] = {
	NULL,
};

G_DEFINE_TYPE_WITH_PRIVATE (PkClient, pk_client, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (pk_client_get_instance_private (o))

#define PK_TYPE_CLIENT_STATE (pk_client_state_get_type ())

G_DECLARE_FINAL_TYPE (PkClientState, pk_client_state, PK, CLIENT_STATE, GObject)

static GWeakRef *
pk_client_weak_ref_new (gpointer object)
{
	GWeakRef *weak_ref;

	weak_ref = g_slice_new0 (GWeakRef);
	g_weak_ref_init (weak_ref, object);

	return weak_ref;
}

static void
pk_client_weak_ref_free (gpointer ptr)
{
	GWeakRef *weak_ref = ptr;

	g_return_if_fail (weak_ref != NULL);

	g_weak_ref_clear (weak_ref);
	g_slice_free (GWeakRef, weak_ref);
}

static void
pk_client_weak_ref_free_gclosure (gpointer ptr, GClosure *closure)
{
	pk_client_weak_ref_free (ptr);
}

static void pk_client_properties_changed_cb (GDBusProxy *proxy,
					     GVariant *changed_properties,
					     const gchar *const *invalidated_properties,
					     gpointer user_data);
static void pk_client_signal_cb (GDBusProxy *proxy,
				 const gchar *sender_name,
				 const gchar *signal_name,
				 GVariant *parameters,
				 gpointer user_data);
static void pk_client_notify_name_owner_cb (GObject *obj, GParamSpec *pspec, gpointer user_data);

struct _PkClientState
{
	GObject parent_instance;

	gboolean allow_deps;
	gboolean autoremove;
	gboolean enabled;
	gboolean force;
	PkBitfield transaction_flags;
	gboolean recursive;
	gboolean ret;
	gchar *directory;
	gchar *eula_id;
	gchar **files;
	gchar *key_id;
	gchar *package_id;
	gchar **package_ids;
	gchar *parameter;
	gchar *repo_id;
	gchar **search;
	gchar *tid;
	gchar *distro_id;
	gchar *transaction_id;
	gchar *value;
	gpointer user_data;
	guint number;
	gulong cancellable_id;
	GDBusProxy *proxy;
	GCancellable *cancellable;
	GCancellable *cancellable_client;
	GTask *res;
	PkBitfield filters;
	PkClient *client; /* (not nullable) (not owned) */
	PkProgress *progress;
	PkResults *results;
	PkRoleEnum role;
	PkSigTypeEnum type;
	PkUpgradeKindEnum upgrade_kind;
	gint remaining_files_to_copy;
	PkClientHelper *client_helper;
	gboolean waiting_for_finished;

	/* True if this PkClientState represents a peek at a transaction which
	 * it doesn’t own, rather than being the owner of the transaction: */
	gboolean querying_progress;
};

G_DEFINE_TYPE (PkClientState, pk_client_state, G_TYPE_OBJECT)

static void
pk_client_state_unset_proxy (PkClientState *state)
{
	if (state->proxy != NULL) {
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_properties_changed_cb),
						      state);
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_signal_cb),
						      state);
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_notify_name_owner_cb),
						      state);
		g_clear_object (&state->proxy);
	}
}

static void
pk_client_state_remove (PkClient *client, PkClientState *state)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	gboolean was_removed, is_idle;

	was_removed = g_ptr_array_remove_fast (priv->calls, state);
	g_debug ("%s: PkClientState %p %s removed from PkClient %p",
		 G_STRFUNC,
		 state,
		 was_removed ? "was" : "was not",
		 client);
	/* state may have been finalised after this point */

	/* has the idle state changed? */
	is_idle = (priv->calls->len == 0);
	if (was_removed && is_idle)
		g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_IDLE]);
}

/*
 * pk_client_state_finish:
 * @state: (transfer none): the #PkClientState
 * @error: (transfer full): the #GError
 **/
static void
pk_client_state_finish (PkClientState *state, GError *error)
{
	g_autoptr(PkClientState) state_owned = g_object_ref (state);
	g_autoptr(GError) error_owned = g_steal_pointer (&error);

	g_debug ("%s: PkClientState %p, error %p, PkClientState.res (GTask) %p",
		 G_STRFUNC,
		 state,
		 error_owned,
		 state->res);

	if (state->res == NULL)
		return;

	/* Either have to have been successful, or have set an error */
	g_assert (state->ret || error_owned != NULL);

	/* force finished (if not already set) so clients can update the UI's */
	if (state->progress != NULL && !state->querying_progress) {
		pk_progress_set_status (state->progress, PK_STATUS_ENUM_FINISHED);
	}

	pk_client_state_unset_proxy (state);

	/* remove any socket file */
	if (state->client_helper != NULL) {
		g_autoptr(GError) error_local = NULL;

		if (!pk_client_helper_stop (state->client_helper, &error_local))
			g_warning ("failed to stop the client helper: %s", error_local->message);
		g_object_unref (state->client_helper);
	}

	/* Remove the state from the in-flight list *before* returning the result.
	 * g_task_return_*() may dispatch the completion callback synchronously, and
	 * a caller inspecting the client from that callback (e.g. the "idle"
	 * property) must observe this transaction as already finished. */
	pk_client_state_remove (state->client, state);
	/* `state_owned` may be the only strong ref to `state` remaining after this point */

	if (state->ret) {
		g_task_return_pointer (state->res,
				       state->querying_progress
				       ? G_OBJECT (g_object_ref (state->progress))
				       : G_OBJECT (g_object_ref (state->results)), g_object_unref);
	} else {
		g_task_return_error (state->res, g_steal_pointer (&error_owned));
	}

	/* mark the state as finished */
	g_clear_object (&state->res);
}

static gboolean
pk_client_state_is_finished (PkClientState *self)
{
	g_return_val_if_fail (PK_IS_CLIENT_STATE (self), TRUE);

	return (self->res == NULL);
}

static void
pk_client_state_dispose (GObject *object)
{
	PkClientState *state = PK_CLIENT_STATE (object);

	if (state->cancellable_id > 0) {
		g_cancellable_disconnect (state->cancellable_client, state->cancellable_id);
		state->cancellable_id = 0;
	}
	g_clear_object (&state->cancellable);
	g_clear_object (&state->cancellable_client);

	G_OBJECT_CLASS (pk_client_state_parent_class)->dispose (object);
}

static void
pk_client_state_finalize (GObject *object)
{
	PkClientState *state = PK_CLIENT_STATE (object);

	g_free (state->directory);
	g_free (state->eula_id);
	g_free (state->key_id);
	g_free (state->package_id);
	g_free (state->parameter);
	g_free (state->repo_id);
	g_strfreev (state->search);
	g_free (state->value);
	g_free (state->tid);
	g_free (state->distro_id);
	g_free (state->transaction_id);
	g_strfreev (state->files);
	g_strfreev (state->package_ids);
	pk_client_state_unset_proxy (state);

	/* results will not exist if the CreateTransaction fails */
	g_clear_object (&state->results);
	g_clear_object (&state->progress);
	g_clear_object (&state->res);
	state->client = NULL;

	G_OBJECT_CLASS (pk_client_state_parent_class)->finalize (object);
}

static void
pk_client_state_class_init (PkClientStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = pk_client_state_dispose;
	object_class->finalize = pk_client_state_finalize;
}

static void
pk_client_state_init (PkClientState *state)
{}

static void
pk_client_cancel_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	GWeakRef *weak_ref = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;
	g_autoptr(PkClientState) state = NULL;

	state = g_weak_ref_get (weak_ref);

	pk_client_weak_ref_free (weak_ref);

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* Instructing the daemon to cancel failed, so just return an
		 * error to the client so they don’t wait forever. */
		g_debug ("failed to cancel: %s", error->message);

		if (state)
			pk_client_state_finish (state, g_steal_pointer (&error));
	}

	/* wait for the response from the server to the Cancel() method */
}

static void
pk_client_cancellable_cancel_cb (GCancellable *cancellable, gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	g_autoptr(PkClientState) state = NULL;

	state = g_weak_ref_get (weak_ref);

	if (state == NULL) {
		g_debug ("Cancelled, but the operation is already over");
		return;
	}

	/* D-Bus method has not yet fired. This can happen, for example, when
	 * pk_client_state_new() is called with a #GCancellable which has
	 * already been cancelled. */
	if (state->proxy == NULL) {
		g_autoptr(GError) local_error = NULL;

		g_debug ("%s: Cancelled PkClientState %p, but no proxy", G_STRFUNC, state);

		if (!g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
			local_error = g_error_new_literal (PK_CLIENT_ERROR,
							   PK_CLIENT_ERROR_FAILED,
							   "PackageKit transaction disappeared");
		}

		pk_client_state_finish (state, g_steal_pointer (&local_error));
		return;
	}

	/* takeover the call with the cancel method */
	g_debug ("cancelling %s", state->tid);
	g_dbus_proxy_call (state->proxy,
			   "Cancel",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CLIENT_DBUS_METHOD_TIMEOUT,
			   NULL,
			   pk_client_cancel_cb,
			   pk_client_weak_ref_new (state));
}

static void pk_client_state_add (PkClient *client, PkClientState *state);

static PkClientState *
pk_client_state_new (PkClient *client,
		     GAsyncReadyCallback callback_ready,
		     gpointer user_data,
		     gpointer source_tag,
		     PkRoleEnum role,
		     GCancellable *cancellable)
{
	PkClientState *state;

	state = g_object_new (PK_TYPE_CLIENT_STATE, NULL);
	state->role = role;
	state->cancellable = g_cancellable_new ();
	state->res = g_task_new (client, state->cancellable, callback_ready, user_data);
	state->client = client;
	g_task_set_source_tag (state->res, source_tag);

	g_debug (
	    "%s: Created new PkClientState %p with PkClientState.res (GTask) %p for PkClient %p",
	    G_STRFUNC,
	    state,
	    state->res,
	    client);

	/* track state */
	pk_client_state_add (client, state);

	/* Set up cancellation. If `cancellable` has already been cancelled,
	 * this could result in pk_client_cancellable_cancel_cb() being called
	 * synchronously. */
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (
		    cancellable,
		    G_CALLBACK (pk_client_cancellable_cancel_cb),
		    pk_client_weak_ref_new (state),
		    pk_client_weak_ref_free);
	}

	return state;
}

/**
 * pk_client_error_quark:
 *
 * An error quark for #PkClientError.
 *
 * Return value: an error quark.
 *
 * Since: 0.5.2
 **/
G_DEFINE_QUARK (pk-client-error-quark, pk_client_error)

/*
 * pk_client_get_property:
 **/
static void
pk_client_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = GET_PRIVATE(client);

	switch (prop_id) {
	case PROP_LOCALE:
		g_value_set_string (value, priv->locale);
		break;
	case PROP_BACKGROUND:
		g_value_set_boolean (value, priv->background);
		break;
	case PROP_INTERACTIVE:
		g_value_set_boolean (value, priv->interactive);
		break;
	case PROP_IDLE:
		g_value_set_boolean (value, pk_client_get_idle (client));
		break;
	case PROP_CACHE_AGE:
		g_value_set_uint (value, priv->cache_age);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_client_set_property:
 **/
static void
pk_client_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = GET_PRIVATE(client);

	switch (prop_id) {
	case PROP_LOCALE:
		g_free (priv->locale);
		priv->locale = g_strdup (g_value_get_string (value));
		break;
	case PROP_BACKGROUND:
		priv->background = g_value_get_boolean (value);
		break;
	case PROP_INTERACTIVE:
		priv->interactive = g_value_get_boolean (value);
		break;
	case PROP_CACHE_AGE:
		priv->cache_age = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_client_fixup_dbus_error:
 **/
static void
pk_client_fixup_dbus_error (GError *error)
{
	const gchar *name_suffix = NULL;
	g_autofree gchar *name = NULL;

	g_return_if_fail (error != NULL);

	/* old style PolicyKit failure */
	if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
		g_debug ("fixing up code for Policykit auth failure");
		error->code = PK_CLIENT_ERROR_FAILED_AUTH;
		g_free (error->message);
		error->message = g_strdup ("PolicyKit authorization failure");
		return;
	}

	/* is a remote error? */
	if (!g_dbus_error_is_remote_error (error))
		return;

	/* fall back to generic */
	error->domain = PK_CLIENT_ERROR;
	error->code = PK_CLIENT_ERROR_FAILED;

	/* parse the remote error */
	name = g_dbus_error_get_remote_error (error);
	g_dbus_error_strip_remote_error (error);
	if (g_str_has_prefix (name, PK_DBUS_INTERFACE_TRANSACTION "."))
		name_suffix = name + strlen (PK_DBUS_INTERFACE_TRANSACTION ".");
	if (g_strcmp0 (name_suffix, "Denied") == 0 ||
	    g_strcmp0 (name_suffix, "RefusedByPolicy") == 0) {
		error->code = PK_CLIENT_ERROR_FAILED_AUTH;
		return;
	}
	if (g_strcmp0 (name_suffix, "PackageIdInvalid") == 0 ||
	    g_strcmp0 (name_suffix, "SearchInvalid") == 0 ||
	    g_strcmp0 (name_suffix, "FilterInvalid") == 0 ||
	    g_strcmp0 (name_suffix, "InvalidProvide") == 0 ||
	    g_strcmp0 (name_suffix, "InputInvalid") == 0) {
		error->code = PK_CLIENT_ERROR_INVALID_INPUT;
		return;
	}
	if (g_strcmp0 (name_suffix, "PackInvalid") == 0 ||
	    g_strcmp0 (name_suffix, "NoSuchFile") == 0 ||
	    g_strcmp0 (name_suffix, "MimeTypeNotSupported") == 0 ||
	    g_strcmp0 (name_suffix, "NoSuchDirectory") == 0) {
		error->code = PK_CLIENT_ERROR_INVALID_FILE;
		return;
	}
	if (g_strcmp0 (name_suffix, "NotSupported") == 0) {
		error->code = PK_CLIENT_ERROR_NOT_SUPPORTED;
		return;
	}

	/* standard D-Bus errors (e.g. UnknownMethod from calling a method on a
	 * transaction object that no longer exists) are expected remote-error
	 * traffic. Keep the generic failure code set above */
	if (g_str_has_prefix (name, "org.freedesktop.DBus.Error.")) {
		g_debug ("mapped standard D-Bus error '%s' to a generic failure", name);
		return;
	}

	g_warning ("couldn't parse exception '%s', please report", name);
}

/*
 * pk_client_real_path:
 *
 * Resolves paths like ../../Desktop/bar.rpm to /home/hughsie/Desktop/bar.rpm
 * TODO: We should use canonicalize_filename() in gio/glocalfile.c as realpath()
 * is crap.
 **/
static gchar *
pk_client_real_path (const gchar *path)
{
	gchar *real = NULL;
	gchar *temp;

	/* don't trust realpath one little bit */
	if (path == NULL)
		return NULL;

#ifndef __FreeBSD__
	/* ITS4: ignore, glibc allocates us a buffer to try and fix some brain damage */
	temp = realpath (path, NULL);
	if (temp != NULL) {
		real = g_strdup (temp);
		/* yes, free, not g_free */
		free (temp);
	}
#else /* __FreeBSD__ */
	{
		gchar abs_path[PATH_MAX];
		temp = realpath (path, abs_path);
		if (temp != NULL)
			real = g_strdup (temp);
	}
#endif
	return real;
}

/*
 * pk_client_convert_real_paths:
 **/
static gchar **
pk_client_convert_real_paths (gchar **paths, GError **error)
{
	guint i;
	guint len;
	g_auto(GStrv) res = NULL;

	/* create output array */
	len = g_strv_length (paths);
	res = g_new0 (gchar *, len + 1);

	/* resolve each path */
	for (i = 0; i < len; i++) {
		res[i] = pk_client_real_path (paths[i]);
		if (res[i] == NULL) {
			g_set_error (error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_INVALID_INPUT,
				     "could not resolve: %s",
				     paths[i]);
			return NULL;
		}
	}
	return g_strdupv (res);
}

/*
 * pk_client_percentage_to_signed:
 */
static gint
pk_client_percentage_to_signed (guint percentage)
{
	if (percentage == 101)
		return -1;
	return (gint) percentage;
}

/*
 * pk_client_set_property_value:
 **/
static void
pk_client_set_property_value (PkClientState *state, const char *key, GVariant *value)
{
	const gchar *package_id;

	/* role */
	if (g_strcmp0 (key, "Role") == 0) {
		pk_progress_set_role (state->progress, g_variant_get_uint32 (value));
		return;
	}

	/* status */
	if (g_strcmp0 (key, "Status") == 0) {
		pk_progress_set_status (state->progress, g_variant_get_uint32 (value));
		return;
	}

	/* last-package */
	if (g_strcmp0 (key, "LastPackage") == 0) {
		package_id = g_variant_get_string (value, NULL);
		/* check to see if it's been set yet */
		if (!pk_package_id_check (package_id))
			return;
		pk_progress_set_package_id (state->progress, package_id);
		return;
	}

	/* percentage */
	if (g_strcmp0 (key, "Percentage") == 0) {
		pk_progress_set_percentage (
		    state->progress,
		    pk_client_percentage_to_signed (g_variant_get_uint32 (value)));
		return;
	}

	/* allow-cancel */
	if (g_strcmp0 (key, "AllowCancel") == 0) {
		pk_progress_set_allow_cancel (state->progress, g_variant_get_boolean (value));
		return;
	}

	/* caller-active */
	if (g_strcmp0 (key, "CallerActive") == 0) {
		pk_progress_set_caller_active (state->progress, g_variant_get_boolean (value));
		return;
	}

	/* elapsed-time */
	if (g_strcmp0 (key, "ElapsedTime") == 0) {
		pk_progress_set_elapsed_time (state->progress, g_variant_get_uint32 (value));
		return;
	}

	/* remaining-time */
	if (g_strcmp0 (key, "RemainingTime") == 0) {
		pk_progress_set_remaining_time (state->progress, g_variant_get_uint32 (value));
		return;
	}

	/* speed */
	if (g_strcmp0 (key, "Speed") == 0) {
		pk_progress_set_speed (state->progress, g_variant_get_uint32 (value));
		return;
	}

	/* download-size-remaining */
	if (g_strcmp0 (key, "DownloadSizeRemaining") == 0) {
		pk_progress_set_download_size_remaining (state->progress,
							 g_variant_get_uint64 (value));
		return;
	}

	/* transaction-flags */
	if (g_strcmp0 (key, "TransactionFlags") == 0) {
		pk_progress_set_transaction_flags (state->progress, g_variant_get_uint64 (value));
		return;
	}

	/* uid */
	if (g_strcmp0 (key, "Uid") == 0) {
		pk_progress_set_uid (state->progress, g_variant_get_uint32 (value));
		return;
	}

	/* sender */
	if (g_strcmp0 (key, "Sender") == 0) {
		pk_progress_set_sender (state->progress, g_variant_get_string (value, NULL));
		return;
	}

	g_warning ("unhandled property '%s'", key);
}

/*
 * pk_client_state_add:
 **/
static void
pk_client_state_add (PkClient *client, PkClientState *state)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	gboolean was_idle;

	g_debug ("%s: adding PkClientState %p to PkClient %p", G_STRFUNC, state, client);

	was_idle = pk_client_get_idle (client);

	g_ptr_array_add (priv->calls, g_object_ref (state));

	/* has the idle state changed? */
	if (was_idle)
		g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_IDLE]);
}

/*
 * pk_client_properties_changed_cb:
 **/
static void
pk_client_properties_changed_cb (GDBusProxy *proxy,
				 GVariant *changed_properties,
				 const gchar *const *invalidated_properties,
				 gpointer user_data)
{
	const gchar *key;
	GVariantIter *iter;
	GVariant *value;
	GWeakRef *weak_ref = user_data;
	g_autoptr(PkClientState) state = g_weak_ref_get (weak_ref);

	if (!state)
		return;

	if (g_variant_n_children (changed_properties) > 0) {
		g_variant_get (changed_properties, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value))
			pk_client_set_property_value (state, key, value);
		g_variant_iter_free (iter);
	}
}

/*
 * pk_client_signal_package:
 */
static void
pk_client_signal_package (PkClientState *state,
			  PkInfoEnum info_enum,
			  PkSeverityEnum update_severity,
			  const gchar *package_id,
			  const gchar *summary)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkPackage) package = NULL;

	/* create virtual package */
	package = pk_package_new ();
	if (!pk_package_set_id (package, package_id, &error)) {
		g_warning ("failed to set package id for %s", package_id);
		return;
	}
	g_object_set (package,
		      "info",
		      info_enum,
		      "summary",
		      summary,
		      "update-severity",
		      update_severity,
		      "role",
		      state->role,
		      "transaction-id",
		      state->transaction_id,
		      NULL);

	/* add to results */
	if (state->results != NULL)
		pk_results_add_package (state->results, package);

	/* only emit progress for verb packages */
	switch (info_enum) {
	case PK_INFO_ENUM_DOWNLOADING:
	case PK_INFO_ENUM_UPDATING:
	case PK_INFO_ENUM_INSTALLING:
	case PK_INFO_ENUM_REMOVING:
	case PK_INFO_ENUM_CLEANUP:
	case PK_INFO_ENUM_OBSOLETING:
	case PK_INFO_ENUM_REINSTALLING:
	case PK_INFO_ENUM_DOWNGRADING:
	case PK_INFO_ENUM_PREPARING:
		pk_progress_set_package_id (state->progress, package_id);
		pk_progress_set_package (state->progress, package);
		break;
	default:
		break;
	}
}

/*
 * pk_client_copy_finished_remove_old_files:
 *
 * Removes all the files that are not directly in the destination directory.
 * This should remove all the old /var/cache/PackageKit/$TMP/powertop-1.8-1.fc8.rpm
 * and leave the $DESTDIR/powertop-1.8-1.fc8.rpm files.
 *
 * The parent directory is compared rather than a path prefix, as the daemon's
 * download cache may itself live below the destination directory, e.g. when
 * downloading to /tmp while the daemon runs with a RootDir under /tmp.
 */
static void
pk_client_copy_finished_remove_old_files (PkClientState *state)
{
	guint i;
	g_autoptr(GFile) directory = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* get the data */
	array = pk_results_get_files_array (state->results);
	directory = g_file_new_for_path (state->directory);

	/* remove any not in the destination directory */
	for (i = 0; i < array->len;) {
		PkFiles *item;
		GPtrArray *files;
		g_autoptr(GFile) file = NULL;
		g_autoptr(GFile) parent = NULL;

		item = g_ptr_array_index (array, i);
		files = pk_files_get_files (item);
		file = g_file_new_for_path (pk_file_get_path (g_ptr_array_index (files, 0)));
		parent = g_file_get_parent (file);
		if (parent == NULL || !g_file_equal (parent, directory))
			g_ptr_array_remove_index_fast (array, i);
		else
			i++;
	}
}

/*
 * pk_client_copy_downloaded_finished_cb:
 */
static void
pk_client_copy_downloaded_finished_cb (GFile *file, GAsyncResult *res, gpointer user_data)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkClientState) state = user_data;

	/* debug */
	path = g_file_get_path (file);
	g_debug ("finished copy of %s", path);

	/* get the result */
	if (!g_file_copy_finish (file, res, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* no more copies pending? */
	if (g_atomic_int_dec_and_test (&state->remaining_files_to_copy)) {
		pk_client_copy_finished_remove_old_files (state);
		state->ret = TRUE;
		pk_client_state_finish (state, NULL);
	}
}

/*
 * pk_client_copy_progress_cb:
 */
static void
pk_client_copy_progress_cb (goffset current_num_bytes,
			    goffset total_num_bytes,
			    PkClientState *state)
{
	gint percentage = -1;

	/* save status */
	pk_progress_set_status (state->progress, PK_STATUS_ENUM_COPY_FILES);

	/* calculate percentage */
	if (total_num_bytes > 0)
		percentage = 100 * current_num_bytes / total_num_bytes;

	/* save percentage */
	pk_progress_set_percentage (state->progress, percentage);
}

/*
 * pk_client_copy_downloaded_file:
 */
static void
pk_client_copy_downloaded_file (PkClientState *state,
				const gchar *package_id,
				const gchar *source_file)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GFile) destination = NULL;
	g_autoptr(GFile) source = NULL;
	g_autoptr(PkFiles) item = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* generate the destination location */
	basename = g_path_get_basename (source_file);
	path = g_build_filename (state->directory, basename, NULL);

	/* copy async */
	g_debug ("copy %s to %s", source_file, path);
	source = g_file_new_for_path (source_file);
	destination = g_file_new_for_path (path);
	if (g_file_query_exists (destination, state->cancellable)) {
		g_set_error (&error,
			     PK_CLIENT_ERROR,
			     PK_ERROR_ENUM_FILE_CONFLICTS,
			     "file %s already exists",
			     path);
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}
	g_file_copy_async (source,
			   destination,
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   state->cancellable,
			   (GFileProgressCallback) pk_client_copy_progress_cb,
			   state,
			   (GAsyncReadyCallback) pk_client_copy_downloaded_finished_cb,
			   g_object_ref (state));

	/* add the result to the results set */
	files = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_file_free);
	g_ptr_array_add (files, pk_file_new (path, PK_FILE_KIND_ENUM_REGULAR));
	item = pk_files_new ();
	g_object_set (item,
		      "package-id",
		      package_id,
		      "files",
		      files,
		      "role",
		      state->role,
		      "transaction-id",
		      state->transaction_id,
		      NULL);
	pk_results_add_files (state->results, item);
}

/*
 * pk_client_copy_downloaded:
 *
 * We have to copy the files from the temporary directory into the user-specified
 * directory. There should only be one file for each package, although this is
 * not encoded in the spec.
 *
 * This will eventually call pk_client_state_finish() on `state`.
 */
static void
pk_client_copy_downloaded (PkClientState *state)
{
	guint len;
	PkFiles *item;
	g_autoptr(GPtrArray) array = NULL;
	unsigned int n_files_to_copy;

	/* get data */
	array = pk_results_get_files_array (state->results);

	/* get the number of files to copy */
	n_files_to_copy = 0;
	for (guint i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		n_files_to_copy += pk_files_get_files (item)->len;
	}

	g_atomic_int_set (&state->remaining_files_to_copy, n_files_to_copy);

	/* get a cached value, as pk_client_copy_downloaded_file() adds items */
	len = array->len;

	/* save percentage */
	pk_progress_set_percentage (state->progress, -1);

	/* do the copies pipelined */
	for (guint i = 0; i < len; i++) {
		GPtrArray *files;
		item = g_ptr_array_index (array, i);
		files = pk_files_get_files (item);
		for (guint j = 0; j < files->len; j++) {
			pk_client_copy_downloaded_file (
			    state,
			    pk_files_get_package_id (item),
			    pk_file_get_path (g_ptr_array_index (files, j)));
		}
	}

	/* were there actually any files to copy? */
	if (n_files_to_copy == 0) {
		state->ret = TRUE;
		pk_client_state_finish (state, NULL);
	}
}

/*
 * pk_client_signal_finished:
 */
static void
pk_client_signal_finished (PkClientState *state, PkExitEnum exit_enum, guint runtime)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;

	/* yay */
	pk_results_set_exit_code (state->results, exit_enum);

	/* failed */
	if (exit_enum == PK_EXIT_ENUM_FAILED) {

		/* get error code and error message */
		error_code = pk_results_get_error_code (state->results);
		if (error_code != NULL) {
			/* should only ever have one ErrorCode */
			error = g_error_new (PK_CLIENT_ERROR,
					     0xFF + pk_error_get_code (error_code),
					     "%s",
					     pk_error_get_details (error_code));
		} else {
			/* fallback where the daemon didn't sent ErrorCode */
			error = g_error_new (PK_CLIENT_ERROR,
					     PK_CLIENT_ERROR_FAILED,
					     "Failed: %s",
					     pk_exit_enum_to_string (exit_enum));
		}
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* do we have to copy results? If so, this will eventually call
	 * pk_client_state_finish() for us. */
	if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES && state->directory != NULL &&
	    exit_enum != PK_EXIT_ENUM_CANCELLED) {
		pk_client_copy_downloaded (state);
		return;
	}

	/* we're done */
	state->ret = TRUE;
	pk_client_state_finish (state, NULL);
}

/**
 * pk_client_issues_from_variant:
 *
 * Parse the `issues` value of an update detail (an `aa{sv}`)
 */
static GPtrArray *
pk_client_issues_from_variant (GVariant *variant)
{
	GPtrArray *issues = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_issue_free);
	GVariantIter iter;
	GVariant *child;

	g_variant_iter_init (&iter, variant);
	while ((child = g_variant_iter_next_value (&iter)) != NULL) {
		guint kind = PK_ISSUE_KIND_ENUM_UNKNOWN;
		const gchar *url = NULL;
		const gchar *id = NULL;
		const gchar *title = NULL;

		g_variant_lookup (child, "kind", "u", &kind);
		g_variant_lookup (child, "url", "&s", &url);
		g_variant_lookup (child, "id", "&s", &id);
		g_variant_lookup (child, "title", "&s", &title);
		if (kind >= PK_ISSUE_KIND_ENUM_LAST)
			kind = PK_ISSUE_KIND_ENUM_UNKNOWN;
		if (url != NULL)
			g_ptr_array_add (issues, pk_issue_new (kind, url, id, title));
		g_variant_unref (child);
	}

	return issues;
}

/*
 * pk_client_packages_from_variant:
 * @variant: the `packages` value of a past transaction, as `a(uss)`
 *
 * Returns: (element-type PkPackage) (transfer full): the packages
 */
static GPtrArray *
pk_client_packages_from_variant (GVariant *variant)
{
	GPtrArray *packages = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GVariantIter) iter = NULL;
	guint info;
	const gchar *package_id;
	const gchar *summary;

	if (!g_variant_is_of_type (variant, G_VARIANT_TYPE ("a(uss)")))
		return packages;
	g_variant_get (variant, "a(uss)", &iter);
	while (g_variant_iter_loop (iter, "(u&s&s)", &info, &package_id, &summary)) {
		g_autoptr(PkPackage) package = pk_package_new ();
		if (!pk_package_set_id (package, package_id, NULL))
			continue;
		pk_package_set_info (package, info);
		pk_package_set_summary (package, summary);
		g_ptr_array_add (packages, g_steal_pointer (&package));
	}
	return packages;
}

/**
 * results_add_update_detail_from_variant:
 *
 * Parse one `(sa{sv})` element of the UpdateDetails signal.
 */
static void
results_add_update_detail_from_variant (PkResults *results,
					GVariant *update_variant,
					PkRoleEnum role,
					const gchar *transaction_id)
{
	g_autoptr(PkUpdateDetail) item = NULL;
	g_autoptr(GVariantIter) dict = NULL;
	const gchar *package_id;
	const gchar *key;
	GVariant *value;

	g_variant_get (update_variant, "(&sa{sv})", &package_id, &dict);
	item = pk_update_detail_new ();
	g_object_set (item,
		      "package-id",
		      package_id,
		      "role",
		      role,
		      "transaction-id",
		      transaction_id,
		      NULL);

	while (g_variant_iter_loop (dict, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, "updates") == 0 || g_strcmp0 (key, "obsoletes") == 0) {
			g_autofree const gchar **strv = g_variant_get_strv (value, NULL);
			g_object_set (item, key, strv, NULL);
		} else if (g_strcmp0 (key, "issues") == 0) {
			g_autoptr(GPtrArray) issues = pk_client_issues_from_variant (value);
			g_object_set (item, "issues", issues, NULL);
		} else if (g_strcmp0 (key, "restart") == 0) {
			guint restart = g_variant_get_uint32 (value);
			if (restart >= PK_RESTART_ENUM_LAST)
				restart = PK_RESTART_ENUM_UNKNOWN;
			g_object_set (item, "restart", restart, NULL);
		} else if (g_strcmp0 (key, "state") == 0) {
			guint state = g_variant_get_uint32 (value);
			if (state >= PK_UPDATE_STATE_ENUM_LAST)
				state = PK_UPDATE_STATE_ENUM_UNKNOWN;
			g_object_set (item, "state", state, NULL);
		} else if (g_strcmp0 (key, "update-text") == 0 ||
			   g_strcmp0 (key, "changelog") == 0 ||
			   g_strcmp0 (key, "time-issued") == 0) {
			g_object_set (item, key, g_variant_get_string (value, NULL), NULL);
		}
	}

	pk_results_add_update_detail (results, item);
}

/*
 * pk_client_signal_cb:
 **/
static void
pk_client_signal_cb (GDBusProxy *proxy,
		     const gchar *sender_name,
		     const gchar *signal_name,
		     GVariant *parameters,
		     gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	g_autoptr(PkClientState) state = g_weak_ref_get (weak_ref);
	gchar *tmp_str[12];
	gboolean tmp_bool;
	guint tmp_uint;
	guint tmp_uint2;

	if (!state)
		return;

	if (g_strcmp0 (signal_name, "Finished") == 0) {
		if (state->waiting_for_finished) {
			g_variant_get (parameters, "(uu)", &tmp_uint2, &tmp_uint);
			/* this will call pk_client_state_finish(): */
			pk_client_signal_finished (state, tmp_uint2, tmp_uint);
		}
		return;
	}
	if (g_strcmp0 (signal_name, "Packages") == 0) {
		g_autoptr(GVariantIter) iter = NULL;
		guint info, severity;
		const gchar *package_id, *summary;

		g_variant_get (parameters, "(a(uuss))", &iter);

		while (
		    g_variant_iter_loop (iter, "(uu&s&s)", &info, &severity, &package_id, &summary))
			pk_client_signal_package (state, info, severity, package_id, summary);

		return;
	}
	if (g_strcmp0 (signal_name, "Details") == 0) {
		gchar *key;
		GVariantIter *dictionary;
		GVariant *value;
		g_autoptr(PkDetails) item = NULL;
		item = pk_details_new ();

		if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(a{sv})"))) {
			g_variant_get_child (parameters, 0, "a{sv}", &dictionary);
			while (g_variant_iter_loop (dictionary, "{sv}", &key, &value)) {
				if (g_strcmp0 (key, "group") == 0)
					g_object_set (item,
						      "group",
						      g_variant_get_uint32 (value),
						      NULL);
				else if (g_strcmp0 (key, "size") == 0)
					g_object_set (item,
						      "size",
						      g_variant_get_uint64 (value),
						      NULL);
				else if (g_strcmp0 (key, "download-size") == 0)
					g_object_set (item,
						      "download-size",
						      g_variant_get_uint64 (value),
						      NULL);
				else if (g_strcmp0 (key, "package-id") == 0 ||
					 g_strcmp0 (key, "summary") == 0 ||
					 g_strcmp0 (key, "description") == 0 ||
					 g_strcmp0 (key, "url") == 0 ||
					 g_strcmp0 (key, "license") == 0)
					g_object_set (item,
						      key,
						      g_variant_get_string (value, NULL),
						      NULL);
				else
					g_debug ("ignoring unknown Details key '%s'", key);
			}
			g_variant_iter_free (dictionary);
		} else {
			guint64 tmp_uint64;
			g_variant_get (parameters,
				       "(&s&su&s&st)",
				       &tmp_str[0],
				       &tmp_str[1],
				       &tmp_uint,
				       &tmp_str[3],
				       &tmp_str[4],
				       &tmp_uint64);
			g_object_set (item,
				      "package-id",
				      tmp_str[0],
				      "license",
				      tmp_str[1],
				      "group",
				      tmp_uint,
				      "description",
				      tmp_str[3],
				      "url",
				      tmp_str[4],
				      "size",
				      tmp_uint64,
				      "role",
				      state->role,
				      "transaction-id",
				      state->transaction_id,
				      NULL);
		}
		pk_results_add_details (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "UpdateDetails") == 0) {
		g_autoptr(GVariantIter) iter = NULL;
		g_autoptr(GVariant) update_detail = NULL;

		g_variant_get (parameters, "(a(sa{sv}))", &iter);

		while ((update_detail = g_variant_iter_next_value (iter))) {
			results_add_update_detail_from_variant (state->results,
								update_detail,
								state->role,
								state->transaction_id);
			g_clear_pointer (&update_detail, g_variant_unref);
		}

		return;
	}
	if (g_strcmp0 (signal_name, "Transactions") == 0) {
		GVariantIter *iter;
		GVariantIter *dict;
		const gchar *tid;
		const gchar *key;
		GVariant *value;

		g_variant_get (parameters, "(a(oa{sv}))", &iter);
		while (g_variant_iter_loop (iter, "(&oa{sv})", &tid, &dict)) {
			g_autoptr(PkTransactionPast) item = pk_transaction_past_new ();
			g_object_set (item,
				      "tid",
				      tid,
				      "PkSource::role",
				      state->role,
				      "transaction-id",
				      state->transaction_id,
				      NULL);
			while (g_variant_iter_loop (dict, "{&sv}", &key, &value)) {
				if (g_strcmp0 (key, "time-started") == 0 ||
				    g_strcmp0 (key, "cmdline") == 0) {
					g_object_set (item,
						      key,
						      g_variant_get_string (value, NULL),
						      NULL);
				} else if (g_strcmp0 (key, "succeeded") == 0) {
					g_object_set (item,
						      key,
						      g_variant_get_boolean (value),
						      NULL);
				} else if (g_strcmp0 (key, "role") == 0 ||
					   g_strcmp0 (key, "duration") == 0 ||
					   g_strcmp0 (key, "uid") == 0) {
					g_object_set (item,
						      key,
						      g_variant_get_uint32 (value),
						      NULL);
				} else if (g_strcmp0 (key, "packages") == 0) {
					g_autoptr(GPtrArray) packages = NULL;
					packages = pk_client_packages_from_variant (value);
					g_object_set (item, "packages", packages, NULL);
				}
			}
			pk_results_add_transaction (state->results, item);
		}
		g_variant_iter_free (iter);
		return;
	}
	if (g_strcmp0 (signal_name, "DistroUpgrade") == 0) {
		g_autoptr(PkDistroUpgrade) item = NULL;
		g_variant_get (parameters, "(u&s&s)", &tmp_uint, &tmp_str[1], &tmp_str[2]);
		item = pk_distro_upgrade_new ();
		g_object_set (item,
			      "state",
			      tmp_uint,
			      "name",
			      tmp_str[1],
			      "summary",
			      tmp_str[2],
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_add_distro_upgrade (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "RequireRestart") == 0) {
		g_autoptr(PkRequireRestart) item = NULL;
		g_variant_get (parameters, "(u&s)", &tmp_uint, &tmp_str[1]);
		item = pk_require_restart_new ();
		g_object_set (item,
			      "restart",
			      tmp_uint,
			      "package-id",
			      tmp_str[1],
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_add_require_restart (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "Files") == 0) {
		g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func (
		    (GDestroyNotify) pk_file_free);
		g_autoptr(GVariantIter) iter = NULL;
		g_autoptr(PkFiles) item = NULL;
		const gchar *path;
		guint32 kind;
		g_variant_get (parameters, "(&sa(su))", &tmp_str[0], &iter);
		while (g_variant_iter_next (iter, "(&su)", &path, &kind))
			g_ptr_array_add (files, pk_file_new (path, kind));
		item = pk_files_new ();
		g_object_set (item,
			      "package-id",
			      tmp_str[0],
			      "files",
			      files,
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_add_files (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "RepoSignatureRequired") == 0) {
		g_autoptr(PkRepoSignatureRequired) item = NULL;
		g_variant_get (parameters,
			       "(&s&s&s&s&s&s&su)",
			       &tmp_str[0],
			       &tmp_str[1],
			       &tmp_str[2],
			       &tmp_str[3],
			       &tmp_str[4],
			       &tmp_str[5],
			       &tmp_str[6],
			       &tmp_uint);
		item = pk_repo_signature_required_new ();
		g_object_set (item,
			      "package-id",
			      tmp_str[0],
			      "repository-name",
			      tmp_str[1],
			      "key-url",
			      tmp_str[2],
			      "key-userid",
			      tmp_str[3],
			      "key-id",
			      tmp_str[4],
			      "key-fingerprint",
			      tmp_str[5],
			      "key-timestamp",
			      tmp_str[6],
			      "type",
			      tmp_uint,
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_add_repo_signature_required (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "EulaRequired") == 0) {
		g_autoptr(PkEulaRequired) item = NULL;
		g_variant_get (parameters,
			       "(&s&s&s&s)",
			       &tmp_str[0],
			       &tmp_str[1],
			       &tmp_str[2],
			       &tmp_str[3]);
		item = pk_eula_required_new ();
		g_object_set (item,
			      "eula-id",
			      tmp_str[0],
			      "package-id",
			      tmp_str[1],
			      "vendor-name",
			      tmp_str[2],
			      "license-agreement",
			      tmp_str[3],
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_add_eula_required (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "RepoDetail") == 0) {
		g_autoptr(PkRepoDetail) item = NULL;
		g_variant_get (parameters, "(&s&sb)", &tmp_str[0], &tmp_str[1], &tmp_bool);
		item = pk_repo_detail_new ();
		g_object_set (item,
			      "repo-id",
			      tmp_str[0],
			      "description",
			      tmp_str[1],
			      "enabled",
			      tmp_bool,
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_add_repo_detail (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "ErrorCode") == 0) {
		g_autoptr(PkError) item = NULL;
		g_variant_get (parameters, "(u&s)", &tmp_uint, &tmp_str[1]);
		item = pk_error_new ();
		g_object_set (item,
			      "code",
			      tmp_uint,
			      "details",
			      tmp_str[1],
			      "role",
			      state->role,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_results_set_error_code (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "ItemProgress") == 0) {
		g_autoptr(PkItemProgress) item = NULL;
		g_variant_get (parameters, "(&suu)", &tmp_str[0], &tmp_uint, &tmp_uint2);
		item = pk_item_progress_new ();
		g_object_set (item,
			      "package-id",
			      tmp_str[0],
			      "status",
			      tmp_uint,
			      "percentage",
			      tmp_uint2,
			      "transaction-id",
			      state->transaction_id,
			      NULL);
		pk_progress_set_item_progress (state->progress, item);
		return;
	}
	if (g_strcmp0 (signal_name, "Destroy") == 0) {
		g_autoptr(GError) local_error = NULL;

		if (state->waiting_for_finished) {
			local_error = g_error_new_literal (PK_CLIENT_ERROR,
							   PK_CLIENT_ERROR_FAILED,
							   "PackageKit transaction disappeared");
			pk_client_state_finish (state, g_steal_pointer (&local_error));
		}
		return;
	}
}

static void
pk_client_notify_name_owner_cb (GObject *obj, GParamSpec *pspec, gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	g_autoptr(PkClientState) state = g_weak_ref_get (weak_ref);

	if (!state)
		return;

	if (state->waiting_for_finished) {
		g_autoptr(GError) local_error = NULL;

		local_error = g_error_new_literal (PK_CLIENT_ERROR,
						   PK_CLIENT_ERROR_FAILED,
						   "PackageKit daemon disappeared");
		pk_client_state_finish (state, g_steal_pointer (&local_error));
	} else {
		pk_client_state_unset_proxy (state);
		g_cancellable_cancel (state->cancellable);
	}
}

/*
 * pk_client_proxy_connect:
 **/
static void
pk_client_proxy_connect (PkClientState *state)
{
	guint i;
	g_auto(GStrv) props = NULL;

	/* coldplug properties */
	props = g_dbus_proxy_get_cached_property_names (state->proxy);
	for (i = 0; props != NULL && props[i] != NULL; i++) {
		g_autoptr(GVariant) value_tmp = NULL;
		value_tmp = g_dbus_proxy_get_cached_property (state->proxy, props[i]);
		pk_client_set_property_value (state, props[i], value_tmp);
	}

	/* connect up signals */
	g_signal_connect_data (state->proxy,
			       "g-properties-changed",
			       G_CALLBACK (pk_client_properties_changed_cb),
			       pk_client_weak_ref_new (state),
			       pk_client_weak_ref_free_gclosure,
			       0);
	g_signal_connect_data (state->proxy,
			       "g-signal",
			       G_CALLBACK (pk_client_signal_cb),
			       pk_client_weak_ref_new (state),
			       pk_client_weak_ref_free_gclosure,
			       0);
	g_signal_connect_data (state->proxy,
			       "notify::g-name-owner",
			       G_CALLBACK (pk_client_notify_name_owner_cb),
			       pk_client_weak_ref_new (state),
			       pk_client_weak_ref_free_gclosure,
			       0);
}

/*
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* wait for ::Finished() or ::Destroy() or notify::g-name-owner (if the daemon disappears) */
	state->waiting_for_finished = TRUE;
}

/*
 * pk_client_open_files:
 *
 * The daemon takes package files as fds, so it never has to resolve a path
 * as root that the caller may not be allowed to read.
 */
static GUnixFDList *
pk_client_open_files (gchar **files, GError **error)
{
	g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new ();

	for (guint i = 0; files[i] != NULL; i++) {
		gint fd = open (files[i], O_RDONLY | O_CLOEXEC);
		gint ret;

		if (fd < 0) {
			g_set_error (error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_INVALID_INPUT,
				     "could not open %s: %s",
				     files[i],
				     g_strerror (errno));
			return NULL;
		}
		ret = g_unix_fd_list_append (fd_list, fd, error);
		close (fd);
		if (ret < 0)
			return NULL;
	}
	return g_steal_pointer (&fd_list);
}

/*
 * pk_client_set_role:
 **/
static void
pk_client_set_role (PkClientState *state, PkRoleEnum role)
{
	pk_progress_set_transaction_flags (state->progress, state->transaction_flags);
	pk_progress_set_role (state->progress, role);
}

static void pk_client_call_role_method (PkClientState *state);
static void
pk_client_frontend_socket_cb (GObject *source_object, GAsyncResult *res, gpointer user_data);
static gboolean pk_client_frontend_helper_available (PkClientState *state);
static void pk_client_start_frontend_helper (PkClientState *state, GUnixFDList *fds, GVariant *value);

static gboolean
pk_client_role_needs_frontend (PkRoleEnum role)
{
	return role == PK_ROLE_ENUM_INSTALL_FILES || role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	       role == PK_ROLE_ENUM_REMOVE_PACKAGES || role == PK_ROLE_ENUM_UPDATE_PACKAGES;
}

/*
 * pk_client_set_hints_cb:
 **/
static void
pk_client_set_hints_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	PkClientPrivate *priv = GET_PRIVATE(state->client);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* Check again for cancellation, as it’s possible the `PkClientState`
	 * was cancelled and `pk_client_state_finish()` called asynchronously
	 * while we were waiting for the hints to be set. */
	if (g_cancellable_set_error_if_cancelled (state->cancellable_client, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	g_assert (!pk_client_state_is_finished (state));

	/* the backend may need a session helper to ask the user questions */
	if (pk_client_role_needs_frontend (state->role) && priv->interactive &&
	    !pk_bitfield_contain (state->transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE) &&
	    !pk_bitfield_contain (state->transaction_flags,
				  PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD) &&
	    pk_client_frontend_helper_available (state)) {
		g_dbus_proxy_call_with_unix_fd_list (state->proxy,
						     "GetFrontendSocket",
						     NULL,
						     G_DBUS_CALL_FLAGS_NONE,
						     PK_CLIENT_DBUS_METHOD_TIMEOUT,
						     NULL,
						     state->cancellable,
						     pk_client_frontend_socket_cb,
						     g_object_ref (state));
		return;
	}
	pk_client_call_role_method (state);
}

/*
 * pk_client_frontend_socket_cb:
 **/
static void
pk_client_frontend_socket_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) value = NULL;
	g_autoptr(GUnixFDList) fds = NULL;

	/* a transaction without a helper still works, questions just use defaults */
	value = g_dbus_proxy_call_with_unix_fd_list_finish (proxy, &fds, res, &error);
	if (value == NULL) {
		pk_client_fixup_dbus_error (error);
		if (g_error_matches (error, PK_CLIENT_ERROR, PK_CLIENT_ERROR_NOT_SUPPORTED))
			g_debug ("backend does not use a frontend socket");
		else
			g_warning ("failed to get frontend socket: %s", error->message);
		g_clear_error (&error);
	} else {
		pk_client_start_frontend_helper (state, fds, value);
	}

	if (g_cancellable_set_error_if_cancelled (state->cancellable_client, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}
	pk_client_call_role_method (state);
}

/*
 * pk_client_call_method:
 **/
static void
pk_client_call_role_method (PkClientState *state)
{
	g_autoptr(GError) error = NULL;

	/* we'll have results from now on */
	state->results = pk_results_new ();
	g_object_set (state->results,
		      "role",
		      state->role,
		      "progress",
		      state->progress,
		      "transaction-flags",
		      state->transaction_flags,
		      NULL);

	/* do this async, although this should be pretty fast anyway */
	if (state->role == PK_ROLE_ENUM_RESOLVE) {
		g_dbus_proxy_call (state->proxy,
				   "Resolve",
				   g_variant_new ("(t^a&s)", state->filters, state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_NAME) {
		g_dbus_proxy_call (state->proxy,
				   "SearchNames",
				   g_variant_new ("(t^a&s)", state->filters, state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		g_dbus_proxy_call (state->proxy,
				   "SearchDetails",
				   g_variant_new ("(t^a&s)", state->filters, state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		g_dbus_proxy_call (state->proxy,
				   "SearchGroups",
				   g_variant_new ("(t^a&s)", state->filters, state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		g_dbus_proxy_call (state->proxy,
				   "SearchFiles",
				   g_variant_new ("(t^a&s)", state->filters, state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		g_dbus_proxy_call (state->proxy,
				   "GetDetails",
				   g_variant_new ("(^a&s)", state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS_LOCAL) {
		g_autoptr(GUnixFDList) fd_list = pk_client_open_files (state->files, &error);
		if (fd_list == NULL) {
			pk_client_state_finish (state, g_steal_pointer (&error));
			return;
		}
		g_dbus_proxy_call_with_unix_fd_list (
		    state->proxy,
		    "GetDetailsLocal",
		    g_variant_new ("(@ah)", pk_unix_fd_list_get_handles (fd_list)),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    fd_list,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->files), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_FILES_LOCAL) {
		g_autoptr(GUnixFDList) fd_list = pk_client_open_files (state->files, &error);
		if (fd_list == NULL) {
			pk_client_state_finish (state, g_steal_pointer (&error));
			return;
		}
		g_dbus_proxy_call_with_unix_fd_list (
		    state->proxy,
		    "GetFilesLocal",
		    g_variant_new ("(@ah)", pk_unix_fd_list_get_handles (fd_list)),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    fd_list,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->files), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		g_dbus_proxy_call (state->proxy,
				   "GetUpdateDetail",
				   g_variant_new ("(^a&s)", state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_OLD_TRANSACTIONS) {
		g_dbus_proxy_call (state->proxy,
				   "GetOldTransactions",
				   g_variant_new ("(u)", state->number),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		g_dbus_proxy_call (
		    state->proxy,
		    "DownloadPackages",
		    g_variant_new ("(b^a&s)", (state->directory == NULL), state->package_ids),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATES) {
		g_dbus_proxy_call (state->proxy,
				   "GetUpdates",
				   g_variant_new ("(t)", state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_DEPENDS_ON) {
		g_dbus_proxy_call (state->proxy,
				   "DependsOn",
				   g_variant_new ("(t^a&sb)",
						  state->filters,
						  state->package_ids,
						  state->recursive),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);

	} else if (state->role == PK_ROLE_ENUM_REQUIRED_BY) {
		g_dbus_proxy_call (state->proxy,
				   "RequiredBy",
				   g_variant_new ("(t^a&sb)",
						  state->filters,
						  state->package_ids,
						  state->recursive),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_PACKAGES) {
		g_dbus_proxy_call (state->proxy,
				   "GetPackages",
				   g_variant_new ("(t)", state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		g_dbus_proxy_call (state->proxy,
				   "WhatProvides",
				   g_variant_new ("(t^a&s)", state->filters, state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		g_dbus_proxy_call (state->proxy,
				   "GetDistroUpgrades",
				   NULL,
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_FILES) {
		g_dbus_proxy_call (state->proxy,
				   "GetFiles",
				   g_variant_new ("(^a&s)", state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		g_dbus_proxy_call (state->proxy,
				   "RemovePackages",
				   g_variant_new ("(t^a&sbb)",
						  state->transaction_flags,
						  state->package_ids,
						  state->allow_deps,
						  state->autoremove),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		g_dbus_proxy_call (state->proxy,
				   "RefreshCache",
				   g_variant_new ("(b)", state->force),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		g_dbus_proxy_call (
		    state->proxy,
		    "InstallPackages",
		    g_variant_new ("(t^a&s)", state->transaction_flags, state->package_ids),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_SIGNATURE) {
		g_dbus_proxy_call (
		    state->proxy,
		    "InstallSignature",
		    g_variant_new ("(uss)", state->type, state->key_id, state->package_id),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		g_dbus_proxy_call (
		    state->proxy,
		    "UpdatePackages",
		    g_variant_new ("(t^a&s)", state->transaction_flags, state->package_ids),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		g_autoptr(GUnixFDList) fd_list = pk_client_open_files (state->files, &error);
		if (fd_list == NULL) {
			pk_client_state_finish (state, g_steal_pointer (&error));
			return;
		}
		g_dbus_proxy_call_with_unix_fd_list (
		    state->proxy,
		    "InstallFiles",
		    g_variant_new ("(t@ah)",
				   state->transaction_flags,
				   pk_unix_fd_list_get_handles (fd_list)),
		    G_DBUS_CALL_FLAGS_NONE,
		    PK_CLIENT_DBUS_METHOD_TIMEOUT,
		    fd_list,
		    state->cancellable,
		    pk_client_method_cb,
		    g_object_ref (state));
		g_object_set (state->results, "inputs", g_strv_length (state->files), NULL);
	} else if (state->role == PK_ROLE_ENUM_ACCEPT_EULA) {
		g_dbus_proxy_call (state->proxy,
				   "AcceptEula",
				   g_variant_new ("(s)", state->eula_id),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		g_dbus_proxy_call (state->proxy,
				   "GetRepoList",
				   g_variant_new ("(t)", state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REPO_ENABLE) {
		g_dbus_proxy_call (state->proxy,
				   "RepoEnable",
				   g_variant_new ("(sb)", state->repo_id, state->enabled),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		g_dbus_proxy_call (state->proxy,
				   "RepoSetData",
				   g_variant_new ("(sss)",
						  state->repo_id,
						  state->parameter ? state->parameter : "",
						  state->value ? state->value : ""),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REPO_REMOVE) {
		g_dbus_proxy_call (state->proxy,
				   "RepoRemove",
				   g_variant_new ("(tsb)",
						  state->transaction_flags,
						  state->repo_id,
						  state->autoremove),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		g_dbus_proxy_call (state->proxy,
				   "UpgradeSystem",
				   g_variant_new ("(tsu)",
						  state->transaction_flags,
						  state->distro_id,
						  state->upgrade_kind),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		g_dbus_proxy_call (state->proxy,
				   "RepairSystem",
				   g_variant_new ("(t)", state->transaction_flags),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else {
		g_assert_not_reached ();
	}
}

/*
 * pk_client_frontend_available:
 **/
static gboolean
pk_client_frontend_helper_available (PkClientState *state)
{
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) envp = NULL;

	return pk_client_helper_create_argv_envp (&argv, &envp);
}

/*
 * pk_client_start_frontend:
 *
 * Gets the questions on the socket the daemon returned from
 * GetFrontendSocket answered, by the application if it wants to.
 **/
static void
pk_client_start_frontend_helper (PkClientState *state, GUnixFDList *fds, GVariant *value)
{
	gint handle;
	gint fd;
	gboolean handled = FALSE;
	g_autoptr(GSocket) socket = NULL;
	g_autoptr(GError) error = NULL;

	g_variant_get (value, "(h)", &handle);
	fd = fds != NULL ? g_unix_fd_list_get (fds, handle, &error) : -1;
	if (fd < 0) {
		g_warning ("no frontend socket received: %s",
			   error != NULL ? error->message : "no fds");
		return;
	}
	socket = g_socket_new_from_fd (fd, &error);
	if (socket == NULL) {
		g_warning ("invalid frontend socket: %s", error->message);
		close (fd);
		return;
	}

	g_signal_emit (state->client, signals[SIGNAL_FRONTEND_SOCKET], 0, socket, &handled);
	if (handled)
		return;

	state->client_helper = pk_client_helper_new ();
	if (!pk_client_helper_start (state->client_helper, socket, NULL, NULL, &error))
		g_warning ("failed to start frontend helper: %s", error->message);
}

/*
 * pk_client_get_proxy_cb:
 **/
static void
pk_client_get_proxy_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	g_autoptr(GError) error = NULL;
	g_auto(GVariantBuilder) hints = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("a{sv}"));
	PkClientPrivate *priv = pk_client_get_instance_private (state->client);

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		g_debug ("Cannot connect to PackageKit on %s", state->tid);
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* Check again for cancellation, as it’s possible the `PkClientState`
	 * was cancelled and `pk_client_state_finish()` called asynchronously
	 * while we were waiting for the proxy to be constructed. */
	if (g_cancellable_set_error_if_cancelled (state->cancellable_client, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	g_assert (!pk_client_state_is_finished (state));

	/* connect */
	pk_client_proxy_connect (state);

	/* get hints */
	if (priv->locale != NULL)
		g_variant_builder_add (&hints,
				       "{sv}",
				       "locale",
				       g_variant_new_string (priv->locale));
	g_variant_builder_add (&hints,
			       "{sv}",
			       "background",
			       g_variant_new_boolean (priv->background));
	g_variant_builder_add (&hints,
			       "{sv}",
			       "interactive",
			       g_variant_new_boolean (priv->interactive));
	if (priv->cache_age > 0)
		g_variant_builder_add (&hints,
				       "{sv}",
				       "cache-age",
				       g_variant_new_uint32 (priv->cache_age));

	/* set hints */
	g_dbus_proxy_call (state->proxy,
			   "SetHints",
			   g_variant_new ("(a{sv})", &hints),
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CLIENT_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_client_set_hints_cb,
			   g_object_ref (state));
}

/*
 * pk_client_get_tid_cb:
 **/
static void
pk_client_get_tid_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	PkControl *control = PK_CONTROL (object);
	g_autoptr(GError) error = NULL;

	state->tid = pk_control_get_tid_finish (control, res, &error);
	if (state->tid == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* Check for cancellation, as it’s possible the `PkClientState` was
	 * cancelled and `pk_client_state_finish()` called asynchronously while
	 * we were waiting for a TID. */
	if (g_cancellable_set_error_if_cancelled (state->cancellable_client, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	g_assert (!pk_client_state_is_finished (state));

	pk_progress_set_transaction_id (state->progress, state->tid);

	/* get a connection to the transaction interface */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_NONE,
				  NULL,
				  PK_DBUS_SERVICE,
				  state->tid,
				  PK_DBUS_INTERFACE_TRANSACTION,
				  state->cancellable,
				  pk_client_get_proxy_cb,
				  g_object_ref (state));
}

/**
 * pk_client_generic_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: (transfer full): the #PkResults, or %NULL. Free with g_object_unref()
 *
 * Since: 0.5.2
 **/
PkResults *
pk_client_generic_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/**
 * pk_client_resolve_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @packages: (array zero-terminated=1): an array of package names to resolve, e.g. "gnome-system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Resolve a package name into a @package_id. This can return installed and
 * available packages and allows you find out if a package is installed locally
 * or is available in a repository.
 *
 * Since: 0.5.2
 **/
void
pk_client_resolve_async (PkClient *client,
			 PkBitfield filters,
			 gchar **packages,
			 GCancellable *cancellable,
			 PkProgressCallback progress_callback,
			 gpointer progress_user_data,
			 GAsyncReadyCallback callback_ready,
			 gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_resolve_async,
				     PK_ROLE_ENUM_RESOLVE,
				     cancellable);
	state->filters = filters;
	state->package_ids = g_strdupv (packages);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_search_names_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search all the locally installed files and remote repositories for a package
 * that matches a specific name.
 *
 * Since: 0.5.5
 **/
void
pk_client_search_names_async (PkClient *client,
			      PkBitfield filters,
			      gchar **values,
			      GCancellable *cancellable,
			      PkProgressCallback progress_callback,
			      gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready,
			      gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_search_names_async,
				     PK_ROLE_ENUM_SEARCH_NAME,
				     cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_search_details_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search all detailed summary information to try and find a keyword.
 * Think of this as pk_client_search_names(), but trying much harder and
 * taking longer.
 *
 * Since: 0.5.5
 **/
void
pk_client_search_details_async (PkClient *client,
				PkBitfield filters,
				gchar **values,
				GCancellable *cancellable,
				PkProgressCallback progress_callback,
				gpointer progress_user_data,
				GAsyncReadyCallback callback_ready,
				gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_search_details_async,
				     PK_ROLE_ENUM_SEARCH_DETAILS,
				     cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_search_groups_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): a group enum to search for, for instance, "system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Return all packages in a specific group.
 *
 * Since: 0.5.5
 **/
void
pk_client_search_groups_async (PkClient *client,
			       PkBitfield filters,
			       gchar **values,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_search_groups_async,
				     PK_ROLE_ENUM_SEARCH_GROUP,
				     cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_search_files_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): file to search for, for instance, "/sbin/service"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search for packages that provide a specific file.
 *
 * Since: 0.5.5
 **/
void
pk_client_search_files_async (PkClient *client,
			      PkBitfield filters,
			      gchar **values,
			      GCancellable *cancellable,
			      PkProgressCallback progress_callback,
			      gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready,
			      gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_search_files_async,
				     PK_ROLE_ENUM_SEARCH_FILE,
				     cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_details_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get details of a package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_details_async (PkClient *client,
			     gchar **package_ids,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_details_async,
				     PK_ROLE_ENUM_GET_DETAILS,
				     cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_details_local_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @files: (array zero-terminated=1): a null terminated array of filenames
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get details of a package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Since: 0.8.17
 **/
void
pk_client_get_details_local_async (PkClient *client,
				   gchar **files,
				   GCancellable *cancellable,
				   PkProgressCallback progress_callback,
				   gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready,
				   gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_details_local_async,
				     PK_ROLE_ENUM_GET_DETAILS_LOCAL,
				     cancellable);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	state->files = pk_client_convert_real_paths (files, &error);
	if (state->files == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_files_local_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @files: (array zero-terminated=1): a null terminated array of filenames
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get file list of a package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Since: 0.9.1
 **/
void
pk_client_get_files_local_async (PkClient *client,
				 gchar **files,
				 GCancellable *cancellable,
				 PkProgressCallback progress_callback,
				 gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready,
				 gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_files_local_async,
				     PK_ROLE_ENUM_GET_FILES_LOCAL,
				     cancellable);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);
	state->files = pk_client_convert_real_paths (files, &error);
	if (state->files == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_update_detail_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get details about the specific update, for instance any CVE urls and
 * severity information.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_update_detail_async (PkClient *client,
				   gchar **package_ids,
				   GCancellable *cancellable,
				   PkProgressCallback progress_callback,
				   gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready,
				   gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_update_detail_async,
				     PK_ROLE_ENUM_GET_UPDATE_DETAIL,
				     cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_download_packages_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @directory: the location where packages are to be downloaded
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Downloads package files to a specified location.
 *
 * Since: 0.5.2
 **/
void
pk_client_download_packages_async (PkClient *client,
				   gchar **package_ids,
				   const gchar *directory,
				   GCancellable *cancellable,
				   PkProgressCallback progress_callback,
				   gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready,
				   gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_download_packages_async,
				     PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
				     cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_updates_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_DEVELOPMENT or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get a list of all the packages that can be updated for all repositories.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_updates_async (PkClient *client,
			     PkBitfield filters,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_updates_async,
				     PK_ROLE_ENUM_GET_UPDATES,
				     cancellable);
	state->filters = filters;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_old_transactions_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @number: the number of past transactions to return, or 0 for all
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the old transaction list, mainly used for the transaction viewer.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_old_transactions_async (PkClient *client,
				      guint number,
				      GCancellable *cancellable,
				      PkProgressCallback progress_callback,
				      gpointer progress_user_data,
				      GAsyncReadyCallback callback_ready,
				      gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_old_transactions_async,
				     PK_ROLE_ENUM_GET_OLD_TRANSACTIONS,
				     cancellable);
	state->number = number;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_depends_on_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @recursive: If we should search recursively for depends
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the packages that depend this one, i.e. child->parent.
 *
 * Since: 0.5.2
 **/
void
pk_client_depends_on_async (PkClient *client,
			    PkBitfield filters,
			    gchar **package_ids,
			    gboolean recursive,
			    GCancellable *cancellable,
			    PkProgressCallback progress_callback,
			    gpointer progress_user_data,
			    GAsyncReadyCallback callback_ready,
			    gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_depends_on_async,
				     PK_ROLE_ENUM_DEPENDS_ON,
				     cancellable);
	state->filters = filters;
	state->recursive = recursive;
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_packages_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the list of packages from the backend
 *
 * Since: 0.5.2
 **/
void
pk_client_get_packages_async (PkClient *client,
			      PkBitfield filters,
			      GCancellable *cancellable,
			      PkProgressCallback progress_callback,
			      gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready,
			      gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_packages_async,
				     PK_ROLE_ENUM_GET_PACKAGES,
				     cancellable);
	state->filters = filters;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_required_by_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @recursive: If we should search recursively for requires
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the packages that require this one, i.e. parent->child.
 *
 * Since: 0.5.2
 **/
void
pk_client_required_by_async (PkClient *client,
			     PkBitfield filters,
			     gchar **package_ids,
			     gboolean recursive,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_required_by_async,
				     PK_ROLE_ENUM_REQUIRED_BY,
				     cancellable);
	state->recursive = recursive;
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_what_provides_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): a search term such as "sound/mp3"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * This should return packages that provide the supplied attributes.
 * This method is useful for finding out what package(s) provide a modalias
 * or GStreamer codec string.
 *
 * Since: 0.5.2
 **/
void
pk_client_what_provides_async (PkClient *client,
			       PkBitfield filters,
			       gchar **values,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_what_provides_async,
				     PK_ROLE_ENUM_WHAT_PROVIDES,
				     cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_distro_upgrades_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * This method should return a list of distribution upgrades that are available.
 * It should not return updates, only major upgrades.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_distro_upgrades_async (PkClient *client,
				     GCancellable *cancellable,
				     PkProgressCallback progress_callback,
				     gpointer progress_user_data,
				     GAsyncReadyCallback callback_ready,
				     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_distro_upgrades_async,
				     PK_ROLE_ENUM_GET_DISTRO_UPGRADES,
				     cancellable);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_files_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the file list (i.e. a list of files installed) for the specified package.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_files_async (PkClient *client,
			   gchar **package_ids,
			   GCancellable *cancellable,
			   PkProgressCallback progress_callback,
			   gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready,
			   gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_files_async,
				     PK_ROLE_ENUM_GET_FILES,
				     cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_remove_packages_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @allow_deps: if other dependent packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Remove a package (optionally with dependancies) from the system.
 * If @allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
 *
 * Since: 0.8.1
 **/
void
pk_client_remove_packages_async (PkClient *client,
				 PkBitfield transaction_flags,
				 gchar **package_ids,
				 gboolean allow_deps,
				 gboolean autoremove,
				 GCancellable *cancellable,
				 PkProgressCallback progress_callback,
				 gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready,
				 gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_remove_packages_async,
				     PK_ROLE_ENUM_REMOVE_PACKAGES,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_refresh_cache_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @force: if we should aggressively drop caches
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Refresh the cache, i.e. download new metadata from a remote URL so that
 * package lists are up to date.
 * This action may take a few minutes and should be done when the session and
 * system are idle.
 *
 * Since: 0.5.2
 **/
void
pk_client_refresh_cache_async (PkClient *client,
			       gboolean force,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_refresh_cache_async,
				     PK_ROLE_ENUM_REFRESH_CACHE,
				     cancellable);
	state->force = force;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_install_packages_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a package of the newest and most correct version.
 *
 * Since: 0.8.1
 **/
void
pk_client_install_packages_async (PkClient *client,
				  PkBitfield transaction_flags,
				  gchar **package_ids,
				  GCancellable *cancellable,
				  PkProgressCallback progress_callback,
				  gpointer progress_user_data,
				  GAsyncReadyCallback callback_ready,
				  gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_install_packages_async,
				     PK_ROLE_ENUM_INSTALL_PACKAGES,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_install_signature_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @type: the signature type, e.g. %PK_SIGTYPE_ENUM_GPG
 * @key_id: a key ID such as "0df23df"
 * @package_id: a signature_id structure such as "hal;0.0.1;i386;fedora;"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a software repository signature of the newest and most correct version.
 *
 * Since: 0.5.2
 **/
void
pk_client_install_signature_async (PkClient *client,
				   PkSigTypeEnum type,
				   const gchar *key_id,
				   const gchar *package_id,
				   GCancellable *cancellable,
				   PkProgressCallback progress_callback,
				   gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready,
				   gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_install_signature_async,
				     PK_ROLE_ENUM_INSTALL_SIGNATURE,
				     cancellable);
	state->type = type;
	state->key_id = g_strdup (key_id);
	state->package_id = g_strdup (package_id);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_update_packages_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora;"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Update specific packages to the newest available versions.
 *
 * Since: 0.8.1
 **/
void
pk_client_update_packages_async (PkClient *client,
				 PkBitfield transaction_flags,
				 gchar **package_ids,
				 GCancellable *cancellable,
				 PkProgressCallback progress_callback,
				 gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready,
				 gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_update_packages_async,
				     PK_ROLE_ENUM_UPDATE_PACKAGES,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->package_ids = g_strdupv (package_ids);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_install_files_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @files: (array zero-terminated=1): a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Since: 0.8.1
 **/
void
pk_client_install_files_async (PkClient *client,
			       PkBitfield transaction_flags,
			       gchar **files,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_install_files_async,
				     PK_ROLE_ENUM_INSTALL_FILES,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* check files are valid */
	state->files = pk_client_convert_real_paths (files, &error);
	if (state->files == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_accept_eula_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @eula_id: the `eula_id` we are agreeing to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * We may want to agree to a EULA dialog if one is presented.
 *
 * Since: 0.5.2
 **/
void
pk_client_accept_eula_async (PkClient *client,
			     const gchar *eula_id,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_accept_eula_async,
				     PK_ROLE_ENUM_ACCEPT_EULA,
				     cancellable);
	state->eula_id = g_strdup (eula_id);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_get_repo_list_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_DEVELOPMENT or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the list of repositories installed on the system.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_repo_list_async (PkClient *client,
			       PkBitfield filters,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_repo_list_async,
				     PK_ROLE_ENUM_GET_REPO_LIST,
				     cancellable);
	state->filters = filters;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_repo_enable_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @enabled: if we should enable the repository
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Enable or disable the repository.
 *
 * Since: 0.5.2
 **/
void
pk_client_repo_enable_async (PkClient *client,
			     const gchar *repo_id,
			     gboolean enabled,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_repo_enable_async,
				     PK_ROLE_ENUM_REPO_ENABLE,
				     cancellable);
	state->enabled = enabled;
	state->repo_id = g_strdup (repo_id);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_repo_set_data_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @parameter: the parameter to change
 * @value: what we should change it to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * We may want to set a repository parameter.
 * NOTE: this is free text, and is left to the backend to define a format.
 *
 * Since: 0.5.2
 **/
void
pk_client_repo_set_data_async (PkClient *client,
			       const gchar *repo_id,
			       const gchar *parameter,
			       const gchar *value,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_repo_set_data_async,
				     PK_ROLE_ENUM_REPO_SET_DATA,
				     cancellable);
	state->repo_id = g_strdup (repo_id);
	state->parameter = g_strdup (parameter);
	state->value = g_strdup (value);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_repo_remove_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: transaction flags
 * @repo_id: a repo_id structure such as "livna-devel"
 * @autoremove: If packages should be auto-removed
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Removes a repo and optionally the packages installed from it.
 *
 * Since: 0.9.1
 **/
void
pk_client_repo_remove_async (PkClient *client,
			     PkBitfield transaction_flags,
			     const gchar *repo_id,
			     gboolean autoremove,
			     GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready,
			     gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_repo_remove_async,
				     PK_ROLE_ENUM_REPO_REMOVE,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->repo_id = g_strdup (repo_id);
	state->autoremove = autoremove;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_upgrade_system_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @distro_id: a distro ID such as "fedora-14"
 * @upgrade_kind: a #PkUpgradeKindEnum such as %PK_UPGRADE_KIND_ENUM_COMPLETE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * This transaction will update the distro to the next version, which may
 * involve just downloading the installer and setting up the boot device,
 * or may involve doing an on-line upgrade.
 *
 * The backend will decide what is best to do.
 *
 * Since: 1.0.10
 **/
void
pk_client_upgrade_system_async (PkClient *client,
				PkBitfield transaction_flags,
				const gchar *distro_id,
				PkUpgradeKindEnum upgrade_kind,
				GCancellable *cancellable,
				PkProgressCallback progress_callback,
				gpointer progress_user_data,
				GAsyncReadyCallback callback_ready,
				gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_upgrade_system_async,
				     PK_ROLE_ENUM_UPGRADE_SYSTEM,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->distro_id = g_strdup (distro_id);
	state->upgrade_kind = upgrade_kind;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**
 * pk_client_repair_system_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * This transaction will try to recover from a broken package management system:
 * e.g. the installation of a package with unsatisfied dependencies has
 * been forced by the user using a low level tool (rpm or dpkg) or the
 * system was shutdown during processing an installation.
 *
 * The backend will decide what is best to do.
 *
 * Since: 0.8.1
 **/
void
pk_client_repair_system_async (PkClient *client,
			       PkBitfield transaction_flags,
			       GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready,
			       gpointer user_data)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_repair_system_async,
				     PK_ROLE_ENUM_REPAIR_SYSTEM,
				     cancellable);
	state->transaction_flags = transaction_flags;
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  g_steal_pointer (&state));
}

/**********************************************************************/

/*
 * pk_client_adopt_get_proxy_cb:
 **/
static void
pk_client_adopt_get_proxy_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* connect */
	pk_client_proxy_connect (state);
}

/**
 * pk_client_adopt_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @transaction_id: a transaction ID such as "/21_ebcbdaae_data"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Adopt a transaction which allows the caller to monitor the state or cancel it.
 *
 * Since: 0.5.2
 **/
void
pk_client_adopt_async (PkClient *client,
		       const gchar *transaction_id,
		       GCancellable *cancellable,
		       PkProgressCallback progress_callback,
		       gpointer progress_user_data,
		       GAsyncReadyCallback callback_ready,
		       gpointer user_data)
{
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_adopt_async,
				     PK_ROLE_ENUM_UNKNOWN,
				     cancellable);
	state->tid = g_strdup (transaction_id);
	state->progress = pk_progress_new_with_callback (progress_callback, progress_user_data);
	state->results = pk_results_new ();
	g_object_set (state->results, "role", state->role, "progress", state->progress, NULL);

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_client_set_role (state, state->role);
	pk_progress_set_transaction_id (state->progress, state->tid);

	/* get a connection to the transaction interface */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_NONE,
				  NULL,
				  PK_DBUS_SERVICE,
				  state->tid,
				  PK_DBUS_INTERFACE_TRANSACTION,
				  state->cancellable,
				  pk_client_adopt_get_proxy_cb,
				  g_object_ref (state));
}

/**********************************************************************/

/**
 * pk_client_get_progress_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: (transfer full): the #PkProgress, or %NULL. Free with g_object_unref()
 *
 * Since: 0.5.2
 **/
PkProgress *
pk_client_get_progress_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/*
 * pk_client_get_progress_cb:
 **/
static void
pk_client_get_progress_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* connect */
	pk_client_proxy_connect (state);

	state->ret = TRUE;
	pk_client_state_finish (state, NULL);
}

/**
 * pk_client_get_progress_async:
 * @client: a valid #PkClient instance
 * @transaction_id: a transaction ID such as "/21_ebcbdaae_data"
 * @cancellable: a #GCancellable or %NULL
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Find the current state of a transaction.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_progress_async (PkClient *client,
			      const gchar *transaction_id,
			      GCancellable *cancellable,
			      GAsyncReadyCallback callback_ready,
			      gpointer user_data)
{
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client,
				     callback_ready,
				     user_data,
				     pk_client_get_progress_async,
				     PK_ROLE_ENUM_UNKNOWN,
				     cancellable);
	state->tid = g_strdup (transaction_id);
	state->progress = pk_progress_new ();
	state->querying_progress = TRUE;

	/* check not already cancelled */
	if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* identify */
	pk_progress_set_transaction_id (state->progress, state->tid);

	/* get a connection to the transaction interface */
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_NONE,
				  NULL,
				  PK_DBUS_SERVICE,
				  state->tid,
				  PK_DBUS_INTERFACE_TRANSACTION,
				  state->cancellable,
				  pk_client_get_progress_cb,
				  g_object_ref (state));
}

/**********************************************************************/

/*
 * pk_client_cancel_all_dbus_methods:
 **/
static gboolean
pk_client_cancel_all_dbus_methods (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);
	const PkClientState *state;
	guint i;
	g_autoptr(GPtrArray) array = NULL;

	/* just cancel the call; cancelling is synchronous so this should result
	 * in pk_client_state_remove() being called, which will modify the array,
	 * so take a copy of the array first */
	array = g_ptr_array_new_from_array (priv->calls->pdata, priv->calls->len, NULL, NULL, NULL);
	for (i = 0; i < array->len; i++) {
		state = g_ptr_array_index (array, i);
		if (state->proxy == NULL)
			continue;
		g_debug ("cancel in flight call");
		g_cancellable_cancel (state->cancellable);
	}

	return TRUE;
}

/**
 * pk_client_set_locale:
 * @client: a valid #PkClient instance
 * @locale: the locale to set, e.g. "en_GB.UTF-8"
 *
 * Sets the locale to be used for the client. This may affect returned
 * results.
 *
 * Since: 0.6.10
 **/
void
pk_client_set_locale (PkClient *client, const gchar *locale)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_if_fail (PK_IS_CLIENT (client));

	if (g_strcmp0 (priv->locale, locale) == 0)
		return;

	g_free (priv->locale);
	priv->locale = g_strdup (locale);
	g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_LOCALE]);
}

/**
 * pk_client_get_locale:
 * @client: a valid #PkClient instance
 *
 * Gets the locale used for this transaction.
 *
 * Return value: The locale.
 *
 * Since: 0.6.10
 **/
const gchar *
pk_client_get_locale (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);

	return priv->locale;
}

/**
 * pk_client_set_background:
 * @client: a valid #PkClient instance
 * @background: if the transaction is a background transaction
 *
 * Sets the background value for the client. A background transaction
 * is usually scheduled at a lower priority and is usually given less
 * network and disk performance.
 *
 * Since: 0.6.10
 **/
void
pk_client_set_background (PkClient *client, gboolean background)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_if_fail (PK_IS_CLIENT (client));

	if (priv->background == background)
		return;

	priv->background = background;
	g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_BACKGROUND]);
}

/**
 * pk_client_get_background:
 * @client: a valid #PkClient instance
 *
 * Gets the background value.
 *
 * Return value: The background status.
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_get_background (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return priv->background;
}

/**
 * pk_client_set_interactive:
 * @client: a valid #PkClient instance
 * @interactive: the value to set
 *
 * Sets the interactive value for the client. Interactive transactions
 * are usually allowed to ask the user questions.
 *
 * Since: 0.6.10
 **/
void
pk_client_set_interactive (PkClient *client, gboolean interactive)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_if_fail (PK_IS_CLIENT (client));

	if (priv->interactive == interactive)
		return;

	priv->interactive = interactive;
	g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_INTERACTIVE]);
}

/**
 * pk_client_get_interactive:
 * @client: a valid #PkClient instance
 *
 * Gets the client interactive value.
 *
 * Return value: if the transaction is due to run interactivly.
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_get_interactive (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return priv->interactive;
}

/**
 * pk_client_get_idle:
 * @client: a valid #PkClient instance
 *
 * Gets if the transaction client idle value.
 *
 * Return value: if this client is idle.
 *
 * Since: 0.6.10
 **/
gboolean
pk_client_get_idle (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return (priv->calls->len == 0);
}

/**
 * pk_client_set_cache_age:
 * @client: a valid #PkClient instance
 * @cache_age: the cache age to set in seconds, where %G_MAXUINT
 * means cache "never expires"
 *
 * Sets the maximum cache age value for the client.
 *
 * Since: 0.6.10
 **/
void
pk_client_set_cache_age (PkClient *client, guint cache_age)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_if_fail (PK_IS_CLIENT (client));

	if (priv->cache_age == cache_age)
		return;

	priv->cache_age = cache_age;
	g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_CACHE_AGE]);
}

/**
 * pk_client_get_cache_age:
 * @client: a valid #PkClient instance
 *
 * Gets the maximum cache age value.
 *
 * Return value: The cache age in seconds
 *
 * Since: 0.6.10
 **/
guint
pk_client_get_cache_age (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return priv->cache_age;
}

/*
 * pk_client_class_init:
 **/
static void
pk_client_class_init (PkClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_client_finalize;
	object_class->get_property = pk_client_get_property;
	object_class->set_property = pk_client_set_property;

	/**
	 * PkClient:locale:
	 *
	 * Since: 0.5.3
	 */
	obj_properties[PROP_LOCALE] = g_param_spec_string ("locale",
							   NULL,
							   NULL,
							   NULL,
							   G_PARAM_READWRITE |
							       G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:background:
	 *
	 * Since: 0.5.3
	 */
	obj_properties[PROP_BACKGROUND] = g_param_spec_boolean ("background",
								NULL,
								NULL,
								FALSE,
								G_PARAM_READWRITE |
								    G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:interactive:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_INTERACTIVE] = g_param_spec_boolean ("interactive",
								 NULL,
								 NULL,
								 TRUE,
								 G_PARAM_READWRITE |
								     G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:idle:
	 *
	 * Whether there are transactions in progress on this client or not
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_IDLE] = g_param_spec_boolean ("idle",
							  NULL,
							  NULL,
							  TRUE,
							  G_PARAM_READABLE |
							      G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:cache-age:
	 *
	 * The cache age in seconds, where %G_MAXUINT means cache
	 * "never expires"
	 *
	 * Since: 0.6.10
	 */
	obj_properties[PROP_CACHE_AGE] = g_param_spec_uint ("cache-age",
							    NULL,
							    NULL,
							    0,
							    G_MAXUINT,
							    G_MAXUINT,
							    G_PARAM_READWRITE |
								G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, PROP_LAST, obj_properties);

	/**
	 * PkClient::frontend-socket:
	 * @client: the #PkClient
	 * @socket: a listening Unix stream socket
	 *
	 * Emitted when the daemon offers a socket on which the backend asks the
	 * user questions during an interactive transaction, e.g. debconf
	 * questions of maintainer scripts. Each connection is one question
	 * session in the debconf passthrough protocol.
	 *
	 * A handler that returns %TRUE takes over @socket and answers the
	 * questions itself, or closes it to leave them unanswered. Otherwise
	 * they are answered by the session's agent if one is running, or by a
	 * frontend program spawned for each connection.
	 *
	 * Returns: %TRUE if the handler took over the socket
	 *
	 * Since: 2.0.0
	 */
	signals[SIGNAL_FRONTEND_SOCKET] = g_signal_new (
	    "frontend-socket",
	    G_TYPE_FROM_CLASS (object_class),
	    G_SIGNAL_RUN_LAST,
	    G_STRUCT_OFFSET (PkClientClass, frontend_socket),
	    g_signal_accumulator_true_handled,
	    NULL,
	    NULL,
	    G_TYPE_BOOLEAN,
	    1,
	    G_TYPE_SOCKET);
}

/*
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	PkClientPrivate *priv = GET_PRIVATE(client);

	client->priv = priv;
	priv->calls = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->background = FALSE;
	priv->interactive = TRUE;
	priv->cache_age = G_MAXUINT;

	/* use a control object */
	priv->control = pk_control_new ();

	/* cache locale */
	priv->locale = g_strdup (setlocale (LC_MESSAGES, NULL));
}

/*
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = GET_PRIVATE(client);

	/* ensure we cancel any in-flight D-Bus calls */
	pk_client_cancel_all_dbus_methods (client);

	g_clear_pointer (&priv->locale, g_free);
	g_clear_object (&priv->control);

	g_assert (priv->calls->len == 0);
	g_clear_pointer (&priv->calls, g_ptr_array_unref);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 *
 * #PkClient is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new #PkClient instance
 *
 * Since: 0.5.2
 **/
PkClient *
pk_client_new (void)
{
	PkClient *client;
	client = g_object_new (PK_TYPE_CLIENT, NULL);
	return PK_CLIENT (client);
}
