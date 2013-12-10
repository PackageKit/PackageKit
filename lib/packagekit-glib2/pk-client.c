/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Richard Hughes <richard@hughsie.com>
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
 * http://www.packagekit.org/gtk-doc/introduction-ideas-transactions.html
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

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

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
	guint			 cache_age;
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

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

typedef struct {
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
	GSimpleAsyncResult		*res;
	PkBitfield			 filters;
	PkClient			*client;
	PkProgress			*progress;
	PkProgressCallback		 progress_callback;
	PkProvidesEnum			 provides;
	PkResults			*results;
	PkRoleEnum			 role;
	PkSigTypeEnum			 type;
	PkUpgradeKindEnum		 upgrade_kind;
	guint				 refcount;
	PkClientHelper			*client_helper;
} PkClientState;

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

/**
 * pk_client_error_quark:
 *
 * Return value: Our personal error quark.
 *
 * Since: 0.5.2
 **/
GQuark
pk_client_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_client_error");
	return quark;
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
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = client->priv;

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

/**
 * pk_client_fixup_dbus_error:
 **/
static void
pk_client_fixup_dbus_error (GError *error)
{
	gchar *name = NULL;
	const gchar *name_suffix = NULL;

	g_return_if_fail (error != NULL);

	/* old style PolicyKit failure */
	if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
		g_debug ("fixing up code for Policykit auth failure");
		error->code = PK_CLIENT_ERROR_FAILED_AUTH;
		g_free (error->message);
		error->message = g_strdup ("PolicyKit authorization failure");
		goto out;
	}

	/* is a remote error? */
	if (!g_dbus_error_is_remote_error (error))
		goto out;

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
		goto out;
	}
	if (g_strcmp0 (name_suffix, "PackageIdInvalid") == 0 ||
		 g_strcmp0 (name_suffix, "SearchInvalid") == 0 ||
		 g_strcmp0 (name_suffix, "FilterInvalid") == 0 ||
		 g_strcmp0 (name_suffix, "InvalidProvide") == 0 ||
		 g_strcmp0 (name_suffix, "InputInvalid") == 0) {
		error->code = PK_CLIENT_ERROR_INVALID_INPUT;
		goto out;
	}
	if (g_strcmp0 (name_suffix, "PackInvalid") == 0 ||
	    g_strcmp0 (name_suffix, "NoSuchFile") == 0 ||
	    g_strcmp0 (name_suffix, "MimeTypeNotSupported") == 0 ||
	    g_strcmp0 (name_suffix, "NoSuchDirectory") == 0) {
		error->code = PK_CLIENT_ERROR_INVALID_FILE;
		goto out;
	}
	if (g_strcmp0 (name_suffix, "NotSupported") == 0) {
		error->code = PK_CLIENT_ERROR_NOT_SUPPORTED;
		goto out;
	}
	g_warning ("couldn't parse execption '%s', please report", name);
out:
	g_free (name);
}

/**
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

/**
 * pk_client_convert_real_paths:
 **/
static gchar **
pk_client_convert_real_paths (gchar **paths, GError **error)
{
	guint i;
	guint len;
	gchar **res;

	/* create output array */
	len = g_strv_length (paths);
	res = g_new0 (gchar *, len+1);

	/* resolve each path */
	for (i=0; i<len; i++) {
		res[i] = pk_client_real_path (paths[i]);
		if (res[i] == NULL) {
			/* set an error, and abort, tearing down all our hard work */
			g_set_error (error, PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "could not resolve: %s", paths[i]);
			g_strfreev (res);
			res = NULL;
			goto out;
		}
	}
out:
	return res;
}

/**
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
	GFile *file;
	gboolean ret;
	gchar *path = NULL;

	/* build path in home folder */
	path = g_build_filename (g_get_user_cache_dir (), "PackageKit", subfolder, NULL);

	/* find if exists */
	file = g_file_new_for_path (path);
	ret = g_file_query_exists (file, NULL);
	if (ret)
		goto out;

	/* create as does not exist */
	ret = g_file_make_directory_with_parents (file, NULL, error);
	if (!ret) {
		/* return nothing.. */
		g_free (path);
		path = NULL;
	}
out:
	g_object_unref (file);
	return path;
}

/**
 * pk_client_is_file_native:
 **/
static gboolean
pk_client_is_file_native (const gchar *filename)
{
	gboolean ret;
	GFile *source;

	/* does gvfs think the file is on a remote filesystem? */
	source = g_file_new_for_path (filename);
	ret = g_file_is_native (source);
	if (!ret)
		goto out;

	/* are we FUSE mounted */
	ret = (g_strstr_len (filename, -1, "/.gvfs/") == NULL);
	if (!ret)
		goto out;
out:
	g_object_unref (source);
	return ret;
}

/**
 * pk_client_percentage_to_signed:
 */
static gint
pk_client_percentage_to_signed (guint percentage)
{
	if (percentage == 101)
		return -1;
	return (gint) percentage;
}

