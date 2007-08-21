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
#include "pk-task-client.h"
#include "pk-task-monitor.h"

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
	PkTaskStatus		 last_status;
	PkTaskMonitor		*tmonitor;
	gboolean		 is_finished;
	PkConnection		*pconnection;
};

typedef enum {
	PK_TASK_CLIENT_JOB_STATUS_CHANGED,
	PK_TASK_CLIENT_PERCENTAGE_CHANGED,
	PK_TASK_CLIENT_NO_PERCENTAGE_UPDATES,
	PK_TASK_CLIENT_PACKAGE,
	PK_TASK_CLIENT_ERROR_CODE,
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
 * pk_task_client_reset:
 **/
gboolean
pk_task_client_reset (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	if (tclient->priv->is_finished != TRUE) {
		pk_warning ("not exit status, cannot reset");
		return FALSE;
	}
	tclient->priv->assigned = FALSE;
	tclient->priv->is_sync = FALSE;
	tclient->priv->job = 0;
	tclient->priv->last_status = PK_TASK_STATUS_UNKNOWN;
	tclient->priv->is_finished = FALSE;
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
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetUpdates failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
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
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("UpdateSystem failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_find_packages:
 **/
gboolean
pk_task_client_find_packages (PkTaskClient *tclient, const gchar *search, guint depth, gboolean installed, gboolean available)
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
				 G_TYPE_UINT, depth,
				 G_TYPE_BOOLEAN, installed,
				 G_TYPE_BOOLEAN, available,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("FindPackages failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

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
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetDeps failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_get_description:
 **/
gboolean
pk_task_client_get_description (PkTaskClient *tclient, const gchar *package)
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
	ret = dbus_g_proxy_call (tclient->priv->proxy, "GetDescription", &error,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetDescription failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
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
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RemovePackage failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_refresh_cache:
 **/
gboolean
pk_task_client_refresh_cache (PkTaskClient *tclient, gboolean force)
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
	ret = dbus_g_proxy_call (tclient->priv->proxy, "RefreshCache", &error,
				 G_TYPE_BOOLEAN, force,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RefreshCache failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
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
		const gchar *error_name;
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RemovePackageWithDeps failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

static gboolean
attempt_to_gain_privilege (const char *error_detail)
{
	const gchar *pk_action;
	const gchar *pk_result;
	gchar **tokens;
	gboolean ret;
	DBusGConnection *session_bus;
	DBusGProxy *polkit_gnome_proxy;
	GError *error = NULL;
	gboolean gained_privilege;

	ret = FALSE;
	tokens = NULL;
	polkit_gnome_proxy = NULL;

	tokens = g_strsplit (error_detail, " ", 0);
	if (tokens == NULL) {
		goto out;
	}
	if (g_strv_length (tokens) < 2) {
		goto out;
	}
	pk_action = tokens[0];
	pk_result = tokens[1];

	pk_debug ("pk_action='%s' pk_result='%s'", pk_action, pk_result);

	session_bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (session_bus == NULL) {
		pk_warning ("Caught exception '%s'", error->message);
		g_error_free (error);
		goto out;
	}
	polkit_gnome_proxy = dbus_g_proxy_new_for_name (session_bus,
							"org.gnome.PolicyKit",	   /* bus name */
							"/org/gnome/PolicyKit/Manager",  /* object */
							"org.gnome.PolicyKit.Manager");  /* interface */
	if (polkit_gnome_proxy == NULL) {
		pk_warning ("failed to connect to PolicyKit-gnome");
		goto out;
	}

	/* now use PolicyKit-gnome to bring up an auth dialog (we
	 * don't have any windows so set the XID to "null") */
	if (!dbus_g_proxy_call_with_timeout (polkit_gnome_proxy,
					     "ShowDialog",
					     INT_MAX,
					     &error,
					     /* parameters: */
					     G_TYPE_STRING, pk_action,      /* action_id */
					     G_TYPE_UINT, 0,		/* X11 window ID */
					     G_TYPE_INVALID,
					     /* return values: */
					     G_TYPE_BOOLEAN, &gained_privilege,
					     G_TYPE_INVALID)) {
		pk_warning ("Caught exception '%s'", error->message);
		g_error_free (error);
		goto out;
	}

	ret = gained_privilege;
	pk_debug ("gained privilege = %d", gained_privilege);

out:
	if (polkit_gnome_proxy != NULL)
		g_object_unref (polkit_gnome_proxy);
	if (tokens != NULL)
		g_strfreev (tokens);

	return ret;
}

/**
 * pk_task_client_install_package_dbus:
 **/
gboolean
pk_task_client_install_package_dbus (PkTaskClient *tclient, const gchar *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "InstallPackage", error,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("InstallPackage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_task_client_install_package:
 **/
gboolean
pk_task_client_install_package (PkTaskClient *tclient, const gchar *package)
{
	gboolean ret;
	const gchar *error_name;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		pk_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	ret = pk_task_client_install_package_dbus (tclient, package, &error);
	if (ret == FALSE) {
		error_name = dbus_g_error_get_name (error);
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		if (strcmp (error_name, "org.freedesktop.PackageKit.RefusedByPolicy") == 0) {
			gboolean ret = attempt_to_gain_privilege (error->message);
			pk_warning ("now=%i", ret);
		}
		g_error_free (error);

		ret = pk_task_client_install_package_dbus (tclient, package, &error);
		pk_warning ("now2=%i", ret);
	}

	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
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
pk_task_client_finished_cb (PkTaskMonitor *tmonitor,
			    PkTaskExit     exit,
			    PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit finished %i", exit);
	tclient->priv->is_finished = TRUE;
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_FINISHED], 0, exit);

	/* if we are async, then cancel */
	if (tclient->priv->loop != NULL) {
		g_main_loop_quit (tclient->priv->loop);
	}
}

/**
 * pk_task_client_percentage_changed_cb:
 */
static void
pk_task_client_percentage_changed_cb (PkTaskMonitor *tmonitor,
				      guint	     percentage,
				      PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit percentage-changed %i", percentage);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_PERCENTAGE_CHANGED], 0, percentage);
}

/**
 * pk_task_client_no_percentage_updates_cb:
 */
static void
pk_task_client_no_percentage_updates_cb (PkTaskMonitor *tmonitor,
				         PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit no-percentage-updates");
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_NO_PERCENTAGE_UPDATES], 0);
}

/**
 * pk_task_client_job_status_changed_cb:
 */
static void
pk_task_client_job_status_changed_cb (PkTaskMonitor *tmonitor,
				      PkTaskStatus   status,
				      PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit job-status-changed %i", status);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_JOB_STATUS_CHANGED], 0, status);
	tclient->priv->last_status = status;
}

/**
 * pk_task_client_package_cb:
 */
static void
pk_task_client_package_cb (PkTaskMonitor *tmonitor,
			   guint          value,
			   const gchar   *package,
			   const gchar   *summary,
			   PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit package %i, %s, %s", value, package, summary);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_PACKAGE], 0, value, package, summary);
}

/**
 * pk_task_client_error_code_cb:
 */
static void
pk_task_client_error_code_cb (PkTaskMonitor  *tmonitor,
			      PkTaskErrorCode code,
			      const gchar    *details,
			      PkTaskClient   *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit error-code %i, %s", code, details);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_ERROR_CODE], 0, code, details);
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
	signals [PK_TASK_CLIENT_NO_PERCENTAGE_UPDATES] =
		g_signal_new ("no-percentage-updates",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_TASK_CLIENT_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_TASK_CLIENT_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_TASK_CLIENT_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkTaskClientPrivate));
}

/**
 * pk_task_client_connect:
 **/
static void
pk_task_client_connect (PkTaskClient *tclient)
{
	pk_debug ("connect");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkTaskClient *tclient)
{
	pk_debug ("connected=%i", connected);
	/* do we have to requeue the action if PK exitied half way? */
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
	tclient->priv->last_status = PK_TASK_STATUS_UNKNOWN;
	tclient->priv->is_finished = FALSE;

	/* check dbus connections, exit if not valid */
	tclient->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* watch for PackageKit on the bus, and try to connect up at start */
	tclient->priv->pconnection = pk_connection_new ();
	g_signal_connect (tclient->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), tclient);
	if (pk_connection_valid (tclient->priv->pconnection)) {
		pk_task_client_connect (tclient);
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (tclient->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	tclient->priv->proxy = proxy;

	tclient->priv->tmonitor = pk_task_monitor_new ();
	g_signal_connect (tclient->priv->tmonitor, "finished",
			  G_CALLBACK (pk_task_client_finished_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "percentage-changed",
			  G_CALLBACK (pk_task_client_percentage_changed_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "no-percentage-updates",
			  G_CALLBACK (pk_task_client_no_percentage_updates_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "job-status-changed",
			  G_CALLBACK (pk_task_client_job_status_changed_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "package",
			  G_CALLBACK (pk_task_client_package_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "error-code",
			  G_CALLBACK (pk_task_client_error_code_cb), tclient);
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
	g_object_unref (tclient->priv->pconnection);

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

