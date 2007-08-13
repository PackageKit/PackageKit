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
#include "pk-job-list.h"
#include "pk-task-client.h"
#include "pk-task-common.h"
#include "pk-task-list.h"

static void     pk_notify_class_init	(PkNotifyClass *klass);
static void     pk_notify_init		(PkNotify      *notify);
static void     pk_notify_finalize	(GObject       *object);

#define PK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NOTIFY, PkNotifyPrivate))

struct PkNotifyPrivate
{
	GtkStatusIcon		*status_icon;
	PkTaskList		*tlist;
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
 * pk_notify_set_icon:
 **/
static gboolean
pk_notify_set_icon (PkNotify *notify, const gchar *icon)
{
	if (icon == NULL) {
		gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), FALSE);
		return FALSE;
	}
	gtk_status_icon_set_from_icon_name (GTK_STATUS_ICON (notify->priv->status_icon), icon);
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), TRUE);
	return TRUE;
}

/**
 * pk_notify_refresh_state:
 **/
static gboolean
pk_notify_refresh_state (PkNotify *notify)
{
	g_warning ("rescan");
	guint i;
	PkTaskListItem *item;
	PkTaskStatus state;
	guint length;
	GPtrArray *array;
	gboolean state_install = FALSE;
	gboolean state_remove = FALSE;
	gboolean state_setup = FALSE;
	gboolean state_update = FALSE;
	gboolean state_download = FALSE;
	gboolean state_query = FALSE;
	const gchar *icon = NULL;

	array = pk_task_list_get_latest	(notify->priv->tlist);

	length = array->len;
	if (length == 0) {
		pk_debug ("no activity, so hide the icon");
		gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), FALSE);
		return TRUE;
	}
	for (i=0; i<length; i++) {
		item = g_ptr_array_index (array, i);
		state = item->status;
		pk_debug ("%i %s", item->job, pk_task_status_to_text (state));
		if (state == PK_TASK_STATUS_SETUP) {
			state_setup = TRUE;
		} else if (state == PK_TASK_STATUS_QUERY) {
			state_query = TRUE;
		} else if (state == PK_TASK_STATUS_REMOVE) {
			state_remove = TRUE;
		} else if (state == PK_TASK_STATUS_DOWNLOAD) {
			state_download = TRUE;
		} else if (state == PK_TASK_STATUS_INSTALL) {
			state_install = TRUE;
		} else if (state == PK_TASK_STATUS_UPDATE) {
			state_update = TRUE;
		}
	}
	/* in order of priority */
	if (state_install == TRUE) {
		icon = "view-refresh";
	} else if (state_remove == TRUE) {
		icon = "edit-clear";
	} else if (state_setup == TRUE) {
		icon = "emblem-system";
	} else if (state_update == TRUE) {
		icon = "system-software-update";
	} else if (state_download == TRUE) {
		icon = "mail-send-receive";
	} else if (state_query == TRUE) {
		icon = "system-search";
	}
	pk_notify_set_icon (notify, icon);

	return TRUE;
}

/**
 * pk_notify_task_list_changed_cb:
 **/
static void
pk_notify_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	PkNotify *notify = (PkNotify *) data;
	pk_notify_refresh_state (notify);
}

/**
 * pk_notify_init:
 * @notify: This class instance
 **/
static void
pk_notify_init (PkNotify *notify)
{
	notify->priv = PK_NOTIFY_GET_PRIVATE (notify);

	notify->priv->status_icon = gtk_status_icon_new ();

	notify->priv->tlist = pk_task_list_new ();
	g_signal_connect (notify->priv->tlist, "task-list-changed",
			  G_CALLBACK (pk_notify_task_list_changed_cb), notify);
	pk_notify_refresh_state (notify);

//	tclient = pk_task_client_new ();
//	g_signal_connect (tclient, "package",
//			  G_CALLBACK (pk_notify_package_cb), notify);
//	g_signal_connect (tclient, "percentage-changed",
//			  G_CALLBACK (pk_notify_percentage_changed_cb), notify);
//	g_object_unref (tclient);

	gtk_status_icon_set_tooltip (GTK_STATUS_ICON (notify->priv->status_icon), "Updates available:\ngnome-power-manager: GNOME Power Manager\nevince: Document viewer");
	gtk_status_icon_set_visible (GTK_STATUS_ICON (notify->priv->status_icon), FALSE);
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
	g_object_unref (notify->priv->tlist);

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

