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
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-marshal.h>
#include <packagekit-glib2/pk-package-ids.h>

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
	GPtrArray		*calls;
	PkControl		*control;
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
 * pk_client_cancel_cb:
 **/
static void
pk_client_cancel_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;
	gboolean ret;

	egg_debug ("cancelled %s (%p)", state->tid, state->call);

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		/* there's not really a lot we can do here */
		egg_warning ("failed: %s", error->message);
		g_error_free (error);
	}

	/* finished this call */
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
		state->call = NULL;
		return;
	}

	/* takeover the call with the cancel method */
	state->call = dbus_g_proxy_begin_call (state->proxy, "Cancel",
					       (DBusGProxyCallNotify) pk_client_cancel_cb, state,
					       NULL, G_TYPE_INVALID);
	egg_debug ("cancelling %s (%p)", state->tid, state->call);
}

/**
 * pk_client_state_finish:
 **/
static void
pk_client_state_finish (PkClientState *state, GError *error)
{
	PkClientPrivate *priv;
	priv = state->client->priv;

	if (state->client != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

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
		g_error_free (error);
	}

	/* remove from list */
	g_ptr_array_remove (priv->calls, state);
	egg_debug ("state array remove %p", state);

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
	g_object_unref (state->progress);
	g_object_unref (state->results);
	g_object_unref (state->res);
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
	const PkItemFiles *item;
	GPtrArray *array = NULL;
	guint i;

	/* get the data */
	array = pk_results_get_files_array (state->results);
	if (array == NULL)
		egg_error ("internal error");

	/* remove any without dest path */
	for (i=0; i < array->len; ) {
		item = g_ptr_array_index (array, i);
		if (!g_str_has_prefix (item->files[0], state->directory))
			g_ptr_array_remove_index_fast (array, i);
		else
			i++;
	}

	/* we're done modifying the data */
	g_ptr_array_unref (array);
}

/**
 * pk_client_copy_finished_cb:
 */
static void
pk_client_copy_finished_cb (GFile *file, GAsyncResult *res, PkClientState *state)
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
		pk_client_state_finish (state, error);
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

	/* save progress */
	ret = pk_progress_set_status (state->progress, PK_STATUS_ENUM_REPACKAGING);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);
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
	PkItemFiles *item;

	/* generate the destination location */
	basename = g_path_get_basename (source_file);
	path = g_build_filename (state->directory, basename, NULL);

	/* copy async */
	egg_debug ("copy %s to %s", source_file, path);
	source = g_file_new_for_path (source_file);
	destination = g_file_new_for_path (path);
	g_file_copy_async (source, destination, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, state->cancellable,
			   (GFileProgressCallback) pk_client_copy_progress_cb, state,
			   (GAsyncReadyCallback) pk_client_copy_finished_cb, state);

	/* Add the result (as a GStrv) to the results set */
	files = g_strsplit (path, ",", -1);
	item = pk_item_files_new (package_id, files);
	pk_results_add_files (state->results, item);

	/* free everything we've used */
	pk_item_files_unref (item);
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
	const PkItemFiles *item;
	GPtrArray *array = NULL;

	/* get data */
	array = pk_results_get_files_array (state->results);
	if (array == NULL)
		egg_error ("internal error");
	/* get the number of files to copy */
	for (i=0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		state->refcount += g_strv_length (item->files);
	}
	egg_debug ("%i files to copy", state->refcount);

	/* get a cached value, as pk_client_copy_downloaded_file() adds items */
	len = array->len;

	/* do the copies pipelined */
	for (i=0; i < len; i++) {
		item = g_ptr_array_index (array, i);
		for (j=0; item->files[j] != NULL; j++)
			pk_client_copy_downloaded_file (state, item->package_id, item->files[j]);
	}
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
	PkItemErrorCode *error_item = NULL;

	egg_debug ("exit_text=%s", exit_text);

	/* yay */
	exit_enum = pk_exit_enum_from_text (exit_text);
	pk_results_set_exit_code (state->results, exit_enum);

	/* failed */
	if (exit_enum == PK_EXIT_ENUM_FAILED) {

		/* get error code and error message */
		error_item = pk_results_get_error_code (state->results);
		if (error_item != NULL) {
			/* should only ever have one ErrorCode */
			error = g_error_new (PK_CLIENT_ERROR, 0xFF + error_item->code, "%s", error_item->details);
		} else {
			/* fallback where the daemon didn't sent ErrorCode */
			error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "Failed: %s", exit_text);
		}
		pk_client_state_finish (state, error);
		goto out;
	}

	/* do we have to copy results? */
	if (state->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		pk_client_copy_downloaded (state);
		goto out;
	}

	/* we're done */
	pk_client_state_finish (state, error);
