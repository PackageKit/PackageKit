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

/**
 * pk_task_resolve_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @packages: package names to find
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Resolves a package name to a package-id.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_resolve_sync (PkTask *task, PkBitfield filters, gchar **packages, GCancellable *cancellable,
		      PkProgressCallback progress_callback, gpointer progress_user_data,
		      GError **error)
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
	pk_task_resolve_async (task, filters, packages, cancellable, progress_callback, progress_user_data,
			       (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_search_names_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Searches for a package name.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_search_names_sync (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GError **error)
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
	pk_task_search_names_async (task, filters, values, cancellable, progress_callback, progress_user_data,
				    (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_search_details_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Searches for some package details.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_search_details_sync (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GError **error)
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
	pk_task_search_details_async (task, filters, values, cancellable, progress_callback, progress_user_data,
				      (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_search_groups_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Searches the group lists.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_search_groups_sync (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GError **error)
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
	pk_task_search_groups_async (task, filters, values, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_search_files_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @values: search values
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Searches for specific files.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_search_files_sync (PkTask *task, PkBitfield filters, gchar **values, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GError **error)
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
	pk_task_search_files_async (task, filters, values, cancellable, progress_callback, progress_user_data,
				    (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_details_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Gets details about packages.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_details_sync (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data,
			  GError **error)
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
	pk_task_get_details_async (task, package_ids, cancellable, progress_callback, progress_user_data,
				   (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_update_detail_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Gets details about updates.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_update_detail_sync (PkTask *task, gchar **package_ids, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GError **error)
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
	pk_task_get_update_detail_async (task, package_ids, cancellable, progress_callback, progress_user_data,
				         (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_download_packages_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the destination directory
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Downloads packages
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_download_packages_sync (PkTask *task, gchar **package_ids, const gchar *directory, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data,
				GError **error)
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
	pk_task_download_packages_async (task, package_ids, directory, cancellable, progress_callback, progress_user_data,
				         (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_updates_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Gets the update lists.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_updates_sync (PkTask *task, PkBitfield filters, GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data,
			  GError **error)
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
	pk_task_get_updates_async (task, filters, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_depends_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: if we should recurse to packages that depend on other packages
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the list of dependant packages.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_depends_sync (PkTask *task, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data,
			  GError **error)
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
	pk_task_get_depends_async (task, filters, package_ids, recursive, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_packages_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Gets the list of packages.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_packages_sync (PkTask *task, PkBitfield filters, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GError **error)
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
	pk_task_get_packages_async (task, filters, cancellable, progress_callback, progress_user_data,
				    (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_requires_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: if we should return packages that depend on the ones we do
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the packages this package requires.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_requires_sync (PkTask *task, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			   PkProgressCallback progress_callback, gpointer progress_user_data,
			   GError **error)
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
	pk_task_get_requires_async (task, filters, package_ids, recursive, cancellable, progress_callback, progress_user_data,
				    (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_what_provides_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @provides: a #PkProvidesEnum type
 * @values: values to search for
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Find the package that provides some resource.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_what_provides_sync (PkTask *task, PkBitfield filters, PkProvidesEnum provides, gchar **values, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GError **error)
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
	pk_task_what_provides_async (task, filters, provides, values, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_files_sync:
 * @task: a valid #PkTask instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the files in a package.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_files_sync (PkTask *task, gchar **package_ids, GCancellable *cancellable,
			PkProgressCallback progress_callback, gpointer progress_user_data,
			GError **error)
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
	pk_task_get_files_async (task, package_ids, cancellable, progress_callback, progress_user_data,
				 (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_categories_sync:
 * @task: a valid #PkTask instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the categories available.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_categories_sync (PkTask *task, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GError **error)
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
	pk_task_get_categories_async (task, cancellable, progress_callback, progress_user_data,
				      (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_refresh_cache_sync:
 * @task: a valid #PkTask instance
 * @force: if the metadata should be deleted and re-downloaded even if it is correct
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Refresh the package cache.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_refresh_cache_sync (PkTask *task, gboolean force, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GError **error)
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
	pk_task_refresh_cache_async (task, force, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_rollback_sync:
 * @task: a valid #PkTask instance
 * @transaction_id: The transaction ID of the old transaction
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Rollback to a previous package state.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_rollback_sync (PkTask *task, const gchar *transaction_id, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data,
		       GError **error)
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
	pk_task_rollback_async (task, transaction_id, cancellable, progress_callback, progress_user_data,
				(GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_get_repo_list_sync:
 * @task: a valid #PkTask instance
 * @filters: a bitfield of filters that can be used to limit the results
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the list of available repositories.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_get_repo_list_sync (PkTask *task, PkBitfield filters, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data,
			    GError **error)
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
	pk_task_get_repo_list_async (task, filters, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_task_repo_enable_sync:
 * @task: a valid #PkTask instance
 * @repo_id: The software source ID
 * @enabled: %TRUE or %FALSE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Enable or disable a specific repo.
 *
 * Since: 0.6.5
 **/
PkResults *
pk_task_repo_enable_sync (PkTask *task, const gchar *repo_id, gboolean enabled, GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data,
			  GError **error)
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
	pk_task_repo_enable_async (task, repo_id, enabled, cancellable, progress_callback, progress_user_data,
				   (GAsyncReadyCallback) pk_task_generic_finish_sync, helper);

	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

