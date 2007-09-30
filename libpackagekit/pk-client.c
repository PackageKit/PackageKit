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

#include "pk-client.h"
#include "pk-connection.h"
#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-polkit-client.h"
#include "pk-task-common.h"

static void     pk_client_class_init	(PkClientClass *klass);
static void     pk_client_init		(PkClient      *client);
static void     pk_client_finalize	(GObject       *object);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

struct PkClientPrivate
{
	DBusGConnection	*connection;
	DBusGProxy	*proxy;
	gboolean	 is_finished;
	gboolean	 use_buffer;
	gchar		*tid;
	GPtrArray	*cache_package;
	PkConnection	*pconnection;
	PkPolkitClient	*polkit;
	PkRestartEnum	 require_restart;
	PkStatusEnum	 last_status;
};

typedef enum {
	PK_CLIENT_DESCRIPTION,
	PK_CLIENT_ERROR_CODE,
	PK_CLIENT_FINISHED,
	PK_CLIENT_NO_PERCENTAGE_UPDATES,
	PK_CLIENT_PACKAGE,
	PK_CLIENT_PERCENTAGE_CHANGED,
	PK_CLIENT_UPDATES_CHANGED,
	PK_CLIENT_REQUIRE_RESTART,
	PK_CLIENT_SUB_PERCENTAGE_CHANGED,
	PK_CLIENT_TRANSACTION,
	PK_CLIENT_TRANSACTION_STATUS_CHANGED,
	PK_CLIENT_UPDATE_DETAIL,
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
	client->priv->tid = g_strdup (tid);
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
 * pk_client_get_package_buffer:
 **/
GPtrArray *
pk_client_get_package_buffer (PkClient *client)
{
	if (client->priv->use_buffer == FALSE) {
		return NULL;
	}
	return client->priv->cache_package;
}

/**
 * pk_client_remove_cache_package:
 **/
static void
pk_client_remove_cache_package (PkClient *client)
{
	PkClientPackageItem *item;
	while (client->priv->cache_package->len > 0) {
		item = g_ptr_array_index (client->priv->cache_package, 0);
		g_free (item->package_id);
		g_free (item->summary);
		g_free (item);
		g_ptr_array_remove_index_fast (client->priv->cache_package, 0);
	}
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
	client->priv->is_finished = FALSE;
	pk_client_remove_cache_package (client);
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

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		exit = pk_exit_enum_from_text (exit_text);
		pk_debug ("emit finished %i, %i", exit, runtime);
		g_signal_emit (client , signals [PK_CLIENT_FINISHED], 0, exit, runtime);
	}
	client->priv->is_finished = TRUE;
}

/**
 * pk_client_percentage_changed_cb:
 */
