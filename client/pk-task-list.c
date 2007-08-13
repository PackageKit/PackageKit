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
#include "pk-task-list.h"

static void     pk_task_list_class_init	(PkTaskListClass *klass);
static void     pk_task_list_init		(PkTaskList      *task_list);
static void     pk_task_list_finalize		(GObject           *object);

#define PK_TASK_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_LIST, PkTaskListPrivate))

struct PkTaskListPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
};

typedef enum {
	PK_TASK_LIST_JOB_LIST_CHANGED,
	PK_TASK_LIST_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_LIST_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskList, pk_task_list, G_TYPE_OBJECT)

/**
 * pk_task_list_get_job_list:
 **/
gboolean
pk_task_list_get_job_list (PkTaskList *tlist, GSList *list)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tlist->priv->proxy, "GetJobList", &error,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &list,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetJobList failed!");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_task_list_job_list_changed_cb:
 */
static void
pk_task_list_job_list_changed_cb (DBusGProxy   *proxy,
			    guint	  job,
			    const gchar	 *exit_text,
			    PkTaskList *tlist)
{
	g_return_if_fail (tlist != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (tlist));

//	exit = pk_task_list_exit_from_text (exit_text);
//	pk_debug ("emit finished %i", exit);
//	g_signal_emit (tlist , signals [PK_TASK_LIST_FINISHED], 0, exit);
}

/**
 * pk_task_list_class_init:
 **/
static void
pk_task_list_class_init (PkTaskListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_list_finalize;

	signals [PK_TASK_LIST_JOB_LIST_CHANGED] =
		g_signal_new ("job-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	g_type_class_add_private (klass, sizeof (PkTaskListPrivate));
}

/**
 * pk_task_list_init:
 **/
static void
pk_task_list_init (PkTaskList *tlist)
{
	GError *error = NULL;
	DBusGProxy *proxy = NULL;

	tlist->priv = PK_TASK_LIST_GET_PRIVATE (tlist);

	/* check dbus connections, exit if not valid */
	tlist->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (tlist->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	tlist->priv->proxy = proxy;
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "JobListChanged",
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "JobListChanged",
				     G_CALLBACK (pk_task_list_job_list_changed_cb), tlist, NULL);
}

/**
 * pk_task_list_finalize:
 **/
static void
pk_task_list_finalize (GObject *object)
{
	PkTaskList *tlist;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (object));
	tlist = PK_TASK_LIST (object);
	g_return_if_fail (tlist->priv != NULL);

	/* free the proxy */
	g_object_unref (G_OBJECT (tlist->priv->proxy));

	G_OBJECT_CLASS (pk_task_list_parent_class)->finalize (object);
}

/**
 * pk_task_list_new:
 **/
PkTaskList *
pk_task_list_new (void)
{
	PkTaskList *tlist;
	tlist = g_object_new (PK_TYPE_TASK_LIST, NULL);
	return PK_TASK_LIST (tlist);
}

