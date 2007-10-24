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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "pk-enum.h"
#include "pk-client.h"
#include "pk-connection.h"
#include "pk-package-list.h"
#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-polkit-client.h"
#include "pk-common.h"

static void     pk_client_class_init	(PkClientClass *klass);
static void     pk_client_init		(PkClient      *client);
static void     pk_client_finalize	(GObject       *object);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

struct PkClientPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	gboolean		 is_finished;
	gboolean		 use_buffer;
	gboolean		 promiscuous;
	gchar			*tid;
	PkPackageList		*package_list;
	PkConnection		*pconnection;
	PkPolkitClient		*polkit;
	PkRestartEnum		 require_restart;
	PkStatusEnum		 last_status;
	PkRoleEnum		 role;
	gboolean		 xcached_force;
	gboolean		 xcached_allow_deps;
	gchar			*xcached_package_id;
	gchar			*xcached_transaction_id;
	gchar			*xcached_full_path;
	gchar			*xcached_filter;
	gchar			*xcached_search;
};

typedef enum {
	PK_CLIENT_DESCRIPTION,
	PK_CLIENT_ERROR_CODE,
	PK_CLIENT_FINISHED,
	PK_CLIENT_PACKAGE,
	PK_CLIENT_PROGRESS_CHANGED,
	PK_CLIENT_UPDATES_CHANGED,
	PK_CLIENT_REQUIRE_RESTART,
	PK_CLIENT_TRANSACTION,
	PK_CLIENT_TRANSACTION_STATUS_CHANGED,
	PK_CLIENT_UPDATE_DETAIL,
	PK_CLIENT_REPO_SIGNATURE_REQUIRED,
	PK_CLIENT_REPO_DETAIL,
	PK_CLIENT_LOCKED,
	PK_CLIENT_LAST_SIGNAL
} PkSignals;

static guint signals [PK_CLIENT_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

/******************************************************************************
 *                    LOCAL FUNCTIONS
 ******************************************************************************/

/**
 * pk_client_set_tid:
 **/
gboolean
pk_client_set_tid (PkClient *client, const gchar *tid)
{
	if (client->priv->promiscuous == TRUE) {
		pk_warning ("you can't set the tid on a promiscuous client");
		return FALSE;
	}
	client->priv->tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_client_set_promiscuous:
 **/
gboolean
pk_client_set_promiscuous (PkClient *client, gboolean enabled)
{
	if (client->priv->tid != NULL) {
		pk_warning ("you can't set promiscuous on a tid client");
		return FALSE;
	}
	client->priv->promiscuous = enabled;
	return TRUE;
}

/**
 * pk_client_get_tid:
 **/
gchar *
pk_client_get_tid (PkClient *client)
{
	return g_strdup (client->priv->tid);
}

/**
 * pk_transaction_id_equal:
 * TODO: only compare first two sections...
 **/
static gboolean
pk_transaction_id_equal (const gchar *tid1, const gchar *tid2)
{
	if (tid1 == NULL || tid2 == NULL) {
		pk_warning ("tid compare invalid '%s' and '%s'", tid1, tid2);
		return FALSE;
	}
	return (strcmp (tid1, tid2) == 0);
}

/**
 * pk_client_set_use_buffer:
 **/
gboolean
pk_client_set_use_buffer (PkClient *client, gboolean use_buffer)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	client->priv->use_buffer = use_buffer;
	return TRUE;
}

/**
 * pk_client_get_use_buffer:
 **/
gboolean
pk_client_get_use_buffer (PkClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return client->priv->use_buffer;
}

/**
 * pk_client_get_use_buffer:
 **/
PkRestartEnum
pk_client_get_require_restart (PkClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return client->priv->require_restart;
}

/**
 * pk_client_package_buffer_get_size:
 **/
guint
pk_client_package_buffer_get_size (PkClient *client)
{
	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (PK_IS_CLIENT (client), 0);
	if (client->priv->use_buffer == FALSE) {
		return 0;
	}
	return pk_package_list_get_size (client->priv->package_list);
}

/**
 * pk_client_package_buffer_get_item:
 **/
PkPackageItem *
pk_client_package_buffer_get_item (PkClient *client, guint item)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	if (client->priv->use_buffer == FALSE) {
		return NULL;
	}
	return pk_package_list_get_item (client->priv->package_list, item);
}

