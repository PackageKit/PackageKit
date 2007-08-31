/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Grzegorz Dąbrowski <gdx@o2.pl>
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
#include "pk-spawn.h"
#include "pk-network.h"
#include "pk-package-id.h"

#include <sqlite3.h>
#include <libbox/libbox-db.h>
#include <libbox/libbox-db-utils.h>
#include <libbox/libbox-db-repos.h>

static void     pk_task_class_init	(PkTaskClass *klass);
static void     pk_task_init		(PkTask      *task);
static void     pk_task_finalize	(GObject     *object);

#define PK_TASK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TASK, PkTaskPrivate))

struct PkTaskPrivate
{
	guint			 progress_percentage;
	PkNetwork		*network;
};

static guint signals [PK_TASK_LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (PkTask, pk_task, G_TYPE_OBJECT)


static void
add_packages_from_list (PkTask *task, GList *list)
{
	PackageSearch *package = NULL;
	GList *li = NULL;
	gchar *pkg_string = NULL;

	for (li = list; li != NULL; li = li->next) {
		package = (PackageSearch*)li->data;
		pkg_string = pk_package_id_build(package->package, package->version, package->arch, "");

		pk_task_package (task, package->installed, pkg_string, package->description);

		g_free(pkg_string);
	}
}

/* TODO: rewrite and share this code */
static void
parse_filter(const gchar *filter,  gboolean *installed,  gboolean *available,  gboolean *devel,
        gboolean *nondevel, gboolean *gui, gboolean *text)
{
	gchar **sections = NULL;
	gint i = 0;

	*installed = TRUE;
	*available = TRUE;
	*devel = TRUE;
	*nondevel = TRUE;
	*gui = TRUE;
	*text = TRUE;

	sections = g_strsplit (filter, ";", 0);
	while (sections[i]) {
		if (strcmp(sections[i], "installed") == 0)
			*available = FALSE;
		if (strcmp(sections[i], "~installed") == 0)
			*installed = FALSE;
		if (strcmp(sections[i], "devel") == 0)
			*nondevel = FALSE;
		if (strcmp(sections[i], "~devel") == 0)
			*devel = FALSE;
		if (strcmp(sections[i], "gui") == 0)
			*text = FALSE;
		if (strcmp(sections[i], "~gui") == 0)
			*gui = FALSE;
		i++;
	}
	g_strfreev (sections);
}


/**
 * pk_task_get_actions:
 **/
gchar *
pk_task_get_actions (void)
{
	gchar *actions;
	actions = pk_task_action_build (/*PK_TASK_ACTION_INSTALL,*/
				        /*PK_TASK_ACTION_REMOVE,*/
				        /*PK_TASK_ACTION_UPDATE,*/
				        PK_TASK_ACTION_GET_UPDATES,
				        PK_TASK_ACTION_REFRESH_CACHE,
				        /*PK_TASK_ACTION_UPDATE_SYSTEM,*/
				        PK_TASK_ACTION_SEARCH_NAME,
				        /*PK_TASK_ACTION_SEARCH_DETAILS,*/
				        /*PK_TASK_ACTION_SEARCH_GROUP,*/
				        PK_TASK_ACTION_SEARCH_FILE,
				        /*PK_TASK_ACTION_GET_DEPS,*/
				        /*PK_TASK_ACTION_GET_DESCRIPTION,*/
				        0);
	return actions;
}

/**
 * pk_task_get_updates:
 **/
gboolean
pk_task_get_updates (PkTask *task)
{
	GList *list = NULL;
	sqlite3 *db = NULL;

	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, NULL);
	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);

	db = box_db_open ("/");
	box_db_attach_repo (db, "/", "core");
	box_db_repos_init (db);

	/* TODO: make it more async */
	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (task, list);
	box_db_repos_package_list_free (list);

	pk_task_finished (task, PK_TASK_EXIT_SUCCESS);

	box_db_detach_repo(db, "core");
	box_db_close(db);

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

	/* check network state */
	if (pk_network_is_online (task->priv->network) == FALSE) {
		pk_task_error_code (task, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_task_finished (task, PK_TASK_EXIT_FAILED);
		return TRUE;
	}

	/* easy as that */
	pk_task_change_job_status (task, PK_TASK_STATUS_REFRESH_CACHE);
	pk_task_spawn_helper (task, "refresh-cache.sh", NULL);

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

	pk_task_set_job_role (task, PK_TASK_ROLE_SYSTEM_UPDATE, NULL);
	pk_task_not_implemented_yet (task, "UpdateSystem");
	return TRUE;
}

