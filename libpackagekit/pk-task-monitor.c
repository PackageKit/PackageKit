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

#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-connection.h"
#include "pk-task-common.h"
#include "pk-task-monitor.h"

static void     pk_task_monitor_class_init	(PkTaskMonitorClass *klass);
static void     pk_task_monitor_init		(PkTaskMonitor      *task_monitor);
static void     pk_task_monitor_finalize	(GObject           *object);

#define PK_TASK_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_MONITOR, PkTaskMonitorPrivate))

struct PkTaskMonitorPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	gchar			*tid;
	PkConnection		*pconnection;
};

typedef enum {
	PK_TASK_MONITOR_TRANSACTION_STATUS_CHANGED,
	PK_TASK_MONITOR_PERCENTAGE_CHANGED,
	PK_TASK_MONITOR_SUB_PERCENTAGE_CHANGED,
	PK_TASK_MONITOR_NO_PERCENTAGE_UPDATES,
	PK_TASK_MONITOR_PACKAGE,
	PK_TASK_MONITOR_TRANSACTION,
	PK_TASK_MONITOR_UPDATE_DETAIL,
	PK_TASK_MONITOR_DESCRIPTION,
	PK_TASK_MONITOR_ERROR_CODE,
	PK_TASK_MONITOR_REQUIRE_RESTART,
	PK_TASK_MONITOR_FINISHED,
	PK_TASK_MONITOR_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_MONITOR_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskMonitor, pk_task_monitor, G_TYPE_OBJECT)

/**
 * pk_task_monitor_set_tid:
 **/
gboolean
pk_task_monitor_set_tid (PkTaskMonitor *tmonitor, const gchar *tid)
{
	tmonitor->priv->tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_task_monitor_get_tid:
 **/
gchar *
pk_task_monitor_get_tid (PkTaskMonitor *tmonitor)
{
	return g_strdup (tmonitor->priv->tid);
}

/**
 * pk_task_monitor_get_status:
 **/
gboolean
pk_task_monitor_get_status (PkTaskMonitor *tmonitor, PkStatusEnum *status)
{
	gboolean ret;
	gchar *status_text;
	GError *error;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (status != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->tid != NULL, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetStatus", &error,
				 G_TYPE_STRING, tmonitor->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_INVALID);
	if (error) {
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
 * pk_task_monitor_get_package:
 **/
gboolean
pk_task_monitor_get_package (PkTaskMonitor *tmonitor, gchar **package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->tid != NULL, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetPackage", &error,
				 G_TYPE_STRING, tmonitor->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID);
	if (error) {
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
 * pk_task_monitor_cancel:
 **/
gboolean
pk_task_monitor_cancel (PkTaskMonitor *tmonitor)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->tid != NULL, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "Cancel", &error,
				 G_TYPE_STRING, tmonitor->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("Cancel failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_task_monitor_get_percentage:
 **/
gboolean
pk_task_monitor_get_percentage (PkTaskMonitor *tmonitor, guint *percentage)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (percentage != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->tid != NULL, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetPercentage", &error,
				 G_TYPE_STRING, tmonitor->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_INVALID);
	if (error) {
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
 * pk_task_monitor_get_sub_percentage:
 **/
gboolean
pk_task_monitor_get_sub_percentage (PkTaskMonitor *tmonitor, guint *percentage)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (percentage != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->tid != NULL, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetSubPercentage", &error,
				 G_TYPE_STRING, tmonitor->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_INVALID);
	if (error) {
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
 * pk_task_monitor_get_role:
 **/
gboolean
pk_task_monitor_get_role (PkTaskMonitor *tmonitor, PkRoleEnum *role, gchar **package_id)
{
	gboolean ret;
	GError *error;
	gchar *role_text;
	gchar *package_id_temp;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (role != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->tid != NULL, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetRole", &error,
				 G_TYPE_STRING, tmonitor->priv->tid,
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
 * pk_task_monitor_finished_cb:
 */
static void
pk_task_monitor_finished_cb (DBusGProxy    *proxy,
			     gchar	   *tid,
			     const gchar   *exit_text,
			     guint          runtime,
			     PkTaskMonitor *tmonitor)
{
	PkExitEnum exit;

	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		exit = pk_exit_enum_from_text (exit_text);
		pk_debug ("emit finished %i, %i", exit, runtime);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_FINISHED], 0, exit, runtime);
	}
}

/**
 * pk_task_monitor_percentage_changed_cb:
 */
static void
pk_task_monitor_percentage_changed_cb (DBusGProxy    *proxy,
				       const gchar   *tid,
				       guint	      percentage,
				       PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		pk_debug ("emit percentage-changed %i", percentage);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_task_monitor_sub_percentage_changed_cb:
 */
static void
pk_task_monitor_sub_percentage_changed_cb (DBusGProxy    *proxy,
			   		   const gchar   *tid,
				           guint	  percentage,
				           PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		pk_debug ("emit sub-percentage-changed %i", percentage);
		g_signal_emit (tmonitor, signals [PK_TASK_MONITOR_SUB_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_task_monitor_no_percentage_updates_cb:
 */
static void
pk_task_monitor_no_percentage_updates_cb (DBusGProxy    *proxy,
			                  const gchar  *tid,
					  PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		pk_debug ("emit no-percentage-updates");
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_NO_PERCENTAGE_UPDATES], 0);
	}
}

/**
 * pk_task_monitor_transaction_status_changed_cb:
 */
static void
pk_task_monitor_transaction_status_changed_cb (DBusGProxy   *proxy,
				       const gchar  *tid,
				       const gchar  *status_text,
				       PkTaskMonitor *tmonitor)
{
	PkStatusEnum status;

	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	status = pk_status_enum_from_text (status_text);

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		pk_debug ("emit transaction-status-changed %i", status);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_TRANSACTION_STATUS_CHANGED], 0, status);
	}
}

/**
 * pk_task_monitor_package_cb:
 */
static void
pk_task_monitor_package_cb (DBusGProxy   *proxy,
			    const gchar  *tid,
			    guint         value,
			    const gchar  *package,
			    const gchar  *summary,
			    PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		pk_debug ("emit package %i, %s, %s", value, package, summary);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_PACKAGE], 0, value, package, summary);
	}
}

/**
 * pk_task_monitor_transaction_cb:
 */
static void
pk_task_monitor_transaction_cb (DBusGProxy   *proxy,
				const gchar *tid, const gchar *timespec,
				gboolean succeeded, const gchar *role, guint duration,
				PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	pk_debug ("emitting transaction %s, %s, %i, %s, %i", tid, timespec, succeeded, role, duration);
	g_signal_emit (tmonitor, signals [PK_TASK_MONITOR_TRANSACTION], 0, tid, timespec, succeeded, role, duration);
}

/**
 * pk_task_monitor_update_detail_cb:
 */
static void
pk_task_monitor_update_detail_cb (DBusGProxy  *proxy,
			          const gchar  *tid,
			          const gchar *package_id,
			          const gchar *updates,
			          const gchar *obsoletes,
			          const gchar *url,
			          const gchar *restart,
			          const gchar *update_text,
			          PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		pk_debug ("emit update-detail %s, %s, %s, %s, %s, %s",
			  package_id, updates, obsoletes, url, restart, update_text);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_UPDATE_DETAIL], 0,
			       package_id, updates, obsoletes, url, restart, update_text);
	}
}

