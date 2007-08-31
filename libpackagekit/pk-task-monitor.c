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
	guint			 job;
	PkConnection		*pconnection;
};

typedef enum {
	PK_TASK_MONITOR_JOB_STATUS_CHANGED,
	PK_TASK_MONITOR_PERCENTAGE_CHANGED,
	PK_TASK_MONITOR_SUB_PERCENTAGE_CHANGED,
	PK_TASK_MONITOR_NO_PERCENTAGE_UPDATES,
	PK_TASK_MONITOR_PACKAGE,
	PK_TASK_MONITOR_DESCRIPTION,
	PK_TASK_MONITOR_ERROR_CODE,
	PK_TASK_MONITOR_REQUIRE_RESTART,
	PK_TASK_MONITOR_FINISHED,
	PK_TASK_MONITOR_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_MONITOR_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskMonitor, pk_task_monitor, G_TYPE_OBJECT)

/**
 * pk_task_monitor_set_job:
 **/
gboolean
pk_task_monitor_set_job (PkTaskMonitor *tmonitor, guint job)
{
	tmonitor->priv->job = job;
	return TRUE;
}

/**
 * pk_task_monitor_get_job:
 **/
guint
pk_task_monitor_get_job (PkTaskMonitor *tmonitor)
{
	return tmonitor->priv->job;
}

/**
 * pk_task_monitor_get_status:
 **/
gboolean
pk_task_monitor_get_status (PkTaskMonitor *tmonitor, PkTaskStatus *status)
{
	gboolean ret;
	gchar *status_text;
	GError *error;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (status != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->job != 0, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetJobStatus", &error,
				 G_TYPE_UINT, tmonitor->priv->job,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetJobStatus failed!");
		return FALSE;
	}
	*status = pk_task_status_from_text (status_text);
	return TRUE;
}

/**
 * pk_task_monitor_get_role:
 **/
gboolean
pk_task_monitor_get_role (PkTaskMonitor *tmonitor, PkTaskStatus *status, gchar **package_id)
{
	gboolean ret;
	GError *error;
	gchar *status_text;
	gchar *package_id_temp;

	g_return_val_if_fail (tmonitor != NULL, FALSE);
	g_return_val_if_fail (status != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_MONITOR (tmonitor), FALSE);
	g_return_val_if_fail (tmonitor->priv->job != 0, FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tmonitor->priv->proxy, "GetJobRole", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_STRING, &package_id_temp,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetJobStatus failed :%s", error->message);
		g_error_free (error);
		return FALSE;
	}
	*status = pk_task_status_from_text (status_text);
	if (package_id != NULL) {
		*package_id = g_strdup (package_id_temp);
	}
	return TRUE;
}

/**
 * pk_task_monitor_finished_cb:
 */
static void
pk_task_monitor_finished_cb (DBusGProxy    *proxy,
			     guint	    job,
			     const gchar   *exit_text,
			     guint          runtime,
			     PkTaskMonitor *tmonitor)
{
	PkTaskExit exit;

	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		exit = pk_task_exit_from_text (exit_text);
		pk_debug ("emit finished %i, %i", exit, runtime);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_FINISHED], 0, exit, runtime);
	}
}

/**
 * pk_task_monitor_percentage_changed_cb:
 */
static void
pk_task_monitor_percentage_changed_cb (DBusGProxy    *proxy,
				       guint	      job,
				       guint	      percentage,
				       PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		pk_debug ("emit percentage-changed %i", percentage);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_task_monitor_sub_percentage_changed_cb:
 */
static void
pk_task_monitor_sub_percentage_changed_cb (DBusGProxy    *proxy,
				           guint	  job,
				           guint	  percentage,
				           PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		pk_debug ("emit sub-percentage-changed %i", percentage);
		g_signal_emit (tmonitor, signals [PK_TASK_MONITOR_SUB_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_task_monitor_no_percentage_updates_cb:
 */
static void
pk_task_monitor_no_percentage_updates_cb (DBusGProxy    *proxy,
					  guint	         job,
					  PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		pk_debug ("emit no-percentage-updates");
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_NO_PERCENTAGE_UPDATES], 0);
	}
}

/**
 * pk_task_monitor_job_status_changed_cb:
 */
static void
pk_task_monitor_job_status_changed_cb (DBusGProxy   *proxy,
				       guint	    job,
				       const gchar  *status_text,
				       PkTaskMonitor *tmonitor)
{
	PkTaskStatus status;

	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	status = pk_task_status_from_text (status_text);

	if (job == tmonitor->priv->job) {
		pk_debug ("emit job-status-changed %i", status);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_JOB_STATUS_CHANGED], 0, status);
	}
}

