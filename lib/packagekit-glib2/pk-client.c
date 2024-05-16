/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2014 Richard Hughes <richard@hughsie.com>
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
 * SECTION:pk-client
 * @short_description: For creating new transactions
 *
 * A GObject to use for accessing PackageKit asynchronously. If you're
 * using #PkClient to install, remove, or update packages, be prepared that
 * the eula, gpg and trusted callbacks need to be rescheduled manually, as in
 * https://www.freedesktop.org/software/PackageKit/gtk-doc/introduction-ideas-transactions.html
 */

#include "config.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <locale.h>
#include <stdlib.h>

#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-client-helper.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-debug.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>

static void     pk_client_finalize	(GObject     *object);

#define PK_CLIENT_DBUS_METHOD_TIMEOUT	G_MAXINT /* ms */

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct _PkClientPrivate
{
	GDBusConnection		*connection;
	GPtrArray		*calls;
	PkControl		*control;
	gchar			*locale;
	gboolean		 background;
	gboolean		 interactive;
	gboolean		 idle;
	gboolean		 details_with_deps_size;
	guint			 cache_age;
};

enum {
	PROP_0,
	PROP_LOCALE,
	PROP_BACKGROUND,
	PROP_INTERACTIVE,
	PROP_IDLE,
	PROP_CACHE_AGE,
	PROP_DETAILS_WITH_DEPS_SIZE,
	PROP_LAST
};

static GParamSpec *obj_properties[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (PkClient, pk_client, G_TYPE_OBJECT)

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
pk_client_weak_ref_free_gclosure (gpointer ptr,
				  GClosure *closure)
{
	pk_client_weak_ref_free (ptr);
}

static void
pk_client_properties_changed_cb (GDBusProxy *proxy,
				 GVariant *changed_properties,
				 const gchar* const  *invalidated_properties,
				 gpointer user_data);
static void
pk_client_signal_cb (GDBusProxy *proxy,
		     const gchar *sender_name,
		     const gchar *signal_name,
		     GVariant *parameters,
		     gpointer user_data);
static void
pk_client_notify_name_owner_cb (GObject    *obj,
                                GParamSpec *pspec,
                                gpointer    user_data);

struct _PkClientState
{
	GObject				 parent_instance;

	gboolean			 allow_deps;
	gboolean			 autoremove;
	gboolean			 enabled;
	gboolean			 force;
	PkBitfield			 transaction_flags;
	gboolean			 recursive;
	gboolean			 ret;
	gchar				*directory;
	gchar				*eula_id;
	gchar				**files;
	gchar				*key_id;
	gchar				*package_id;
	gchar				**package_ids;
	gchar				*parameter;
	gchar				*repo_id;
	gchar				**search;
	gchar				*tid;
	gchar				*distro_id;
	gchar				*transaction_id;
	gchar				*value;
	gpointer			 progress_user_data;
	gpointer			 user_data;
	guint				 number;
	gulong				 cancellable_id;
	GDBusProxy			*proxy;
	GDBusProxy			*proxy_props;
	GCancellable			*cancellable;
	GCancellable			*cancellable_client;
	GTask				*res;
	PkBitfield			 filters;
	PkClient			*client;
	PkProgress			*progress;
	PkProgressCallback		 progress_callback;
	PkResults			*results;
	PkRoleEnum			 role;
	PkSigTypeEnum			 type;
	PkUpgradeKindEnum		 upgrade_kind;
	guint				 refcount;
	PkClientHelper			*client_helper;
	gboolean			 waiting_for_finished;
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	gboolean is_idle;

	g_ptr_array_remove (priv->calls, state);

	/* has the idle state changed? */
	is_idle = (priv->calls->len == 0);
	if (is_idle != priv->idle) {
		priv->idle = is_idle;
		g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_IDLE]);
	}
}

/*
 * pk_client_state_finish:
 * @state: (transfer full): the #PkClientState
 * @error: (transfer full): the #GError
 **/
static void
pk_client_state_finish (PkClientState *state, GError *error)
{
	gboolean ret;

	if (state->res == NULL)
		return;

	/* force finished (if not already set) so clients can update the UI's */
	if (state->progress != NULL) {
		ret = pk_progress_set_status (state->progress, PK_STATUS_ENUM_FINISHED);
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_STATUS,
						  state->progress_user_data);
			state->progress_callback = NULL;
		}
	}

	pk_client_state_unset_proxy (state);

	if (state->proxy_props != NULL)
		g_object_unref (G_OBJECT (state->proxy_props));

	if (state->ret) {
		g_task_return_pointer (state->res,
		                       g_object_ref (state->results),
		                       g_object_unref);
	} else {
		g_task_return_error (state->res, error);
	}

	/* remove any socket file */
	if (state->client_helper != NULL) {
		g_autoptr(GError) error_local = NULL;

		if (!pk_client_helper_stop (state->client_helper, &error_local))
			g_warning ("failed to stop the client helper: %s", error_local->message);
		g_object_unref (state->client_helper);
	}

	/* remove from list */
	pk_client_state_remove (state->client, state);

	/* mark the state as finished */
	g_clear_object (&state->res);
}

static void
pk_client_state_dispose (GObject *object)
{
	PkClientState *state = PK_CLIENT_STATE (object);

	if (state->cancellable_id > 0) {
		g_cancellable_disconnect (state->cancellable_client,
					  state->cancellable_id);
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
	/* results will not exist if the CreateTransaction fails */
	g_clear_object (&state->results);
	g_clear_object (&state->progress);
	g_clear_object (&state->res);
	g_object_unref (state->client);

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
{
}

static void
pk_client_cancel_cb (GObject *source_object,
		     GAsyncResult *res,
		     gpointer user_data)
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
}

static void
pk_client_cancellable_cancel_cb (GCancellable *cancellable,
				 gpointer user_data)
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

		g_debug ("Cancelled, but no proxy, not sure what to do here");
		local_error = g_error_new_literal (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED,
						   "PackageKit transaction disappeared");
		pk_client_state_finish (state, g_steal_pointer (&local_error));
		return;
	}

	/* takeover the call with the cancel method */
	g_debug ("cancelling %s", state->tid);
	g_dbus_proxy_call (state->proxy, "Cancel",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CLIENT_DBUS_METHOD_TIMEOUT,
			   NULL,
			   pk_client_cancel_cb, pk_client_weak_ref_new (state));
}

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
	state->client = g_object_ref (client);
	g_task_set_source_tag (state->res, source_tag);

	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
		g_value_set_boolean (value, priv->idle);
		break;
	case PROP_CACHE_AGE:
		g_value_set_uint (value, priv->cache_age);
		break;
	case PROP_DETAILS_WITH_DEPS_SIZE:
		g_value_set_boolean (value, priv->details_with_deps_size);
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	case PROP_DETAILS_WITH_DEPS_SIZE:
		priv->details_with_deps_size = g_value_get_boolean (value);
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
	if (g_str_has_prefix (name, "org.freedesktop.PackageKit.Transaction."))
		name_suffix = &name[39];
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
	g_warning ("couldn't parse execption '%s', please report", name);
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
	res = g_new0 (gchar *, len+1);

	/* resolve each path */
	for (i = 0; i < len; i++) {
		res[i] = pk_client_real_path (paths[i]);
		if (res[i] == NULL) {
			g_set_error (error,
				     PK_CLIENT_ERROR,
				     PK_CLIENT_ERROR_INVALID_INPUT,
				     "could not resolve: %s", paths[i]);
			return NULL;
		}
	}
	return g_strdupv (res);
}

