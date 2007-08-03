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
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <glib/gi18n.h>

#include <gtk/gtk.h>
#include <gtk/gtkstatusicon.h>

#include "pk-debug.h"
#include "pk-notify.h"
#include "pk-task-client.h"

static void     pk_notify_class_init	(PkNotifyClass *klass);
static void     pk_notify_init		(PkNotify      *notify);
static void     pk_notify_finalize	(GObject       *object);

#define PK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NOTIFY, PkNotifyPrivate))

struct PkNotifyPrivate
{
	GtkStatusIcon		*status_icon;
};

G_DEFINE_TYPE (PkNotify, pk_notify, G_TYPE_OBJECT)

/**
 * pk_notify_get_updates:
 **/
gboolean
pk_notify_get_updates (PkNotify *notify, guint *job, GError **error)
{
	g_return_val_if_fail (notify != NULL, FALSE);
	g_return_val_if_fail (PK_IS_NOTIFY (notify), FALSE);
	return TRUE;
}

/**
 * pk_notify_class_init:
 * @klass: The PkNotifyClass
 **/
static void
pk_notify_class_init (PkNotifyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_notify_finalize;

	g_type_class_add_private (klass, sizeof (PkNotifyPrivate));
}

/**
 * pk_notify_init:
 * @notify: This class instance
 **/
static void
pk_notify_init (PkNotify *notify)
{
	PkTaskClient *tclient;

	notify->priv = PK_NOTIFY_GET_PRIVATE (notify);

	notify->priv->status_icon = gtk_status_icon_new ();

	tclient = pk_task_client_new ();
//	g_signal_connect (tclient, "package",
//			  G_CALLBACK (pk_notify_package_cb), notify);
//	g_signal_connect (tclient, "percentage-changed",
//			  G_CALLBACK (pk_notify_percentage_changed_cb), notify);
	g_object_unref (tclient);

	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (notify->priv->status_icon), "Updates available:\ngnome-power-manager: GNOME Power Manager\nevince: Document viewer");
	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (notify->priv->status_icon), "software-update-available");
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), TRUE);
}

/**
 * pk_notify_finalize:
 * @object: The object to finalize
 **/
static void
pk_notify_finalize (GObject *object)
{
	PkNotify *notify;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NOTIFY (object));

	notify = PK_NOTIFY (object);

	g_return_if_fail (notify->priv != NULL);
	g_object_unref (notify->priv->status_icon);

	G_OBJECT_CLASS (pk_notify_parent_class)->finalize (object);
}

/**
 * pk_notify_new:
 *
 * Return value: a new PkNotify object.
 **/
PkNotify *
pk_notify_new (void)
{
	PkNotify *notify;
	notify = g_object_new (PK_TYPE_NOTIFY, NULL);
	return PK_NOTIFY (notify);
}