/**
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

	g_warning ("unhandled property '%s'", key);
}

/**
 * pk_client_cancel_cb:
 **/
static void
pk_client_cancel_cb (GObject *source_object,
		     GAsyncResult *res,
		     gpointer user_data)
{
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	GError *error = NULL;
	GVariant *value;
	PkClientState *state = (PkClientState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* there's not really a lot we can do here */
		g_warning ("failed to cancel: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* finished this call */
	g_debug ("cancelled %s", state->tid);
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
 * pk_client_cancellable_cancel_cb:
 **/
static void
pk_client_cancellable_cancel_cb (GCancellable *cancellable, PkClientState *state)
{
	/* dbus method has not yet fired */
	if (state->proxy == NULL) {
		g_debug ("Cancelled, but no proxy, not sure what to do here");
		return;
	}

	/* takeover the call with the cancel method */
	g_debug ("cancelling %s", state->tid);
	g_dbus_proxy_call (state->proxy, "Cancel",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   PK_CLIENT_DBUS_METHOD_TIMEOUT,
			   NULL,
			   pk_client_cancel_cb, state);
}

/**
 * pk_client_state_remove:
 **/
static void
pk_client_state_remove (PkClient *client, PkClientState *state)
{
	gboolean is_idle;
	g_ptr_array_remove (client->priv->calls, state);

	/* has the idle state changed? */
	is_idle = (client->priv->calls->len == 0);
	if (is_idle != client->priv->idle) {
		client->priv->idle = is_idle;
		g_object_notify (G_OBJECT(client), "idle");
	}
}

/**
 * pk_client_state_add:
 **/
static void
pk_client_state_add (PkClient *client, PkClientState *state)
{
	gboolean is_idle;

	g_ptr_array_add (client->priv->calls, state);

	/* has the idle state changed? */
	is_idle = (client->priv->calls->len == 0);
	if (is_idle != client->priv->idle) {
		client->priv->idle = is_idle;
		g_object_notify (G_OBJECT(client), "idle");
	}
}

/**
 * pk_client_state_finish:
 **/
static void
pk_client_state_finish (PkClientState *state, const GError *error)
{
	gboolean ret;
	GError *error_local = NULL;

	/* force finished (if not already set) so clients can update the UI's */
	ret = pk_progress_set_status (state->progress, PK_STATUS_ENUM_FINISHED);
	if (ret && state->progress_callback != NULL) {
		state->progress_callback (state->progress,
					  PK_PROGRESS_TYPE_STATUS,
					  state->progress_user_data);
	}

	if (state->cancellable_id > 0) {
		g_cancellable_disconnect (state->cancellable_client,
					  state->cancellable_id);
	}
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);

	if (state->proxy != NULL) {
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_properties_changed_cb),
						      state);
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_signal_cb),
						      state);
		g_object_unref (G_OBJECT (state->proxy));
	}

	if (state->proxy_props != NULL)
		g_object_unref (G_OBJECT (state->proxy_props));

	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_object_ref (state->results),
							   g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove any socket file */
	if (state->client_helper != NULL) {
		ret = pk_client_helper_stop (state->client_helper, &error_local);
		if (!ret) {
			g_warning ("failed to stop the client helper: %s", error_local->message);
			g_error_free (error_local);
		}
		g_object_unref (state->client_helper);
	}

	/* remove from list */
	pk_client_state_remove (state->client, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* destroy state */
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
	/* results will no exists if the CreateTransaction fails */
	if (state->results != NULL)
		g_object_unref (state->results);
	g_object_unref (state->progress);
	g_object_unref (state->res);
	g_object_unref (state->client);
	g_slice_free (PkClientState, state);
}

/**
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
	PkClientState *state = (PkClientState *) user_data;

	if (g_variant_n_children (changed_properties) > 0) {
		g_variant_get (changed_properties,
				"a{sv}",
				&iter);
		while (g_variant_iter_loop (iter, "{&sv}", &key, &value))
			pk_client_set_property_value (state, key, value);
		g_variant_iter_free (iter);
	}
}

/**
 * pk_client_signal_package:
 */