static gboolean
find_packages (PkTask *task, const gchar *search, const gchar *filter, gint mode)
{
	GList *list = NULL;
	sqlite3 *db = NULL;
	gint devel_filter = 0;
	gboolean installed;
	gboolean available;
	gboolean devel;
	gboolean nondevel;
	gboolean gui;
	gboolean text;

	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}
	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, search);

        if (pk_task_filter_check (filter) == FALSE) {
                pk_task_error_code (task, PK_TASK_ERROR_CODE_FILTER_INVALID, "filter '%s' not valid", filter);
                pk_task_finished (task, PK_TASK_EXIT_FAILED);
                return TRUE;
        }

	parse_filter(filter, &installed, &available, &devel, &nondevel, &gui, &text);

	if (devel) {
		devel_filter = devel_filter | PKG_DEVEL;
	}
	if (nondevel) {
		devel_filter = devel_filter | PKG_NON_DEVEL;
	}

	pk_task_change_job_status (task, PK_TASK_STATUS_QUERY);
	pk_task_no_percentage_updates (task);

	db = box_db_open("/");
	box_db_attach_repo(db, "/", "core");
	box_db_repos_init(db);

	if (mode == 1) {
		/* TODO: allow filtering */
		/* TODO: make it more async */
		list = box_db_repos_search_file (db, search);
		add_packages_from_list (task, list);
		box_db_repos_package_list_free (list);
		pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
	} else {

		if (installed == FALSE && available == FALSE) {
			pk_task_error_code (task, PK_TASK_ERROR_CODE_UNKNOWN, "invalid search mode");
			pk_task_finished (task, PK_TASK_EXIT_FAILED);
		} else	{
			/* TODO: make it more async */
			if (installed == TRUE && available == TRUE) {
				list = box_db_repos_packages_search_all(db, (gchar *)search, devel_filter);
			} else if (installed == TRUE) {
				list = box_db_repos_packages_search_installed(db, (gchar *)search, devel_filter);
			} else if (available == TRUE) {
				list = box_db_repos_packages_search_available(db, (gchar *)search, devel_filter);
			}
			add_packages_from_list (task, list);
			box_db_repos_package_list_free (list);
			pk_task_finished (task, PK_TASK_EXIT_SUCCESS);
		}
	}

	box_db_detach_repo(db, "core");
	box_db_close(db);

	return TRUE;
}


/**
 * pk_task_search_name:
 **/
gboolean
pk_task_search_name (PkTask *task, const gchar *filter, const gchar *search)
{
	return find_packages (task, search, filter, 0);
}

/**
 * pk_task_search_details:
 **/
gboolean
pk_task_search_details (PkTask *task, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, search);
	pk_task_not_implemented_yet (task, "SearchDetails");
	return TRUE;
}

/**
 * pk_task_search_group:
 **/
gboolean
pk_task_search_group (PkTask *task, const gchar *filter, const gchar *search)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, search);
	pk_task_not_implemented_yet (task, "SearchGroup");
	return TRUE;
}

/**
 * pk_task_search_file:
 **/
gboolean
pk_task_search_file (PkTask *task, const gchar *filter, const gchar *search)
{
	return find_packages (task, search, filter, 1);
}

/**
 * pk_task_get_deps:
 **/
gboolean
pk_task_get_deps (PkTask *task, const gchar *package_id)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, package_id);
	pk_task_not_implemented_yet (task, "GetDeps");
	return TRUE;
}

/**
 * pk_task_get_description:
 **/
gboolean
pk_task_get_description (PkTask *task, const gchar *package_id)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_QUERY, package_id);
	pk_task_not_implemented_yet (task, "GetDescription");
	return TRUE;
}

/**
 * pk_task_remove_package:
 **/
gboolean
pk_task_remove_package (PkTask *task, const gchar *package_id, gboolean allow_deps)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_PACKAGE_REMOVE, package_id);
	pk_task_not_implemented_yet (task, "RemovePackage");
	return TRUE;
}

/**
 * pk_task_install_package:
 **/
gboolean
pk_task_install_package (PkTask *task, const gchar *package_id)
{
	g_return_val_if_fail (task != NULL, FALSE);
	g_return_val_if_fail (PK_IS_TASK (task), FALSE);

	if (pk_task_assign (task) == FALSE) {
		return FALSE;
	}

	/* check network state */
	if (pk_network_is_online (task->priv->network) == FALSE) {
		pk_task_error_code (task, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot install when offline");
		pk_task_finished (task, PK_TASK_EXIT_FAILED);
		return TRUE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_PACKAGE_INSTALL, package_id);
	pk_task_not_implemented_yet (task, "InstallPackage");
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

	/* check network state */
	if (pk_network_is_online (task->priv->network) == FALSE) {
		pk_task_error_code (task, PK_TASK_ERROR_CODE_NO_NETWORK, "Cannot update when offline");
		pk_task_finished (task, PK_TASK_EXIT_FAILED);
		return TRUE;
	}

	pk_task_set_job_role (task, PK_TASK_ROLE_PACKAGE_UPDATE, package_id);
	pk_task_not_implemented_yet (task, "UpdatePackage");
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

	pk_task_not_implemented_yet (task, "CancelJobTry");
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
	task->priv->network = pk_network_new ();
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
	g_object_unref (task->priv->network);
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

