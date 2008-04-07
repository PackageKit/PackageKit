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

/**
 * SECTION:pk-job-list
 * @short_description: A nice way to keep a list of the jobs being processed
 *
 * These provide a good way to keep a list of the jobs being processed so we
 * can see what type of jobs and thier status easily.
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
#include "pk-common.h"
#include "pk-connection.h"
#include "pk-job-list.h"

static void     pk_job_list_class_init		(PkJobListClass *klass);
static void     pk_job_list_init		(PkJobList      *job_list);
static void     pk_job_list_finalize		(GObject        *object);

#define PK_JOB_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_JOB_LIST, PkJobListPrivate))

/**
 * PkJobListPrivate:
 *
 * Private #PkJobList data
 **/
struct _PkJobListPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	gchar			**array;
	PkConnection		*pconnection;
};

typedef enum {
	PK_JOB_LIST_CHANGED,
	PK_JOB_LIST_LAST_SIGNAL
} PkSignals;

static guint signals [PK_JOB_LIST_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkJobList, pk_job_list, G_TYPE_OBJECT)

/**
 * pk_job_list_print:
 **/
gboolean
pk_job_list_print (PkJobList *jlist)
{
	guint i;
	gchar *tid;
	guint length;

	g_return_val_if_fail (PK_IS_JOB_LIST (jlist), FALSE);

	length = g_strv_length (jlist->priv->array);
	if (length == 0) {
		g_print ("no jobs...\n");
		return TRUE;
	}
	g_print ("jobs: ");
	for (i=0; i<length; i++) {
		tid = jlist->priv->array[i];
		g_print ("%s\n", tid);
	}
	return TRUE;
}

/**
 * pk_job_list_refresh:
 *
 * Not normally required, but force a refresh
 **/
gboolean
pk_job_list_refresh (PkJobList *jlist)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (PK_IS_JOB_LIST (jlist), FALSE);

	/* clear old data */
	if (jlist->priv->array != NULL) {
		g_strfreev (jlist->priv->array);
		jlist->priv->array = NULL;
	}
	error = NULL;
	ret = dbus_g_proxy_call (jlist->priv->proxy, "GetTransactionList", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRV, &jlist->priv->array,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetTransactionList failed!");
		jlist->priv->array = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_job_list_get_latest:
 **/
const gchar **
pk_job_list_get_latest (PkJobList *jlist)
{
	g_return_val_if_fail (jlist != NULL, NULL);
	g_return_val_if_fail (PK_IS_JOB_LIST (jlist), NULL);
	return (const gchar **) jlist->priv->array;
}

/**
 * pk_job_list_changed_cb:
 */
static void
pk_job_list_changed_cb (DBusGProxy *proxy,
			gchar     **array,
			PkJobList  *jlist)
{
	g_return_if_fail (jlist != NULL);
	g_return_if_fail (PK_IS_JOB_LIST (jlist));

	/* clear old data */
	if (jlist->priv->array != NULL) {
		g_strfreev (jlist->priv->array);
	}
	jlist->priv->array = g_strdupv (array);
	pk_debug ("emit transaction-list-changed");
	g_signal_emit (jlist , signals [PK_JOB_LIST_CHANGED], 0);
}

/**
 * pk_job_list_class_init:
 **/
static void
pk_job_list_class_init (PkJobListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_job_list_finalize;

	signals [PK_JOB_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkJobListPrivate));
}

/**
 * pk_client_connect:
 **/
static void
pk_job_list_connect (PkJobList *jlist)
{
	pk_debug ("connect");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkJobList *jlist)
{
	pk_debug ("connected=%i", connected);
	/* force a refresh so we have valid data*/
	if (connected) {
		pk_job_list_refresh (jlist);
	}
}

/**
 * pk_job_list_init:
 **/
static void
pk_job_list_init (PkJobList *jlist)
{
	GError *error = NULL;
	DBusGProxy *proxy = NULL;

	jlist->priv = PK_JOB_LIST_GET_PRIVATE (jlist);
	jlist->priv->proxy = NULL;

	/* check dbus connections, exit if not valid */
	jlist->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("Could not connect to system DBUS.");
	}

	/* we maintain a local copy */
	jlist->priv->array = NULL;

	/* watch for PackageKit on the bus, and try to connect up at start */
	jlist->priv->pconnection = pk_connection_new ();
	g_signal_connect (jlist->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), jlist);
	if (pk_connection_valid (jlist->priv->pconnection)) {
		pk_job_list_connect (jlist);
	} else {
		pk_debug ("no PK instance on the bus yet");
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (jlist->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	jlist->priv->proxy = proxy;

	dbus_g_proxy_add_signal (proxy, "TransactionListChanged",
				 G_TYPE_STRV, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "TransactionListChanged",
				     G_CALLBACK(pk_job_list_changed_cb), jlist, NULL);

	/* force a refresh so we have valid data*/
	pk_job_list_refresh (jlist);
}

/**
 * pk_job_list_finalize:
 **/
static void
pk_job_list_finalize (GObject *object)
{
	PkJobList *jlist;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_JOB_LIST (object));
	jlist = PK_JOB_LIST (object);
	g_return_if_fail (jlist->priv != NULL);

	/* free the proxy */
	g_object_unref (G_OBJECT (jlist->priv->proxy));
	g_object_unref (jlist->priv->pconnection);
	if (jlist->priv->array != NULL) {
		g_strfreev (jlist->priv->array);
	}

	G_OBJECT_CLASS (pk_job_list_parent_class)->finalize (object);
}

/**
 * pk_job_list_new:
 **/
PkJobList *
pk_job_list_new (void)
{
	PkJobList *jlist;
	jlist = g_object_new (PK_TYPE_JOB_LIST, NULL);
	return PK_JOB_LIST (jlist);
}

