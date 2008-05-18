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

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include "pk-debug.h"
#include "pk-control.h"
#include "pk-marshal.h"
#include "pk-connection.h"
#include "pk-common.h"
#include "pk-enum.h"

static void     pk_control_class_init	(PkControlClass *klass);
static void     pk_control_init		(PkControl      *control);
static void     pk_control_finalize	(GObject     *object);

#define PK_CONTROL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONTROL, PkControlPrivate))

/**
 * PkControlPrivate:
 *
 * Private #PkControl data
 **/
struct _PkControlPrivate
{
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	PkConnection		*pconnection;
	gchar			**array;
};

enum {
	PK_CONTROL_LOCKED,
	PK_CONTROL_LIST_CHANGED,
	PK_CONTROL_RESTART_SCHEDULE,
	PK_CONTROL_UPDATES_CHANGED,
	PK_CONTROL_REPO_LIST_CHANGED,
	PK_CONTROL_NETWORK_STATE_CHANGED,
	PK_CONTROL_LAST_SIGNAL
};

static guint signals [PK_CONTROL_LAST_SIGNAL] = { 0 };
static gpointer pk_control_object = NULL;

G_DEFINE_TYPE (PkControl, pk_control, G_TYPE_OBJECT)

/**
 * pk_control_error_quark:
 *
 * We are a clever GObject that sets errors
 *
 * Return value: Our personal error quark.
 **/
GQuark
pk_control_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("pk_control_error");
	}
	return quark;
}

/**
 * pk_control_error_set:
 *
 * Sets the correct error code (if allowed) and print to the screen
 * as a warning.
 **/
static gboolean
pk_control_error_set (GError **error, gint code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;
	gboolean ret = TRUE;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* dumb */
	if (error == NULL) {
		pk_warning ("No error set, so can't set: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* already set */
	if (*error != NULL) {
		pk_warning ("not NULL error!");
		g_clear_error (error);
	}

	/* propogate */
	g_set_error (error, PK_CONTROL_ERROR, code, "%s", buffer);

out:
	g_free(buffer);
	return ret;
}

/**
 * pk_control_error_fixup:
 * @error: a %GError
 **/
static gboolean
pk_control_error_fixup (GError **error)
{
	if (error != NULL && *error != NULL) {
		/* get some proper debugging */
		if ((*error)->domain == DBUS_GERROR &&
		    (*error)->code == DBUS_GERROR_REMOTE_EXCEPTION) {
			/* use one of our local codes */
			pk_debug ("fixing up code from %i", (*error)->code);
			(*error)->code = PK_CONTROL_ERROR_FAILED;
		}
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_control_get_actions:
 * @control: a valid #PkControl instance
 *
 * Actions are roles that the daemon can do with the current backend
 *
 * Return value: an enumerated list of the actions the backend supports
 **/
PkRoleEnum
pk_control_get_actions (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;
	gchar *actions;
	PkRoleEnum roles_enum = PK_GROUP_ENUM_UNKNOWN;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_GROUP_ENUM_UNKNOWN);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetActions", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &actions,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetActions failed :%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to enumerated types */
	roles_enum = pk_role_enums_from_text (actions);
	g_free (actions);
out:
	return roles_enum;
}

/**
 * pk_control_set_proxy:
 * @control: a valid #PkControl instance
 * @proxy_http: a HTTP proxy string such as "username:password@server.lan:8080"
 * @proxy_ftp: a FTP proxy string such as "server.lan:8080"
 *
 * Set a proxy on the PK daemon
 *
 * Return value: if we set the proxy successfully
 **/
gboolean
pk_control_set_proxy (PkControl *control, const gchar *proxy_http, const gchar *proxy_ftp)
{
	gboolean ret = FALSE;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_GROUP_ENUM_UNKNOWN);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "SetProxy", &error,
				 G_TYPE_STRING, proxy_http,
				 G_TYPE_STRING, proxy_ftp,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("SetProxy failed :%s", error->message);
		g_error_free (error);
	}
out:
	return ret;
}

/**
 * pk_control_get_groups:
 * @control: a valid #PkControl instance
 *
 * The group list is enumerated so it can be localised and have deep
 * integration with desktops.
 * This method allows a frontend to only display the groups that are supported.
 *
 * Return value: an enumerated list of the groups the backend supports
 **/
PkGroupEnum
pk_control_get_groups (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;
	gchar *groups;
	PkGroupEnum groups_enum = PK_GROUP_ENUM_UNKNOWN;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_GROUP_ENUM_UNKNOWN);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetGroups", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &groups,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetGroups failed :%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to enumerated types */
	groups_enum = pk_group_enums_from_text (groups);
	g_free (groups);
out:
	return groups_enum;
}