out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
}

/**
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;

	/* we've sent this async */
	egg_debug ("got reply to request");

	/* finished this call */
	state->call = NULL;

	/* get the result */
	state->ret = dbus_g_proxy_end_call (proxy, call, &error,
					    G_TYPE_INVALID);
	if (!state->ret) {
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
		pk_client_state_finish (state, error);
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
	PkItemPackage *item;
	g_return_if_fail (PK_IS_CLIENT (state->client));

	/* add to results */
	info_enum = pk_info_enum_from_text (info_text);
	if (info_enum != PK_INFO_ENUM_FINISHED) {
		item = pk_item_package_new (info_enum, package_id, summary);
		pk_results_add_package (state->results, item);
		pk_item_package_unref (item);
	}

	/* save progress */
	ret = pk_progress_set_package_id (state->progress, package_id);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PACKAGE_ID, state->progress_user_data);
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
 * pk_client_progress_changed_cb:
 */
static void
pk_client_progress_changed_cb (DBusGProxy *proxy, guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkClientState *state)
{
	gboolean ret;
	gint percentage_new;
	gint subpercentage_new;

	/* convert to signed */
	percentage_new = pk_client_percentage_to_signed (percentage);
	subpercentage_new = pk_client_percentage_to_signed (subpercentage);

	/* save progress */
	ret = pk_progress_set_percentage (state->progress, percentage_new);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);

	/* save progress */
	ret = pk_progress_set_subpercentage (state->progress, subpercentage_new);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_SUBPERCENTAGE, state->progress_user_data);
}

/**
 * pk_client_status_changed_cb:
 */
static void
pk_client_status_changed_cb (DBusGProxy *proxy, const gchar *status_text, PkClientState *state)
{
	gboolean ret;
	PkStatusEnum status_enum;

	/* convert from text */
	status_enum = pk_status_enum_from_text (status_text);

	/* save progress */
	ret = pk_progress_set_status (state->progress, status_enum);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);
}

/**
 * pk_client_allow_cancel_cb:
 */
static void
pk_client_allow_cancel_cb (DBusGProxy *proxy, gboolean allow_cancel, PkClientState *state)
{
	gboolean ret;

	/* save progress */
	ret = pk_progress_set_allow_cancel (state->progress, allow_cancel);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_ALLOW_CANCEL, state->progress_user_data);
}

/**
 * pk_client_caller_active_changed_cb:
 */
static void
pk_client_caller_active_changed_cb (DBusGProxy *proxy, gboolean caller_active, PkClientState *state)
{
	gboolean ret;

	/* save progress */
	ret = pk_progress_set_caller_active (state->progress, caller_active);

	/* do the callback for GUI programs */
	if (state->progress_callback != NULL && ret)
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
	PkItemDetails *item;
	group_enum = pk_group_enum_from_text (group_text);

	/* add to results */
	item = pk_item_details_new (package_id, license, group_enum, description, url, size);
	pk_results_add_details (state->results, item);
	pk_item_details_unref (item);
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
	PkItemUpdateDetail *item;

	restart_enum = pk_restart_enum_from_text (restart_text);
	state_enum = pk_update_state_enum_from_text (state_text);
	issued = pk_iso8601_to_date (issued_text);
	updated = pk_iso8601_to_date (updated_text);

	/* add to results */
	item = pk_item_update_detail_new (package_id, updates, obsoletes, vendor_url,
					  bugzilla_url, cve_url, restart_enum, update_text, changelog,
					  state_enum, issued, updated);
	pk_results_add_update_detail (state->results, item);
	pk_item_update_detail_unref (item);

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
	PkItemTransaction *item;
	role_enum = pk_role_enum_from_text (role_text);

	/* add to results */
	item = pk_item_transaction_new (old_tid, timespec, succeeded, role_enum, duration, data, uid, cmdline);
	pk_results_add_transaction (state->results, item);
	pk_item_transaction_unref (item);
}