/*
 * pk_client_get_user_temp:
 *
 * Return (and create if does not exist) a temporary directory
 * that is writable only by the user, and readable by root.
 *
 * Return value: the temp directory, or %NULL for create error
 **/
static gchar *
pk_client_get_user_temp (const gchar *subfolder, GError **error)
{
	gchar *path = NULL;
	g_autoptr(GFile) file = NULL;

	/* build path in home folder */
	path = g_build_filename (g_get_user_cache_dir (), "PackageKit", subfolder, NULL);

	/* find if exists */
	file = g_file_new_for_path (path);
	if (g_file_query_exists (file, NULL))
		return path;

	/* create as does not exist */
	if (!g_file_make_directory_with_parents (file, NULL, error)) {
		/* return nothing.. */
		g_free (path);
		return NULL;
	}
	return path;
}

/*
 * pk_client_is_file_native:
 **/
static gboolean
pk_client_is_file_native (const gchar *filename)
{
	g_autoptr(GFile) source = NULL;

	/* does gvfs think the file is on a remote filesystem? */
	source = g_file_new_for_path (filename);
	if (!g_file_is_native (source))
		return FALSE;

	/* are we FUSE mounted */
	if (g_strstr_len (filename, -1, "/.gvfs/") != NULL)
		return FALSE;
	return TRUE;
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
pk_client_set_property_value (PkClientState *state,
			      const char *key,
			      GVariant *value)
{
	gboolean ret;
	const gchar *package_id;

	/* role */
	if (g_strcmp0 (key, "Role") == 0) {
		ret = pk_progress_set_role (state->progress,
					    g_variant_get_uint32 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_ROLE,
						  state->progress_user_data);
		}
		return;
	}

	/* status */
	if (g_strcmp0 (key, "Status") == 0) {
		ret = pk_progress_set_status (state->progress,
					      g_variant_get_uint32 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_STATUS,
						  state->progress_user_data);
		}
		return;
	}

	/* last-package */
	if (g_strcmp0 (key, "LastPackage") == 0) {
		package_id = g_variant_get_string (value, NULL);
		/* check to see if it's been set yet */
		ret = pk_package_id_check (package_id);
		if (!ret)
			return;
		ret = pk_progress_set_package_id (state->progress,
						  package_id);
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_PACKAGE_ID,
						  state->progress_user_data);
		}
		return;
	}

	/* percentage */
	if (g_strcmp0 (key, "Percentage") == 0) {
		ret = pk_progress_set_percentage (state->progress,
						  pk_client_percentage_to_signed (g_variant_get_uint32 (value)));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_PERCENTAGE,
						  state->progress_user_data);
		}
		return;
	}

	/* allow-cancel */
	if (g_strcmp0 (key, "AllowCancel") == 0) {
		ret = pk_progress_set_allow_cancel (state->progress,
						  g_variant_get_boolean (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_ALLOW_CANCEL,
						  state->progress_user_data);
		}
		return;
	}

	/* caller-active */
	if (g_strcmp0 (key, "CallerActive") == 0) {
		ret = pk_progress_set_caller_active (state->progress,
						  g_variant_get_boolean (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_CALLER_ACTIVE,
						  state->progress_user_data);
		}
		return;
	}

	/* elapsed-time */
	if (g_strcmp0 (key, "ElapsedTime") == 0) {
		ret = pk_progress_set_elapsed_time (state->progress,
						  g_variant_get_uint32 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_ELAPSED_TIME,
						  state->progress_user_data);
		}
		return;
	}

	/* remaining-time */
	if (g_strcmp0 (key, "RemainingTime") == 0) {
		ret = pk_progress_set_elapsed_time (state->progress,
						    g_variant_get_uint32 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_REMAINING_TIME,
						  state->progress_user_data);
		}
		return;
	}

	/* speed */
	if (g_strcmp0 (key, "Speed") == 0) {
		ret = pk_progress_set_speed (state->progress,
					     g_variant_get_uint32 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_SPEED,
						  state->progress_user_data);
		}
		return;
	}

	/* download-size-remaining */
	if (g_strcmp0 (key, "DownloadSizeRemaining") == 0) {
		ret = pk_progress_set_download_size_remaining (state->progress,
							       g_variant_get_uint64 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_DOWNLOAD_SIZE_REMAINING,
						  state->progress_user_data);
		}
		return;
	}

	/* transaction-flags */
	if (g_strcmp0 (key, "TransactionFlags") == 0) {
		ret = pk_progress_set_transaction_flags (state->progress,
							 g_variant_get_uint64 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_TRANSACTION_FLAGS,
						  state->progress_user_data);
		}
		return;
	}

	/* uid */
	if (g_strcmp0 (key, "Uid") == 0) {
		ret = pk_progress_set_uid (state->progress,
						  g_variant_get_uint32 (value));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_UID,
						  state->progress_user_data);
		}
		return;
	}

	/* sender */
	if (g_strcmp0 (key, "Sender") == 0) {
		ret = pk_progress_set_sender (state->progress,
						  g_variant_get_string (value, NULL));
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_SENDER,
						  state->progress_user_data);
		}
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	gboolean is_idle;

	g_ptr_array_add (priv->calls, state);

	/* has the idle state changed? */
	is_idle = (priv->calls->len == 0);
	if (is_idle != priv->idle) {
		priv->idle = is_idle;
		g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_IDLE]);
	}
}

/*
 * pk_client_properties_changed_cb:
 **/
static void
pk_client_properties_changed_cb (GDBusProxy *proxy,
				 GVariant *changed_properties,
				 const gchar* const  *invalidated_properties,
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
		g_variant_get (changed_properties,
				"a{sv}",
				&iter);
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
			  PkInfoEnum update_severity,
			  const gchar *package_id,
			  const gchar *summary)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkPackage) package = NULL;

	/* create virtual package */
	package = pk_package_new ();
	if (!pk_package_set_id (package, package_id, &error)) {
		g_warning ("failed to set package id for %s", package_id);
		return;
	}
	g_object_set (package,
		      "info", info_enum,
		      "summary", summary,
		      "update-severity", update_severity,
		      "role", state->role,
		      "transaction-id", state->transaction_id,
		      NULL);

	/* add to results */
	if (state->results != NULL && info_enum != PK_INFO_ENUM_FINISHED)
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
	case PK_INFO_ENUM_DECOMPRESSING:
	case PK_INFO_ENUM_FINISHED:
		ret = pk_progress_set_package_id (state->progress, package_id);
		if (state->progress_callback != NULL && ret) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_PACKAGE_ID,
						  state->progress_user_data);
		}
		ret = pk_progress_set_package (state->progress, package);
		if (state->progress_callback != NULL && ret) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_PACKAGE,
						  state->progress_user_data);
		}
		break;
	default:
		break;
	}
}

/*
 * pk_client_copy_finished_remove_old_files:
 *
 * Removes all the files that do not have the prefix destination path.
 * This should remove all the old /var/cache/PackageKit/$TMP/powertop-1.8-1.fc8.rpm
 * and leave the $DESTDIR/powertop-1.8-1.fc8.rpm files.
 */