/**
 * pk_control_get_network_state:
 * @control: a valid #PkControl instance
 *
 * Return value: an enumerated network state
 **/
PkNetworkEnum
pk_control_get_network_state (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;
	gchar *network_state;
	PkGroupEnum network_state_enum = PK_NETWORK_ENUM_UNKNOWN;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_NETWORK_ENUM_UNKNOWN);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetNetworkState", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &network_state,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetNetworkState failed :%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to enumerated types */
	network_state_enum = pk_network_enum_from_text (network_state);
	g_free (network_state);
out:
	return network_state_enum;
}

/**
 * pk_control_get_filters:
 * @control: a valid #PkControl instance
 *
 * Filters are how the backend can specify what type of package is returned.
 *
 * Return value: an enumerated list of the filters the backend supports
 **/
PkFilterEnum
pk_control_get_filters (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;
	gchar *filters;
	PkFilterEnum filters_enum = PK_FILTER_ENUM_UNKNOWN;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_FILTER_ENUM_UNKNOWN);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetFilters", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &filters,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetFilters failed :%s", error->message);
		g_error_free (error);
		goto out;
	}

	/* convert to enumerated types */
	filters_enum = pk_filter_enums_from_text (filters);
	g_free (filters);
out:
	return filters_enum;
}

/**
 * pk_control_get_backend_detail:
 * @control: a valid #PkControl instance
 * @name: the name of the backend
 * @author: the author of the backend
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The backend detail is useful for the pk-backend-status program, or for
 * automatic bugreports.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_control_get_backend_detail (PkControl *control, gchar **name, gchar **author, GError **error)
{
	gboolean ret;
	gchar *tname;
	gchar *tauthor;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for manager");
		return FALSE;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetBackendDetail", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tname,
				 G_TYPE_STRING, &tauthor,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		pk_control_error_fixup (error);
		return FALSE;
	}

	/* copy needed bits */
	if (name != NULL) {
		*name = tname;
	} else {
		g_free (tauthor);
	}
	/* copy needed bits */
	if (author != NULL) {
		*author = tauthor;
	} else {
		g_free (tauthor);
	}
	return ret;
}

/**
 * pk_control_get_time_since_action:
 * @control: a valid #PkControl instance
 * @role: the role we are querying
 * @seconds: the number of seconds since the request was completed
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We may want to know how long it has been since we refreshed the cache or
 * retrieved the update list.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_control_get_time_since_action (PkControl *control, PkRoleEnum role, guint *seconds, GError **error)
{
	gboolean ret;
	const gchar *role_text;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);

	role_text = pk_role_enum_to_text (role);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for manager");
		return FALSE;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetTimeSinceAction", error,
				 G_TYPE_STRING, role_text,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, seconds,
				 G_TYPE_INVALID);
	pk_control_error_fixup (error);
	return ret;
}

/**
 * pk_control_allocate_transaction_id:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We have to create a transaction ID then use it, as a one-step constructor
 * is inherently racey.
 *
 * Return value: %TRUE if we allocated a TID.
 **/
gboolean
pk_control_allocate_transaction_id (PkControl *control, gchar **tid, GError **error)
{
	gboolean ret;
	gchar *tid_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for GetTid");
		return FALSE;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetTid", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tid_local,
				 G_TYPE_INVALID);
	if (!ret) {
		pk_control_error_fixup (error);
		goto out;
	}

	/* check we are not running new client tools with an old server */
	if (g_strrstr (tid_local, ";") != NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "Incorrect path with ';' returned!");
		ret = FALSE;
		goto out;
	}

	/* copy */
	*tid = g_strdup (tid_local);
	pk_debug ("Got tid: '%s'", tid_local);
