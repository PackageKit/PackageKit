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
#include <dbus/dbus-glib-lowlevel.h>

#include "pk-debug.h"
#include "pk-task.h"
#include "pk-task-common.h"

static void     pk_task_class_init	(PkTaskClass *klass);
static void     pk_task_init		(PkTask      *task);
static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

struct PkTaskPrivate
{
	guint			 progress_percentage;
};

static guint signals [PK_TASK_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTask, pk_task, G_TYPE_OBJECT)

/**
 * pk_task_get_updates:
 **/
gchar *
pk_task_get_actions (void)
{
	gchar *actions;
	actions = pk_task_action_build (PK_TASK_ACTION_INSTALL,
				        PK_TASK_ACTION_REMOVE,
				        PK_TASK_ACTION_UPDATE,
				        PK_TASK_ACTION_GET_UPDATES,
				        PK_TASK_ACTION_REFRESH_CACHE,
				        PK_TASK_ACTION_UPDATE_SYSTEM,
				        PK_TASK_ACTION_SEARCH_NAME,
				        PK_TASK_ACTION_SEARCH_DETAILS,
				        PK_TASK_ACTION_SEARCH_GROUP,
				        PK_TASK_ACTION_SEARCH_FILE,
				        PK_TASK_ACTION_GET_DEPS,
				        PK_TASK_ACTION_GET_DESCRIPTION,
				        0);
	return actions;
}

/**
 * pk_task_get_updates:
 **/