static void
pk_client_copy_finished_remove_old_files (PkClientState *state)
{
	guint i;
	g_autoptr(GPtrArray) array = NULL;

	/* get the data */
	array = pk_results_get_files_array (state->results);
	if (array == NULL) {
		g_warning ("internal error, no files in array");
		return;
	}

	/* remove any without dest path */
	for (i = 0; i < array->len; ) {
		PkFiles *item;
		gchar **files;

		item = g_ptr_array_index (array, i);
		files = pk_files_get_files (item);
		if (!g_str_has_prefix (files[0], state->directory))
			g_ptr_array_remove_index_fast (array, i);
		else
			i++;
	}
}

/*
 * pk_client_copy_downloaded_finished_cb:
 */
static void
pk_client_copy_downloaded_finished_cb (GFile *file, GAsyncResult *res, PkClientState *state)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GError) error = NULL;

	/* debug */
	path = g_file_get_path (file);
	g_debug ("finished copy of %s", path);

	/* get the result */
	if (!g_file_copy_finish (file, res, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* no more copies pending? */
	if (--state->refcount == 0) {
		pk_client_copy_finished_remove_old_files (state);
		state->ret = TRUE;
		pk_client_state_finish (state, NULL);
	}
}

/*
 * pk_client_copy_progress_cb:
 */
static void
pk_client_copy_progress_cb (goffset current_num_bytes, goffset total_num_bytes, PkClientState *state)
{
	gboolean ret;
	gint percentage = -1;

	/* save status */
	ret = pk_progress_set_status (state->progress, PK_STATUS_ENUM_COPY_FILES);
	if (state->progress_callback != NULL && ret) {
		state->progress_callback (state->progress,
					  PK_PROGRESS_TYPE_STATUS,
					  state->progress_user_data);
	}

	/* calculate percentage */
	if (total_num_bytes > 0)
		percentage = 100 * current_num_bytes / total_num_bytes;

	/* save percentage */
	ret = pk_progress_set_percentage (state->progress, percentage);
	if (state->progress_callback != NULL && ret) {
		state->progress_callback (state->progress,
					  PK_PROGRESS_TYPE_PERCENTAGE,
					  state->progress_user_data);
	}
}

/*
 * pk_client_copy_downloaded_file:
 */
static void
pk_client_copy_downloaded_file (PkClientState *state, const gchar *package_id, const gchar *source_file)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(GFile) destination = NULL;
	g_autoptr(GFile) source = NULL;
	g_autoptr(PkFiles) item = NULL;
	g_auto(GStrv) files = NULL;

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
			     "file %s already exists", path);
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}
	g_file_copy_async (source, destination, G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT, state->cancellable,
			   (GFileProgressCallback) pk_client_copy_progress_cb, state,
			   (GAsyncReadyCallback) pk_client_copy_downloaded_finished_cb, state);

	/* Add the result (as a GStrv) to the results set */
	files = g_strsplit (path, ",", -1);
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      "role", state->role,
		      "transaction-id", state->transaction_id,
		      NULL);
	pk_results_add_files (state->results, item);
}

/*
 * pk_client_copy_downloaded:
 *
 * We have to copy the files from the temporary directory into the user-specified
 * directory. There should only be one file for each package, although this is
 * not encoded in the spec.
 */
static void
pk_client_copy_downloaded (PkClientState *state)
{
	guint i;
	guint j;
	guint len;
	PkFiles *item;
	gboolean ret;
	g_autoptr(GPtrArray) array = NULL;

	/* get data */
	array = pk_results_get_files_array (state->results);
	if (array == NULL) {
		g_warning ("internal error, no files in array");
		return;
	}

	/* get the number of files to copy */
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		state->refcount += g_strv_length (pk_files_get_files (item));
	}

	/* get a cached value, as pk_client_copy_downloaded_file() adds items */
	len = array->len;

	/* save percentage */
	ret = pk_progress_set_percentage (state->progress, -1);
	if (state->progress_callback != NULL && ret) {
		state->progress_callback (state->progress,
					  PK_PROGRESS_TYPE_PERCENTAGE,
					  state->progress_user_data);
	}

	/* do the copies pipelined */
	for (i = 0; i < len; i++) {
		gchar **files;
		item = g_ptr_array_index (array, i);
		files = pk_files_get_files (item);
		for (j = 0; files[j] != NULL; j++) {
			pk_client_copy_downloaded_file (state,
							pk_files_get_package_id (item),
							files[j]);
		}
	}
}

/*
 * pk_client_signal_finished:
 */
static void
pk_client_signal_finished (PkClientState *state,
			   PkExitEnum exit_enum,
			   guint runtime)
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

	/* do we have to copy results? */
	if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    state->directory != NULL &&
	    exit_enum != PK_EXIT_ENUM_CANCELLED) {
		pk_client_copy_downloaded (state);
		return;
	}

	/* we're done */
	state->ret = TRUE;
	pk_client_state_finish (state, NULL);
}