/**
 * pk_task_monitor_description_cb:
 */
static void
pk_task_monitor_description_cb (DBusGProxy    *proxy,
			        const gchar   *tid,
				const gchar   *package_id,
				const gchar   *licence,
				const gchar   *group_text,
				const gchar   *description,
				const gchar   *url,
				PkTaskMonitor *tmonitor)
{
	PkGroupEnum group;
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		group = pk_group_enum_from_text (group_text);
		pk_debug ("emit description %s, %s, %i, %s, %s", package_id, licence, group, description, url);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_DESCRIPTION], 0, package_id, licence, group, description, url);
	}
}

/**
 * pk_task_monitor_error_code_cb:
 */
static void
pk_task_monitor_error_code_cb (DBusGProxy   *proxy,
			   const gchar  *tid,
			   const gchar  *code_text,
			   const gchar  *details,
			   PkTaskMonitor *tmonitor)
{
	PkErrorCodeEnum code;
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		code = pk_error_enum_from_text (code_text);
		pk_debug ("emit error-code %i, %s", code, details);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_ERROR_CODE], 0, code, details);
	}
}

/**
 * pk_task_monitor_require_restart_cb:
 */
static void
pk_task_monitor_require_restart_cb (DBusGProxy   *proxy,
			   const gchar  *tid,
			   const gchar  *restart_text,
			   const gchar  *details,
			   PkTaskMonitor *tmonitor)
{
	PkRestartEnum restart;
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (pk_transaction_id_equal (tid, tmonitor->priv->tid) == TRUE) {
		restart = pk_restart_enum_from_text (restart_text);
		pk_debug ("emit require-restart %i, %s", restart, details);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_REQUIRE_RESTART], 0, restart, details);
	}
}