static void
pk_client_signal_package (PkClientState *state,
			  PkInfoEnum info_enum,
			  const gchar *package_id,
			  const gchar *summary)
{
	gboolean ret;
	GError *error = NULL;
	PkPackage *package;

	/* create virtual package */
	package = pk_package_new ();
	ret = pk_package_set_id (package, package_id, &error);
	if (!ret) {
		g_warning ("failed to set package id for %s", package_id);
		g_error_free (error);
		goto out;
	}
	g_object_set (package,
		      "info", info_enum,
		      "summary", summary,
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
out:
	g_object_unref (package);
}

/**
 * pk_client_copy_finished_remove_old_files:
 *
 * Removes all the files that do not have the prefix destination path.
 * This should remove all the old /var/cache/PackageKit/$TMP/powertop-1.8-1.fc8.rpm
 * and leave the $DESTDIR/powertop-1.8-1.fc8.rpm files.
 */
static void
pk_client_copy_finished_remove_old_files (PkClientState *state)
{
	PkFiles *item;
	GPtrArray *array = NULL;
	guint i;
	gchar **files;

	/* get the data */
	array = pk_results_get_files_array (state->results);
	if (array == NULL) {
		g_warning ("internal error, no files in array");
		goto out;
	}

	/* remove any without dest path */
	for (i=0; i < array->len; ) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "files", &files,
			      NULL);
		if (!g_str_has_prefix (files[0], state->directory))
			g_ptr_array_remove_index_fast (array, i);
		else
			i++;
		g_strfreev (files);
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * pk_client_copy_downloaded_finished_cb:
 */
static void
pk_client_copy_downloaded_finished_cb (GFile *file, GAsyncResult *res, PkClientState *state)
{
	gboolean ret;
	gchar *path;
	GError *error = NULL;

	/* debug */
	path = g_file_get_path (file);
	g_debug ("finished copy of %s", path);

	/* get the result */
	ret = g_file_copy_finish (file, res, &error);
	if (!ret) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* no more copies pending? */
	if (--state->refcount == 0) {
		pk_client_copy_finished_remove_old_files (state);
		state->ret = TRUE;
		pk_client_state_finish (state, NULL);
	}
out:
	g_free (path);
}

/**
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

/**
 * pk_client_copy_downloaded_file:
 */
static void
pk_client_copy_downloaded_file (PkClientState *state, const gchar *package_id, const gchar *source_file)
{
	gchar *basename;
	gchar *path;
	gchar **files = NULL;
	GFile *source;
	GFile *destination;
	PkFiles *item = NULL;
	GError *error = NULL;

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
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}
	g_file_copy_async (source, destination, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, state->cancellable,
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

	/* free everything we've used */
out:
	if (item != NULL)
		g_object_unref (item);
	g_object_unref (source);
	g_object_unref (destination);
	g_strfreev (files);
	g_free (basename);
	g_free (path);
}

/**
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
	GPtrArray *array = NULL;
	gboolean ret;
	gchar *package_id;
	gchar **files;

	/* get data */
	array = pk_results_get_files_array (state->results);
	if (array == NULL) {
		g_warning ("internal error, no files in array");
		goto out;
	}

	/* get the number of files to copy */
	for (i=0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "files", &files,
			      NULL);
		state->refcount += g_strv_length (files);
		g_strfreev (files);
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
	for (i=0; i < len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "package-id", &package_id,
			      "files", &files,
			      NULL);
		for (j=0; files[j] != NULL; j++)
			pk_client_copy_downloaded_file (state, package_id, files[j]);
		g_free (package_id);
		g_strfreev (files);
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
}

/**
 * pk_client_signal_finished:
 */
static void
pk_client_signal_finished (PkClientState *state,
			   PkExitEnum exit_enum,
			   guint runtime)
{
	GError *error = NULL;
	PkError *error_code = NULL;

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
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* do we have to copy results? */
	if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    state->directory != NULL) {
		pk_client_copy_downloaded (state);
		goto out;
	}

	/* we're done */
	state->ret = TRUE;
	pk_client_state_finish (state, NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
}

/**
 * pk_client_signal_cb:
 **/