/**
 * pk_client_reset:
 **/
gboolean
pk_client_reset (PkClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	if (client->priv->is_finished != TRUE) {
		pk_warning ("not exit status, reset might be invalid");
	}
	g_free (client->priv->tid);
	client->priv->tid = NULL;
	client->priv->use_buffer = FALSE;
	client->priv->tid = NULL;
	client->priv->last_status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;
	pk_package_list_clear (client->priv->package_list);
	return TRUE;
}

/**
 * pk_client_get_error_name:
 **/
static const gchar *
pk_client_get_error_name (GError *error)
{
	const gchar *name;
	if (error->domain == DBUS_GERROR &&
	    error->code == DBUS_GERROR_REMOTE_EXCEPTION) {
		name = dbus_g_error_get_name (error);
	} else {
		name = g_quark_to_string (error->domain);
	}
	return name;
}

/******************************************************************************
 *                    SIGNALS
 ******************************************************************************/

/**
 * pk_client_should_proxy:
 */
static gboolean
pk_client_should_proxy (PkClient *client, const gchar *tid)
{
	if (client->priv->promiscuous == TRUE) {
		return TRUE;
	}
	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_client_finished_cb:
 */
static void
pk_client_finished_cb (DBusGProxy  *proxy,
		       gchar	   *tid,
		       const gchar *exit_text,
		       guint        runtime,
		       PkClient    *client)
{
	PkExitEnum exit;

	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		exit = pk_exit_enum_from_text (exit_text);
		pk_debug ("emit finished %s, %i", exit_text, runtime);

		/* only this instance is finished, and do it before the signal so we can reset */
		client->priv->is_finished = TRUE;

		g_signal_emit (client , signals [PK_CLIENT_FINISHED], 0, exit, runtime);
	}
}

/**
 * pk_client_progress_changed_cb:
 */
static void
pk_client_progress_changed_cb (DBusGProxy  *proxy, const gchar *tid,
			       guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		pk_debug ("emit progress-changed %i, %i, %i, %i", percentage, subpercentage, elapsed, remaining);
		g_signal_emit (client , signals [PK_CLIENT_PROGRESS_CHANGED], 0,
			       percentage, subpercentage, elapsed, remaining);
	}
}

/**
 * pk_client_transaction_status_changed_cb:
 */
static void
pk_client_transaction_status_changed_cb (DBusGProxy  *proxy,
				         const gchar *tid,
				         const gchar *status_text,
				         PkClient    *client)
{
	PkStatusEnum status;

	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	status = pk_status_enum_from_text (status_text);
	if (pk_client_should_proxy (client, tid) == TRUE) {
		pk_debug ("emit transaction-status-changed %i", status);
		g_signal_emit (client , signals [PK_CLIENT_TRANSACTION_STATUS_CHANGED], 0, status);
	}
	client->priv->last_status = status;
}

/**
 * pk_client_package_cb:
 */
static void
pk_client_package_cb (DBusGProxy   *proxy,
		      const gchar  *tid,
		      const gchar  *info_text,
		      const gchar  *package_id,
		      const gchar  *summary,
		      PkClient     *client)
{
	PkInfoEnum info;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		pk_debug ("emit package %s, %s, %s", info_text, package_id, summary);
		info = pk_info_enum_from_text (info_text);
		g_signal_emit (client , signals [PK_CLIENT_PACKAGE], 0, info, package_id, summary);

		/* cache */
		if (client->priv->use_buffer == TRUE) {
			pk_debug ("adding to cache array package %i, %s, %s", info, package_id, summary);
			pk_package_list_add (client->priv->package_list, info, package_id, summary);
		}
	}
}