/**
 * pk_task_monitor_class_init:
 **/
static void
pk_task_monitor_class_init (PkTaskMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_monitor_finalize;

	signals [PK_TASK_MONITOR_TRANSACTION_STATUS_CHANGED] =
		g_signal_new ("transaction-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_MONITOR_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_MONITOR_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_MONITOR_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_TASK_MONITOR_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_MONITOR_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_STRING_UINT,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_TASK_MONITOR_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_MONITOR_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT_STRING_STRING,
			      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_MONITOR_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TASK_MONITOR_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TASK_MONITOR_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkTaskMonitorPrivate));
}

/**
 * pk_task_monitor_connect:
 **/
static void
pk_task_monitor_connect (PkTaskMonitor *tmonitor)
{
	pk_debug ("connect");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkTaskMonitor *tmonitor)
{
	pk_debug ("connected=%i", connected);

	/* TODO: if PK re-started mid-transaction then show a big fat warning */
}

/**
 * pk_task_monitor_init:
 **/
static void
pk_task_monitor_init (PkTaskMonitor *tmonitor)
{
	GError *error = NULL;
	DBusGProxy *proxy = NULL;

	tmonitor->priv = PK_TASK_MONITOR_GET_PRIVATE (tmonitor);
	tmonitor->priv->tid = NULL;

	/* check dbus connections, exit if not valid */
	tmonitor->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* watch for PackageKit on the bus, and try to connect up at start */
	tmonitor->priv->pconnection = pk_connection_new ();
	g_signal_connect (tmonitor->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), tmonitor);
	if (pk_connection_valid (tmonitor->priv->pconnection)) {
		pk_task_monitor_connect (tmonitor);
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (tmonitor->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	tmonitor->priv->proxy = proxy;
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
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_BOOL_STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_task_monitor_finished_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "PercentageChanged",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PercentageChanged",
				     G_CALLBACK (pk_task_monitor_percentage_changed_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "SubPercentageChanged",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "SubPercentageChanged",
				     G_CALLBACK (pk_task_monitor_sub_percentage_changed_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "NoPercentageUpdates",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "NoPercentageUpdates",
				     G_CALLBACK (pk_task_monitor_no_percentage_updates_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "TransactionStatusChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "TransactionStatusChanged",
				     G_CALLBACK (pk_task_monitor_transaction_status_changed_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_task_monitor_package_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "Transaction",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Transaction",
				     G_CALLBACK (pk_task_monitor_transaction_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "UpdateDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_task_monitor_update_detail_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "Description",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Description",
				     G_CALLBACK (pk_task_monitor_description_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "ErrorCode",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_task_monitor_error_code_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "RequireRestart",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_task_monitor_require_restart_cb), tmonitor, NULL);
}

/**
 * pk_task_monitor_finalize:
 **/
static void
pk_task_monitor_finalize (GObject *object)
{
	PkTaskMonitor *tmonitor;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (object));
	tmonitor = PK_TASK_MONITOR (object);
	g_return_if_fail (tmonitor->priv != NULL);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "Finished",
				        G_CALLBACK (pk_task_monitor_finished_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "PercentageChanged",
				        G_CALLBACK (pk_task_monitor_percentage_changed_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "SubPercentageChanged",
				        G_CALLBACK (pk_task_monitor_sub_percentage_changed_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "NoPercentageUpdates",
				        G_CALLBACK (pk_task_monitor_no_percentage_updates_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "TransactionStatusChanged",
				        G_CALLBACK (pk_task_monitor_transaction_status_changed_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "Package",
				        G_CALLBACK (pk_task_monitor_package_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "Transaction",
				        G_CALLBACK (pk_task_monitor_transaction_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "Description",
				        G_CALLBACK (pk_task_monitor_description_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "ErrorCode",
				        G_CALLBACK (pk_task_monitor_error_code_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "RequireRestart",
				        G_CALLBACK (pk_task_monitor_require_restart_cb), tmonitor);

	/* free the proxy */
	g_object_unref (G_OBJECT (tmonitor->priv->proxy));
	g_object_unref (tmonitor->priv->pconnection);

	G_OBJECT_CLASS (pk_task_monitor_parent_class)->finalize (object);
}

/**
 * pk_task_monitor_new:
 **/
PkTaskMonitor *
pk_task_monitor_new (void)
{
	PkTaskMonitor *tmonitor;
	tmonitor = g_object_new (PK_TYPE_TASK_MONITOR, NULL);
	return PK_TASK_MONITOR (tmonitor);
}

