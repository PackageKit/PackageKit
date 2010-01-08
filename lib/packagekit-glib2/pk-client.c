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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include <dbus/dbus-glib.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <locale.h>
#include <stdlib.h>

#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-marshal.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-package-ids.h>

#include "egg-debug.h"

static void     pk_client_finalize	(GObject     *object);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

#define PK_CLIENT_DBUS_METHOD_TIMEOUT	1500 /* ms */

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct _PkClientPrivate
{
	DBusGConnection		*connection;
	GPtrArray		*calls;
	PkControl		*control;
	gchar			*locale;
	gboolean		 background;
	gboolean		 interactive;
	gboolean		 idle;
};

enum {
	PROP_0,
	PROP_LOCALE,
	PROP_BACKGROUND,
	PROP_INTERACTIVE,
	PROP_IDLE,
	PROP_LAST
};

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

typedef struct {
	gboolean			 allow_deps;
	gboolean			 autoremove;
	gboolean			 enabled;
	gboolean			 force;
	gboolean			 only_trusted;
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
	gchar				*search;
	gchar				*tid;
	gchar				*transaction_id;
	gchar				*value;
	gpointer			 progress_user_data;
	gpointer			 user_data;
	guint				 number;
	gulong				 cancellable_id;
	DBusGProxyCall			*call;
	DBusGProxyCall			*call_interface_changed;
	DBusGProxy			*proxy;
	DBusGProxy			*proxy_props;
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
	guint				 refcount;
	gboolean			 signals_connected;
} PkClientState;

static void pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClientState *state);
static void pk_client_disconnect_proxy (DBusGProxy *proxy, PkClientState *state);

/**
 * pk_client_error_quark:
 *
 * Return value: Our personal error quark.
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
	const gchar *name;

	g_return_if_fail (error != NULL);

	/* old style PolicyKit failure */
	if (g_str_has_prefix (error->message, "org.freedesktop.packagekit.")) {
		egg_debug ("fixing up code for Policykit auth failure");
		error->code = PK_CLIENT_ERROR_FAILED_AUTH;
		g_free (error->message);
		error->message = g_strdup ("PolicyKit authorization failure");
		goto out;
	}

	/* find a better failure code */
	if (error->domain == DBUS_GERROR &&
	    error->code == DBUS_GERROR_REMOTE_EXCEPTION) {

		/* use one of our local codes */
		name = dbus_g_error_get_name (error);

		/* fall back to generic */
		error->code = PK_CLIENT_ERROR_FAILED;

		/* trim common prefix */
		if (g_str_has_prefix (name, "org.freedesktop.PackageKit.Transaction."))
			name = &name[39];

		/* try to get a better error */
		if (g_str_has_prefix (name, "PermissionDenied") ||
		    g_str_has_prefix (name, "RefusedByPolicy")) {
			error->code = PK_CLIENT_ERROR_FAILED_AUTH;
			goto out;
		}
		if (g_str_has_prefix (name, "PackageIdInvalid") ||
			 g_str_has_prefix (name, "SearchInvalid") ||
			 g_str_has_prefix (name, "FilterInvalid") ||
			 g_str_has_prefix (name, "InvalidProvide") ||
			 g_str_has_prefix (name, "InputInvalid")) {
			error->code = PK_CLIENT_ERROR_INVALID_INPUT;
			goto out;
		}
		if (g_str_has_prefix (name, "PackInvalid") ||
		    g_str_has_prefix (name, "NoSuchFile") ||
		    g_str_has_prefix (name, "MimeTypeNotSupported") ||
		    g_str_has_prefix (name, "NoSuchDirectory")) {
			error->code = PK_CLIENT_ERROR_INVALID_FILE;
			goto out;
		}
		if (g_str_has_prefix (name, "NotSupported")) {
			error->code = PK_CLIENT_ERROR_NOT_SUPPORTED;
			goto out;
		}
		egg_warning ("couldn't parse execption '%s', please report", name);
	}

out:
	/* hardcode domain */
	error->domain = PK_CLIENT_ERROR;
	return;
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
 * pk_client_real_paths:
 **/
static gchar **
pk_client_real_paths (gchar **paths)
{
	guint i;
	guint len;
	gchar **res;

	/* create output array */
	len = g_strv_length (paths);
	res = g_new0 (gchar *, len+1);

	/* resolve each path */
	for (i=0; i<len; i++)
		res[i] = pk_client_real_path (paths[i]);
	return res;
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
	path = g_build_filename (g_get_home_dir (), ".PackageKit", subfolder, NULL);

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
 * pk_client_get_properties_collect_cb:
 **/
static void
pk_client_get_properties_collect_cb (const char *key, const GValue *value, PkClientState *state)
{
	gboolean ret;
	const gchar *package_id;

	/* role */
	if (g_strcmp0 (key, "Role") == 0) {
		ret = pk_progress_set_role (state->progress, pk_role_enum_from_text (g_value_get_string (value)));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_ROLE, state->progress_user_data);
		return;
	}

	/* status */
	if (g_strcmp0 (key, "Status") == 0) {
		ret = pk_progress_set_status (state->progress, pk_status_enum_from_text (g_value_get_string (value)));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);
		return;
	}

	/* last-package */
	if (g_strcmp0 (key, "LastPackage") == 0) {
		package_id = g_value_get_string (value);
		/* check to see if it's been set yet */
		ret = pk_package_id_check (package_id);
		if (!ret)
			return;
		ret = pk_progress_set_package_id (state->progress, package_id);
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_PACKAGE_ID, state->progress_user_data);
		return;
	}

	/* percentage */
	if (g_strcmp0 (key, "Percentage") == 0) {
		ret = pk_progress_set_percentage (state->progress, pk_client_percentage_to_signed (g_value_get_uint (value)));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);
		return;
	}

	/* subpercentage */
	if (g_strcmp0 (key, "Subpercentage") == 0) {
		ret = pk_progress_set_subpercentage (state->progress, pk_client_percentage_to_signed (g_value_get_uint (value)));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_SUBPERCENTAGE, state->progress_user_data);
		return;
	}

	/* allow-cancel */
	if (g_strcmp0 (key, "AllowCancel") == 0) {
		ret = pk_progress_set_allow_cancel (state->progress, g_value_get_boolean (value));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_ALLOW_CANCEL, state->progress_user_data);
		return;
	}

	/* caller-active */
	if (g_strcmp0 (key, "CallerActive") == 0) {
		ret = pk_progress_set_caller_active (state->progress, g_value_get_boolean (value));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_CALLER_ACTIVE, state->progress_user_data);
		return;
	}

	/* elapsed-time */
	if (g_strcmp0 (key, "ElapsedTime") == 0) {
		ret = pk_progress_set_elapsed_time (state->progress, g_value_get_uint (value));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_ELAPSED_TIME, state->progress_user_data);
		return;
	}

	/* remaining-time */
	if (g_strcmp0 (key, "RemainingTime") == 0) {
		ret = pk_progress_set_elapsed_time (state->progress, g_value_get_uint (value));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_REMAINING_TIME, state->progress_user_data);
		return;
	}

	/* speed */
	if (g_strcmp0 (key, "Speed") == 0) {
		ret = pk_progress_set_speed (state->progress, g_value_get_uint (value));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_SPEED, state->progress_user_data);
		return;
	}

	/* uid */
	if (g_strcmp0 (key, "Uid") == 0) {
		ret = pk_progress_set_uid (state->progress, g_value_get_uint (value));
		if (ret && state->progress_callback != NULL)
			state->progress_callback (state->progress, PK_PROGRESS_TYPE_UID, state->progress_user_data);
		return;
	}

	egg_warning ("unhandled property '%s'", key);
}

