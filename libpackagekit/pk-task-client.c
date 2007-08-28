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
#include "pk-polkit-client.h"

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
	gboolean		 use_buffer;
	guint			 job;
	GMainLoop		*loop;
	PkTaskStatus		 last_status;
	PkTaskMonitor		*tmonitor;
	PkConnection		*pconnection;
	PkPolkitClient		*polkit;
	PkTaskRestart		 require_restart;
	gboolean		 is_finished;
	GPtrArray		*package_items;
};

typedef enum {
	PK_TASK_CLIENT_JOB_STATUS_CHANGED,
	PK_TASK_CLIENT_PERCENTAGE_CHANGED,
	PK_TASK_CLIENT_SUB_PERCENTAGE_CHANGED,
	PK_TASK_CLIENT_NO_PERCENTAGE_UPDATES,
	PK_TASK_CLIENT_PACKAGE,
	PK_TASK_CLIENT_ERROR_CODE,
	PK_TASK_CLIENT_FINISHED,
	PK_TASK_CLIENT_LAST_SIGNAL
} PkSignals;

static guint signals [PK_TASK_CLIENT_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTaskClient, pk_task_client, G_TYPE_OBJECT)

/**
 * pk_task_client_set_use_buffer:
 **/
gboolean
pk_task_client_set_use_buffer (PkTaskClient *tclient, gboolean use_buffer)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	tclient->priv->use_buffer = use_buffer;
	return TRUE;
}

/**
 * pk_task_client_get_use_buffer:
 **/
gboolean
pk_task_client_get_use_buffer (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	return tclient->priv->use_buffer;
}

/**
 * pk_task_client_get_use_buffer:
 **/
PkTaskRestart
pk_task_client_get_require_restart (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	return tclient->priv->require_restart;
}

/**
 * pk_task_client_set_sync:
 **/
gboolean
pk_task_client_set_sync (PkTaskClient *tclient, gboolean is_sync)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	tclient->priv->is_sync = is_sync;
	tclient->priv->use_buffer = is_sync;
	return TRUE;
}

/**
 * pk_task_client_get_sync:
 **/
