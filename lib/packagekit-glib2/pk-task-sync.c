/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdio.h>
#include <gio/gio.h>
#include <glib.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-task.h>
#include <packagekit-glib2/pk-progress.h>

#include "egg-debug.h"

#include "pk-task-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	PkResults	*results;
} PkTaskHelper;

/**
 * pk_task_generic_finish_sync:
 **/
static void
pk_task_generic_finish_sync (PkTask *task, GAsyncResult *res, PkTaskHelper *helper)
{
	PkResults *results;
	/* get the result */
	results = pk_task_generic_finish (task, res, helper->error);
	if (results != NULL) {
		g_object_unref (results);
		helper->results = g_object_ref (G_OBJECT (results));
	}
	g_main_loop_quit (helper->loop);
}

/**
 * pk_task_update_system_sync:
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Update all the packages on the system with the highest versions found in all
 * repositories.
 * NOTE: you can't choose what repositories to update from, but you can do:
 * - pk_task_repo_disable()
 * - pk_task_update_system()
 * - pk_task_repo_enable()
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_task_update_system_sync (PkTask *task, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkTaskHelper *helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	helper = g_new0 (PkTaskHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_task_update_system_async (task, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_remove_packages_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependant packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Remove a package (optionally with dependancies) from the system.
 * If %allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_task_remove_packages_sync (PkTask *task, gchar **package_ids, gboolean allow_deps, gboolean autoremove, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkTaskHelper *helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	helper = g_new0 (PkTaskHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_task_remove_packages_async (task, package_ids, allow_deps, autoremove, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_install_packages_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_task_install_packages_sync (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkTaskHelper *helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	helper = g_new0 (PkTaskHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_task_install_packages_async (task, package_ids, cancellable, progress_callback, progress_user_data,
					(GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_update_packages_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Update specific packages to the newest available versions.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_task_update_packages_sync (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			      PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkTaskHelper *helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	helper = g_new0 (PkTaskHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_task_update_packages_async (task, package_ids, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_install_files_sync:
 * @task: a valid #PkTask instance
 * @files: a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_task_install_files_sync (PkTask *task, gchar **files, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkTaskHelper *helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_TASK (task), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	helper = g_new0 (PkTaskHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_task_install_files_async (task, files, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