/**
 * pk_client_cancel_cb:
 **/
static void
pk_client_cancel_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		/* there's not really a lot we can do here */
		egg_warning ("failed to cancel: %s", error->message);
		g_error_free (error);
	}

	/* finished this call */
	egg_debug ("cancelled %s, ended DBus call: %p", state->tid, state->call);
	state->call = NULL;
}

/**
 * pk_client_cancellable_cancel_cb:
 **/
static void
pk_client_cancellable_cancel_cb (GCancellable *cancellable, PkClientState *state)
{
	/* dbus method has not yet fired */
	if (state->proxy == NULL) {
		egg_warning ("Cancelled, but no proxy, not sure what to do here");
		return;
	}

	/* dbus method is pending now, just cancel */
	if (state->call != NULL) {
		dbus_g_proxy_cancel_call (state->proxy, state->call);
		egg_debug ("cancelling %s, ended DBus call: %p", state->tid, state->call);
		state->call = NULL;
		return;
	}
	if (state->call_interface_changed != NULL) {
		dbus_g_proxy_cancel_call (state->proxy, state->call_interface_changed);
		egg_debug ("cancelling %s, ended DBus call: %p", state->tid, state->call_interface_changed);
		state->call_interface_changed = NULL;
	}

	/* takeover the call with the cancel method */
	state->call = dbus_g_proxy_begin_call (state->proxy, "Cancel",
					       (DBusGProxyCallNotify) pk_client_cancel_cb, state,
					       NULL, G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");
	egg_debug ("cancelling %s (%p)", state->tid, state->call);
}

/**
 * pk_client_state_remove:
 **/
static void
pk_client_state_remove (PkClient *client, PkClientState *state)
{
	gboolean is_idle;
	g_ptr_array_remove (client->priv->calls, state);
	egg_debug ("state array remove %p", state);

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
	egg_debug ("state array add %p", state);

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

	/* force finished (if not already set) so clients can update the UI's */
	ret = pk_progress_set_status (state->progress, PK_STATUS_ENUM_FINISHED);
	if (ret && state->progress_callback != NULL)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);

	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}

	if (state->proxy != NULL) {
		pk_client_disconnect_proxy (state->proxy, state);
		g_object_unref (G_OBJECT (state->proxy));
	}

	if (state->proxy_props != NULL)
		g_object_unref (G_OBJECT (state->proxy_props));

	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_object_ref (state->results), g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
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
	g_free (state->search);
	g_free (state->value);
	g_free (state->tid);
	g_free (state->transaction_id);
	g_strfreev (state->files);
	g_strfreev (state->package_ids);
	/* results will no exists if the GetTid fails */
	if (state->results != NULL)
		g_object_unref (state->results);
	g_object_unref (state->progress);
	g_object_unref (state->res);
	g_object_unref (state->client);
	g_slice_free (PkClientState, state);
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
		egg_warning ("internal error, no files in array");
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
	egg_debug ("finished copy of %s", path);

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
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);

	/* calculate percentage */
	if (total_num_bytes > 0)
		percentage = 100 * current_num_bytes / total_num_bytes;

	/* save percentage */
	ret = pk_progress_set_percentage (state->progress, percentage);
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);
}

/**
 * pk_client_copy_downloaded_file:
 */
static void
pk_client_copy_downloaded_file (PkClientState *state, const gchar *package_id, const gchar *source_file)
{
	gchar *basename;
	gchar *path;
	gchar **files;
	GFile *source;
	GFile *destination;
	PkFiles *item;

	/* generate the destination location */
	basename = g_path_get_basename (source_file);
	path = g_build_filename (state->directory, basename, NULL);

	/* copy async */
	egg_debug ("copy %s to %s", source_file, path);
	source = g_file_new_for_path (source_file);
	destination = g_file_new_for_path (path);
	g_file_copy_async (source, destination, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, state->cancellable,
			   (GFileProgressCallback) pk_client_copy_progress_cb, state,
			   (GAsyncReadyCallback) pk_client_copy_downloaded_finished_cb, state);

	/* Add the result (as a GStrv) to the results set */
	files = g_strsplit (path, ",", -1);
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      NULL);
	pk_results_add_files (state->results, item);

	/* free everything we've used */
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
		egg_warning ("internal error, no files in array");
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
	egg_debug ("%i files to copy", state->refcount);

	/* get a cached value, as pk_client_copy_downloaded_file() adds items */
	len = array->len;

	/* save percentage */
	ret = pk_progress_set_percentage (state->progress, -1);
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);

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
 * pk_client_finished_cb:
 */
static void
pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClientState *state)
{
	GError *error = NULL;
	PkExitEnum exit_enum;
	PkError *error_code = NULL;

	egg_debug ("exit_text=%s", exit_text);

	/* yay */
	exit_enum = pk_exit_enum_from_text (exit_text);
	pk_results_set_exit_code (state->results, exit_enum);

	/* failed */
	if (exit_enum == PK_EXIT_ENUM_FAILED) {

		/* get error code and error message */
		error_code = pk_results_get_error_code (state->results);
		if (error_code != NULL) {
			/* should only ever have one ErrorCode */
			error = g_error_new (PK_CLIENT_ERROR, 0xFF + pk_error_get_code (error_code), "%s", pk_error_get_details (error_code));
		} else {
			/* fallback where the daemon didn't sent ErrorCode */
			error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "Failed: %s", exit_text);
		}
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* do we have to copy results? */
	if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		pk_client_copy_downloaded (state);
		goto out;
	}

	/* we're done */
	pk_client_state_finish (state, NULL);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
}