gboolean
pk_task_client_get_sync (PkTaskClient *tclient)
{
	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	return tclient->priv->is_sync;
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
 * pk_task_client_get_package_buffer:
 **/
GPtrArray *
pk_task_client_get_package_buffer (PkTaskClient *tclient)
{
	if (tclient->priv->use_buffer == FALSE) {
		return NULL;
	}
	return tclient->priv->package_items;
}

/**
 * pk_task_client_remove_package_items:
 **/
static void
pk_task_client_remove_package_items (PkTaskClient *tclient)
{
	PkTaskClientPackageItem *item;
	while (tclient->priv->package_items->len > 0) {
		item = g_ptr_array_index (tclient->priv->package_items, 0);
		g_free (item->package_id);
		g_free (item->summary);
		g_free (item);
		g_ptr_array_remove_index_fast (tclient->priv->package_items, 0);
	}
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
		pk_warning ("not exit status, reset might be invalid");
	}
	tclient->priv->assigned = FALSE;
	tclient->priv->is_sync = FALSE;
	tclient->priv->use_buffer = FALSE;
	tclient->priv->job = 0;
	tclient->priv->last_status = PK_TASK_STATUS_UNKNOWN;
	tclient->priv->is_finished = FALSE;
	pk_task_client_remove_package_items (tclient);
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
pk_warning("set job");
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_update_system_action:
 **/
gboolean
pk_task_client_update_system_action (PkTaskClient *tclient, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "UpdateSystem", error,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("UpdateSystem failed!");
		return FALSE;
	}
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

	/* hopefully do the operation first time */
	ret = pk_task_client_update_system_action (tclient, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (tclient->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_task_client_update_system_action (tclient, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	if (ret == TRUE) {
		pk_task_client_wait_if_sync (tclient);
	}

	return ret;
}

/**
 * pk_task_client_search_name:
 **/
gboolean
pk_task_client_search_name (PkTaskClient *tclient, const gchar *filter, const gchar *search)
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
	ret = dbus_g_proxy_call (tclient->priv->proxy, "SearchName", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (error) {
		const gchar *error_name;
		if (error->domain == DBUS_GERROR && 
			error->code == DBUS_GERROR_REMOTE_EXCEPTION) {
			error_name = dbus_g_error_get_name (error);
		}
		else {
			error_name = g_quark_to_string(error->domain);
		}
		pk_debug ("ERROR: %s: %s", error_name, error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("SearchName failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_search_details:
 **/
gboolean
pk_task_client_search_details (PkTaskClient *tclient, const gchar *filter, const gchar *search)
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
	ret = dbus_g_proxy_call (tclient->priv->proxy, "SearchDetails", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
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
		pk_warning ("SearchDetails failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_search_group:
 **/
gboolean
pk_task_client_search_group (PkTaskClient *tclient, const gchar *filter, const gchar *search)
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
	ret = dbus_g_proxy_call (tclient->priv->proxy, "SearchGroup", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
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
		pk_warning ("SearchGroup failed!");
		return FALSE;
	}
	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	pk_task_client_wait_if_sync (tclient);

	return TRUE;
}

/**
 * pk_task_client_search_file:
 **/
gboolean
pk_task_client_search_file (PkTaskClient *tclient, const gchar *filter, const gchar *search)
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
	ret = dbus_g_proxy_call (tclient->priv->proxy, "SearchFile", &error,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
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
		pk_warning ("SearchFile failed!");
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
 * pk_task_client_remove_package_action:
 **/
gboolean
pk_task_client_remove_package_action (PkTaskClient *tclient, const gchar *package,
				      gboolean allow_deps, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	*error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "RemovePackage", error,
				 G_TYPE_STRING, package,
				 G_TYPE_BOOLEAN, allow_deps,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &tclient->priv->job,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RemovePackage failed!");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_task_client_remove_package:
 **/
gboolean
pk_task_client_remove_package (PkTaskClient *tclient, const gchar *package, gboolean allow_deps)
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

	/* hopefully do the operation first time */
	ret = pk_task_client_remove_package_action (tclient, package, allow_deps, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (tclient->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_task_client_remove_package_action (tclient, package, allow_deps, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);
	if (ret == TRUE) {
		pk_task_client_wait_if_sync (tclient);
	}

	return ret;
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
 * pk_task_client_install_package_action:
 **/
gboolean
pk_task_client_install_package_action (PkTaskClient *tclient, const gchar *package, GError **error)
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
pk_task_client_install_package (PkTaskClient *tclient, const gchar *package_id)
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

	/* hopefully do the operation first time */
	ret = pk_task_client_install_package_action (tclient, package_id, &error);

	/* we were refused by policy then try to get auth */
	if (ret == FALSE) {
		if (pk_polkit_client_error_denied_by_policy (error) == TRUE) {
			/* retry the action if we succeeded */
			if (pk_polkit_client_gain_privilege_str (tclient->priv->polkit, error->message) == TRUE) {
				pk_debug ("gained priv");
				g_error_free (error);
				/* do it all over again */
				ret = pk_task_client_install_package_action (tclient, package_id, &error);
			}
		}
		if (error != NULL) {
			pk_debug ("ERROR: %s", error->message);
			g_error_free (error);
		}
	}

	pk_task_monitor_set_job (tclient->priv->tmonitor, tclient->priv->job);

	/* only wait if the command succeeded. False is usually due to PolicyKit auth failure */
	if (ret == TRUE) {
		pk_task_client_wait_if_sync (tclient);
	}

	return ret;
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
 * pk_task_client_get_actions:
 **/
gchar *
pk_task_client_get_actions (PkTaskClient *tclient)
{
	gboolean ret;
	GError *error;
	gchar *actions;

	g_return_val_if_fail (tclient != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK_CLIENT (tclient), FALSE);

	error = NULL;
	ret = dbus_g_proxy_call (tclient->priv->proxy, "GetActions", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &actions,
				 G_TYPE_INVALID);
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("GetActions failed :%s", error->message);
		g_error_free (error);
		return NULL;
	}
	return actions;
}

/**
 * pk_task_client_finished_cb:
 */
static void
pk_task_client_finished_cb (PkTaskMonitor *tmonitor,
			    PkTaskExit     exit,
			    guint          runtime,
			    PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit finished %i, %i", exit, runtime);
	tclient->priv->is_finished = TRUE;
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_FINISHED], 0, exit, runtime);

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
 * pk_task_client_sub_percentage_changed_cb:
 */
static void
pk_task_client_sub_percentage_changed_cb (PkTaskMonitor *tmonitor,
				          guint	         percentage,
				          PkTaskClient  *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	pk_debug ("emit sub-percentage-changed %i", percentage);
	g_signal_emit (tclient , signals [PK_TASK_CLIENT_SUB_PERCENTAGE_CHANGED], 0, percentage);
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
			   const gchar   *package_id,
			   const gchar   *summary,
			   PkTaskClient  *tclient)
{
	PkTaskClientPackageItem *item;

	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	/* if sync then just add results to an array */
	if (tclient->priv->use_buffer == TRUE) {
		pk_debug ("adding to cache array package %i, %s, %s", value, package_id, summary);
		item = g_new0 (PkTaskClientPackageItem, 1);
		item->value = value;
		item->package_id = g_strdup (package_id);
		item->summary = g_strdup (summary);
		g_ptr_array_add (tclient->priv->package_items, item);
	} else {
		pk_debug ("emit package %i, %s, %s", value, package_id, summary);
		g_signal_emit (tclient , signals [PK_TASK_CLIENT_PACKAGE], 0, value, package_id, summary);
	}
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
 * pk_task_client_require_restart_cb:
 */
static void
pk_task_client_require_restart_cb (PkTaskMonitor  *tmonitor,
				   PkTaskRestart   restart,
				   const gchar    *details,
				   PkTaskClient   *tclient)
{
	g_return_if_fail (tclient != NULL);
	g_return_if_fail (PK_IS_TASK_CLIENT (tclient));

	/* always use the 'worst' possible restart scenario */
	if (restart > tclient->priv->require_restart) {
		tclient->priv->require_restart = restart;
		pk_debug ("restart status now %s", pk_task_restart_to_text (restart));
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
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_TASK_CLIENT_SUB_PERCENTAGE_CHANGED] =
		g_signal_new ("sub-percentage-changed",
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
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

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
	tclient->priv->use_buffer = FALSE;
	tclient->priv->job = 0;
	tclient->priv->last_status = PK_TASK_STATUS_UNKNOWN;
	tclient->priv->require_restart = PK_TASK_RESTART_NONE;
	tclient->priv->is_finished = FALSE;
	tclient->priv->package_items = g_ptr_array_new ();

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
	g_signal_connect (tclient->priv->tmonitor, "sub-percentage-changed",
			  G_CALLBACK (pk_task_client_sub_percentage_changed_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "no-percentage-updates",
			  G_CALLBACK (pk_task_client_no_percentage_updates_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "job-status-changed",
			  G_CALLBACK (pk_task_client_job_status_changed_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "package",
			  G_CALLBACK (pk_task_client_package_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "error-code",
			  G_CALLBACK (pk_task_client_error_code_cb), tclient);
	g_signal_connect (tclient->priv->tmonitor, "require-restart",
			  G_CALLBACK (pk_task_client_require_restart_cb), tclient);

	/* use PolicyKit */
	tclient->priv->polkit = pk_polkit_client_new ();
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
	g_object_unref (tclient->priv->polkit);

	/* removed any cached packages */
	pk_task_client_remove_package_items (tclient);
	g_ptr_array_free (tclient->priv->package_items, TRUE);

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