/**
 * pk_client_updates_changed_cb:
 */
static void
pk_client_updates_changed_cb (DBusGProxy *proxy, const gchar *tid, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* we always emit, even if the tid does not match */
	pk_debug ("emitting updates-changed");
	g_signal_emit (client, signals [PK_CLIENT_UPDATES_CHANGED], 0);

}

/**
 * pk_client_transaction_cb:
 */
static void
pk_client_transaction_cb (DBusGProxy *proxy,
			  const gchar *tid, const gchar *old_tid, const gchar *timespec,
			  gboolean succeeded, const gchar *role_text, guint duration,
			  const gchar *data, PkClient *client)
{
	PkRoleEnum role;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		role = pk_role_enum_from_text (role_text);
		pk_debug ("emitting transaction %s, %s, %i, %s, %i, %s", old_tid, timespec, succeeded, role_text, duration, data);
		g_signal_emit (client, signals [PK_CLIENT_TRANSACTION], 0, old_tid, timespec, succeeded, role, duration, data);
	}
}

/**
 * pk_client_update_detail_cb:
 */
static void
pk_client_update_detail_cb (DBusGProxy  *proxy,
			    const gchar *tid,
			    const gchar *package_id,
			    const gchar *updates,
			    const gchar *obsoletes,
			    const gchar *url,
			    const gchar *restart,
			    const gchar *update_text,
			    PkClient    *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		pk_debug ("emit update-detail %s, %s, %s, %s, %s, %s",
			  package_id, updates, obsoletes, url, restart, update_text);
		g_signal_emit (client , signals [PK_CLIENT_UPDATE_DETAIL], 0,
			       package_id, updates, obsoletes, url, restart, update_text);
	}
}

/**
 * pk_client_description_cb:
 */
static void
pk_client_description_cb (DBusGProxy  *proxy,
			  const gchar *tid,
			  const gchar *package_id,
			  const gchar *licence,
			  const gchar *group_text,
			  const gchar *description,
			  const gchar *url,
			  guint64      size,
			  const gchar *filelist,
			  PkClient    *client)
{
	PkGroupEnum group;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		group = pk_group_enum_from_text (group_text);
		pk_debug ("emit description %s, %s, %i, %s, %s, %ld, %s",
			  package_id, licence, group, description, url, (long int) size, filelist);
		g_signal_emit (client , signals [PK_CLIENT_DESCRIPTION], 0,
			       package_id, licence, group, description, url, size, filelist);
	}
}

/**
 * pk_client_repo_signature_required_cb:
 **/
static void
pk_client_repo_signature_required_cb (DBusGProxy *proxy, const gchar *tid, const gchar *repository_name,
				      const gchar *key_url, const gchar *key_userid, const gchar *key_id, 
				      const gchar *key_fingerprint, const gchar *key_timestamp,
				      const gchar *type_text, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		pk_debug ("emit repo_signature_required tid:%s, %s, %s, %s, %s, %s, %s, %s",
			  tid, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
		g_signal_emit (client, signals [PK_CLIENT_REPO_SIGNATURE_REQUIRED], 0,
			       repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
	}
}

/**
 * pk_client_repo_detail_cb:
 **/
static void
pk_client_repo_detail_cb (DBusGProxy *proxy, const gchar *tid, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		pk_debug ("emit repo-detail %s, %s, %i", repo_id, description, enabled);
		g_signal_emit (client, signals [PK_CLIENT_REPO_DETAIL], 0, repo_id, description, enabled);
	}
}

/**
 * pk_client_error_code_cb:
 */
