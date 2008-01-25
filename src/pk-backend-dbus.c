/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <glib/gprintf.h>

#include <gmodule.h>
#include <libgbus.h>
#include <dbus/dbus-glib.h>

#include <pk-common.h>
#include <pk-package-id.h>
#include <pk-enum.h>
#include <pk-network.h>

#include "pk-debug.h"
#include "pk-backend-internal.h"
#include "pk-backend-dbus.h"
#include "pk-marshal.h"
#include "pk-enum.h"
#include "pk-time.h"
#include "pk-inhibit.h"
#include "pk-thread-list.h"

#define PK_BACKEND_DBUS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_DBUS, PkBackendDbusPrivate))

struct PkBackendDbusPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	PkBackend		*backend;
	gchar			*service;
	gchar			*interface;
	gchar			*path;
	gulong			 signal_finished;
};

G_DEFINE_TYPE (PkBackendDbus, pk_backend_dbus, G_TYPE_OBJECT)

/**
 * pk_backend_dbus_lock:
 **/
static gboolean
pk_backend_dbus_lock (PkBackendDbus *backend_dbus)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Lock", &error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_unlock:
 **/
static gboolean
pk_backend_dbus_unlock (PkBackendDbus *backend_dbus)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Unlock", &error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_repo_detail_cb:
 **/
static void
pk_backend_dbus_repo_detail_cb (DBusGProxy *proxy, const gchar *repo_id,
				const gchar *description, gboolean enabled,
				PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_repo_detail (backend_dbus->priv->backend, repo_id, description, enabled);
}

/**
 * pk_backend_dbus_status_changed_cb:
 **/
static void
pk_backend_dbus_status_changed_cb (DBusGProxy *proxy, PkStatusEnum status, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_set_status (backend_dbus->priv->backend, status);
}

/**
 * pk_backend_dbus_percentage_changed_cb:
 **/
static void
pk_backend_dbus_percentage_changed_cb (DBusGProxy *proxy, guint percentage, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_set_percentage (backend_dbus->priv->backend, percentage);
}

/**
 * pk_backend_dbus_sub_percentage_changed_cb:
 **/
static void
pk_backend_dbus_sub_percentage_changed_cb (DBusGProxy *proxy, guint sub_percentage, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_set_sub_percentage (backend_dbus->priv->backend, sub_percentage);
}

/**
 * pk_backend_dbus_no_percentage_updates_cb:
 **/
static void
pk_backend_dbus_no_percentage_updates_cb (DBusGProxy *proxy, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_no_percentage_updates (backend_dbus->priv->backend);
}

/**
 * pk_backend_dbus_package_cb:
 **/
static void
pk_backend_dbus_package_cb (DBusGProxy *proxy, PkInfoEnum info, const gchar *package_id,
			    const gchar *summary, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_package (backend_dbus->priv->backend, info, package_id, summary);
}

/**
 * pk_backend_dbus_description_cb:
 **/
static void
pk_backend_dbus_description_cb (DBusGProxy *proxy, const gchar *package_id,
				const gchar *license, PkGroupEnum group,
				const gchar *detail, const gchar *url,
				guint32 size, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_description (backend_dbus->priv->backend, package_id, license, group, detail, url, size);
}

/**
 * pk_backend_dbus_files_cb:
 **/
static void
pk_backend_dbus_files_cb (DBusGProxy *proxy, const gchar *package_id,
			  const gchar *file_list, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_files (backend_dbus->priv->backend, package_id, file_list);
}

/**
 * pk_backend_dbus_update_detail_cb:
 **/
static void
pk_backend_dbus_update_detail_cb (DBusGProxy *proxy, const gchar *package_id,
				  const gchar *updates, const gchar *obsoletes,
				  const gchar *vendor_url, const gchar *bugzilla_url,
				  const gchar *cve_url, PkRestartEnum restart,
				  const gchar *update_text, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_update_detail (backend_dbus->priv->backend, package_id, updates,
				  obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text);
}

/**
 * pk_backend_dbus_finished_cb:
 **/
static void
pk_backend_dbus_finished_cb (DBusGProxy *proxy, PkExitEnum exit, PkBackendDbus *backend_dbus)
{
	pk_debug ("deleting dbus %p, exit %s", backend_dbus, pk_exit_enum_to_text (exit));
	pk_backend_dbus_unlock (backend_dbus);
	pk_backend_finished (backend_dbus->priv->backend);
}

/**
 * pk_backend_dbus_allow_cancel_cb:
 **/
static void
pk_backend_dbus_allow_cancel_cb (DBusGProxy *proxy, gboolean allow_cancel, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_set_allow_cancel (backend_dbus->priv->backend, allow_cancel);
}

/**
 * pk_backend_dbus_error_code_cb:
 **/
