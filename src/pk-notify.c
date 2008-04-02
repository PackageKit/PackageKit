/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <dbus/dbus-glib.h>

#include "pk-debug.h"
#include "pk-notify.h"
#include "pk-marshal.h"

#define PK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NOTIFY, PkNotifyPrivate))

struct PkNotifyPrivate
{
	gboolean		 dummy;
	gchar			*tid;
	guint			 timeout_id;
};

enum {
	PK_NOTIFY_REPO_LIST_CHANGED,
	PK_NOTIFY_RESTART_SCHEDULE,
	PK_NOTIFY_UPDATES_CHANGED,
	PK_NOTIFY_LAST_SIGNAL
};

static gpointer pk_notify_object = NULL;
static guint signals [PK_NOTIFY_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkNotify, pk_notify, G_TYPE_OBJECT)

/**
 * pk_notify_restart_schedule:
 **/
gboolean
pk_notify_restart_schedule (PkNotify *notify)
{
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);

	pk_debug ("emitting restart-schedule");
	g_signal_emit (notify, signals [PK_NOTIFY_RESTART_SCHEDULE], 0);
	return TRUE;
}

/**
 * pk_notify_repo_list_changed:
 **/
gboolean
pk_notify_repo_list_changed (PkNotify *notify, const gchar *tid)
{
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	pk_debug ("emitting repo-list-changed tid:%s", tid);
	g_signal_emit (notify, signals [PK_NOTIFY_REPO_LIST_CHANGED], 0, tid);
	return TRUE;
}

/**
 * pk_notify_updates_changed:
 **/
gboolean
pk_notify_updates_changed (PkNotify *notify, const gchar *tid)
{
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	pk_debug ("emitting updates-changed tid:%s", tid);
	g_signal_emit (notify, signals [PK_NOTIFY_UPDATES_CHANGED], 0, tid);
	return TRUE;
}

/**
 * pk_notify_finished_updates_changed_cb:
 **/
static gboolean
pk_notify_finished_updates_changed_cb (gpointer data)
{
	PkNotify *notify = PK_NOTIFY (data);
	pk_notify_updates_changed (notify, notify->priv->tid);
	notify->priv->timeout_id = 0;
	return FALSE;
}


/**
 * pk_notify_wait_updates_changed:
 **/
gboolean
pk_notify_wait_updates_changed (PkNotify *notify, const gchar *tid, guint timeout)
{
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);
	g_return_val_if_fail (tid != NULL, FALSE);

	/* check if we did this more than once */
	if (notify->priv->timeout_id != 0) {
		pk_warning ("already scheduled");
		return FALSE;
	}

	/* save */
	g_free (notify->priv->tid);
	notify->priv->tid = g_strdup (tid);

	/* schedule */
	notify->priv->timeout_id = g_timeout_add (timeout, pk_notify_finished_updates_changed_cb, notify);
	return TRUE;
}

/**
 * pk_notify_finalize:
 **/
static void
pk_notify_finalize (GObject *object)
{
	PkNotify *notify;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NOTIFY (object));
	notify = PK_NOTIFY (object);

	/* cancel the delayed signals */
	if (notify->priv->timeout_id != 0) {
		g_source_remove (notify->priv->timeout_id);
	}

	g_free (notify->priv->tid);
	G_OBJECT_CLASS (pk_notify_parent_class)->finalize (object);
}

/**
 * pk_notify_class_init:
 **/
static void
pk_notify_class_init (PkNotifyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_notify_finalize;

	signals [PK_NOTIFY_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_NOTIFY_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals [PK_NOTIFY_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (PkNotifyPrivate));
}

/**
 * pk_notify_init:
 *
 * initializes the notify class. NOTE: We expect notify objects
 * to *NOT* be removed or added during the session.
 * We only control the first notify object if there are more than one.
 **/
static void
pk_notify_init (PkNotify *notify)
{
	notify->priv = PK_NOTIFY_GET_PRIVATE (notify);
	notify->priv->tid = NULL;
	notify->priv->timeout_id = 0;
}

/**
 * pk_notify_new:
 * Return value: A new notify class instance.
 **/
PkNotify *
pk_notify_new (void)
{
	if (pk_notify_object != NULL) {
		g_object_ref (pk_notify_object);
	} else {
		pk_notify_object = g_object_new (PK_TYPE_NOTIFY, NULL);
		g_object_add_weak_pointer (pk_notify_object, &pk_notify_object);
	}
	return PK_NOTIFY (pk_notify_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_notify (LibSelfTest *test)
{
	PkNotify *notify;

	if (libst_start (test, "PkNotify", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an instance");
	notify = pk_notify_new ();
	if (notify != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (notify);

	libst_end (test);
}
#endif