/**
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;

	/* finished this call */
	egg_debug ("got reply to request, ended DBus call: %p (%p)", state, state->call);
	state->call = NULL;

	/* get the result */
	state->ret = dbus_g_proxy_end_call (proxy, call, &error,
					    G_TYPE_INVALID);
	if (!state->ret) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, error);
		g_error_free (error);
		return;
	}

	/* wait for ::Finished() */
}

/**
 * pk_client_package_cb:
 */
static void
pk_client_package_cb (DBusGProxy *proxy, const gchar *info_text, const gchar *package_id, const gchar *summary, PkClientState *state)
{
	gboolean ret;
	PkInfoEnum info_enum;
	PkPackage *item;
	PkPackage *package;
	g_return_if_fail (PK_IS_CLIENT (state->client));

	/* add to results */
	info_enum = pk_info_enum_from_text (info_text);
	if (info_enum != PK_INFO_ENUM_FINISHED) {
		item = pk_package_new ();
		g_object_set (item,
			      "info", info_enum,
			      "package-id", package_id,
			      "summary", summary,
			      NULL);
		pk_results_add_package (state->results, item);
		g_object_unref (item);
	}

	/* save package-id */
	ret = pk_progress_set_package_id (state->progress, package_id);
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PACKAGE_ID, state->progress_user_data);

	/* save package object */
	package = pk_package_new ();
	pk_package_set_id (package, package_id, NULL);
	g_object_set (package,
		      "info", info_enum,
		      "summary", summary,
		      NULL);
	ret = pk_progress_set_package (state->progress, package);
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PACKAGE, state->progress_user_data);
	g_object_unref (package);
}

/**
 * pk_client_get_properties_cb:
 **/
static void
pk_client_get_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;
	GHashTable *hash;
	gboolean ret;

	egg_debug ("got properties, ended DBus call: %p (%p)", state, state->call_interface_changed);
	state->call_interface_changed = NULL;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to get properties: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* process results */
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_client_get_properties_collect_cb, state);
		g_hash_table_unref (hash);
	}

out:
	/* finished this call */
	egg_debug ("got property results, ended DBus call: %p (%p)", state, state->call);
	state->call = NULL;
}

/**
 * pk_client_changed_cb:
 */