out:
	g_free (tid_local);
	return ret;
}

/**
 * pk_control_transaction_list_print:
 **/
gboolean
pk_control_transaction_list_print (PkControl *control)
{
	guint i;
	gchar *tid;
	guint length;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);

	length = g_strv_length (control->priv->array);
	if (length == 0) {
		g_print ("no jobs...\n");
		return TRUE;
	}
	g_print ("jobs: ");
	for (i=0; i<length; i++) {
		tid = control->priv->array[i];
		g_print ("%s\n", tid);
	}
	return TRUE;
}

/**
 * pk_control_transaction_list_refresh:
 *
 * Not normally required, but force a refresh
 **/
static gboolean
pk_control_transaction_list_refresh (PkControl *control)
{
	gboolean ret;
	GError *error;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);

	/* clear old data */
	if (control->priv->array != NULL) {
		g_strfreev (control->priv->array);
		control->priv->array = NULL;
	}
	error = NULL;
	ret = dbus_g_proxy_call (control->priv->proxy, "GetTransactionList", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRV, &control->priv->array,
				 G_TYPE_INVALID);
	if (error != NULL) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetTransactionList failed!");
		control->priv->array = NULL;
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_control_transaction_list_get:
 **/
const gchar **
pk_control_transaction_list_get (PkControl *control)
{
	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	return (const gchar **) control->priv->array;
}

/**
 * pk_control_transaction_list_changed_cb:
 */
static void
pk_control_transaction_list_changed_cb (DBusGProxy *proxy, gchar **array, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	/* clear old data */
	if (control->priv->array != NULL) {
		g_strfreev (control->priv->array);
	}
	control->priv->array = g_strdupv (array);
	pk_debug ("emit transaction-list-changed");
	g_signal_emit (control , signals [PK_CONTROL_LIST_CHANGED], 0);
}

/**
 * pk_control_connection_changed_cb:
 **/
static void
pk_control_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkControl *control)
{
	pk_debug ("connected=%i", connected);
	/* force a refresh so we have valid data*/
	if (connected) {
		pk_control_transaction_list_refresh (control);
	}
}

/**
 * pk_control_restart_schedule_cb:
 */
static void
pk_control_restart_schedule_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	pk_debug ("emitting restart-schedule");
	g_signal_emit (control, signals [PK_CONTROL_RESTART_SCHEDULE], 0);

}

/**
 * pk_control_updates_changed_cb:
 */
static void
pk_control_updates_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	pk_debug ("emitting updates-changed");
	g_signal_emit (control, signals [PK_CONTROL_UPDATES_CHANGED], 0);

}

/**
 * pk_control_repo_list_changed_cb:
 */
static void
pk_control_repo_list_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	pk_debug ("emitting repo-list-changed");
	g_signal_emit (control, signals [PK_CONTROL_REPO_LIST_CHANGED], 0);
}

/**
 * pk_control_network_state_changed_cb:
 */
static void
pk_control_network_state_changed_cb (DBusGProxy *proxy, const gchar *network_text, PkControl *control)
{
	PkNetworkEnum network;
	g_return_if_fail (PK_IS_CONTROL (control));

	network = pk_network_enum_from_text (network_text);
	pk_debug ("emitting network-state-changed: %s", network_text);
	g_signal_emit (control, signals [PK_CONTROL_NETWORK_STATE_CHANGED], 0, network);
}

/**
 * pk_control_locked_cb:
 */
static void
pk_control_locked_cb (DBusGProxy *proxy, gboolean is_locked, PkControl *control)
{
	pk_debug ("emit locked %i", is_locked);
	g_signal_emit (control , signals [PK_CONTROL_LOCKED], 0, is_locked);
}

/**
 * pk_control_class_init:
 * @klass: The PkControlClass
 **/