/**
 * pk_task_monitor_package_cb:
 */
static void
pk_task_monitor_package_cb (DBusGProxy   *proxy,
			    guint	 job,
			    guint         value,
			    const gchar  *package,
			    const gchar  *summary,
			    PkTaskMonitor *tmonitor)
{
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		pk_debug ("emit package %i, %s, %s", value, package, summary);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_PACKAGE], 0, value, package, summary);
	}
}

/**
 * pk_task_monitor_description_cb:
 */
static void
pk_task_monitor_description_cb (DBusGProxy    *proxy,
				guint	       job,
				const gchar   *package_id,
				const gchar   *group_text,
				const gchar   *description,
				const gchar   *url,
				PkTaskMonitor *tmonitor)
{
	PkTaskGroup group;
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		group = pk_task_group_from_text (group_text);
		pk_debug ("emit description %s, %i, %s, %s", package_id, group, description, url);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_DESCRIPTION], 0, package_id, group, description, url);
	}
}

/**
 * pk_task_monitor_error_code_cb:
 */
static void
pk_task_monitor_error_code_cb (DBusGProxy   *proxy,
			   guint	 job,
			   const gchar  *code_text,
			   const gchar  *details,
			   PkTaskMonitor *tmonitor)
{
	PkTaskErrorCode code;
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		code = pk_task_error_code_from_text (code_text);
		pk_debug ("emit error-code %i, %s", code, details);
		g_signal_emit (tmonitor , signals [PK_TASK_MONITOR_ERROR_CODE], 0, code, details);
	}
}

/**
 * pk_task_monitor_require_restart_cb:
 */
static void
pk_task_monitor_require_restart_cb (DBusGProxy   *proxy,
			   guint	 job,
			   const gchar  *restart_text,
			   const gchar  *details,
			   PkTaskMonitor *tmonitor)
{
	PkTaskRestart restart;
	g_return_if_fail (tmonitor != NULL);
	g_return_if_fail (PK_IS_TASK_MONITOR (tmonitor));

	if (job == tmonitor->priv->job) {
		restart = pk_task_restart_from_text (restart_text);
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

	signals [PK_TASK_MONITOR_JOB_STATUS_CHANGED] =
		g_signal_new ("job-status-changed",
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
	signals [PK_TASK_MONITOR_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
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
	tmonitor->priv->job = 0;

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
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_STRING_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_task_monitor_finished_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "PercentageChanged",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PercentageChanged",
				     G_CALLBACK (pk_task_monitor_percentage_changed_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "SubPercentageChanged",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "SubPercentageChanged",
				     G_CALLBACK (pk_task_monitor_sub_percentage_changed_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "NoPercentageUpdates",
				 G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "NoPercentageUpdates",
				     G_CALLBACK (pk_task_monitor_no_percentage_updates_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "JobStatusChanged",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "JobStatusChanged",
				     G_CALLBACK (pk_task_monitor_job_status_changed_cb), tmonitor, NULL);

	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_task_monitor_package_cb), tmonitor, NULL);
	dbus_g_proxy_add_signal (proxy, "Description",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Description",
				     G_CALLBACK (pk_task_monitor_description_cb), tmonitor, NULL);
	dbus_g_proxy_add_signal (proxy, "ErrorCode",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_task_monitor_error_code_cb), tmonitor, NULL);
	dbus_g_proxy_add_signal (proxy, "RequireRestart",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
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
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "JobStatusChanged",
				        G_CALLBACK (pk_task_monitor_job_status_changed_cb), tmonitor);
	dbus_g_proxy_disconnect_signal (tmonitor->priv->proxy, "Package",
				        G_CALLBACK (pk_task_monitor_package_cb), tmonitor);
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