/**
 * pk_client_distro_upgrade_cb:
 */
static void
pk_client_distro_upgrade_cb (DBusGProxy *proxy, const gchar *type_text, const gchar *name,
			     const gchar *summary, PkClientState *state)
{
	PkUpdateStateEnum type_enum;
	PkItemDistroUpgrade *item;
	type_enum = pk_update_state_enum_from_text (type_text);

	/* add to results */
	item = pk_item_distro_upgrade_new (type_enum, name, summary);
	pk_results_add_distro_upgrade (state->results, item);
	pk_item_distro_upgrade_unref (item);
}

/**
 * pk_client_require_restart_cb:
 */
static void
pk_client_require_restart_cb (DBusGProxy  *proxy, const gchar *restart_text, const gchar *package_id, PkClientState *state)
{
	PkRestartEnum restart_enum;
	PkItemRequireRestart *item;
	restart_enum = pk_restart_enum_from_text (restart_text);

	/* add to results */
	item = pk_item_require_restart_new (restart_enum, package_id);
	pk_results_add_require_restart (state->results, item);
	pk_item_require_restart_unref (item);
}

/**
 * pk_client_category_cb:
 */
static void
pk_client_category_cb (DBusGProxy  *proxy, const gchar *parent_id, const gchar *cat_id,
		       const gchar *name, const gchar *summary, const gchar *icon, PkClientState *state)
{
	PkItemCategory *item;

	/* add to results */
	item = pk_item_category_new (parent_id, cat_id, name, summary, icon);
	pk_results_add_category (state->results, item);
	pk_item_category_unref (item);
}

/**
 * pk_client_files_cb:
 */
static void
pk_client_files_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *filelist, PkClientState *state)
{
	gchar **files;
	PkItemFiles *item;
	files = g_strsplit (filelist, ";", -1);

	/* add to results */
	item = pk_item_files_new (package_id, files);
	pk_results_add_files (state->results, item);
	pk_item_files_unref (item);
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
	PkItemRepoSignatureRequired *item;
	type_enum = pk_sig_type_enum_from_text (type_text);

	/* add to results */
	item = pk_item_repo_signature_required_new (package_id, repository_name, key_url, key_userid,
						    key_id, key_fingerprint, key_timestamp, type_enum);
	pk_results_add_repo_signature_required (state->results, item);
	pk_item_repo_signature_required_unref (item);
}

/**
 * pk_client_eula_required_cb:
 **/
static void
pk_client_eula_required_cb (DBusGProxy *proxy, const gchar *eula_id, const gchar *package_id,
			    const gchar *vendor_name, const gchar *license_agreement, PkClientState *state)
{
	PkItemEulaRequired *item;

	/* add to results */
	item = pk_item_eula_required_new (eula_id, package_id, vendor_name, license_agreement);
	pk_results_add_eula_required (state->results, item);
	pk_item_eula_required_unref (item);
}

/**
 * pk_client_media_change_required_cb:
 **/
static void
pk_client_media_change_required_cb (DBusGProxy *proxy, const gchar *media_type_text,
				    const gchar *media_id, const gchar *media_text, PkClientState *state)
{
	PkMediaTypeEnum media_type_enum;
	PkItemMediaChangeRequired *item;
	media_type_enum = pk_media_type_enum_from_text (media_type_text);

	/* add to results */
	item = pk_item_media_change_required_new (media_type_enum, media_id, media_text);
	pk_results_add_media_change_required (state->results, item);
	pk_item_media_change_required_unref (item);
}

/**
 * pk_client_repo_detail_cb:
 **/
static void
pk_client_repo_detail_cb (DBusGProxy *proxy, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkClientState *state)
{
	PkItemRepoDetail *item;

	/* add to results */
	item = pk_item_repo_detail_new (repo_id, description, enabled);
	pk_results_add_repo_detail (state->results, item);
	pk_item_repo_detail_unref (item);
}

