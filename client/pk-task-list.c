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

static void     pk_task_list_class_init		(PkTaskListClass *klass);
static void     pk_task_list_init		(PkTaskList      *task_list);
static void     pk_task_list_finalize		(GObject           *object);

#define PK_TASK_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK_LIST, PkTaskListPrivate))

struct PkTaskListPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	GArray			*job_list;
};

typedef enum {
	PK_TASK_LIST_JOB_LIST_CHANGED,
	PK_TASK_LIST_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_LIST_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskList, pk_task_list, G_TYPE_OBJECT)

/**
 * pk_task_list_refresh_array_with_data:
 **/
gboolean
pk_task_list_refresh_array_with_data (PkTaskList *tlist, GPtrArray *ptrarray)
{
	guint i;
	guint job;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	pk_debug ("ptrarray->len=%i", ptrarray->len);

	/* reset the array */
	g_array_set_size (tlist->priv->job_list, 0);

	for (i=0; i< ptrarray->len; i++) {
		job = (guint) g_ptr_array_index (ptrarray, i);
		pk_debug ("job[%i]=%i", i, job);
		g_array_append_val (tlist->priv->job_list, job);
	}
	return TRUE;
}

/**
 * pk_task_list_print:
 **/
gboolean
pk_task_list_print (PkTaskList *tlist)
{
	guint i;
	guint job;
	guint length;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	length = tlist->priv->job_list->len;
	if (length == 0) {
		g_print ("no jobs...\n");
		return TRUE;
	}
	g_print ("jobs: ");
	for (i=0; i<length; i++) {
		job = g_array_index (tlist->priv->job_list, guint, i);
		g_print ("%i, ", job);
	}
	g_print ("\n");
	return TRUE;
}

/**
 * pk_task_list_refresh:
 *
 * Not normally required, but force a refresh
 **/
gboolean
pk_task_list_refresh (PkTaskList *tlist)
{
	gboolean ret;
	GError *error;
	GPtrArray *ptrarray = NULL;
	GType g_type_ptrarray;

	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);

	error = NULL;
	g_type_ptrarray = dbus_g_type_get_collection ("GPtrArray", G_TYPE_UINT);
	ret = dbus_g_proxy_call (tlist->priv->proxy, "GetJobList", &error,
				 G_TYPE_INVALID,
				 g_type_ptrarray, &ptrarray,
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
	pk_task_list_refresh_array_with_data (tlist, ptrarray);
	g_ptr_array_free (ptrarray, TRUE);
	return TRUE;
}

/**
 * pk_task_list_get_job_list:
 *
 * DO NOT FREE THIS.
 **/
GArray *
pk_task_list_get_job_list (PkTaskList *tlist)
{
	g_return_val_if_fail (tlist != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_LIST (tlist), FALSE);
	return tlist->priv->job_list;
}

/**
 * pk_task_list_job_list_changed_cb:
 */
static void
pk_task_list_job_list_changed_cb (DBusGProxy *proxy,
				  GPtrArray  *job_list,
				  PkTaskList *tlist)
{
	g_return_if_fail (tlist != NULL);
	g_return_if_fail (PK_IS_TASK_LIST (tlist));

	pk_task_list_refresh_array_with_data (tlist, job_list);
	pk_debug ("emit job-list-changed");
	g_signal_emit (tlist , signals [PK_TASK_LIST_JOB_LIST_CHANGED], 0);
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
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

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
	GType struct_array_type;

	tlist->priv = PK_TASK_LIST_GET_PRIVATE (tlist);

	/* check dbus connections, exit if not valid */
	tlist->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* we maintain a local copy */
	tlist->priv->job_list = g_array_new (FALSE, FALSE, sizeof (guint));

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (tlist->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	tlist->priv->proxy = proxy;

	struct_array_type = dbus_g_type_get_collection ("GArray", G_TYPE_UINT);

	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOXED,
					   G_TYPE_NONE, struct_array_type, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "JobListChanged",
				 struct_array_type, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "JobListChanged",
				     G_CALLBACK(pk_task_list_job_list_changed_cb), tlist, NULL);

	/* force a refresh so we have valid data*/
	pk_task_list_get_job_list (tlist);
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
	g_array_free (tlist->priv->job_list, TRUE);

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