static void
pk_client_signal_cb (GDBusProxy *proxy,
		     const gchar *sender_name,
		     const gchar *signal_name,
		     GVariant *parameters,
		     gpointer user_data)
{
	PkClientState *state = (PkClientState *) user_data;
	gchar *tmp_str[12];
	gchar **tmp_strv[5];
	gboolean tmp_bool;
	gboolean ret;
	guint tmp_uint;
	guint tmp_uint2;
	guint tmp_uint3;
	guint64 tmp_uint64;

	/* connect up the signals */
	if (g_strcmp0 (signal_name, "Message") == 0) {
		PkMessage *item;
		g_variant_get (parameters,
			       "(u&s)",
			       &tmp_uint,
			       &tmp_str[1]);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		item = pk_message_new ();
		g_object_set (item,
			      "type", tmp_uint,
			      "details", tmp_str[1],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_message (state->results, item);
G_GNUC_END_IGNORE_DEPRECATIONS
		g_object_unref (item);
		return;
	}
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
		pk_client_signal_package (state,
					  tmp_uint,
					  tmp_str[1],
					  tmp_str[2]);
		return;
	}
	if (g_strcmp0 (signal_name, "Details") == 0) {
		PkDetails *item;
		g_variant_get (parameters,
			       "(&s&su&s&st)",
			       &tmp_str[0],
			       &tmp_str[1],
			       &tmp_uint,
			       &tmp_str[3],
			       &tmp_str[4],
			       &tmp_uint64);
		item = pk_details_new ();
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
		pk_results_add_details (state->results, item);
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "UpdateDetail") == 0) {
		PkUpdateDetail *item;
		g_variant_get (parameters,
			       "(&s^a&s^a&s^a&s^a&s^a&su&s&su&s&s)",
			       &tmp_str[0],
			       &tmp_strv[0],
			       &tmp_strv[1],
			       &tmp_strv[2],
			       &tmp_strv[3],
			       &tmp_strv[4],
			       &tmp_uint,
			       &tmp_str[7],
			       &tmp_str[8],
			       &tmp_uint2,
			       &tmp_str[10],
			       &tmp_str[11]);
		item = pk_update_detail_new ();
		g_object_set (item,
			      "package-id", tmp_str[0],
			      "updates", tmp_strv[0][0] != NULL ? tmp_strv[0] : NULL,
			      "obsoletes", tmp_strv[1][0] != NULL ? tmp_strv[1] : NULL,
			      "vendor-urls", tmp_strv[2][0] != NULL ? tmp_strv[2] : NULL,
			      "bugzilla-urls", tmp_strv[3][0] != NULL ? tmp_strv[3] : NULL,
			      "cve-urls", tmp_strv[4][0] != NULL ? tmp_strv[4] : NULL,
			      "restart", tmp_uint,
			      "update-text", tmp_str[7],
			      "changelog", tmp_str[8],
			      "state", tmp_uint2,
			      "issued", tmp_str[10],
			      "updated", tmp_str[11],
			      "role", state->role,
			      "transaction-id", state->transaction_id,
			      NULL);
		pk_results_add_update_detail (state->results, item);
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "Transaction") == 0) {
		PkTransactionPast *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "DistroUpgrade") == 0) {
		PkDistroUpgrade *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "RequireRestart") == 0) {
		PkRequireRestart *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "Category") == 0) {
		PkCategory *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "Files") == 0) {
		gchar **files;
		PkFiles *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "RepoSignatureRequired") == 0) {
		PkRepoSignatureRequired *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "EulaRequired") == 0) {
		PkEulaRequired *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "RepoDetail") == 0) {
		PkRepoDetail *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "ErrorCode") == 0) {
		PkError *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "MediaChangeRequired") == 0) {
		PkMediaChangeRequired *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "ItemProgress") == 0) {
		PkItemProgress *item;
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
		g_object_unref (item);
		return;
	}
	if (g_strcmp0 (signal_name, "Changed") == 0) {
		return;
	}
	if (g_strcmp0 (signal_name, "Destroy") == 0) {
		return;
	}
}

/**
 * pk_client_proxy_connect:
 **/
static void
pk_client_proxy_connect (PkClientState *state)
{
	gchar **props = NULL;
	guint i;
	GVariant *value_tmp;

	/* coldplug properties */
	props = g_dbus_proxy_get_cached_property_names (state->proxy);
	for (i = 0; props != NULL && props[i] != NULL; i++) {
		value_tmp = g_dbus_proxy_get_cached_property (state->proxy,
							      props[i]);
		pk_client_set_property_value (state,
					      props[i],
					      value_tmp);
		g_variant_unref (value_tmp);
	}

	/* connect up signals */
	g_signal_connect (state->proxy, "g-properties-changed",
			  G_CALLBACK (pk_client_properties_changed_cb),
			  state);
	g_signal_connect (state->proxy, "g-signal",
			  G_CALLBACK (pk_client_signal_cb),
			  state);

	g_strfreev (props);
}

/**
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (GObject *source_object,
		     GAsyncResult *res,
		     gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkClientState *state = (PkClientState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* wait for ::Finished() */
out:
	if (value != NULL)
		g_variant_unref (value);
}

/**
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

/**
 * pk_client_set_hints_cb:
 **/