/**
 * pk_client_error_code_cb:
 */
static void
pk_client_error_code_cb (DBusGProxy *proxy, const gchar *code_text, const gchar *details, PkClientState *state)
{
	PkErrorCodeEnum code_enum;
	PkItemErrorCode *item;
	code_enum = pk_error_enum_from_text (code_text);

	/* add to results */
	item = pk_item_error_code_new (code_enum, details);
	pk_results_add_error_code (state->results, item);
	pk_item_error_code_unref (item);
}

/**
 * pk_client_message_cb:
 */
static void
pk_client_message_cb (DBusGProxy  *proxy, const gchar *message_text, const gchar *details, PkClientState *state)
{
	PkMessageEnum message_enum;
	PkItemMessage *item;
	message_enum = pk_message_enum_from_text (message_text);

	/* add to results */
	item = pk_item_message_new (message_enum, details);
	pk_results_add_message (state->results, item);
	pk_item_message_unref (item);
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
		/* fix up the D-Bus error */
		pk_client_fixup_dbus_error (error);
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
	} else if (state->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		state->call = dbus_g_proxy_begin_call (state->proxy, "GetUpdateDetail",
						       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
						       G_TYPE_STRV, state->package_ids,
						       G_TYPE_INVALID);
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
	g_object_set (state->results,
		      "role", state->role,
		      NULL);

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
	const gchar *locale;

	state->tid = pk_control_get_tid_finish (control, res, &error);
	if (state->tid == NULL) {
		pk_client_state_finish (state, error);
		g_error_free (error);
		return;
	}

	egg_debug ("tid = %s", state->tid);

	/* get a connection to the transaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, state->tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	/* set locale */
	locale = (const gchar *) setlocale (LC_ALL, NULL);
	state->call = dbus_g_proxy_begin_call (state->proxy, "SetLocale",
					       (DBusGProxyCallNotify) pk_client_set_locale_cb, state, NULL,
					       G_TYPE_STRING, locale,
					       G_TYPE_INVALID);

	/* track state */
	g_ptr_array_add (state->client->priv->calls, state);
	egg_debug ("state array add %p", state);

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
 * Return value: the #PkResults, or %NULL. Free with g_object_unref()
 **/
PkResults *
pk_client_generic_finish (PkClient *client, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->package_ids = g_strdupv (packages);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_name_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: free text to search for, for instance, "power"
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_details_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search all detailed summary information to try and find a keyword.
 * Think of this as pk_client_search_name(), but trying much harder and
 * taking longer.
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_group_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: a group enum to search for, for instance, "system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Return all packages in a specific group.
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_search_file_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: file to search for, for instance, "/sbin/service"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @callback_ready: the function to run on completion
 * @user_data: the data to pass to @callback_ready
 *
 * Search for packages that provide a specific file.
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_details_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DETAILS;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_update_detail_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_download_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->directory = g_strdup (directory);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->number = number;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->only_trusted = only_trusted;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_depends_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_DEPENDS;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->recursive = recursive;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_requires_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_REQUIRES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->recursive = recursive;
	state->filters = filters;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/**
 * pk_client_what_provides_async:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @provides: a #PkProvidesEnum value such as PK_PROVIDES_ENUM_CODEC
 * @search: a search term such as "sound/mp3"
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->provides = provides;
	state->search = g_strdup (search);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_get_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_GET_FILES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->allow_deps = allow_deps;
	state->autoremove = autoremove;
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->force = force;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->only_trusted = only_trusted;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->type = type;
	state->key_id = g_strdup (key_id);
	state->package_id = g_strdup (package_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->only_trusted = only_trusted;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
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

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_INSTALL_FILES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->only_trusted = only_trusted;
	state->files = pk_client_real_paths (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->eula_id = g_strdup (eula_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->transaction_id = g_strdup (transaction_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->filters = filters;
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->enabled = enabled;
	state->repo_id = g_strdup (repo_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->repo_id = g_strdup (repo_id);
	state->parameter = g_strdup (parameter);
	state->value = g_strdup (value);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback_ready != NULL);

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_install_files_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_INSTALL_FILES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->files = pk_client_real_paths (files);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_install_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_remove_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
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

	res = g_simple_async_result_new (G_OBJECT (client), callback_ready, user_data, pk_client_simulate_update_packages_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->role = PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES;
	state->res = g_object_ref (res);
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->package_ids = g_strdupv (package_ids);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (res);
}

/***************************************************************************************************/

/**
 * pk_client_get_properties_collect_cb:
 **/
static void
pk_client_get_properties_collect_cb (const char *key, const GValue *value, PkClientState *state)
{
	const gchar *tmp_str;
	guint tmp;
	gboolean ret;

	/* do the callback for GUI programs */
	if (state->progress_callback == NULL)
		return;

	/* role */
	if (g_strcmp0 (key, "Role") == 0) {
		tmp_str = g_value_get_string (value);
		tmp = pk_role_enum_from_text (tmp_str);
		g_object_set (state->progress, "role", tmp, NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_ROLE, state->progress_user_data);
		return;
	}

	/* status */
	if (g_strcmp0 (key, "Status") == 0) {
		tmp_str = g_value_get_string (value);
		tmp = pk_status_enum_from_text (tmp_str);
		g_object_set (state->progress, "status", tmp, NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_STATUS, state->progress_user_data);
		return;
	}

	/* last-package */
	if (g_strcmp0 (key, "LastPackage") == 0) {
		tmp_str = g_value_get_string (value);
		g_object_set (state->progress, "package-id", tmp_str, NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PACKAGE_ID, state->progress_user_data);
		return;
	}

#if 0
	/* uid */
	if (g_strcmp0 (key, "Uid") == 0) {
		tmp = g_value_get_uint (value);
		g_object_set (state->progress, "uid", tmp, NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_UID, state->progress_user_data);
		return;
	}
#endif

	/* percentage */
	if (g_strcmp0 (key, "Percentage") == 0) {
		tmp = g_value_get_uint (value);
		g_object_set (state->progress, "percentage", pk_client_percentage_to_signed (tmp), NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_PERCENTAGE, state->progress_user_data);
		return;
	}

	/* subpercentage */
	if (g_strcmp0 (key, "Subpercentage") == 0) {
		tmp = g_value_get_uint (value);
		g_object_set (state->progress, "subpercentage", pk_client_percentage_to_signed (tmp), NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_SUBPERCENTAGE, state->progress_user_data);
		return;
	}

	/* allow-cancel */
	if (g_strcmp0 (key, "AllowCancel") == 0) {
		ret = g_value_get_boolean (value);
		g_object_set (state->progress, "allow-cancel", ret, NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_ALLOW_CANCEL, state->progress_user_data);
		return;
	}

	/* caller-active */
	if (g_strcmp0 (key, "CallerActive") == 0) {
		ret = g_value_get_boolean (value);
		g_object_set (state->progress, "caller-active", ret, NULL);
		state->progress_callback (state->progress, PK_PROGRESS_TYPE_CALLER_ACTIVE, state->progress_user_data);
		return;
	}

	egg_warning ("unhandled property '%s'", key);
}

/**
 * pk_client_get_properties_cb:
 **/
static void
pk_client_get_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
	GError *error = NULL;
	GHashTable *hash;

	/* get the result */
	state->ret = dbus_g_proxy_end_call (proxy, call, &error,
					    dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE), &hash,
					    G_TYPE_INVALID);
	if (!state->ret) {
		pk_client_state_finish (state, error);
		return;
	}

	/* finished this call */
	state->call = NULL;

	/* setup the proxies ready for use */
	pk_client_connect_proxy (state->proxy, state);

	/* process results */
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_client_get_properties_collect_cb, state);
		g_hash_table_unref (hash);
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
	if (cancellable != NULL) {
		state->cancellable = g_object_ref (cancellable);
		state->cancellable_id = g_cancellable_connect (cancellable, G_CALLBACK (pk_client_cancellable_cancel_cb), state, NULL);
	}
	state->client = client;
	state->tid = g_strdup (transaction_id);
	state->progress_callback = progress_callback;
	state->progress_user_data = progress_user_data;
	state->progress = pk_progress_new ();
	pk_progress_set_role (state->progress, state->role);
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get a connection to the transaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, state->tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", state->tid);

	/* get a connection to the properties interface */
	state->proxy_props = dbus_g_proxy_new_for_name (state->client->priv->connection,
							PK_DBUS_SERVICE, state->tid,
							"org.freedesktop.DBus.Properties");
	if (state->proxy_props == NULL)
		egg_error ("Cannot connect to PackageKit on %s", transaction_id);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	/* call D-Bus get_properties async */
	state->call = dbus_g_proxy_begin_call (state->proxy_props, "GetAll",
					       (DBusGProxyCallNotify) pk_client_get_properties_cb, state, NULL,
					       G_TYPE_STRING, "org.freedesktop.PackageKit.Transaction",
					       G_TYPE_INVALID);

	/* we'll have results from now on */
	state->results = pk_results_new ();
	g_object_set (state->results,
		      "role", state->role,
		      NULL);

	/* track state */
	g_ptr_array_add (client->priv->calls, state);
	egg_debug ("state array add %p", state);

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
		egg_debug ("cancel in flight call: %p", state->call);
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
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_client_finalize;

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

	/* ensure we cancel any in-flight DBus calls */
	pk_client_cancel_all_dbus_methods (client);

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