static void
pk_client_error_code_cb (DBusGProxy  *proxy,
			 const gchar *tid,
			 const gchar *code_text,
			 const gchar *details,
			 PkClient    *client)
{
	PkErrorCodeEnum code;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		code = pk_error_enum_from_text (code_text);
		pk_debug ("emit error-code %i, %s", code, details);
		g_signal_emit (client , signals [PK_CLIENT_ERROR_CODE], 0, code, details);
	}
}

/**
 * pk_client_locked_cb:
 */
static void
pk_client_locked_cb (DBusGProxy *proxy, gboolean is_locked, PkClient *client)
{
	pk_debug ("emit locked %i", is_locked);
	g_signal_emit (client , signals [PK_CLIENT_LOCKED], 0, is_locked);
}

/**
 * pk_client_require_restart_cb:
 */
static void
pk_client_require_restart_cb (DBusGProxy  *proxy,
			      const gchar *tid,
			      const gchar *restart_text,
			      const gchar *details,
			      PkClient    *client)
{
	PkRestartEnum restart;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		pk_debug ("ignoring tid:%s as we are not yet assigned", tid);
		return;
	}

	if (pk_client_should_proxy (client, tid) == TRUE) {
		restart = pk_restart_enum_from_text (restart_text);
		pk_debug ("emit require-restart %i, %s", restart, details);
		g_signal_emit (client , signals [PK_CLIENT_REQUIRE_RESTART], 0, restart, details);
		if (restart > client->priv->require_restart) {
			client->priv->require_restart = restart;
			pk_debug ("restart status now %s", pk_restart_enum_to_text (restart));
		}
	}
}

/******************************************************************************
 *                    TRANSACTION ID USING METHODS
 ******************************************************************************/

/**
 * pk_client_get_status:
 **/
gboolean
pk_client_get_status (PkClient *client, PkStatusEnum *status)
{
	gboolean ret;
	gchar *status_text;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (status != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetStatus", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetStatus failed!");
		return FALSE;
	}
	*status = pk_status_enum_from_text (status_text);
	return TRUE;
}

/**
 * pk_client_get_package:
 **/
gboolean
pk_client_get_package (PkClient *client, gchar **package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetPackage", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetPackage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_get_progress:
 **/
gboolean
pk_client_get_progress (PkClient *client, guint *percentage, guint *subpercentage,
			guint *elapsed, guint *remaining)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetProgress", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_UINT, subpercentage,
				 G_TYPE_UINT, elapsed,
				 G_TYPE_UINT, remaining,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetProgress failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_get_role:
 **/
gboolean
pk_client_get_role (PkClient *client, PkRoleEnum *role, gchar **package_id)
{
	gboolean ret;
	GError *error;
	gchar *role_text;
	gchar *package_id_temp;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (role != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	/* we can avoid a trip to the daemon */
	if (package_id == NULL) {
		*role = client->priv->role;
		return TRUE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetRole", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &role_text,
				 G_TYPE_STRING, &package_id_temp,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetRole failed :%s", error->message);
		g_error_free (error);
		return FALSE;
	}
	*role = pk_role_enum_from_text (role_text);
	if (package_id != NULL) {
		*package_id = g_strdup (package_id_temp);
	}
	return TRUE;
}

/**
 * pk_client_cancel:
 **/
gboolean
pk_client_cancel (PkClient *client)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we have an tid */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "Cancel", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("Cancel failed :%s", error->message);
		g_error_free (error);
	}
	return ret;
}

/******************************************************************************
 *                    TRANSACTION ID CREATING METHODS
 ******************************************************************************/

/**
 * pk_client_allocate_transaction_id:
 **/
static gboolean
pk_client_allocate_transaction_id (PkClient *client)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	if (client->priv->tid != NULL) {
		pk_warning ("Already has transaction ID");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetTid", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &client->priv->tid,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("NewTid failed!");
		return FALSE;
	} else {
		pk_debug ("Got tid: '%s'", client->priv->tid);
	}

	return TRUE;
}

/**
 * pk_client_get_updates:
 **/