static void
results_add_update_detail_from_variant (PkResults   *results,
                                        GVariant    *update_variant,
                                        PkRoleEnum   role,
                                        const gchar *transaction_id)
{
	g_autoptr(PkUpdateDetail) item = NULL;
	const gchar *package_id;
	g_autofree gchar **updates_strv = NULL;
	g_autofree gchar **obsoletes_strv = NULL;
	g_autofree gchar **vendor_urls_strv = NULL;
	g_autofree gchar **bugzilla_urls_strv = NULL;
	g_autofree gchar **cve_urls_strv = NULL;
	guint restart, state;
	const gchar *update_text, *changelog, *issued, *updated;

	g_variant_get (update_variant,
		       "(&s^a&s^a&s^a&s^a&s^a&su&s&su&s&s)",
		       &package_id,
		       &updates_strv,
		       &obsoletes_strv,
		       &vendor_urls_strv,
		       &bugzilla_urls_strv,
		       &cve_urls_strv,
		       &restart,
		       &update_text,
		       &changelog,
		       &state,
		       &issued,
		       &updated);

	item = pk_update_detail_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "updates", updates_strv[0] != NULL ? updates_strv : NULL,
		      "obsoletes", obsoletes_strv[0] != NULL ? obsoletes_strv : NULL,
		      "vendor-urls", vendor_urls_strv[0] != NULL ? vendor_urls_strv : NULL,
		      "bugzilla-urls", bugzilla_urls_strv[0] != NULL ? bugzilla_urls_strv : NULL,
		      "cve-urls", cve_urls_strv[0] != NULL ? cve_urls_strv : NULL,
		      "restart", restart,
		      "update-text", update_text,
		      "changelog", changelog,
		      "state", state,
		      "issued", issued,
		      "updated", updated,
		      "role", role,
		      "transaction-id", transaction_id,
		      NULL);

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
	gboolean ret;
	guint tmp_uint;
	guint tmp_uint2;
	guint tmp_uint3;

	if (!state)
		return;

	if (g_strcmp0 (signal_name, "Finished") == 0) {
		g_variant_get (parameters,
			       "(uu)",
			       &tmp_uint2,
			       &tmp_uint);
		pk_client_signal_finished (state,
					   tmp_uint2,
					   tmp_uint);
		return;
	}
	if (g_strcmp0 (signal_name, "Package") == 0) {
		g_variant_get (parameters,
			       "(u&s&s)",
			       &tmp_uint,
			       &tmp_str[1],
			       &tmp_str[2]);
		/* The 'info' and 'update-severity' are encoded in the single value */
		tmp_uint2 = tmp_uint & 0xFFFF;
		tmp_uint3 = (tmp_uint >> 16) & 0xFFFF;
		pk_client_signal_package (state,
					  tmp_uint2,
					  tmp_uint3,
					  tmp_str[1],
					  tmp_str[2]);
		return;
	}
	if (g_strcmp0 (signal_name, "Packages") == 0) {
		g_autoptr(GVariantIter) iter = NULL;
		guint flags;
		PkInfoEnum info, severity;
		const gchar *package_id, *summary;

		g_variant_get (parameters, "(a(uss))", &iter);

		while (g_variant_iter_loop (iter, "(u&s&s)",
					    &flags,
					    &package_id,
					    &summary)) {
			/* The 'info' and 'update-severity' are encoded in the single value */
			info = flags & 0xFFFF;
			severity = (flags >> 16) & 0xFFFF;
			pk_client_signal_package (state,
						  info,
						  severity,
						  package_id,
						  summary);
		}

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
					g_object_set (item, "group", g_variant_get_uint32 (value), NULL);
				else if (g_strcmp0 (key, "size") == 0)
					g_object_set (item, "size", g_variant_get_uint64 (value), NULL);
				else if (g_strcmp0 (key, "download-size") == 0)
					g_object_set (item, "download-size", g_variant_get_uint64 (value), NULL);
				else
					g_object_set (item, key, g_variant_get_string (value, NULL), NULL);
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
				      "package-id", tmp_str[0],
				      "license", tmp_str[1],
				      "group", tmp_uint,
				      "description", tmp_str[3],
				      "url", tmp_str[4],
				      "size", tmp_uint64,
				      "role", state->role,
				      "transaction-id", state->transaction_id,
				      NULL);
		}
		pk_results_add_details (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "UpdateDetail") == 0) {
		results_add_update_detail_from_variant (state->results, parameters,
							state->role, state->transaction_id);
		return;
	}
	if (g_strcmp0 (signal_name, "UpdateDetails") == 0) {
		g_autoptr(GVariantIter) iter = NULL;
		g_autoptr(GVariant) update_detail = NULL;

		g_variant_get (parameters, "(a(sasasasasasussuss))", &iter);

		while ((update_detail = g_variant_iter_next_value (iter))) {
			results_add_update_detail_from_variant (state->results, update_detail,
								state->role, state->transaction_id);
			g_clear_pointer (&update_detail, g_variant_unref);
		}

		return;
	}
	if (g_strcmp0 (signal_name, "Transaction") == 0) {
		g_autoptr(PkTransactionPast) item = NULL;
		g_variant_get (parameters,
			       "(&o&sbuu&su&s)",
			       &tmp_str[0],
			       &tmp_str[1],
			       &tmp_bool,
			       &tmp_uint3,
			       &tmp_uint,
			       &tmp_str[3],
			       &tmp_uint2,
			       &tmp_str[4]);
		item = pk_transaction_past_new ();
		g_object_set (item,
			      "tid", tmp_str[0],
			      "timespec", tmp_str[1],
			      "succeeded", tmp_bool,
			      "role", tmp_uint3,
			      "duration", tmp_uint,
			      "data", tmp_str[3],
			      "uid", tmp_uint2,
			      "cmdline", tmp_str[4],
			      "PkSource::role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_transaction (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "DistroUpgrade") == 0) {
		g_autoptr(PkDistroUpgrade) item = NULL;
		g_variant_get (parameters,
			       "(u&s&s)",
			       &tmp_uint,
			       &tmp_str[1],
			       &tmp_str[2]);
		item = pk_distro_upgrade_new ();
		g_object_set (item,
			      "state", tmp_uint,
			      "name", tmp_str[1],
			      "summary", tmp_str[2],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_distro_upgrade (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "RequireRestart") == 0) {
		g_autoptr(PkRequireRestart) item = NULL;
		g_variant_get (parameters,
			       "(u&s)",
			       &tmp_uint,
			       &tmp_str[1]);
		item = pk_require_restart_new ();
		g_object_set (item,
			      "restart", tmp_uint,
			      "package-id", tmp_str[1],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_require_restart (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "Category") == 0) {
		g_autoptr(PkCategory) item = NULL;
		g_variant_get (parameters,
			       "(&s&s&s&s&s)",
			       &tmp_str[0],
			       &tmp_str[1],
			       &tmp_str[2],
			       &tmp_str[3],
			       &tmp_str[4]);
		item = pk_category_new ();
		g_object_set (item,
			      "parent-id", tmp_str[0],
			      "cat-id", tmp_str[1],
			      "name", tmp_str[2],
			      "summary", tmp_str[3],
			      "icon", tmp_str[4],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_category (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "Files") == 0) {
		g_autofree gchar **files = NULL;
		g_autoptr(PkFiles) item = NULL;
		g_variant_get (parameters,
			       "(&s^a&s)",
			       &tmp_str[0],
			       &files);
		item = pk_files_new ();
		g_object_set (item,
			      "package-id", tmp_str[0],
			      "files", files,
			      "role", state->role,
			      "transaction-id", state->transaction_id,
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
			      "package-id", tmp_str[0],
			      "repository-name", tmp_str[1],
			      "key-url", tmp_str[2],
			      "key-userid", tmp_str[3],
			      "key-id", tmp_str[4],
			      "key-fingerprint", tmp_str[5],
			      "key-timestamp", tmp_str[6],
			      "type", tmp_uint,
			      "role", state->role,
			      "transaction-id", state->transaction_id,
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
			      "eula-id", tmp_str[0],
			      "package-id", tmp_str[1],
			      "vendor-name", tmp_str[2],
			      "license-agreement", tmp_str[3],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_eula_required (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "RepoDetail") == 0) {
		g_autoptr(PkRepoDetail) item = NULL;
		g_variant_get (parameters,
			       "(&s&sb)",
			       &tmp_str[0],
			       &tmp_str[1],
			       &tmp_bool);
		item = pk_repo_detail_new ();
		g_object_set (item,
			      "repo-id", tmp_str[0],
			      "description", tmp_str[1],
			      "enabled", tmp_bool,
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_repo_detail (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "ErrorCode") == 0) {
		g_autoptr(PkError) item = NULL;
		g_variant_get (parameters,
			       "(u&s)",
			       &tmp_uint,
			       &tmp_str[1]);
		item = pk_error_new ();
		g_object_set (item,
			      "code", tmp_uint,
			      "details", tmp_str[1],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_set_error_code (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "MediaChangeRequired") == 0) {
		g_autoptr(PkMediaChangeRequired) item = NULL;
		g_variant_get (parameters,
			       "(u&s&s)",
			       &tmp_uint,
			       &tmp_str[1],
			       &tmp_str[2]);
		item = pk_media_change_required_new ();
		g_object_set (item,
			      "media-type", tmp_uint,
			      "media-id", tmp_str[1],
			      "media-text", tmp_str[2],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_media_change_required (state->results, item);
		return;
	}
	if (g_strcmp0 (signal_name, "ItemProgress") == 0) {
		g_autoptr(PkItemProgress) item = NULL;
		g_variant_get (parameters,
			       "(&suu)",
			       &tmp_str[0],
			       &tmp_uint,
			       &tmp_uint2);
		item = pk_item_progress_new ();
		g_object_set (item,
			      "package-id", tmp_str[0],
			      "status", tmp_uint,
			      "percentage", tmp_uint2,
			      "transaction-id", state->transaction_id,
			      NULL);
		ret = pk_progress_set_item_progress (state->progress,
						     item);
		if (ret && state->progress_callback != NULL) {
			state->progress_callback (state->progress,
						  PK_PROGRESS_TYPE_ITEM_PROGRESS,
						  state->progress_user_data);
		}
		return;
	}
	if (g_strcmp0 (signal_name, "Destroy") == 0) {
		g_autoptr(GError) local_error = NULL;

		if (state->waiting_for_finished)
			local_error = g_error_new_literal (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED,
							   "PackageKit transaction disappeared");

		pk_client_state_finish (state, g_steal_pointer (&local_error));
		return;
	}
}

static void
pk_client_notify_name_owner_cb (GObject *obj,
				GParamSpec *pspec,
				gpointer user_data)
{
	GWeakRef *weak_ref = user_data;
	g_autoptr(PkClientState) state = g_weak_ref_get (weak_ref);

	if (!state)
		return;

	if (state->waiting_for_finished) {
		g_autoptr(GError) local_error = NULL;

		local_error = g_error_new_literal (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED,
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
		value_tmp = g_dbus_proxy_get_cached_property (state->proxy,
							      props[i]);
		pk_client_set_property_value (state,
					      props[i],
					      value_tmp);
	}

	/* connect up signals */
	g_signal_connect_data (state->proxy, "g-properties-changed",
			       G_CALLBACK (pk_client_properties_changed_cb),
			       pk_client_weak_ref_new (state), pk_client_weak_ref_free_gclosure, 0);
	g_signal_connect_data (state->proxy, "g-signal",
			       G_CALLBACK (pk_client_signal_cb),
			       pk_client_weak_ref_new (state), pk_client_weak_ref_free_gclosure, 0);
	g_signal_connect_data (state->proxy, "notify::g-name-owner",
			       G_CALLBACK (pk_client_notify_name_owner_cb),
			       pk_client_weak_ref_new (state), pk_client_weak_ref_free_gclosure, 0);
}

/*
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (GObject *source_object,
		     GAsyncResult *res,
		     gpointer user_data)
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

	/* wait for ::Finished() or notify::g-name-owner (if the daemon disappears) */
	state->waiting_for_finished = TRUE;
	g_object_ref (state);
}

/*
 * pk_client_set_role:
 **/
static void
pk_client_set_role (PkClientState *state, PkRoleEnum role)
{
	gboolean ret;
	pk_progress_set_transaction_flags (state->progress,
					   state->transaction_flags);
	ret = pk_progress_set_role (state->progress, role);
	if (ret && state->progress_callback != NULL) {
		state->progress_callback (state->progress,
					  PK_PROGRESS_TYPE_ROLE,
					  state->progress_user_data);
	}
	return;
}

/*
 * pk_client_set_hints_cb:
 **/
static void
pk_client_set_hints_cb (GObject *source_object,
			GAsyncResult *res,
			gpointer user_data)
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

	/* we'll have results from now on */
	state->results = pk_results_new ();
	g_object_set (state->results,
		      "role", state->role,
		      "progress", state->progress,
		      "transaction-flags", state->transaction_flags,
		      NULL);

	/* do this async, although this should be pretty fast anyway */
	if (state->role == PK_ROLE_ENUM_RESOLVE) {
		g_dbus_proxy_call (state->proxy, "Resolve",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_NAME) {
		g_dbus_proxy_call (state->proxy, "SearchNames",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		g_dbus_proxy_call (state->proxy, "SearchDetails",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		g_dbus_proxy_call (state->proxy, "SearchGroups",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		g_dbus_proxy_call (state->proxy, "SearchFiles",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		g_dbus_proxy_call (state->proxy, "GetDetails",
				   g_variant_new ("(^a&s)",
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS_LOCAL) {
		g_dbus_proxy_call (state->proxy, "GetDetailsLocal",
				   g_variant_new ("(^a&s)",
						  state->files),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->files),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_FILES_LOCAL) {
		g_dbus_proxy_call (state->proxy, "GetFilesLocal",
				   g_variant_new ("(^a&s)",
						  state->files),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->files),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		g_dbus_proxy_call (state->proxy, "GetUpdateDetail",
				   g_variant_new ("(^a&s)",
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_OLD_TRANSACTIONS) {
		g_dbus_proxy_call (state->proxy, "GetOldTransactions",
				   g_variant_new ("(u)",
						  state->number),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "DownloadPackages",
				   g_variant_new ("(b^a&s)",
						  (state->directory == NULL),
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATES) {
		g_dbus_proxy_call (state->proxy, "GetUpdates",
				   g_variant_new ("(t)",
						  state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_DEPENDS_ON) {
		g_dbus_proxy_call (state->proxy, "DependsOn",
				   g_variant_new ("(t^a&sb)",
						  state->filters,
						  state->package_ids,
						  state->recursive),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);

	} else if (state->role == PK_ROLE_ENUM_REQUIRED_BY) {
		g_dbus_proxy_call (state->proxy, "RequiredBy",
				   g_variant_new ("(t^a&sb)",
						  state->filters,
						  state->package_ids,
						  state->recursive),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "GetPackages",
				   g_variant_new ("(t)",
						  state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		g_dbus_proxy_call (state->proxy, "WhatProvides",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		g_dbus_proxy_call (state->proxy, "GetDistroUpgrades",
				   NULL,
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_FILES) {
		g_dbus_proxy_call (state->proxy, "GetFiles",
				   g_variant_new ("(^a&s)",
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_CATEGORIES) {
		g_dbus_proxy_call (state->proxy, "GetCategories",
				   NULL,
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "RemovePackages",
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
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		g_dbus_proxy_call (state->proxy, "RefreshCache",
				   g_variant_new ("(b)",
						  state->force),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "InstallPackages",
				   g_variant_new ("(t^a&s)",
						  state->transaction_flags,
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_SIGNATURE) {
		g_dbus_proxy_call (state->proxy, "InstallSignature",
				   g_variant_new ("(uss)",
						  state->type,
						  state->key_id,
						  state->package_id),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "UpdatePackages",
				   g_variant_new ("(t^a&s)",
						  state->transaction_flags,
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		g_dbus_proxy_call (state->proxy, "InstallFiles",
				   g_variant_new ("(t^a&s)",
						  state->transaction_flags,
						  state->files),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
		g_object_set (state->results,
			      "inputs", g_strv_length (state->files),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_ACCEPT_EULA) {
		g_dbus_proxy_call (state->proxy, "AcceptEula",
				   g_variant_new ("(s)",
						  state->eula_id),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		g_dbus_proxy_call (state->proxy, "GetRepoList",
				   g_variant_new ("(t)",
						  state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REPO_ENABLE) {
		g_dbus_proxy_call (state->proxy, "RepoEnable",
				   g_variant_new ("(sb)",
						  state->repo_id,
						  state->enabled),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   g_object_ref (state));
	} else if (state->role == PK_ROLE_ENUM_REPO_SET_DATA) {
		g_dbus_proxy_call (state->proxy, "RepoSetData",
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
		g_dbus_proxy_call (state->proxy, "RepoRemove",
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
		g_dbus_proxy_call (state->proxy, "UpgradeSystem",
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
		g_dbus_proxy_call (state->proxy, "RepairSystem",
				   g_variant_new ("(t)",
						  state->transaction_flags),
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
 * pk_client_bool_to_string:
 **/
static const gchar *
pk_client_bool_to_string (gboolean value)
{
	if (value)
		return "true";
	return "false";
}

/*
 * pk_client_create_helper_argv_envp_test:
 **/
static gboolean
pk_client_create_helper_argv_envp_test (PkClientState *state,
					gchar ***argv,
					gchar ***envp)
{
	gboolean ret;

	/* check we have the right file */
	ret = g_file_test (TESTDATADIR "/pk-client-helper-test.py",
			   G_FILE_TEST_EXISTS);
	if (!ret) {
		g_warning ("could not find the socket helper!");
		return FALSE;
	}

	/* setup simple test socket */
	*argv = g_new0 (gchar *, 2);
	*argv[0] = g_build_filename (TESTDATADIR,
				     "pk-client-helper-test.py",
				     NULL);
	return TRUE;
}

/*
 * pk_client_create_helper_argv_envp:
 **/
gboolean
pk_client_create_helper_argv_envp (gchar ***argv,
				   gchar ***envp_out)
{
	const gchar *dialog = NULL;
	const gchar *display;
	const gchar *term;
	gboolean ret;
	guint envpi = 0;
	gchar **envp;

	/* check we have the right file */
	ret = g_file_test ("/usr/bin/debconf-communicate",
			   G_FILE_TEST_EXISTS);
	if (!ret)
		return FALSE;

	/* setup simple test socket */
	*argv = g_new0 (gchar *, 2);
	*argv[0] = g_strdup ("/usr/bin/debconf-communicate");

	*envp_out = g_new0 (gchar *, 8);
	envp = *envp_out;
	envp[envpi++] = g_strdup ("DEBCONF_DB_REPLACE=configdb");
	envp[envpi++] = g_strdup ("DEBCONF_DB_OVERRIDE=Pipe{infd:none outfd:none}");
	if (pk_debug_is_verbose ())
		envp[envpi++] = g_strdup ("DEBCONF_DEBUG=.");

	/* do we have an available terminal to use */
	term = g_getenv ("TERM");
	if (term != NULL) {
		envp[envpi++] = g_strdup_printf ("TERM=%s", term);
		dialog = "dialog";
	}

	/* do we have access to the display */
	display = g_getenv ("DISPLAY");
	if (display != NULL) {
		envp[envpi++] = g_strdup_printf ("DISPLAY=%s", display);
		if (g_strcmp0 (g_getenv ("KDE_FULL_SESSION"), "true") == 0)
			dialog = "kde";
		else
			dialog = "gnome";
	}

	/* indicate a prefered frontend */
	if (dialog != NULL) {
		envp[envpi++] = g_strdup_printf ("DEBIAN_FRONTEND=%s", dialog);
		g_debug ("using frontend %s", dialog);
	}
	return TRUE;
}

/*
 * pk_client_create_helper_socket:
 **/
static gchar *
pk_client_create_helper_socket (PkClientState *state)
{
	gboolean ret = FALSE;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *socket_filename = NULL;
	g_autofree gchar *socket_id = NULL;
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) envp = NULL;

	/* use the test socket */
	if (g_getenv ("PK_SELF_TEST") != NULL) {
		ret = pk_client_create_helper_argv_envp_test (state,
							      &argv,
							      &envp);
	}

	/* either the self test failed, or we're not in self test */
	if (!ret) {
		ret = pk_client_create_helper_argv_envp (&argv,
							 &envp);
	}

	/* no supported frontends available */
	if (!ret)
		return NULL;

	/* This is not a specially handled debian frontend (current terminal or
	 * the debconf-kde stuff, use a systemd-activated helper if available)
	 */
	if (envp != NULL &&
	    !g_strv_contains ((const gchar * const *) envp, "DEBIAN_FRONTEND=kde") &&
	    !g_strv_contains ((const gchar * const *) envp, "DEBIAN_FRONTEND=dialog")) {
		g_autofree gchar *existing_socket_filename = g_build_filename (g_get_user_runtime_dir (), "pk-debconf-socket", NULL);
		if (g_file_test (existing_socket_filename, G_FILE_TEST_EXISTS))
			return g_strdup_printf ("frontend-socket=%s", existing_socket_filename);
	}

	/* create object */
	state->client_helper = pk_client_helper_new ();

	/* create socket to read from /tmp */
	socket_id = g_strdup_printf ("gpk-%s.socket", &state->tid[1]);
	socket_filename = g_build_filename (g_get_tmp_dir (), socket_id, NULL);

	/* start the helper process */
	ret = pk_client_helper_start (state->client_helper, socket_filename, argv, envp, &error);
	if (!ret) {
		g_warning ("failed to open debconf socket: %s", error->message);
		return NULL;
	}

	/* success */
	return g_strdup_printf ("frontend-socket=%s", socket_filename);
}

/*
 * pk_client_get_proxy_cb:
 **/
static void
pk_client_get_proxy_cb (GObject *object,
			GAsyncResult *res,
			gpointer user_data)
{
	gchar *hint;
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;
	PkClientPrivate *priv = pk_client_get_instance_private (state->client);

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL)
		g_error ("Cannot connect to PackageKit on %s", state->tid);

	/* connect */
	pk_client_proxy_connect (state);

	/* get hints */
	array = g_ptr_array_new_with_free_func (g_free);

	/* locale */
	if (priv->locale != NULL) {
		hint = g_strdup_printf ("locale=%s", priv->locale);
		g_ptr_array_add (array, hint);
	}

	/* background */
	hint = g_strdup_printf ("background=%s",
				pk_client_bool_to_string (priv->background));
	g_ptr_array_add (array, hint);

	/* interactive */
	hint = g_strdup_printf ("interactive=%s",
				pk_client_bool_to_string (priv->interactive));
	g_ptr_array_add (array, hint);

	if (priv->details_with_deps_size &&
	    state->role == PK_ROLE_ENUM_GET_DETAILS)
		g_ptr_array_add (array, g_strdup ("details-with-deps-size=true"));

	/* cache-age */
	if (priv->cache_age > 0) {
		hint = g_strdup_printf ("cache-age=%u", priv->cache_age);
		g_ptr_array_add (array, hint);
	}

	/* Always set the supports-plural-signals hint to get higher performance signals */
	g_ptr_array_add (array, g_strdup ("supports-plural-signals=true"));

	/* create socket for roles that need interaction */
	if (state->role == PK_ROLE_ENUM_INSTALL_FILES ||
	    state->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	    state->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	    state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		hint = pk_client_create_helper_socket (state);
		if (hint != NULL)
			g_ptr_array_add (array, hint);
	}

	/* set hints */
	g_ptr_array_add (array, NULL);
	g_dbus_proxy_call (state->proxy, "SetHints",
			   g_variant_new ("(^a&s)",
					  array->pdata),
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CLIENT_DBUS_METHOD_TIMEOUT,
			   state->cancellable,
			   pk_client_set_hints_cb,
			   g_object_ref (state));

	/* track state */
	g_ptr_array_add (priv->calls, state);
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
pk_client_resolve_async (PkClient *client, PkBitfield filters, gchar **packages, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data,
			 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_resolve_async, PK_ROLE_ENUM_RESOLVE, cancellable);
	state->filters = filters;
	state->package_ids = g_strdupv (packages);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_search_names_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_search_names_async, PK_ROLE_ENUM_SEARCH_NAME, cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_search_details_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_search_details_async, PK_ROLE_ENUM_SEARCH_DETAILS, cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_search_groups_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_search_groups_async, PK_ROLE_ENUM_SEARCH_GROUP, cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_search_files_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_search_files_async, PK_ROLE_ENUM_SEARCH_FILE, cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_get_details_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_details_async, PK_ROLE_ENUM_GET_DETAILS, cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_details_local_async (PkClient *client, gchar **files, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_details_local_async, PK_ROLE_ENUM_GET_DETAILS_LOCAL, cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	state->files = pk_client_convert_real_paths (files, &error);
	if (state->files == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_files_local_async (PkClient *client, gchar **files, GCancellable *cancellable,
				 PkProgressCallback progress_callback, gpointer progress_user_data,
				 GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_files_local_async, PK_ROLE_ENUM_GET_FILES_LOCAL, cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	state->files = pk_client_convert_real_paths (files, &error);
	if (state->files == NULL) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_get_update_detail_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_update_detail_async, PK_ROLE_ENUM_GET_UPDATE_DETAIL, cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_download_packages_async (PkClient *client, gchar **package_ids, const gchar *directory, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_download_packages_async, PK_ROLE_ENUM_DOWNLOAD_PACKAGES, cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_updates_async (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_updates_async, PK_ROLE_ENUM_GET_UPDATES, cancellable);
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_old_transactions_async (PkClient *client, guint number, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_old_transactions_async, PK_ROLE_ENUM_GET_OLD_TRANSACTIONS, cancellable);
	state->number = number;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_depends_on_async (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_depends_on_async, PK_ROLE_ENUM_DEPENDS_ON, cancellable);
	state->filters = filters;
	state->recursive = recursive;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_packages_async (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_packages_async, PK_ROLE_ENUM_GET_PACKAGES, cancellable);
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_required_by_async (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_required_by_async, PK_ROLE_ENUM_REQUIRED_BY, cancellable);
	state->recursive = recursive;
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_what_provides_async, PK_ROLE_ENUM_WHAT_PROVIDES, cancellable);
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_distro_upgrades_async (PkClient *client, GCancellable *cancellable,
				     PkProgressCallback progress_callback, gpointer progress_user_data,
				     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_distro_upgrades_async, PK_ROLE_ENUM_GET_DISTRO_UPGRADES, cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_get_files_async (PkClient *client, gchar **package_ids, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_files_async, PK_ROLE_ENUM_GET_FILES, cancellable);
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * pk_client_get_categories_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope notified): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get a list of all categories supported.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_categories_async (PkClient *client, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_categories_async, PK_ROLE_ENUM_GET_CATEGORIES, cancellable);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_remove_packages_async, PK_ROLE_ENUM_REMOVE_PACKAGES, cancellable);
	state->transaction_flags = transaction_flags;
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_refresh_cache_async (PkClient *client, gboolean force, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_refresh_cache_async, PK_ROLE_ENUM_REFRESH_CACHE, cancellable);
	state->force = force;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
pk_client_install_packages_async (PkClient *client, PkBitfield transaction_flags, gchar **package_ids, GCancellable *cancellable,
				  PkProgressCallback progress_callback, gpointer progress_user_data,
				  GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_install_packages_async, PK_ROLE_ENUM_INSTALL_PACKAGES, cancellable);
	state->transaction_flags = transaction_flags;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_id: a signature_id structure such as "hal;0.0.1;i386;fedora"
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
pk_client_install_signature_async (PkClient *client, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_install_signature_async, PK_ROLE_ENUM_INSTALL_SIGNATURE, cancellable);
	state->type = type;
	state->key_id = g_strdup (key_id);
	state->package_id = g_strdup (package_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_update_packages_async, PK_ROLE_ENUM_UPDATE_PACKAGES, cancellable);
	state->transaction_flags = transaction_flags;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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

/*
 * pk_client_copy_native_finished_cb:
 */
static void
pk_client_copy_native_finished_cb (GFile *file, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));
	g_autoptr(GError) error = NULL;

	/* get the result */
	if (!g_file_copy_finish (file, res, &error)) {
		pk_client_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* no more copies pending? */
	if (--state->refcount == 0) {
		PkClientPrivate *client_priv = pk_client_get_instance_private (state->client);
		/* now get tid and continue on our merry way */
		pk_control_get_tid_async (client_priv->control,
					  state->cancellable,
					  (GAsyncReadyCallback) pk_client_get_tid_cb,
					  g_object_ref (state));
	}
}

/*
 * pk_client_copy_non_native_then_get_tid:
 **/
static void
pk_client_copy_non_native_then_get_tid (PkClientState *state)
{
	gchar *path;
	gboolean ret;
	guint i;
	g_autofree gchar *user_temp = NULL;
	g_autoptr(GError) error = NULL;

	/* get a temp dir accessible by the daemon */
	user_temp = pk_client_get_user_temp ("native-cache", &error);
	g_debug ("using temp dir %s", user_temp);

	/* save percentage */
	ret = pk_progress_set_percentage (state->progress, -1);
	if (state->progress_callback != NULL && ret) {
		state->progress_callback (state->progress,
					  PK_PROGRESS_TYPE_PERCENTAGE,
					  state->progress_user_data);
	}

	/* copy each file that is non-native */
	for (i = 0; state->files[i] != NULL; i++) {
		ret = pk_client_is_file_native (state->files[i]);
		g_debug ("%s native=%i", state->files[i], ret);
		if (!ret) {
			/* generate the destination location */
			g_autofree gchar *basename = NULL;
			g_autoptr(GFile) destination = NULL;
			g_autoptr(GFile) source = NULL;
			basename = g_path_get_basename (state->files[i]);
			path = g_build_filename (user_temp, basename, NULL);
			g_debug ("copy from %s to %s", state->files[i], path);
			source = g_file_new_for_path (state->files[i]);
			destination = g_file_new_for_path (path);

			/* copy the file async */
			g_file_copy_async (source, destination, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, state->cancellable,
					   (GFileProgressCallback) pk_client_copy_progress_cb, state,
					   (GAsyncReadyCallback) pk_client_copy_native_finished_cb, g_object_ref (state));

			/* pass the new path to PackageKit */
			g_free (state->files[i]);
			state->files[i] = path;
		}
	}
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	gboolean ret;
	guint i;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_install_files_async, PK_ROLE_ENUM_INSTALL_FILES, cancellable);
	state->transaction_flags = transaction_flags;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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

	/* how many non-native */
	for (i = 0; state->files[i] != NULL; i++) {
		ret = pk_client_is_file_native (state->files[i]);
		/* on a FUSE mount (probably created by gvfs) and not readable by packagekitd */
		if (!ret)
			state->refcount++;
	}

	/* nothing to copy, common case */
	if (state->refcount == 0) {
		/* just get tid */
		pk_control_get_tid_async (priv->control,
					  cancellable,
					  (GAsyncReadyCallback) pk_client_get_tid_cb,
					  g_object_ref (state));
		return;
	}

	/* copy the files first */
	pk_client_copy_non_native_then_get_tid (state);
}

/**
 * pk_client_accept_eula_async: (finish-func pk_client_generic_finish):
 * @client: a valid #PkClient instance
 * @eula_id: the <literal>eula_id</literal> we are agreeing to
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
pk_client_accept_eula_async (PkClient *client, const gchar *eula_id, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_accept_eula_async, PK_ROLE_ENUM_ACCEPT_EULA, cancellable);
	state->eula_id = g_strdup (eula_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_get_repo_list_async (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_repo_list_async, PK_ROLE_ENUM_GET_REPO_LIST, cancellable);
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_repo_enable_async (PkClient *client, const gchar *repo_id, gboolean enabled, GCancellable *cancellable,
			     PkProgressCallback progress_callback,
			     gpointer progress_user_data, GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_repo_enable_async, PK_ROLE_ENUM_REPO_ENABLE, cancellable);
	state->enabled = enabled;
	state->repo_id = g_strdup (repo_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
pk_client_repo_set_data_async (PkClient *client, const gchar *repo_id, const gchar *parameter, const gchar *value, GCancellable *cancellable,
			       PkProgressCallback progress_callback,
			       gpointer progress_user_data, GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_repo_set_data_async, PK_ROLE_ENUM_REPO_SET_DATA, cancellable);
	state->repo_id = g_strdup (repo_id);
	state->parameter = g_strdup (parameter);
	state->value = g_strdup (value);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_repo_remove_async, PK_ROLE_ENUM_REPO_REMOVE, cancellable);
	state->transaction_flags = transaction_flags;
	state->repo_id = g_strdup (repo_id);
	state->autoremove = autoremove;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
				const gchar *distro_id, PkUpgradeKindEnum upgrade_kind,
				GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback_ready, gpointer user_data)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_upgrade_system_async, PK_ROLE_ENUM_UPGRADE_SYSTEM, cancellable);
	state->transaction_flags = transaction_flags;
	state->distro_id = g_strdup (distro_id);
	state->upgrade_kind = upgrade_kind;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	g_autoptr(PkClientState) state = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_repair_system_async, PK_ROLE_ENUM_REPAIR_SYSTEM, cancellable);
	state->transaction_flags = transaction_flags;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

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
pk_client_adopt_get_proxy_cb (GObject *object,
			      GAsyncResult *res,
			      gpointer user_data)
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
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_adopt_async, PK_ROLE_ENUM_UNKNOWN, cancellable);
	state->tid = g_strdup (transaction_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	state->results = pk_results_new ();
	g_object_set (state->results,
		      "role", state->role,
		      "progress", state->progress,
		      NULL);

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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

	/* track state */
	pk_client_state_add (client, state);
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
 * pk_client_get_progress_state_finish:
 * @state: a #PkClientState
 * @error: (transfer full)
 **/
static void
pk_client_get_progress_state_finish (PkClientState *state, GError *error)
{
	if (state->cancellable_id > 0) {
		g_cancellable_disconnect (state->cancellable_client,
					  state->cancellable_id);
		state->cancellable_id = 0;
	}
	g_clear_object (&state->cancellable);
	g_clear_object (&state->cancellable_client);

	pk_client_state_unset_proxy (state);

	if (state->proxy_props != NULL)
		g_object_unref (G_OBJECT (state->proxy_props));

	if (state->ret) {
		g_task_return_pointer (state->res,
		                       g_object_ref (state->progress),
		                       g_object_unref);
	} else {
		g_task_return_error (state->res, g_steal_pointer (&error));
	}

	/* remove from list */
	pk_client_state_remove (state->client, state);
}

/*
 * pk_client_get_progress_cb:
 **/
static void
pk_client_get_progress_cb (GObject *source_object,
			   GAsyncResult *res,
			   gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkClientState) state = PK_CLIENT_STATE (g_steal_pointer (&user_data));

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_client_get_progress_state_finish (state, g_steal_pointer (&error));
		return;
	}

	/* connect */
	pk_client_proxy_connect (state);

	state->ret = TRUE;
	pk_client_get_progress_state_finish (state, NULL);
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
	state = pk_client_state_new (client, callback_ready, user_data, pk_client_get_progress_async, PK_ROLE_ENUM_UNKNOWN, cancellable);
	state->tid = g_strdup (transaction_id);
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
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

	/* track state */
	pk_client_state_add (client, state);
}

/**********************************************************************/

/*
 * pk_client_cancel_all_dbus_methods:
 **/
static gboolean
pk_client_cancel_all_dbus_methods (PkClient *client)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);
	const PkClientState *state;
	guint i;
	GPtrArray *array;

	/* just cancel the call */
	array = priv->calls;
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return priv->idle;
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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

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
	PkClientPrivate *priv = pk_client_get_instance_private (client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return priv->cache_age;
}

/**
 * pk_client_set_details_with_deps_size:
 * @client: a valid #PkClient instance
 * @details_with_deps_size: the value to set
 *
 * Sets whether the pk_client_get_details_async() should include dependencies
 * download sizes for packages, which are not installed.
 *
 * Since: 1.2.7
 **/
void
pk_client_set_details_with_deps_size (PkClient *client, gboolean details_with_deps_size)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);

	g_return_if_fail (PK_IS_CLIENT (client));

	if (priv->details_with_deps_size == details_with_deps_size)
		return;

	priv->details_with_deps_size = details_with_deps_size;
	g_object_notify_by_pspec (G_OBJECT (client), obj_properties[PROP_DETAILS_WITH_DEPS_SIZE]);
}

/**
 * pk_client_get_details_with_deps_size:
 * @client: a valid #PkClient instance
 *
 * Gets the client details-with-deps-size value.
 *
 * Returns: whether the pk_client_get_details_async() should include dependencies
 *    download sizes for packages, which are not installed.
 *
 * Since: 1.2.7
 **/
gboolean
pk_client_get_details_with_deps_size (PkClient *client)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return priv->details_with_deps_size;
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
	obj_properties[PROP_LOCALE] =
		g_param_spec_string ("locale", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:background:
	 *
	 * Since: 0.5.3
	 */
	obj_properties[PROP_BACKGROUND] =
		g_param_spec_boolean ("background", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:interactive:
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_INTERACTIVE] =
		g_param_spec_boolean ("interactive", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:idle:
	 *
	 * Whether there are transactions in progress on this client or not
	 *
	 * Since: 0.5.4
	 */
	obj_properties[PROP_IDLE] =
		g_param_spec_boolean ("idle", NULL, NULL,
				      TRUE,
				      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:cache-age:
	 *
	 * The cache age in seconds, where %G_MAXUINT means cache
	 * "never expires"
	 *
	 * Since: 0.6.10
	 */
	obj_properties[PROP_CACHE_AGE] =
		g_param_spec_uint ("cache-age", NULL, NULL,
				   0, G_MAXUINT, G_MAXUINT,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * PkClient:details-with-deps-size:
	 *
	 * Since: 1.2.7
	 */
	obj_properties[PROP_DETAILS_WITH_DEPS_SIZE] =
		g_param_spec_boolean ("details-with-deps-size", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, PROP_LAST, obj_properties);
}

/*
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	PkClientPrivate *priv = pk_client_get_instance_private (client);

	client->priv = priv;
	priv->calls = g_ptr_array_new ();
	priv->background = FALSE;
	priv->interactive = TRUE;
	priv->idle = TRUE;
	priv->cache_age = G_MAXUINT;
	priv->details_with_deps_size = FALSE;

	/* use a control object */
	priv->control = pk_control_new ();

	/* cache locale */
	priv->locale = 	g_strdup (setlocale (LC_MESSAGES, NULL));
}

/*
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = pk_client_get_instance_private (client);

	/* ensure we cancel any in-flight DBus calls */
	pk_client_cancel_all_dbus_methods (client);

	g_clear_pointer (&priv->locale, g_free);
	g_clear_object (&priv->control);
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