static void
pk_control_class_init (PkControlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_control_finalize;

	/**
	 * PkControl::updates-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::updates-changed signal is emitted when the update list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals [PK_CONTROL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::repo-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::repo-list-changed signal is emitted when the repo list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals [PK_CONTROL_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, repo_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::network-state-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::network-state-changed signal is emitted when the network has changed speed or
	 * connections state.
	 **/
	signals [PK_CONTROL_NETWORK_STATE_CHANGED] =
		g_signal_new ("network-state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, network_state_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	/**
	 * PkControl::restart_schedule:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::restart_schedule signal is emitted when the service has been
	 * restarted. Client programs should reload themselves.
	 **/
	signals [PK_CONTROL_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, restart_schedule),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::transaction-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::transaction-list-changed signal is emitted when TODO
	 **/
	signals [PK_CONTROL_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, transaction_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::locked:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::locked signal is emitted when TODO
	 **/
	signals [PK_CONTROL_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, locked),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkControlPrivate));
}

/**
 * pk_control_init:
 * @control: This class instance
 **/
static void
pk_control_init (PkControl *control)
{
	GError *error = NULL;

	control->priv = PK_CONTROL_GET_PRIVATE (control);
	/* check dbus connections, exit if not valid */
	control->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* we maintain a local copy */
	control->priv->array = NULL;

	/* watch for PackageKit on the bus, and try to connect up at start */
	control->priv->pconnection = pk_connection_new ();
	g_signal_connect (control->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_control_connection_changed_cb), control);

	/* get a connection to the engine object */
	control->priv->proxy = dbus_g_proxy_new_for_name (control->priv->connection,
							 PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL) {
		pk_error ("Cannot connect to PackageKit.");
	}

	dbus_g_proxy_add_signal (control->priv->proxy, "TransactionListChanged",
				 G_TYPE_STRV, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "TransactionListChanged",
				     G_CALLBACK(pk_control_transaction_list_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "UpdatesChanged", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "UpdatesChanged",
				     G_CALLBACK (pk_control_updates_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RepoListChanged", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RepoListChanged",
				     G_CALLBACK (pk_control_repo_list_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "NetworkStateChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "NetworkStateChanged",
				     G_CALLBACK (pk_control_network_state_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RestartSchedule", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RestartSchedule",
				     G_CALLBACK (pk_control_restart_schedule_cb), control, NULL);

	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (control->priv->proxy, "Locked", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "Locked",
				     G_CALLBACK (pk_control_locked_cb), control, NULL);

	/* force a refresh so we have valid data*/
	pk_control_transaction_list_refresh (control);
}

/**
 * pk_control_finalize:
 * @object: The object to finalize
 **/
static void
pk_control_finalize (GObject *object)
{
	PkControl *control;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CONTROL (object));
	control = PK_CONTROL (object);

	g_return_if_fail (control->priv != NULL);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "Locked",
				        G_CALLBACK (pk_control_locked_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "TransactionListChanged",
				        G_CALLBACK (pk_control_transaction_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_control_updates_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RepoListChanged",
				        G_CALLBACK (pk_control_repo_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "NetworkStateChanged",
				        G_CALLBACK (pk_control_network_state_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RestartSchedule",
				        G_CALLBACK (pk_control_restart_schedule_cb), control);

	/* free the proxy */
	g_object_unref (G_OBJECT (control->priv->proxy));

	G_OBJECT_CLASS (pk_control_parent_class)->finalize (object);
}

/**
 * pk_control_new:
 *
 * Return value: a new PkControl object.
 **/
PkControl *
pk_control_new (void)
{
	if (pk_control_object != NULL) {
		g_object_ref (pk_control_object);
	} else {
		pk_control_object = g_object_new (PK_TYPE_CONTROL, NULL);
		g_object_add_weak_pointer (pk_control_object, &pk_control_object);
	}
	return PK_CONTROL (pk_control_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_control (LibSelfTest *test)
{
	PkControl *control;

	if (libst_start (test, "PkControl", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get control");
	control = pk_control_new ();
	if (control != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}
	g_object_unref (control);

	libst_end (test);
}
#endif