static void
pk_client_percentage_changed_cb (DBusGProxy  *proxy,
			         const gchar *tid,
			         guint	      percentage,
			         PkClient    *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		pk_debug ("emit percentage-changed %i", percentage);
		g_signal_emit (client , signals [PK_CLIENT_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_client_sub_percentage_changed_cb:
 */
static void
pk_client_sub_percentage_changed_cb (DBusGProxy  *proxy,
				     const gchar *tid,
			             guint	  percentage,
			             PkClient    *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		pk_debug ("emit sub-percentage-changed %i", percentage);
		g_signal_emit (client, signals [PK_CLIENT_SUB_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_client_no_percentage_updates_cb:
 */
static void
pk_client_no_percentage_updates_cb (DBusGProxy  *proxy,
			            const gchar *tid,
				    PkClient    *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		pk_debug ("emit no-percentage-updates");
		g_signal_emit (client , signals [PK_CLIENT_NO_PERCENTAGE_UPDATES], 0);
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

	status = pk_status_enum_from_text (status_text);
	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
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
	PkClientPackageItem *item;
	PkInfoEnum info;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		pk_debug ("emit package %s, %s, %s", info_text, package_id, summary);
		info = pk_info_enum_from_text (info_text);
		g_signal_emit (client , signals [PK_CLIENT_PACKAGE], 0, info, package_id, summary);

		/* cache */
		if (client->priv->use_buffer == TRUE) {
			pk_debug ("adding to cache array package %i, %s, %s", info, package_id, summary);
			item = g_new0 (PkClientPackageItem, 1);
			item->info = info;
			item->package_id = g_strdup (package_id);
			item->summary = g_strdup (summary);
			g_ptr_array_add (client->priv->cache_package, item);
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
			  PkClient *client)
{
	PkRoleEnum role;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		role = pk_role_enum_from_text (role_text);
		pk_debug ("emitting transaction %s, %s, %i, %s, %i", old_tid, timespec, succeeded, role_text, duration);
		g_signal_emit (client, signals [PK_CLIENT_TRANSACTION], 0, tid, timespec, succeeded, role, duration);
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

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
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
			  PkClient    *client)
{
	PkGroupEnum group;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		group = pk_group_enum_from_text (group_text);
		pk_debug ("emit description %s, %s, %i, %s, %s", package_id, licence, group, description, url);
		g_signal_emit (client , signals [PK_CLIENT_DESCRIPTION], 0, package_id, licence, group, description, url);
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

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
		code = pk_error_enum_from_text (code_text);
		pk_debug ("emit error-code %i, %s", code, details);
		g_signal_emit (client , signals [PK_CLIENT_ERROR_CODE], 0, code, details);
	}
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

	if (pk_transaction_id_equal (tid, client->priv->tid) == TRUE) {
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
 * pk_client_get_percentage:
 **/
gboolean
pk_client_get_percentage (PkClient *client, guint *percentage)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (percentage != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetPercentage", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetPercentage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_client_get_sub_percentage:
 **/
gboolean
pk_client_get_sub_percentage (PkClient *client, guint *percentage)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (percentage != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_warning ("Transaction ID not set");
		return FALSE;
	}

	error = NULL;
	ret = dbus_g_proxy_call (client->priv->proxy, "GetSubPercentage", &error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetSubPercentage failed!");
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
 * pk_client_resolve:
 **/
gboolean
pk_client_resolve (PkClient *client, const gchar *package)
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
	ret = dbus_g_proxy_call (client->priv->proxy, "Resolve", &error,
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

	/* only wait if the command succeeded. False is usually due to PolicyKit auth failure */
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
	signals [PK_CLIENT_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_CLIENT_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_CLIENT_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_CLIENT_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_CLIENT_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_UINT_UINT,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_CLIENT_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_CLIENT_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT_STRING_STRING,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
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
	client->priv->last_status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->require_restart = PK_RESTART_ENUM_NONE;
	client->priv->is_finished = FALSE;
	client->priv->cache_package = g_ptr_array_new ();

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

	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, 
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* transaction */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_BOOL_STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "PercentageChanged",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PercentageChanged",
				     G_CALLBACK (pk_client_percentage_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "SubPercentageChanged",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "SubPercentageChanged",
				     G_CALLBACK (pk_client_sub_percentage_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "NoPercentageUpdates",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "NoPercentageUpdates",
				     G_CALLBACK (pk_client_no_percentage_updates_cb), client, NULL);

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
				 G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
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
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Description",
				     G_CALLBACK (pk_client_description_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "ErrorCode",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_client_error_code_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RequireRestart",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_client_require_restart_cb), client, NULL);
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

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Finished",
				        G_CALLBACK (pk_client_finished_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "PercentageChanged",
				        G_CALLBACK (pk_client_percentage_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "SubPercentageChanged",
				        G_CALLBACK (pk_client_sub_percentage_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "NoPercentageUpdates",
				        G_CALLBACK (pk_client_no_percentage_updates_cb), client);
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
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ErrorCode",
				        G_CALLBACK (pk_client_error_code_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RequireRestart",
				        G_CALLBACK (pk_client_require_restart_cb), client);

	/* free the proxy */
	g_object_unref (G_OBJECT (client->priv->proxy));
	g_object_unref (client->priv->pconnection);
	g_object_unref (client->priv->polkit);

	/* removed any cached packages */
	pk_client_remove_cache_package (client);
	g_ptr_array_free (client->priv->cache_package, TRUE);

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

