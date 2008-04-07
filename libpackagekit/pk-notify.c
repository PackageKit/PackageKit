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

/**
 * SECTION:pk-notify
 * @short_description: GObject class for PackageKit notifications
 *
 * A nice GObject to use for detecting system wide changes in PackageKit
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
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include "pk-enum.h"
#include "pk-notify.h"
#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-common.h"

static void     pk_notify_class_init	(PkNotifyClass *klass);
static void     pk_notify_init		(PkNotify      *notify);
static void     pk_notify_finalize	(GObject       *object);

#define PK_NOTIFY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NOTIFY, PkNotifyPrivate))

/**
 * PkNotifyPrivate:
 *
 * Private #PkNotify data
 **/
struct _PkNotifyPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
};

typedef enum {
	PK_NOTIFY_RESTART_SCHEDULE,
	PK_NOTIFY_UPDATES_CHANGED,
	PK_NOTIFY_REPO_LIST_CHANGED,
	PK_NOTIFY_LAST_SIGNAL
} PkSignals;

static guint signals [PK_NOTIFY_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkNotify, pk_notify, G_TYPE_OBJECT)

/**
 * pk_notify_restart_schedule_cb:
 */
static void
pk_notify_restart_schedule_cb (DBusGProxy *proxy, PkNotify *notify)
{
	g_return_if_fail (PK_IS_NOTIFY (notify));

	pk_debug ("emitting restart-schedule");
	g_signal_emit (notify, signals [PK_NOTIFY_RESTART_SCHEDULE], 0);

}

/**
 * pk_notify_updates_changed_cb:
 */
static void
pk_notify_updates_changed_cb (DBusGProxy *proxy, const gchar *tid, PkNotify *notify)
{
	g_return_if_fail (PK_IS_NOTIFY (notify));

	pk_debug ("emitting updates-changed");
	g_signal_emit (notify, signals [PK_NOTIFY_UPDATES_CHANGED], 0);

}

/**
 * pk_notify_repo_list_changed_cb:
 */
static void
pk_notify_repo_list_changed_cb (DBusGProxy *proxy, const gchar *tid, PkNotify *notify)
{
	g_return_if_fail (PK_IS_NOTIFY (notify));

	pk_debug ("emitting repo-list-changed");
	g_signal_emit (notify, signals [PK_NOTIFY_REPO_LIST_CHANGED], 0);

}

/**
 * pk_notify_class_init:
 **/
static void
pk_notify_class_init (PkNotifyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_notify_finalize;

	/**
	 * PkNotify::updates-changed:
	 * @notify: the #PkNotify instance that emitted the signal
	 *
	 * The ::updates-changed signal is emitted when the update list may have
	 * changed and the notify program may have to update some UI.
	 **/
	signals [PK_NOTIFY_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkNotifyClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkNotify::repo-list-changed:
	 * @notify: the #PkNotify instance that emitted the signal
	 *
	 * The ::repo-list-changed signal is emitted when the repo list may have
	 * changed and the notify program may have to update some UI.
	 **/
	signals [PK_NOTIFY_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkNotifyClass, repo_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkNotify::restart_schedule:
	 * @notify: the #PkNotify instance that emitted the signal
	 *
	 * The ::restart_schedule signal is emitted when the service has been
	 * restarted. Client programs should reload themselves.
	 **/
	signals [PK_NOTIFY_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkNotifyClass, restart_schedule),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkNotifyPrivate));
}

/**
 * pk_notify_init:
 **/
static void
pk_notify_init (PkNotify *notify)
{
	GError *error = NULL;
	notify->priv = PK_NOTIFY_GET_PRIVATE (notify);

	/* check dbus connections, exit if not valid */
	notify->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("Could not connect to system DBUS.");
	}

	/* get a connection */
	notify->priv->proxy = dbus_g_proxy_new_for_name (notify->priv->connection,
							 PK_DBUS_SERVICE, PK_DBUS_PATH_NOTIFY,
							 PK_DBUS_INTERFACE_NOTIFY);
	if (notify->priv->proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}

	dbus_g_proxy_add_signal (notify->priv->proxy, "UpdatesChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (notify->priv->proxy, "UpdatesChanged",
				     G_CALLBACK (pk_notify_updates_changed_cb), notify, NULL);

	dbus_g_proxy_add_signal (notify->priv->proxy, "RepoListChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (notify->priv->proxy, "RepoListChanged",
				     G_CALLBACK (pk_notify_repo_list_changed_cb), notify, NULL);

	dbus_g_proxy_add_signal (notify->priv->proxy, "RestartSchedule", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (notify->priv->proxy, "RestartSchedule",
				     G_CALLBACK (pk_notify_restart_schedule_cb), notify, NULL);
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
	g_return_if_fail (notify->priv != NULL);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (notify->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_notify_updates_changed_cb), notify);
	dbus_g_proxy_disconnect_signal (notify->priv->proxy, "RepoListChanged",
				        G_CALLBACK (pk_notify_repo_list_changed_cb), notify);
	dbus_g_proxy_disconnect_signal (notify->priv->proxy, "RestartSchedule",
				        G_CALLBACK (pk_notify_restart_schedule_cb), notify);

	/* free the proxy */
	g_object_unref (G_OBJECT (notify->priv->proxy));

	G_OBJECT_CLASS (pk_notify_parent_class)->finalize (object);
}

/**
 * pk_notify_new:
 *
 * PkNotify is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new %PkNotify instance
 **/
PkNotify *
pk_notify_new (void)
{
	PkNotify *notify;
	notify = g_object_new (PK_TYPE_NOTIFY, NULL);
	return PK_NOTIFY (notify);
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
	libst_title (test, "get notify");
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