gboolean
pk_client_get_updates (PkClient *client)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATES;

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetUpdates", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetUpdates failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_update_system_action:
 **/
gboolean
pk_client_update_system_action (PkClient *client, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "UpdateSystem", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("UpdateSystem failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_update_system:
 **/
gboolean
pk_client_update_system (PkClient *client)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_UPDATE_SYSTEM;

	/* hopefully do the operation first time */
	ret = pk_client_update_system_action (client, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_update_system_action (client, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/**
 * pk_client_search_name:
 **/
gboolean
pk_client_search_name (PkClient *client, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_NAME;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchName", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("SearchName failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_search_details:
 **/
gboolean
pk_client_search_details (PkClient *client, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchDetails", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("SearchDetails failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_search_group:
 **/
gboolean
pk_client_search_group (PkClient *client, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_GROUP;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchGroup", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("SearchGroup failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_search_file:
 **/
gboolean
pk_client_search_file (PkClient *client, const gchar *filter, const gchar *search)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_FILE;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchFile", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("SearchFile failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_get_depends:
 **/
gboolean
pk_client_get_depends (PkClient *client, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DEPENDS;
	client->priv->xcached_package_id = g_strdup (package);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetDepends", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetDepends failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_get_requires:
 **/
gboolean
pk_client_get_requires (PkClient *client, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_REQUIRES;
	client->priv->xcached_package_id = g_strdup (package);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetRequires", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetRequires failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_get_update_detail:
 **/
gboolean
pk_client_get_update_detail (PkClient *client, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	client->priv->xcached_package_id = g_strdup (package);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetUpdateDetail", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetUpdateDetail failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_rollback:
 **/
gboolean
pk_client_rollback (PkClient *client, const gchar *transaction_id)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	client->priv->xcached_transaction_id = g_strdup (transaction_id);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "Rollback", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, transaction_id,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("Rollback failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_resolve:
 **/
gboolean
pk_client_resolve (PkClient *client, const gchar *filter, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_RESOLVE;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_package_id = g_strdup (package);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "Resolve", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("Resolve failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_get_description:
 **/
gboolean
pk_client_get_description (PkClient *client, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DESCRIPTION;
	client->priv->xcached_package_id = g_strdup (package);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetDescription", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetDescription failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_remove_package_action:
 **/
gboolean
pk_client_remove_package_action (PkClient *client, const gchar *package,
				 gboolean allow_deps, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "RemovePackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_BOOLEAN, allow_deps,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RemovePackage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_remove_package:
 **/
gboolean
pk_client_remove_package (PkClient *client, const gchar *package, gboolean allow_deps)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REMOVE_PACKAGE;
	client->priv->xcached_allow_deps = allow_deps;
	client->priv->xcached_package_id = g_strdup (package);

	/* hopefully do the operation first time */
	ret = pk_client_remove_package_action (client, package, allow_deps, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_remove_package_action (client, package, allow_deps, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/**
 * pk_client_refresh_cache:
 **/
gboolean
pk_client_refresh_cache (PkClient *client, gboolean force)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REFRESH_CACHE;
	client->priv->xcached_force = force;

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "RefreshCache", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_BOOLEAN, force,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RefreshCache failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_install_package_action:
 **/
gboolean
pk_client_install_package_action (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "InstallPackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("InstallPackage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_install_package:
 **/
gboolean
pk_client_install_package (PkClient *client, const gchar *package_id)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_PACKAGE;
	client->priv->xcached_package_id = g_strdup (package_id);

	/* hopefully do the operation first time */
	ret = pk_client_install_package_action (client, package_id, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_install_package_action (client, package_id, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/**
 * pk_client_update_package_action:
 **/
gboolean
pk_client_update_package_action (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "UpdatePackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("UpdatePackage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_update_package:
 **/
gboolean
pk_client_update_package (PkClient *client, const gchar *package_id)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_PACKAGE;
	client->priv->xcached_package_id = g_strdup (package_id);

	/* hopefully do the operation first time */
	ret = pk_client_update_package_action (client, package_id, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_install_package_action (client, package_id, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/**
 * pk_client_install_file_action:
 **/
gboolean
pk_client_install_file_action (PkClient *client, const gchar *file, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "InstallFile", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, file,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("InstallFile failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_install_file:
 **/
gboolean
pk_client_install_file (PkClient *client, const gchar *file)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_FILE;
	client->priv->xcached_full_path = g_strdup (file);

	/* hopefully do the operation first time */
	ret = pk_client_install_file_action (client, file, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_install_file_action (client, file, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/**
 * pk_client_get_repo_list:
 */
gboolean
pk_client_get_repo_list (PkClient *client)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_REPO_LIST;

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetRepoList", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error != NULL) {
		const gchar *error_name;
		error_name = pk_client_get_error_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetRepoList failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_client_repo_enable_action:
 **/
gboolean
pk_client_repo_enable_action (PkClient *client, const gchar *repo_id, gboolean enabled, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "RepoEnable", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, repo_id,
				 G_TYPE_BOOLEAN, enabled,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RepoEnable failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_repo_enable:
 */
gboolean
pk_client_repo_enable (PkClient *client, const gchar *repo_id, gboolean enabled)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REPO_ENABLE;

	/* hopefully do the operation first time */
	ret = pk_client_repo_enable_action (client, repo_id, enabled, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_repo_enable_action (client, repo_id, enabled, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/**
 * pk_client_repo_set_data_action:
 **/
gboolean
pk_client_repo_set_data_action (PkClient *client, const gchar *repo_id,
				const gchar *parameter, const gchar *value, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "RepoSetData", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, repo_id,
				 G_TYPE_STRING, parameter,
				 G_TYPE_STRING, value,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RepoSetData failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_repo_set_data:
 */
gboolean
pk_client_repo_set_data (PkClient *client, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REPO_SET_DATA;

	/* hopefully do the operation first time */
	ret = pk_client_repo_set_data_action (client, repo_id, parameter, value, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_client_repo_set_data_action (client, repo_id, parameter, value, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	return ret;
}

/******************************************************************************
 *                    NON-TRANSACTION ID METHODS
 ******************************************************************************/

/**
 * pk_client_get_actions:
 **/
PkEnumList *
pk_client_get_actions (PkClient *client)
{
	gboolean ret;
	GError *error;
	gchar *actions;
	PkEnumList *elist;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetActions", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &actions,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetActions failed :%s", error->message);
		g_error_free (error);
		return elist;
	}

	/* convert to enumerated types */
	pk_enum_list_from_string (elist, actions);
	g_free (actions);
	return elist;
}

/**
 * pk_client_get_backend_detail:
 **/
gboolean
pk_client_get_backend_detail (PkClient *client, gchar **name, gchar **author, gchar **version)
{
	gboolean ret;
	GError *error;
	gchar *tname;
	gchar *tauthor;
	gchar *tversion;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetBackendDetail", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tname,
				 G_TYPE_STRING, &tauthor,
				 G_TYPE_STRING, &tversion,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetBackendDetail failed :%s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* copy needed bits */
	if (name != NULL) {
		*name = g_strdup (tname);
	}
	/* copy needed bits */
	if (author != NULL) {
		*author = g_strdup (tauthor);
	}
	/* copy needed bits */
	if (version != NULL) {
		*version = g_strdup (tversion);
	}
	return TRUE;
}

/**
 * pk_client_get_groups:
 **/
PkEnumList *
pk_client_get_groups (PkClient *client)
{
	gboolean ret;
	GError *error;
	gchar *groups;
	PkEnumList *elist;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_GROUP);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetGroups", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &groups,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetGroups failed :%s", error->message);
		g_error_free (error);
		return elist;
	}

	/* convert to enumerated types */
	pk_enum_list_from_string (elist, groups);
	g_free (groups);
	return elist;
}

/**
 * pk_client_get_old_transactions:
 **/
gboolean
pk_client_get_old_transactions (PkClient *client, guint number)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client);
	if (ret == FALSE) {
		pk_warning ("Failed to get transaction ID");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetOldTransactions", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_UINT, number,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetOldTransactions failed :%s", error->message);
		g_error_free (error);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_get_filters:
 **/
PkEnumList *
pk_client_get_filters (PkClient *client)
{
	gboolean ret;
	GError *error;
	gchar *filters;
	PkEnumList *elist;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_FILTER);

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetFilters", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &filters,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetFilters failed :%s", error->message);
		g_error_free (error);
		return elist;
	}

	/* convert to enumerated types */
	pk_enum_list_from_string (elist, filters);
	g_free (filters);
	return elist;
}

/**
 * pk_client_requeue:
 */
gboolean
pk_client_requeue (PkClient *client)
{
	PkRoleEnum role;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* we are no longer waiting, we are setting up */
	if (client->priv->role == PK_ROLE_ENUM_UNKNOWN) {
		pk_warning ("role unknown!!");
		return FALSE;
	}

	/* save the role */
	role = client->priv->role;

	/* reset this client, which doesn't clear cached data */
	pk_client_reset (client);

	/* restore the role */
	client->priv->role = role;

	/* do the correct action with the cached parameters */
	if (client->priv->role == PK_ROLE_ENUM_GET_DEPENDS) {
		pk_client_get_depends (client, client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		pk_client_get_update_detail (client, client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_RESOLVE) {
		pk_client_resolve (client, client->priv->xcached_filter,
				   client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_ROLLBACK) {
		pk_client_rollback (client, client->priv->xcached_transaction_id);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_DESCRIPTION) {
		pk_client_get_description (client, client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_REQUIRES) {
		pk_client_get_requires (client, client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_UPDATES) {
		pk_client_get_updates (client);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		pk_client_search_details (client, client->priv->xcached_filter,
					  client->priv->xcached_search);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_FILE) {
		pk_client_search_file (client, client->priv->xcached_filter,
				       client->priv->xcached_search);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		pk_client_search_group (client, client->priv->xcached_filter,
					client->priv->xcached_search);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_NAME) {
		pk_client_search_name (client, client->priv->xcached_filter,
				       client->priv->xcached_search);
	} else if (client->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		pk_client_install_package (client, client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_INSTALL_FILE) {
		pk_client_install_file (client, client->priv->xcached_full_path);
	} else if (client->priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		pk_client_refresh_cache (client, client->priv->xcached_force);
	} else if (client->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		pk_client_remove_package (client, client->priv->xcached_package_id,
					  client->priv->xcached_allow_deps);
	} else if (client->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		pk_client_update_package (client, client->priv->xcached_package_id);
	} else if (client->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		pk_client_update_system (client);
	} else {
		pk_warning ("role unknown");
		return FALSE;
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

	signals [PK_CLIENT_TRANSACTION_STATUS_CHANGED] =
		g_signal_new ("transaction-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_CLIENT_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_CLIENT_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_CLIENT_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_CLIENT_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_UINT_UINT_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_CLIENT_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT_STRING_STRING_UINT64_STRING,
			      G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_STRING);
	signals [PK_CLIENT_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_UINT,
			      G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_CLIENT_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOLEAN,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	signals [PK_CLIENT_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_CLIENT_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkClientPrivate));
}

/**
 * pk_client_connect:
 **/
static void
pk_client_connect (PkClient *client)
{
	pk_debug ("connect");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkClient *client)
{
	pk_debug ("connected=%i", connected);

	/* TODO: if PK re-started mid-transaction then show a big fat warning */
}

/**
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	GError *error = NULL;
	DBusGProxy *proxy = NULL;

	client->priv = PK_CLIENT_GET_PRIVATE (client);
	client->priv->tid = NULL;
	client->priv->use_buffer = FALSE;
	client->priv->promiscuous = FALSE;
	client->priv->last_status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->require_restart = PK_RESTART_ENUM_NONE;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;
	client->priv->package_list = pk_package_list_new ();
	client->priv->xcached_package_id = NULL;
	client->priv->xcached_transaction_id = NULL;
	client->priv->xcached_full_path = NULL;
	client->priv->xcached_filter = NULL;
	client->priv->xcached_search = NULL;

	/* check dbus connections, exit if not valid */
	client->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* watch for PackageKit on the bus, and try to connect up at start */
	client->priv->pconnection = pk_connection_new ();
	g_signal_connect (client->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), client);
	if (pk_connection_valid (client->priv->pconnection)) {
		pk_client_connect (client);
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (client->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	client->priv->proxy = proxy;

	/* use PolicyKit */
	client->priv->polkit = pk_polkit_client_new ();

	/* ProgressChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT_UINT_UINT_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT,
					   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);

	/* Locked */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* TransactionStatusChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	/* ErrorCode, RequireRestart */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Description */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_UINT64_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
					   G_TYPE_STRING, G_TYPE_INVALID);

	/* Repo Signature Required */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Package */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* RepoDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_BOOL,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* UpdateDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* Transaction */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_BOOL_STRING_UINT_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "ProgressChanged",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ProgressChanged",
				     G_CALLBACK (pk_client_progress_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "TransactionStatusChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "TransactionStatusChanged",
				     G_CALLBACK (pk_client_transaction_status_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_client_package_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Transaction",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Transaction",
				     G_CALLBACK (pk_client_transaction_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "UpdatesChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "UpdatesChanged",
				     G_CALLBACK (pk_client_updates_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "UpdateDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_client_update_detail_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Description",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Description",
				     G_CALLBACK (pk_client_description_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RepoSignatureRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RepoSignatureRequired",
				     G_CALLBACK (pk_client_repo_signature_required_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RepoDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RepoDetail",
				     G_CALLBACK (pk_client_repo_detail_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "ErrorCode",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_client_error_code_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RequireRestart",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_client_require_restart_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Locked", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Locked",
				     G_CALLBACK (pk_client_locked_cb), client, NULL);
}

/**
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CLIENT (object));
	client = PK_CLIENT (object);
	g_return_if_fail (client->priv != NULL);

	/* free cached strings */
	g_free (client->priv->xcached_package_id);
	g_free (client->priv->xcached_transaction_id);
	g_free (client->priv->xcached_full_path);
	g_free (client->priv->xcached_filter);
	g_free (client->priv->xcached_search);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Finished",
				        G_CALLBACK (pk_client_finished_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ProgressChanged",
				        G_CALLBACK (pk_client_progress_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "TransactionStatusChanged",
				        G_CALLBACK (pk_client_transaction_status_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_client_updates_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Package",
				        G_CALLBACK (pk_client_package_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Transaction",
				        G_CALLBACK (pk_client_transaction_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Description",
				        G_CALLBACK (pk_client_description_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RepoSignatureRequired",
				        G_CALLBACK (pk_client_repo_signature_required_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ErrorCode",
				        G_CALLBACK (pk_client_error_code_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RequireRestart",
				        G_CALLBACK (pk_client_require_restart_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Locked",
				        G_CALLBACK (pk_client_locked_cb), client);

	/* free the proxy */
	g_object_unref (G_OBJECT (client->priv->proxy));
	g_object_unref (client->priv->pconnection);
	g_object_unref (client->priv->polkit);
	g_object_unref (client->priv->package_list);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 **/
PkClient *
pk_client_new (void)
{
	PkClient *client;
	client = g_object_new (PK_TYPE_CLIENT, NULL);
	return PK_CLIENT (client);
}

