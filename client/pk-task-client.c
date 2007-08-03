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

#include "pk-marshal.h"
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
};

static guint signals [PK_TASK_CLIENT_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskClient, pk_task_client, G_TYPE_OBJECT)

#if 0
/**
 * pk_task_client_change_percentage:
 **/
static gboolean
pk_task_client_change_percentage (PkTaskClient *tclient, guint percentage)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);
	g_debug ("emit percentage-complete-changed %i", percentage);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_PERCENTAGE_CHANGED], 0, percentage);
	return TRUE;
}

/**
 * pk_task_client_package:
 **/
static gboolean
pk_task_client_package (PkTaskClient *tclient, const gchar *package, const gchar *summary)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	g_debug ("emit package %s, %s", package, summary);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_PACKAGE], 0, package, summary);

	return TRUE;
}
#endif

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

	if (tclient->priv->is_sync == TRUE) {
		g_warning ("sync not supported");
	}
	return TRUE;
}

/**
 * pk_task_client_get_updates:
 **/
gboolean
pk_task_client_get_updates (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	return TRUE;
}

/**
 * pk_task_client_update_system:
 **/
gboolean
pk_task_client_update_system (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	return TRUE;
}

/**
 * pk_task_client_find_packages:
 **/
gboolean
pk_task_client_find_packages (PkTaskClient *tclient, const gchar *search)
{
	guint job;
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "FindPackages", &error,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &job,
				 G_TYPE_INVALID);
	if (error) {
		g_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		g_warning ("FindPackages failed!");
		return FALSE;
	}
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_get_deps:
 **/
gboolean
pk_task_client_get_deps (PkTaskClient *tclient, const gchar *package)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	return TRUE;
}

/**
 * pk_task_client_remove_package:
 **/
gboolean
pk_task_client_remove_package (PkTaskClient *tclient, const gchar *package)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	return TRUE;
}

/**
 * pk_task_client_remove_package_with_deps:
 **/
gboolean
pk_task_client_remove_package_with_deps (PkTaskClient *tclient, const gchar *package)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

	return TRUE;
}

/**
 * pk_task_client_install_package:
 **/
gboolean
pk_task_client_install_package (PkTaskClient *tclient, const gchar *package)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	/* check to see if we already have an action */
	if (tclient->priv->assigned == TRUE) {
		g_warning ("Already assigned");
		return FALSE;
	}
	tclient->priv->assigned = TRUE;

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
		g_warning ("Not assigned");
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
			    guint	  status,
			    PkTaskClient *tclient)
{
	if (job == tclient->priv->job) {
		g_debug ("emit finished %i", status);
		g_signal_emit (tclient , signals [PK_TASK_CLIENT_FINISHED], 0, status);
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
		g_signal_new ("percentage-complete-changed",
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
		g_warning ("%s", error->message);
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
	/* TODO: set up other callbacks */
	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_task_client_finished_cb), tclient, NULL);
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