static void
pk_client_changed_cb (DBusGProxy *proxy, PkClientState *state)
{
	/* successive quick Changed events */
	if (state->call_interface_changed != NULL) {
		egg_debug ("already processing request %p, so ignoring", state->call_interface_changed);
		return;
	}

	/* call D-Bus get_properties async */
	state->call_interface_changed =
		dbus_g_proxy_begin_call (state->proxy_props, "GetAll",
				         (DBusGProxyCallNotify) pk_client_get_properties_cb, state, NULL,
				         G_TYPE_STRING, "org.freedesktop.PackageKit.Transaction",
				         G_TYPE_INVALID);
	if (state->call_interface_changed == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");
	egg_debug ("changed so checking properties, started DBus call: %p (%p)", state, state->call_interface_changed);

	/* we've sent this async */
	egg_debug ("interface changed, started DBus call: %p (%p)", state, state->call_interface_changed);
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
	PkDetails *item;
	group_enum = pk_group_enum_from_text (group_text);

	/* add to results */
	item = pk_details_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "license", license,
		      "group", group_enum,
		      "description", description,
		      "url", url,
		      "size", size,
		      NULL);
	pk_results_add_details (state->results, item);
	g_object_unref (item);
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
	PkUpdateStateEnum state_enum;
	PkRestartEnum restart_enum;
	PkUpdateDetail *item;

	restart_enum = pk_restart_enum_from_text (restart_text);
	state_enum = pk_update_state_enum_from_text (state_text);

	/* add to results */
	item = pk_update_detail_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "updates", updates,
		      "obsoletes", obsoletes,
		      "vendor-url", vendor_url,
		      "bugzilla-url", bugzilla_url,
		      "cve-url", cve_url,
		      "restart", restart_enum,
		      "update-text", update_text,
		      "changelog", changelog,
		      "state", state_enum,
		      "issued", issued_text,
		      "updated", updated_text,
		      NULL);
	pk_results_add_update_detail (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_transaction_cb:
 */
static void
pk_client_transaction_cb (DBusGProxy *proxy, const gchar *tid, const gchar *timespec,
			  gboolean succeeded, const gchar *role_text, guint duration,
			  const gchar *data, guint uid, const gchar *cmdline, PkClientState *state)
{
	PkRoleEnum role_enum;
	PkTransactionPast *item;
	role_enum = pk_role_enum_from_text (role_text);

	/* add to results */
	item = pk_transaction_past_new ();
	g_object_set (item,
		      "tid", tid,
		      "timespec", timespec,
		      "succeeded", succeeded,
		      "role", role_enum,
		      "duration", duration,
		      "data", data,
		      "uid", uid,
		      "cmdline", cmdline,
		      NULL);
	pk_results_add_transaction (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_distro_upgrade_cb:
 */
static void
pk_client_distro_upgrade_cb (DBusGProxy *proxy, const gchar *type_text, const gchar *name,
			     const gchar *summary, PkClientState *state)
{
	PkUpdateStateEnum type_enum;
	PkDistroUpgrade *item;
	type_enum = pk_update_state_enum_from_text (type_text);

	/* add to results */
	item = pk_distro_upgrade_new ();
	g_object_set (item,
		      "type", type_enum,
		      "name", name,
		      "summary", summary,
		      NULL);
	pk_results_add_distro_upgrade (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_require_restart_cb:
 */
static void
pk_client_require_restart_cb (DBusGProxy  *proxy, const gchar *restart_text, const gchar *package_id, PkClientState *state)
{
	PkRestartEnum restart_enum;
	PkRequireRestart *item;
	restart_enum = pk_restart_enum_from_text (restart_text);

	/* add to results */
	item = pk_require_restart_new ();
	g_object_set (item,
		      "restart", restart_enum,
		      "package-id", package_id,
		      NULL);
	pk_results_add_require_restart (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_category_cb:
 */
static void
pk_client_category_cb (DBusGProxy  *proxy, const gchar *parent_id, const gchar *cat_id,
		       const gchar *name, const gchar *summary, const gchar *icon, PkClientState *state)
{
	PkCategory *item;

	/* add to results */
	item = pk_category_new ();
	g_object_set (item,
		      "parent-id", parent_id,
		      "cat-id", cat_id,
		      "name", name,
		      "summary", summary,
		      "icon", icon,
		      NULL);
	pk_results_add_category (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_files_cb:
 */
static void
pk_client_files_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *filelist, PkClientState *state)
{
	gchar **files;
	PkFiles *item;
	files = g_strsplit (filelist, ";", -1);

	/* add to results */
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      NULL);
	pk_results_add_files (state->results, item);
	g_object_unref (item);
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
	PkRepoSignatureRequired *item;
	type_enum = pk_sig_type_enum_from_text (type_text);

	/* add to results */
	item = pk_repo_signature_required_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "repository-name", repository_name,
		      "key-url", key_url,
		      "key-userid", key_userid,
		      "key-id", key_id,
		      "key-fingerprint", key_fingerprint,
		      "key-timestamp", key_timestamp,
		      "type", type_enum,
		      NULL);
	pk_results_add_repo_signature_required (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_eula_required_cb:
 **/
static void
pk_client_eula_required_cb (DBusGProxy *proxy, const gchar *eula_id, const gchar *package_id,
			    const gchar *vendor_name, const gchar *license_agreement, PkClientState *state)
{
	PkEulaRequired *item;

	/* add to results */
	item = pk_eula_required_new ();
	g_object_set (item,
		      "eula-id", eula_id,
		      "package-id", package_id,
		      "vendor-name", vendor_name,
		      "license-agreement", license_agreement,
		      NULL);
	pk_results_add_eula_required (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_media_change_required_cb:
 **/
static void
pk_client_media_change_required_cb (DBusGProxy *proxy, const gchar *media_type_text,
				    const gchar *media_id, const gchar *media_text, PkClientState *state)
{
	PkMediaTypeEnum media_type_enum;
	PkMediaChangeRequired *item;
	media_type_enum = pk_media_type_enum_from_text (media_type_text);

	/* add to results */
	item = pk_media_change_required_new ();
	g_object_set (item,
		      "media-type", media_type_enum,
		      "media-id", media_id,
		      "media-text", media_text,
		      NULL);
	pk_results_add_media_change_required (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_repo_detail_cb:
 **/
static void
pk_client_repo_detail_cb (DBusGProxy *proxy, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkClientState *state)
{
	PkRepoDetail *item;

	/* add to results */
	item = pk_repo_detail_new ();
	g_object_set (item,
		      "repo-id", repo_id,
		      "description", description,
		      "enabled", enabled,
		      NULL);
	pk_results_add_repo_detail (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_error_code_cb:
 */
static void
pk_client_error_code_cb (DBusGProxy *proxy, const gchar *code_text, const gchar *details, PkClientState *state)
{
	PkErrorEnum code_enum;
	PkError *item;
	code_enum = pk_error_enum_from_text (code_text);

	/* add to results */
	item = pk_error_new ();
	g_object_set (item,
		      "code", code_enum,
		      "details", details,
		      NULL);
	pk_results_set_error_code (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_message_cb:
 */
static void
pk_client_message_cb (DBusGProxy  *proxy, const gchar *message_text, const gchar *details, PkClientState *state)
{
	PkMessageEnum message_enum;
	PkMessage *item;
	message_enum = pk_message_enum_from_text (message_text);

	/* add to results */
	item = pk_message_new ();
	g_object_set (item,
		      "type", message_enum,
		      "details", details,
		      NULL);
	pk_results_add_message (state->results, item);
	g_object_unref (item);
}

/**
 * pk_client_connect_proxy:
 **/
static void
pk_client_connect_proxy (DBusGProxy *proxy, PkClientState *state)
{
	/* sanity check */
	if (state->signals_connected) {
		egg_warning ("not connecting as already connected");
		return;
	}

	/* add the signal types */
	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
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
	dbus_g_proxy_add_signal (proxy, "Destroy", G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Category", G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "MediaChangeRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Changed", G_TYPE_INVALID);

	/* connect up the signals */
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), state, NULL);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_client_package_cb), state, NULL);
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
	dbus_g_proxy_connect_signal (proxy, "Changed",
				     G_CALLBACK (pk_client_changed_cb), state, NULL);

	/* save for sanity check */
	state->signals_connected = TRUE;
}

/**
 * pk_client_disconnect_proxy:
 **/
static void
pk_client_disconnect_proxy (DBusGProxy *proxy, PkClientState *state)
{
	/* sanity check */
	if (!state->signals_connected) {
		egg_debug ("not disconnecting as never connected");
		return;
	}

	dbus_g_proxy_disconnect_signal (proxy, "Finished",
					G_CALLBACK (pk_client_finished_cb), state);
	dbus_g_proxy_disconnect_signal (proxy, "Package",
					G_CALLBACK (pk_client_package_cb), state);
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
	dbus_g_proxy_disconnect_signal (proxy, "Changed",
					G_CALLBACK (pk_client_changed_cb), state);

	/* save for sanity check */
	state->signals_connected = FALSE;
}

/**
 * pk_client_set_role:
 **/
static void
pk_client_set_role (PkClientState *state, PkRoleEnum role)
{
	gboolean ret;
	ret = pk_progress_set_role (state->progress, role);
	if (ret && state->progress_callback != NULL)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_ROLE, state->progress_user_data);
	return;
}

/**
 * pk_client_set_hints_cb:
 **/
static void
pk_client_set_hints_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	gchar *filters_text = NULL;
	const gchar *enum_text;
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, error);
		g_error_free (error);
		goto out;
	}

	/* finished this call */
	egg_debug ("set hints, ended DBus call: %p (%p)", state, state->call);
	state->call = NULL;

	/* we'll have results from now on */
	state->results = pk_results_new ();
	g_object_set (state->results,
		      "role", state->role,
		      "progress", state->progress,
		      NULL);

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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_NAME) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchName",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRING, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchDetails",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRING, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchGroup",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRING, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_SEARCH_FILE) {
		filters_text = pk_filter_bitfield_to_text (state->filters);
		state->call = dbus_g_proxy_begin_call (state->proxy, "SearchFile",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, filters_text,
						       G_TYPE_STRING, state->search,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_GET_DETAILS) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetDetails",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetUpdateDetail",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_GET_OLD_TRANSACTIONS) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetOldTransactions",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_UINT, state->number,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "DownloadPackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else if (state->role == PK_ROLE_ENUM_INSTALL_FILES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "InstallFiles",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_BOOLEAN, state->only_trusted,
						       G_TYPE_STRV, state->files,
						       G_TYPE_INVALID);
		g_object_set (state->results, "inputs", g_strv_length (state->files), NULL);
	} else if (state->role == PK_ROLE_ENUM_ACCEPT_EULA) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "AcceptEula",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, state->eula_id,
						       G_TYPE_INVALID);
	} else if (state->role == PK_ROLE_ENUM_ROLLBACK) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "Rollback",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRING, state->transaction_id,
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
		g_object_set (state->results, "inputs", g_strv_length (state->files), NULL);
	} else if (state->role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "SimulateInstallPackages",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
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
		g_object_set (state->results, "inputs", g_strv_length (state->package_ids), NULL);
	} else {
		g_assert_not_reached ();
	}

	/* check we called okay */
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");

	/* we've sent this async */
	egg_debug ("new method '%s', started DBus call: %p (%p)", pk_role_enum_to_text (state->role), state, state->call);

out:
	g_free (filters_text);
	return;
}

/**
 * pk_client_bool_to_text:
 **/
static const gchar *
pk_client_bool_to_text (gboolean value)
{
	if (value)
		return "true";
	return "false";
}

/**
 * pk_client_get_tid_cb:
 **/
static void
pk_client_get_tid_cb (GObject *object, GAsyncResult *res, PkClientState *state)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gchar *hint;
	gchar **hints;
	GPtrArray *array;

	state->tid = pk_control_get_tid_finish (control, res, &error);
	if (state->tid == NULL) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		return;
	}

	egg_debug ("tid = %s", state->tid);
	pk_progress_set_transaction_id (state->progress, state->tid);

	/* get a connection to the transaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, state->tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	/* get a connection to the properties interface */
	state->proxy_props = dbus_g_proxy_new_for_name (state->client->priv->connection,
							PK_DBUS_SERVICE, state->tid,
							"org.freedesktop.DBus.Properties");
	if (state->proxy_props == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* get hints */
	array = g_ptr_array_new_with_free_func (g_free);

	/* locale */
	if (state->client->priv->locale != NULL) {
		hint = g_strdup_printf ("locale=%s", state->client->priv->locale);
		g_ptr_array_add (array, hint);
	}

	/* background */
	hint = g_strdup_printf ("background=%s", pk_client_bool_to_text (state->client->priv->background));
	g_ptr_array_add (array, hint);

	/* interactive */
	hint = g_strdup_printf ("interactive=%s", pk_client_bool_to_text (state->client->priv->interactive));
	g_ptr_array_add (array, hint);

	/* set hints */
	hints = pk_ptr_array_to_strv (array);
	state->call = dbus_g_proxy_begin_call (state->proxy, "SetHints",
					       (DBusGProxyCallNotify) pk_client_set_hints_cb, state, NULL,
					       G_TYPE_STRV, hints,
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");
	egg_debug ("sent locale request, started DBus call: %p (%p)", state, state->call);

	/* track state */
	g_ptr_array_add (state->client->priv->calls, state);
	egg_debug ("state array add %p", state);

	/* we've sent this async */
	g_ptr_array_unref (array);
	g_strfreev (hints);
}

/**
 * pk_client_generic_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the #PkResults, or %NULL. Free with g_object_unref()
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
 * @packages: an array of package names to resolve, e.g. "gnome-system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Resolve a package name into a %package_id. This can return installed and
 * available packages and allows you find out if a package is installed locally
 * or is available in a repository.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->package_ids = g_strdupv (packages);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_names_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search all the locally installed files and remote repositories for a package
 * that matches a specific name.
 **/
void
pk_client_search_names_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_names_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_NAME;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->search = g_strjoinv ("&", values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_details_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search all detailed summary information to try and find a keyword.
 * Think of this as pk_client_search_names(), but trying much harder and
 * taking longer.
 **/
void
pk_client_search_details_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->search = g_strjoinv ("&", values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_groups_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: a group enum to search for, for instance, "system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Return all packages in a specific group.
 **/
void
pk_client_search_groups_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_groups_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_GROUP;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->search = g_strjoinv ("&", values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_files_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: file to search for, for instance, "/sbin/service"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search for packages that provide a specific file.
 **/
void
pk_client_search_files_async (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_search_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SEARCH_FILE;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->search = g_strjoinv ("&", values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_details_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get details of a package, so more information can be obtained for GUI
 * or command line tools.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_details_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DETAILS;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_update_detail_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get details about the specific update, for instance any CVE urls and
 * severity information.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_update_detail_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_download_packages_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the location where packages are to be downloaded
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Downloads package files to a specified location.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_download_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_updates_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_DEVEL or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get a list of all the packages that can be updated for all repositories.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_old_transactions_async:
 * @client: a valid #PkClient instance
 * @number: the number of past transactions to return, or 0 for all
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the old transaction list, mainly used for the rollback viewer.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->number = number;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_update_system_async:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Update all the packages on the system with the highest versions found in all
 * repositories.
 * NOTE: you can't choose what repositories to update from, but you can do:
 * - pk_client_repo_disable()
 * - pk_client_update_system()
 * - pk_client_repo_enable()
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->only_trusted = only_trusted;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_depends_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for depends
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the packages that depend this one, i.e. child->parent.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_depends_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DEPENDS;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->recursive = recursive;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_packages_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the list of packages from the backend
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_requires_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for requires
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the packages that require this one, i.e. parent->child.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_requires_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_REQUIRES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->recursive = recursive;
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_what_provides_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @provides: a #PkProvidesEnum value such as PK_PROVIDES_ENUM_CODEC
 * @values: a search term such as "sound/mp3"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * This should return packages that provide the supplied attributes.
 * This method is useful for finding out what package(s) provide a modalias
 * or GStreamer codec string.
 **/
void
pk_client_what_provides_async (PkClient *client, PkBitfield filters, PkProvidesEnum provides, gchar **values, GCancellable *cancellable,
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->provides = provides;
	state->search = g_strjoinv ("&", values);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
 * This method should return a list of distribution upgrades that are available.
 * It should not return updates, only major upgrades.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_files_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the file list (i.e. a list of files installed) for the specified package.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_FILES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
 * Get a list of all categories supported.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_remove_packages_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependant packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Remove a package (optionally with dependancies) from the system.
 * If %allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_refresh_cache_async:
 * @client: a valid #PkClient instance
 * @force: if we should aggressively drop caches
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Refresh the cache, i.e. download new metadata from a remote URL so that
 * package lists are up to date.
 * This action may take a few minutes and should be done when the session and
 * system are idle.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->force = force;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_install_packages_async:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a package of the newest and most correct version.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->only_trusted = only_trusted;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_install_signature_async:
 * @client: a valid #PkClient instance
 * @type: the signature type, e.g. %PK_SIGTYPE_ENUM_GPG
 * @key_id: a key ID such as "0df23df"
 * @package_id: a signature_id structure such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a software source signature of the newest and most correct version.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->type = type;
	state->key_id = g_strdup (key_id);
	state->package_id = g_strdup (package_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_update_packages_async:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Update specific packages to the newest available versions.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->only_trusted = only_trusted;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}


/**
 * pk_client_copy_native_finished_cb:
 */
static void
pk_client_copy_native_finished_cb (GFile *file, GAsyncResult *res, PkClientState *state)
{
	gboolean ret;
	gchar *path;
	GError *error = NULL;

	/* debug */
	path = g_file_get_path (file);
	egg_debug ("finished copy of %s", path);

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
		pk_control_get_tid_async (state->client->priv->control, state->cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	}
out:
	g_free (path);
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
	egg_debug ("using temp dir %s", user_temp);

	/* save percentage */
	ret = pk_progress_set_percentage (state->progress, -1);
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);

	/* copy each file that is non-native */
	for (i=0; state->files[i] != NULL; i++) {
		ret = pk_client_is_file_native (state->files[i]);
		egg_debug ("%s native=%i", state->files[i], ret);
		if (!ret) {
			/* generate the destination location */
			basename = g_path_get_basename (state->files[i]);
			path = g_build_filename (user_temp, basename, NULL);
			egg_debug ("copy from %s to %s", state->files[i], path);
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
 * @only_trusted: only trusted packages should be installed
 * @files: a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 **/
void
pk_client_install_files_async (PkClient *client, gboolean only_trusted, gchar **files, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data,
			       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	gboolean ret;
	guint i;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (files != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->only_trusted = only_trusted;
	state->files = pk_client_real_paths (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

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
		pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * We may want to agree to a EULA dialog if one is presented.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->eula_id = g_strdup (eula_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_rollback_async:
 * @client: a valid #PkClient instance
 * @transaction_id: the <literal>transaction_id</literal> we want to return to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * We may want to agree to a EULA dialog if one is presented.
 **/
void
pk_client_rollback_async (PkClient *client, const gchar *transaction_id, GCancellable *cancellable,
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
	state->role = PK_ROLE_ENUM_ROLLBACK;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->transaction_id = g_strdup (transaction_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_get_repo_list_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_DEVEL or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Get the list of repositories installed on the system.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_repo_enable_async:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @enabled: if we should enable the repository
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Enable or disable the repository.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->enabled = enabled;
	state->repo_id = g_strdup (repo_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_repo_set_data_async:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @parameter: the parameter to change
 * @value: what we should change it to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * We may want to set a repository parameter.
 * NOTE: this is free text, and is left to the backend to define a format.
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
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->repo_id = g_strdup (repo_id);
	state->parameter = g_strdup (parameter);
	state->value = g_strdup (value);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_simulate_install_files_async:
 * @client: a valid #PkClient instance
 * @files: a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Simulate an installation of files.
 **/
void
pk_client_simulate_install_files_async (PkClient *client, gchar **files, GCancellable *cancellable,
					PkProgressCallback progress_callback, gpointer progress_user_data,
					GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;
	gboolean ret;
	guint i;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);
	g_return_if_fail (files != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_INSTALL_FILES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->files = pk_client_real_paths (files);

	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

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
		pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
		goto out;
	}

	/* copy the files first */
	pk_client_copy_non_native_then_get_tid (state);
out:
	g_object_unref (res);
}

/**
 * pk_client_simulate_install_packages_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Simulate an installation of packages.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_simulate_remove_packages_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Simulate a removal of packages.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_simulate_update_packages_async:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Simulate an update of packages.
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
	g_return_if_fail (package_ids != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, cancellable, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/***************************************************************************************************/

/**
 * pk_client_adopt_get_properties_cb:
 **/
static void
pk_client_adopt_get_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;
	GHashTable *hash;

	/* get the result */
	state->ret = dbus_g_proxy_end_call (proxy, call, &error,
					    dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash,
					    G_TYPE_INVALID);
	if (!state->ret) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		return;
	}

	/* finished this call */
	egg_debug ("coldplugged properties, ended DBus call: %p (%p)", state, state->call);
	state->call = NULL;

	/* setup the proxies ready for use */
	pk_client_connect_proxy (state->proxy, state);

	/* process results */
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_client_get_properties_collect_cb, state);
		g_hash_table_unref (hash);
	}

	/* this is the first time we'll know the actual role */
	if (state->role == PK_ROLE_ENUM_UNKNOWN) {
		g_object_get (state->progress,
			      "role", &state->role,
			      NULL);
		/* proxy this */
		g_object_set (state->results,
			      "role", state->role,
			      NULL);
	}

	/* we're waiting for finished */
}

/**
 * pk_client_adopt_async:
 * @client: a valid #PkClient instance
 * @transaction_id: a transaction ID such as "/21_ebcbdaae_data"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Adopt a transaction which allows the caller to monitor the state or cancel it.
 **/
void
pk_client_adopt_async (PkClient *client, const gchar *transaction_id, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data,
		       GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_adopt_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UNKNOWN;
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->tid = g_strdup (transaction_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_client_set_role (state, state->role);
	pk_progress_set_transaction_id (state->progress, state->tid);

	/* get a connection to the transaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, state->tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	/* get a connection to the properties interface */
	state->proxy_props = dbus_g_proxy_new_for_name (state->client->priv->connection,
							PK_DBUS_SERVICE, state->tid,
							"org.freedesktop.DBus.Properties");
	if (state->proxy_props == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* call D-Bus get_properties async */
	state->call = dbus_g_proxy_begin_call (state->proxy_props, "GetAll",
					       (DBusGProxyCallNotify) pk_client_adopt_get_properties_cb, state, NULL,
					       G_TYPE_STRING, "org.freedesktop.PackageKit.Transaction",
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");
	egg_debug ("coldplug adoptee, started DBus call: %p (%p)", state, state->call);

	/* we'll have results from now on */
	state->results = pk_results_new ();
	g_object_set (state->results,
		      "role", state->role,
		      "progress", state->progress,
		      NULL);

	/* track state */
	pk_client_state_add (client, state);

	g_object_unref (res);
}

/***************************************************************************************************/

/**
 * pk_client_get_progress_finish:
 * @client: a valid #PkClient instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function.
 *
 * Return value: the #PkProgress, or %NULL. Free with g_object_unref()
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
pk_client_get_progress_state_finish (PkClientState *state, GError *error)
{
	if (state->cancellable != NULL) {
		g_cancellable_disconnect (state->cancellable, state->cancellable_id);
		g_object_unref (state->cancellable);
	}

	if (state->proxy != NULL) {
		pk_client_disconnect_proxy (state->proxy, state);
		g_object_unref (G_OBJECT (state->proxy));
	}

	if (state->proxy_props != NULL)
		g_object_unref (G_OBJECT (state->proxy_props));

	if (state->ret) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_object_ref (state->progress), g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
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
pk_client_get_progress_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;
	GHashTable *hash;

	/* get the result */
	state->ret = dbus_g_proxy_end_call (proxy, call, &error,
					    dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash,
					    G_TYPE_INVALID);
	if (!state->ret) {
		pk_client_get_progress_state_finish (state, error);
		return;
	}

	/* finished this call */
	egg_debug ("coldplugged properties, ended DBus call: %p (%p)", state, state->call);
	state->call = NULL;

	/* process results */
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_client_get_properties_collect_cb, state);
		g_hash_table_unref (hash);
	}

	/* we're done */
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
 **/
void
pk_client_get_progress_async (PkClient *client, const gchar *transaction_id, GCancellable *cancellable,
			      GAsyncReadyCallback callback_ready, gpointer user_data)
{
	GSimpleAsyncResult *res;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_progress_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->res = g_object_ref (res);
	state->client = g_object_ref (client);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->tid = g_strdup (transaction_id);
	state->progress = pk_progress_new ();
	pk_progress_set_transaction_id (state->progress, state->tid);

	/* get a connection to the transaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, state->tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	/* get a connection to the properties interface */
	state->proxy_props = dbus_g_proxy_new_for_name (state->client->priv->connection,
							PK_DBUS_SERVICE, state->tid,
							"org.freedesktop.DBus.Properties");
	if (state->proxy_props == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* timeout if we fail to get properties */
	dbus_g_proxy_set_default_timeout (state->proxy_props, PK_CLIENT_DBUS_METHOD_TIMEOUT);

	/* call D-Bus get_properties async */
	state->call = dbus_g_proxy_begin_call (state->proxy_props, "GetAll",
					       (DBusGProxyCallNotify) pk_client_get_progress_cb, state, NULL,
					       G_TYPE_STRING, "org.freedesktop.PackageKit.Transaction",
					       G_TYPE_INVALID);
	if (state->call == NULL)
		egg_error ("failed to setup call, maybe OOM or no connection");
	egg_debug ("getting progress on %s, started DBus call: %p", state->tid, state->call);

	/* track state */
	pk_client_state_add (client, state);

	g_object_unref (res);
}

/***************************************************************************************************/

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
		if (state->call == NULL)
			continue;
		egg_debug ("cancel in flight call: %p (%p)", state, state->call);
		dbus_g_proxy_cancel_call (state->proxy, state->call);
	}

	return TRUE;
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
	 */
	pspec = g_param_spec_string ("locale", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LOCALE, pspec);

	/**
	 * PkClient:background:
	 */
	pspec = g_param_spec_boolean ("background", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKGROUND, pspec);

	/**
	 * PkClient:interactive:
	 */
	pspec = g_param_spec_boolean ("interactive", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	/**
	 * PkClient:idle:
	 */
	pspec = g_param_spec_boolean ("idle", NULL, "if there are no transactions in progress on this client",
				      TRUE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_IDLE, pspec);

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
	client->priv->calls = g_ptr_array_new ();
	client->priv->background = FALSE;
	client->priv->interactive = TRUE;
	client->priv->idle = TRUE;

	/* check dbus connections, exit if not valid */
	client->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
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

	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	/* ErrorCode, RequireRestart, Message */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

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

	/* cache locale */
	client->priv->locale = 	g_strdup (setlocale (LC_ALL, NULL));
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
#include "pk-control-sync.h"
#include "pk-client-sync.h"

static void
pk_client_test_resolve_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	gboolean idle;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));

	packages = pk_results_get_package_array (results);
	if (packages == NULL)
		egg_test_failed (test, "no packages!");

	/* check idle */
	g_object_get (client, "idle", &idle, NULL);
	if (!idle)
		egg_test_failed (test, "not idle in finished handler");

	if (packages->len != 2)
		egg_test_failed (test, "invalid number of packages: %i", packages->len);

	g_ptr_array_unref (packages);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
out:
	if (results != NULL)
		g_object_unref (results);
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

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to get details: %s", pk_exit_enum_to_text (exit_enum));

	details = pk_results_get_details_array (results);
	if (details == NULL)
		egg_test_failed (test, "no details!");

	if (details->len != 1)
		egg_test_failed (test, "invalid number of details: %i", details->len);

	g_ptr_array_unref (details);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
out:
	if (results != NULL)
		g_object_unref (results);
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
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to get updates: %s", pk_exit_enum_to_text (exit_enum));

	sack = pk_results_get_package_sack (results);
	if (sack == NULL)
		egg_test_failed (test, "no details!");

	/* check size */
	size = pk_package_sack_get_size (sack);
	if (size != 3)
		egg_test_failed (test, "invalid number of updates: %i", size);

	g_object_unref (sack);

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
out:
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

static void
pk_client_test_search_name_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_CANCELLED)
		egg_test_failed (test, "failed to cancel search: %s", pk_exit_enum_to_text (exit_enum));

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (pk_error_get_code (error_code) != PK_ERROR_ENUM_TRANSACTION_CANCELLED)
		egg_test_failed (test, "failed to get error code: %i", pk_error_get_code (error_code));
	if (g_strcmp0 (pk_error_get_details (error_code), "The task was stopped successfully") != 0)
		egg_test_failed (test, "failed to get error message: %s", pk_error_get_details (error_code));
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

static guint _progress_cb = 0;
static guint _status_cb = 0;
static guint _package_cb = 0;
static guint _allow_cancel_cb = 0;
gchar *_tid = NULL;

static void
pk_client_test_progress_cb (PkProgress *progress, PkProgressType type, EggTest *test)
{
	gchar *tid;
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

	/* get the running transaction id if we've not set it before */
	g_object_get (progress, "transaction-id", &tid, NULL);
	if (tid != NULL && _tid == NULL)
		_tid = g_strdup (tid);
	g_free (tid);
}

static gboolean
pk_client_test_cancel (GCancellable *cancellable)
{
	egg_warning ("cancelling method");
	g_cancellable_cancel (cancellable);
	return FALSE;
}

static void
pk_client_test_download_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	PkFiles *item;
	GPtrArray *array = NULL;
	guint len;
	gchar *package_id = NULL;
	gchar **files = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to download: %s", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to download: %s", pk_exit_enum_to_text (exit_enum));

	/* check number */
	array = pk_results_get_files_array (results);
	if (array->len != 2)
		egg_test_failed (test, "invalid number of files: %i", array->len);

	/* check a result */
	item = g_ptr_array_index (array, 0);
	g_object_get (item,
		      "package-id", &package_id,
		      "files", &files,
		      NULL);
	if (g_strcmp0 (package_id, "powertop-common;1.8-1.fc8;i386;fedora") != 0)
		egg_test_failed (test, "invalid package_id: %s", package_id);
	len = g_strv_length (files);
	if (len != 1)
		egg_test_failed (test, "invalid number of files: %i", len);
	if (g_strcmp0 (files[0], "/tmp/powertop-common-1.8-1.fc8.rpm") != 0)
		egg_test_failed (test, "invalid filename: %s, maybe not rewritten", files[0]);
out:
	g_strfreev (files);
	g_free (package_id);
	g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

static void
pk_client_test_recursive_signal_cb (PkControl *control, EggTest *test)
{
	gboolean ret;
	ret = pk_control_get_properties (control, NULL, NULL);
	if (!ret)
		egg_test_failed (test, "could not get properties sync");
}

/**
 * pk_client_test_notify_idle_cb:
 **/
static void
pk_client_test_notify_idle_cb (PkClient *client, GParamSpec *pspec, EggTest *test)
{
	gboolean idle;
	g_object_get (client, "idle", &idle, NULL);
	egg_debug ("idle=%i", idle);
}

void
pk_client_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkClient *client;
	gchar **package_ids;
	gchar *file;
	GCancellable *cancellable;
	gboolean ret;
	gchar **values;
	GError *error = NULL;
	PkProgress *progress;
	gchar *tid;
	PkRoleEnum role;
	PkStatusEnum status;
	PkResults *results;

	if (!egg_test_start (test, "PkClient"))
		return;

	/************************************************************/
	egg_test_title (test, "test user temp");
	file = pk_client_get_user_temp ("self-test", NULL);
	if (g_str_has_suffix (file, "/.PackageKit/self-test") && g_str_has_prefix (file, "/home/"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "temp was %s", file);
	g_free (file);

	/************************************************************/
	egg_test_title (test, "test native TRUE");
	ret = pk_client_is_file_native ("/tmp");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "test native FALSE");
	ret = pk_client_is_file_native ("/tmp/.gvfs/moo");
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "test resolve NULL");
	file = pk_client_real_path (NULL);
	if (file == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "test resolve /etc/hosts");
	file = pk_client_real_path ("/etc/hosts");
	if (file != NULL && g_strcmp0 (file, "/etc/hosts") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got: %s", file);
	g_free (file);

	/************************************************************/
	egg_test_title (test, "test resolve /etc/../etc/hosts");
	file = pk_client_real_path ("/etc/../etc/hosts");
	if (file != NULL && g_strcmp0 (file, "/etc/hosts") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got: %s", file);
	g_free (file);

	/************************************************************/
	egg_test_title (test, "get client");
	client = pk_client_new ();
	g_signal_connect (client, "notify::idle",
			  G_CALLBACK (pk_client_test_notify_idle_cb), test);
	egg_test_assert (test, client != NULL);

	/************************************************************/
	egg_test_title (test, "check idle");
	g_object_get (client, "idle", &ret, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "resolve package");
	package_ids = pk_package_ids_from_text ("glib2;2.14.0;i386;fedora&powertop");
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, NULL,
				 (PkProgressCallback) pk_client_test_progress_cb, test,
				 (GAsyncReadyCallback) pk_client_test_resolve_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "check idle");
	g_object_get (client, "idle", &ret, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get progress of past transaction");
	progress = pk_client_get_progress (client, _tid, NULL, &error);
	g_object_get (progress,
		      "transaction-id", &tid,
		      "role", &role,
		      "status", &status,
		      NULL);
	if (g_strcmp0 (tid, _tid) != 0)
		egg_test_failed (test, "incorrect transaction-id, got %s, expected %s", tid, _tid);
	if (role != PK_ROLE_ENUM_RESOLVE)
		egg_test_failed (test, "incorrect role, got %s", pk_role_enum_to_text (role));
	if (status != PK_STATUS_ENUM_FINISHED)
		egg_test_failed (test, "incorrect status, got %s", pk_status_enum_to_text (status));
	egg_test_success (test, "got progress in %i", egg_test_elapsed (test));
	g_object_unref (progress);
	g_free (tid);
	g_free (_tid);

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
	package_ids = pk_package_ids_from_id ("powertop;1.8-1.fc8;i386;fedora");
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

	/* it takes more than 50ms to get the progress of the transaction, and if
	 * getting updates from internal cache, then it'll take a shed load less
	 * than this to complete */
	if (egg_test_elapsed (test) > 100) {
		/************************************************************/
		egg_test_title (test, "got status updates");
		if (_status_cb > 0)
			egg_test_success (test, NULL);
		else
			egg_test_failed (test, "got %i updates", _status_cb);
	}

	/************************************************************/
	egg_test_title (test, "search by name");
	cancellable = g_cancellable_new ();
	values = g_strsplit ("power", "&", -1);
	pk_client_search_names_async (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), values, cancellable,
				     (PkProgressCallback) pk_client_test_progress_cb, test,
				     (GAsyncReadyCallback) pk_client_test_search_name_cb, test);
	g_timeout_add (1000, (GSourceFunc) pk_client_test_cancel, cancellable);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "cancelled in %i", egg_test_elapsed (test));

	g_strfreev (values);
	g_cancellable_reset (cancellable);

	/************************************************************/
	egg_test_title (test, "do downloads");
	package_ids = pk_package_ids_from_id ("powertop;1.8-1.fc8;i386;fedora");
	pk_client_download_packages_async (client, package_ids, "/tmp", cancellable,
					   (PkProgressCallback) pk_client_test_progress_cb, test,
					   (GAsyncReadyCallback) pk_client_test_download_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "downloaded and copied in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "test recursive signal handling");
	g_signal_connect (client->priv->control, "repo-list-changed", G_CALLBACK (pk_client_test_recursive_signal_cb), test);
	results = pk_client_repo_set_data (client, "dave", "moo", "data", NULL, NULL, NULL, NULL);
	egg_test_assert (test, (results != NULL));

	g_object_unref (results);
	g_object_unref (cancellable);
	g_object_unref (client);

	egg_test_end (test);
}
#endif