static void
pk_backend_dbus_error_code_cb (DBusGProxy *proxy, PkErrorCodeEnum code,
			       const gchar *details, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_error_code (backend_dbus->priv->backend, code, details);
}

/**
 * pk_backend_dbus_require_restart_cb:
 **/
static void
pk_backend_dbus_require_restart_cb (DBusGProxy *proxy, PkRestartEnum type,
				    const gchar *details, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_require_restart (backend_dbus->priv->backend, type, details);
}

/**
 * pk_backend_dbus_message_cb:
 **/
static void
pk_backend_dbus_message_cb (DBusGProxy *proxy, PkMessageEnum message,
			    const gchar *details, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_message (backend_dbus->priv->backend, message, details);
}

/**
 * pk_backend_dbus_repo_signature_required_cb:
 **/
static void
pk_backend_dbus_repo_signature_required_cb (DBusGProxy *proxy, const gchar *repository_name,
					    const gchar *key_url, const gchar *key_userid,
					    const gchar *key_id, const gchar *key_fingerprint,
					    const gchar *key_timestamp, PkSigTypeEnum type, PkBackendDbus *backend_dbus)
{
	pk_debug ("got signal");
	pk_backend_repo_signature_required (backend_dbus->priv->backend, repository_name,
					    key_url, key_userid, key_id, key_fingerprint, key_timestamp, type);
}

/**
 * pk_backend_dbus_set_name:
 **/