static void
pk_client_test_resolve_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	const PkItemPackage *item;
	guint i;

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

	/* list, just for shits and giggles */
	for (i=0; i<packages->len; i++) {
		item = g_ptr_array_index (packages, i);
		egg_debug ("%s\t%s\t%s", pk_info_enum_to_text (item->info), item->package_id, item->summary);
	}

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
	const PkItemDetails *item;
	guint i;

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

	/* list, just for shits and giggles */
	for (i=0; i<details->len; i++) {
		item = g_ptr_array_index (details, i);
		egg_debug ("%s\t%s\t%s", item->package_id, item->url, item->description);
	}

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
	PkItemErrorCode *error_item = NULL;

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
	error_item = pk_results_get_error_code (results);
	if (error_item->code != PK_ERROR_ENUM_TRANSACTION_CANCELLED)
		egg_test_failed (test, "failed to get error code: %i", error_item->code);
	if (g_strcmp0 (error_item->details, "The task was stopped successfully") != 0)
		egg_test_failed (test, "failed to get error message: %s", error_item->details);
out:
	if (error_item != NULL)
		pk_item_error_code_unref (error_item);
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

static guint _progress_cb = 0;
static guint _status_cb = 0;
static guint _package_cb = 0;
static guint _allow_cancel_cb = 0;