static void
pk_client_set_hints_cb (GObject *source_object,
			GAsyncResult *res,
			gpointer user_data)
{
	GError *error = NULL;
	GVariant *value;
	GDBusProxy *proxy = G_DBUS_PROXY (source_object);
	PkClientState *state = (PkClientState *) user_data;

	/* get the result */
	value = g_dbus_proxy_call_finish (proxy, res, &error);
	if (value == NULL) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
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
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		g_dbus_proxy_call (state->proxy, "SearchDetails",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		g_dbus_proxy_call (state->proxy, "SearchGroups",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		g_dbus_proxy_call (state->proxy, "SearchFiles",
				   g_variant_new ("(t^a&s)",
						  state->filters,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		g_dbus_proxy_call (state->proxy, "GetDetails",
				   g_variant_new ("(^a&s)",
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		g_dbus_proxy_call (state->proxy, "GetUpdateDetail",
				   g_variant_new ("(^a&s)",
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "DownloadPackages",
				   g_variant_new ("(b^a&s)",
						  (state->directory == NULL),
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_GET_DEPENDS) {
		g_dbus_proxy_call (state->proxy, "GetDepends",
				   g_variant_new ("(t^a&sb)",
						  state->filters,
						  state->package_ids,
						  state->recursive),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_GET_REQUIRES) {
		g_dbus_proxy_call (state->proxy, "GetRequires",
				   g_variant_new ("(t^a&sb)",
						  state->filters,
						  state->package_ids,
						  state->recursive),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
		g_object_set (state->results,
			      "inputs", g_strv_length (state->package_ids),
			      NULL);
	} else if (state->role == PK_ROLE_ENUM_WHAT_PROVIDES) {
		g_dbus_proxy_call (state->proxy, "WhatProvides",
				   g_variant_new ("(tu^a&s)",
						  state->filters,
						  state->provides,
						  state->search),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		g_dbus_proxy_call (state->proxy, "GetDistroUpgrades",
				   NULL,
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_GET_FILES) {
		g_dbus_proxy_call (state->proxy, "GetFiles",
				   g_variant_new ("(^a&s)",
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
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
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "InstallPackages",
				   g_variant_new ("(t^a&s)",
						  state->transaction_flags,
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		g_dbus_proxy_call (state->proxy, "UpdatePackages",
				   g_variant_new ("(t^a&s)",
						  state->transaction_flags,
						  state->package_ids),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_GET_REPO_LIST) {
		g_dbus_proxy_call (state->proxy, "GetRepoList",
				   g_variant_new ("(t)",
						  state->filters),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_REPO_ENABLE) {
		g_dbus_proxy_call (state->proxy, "RepoEnable",
				   g_variant_new ("(sb)",
						  state->repo_id,
						  state->enabled),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
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
				   state);
	} else if (state->role == PK_ROLE_ENUM_UPGRADE_SYSTEM) {
		g_dbus_proxy_call (state->proxy, "UpgradeSystem",
				   g_variant_new ("(su)",
						  state->distro_id,
						  state->upgrade_kind),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else if (state->role == PK_ROLE_ENUM_REPAIR_SYSTEM) {
		g_dbus_proxy_call (state->proxy, "RepairSystem",
				   g_variant_new ("(t)",
						  state->transaction_flags),
				   G_DBUS_CALL_FLAGS_NONE,
				   PK_CLIENT_DBUS_METHOD_TIMEOUT,
				   state->cancellable,
				   pk_client_method_cb,
				   state);
	} else {
		g_assert_not_reached ();
	}

	/* we've sent this async */
out:
	if (value != NULL)
		g_variant_unref (value);
	return;
}

/**
 * pk_client_bool_to_string:
 **/
static const gchar *
pk_client_bool_to_string (gboolean value)
{
	if (value)
		return "true";
	return "false";
}

/**
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
		goto out;
	}

	/* setup simple test socket */
	*argv = g_new0 (gchar *, 2);
	*argv[0] = g_build_filename (TESTDATADIR,
				     "pk-client-helper-test.py",
				     NULL);
out:
	return ret;
}

/**
 * pk_client_create_helper_argv_envp:
 **/
static gboolean
pk_client_create_helper_argv_envp (PkClientState *state,
				   gchar ***argv,
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
		goto out;

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
out:
	return ret;
}

/**
 * pk_client_create_helper_socket:
 **/
static gchar *
pk_client_create_helper_socket (PkClientState *state)
{
	gboolean ret = FALSE;
	gchar **argv = NULL;
	gchar **envp = NULL;
	gchar *hint = NULL;
	gchar *socket_filename = NULL;
	gchar *socket_id = NULL;
	GError *error = NULL;

	/* use the test socket */
	if (g_getenv ("PK_SELF_TEST") != NULL) {
		ret = pk_client_create_helper_argv_envp_test (state,
							      &argv,
							      &envp);
	}

	/* either the self test failed, or we're not in self test */
	if (!ret) {
		ret = pk_client_create_helper_argv_envp (state,
							 &argv,
							 &envp);
	}

	/* no supported frontends available */
	if (!ret)
		goto out;

	/* create object */
	state->client_helper = pk_client_helper_new ();

	/* create socket to read from /tmp */
	socket_id = g_strdup_printf ("gpk-%s.socket", &state->tid[1]);
	socket_filename = g_build_filename (g_get_tmp_dir (), socket_id, NULL);

	/* start the helper process */
	ret = pk_client_helper_start (state->client_helper, socket_filename, argv, envp, &error);
	if (!ret) {
		g_warning ("failed to open debconf socket: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* success */
	hint = g_strdup_printf ("frontend-socket=%s", socket_filename);
out:
	g_free (socket_id);
	g_free (socket_filename);
	g_strfreev (argv);
	g_strfreev (envp);
	return hint;
}

/**
 * pk_client_get_proxy_cb:
 **/
static void
pk_client_get_proxy_cb (GObject *object,
			GAsyncResult *res,
			gpointer user_data)
{
	gchar *hint;
	GError *error = NULL;
	GPtrArray *array;
	PkClientState *state = (PkClientState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL)
		g_error ("Cannot connect to PackageKit on %s", state->tid);

	/* connect */
	pk_client_proxy_connect (state);

	/* get hints */
	array = g_ptr_array_new_with_free_func (g_free);

	/* locale */
	if (state->client->priv->locale != NULL) {
		hint = g_strdup_printf ("locale=%s", state->client->priv->locale);
		g_ptr_array_add (array, hint);
	}

	/* background */
	hint = g_strdup_printf ("background=%s",
				pk_client_bool_to_string (state->client->priv->background));
	g_ptr_array_add (array, hint);

	/* interactive */
	hint = g_strdup_printf ("interactive=%s",
				pk_client_bool_to_string (state->client->priv->interactive));
	g_ptr_array_add (array, hint);

	/* cache-age */
	if (state->client->priv->cache_age > 0) {
		hint = g_strdup_printf ("cache-age=%u",
					state->client->priv->cache_age);
		g_ptr_array_add (array, hint);
	}

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
			   state);

	/* track state */
	g_ptr_array_add (state->client->priv->calls, state);

	/* we've sent this async */
	g_ptr_array_unref (array);
}

/**
 * pk_client_get_tid_cb:
 **/
static void
pk_client_get_tid_cb (GObject *object, GAsyncResult *res, PkClientState *state)
{
	GError *error = NULL;
	PkControl *control = PK_CONTROL (object);

	state->tid = pk_control_get_tid_finish (control, res, &error);
	if (state->tid == NULL) {
		pk_client_state_finish (state, error);
		g_error_free (error);
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
				  state);
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
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

/**
 * pk_client_resolve_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @packages: (array zero-terminated=1): an array of package names to resolve, e.g. "gnome-system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Resolve a package name into a %package_id. This can return installed and
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_resolve_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_RESOLVE;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->package_ids = g_strdupv (packages);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_search_names_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_names_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_NAME;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_search_details_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_details_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_search_groups_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): a group enum to search for, for instance, "system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_groups_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_GROUP;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_search_files_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): file to search for, for instance, "/sbin/service"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_FILE;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_details_async:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_details_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DETAILS;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_update_detail_async:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_update_detail_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_download_packages_async:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the location where packages are to be downloaded
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_download_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_updates_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_DEVEL or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_updates_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_UPDATES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_old_transactions_async:
 * @client: a valid #PkClient instance
 * @number: the number of past transactions to return, or 0 for all
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_old_transactions_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_OLD_TRANSACTIONS;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->number = number;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_depends_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for depends
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the packages that depend this one, i.e. child->parent.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_depends_async (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_depends_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DEPENDS;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->recursive = recursive;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_packages_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_requires_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for requires
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the packages that require this one, i.e. parent->child.
 *
 * Since: 0.5.2
 **/
void
pk_client_get_requires_async (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_requires_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_REQUIRES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->recursive = recursive;
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_what_provides_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @provides: a #PkProvidesEnum value such as PK_PROVIDES_ENUM_CODEC
 * @values: (array zero-terminated=1): a search term such as "sound/mp3"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
pk_client_what_provides_async (PkClient *client, PkBitfield filters, PkProvidesEnum provides, gchar **values, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_what_provides_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_WHAT_PROVIDES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->provides = provides;
	state->search = g_strdupv (values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_distro_upgrades_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_distro_upgrades_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DISTRO_UPGRADES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_files_async:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_FILES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_categories_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_categories_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_CATEGORIES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_remove_packages_async:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependent packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Remove a package (optionally with dependancies) from the system.
 * If %allow_deps is set to %FALSE, and other packages would have to be removed,
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
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
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_refresh_cache_async:
 * @client: a valid #PkClient instance
 * @force: if we should aggressively drop caches
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_refresh_cache_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REFRESH_CACHE;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->force = force;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_install_packages_async:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->transaction_flags = transaction_flags;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_install_signature_async:
 * @client: a valid #PkClient instance
 * @type: the signature type, e.g. %PK_SIGTYPE_ENUM_GPG
 * @key_id: a key ID such as "0df23df"
 * @package_id: a signature_id structure such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a software source signature of the newest and most correct version.
 *
 * Since: 0.5.2
 **/
void
pk_client_install_signature_async (PkClient *client, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id, GCancellable *cancellable,
				   PkProgressCallback progress_callback, gpointer progress_user_data,
				   GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_signature_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_SIGNATURE;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->type = type;
	state->key_id = g_strdup (key_id);
	state->package_id = g_strdup (package_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_update_packages_async:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->transaction_flags = transaction_flags;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_copy_native_finished_cb:
 */
static void
pk_client_copy_native_finished_cb (GFile *file, GAsyncResult *res, PkClientState *state)
{
	gboolean ret;
	GError *error = NULL;

	/* get the result */
	ret = g_file_copy_finish (file, res, &error);
	if (!ret) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* no more copies pending? */
	if (--state->refcount == 0) {
		/* now get tid and continue on our merry way */
		pk_control_get_tid_async (state->client->priv->control,
					  state->cancellable,
					  (GAsyncReadyCallback) pk_client_get_tid_cb,
					  state);
	}
out:
	return;
}

/**
 * pk_client_copy_non_native_then_get_tid:
 **/
static void
pk_client_copy_non_native_then_get_tid (PkClientState *state)
{
	GFile *source;
	gchar *basename;
	gchar *path;
	GFile *destination;
	gchar *user_temp = NULL;
	GError *error = NULL;
	gboolean ret;
	guint i;

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
	for (i=0; state->files[i] != NULL; i++) {
		ret = pk_client_is_file_native (state->files[i]);
		g_debug ("%s native=%i", state->files[i], ret);
		if (!ret) {
			/* generate the destination location */
			basename = g_path_get_basename (state->files[i]);
			path = g_build_filename (user_temp, basename, NULL);
			g_debug ("copy from %s to %s", state->files[i], path);
			source = g_file_new_for_path (state->files[i]);
			destination = g_file_new_for_path (path);

			/* copy the file async */
			g_file_copy_async (source, destination, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, state->cancellable,
					   (GFileProgressCallback) pk_client_copy_progress_cb, state,
					   (GAsyncReadyCallback) pk_client_copy_native_finished_cb, state);
			g_object_unref (source);
			g_object_unref (destination);

			/* pass the new path to PackageKit */
			g_free (state->files[i]);
			state->files[i] = path;
			g_free (basename);
		}
	}
	g_free (user_temp);
}

/**
 * pk_client_install_files_async:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @files: (array zero-terminated=1): a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;
	gboolean ret;
	guint i;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (files != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->transaction_flags = transaction_flags;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* check files are valid */
	state->files = pk_client_convert_real_paths (files, &error);
	if (state->files == NULL) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* how many non-native */
	for (i=0; state->files[i] != NULL; i++) {
		ret = pk_client_is_file_native (state->files[i]);
		/* on a FUSE mount (probably created by gvfs) and not readable by packagekitd */
		if (!ret)
			state->refcount++;
	}

	/* nothing to copy, common case */
	if (state->refcount == 0) {
		/* just get tid */
		pk_control_get_tid_async (client->priv->control,
					  cancellable,
					  (GAsyncReadyCallback) pk_client_get_tid_cb,
					  state);
		goto out;
	}

	/* copy the files first */
	pk_client_copy_non_native_then_get_tid (state);
out:
	g_object_unref (res);
}

/**
 * pk_client_accept_eula_async:
 * @client: a valid #PkClient instance
 * @eula_id: the <literal>eula_id</literal> we are agreeing to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_accept_eula_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_ACCEPT_EULA;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->eula_id = g_strdup (eula_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_get_repo_list_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_DEVEL or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_repo_list_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_REPO_LIST;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_repo_enable_async:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @enabled: if we should enable the repository
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_repo_enable_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REPO_ENABLE;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->enabled = enabled;
	state->repo_id = g_strdup (repo_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_repo_set_data_async:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @parameter: the parameter to change
 * @value: what we should change it to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_repo_set_data_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REPO_SET_DATA;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->repo_id = g_strdup (repo_id);
	state->parameter = g_strdup (parameter);
	state->value = g_strdup (value);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_upgrade_system_async:
 * @client: a valid #PkClient instance
 * @distro_id: a distro ID such as "fedora-14"
 * @upgrade_kind: a #PkUpgradeKindEnum such as %PK_UPGRADE_KIND_ENUM_COMPLETE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
 * Since: 0.6.11
 **/
void
pk_client_upgrade_system_async (PkClient *client, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind,
				GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_upgrade_system_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UPGRADE_SYSTEM;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->distro_id = g_strdup (distro_id);
	state->upgrade_kind = upgrade_kind;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control,
				  cancellable,
				  (GAsyncReadyCallback) pk_client_get_tid_cb,
				  state);
out:
	g_object_unref (res);
}

/**
 * pk_client_repair_system_async:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_repair_system_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REPAIR_SYSTEM;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->transaction_flags = transaction_flags;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL && g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* identify */
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
out:
	g_object_unref (res);
}

/**********************************************************************/

/**
 * pk_client_adopt_get_proxy_cb:
 **/
static void
pk_client_adopt_get_proxy_cb (GObject *object,
			      GAsyncResult *res,
			      gpointer user_data)
{
	GError *error = NULL;
	PkClientState *state = (PkClientState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		return;
	}

	/* connect */
	pk_client_proxy_connect (state);
}

/**
 * pk_client_adopt_async:
 * @client: a valid #PkClient instance
 * @transaction_id: a transaction ID such as "/21_ebcbdaae_data"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_adopt_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UNKNOWN;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
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
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
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
				  state);

	/* track state */
	pk_client_state_add (client, state);
out:
	g_object_unref (res);
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
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_object_ref (g_simple_async_result_get_op_res_gpointer (simple));
}

/**
 * pk_client_get_progress_state_finish:
 **/
static void
pk_client_get_progress_state_finish (PkClientState *state, const GError *error)
{
	if (state->cancellable_id > 0) {
		g_cancellable_disconnect (state->cancellable_client,
					  state->cancellable_id);
	}
	if (state->cancellable != NULL)
		g_object_unref (state->cancellable);

	if (state->proxy != NULL) {
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_properties_changed_cb),
						      state);
		g_signal_handlers_disconnect_by_func (state->proxy,
						      G_CALLBACK (pk_client_signal_cb),
						      state);
		g_object_unref (G_OBJECT (state->proxy));
	}

	if (state->proxy_props != NULL)
		g_object_unref (G_OBJECT (state->proxy_props));

	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res,
							   g_object_ref (state->progress),
							   g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
	}

	/* remove from list */
	pk_client_state_remove (state->client, state);

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* destroy state */
	g_free (state->transaction_id);
	g_object_unref (state->progress);
	g_object_unref (state->res);
	g_object_unref (state->client);
	g_slice_free (PkClientState, state);
}

/**
 * pk_client_get_progress_cb:
 **/
static void
pk_client_get_progress_cb (GObject *source_object,
			   GAsyncResult *res,
			   gpointer user_data)
{
	GError *error = NULL;
	PkClientState *state = (PkClientState *) user_data;

	state->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (state->proxy == NULL) {
		pk_client_get_progress_state_finish (state, error);
		g_error_free (error);
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
	GSimpleAsyncResult *res;
	PkClientState *state;
	GError *error = NULL;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_progress_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	state->cancellable = g_cancellable_new ();
	if (cancellable != NULL) {
		state->cancellable_client = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable,
							       G_CALLBACK (pk_client_cancellable_cancel_cb),
							       state,
							       NULL);
	}
	state->tid = g_strdup (transaction_id);
	state->progress = pk_progress_new ();

	/* check not already cancelled */
	if (cancellable != NULL &&
	    g_cancellable_set_error_if_cancelled (cancellable, &error)) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
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
				  state);

	/* track state */
	pk_client_state_add (client, state);
out:
	g_object_unref (res);
}

/**********************************************************************/

/**
 * pk_client_cancel_all_dbus_methods:
 **/
static gboolean
pk_client_cancel_all_dbus_methods (PkClient *client)
{
	const PkClientState *state;
	guint i;
	GPtrArray *array;

	/* just cancel the call */
	array = client->priv->calls;
	for (i=0; i<array->len; i++) {
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
	g_return_if_fail (PK_IS_CLIENT (client));
	client->priv->locale = g_strdup (locale);
	g_object_notify (G_OBJECT (client), "locale");
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
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	return client->priv->locale;
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
	g_return_if_fail (PK_IS_CLIENT (client));
	client->priv->background = background;
	g_object_notify (G_OBJECT (client), "background");
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
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	return client->priv->background;
}

/**
 * pk_client_set_interactive:
 * @client: a valid #PkClient instance
 * @interactive: the value to set
 *
 * Sets the interactive value for the client. Interactive transactions
 * are usally allowed to ask the user questions.
 *
 * Since: 0.6.10
 **/
void
pk_client_set_interactive (PkClient *client, gboolean interactive)
{
	g_return_if_fail (PK_IS_CLIENT (client));
	client->priv->interactive = interactive;
	g_object_notify (G_OBJECT (client), "interactive");
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
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	return client->priv->interactive;
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
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	return client->priv->idle;
}

/**
 * pk_client_set_cache_age:
 * @client: a valid #PkClient instance
 * @cache_age: the cache age to set
 *
 * Sets the maximum cache age value for the client.
 *
 * Since: 0.6.10
 **/
void
pk_client_set_cache_age (PkClient *client, guint cache_age)
{
	g_return_if_fail (PK_IS_CLIENT (client));
	client->priv->cache_age = cache_age;
	g_object_notify (G_OBJECT (client), "cache-age");
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
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	return client->priv->cache_age;
}

/**
 * pk_client_class_init:
 **/
static void
pk_client_class_init (PkClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_client_finalize;
	object_class->get_property = pk_client_get_property;
	object_class->set_property = pk_client_set_property;

	/**
	 * PkClient:locale:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_string ("locale", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LOCALE, pspec);

	/**
	 * PkClient:background:
	 *
	 * Since: 0.5.3
	 */
	pspec = g_param_spec_boolean ("background", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKGROUND, pspec);

	/**
	 * PkClient:interactive:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boolean ("interactive", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	/**
	 * PkClient:idle:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boolean ("idle", NULL, "if there are no transactions in progress on this client",
				      TRUE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_IDLE, pspec);

	g_type_class_add_private (klass, sizeof (PkClientPrivate));

	/**
	 * PkClient:cache-age:
	 *
	 * Since: 0.6.10
	 */
	pspec = g_param_spec_uint ("cache-age", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CACHE_AGE, pspec);
}

/**
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	client->priv = PK_CLIENT_GET_PRIVATE (client);
	client->priv->calls = g_ptr_array_new ();
	client->priv->background = FALSE;
	client->priv->interactive = TRUE;
	client->priv->idle = TRUE;
	client->priv->cache_age = 0;

	/* use a control object */
	client->priv->control = pk_control_new ();

	/* cache locale */
	client->priv->locale = 	g_strdup (setlocale (LC_MESSAGES, NULL));
}

/**
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = client->priv;

	/* ensure we cancel any in-flight DBus calls */
	pk_client_cancel_all_dbus_methods (client);

	g_free (client->priv->locale);
	g_object_unref (priv->control);
	g_ptr_array_unref (priv->calls);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 *
 * PkClient is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new %PkClient instance
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