gboolean
pk_task_get_updates (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_package (task, 0, "powertop;1.8-1.fc8;i386;fedora",
			 "Power consumption monitor");
	pk_task_package (task, 1, "kernel-2.6.23-0.115.rc3.git1.fc8;i386;installed",
			 "The Linux kernel (the core of the Linux operating system)");
	pk_task_package (task, 1, "gtkhtml2;2.19.1-4.fc8;i386;fedora", "An HTML widget for GTK+ 2.0");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

/**
 * pk_task_refresh_cache:
 **/
gboolean
pk_task_refresh_cache (PkTask *task, gboolean force)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_change_job_status (task, PK_TASK_STATUS_REFRESH_CACHE);
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

static gboolean
pk_task_update_system_timeout (gpointer data)
{
	PkTask *task = (PkTask *) data;
	if (task->priv->progress_percentage == 100) {
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
		return FALSE;
	}
	task->priv->progress_percentage += 10;
	pk_task_change_percentage (task, task->priv->progress_percentage);
	return TRUE;
}


/**
 * pk_task_update_system:
 **/
gboolean
pk_task_update_system (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_change_job_status (task, PK_TASK_STATUS_UPDATE);
	task->priv->progress_percentage = 0;
	pk_task_require_restart (task, PK_TASK_RESTART_SYSTEM, NULL);
	g_timeout_add (1000, pk_task_update_system_timeout, task);

	return TRUE;
}

/**
 * pk_task_search_name_timeout:
 **/
gboolean
pk_task_search_name_timeout (gpointer data)
{
	PkTask *task = (PkTask *) data;
	pk_task_package (task, 1, "evince;0.9.3-5.fc8;i386;installed",
			 "PDF Document viewer");
	pk_task_package (task, 1, "tetex;3.0-41.fc8;i386;fedora",
			 "TeTeX is an implementation of TeX for Linux or UNIX systems.");
	pk_task_package (task, 0, "scribus;1.3.4-1.fc8;i386;fedora",
			 "Scribus is an desktop open source page layout program");
	pk_task_package (task, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return FALSE;
}

/**
 * pk_task_search_name:
 **/
gboolean
pk_task_search_name (PkTask *task, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	task->package = strdup (search);
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_no_percentage_updates (task);

	g_timeout_add (2000, pk_task_search_name_timeout, task);
	return TRUE;
}

/**
 * pk_task_search_details:
 **/
gboolean
pk_task_search_details (PkTask *task, const gchar *filter, const gchar *search)
{
	pk_task_package (task, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

/**
 * pk_task_search_group:
 **/
gboolean
pk_task_search_group (PkTask *task, const gchar *filter, const gchar *search)
{
	pk_task_package (task, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

/**
 * pk_task_search_file:
 **/
gboolean
pk_task_search_file (PkTask *task, const gchar *filter, const gchar *search)
{
	pk_task_package (task, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

/**
 * pk_task_get_deps:
 **/
gboolean
pk_task_get_deps (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	task->package = strdup (package);
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_package (task, 1, "glib2;2.14.0;i386;fedora",
			 "The GLib library");
	pk_task_package (task, 1, "gtk2;gtk2-2.11.6-6.fc8;i386;fedora",
			 "GTK+ Libraries for GIMP");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	return TRUE;
}

/**
 * pk_task_get_description:
 **/
gboolean
pk_task_get_description (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_description (task, "gnome-power-manager", PK_TASK_GROUP_PROGRAMMING,
			     "super long description. la la la", "http://live.gnome.org/GnomePowerManager");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	return TRUE;
}

/**
 * pk_task_remove_package:
 **/
gboolean
pk_task_remove_package (PkTask *task, const gchar *package, gboolean allow_deps)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	task->package = strdup (package);
	pk_task_change_job_status (task, PK_TASK_STATUS_REMOVE);
	pk_task_error_code (task, PK_TASK_ERROR_CODE_NO_NETWORK, "No network connection available");
	pk_task_finished (task, PK_TASK_EXIT_FAILED);

	return TRUE;
}

static gboolean
pk_task_install_timeout (gpointer data)
{
	PkTask *task = (PkTask *) data;
	if (task->priv->progress_percentage == 100) {
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
		return FALSE;
	}
	if (task->priv->progress_percentage == 50) {
		pk_task_change_job_status (task, PK_TASK_STATUS_INSTALL);
	}
	task->priv->progress_percentage += 10;
	pk_task_change_percentage (task, task->priv->progress_percentage);
	return TRUE;
}

/**
 * pk_task_install_package:
 **/
gboolean
pk_task_install_package (PkTask *task, const gchar *package)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	task->package = strdup (package);
	pk_task_change_job_status (task, PK_TASK_STATUS_DOWNLOAD);
	task->priv->progress_percentage = 0;
	g_timeout_add (1000, pk_task_install_timeout, task);
	return TRUE;
}

/**
 * pk_task_update_package:
 **/
gboolean
pk_task_update_package (PkTask *task, const gchar *package_id)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	task->package = strdup (package_id);
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_package (task, 1, package_id, "The same thing");
	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	return TRUE;
}

/**
 * pk_task_cancel_job_try:
 **/
gboolean
pk_task_cancel_job_try (PkTask *task)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	/* check to see if we have an action */
	if (task->assigned == FALSE) {
		pk_warning ("Not assigned");
		return FALSE;
	}
	/* try to cancel action */
	if (task->status != PK_TASK_STATUS_QUERY) {
		pk_warning ("cannot cancel as not query");
		return FALSE;
	}

	return TRUE;
}

/**
 * pk_task_class_init:
 **/
static void
pk_task_class_init (PkTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_task_finalize;
	pk_task_setup_signals (object_class, signals);
	g_type_class_add_private (klass, sizeof (PkTaskPrivate));
}

/**
 * pk_task_init:
 **/
static void
pk_task_init (PkTask *task)
{
	task->priv = PK_TASK_GET_PRIVATE (task);
	task->signals = signals;
	pk_task_clear (task);
}

/**
 * pk_task_finalize:
 **/
static void
pk_task_finalize (GObject *object)
{
	PkTask *task;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TASK (object));
	task = PK_TASK (object);
	g_return_if_fail (task->priv != NULL);
	g_free (task->package);
	G_OBJECT_CLASS (pk_task_parent_class)->finalize (object);
}

/**
 * pk_task_new:
 **/
PkTask *
pk_task_new (void)
{
	PkTask *task;
	task = g_object_new (PK_TYPE_TASK, NULL);
	return PK_TASK (task);
}