gboolean
pk_backend_dbus_set_name (PkBackendDbus *backend_dbus, const gchar *service,
			  const gchar *interface, const gchar *path)
{
	DBusGProxy *proxy;
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	if (backend_dbus->priv->proxy != NULL) {
		pk_warning ("need to unref old one -- is this logically allowed?");
		g_object_unref (backend_dbus->priv->proxy);
	}

	/* grab this */
	proxy = dbus_g_proxy_new_for_name (backend_dbus->priv->connection,
					   service, path, interface);

	dbus_g_proxy_add_signal (proxy, "RepoDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "StatusChanged",
				 G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "PercentageChanged",
				 G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "SubPercentageChanged",
				 G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "NoPercentageChanged", G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Description",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Files",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "UpdateDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "AllowCancel",
				 G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "ErrorCode",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RequireRestart",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Message",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RepoSignatureRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_UINT, G_TYPE_INVALID);

	/* add callbacks */
	dbus_g_proxy_connect_signal (proxy, "RepoDetail",
				     G_CALLBACK (pk_backend_dbus_repo_detail_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "StatusChanged",
				     G_CALLBACK (pk_backend_dbus_status_changed_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "PercentageChanged",
				     G_CALLBACK (pk_backend_dbus_percentage_changed_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "SubPercentageChanged",
				     G_CALLBACK (pk_backend_dbus_sub_percentage_changed_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "NoPercentageChanged",
				     G_CALLBACK (pk_backend_dbus_no_percentage_updates_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_backend_dbus_package_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "Description",
				     G_CALLBACK (pk_backend_dbus_description_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "Files",
				     G_CALLBACK (pk_backend_dbus_files_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_backend_dbus_update_detail_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_backend_dbus_finished_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "AllowCancel",
				     G_CALLBACK (pk_backend_dbus_allow_cancel_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_backend_dbus_error_code_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_backend_dbus_require_restart_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "Message",
				     G_CALLBACK (pk_backend_dbus_message_cb), backend_dbus, NULL);
	dbus_g_proxy_connect_signal (proxy, "RepoSignatureRequired",
				     G_CALLBACK (pk_backend_dbus_repo_signature_required_cb), backend_dbus, NULL);

	backend_dbus->priv->proxy = proxy;

	/* save for later */
	g_free (backend_dbus->priv->service);
	g_free (backend_dbus->priv->interface);
	g_free (backend_dbus->priv->path);
	backend_dbus->priv->service = g_strdup (service);
	backend_dbus->priv->interface = g_strdup (interface);
	backend_dbus->priv->path = g_strdup (path);

	/* manually init the backend, which should get things spawned for us */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Init", &error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;

	return TRUE;
}

/**
 * pk_backend_dbus_kill:
 **/
gboolean
pk_backend_dbus_kill (PkBackendDbus *backend_dbus)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Exit", &error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}


/**
 * pk_backend_dbus_refresh_cache:
 **/
gboolean
pk_backend_dbus_refresh_cache (PkBackendDbus *backend_dbus, gboolean force)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "RefreshCache", &error,
				 G_TYPE_BOOLEAN, force,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_update_system:
 **/
gboolean
pk_backend_dbus_update_system (PkBackendDbus *backend_dbus)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "UpdateSystem", &error,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_resolve:
 **/
gboolean
pk_backend_dbus_resolve (PkBackendDbus *backend_dbus, const gchar *filter, const gchar *package)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Resolve", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_rollback:
 **/
gboolean
pk_backend_dbus_rollback (PkBackendDbus *backend_dbus, const gchar *transaction_id)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "Rollback", &error,
				 G_TYPE_STRING, transaction_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_search_name:
 **/
gboolean
pk_backend_dbus_search_name (PkBackendDbus *backend_dbus, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "SearchName", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_search_details:
 **/
gboolean
pk_backend_dbus_search_details (PkBackendDbus *backend_dbus, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "SearchDetails", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_search_group:
 **/
gboolean
pk_backend_dbus_search_group (PkBackendDbus *backend_dbus, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "SearchGroup", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_search_file:
 **/
gboolean
pk_backend_dbus_search_file (PkBackendDbus *backend_dbus, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "SearchFile", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_get_depends:
 **/
gboolean
pk_backend_dbus_get_depends (PkBackendDbus *backend_dbus, const gchar *package_id, gboolean recursive)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "GetDepends", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_BOOLEAN, recursive,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_get_requires:
 **/
gboolean
pk_backend_dbus_get_requires (PkBackendDbus *backend_dbus, const gchar *package_id, gboolean recursive)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "GetRequires", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_BOOLEAN, recursive,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_get_update_detail:
 **/
gboolean
pk_backend_dbus_get_update_detail (PkBackendDbus *backend_dbus, const gchar *package_id)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "GetUpdateDetail", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_get_description:
 **/
gboolean
pk_backend_dbus_get_description (PkBackendDbus *backend_dbus, const gchar *package_id)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "GetDescription", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_get_files:
 **/
gboolean
pk_backend_dbus_get_files (PkBackendDbus *backend_dbus, const gchar *package_id)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "GetFiles", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_remove_package:
 **/
gboolean
pk_backend_dbus_remove_package (PkBackendDbus *backend_dbus, const gchar *package_id, gboolean allow_deps)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "RemovePackage", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_BOOLEAN, allow_deps,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_install_package:
 **/
gboolean
pk_backend_dbus_install_package (PkBackendDbus *backend_dbus, const gchar *package_id)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "InstallPackage", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_update_package:
 **/
gboolean
pk_backend_dbus_update_package (PkBackendDbus *backend_dbus, const gchar *package_id)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "UpdatePackage", &error,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_install_file:
 **/
gboolean
pk_backend_dbus_install_file (PkBackendDbus *backend_dbus, const gchar *full_path)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (backend_dbus != NULL, FALSE);

	/* lock the backend */
	pk_backend_dbus_lock (backend_dbus);

	/* do the action */
	ret = dbus_g_proxy_call (backend_dbus->priv->proxy, "InstallFile", &error,
				 G_TYPE_STRING, full_path,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* unlock the backend if we failed */
	if (ret == FALSE) {
		pk_backend_dbus_unlock (backend_dbus);
	}

	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/**
 * pk_backend_dbus_finalize:
 **/
static void
pk_backend_dbus_finalize (GObject *object)
{
	PkBackendDbus *backend_dbus;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_BACKEND_DBUS (object));

	backend_dbus = PK_BACKEND_DBUS (object);
	g_free (backend_dbus->priv->service);
	g_free (backend_dbus->priv->interface);
	g_free (backend_dbus->priv->path);
	g_object_unref (backend_dbus->priv->proxy);
	g_object_unref (backend_dbus->priv->backend);

	G_OBJECT_CLASS (pk_backend_dbus_parent_class)->finalize (object);
}

/**
 * pk_backend_dbus_class_init:
 **/
static void
pk_backend_dbus_class_init (PkBackendDbusClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_dbus_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendDbusPrivate));
}

/**
 * pk_backend_dbus_init:
 **/
static void
pk_backend_dbus_init (PkBackendDbus *backend_dbus)
{
	GError *error = NULL;

	backend_dbus->priv = PK_BACKEND_DBUS_GET_PRIVATE (backend_dbus);
	backend_dbus->priv->proxy = NULL;
	backend_dbus->priv->service = NULL;
	backend_dbus->priv->interface = NULL;
	backend_dbus->priv->path = NULL;
	backend_dbus->priv->backend = pk_backend_new ();

	/* get connection */
	backend_dbus->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
	}
}

/**
 * pk_backend_dbus_new:
 **/
PkBackendDbus *
pk_backend_dbus_new (void)
{
	PkBackendDbus *backend_dbus;
	backend_dbus = g_object_new (PK_TYPE_BACKEND_DBUS, NULL);
	return PK_BACKEND_DBUS (backend_dbus);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_backend_dbus (LibSelfTest *test)
{
	PkBackendDbus *backend_dbus;

	if (libst_start (test, "PkBackendDbus", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an backend_dbus");
	backend_dbus = pk_backend_dbus_new ();
	if (backend_dbus != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (backend_dbus);

	libst_end (test);
}
#endif

