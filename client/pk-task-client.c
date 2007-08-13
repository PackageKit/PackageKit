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
#include "pk-task-common.h"
#include "pk-task-client.h"

static void     pk_task_client_class_init	(PkTaskClientClass *klass);
static void     pk_task_client_init		(PkTaskClient      *task_client);
static void     pk_task_client_finalize		(GObject           *object);

#define PK_TASK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_CLIENT, PkTaskClientPrivate))

struct PkTaskClientPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	gboolean		 assigned;
	gboolean		 is_sync;
	guint			 job;
	GMainLoop		*loop;
};

typedef enum {
	PK_TASK_CLIENT_JOB_STATUS_CHANGED,
	PK_TASK_CLIENT_PERCENTAGE_CHANGED,
	PK_TASK_CLIENT_PACKAGE,
	PK_TASK_CLIENT_FINISHED,
	PK_TASK_CLIENT_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_CLIENT_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskClient, pk_task_client, G_TYPE_OBJECT)

/**
 * pk_task_client_set_sync:
 **/
gboolean
pk_task_client_set_sync (PkTaskClient *tclient, gboolean is_sync)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	tclient->priv->is_sync = is_sync;
	return TRUE;
}

/**
 * pk_task_client_wait_if_sync:
 **/
static gboolean
pk_task_client_wait_if_sync (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	pk_debug ("starting loop");
	if (tclient->priv->is_sync == TRUE) {
		tclient->priv->loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (tclient->priv->loop);
	}
	return TRUE;
}

/**
 * pk_task_client_get_updates:
 **/
gboolean
pk_task_client_get_updates (PkTaskClient *tclient)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "GetUpdates", &error,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetUpdates failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_update_system:
 **/
gboolean
pk_task_client_update_system (PkTaskClient *tclient)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "UpdateSystem", &error,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("UpdateSystem failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_find_packages:
 **/
gboolean
pk_task_client_find_packages (PkTaskClient *tclient, const gchar *search)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "FindPackages", &error,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("FindPackages failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_get_job_status:
 **/
gboolean
pk_task_client_get_job_status (PkTaskClient *tclient, guint job,
			       PkTaskStatus *status, gchar **package)
{
	gboolean ret;
	gchar *status_text;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "GetJobStatus", &error,
				 G_TYPE_UINT, job,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_STRING, package,
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
 * pk_task_client_get_deps:
 **/
gboolean
pk_task_client_get_deps (PkTaskClient *tclient, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "GetDeps", &error,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetDeps failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_remove_package:
 **/
gboolean
pk_task_client_remove_package (PkTaskClient *tclient, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "RemovePackage", &error,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RemovePackage failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_remove_package_with_deps:
 **/
gboolean
pk_task_client_remove_package_with_deps (PkTaskClient *tclient, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "RemovePackageWithDeps", &error,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RemovePackageWithDeps failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_install_package:
 **/
gboolean
pk_task_client_install_package (PkTaskClient *tclient, const gchar *package)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "InstallPackage", &error,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("InstallPackage failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_cancel_job_try:
 **/
gboolean
pk_task_client_cancel_job_try (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we have an action */
	if (tclient->priv->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_task_client_finished_cb:
 */
static void
pk_task_client_finished_cb (DBusGProxy   *proxy,
			    guint	  job,
			    const gchar	 *exit_text,
			    PkTaskClient *tclient)
{
	PkTaskExit exit;

	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	if (job == tclient->priv->job) {
		exit = pk_task_exit_from_text (exit_text);
		pk_debug ("emit finished %i", exit);
		g_signal_emit (tclient , signals [PK_TASK_CLIENT_FINISHED], 0, exit);

		/* if we are async, then cancel */
		if (tclient->priv->loop != NULL) {
			g_main_loop_quit (tclient->priv->loop);
		}
	}
}

/**
 * pk_task_client_percentage_changed_cb:
 */
static void
pk_task_client_percentage_changed_cb (DBusGProxy   *proxy,
				      guint	    job,
				      guint	    percentage,
				      PkTaskClient *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	if (job == tclient->priv->job) {
		pk_debug ("emit percentage-changed %i", percentage);
		g_signal_emit (tclient , signals [PK_TASK_CLIENT_PERCENTAGE_CHANGED], 0, percentage);
	}
}

/**
 * pk_task_client_job_status_changed_cb:
 */
static void
pk_task_client_job_status_changed_cb (DBusGProxy   *proxy,
				      guint	    job,
				      const gchar  *status_text,
				      const gchar  *package,
				      PkTaskClient *tclient)
{
	PkTaskStatus status;

	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	status = pk_task_status_from_text (status_text);

	if (job == tclient->priv->job) {
		pk_debug ("emit job-status-changed %i", status);
		g_signal_emit (tclient , signals [PK_TASK_CLIENT_JOB_STATUS_CHANGED], 0, status);
	}
}

/**
 * pk_task_client_package_cb:
 */
static void
pk_task_client_package_cb (DBusGProxy   *proxy,
			   guint	 job,
			   const gchar  *package,
			   const gchar  *summary,
			   PkTaskClient *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	if (job == tclient->priv->job) {
		pk_debug ("emit package %s, %s", package, summary);
		g_signal_emit (tclient , signals [PK_TASK_CLIENT_PACKAGE], 0, package, summary);
	}
}

/**
 * pk_task_client_class_init:
 **/
static void
pk_task_client_class_init (PkTaskClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_client_finalize;

	signals [PK_TASK_CLIENT_JOB_STATUS_CHANGED] =
		g_signal_new ("job-status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_CLIENT_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_CLIENT_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_CLIENT_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkTaskClientPrivate));
}

/**
 * pk_task_client_init:
 **/
static void
pk_task_client_init (PkTaskClient *tclient)
{
	GError *error = NULL;
	DBusGProxy *proxy = NULL;

	tclient->priv = PK_TASK_CLIENT_GET_PRIVATE (tclient);
	tclient->priv->assigned = FALSE;
	tclient->priv->is_sync = FALSE;
	tclient->priv->job = 0;

	/* check dbus connections, exit if not valid */
	tclient->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (tclient->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	tclient->priv->proxy = proxy;
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_task_client_finished_cb), tclient, NULL);

	dbus_g_proxy_add_signal (proxy, "PercentageChanged",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PercentageChanged",
				     G_CALLBACK (pk_task_client_percentage_changed_cb), tclient, NULL);

	dbus_g_proxy_add_signal (proxy, "JobStatusChanged",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "JobStatusChanged",
				     G_CALLBACK (pk_task_client_job_status_changed_cb), tclient, NULL);

	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_task_client_package_cb), tclient, NULL);
}

/**
 * pk_task_client_finalize:
 **/
static void
pk_task_client_finalize (GObject *object)
{
	PkTaskClient *tclient;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (object));
	tclient = PK_TASK_CLIENT (object);
	g_return_if_fail (tclient->priv != NULL);

	/* free the proxy */
	g_object_unref (G_OBJECT (tclient->priv->proxy));

	G_OBJECT_CLASS (pk_task_client_parent_class)->finalize (object);
}

/**
 * pk_task_client_new:
 **/
PkTaskClient *
pk_task_client_new (void)
{
	PkTaskClient *tclient;
	tclient = g_object_new (PK_TYPE_TASK_CLIENT, NULL);
	return PK_TASK_CLIENT (tclient);
}