static void
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
	const PkItemFiles *item;
	GPtrArray *array = NULL;
	guint len;

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
	if (g_strcmp0 (item->package_id, "powertop-common;1.8-1.fc8;i386;fedora") != 0)
		egg_test_failed (test, "invalid package_id: %s", item->package_id);
	len = g_strv_length (item->files);
	if (len != 1)
		egg_test_failed (test, "invalid number of files: %i", len);
	if (g_strcmp0 (item->files[0], "/tmp/powertop-common-1.8-1.fc8.rpm") != 0)
		egg_test_failed (test, "invalid filename: %s, maybe not rewritten", item->files[0]);
out:
	g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	egg_test_loop_quit (test);
}

void
pk_client_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkClient *client;
	gchar **package_ids;
	gchar *file;
	GCancellable *cancellable;

	if (!egg_test_start (test, "PkClient"))
		return;

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
	egg_test_assert (test, client != NULL);

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

	/************************************************************/
	egg_test_title (test, "got status updates");
	if (_status_cb > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %i updates", _status_cb);

	/************************************************************/
	egg_test_title (test, "search by name");
	cancellable = g_cancellable_new ();
	pk_client_search_name_async (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), "power", cancellable,
				     (PkProgressCallback) pk_client_test_progress_cb, test,
				     (GAsyncReadyCallback) pk_client_test_search_name_cb, test);
	g_timeout_add (1000, (GSourceFunc) pk_client_test_cancel, cancellable);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "cancelled in %i", egg_test_elapsed (test));

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

	g_object_unref (cancellable);
	g_object_unref (client);

	egg_test_end (test);
}
#endif

