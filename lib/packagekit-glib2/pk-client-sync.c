/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <glib.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-progress.h>

#include "pk-client-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainContext	*context;
	GMainLoop	*loop;
	PkResults	*results;
	PkProgress	*progress;
} PkClientHelper;

/*
 * pk_client_generic_finish_sync:
 **/
static void
pk_client_generic_finish_sync (PkClient *client, GAsyncResult *res, PkClientHelper *helper)
{
	PkResults *results;
	results = pk_client_generic_finish (client, res, helper->error);
	if (results != NULL) {
		helper->results = g_object_ref (G_OBJECT(results));
		g_object_unref (results);
	}
	g_main_loop_quit (helper->loop);
}

/**
 * pk_client_resolve:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @packages: (array zero-terminated=1): an array of package names to resolve, e.g. "gnome-system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Resolve a package name into a @package_id. This can return installed and
 * available packages and allows you find out if a package is installed locally
 * or is available in a repository.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_resolve (PkClient *client, PkBitfield filters, gchar **packages, GCancellable *cancellable,
		   PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_resolve_async (client, filters, packages, cancellable, progress_callback, progress_user_data,
				 (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_search_names:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Search all the locally installed files and remote repositories for a package
 * that matches a specific name.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.5
 **/
PkResults *
pk_client_search_names (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_search_names_async (client, filters, values, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_search_details:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): free text to search for, for instance, "power"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Search all detailed summary information to try and find a keyword.
 * Think of this as pk_client_search_names(), but trying much harder and
 * taking longer.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.5
 **/
PkResults *
pk_client_search_details (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_search_details_async (client, filters, values, cancellable, progress_callback, progress_user_data,
					(GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_search_groups:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): a group enum to search for, for instance, "system-tools"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Return all packages in a specific group.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.5
 **/
PkResults *
pk_client_search_groups (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
			PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_search_groups_async (client, filters, values, cancellable, progress_callback, progress_user_data,
				      (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_search_files:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): file to search for, for instance, "/sbin/service"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Search for packages that provide a specific file.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.5
 **/
PkResults *
pk_client_search_files (PkClient *client, PkBitfield filters, gchar **values, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_search_files_async (client, filters, values, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_details:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get details of a package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_details (PkClient *client, gchar **package_ids, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_details_async (client, package_ids, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_details_local:
 * @client: a valid #PkClient instance
 * @files: (array zero-terminated=1): a null terminated array of filenames
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get details of a local package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.8.17
 **/
PkResults *
pk_client_get_details_local (PkClient *client, gchar **files, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_details_local_async (client, files, cancellable,
					   progress_callback, progress_user_data,
					   (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_files_local:
 * @client: a valid #PkClient instance
 * @files: (array zero-terminated=1): a null terminated array of filenames
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get file list of a local package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.9.1
 **/
PkResults *
pk_client_get_files_local (PkClient *client, gchar **files, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data,
			     GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_files_local_async (client, files, cancellable,
					 progress_callback, progress_user_data,
					 (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_update_detail:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get details about the specific update, for instance any CVE urls and
 * severity information.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_update_detail (PkClient *client, gchar **package_ids, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_update_detail_async (client, package_ids, cancellable, progress_callback, progress_user_data,
					   (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_download_packages:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the location where packages are to be downloaded
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Downloads package files to a specified location.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_download_packages (PkClient *client, gchar **package_ids, const gchar *directory, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_download_packages_async (client, package_ids, directory, cancellable, progress_callback, progress_user_data,
					   (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_updates:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_DEVELOPMENT or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get a list of all the packages that can be updated for all repositories.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_updates (PkClient *client, PkBitfield filters, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_updates_async (client, filters, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_old_transactions:
 * @client: a valid #PkClient instance
 * @number: the number of past transactions to return, or 0 for all
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the old transaction list, mainly used for the transaction viewer.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_old_transactions (PkClient *client, guint number, GCancellable *cancellable,
				PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_old_transactions_async (client, number, cancellable, progress_callback, progress_user_data,
					      (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_depends_on:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for depends
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the packages that depend this one, i.e. child.parent.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_depends_on (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_depends_on_async (client, filters, package_ids, recursive, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_packages:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the list of packages from the backend
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_packages (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_packages_async (client, filters, cancellable, progress_callback, progress_user_data,
				      (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_required_by:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for requires
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the packages that require this one, i.e. parent.child.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_required_by (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GCancellable *cancellable,
			PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_required_by_async (client, filters, package_ids, recursive, cancellable, progress_callback, progress_user_data,
				      (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_what_provides:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @values: (array zero-terminated=1): a search term such as "sound/mp3"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * This should return packages that provide the supplied attributes.
 * This method is useful for finding out what package(s) provide a modalias
 * or GStreamer codec string.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_what_provides (PkClient *client,
			 PkBitfield filters,
			 gchar **values,
			 GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_what_provides_async (client, filters, values, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_distro_upgrades:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * This method should return a list of distribution upgrades that are available.
 * It should not return updates, only major upgrades.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_distro_upgrades (PkClient *client, GCancellable *cancellable,
			       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_distro_upgrades_async (client, cancellable, progress_callback, progress_user_data,
					     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_files:
 * @client: a valid #PkClient instance
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the file list (i.e. a list of files installed) for the specified package.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_files (PkClient *client, gchar **package_ids, GCancellable *cancellable,
		     PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_files_async (client, package_ids, cancellable, progress_callback, progress_user_data,
				   (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_categories:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get a list of all categories supported.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_categories (PkClient *client, GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_categories_async (client, cancellable, progress_callback, progress_user_data,
					(GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_remove_packages:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependent packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Remove a package (optionally with dependancies) from the system.
 * If @allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.8.1
 **/
PkResults *
pk_client_remove_packages (PkClient *client,
			   PkBitfield transaction_flags,
			   gchar **package_ids,
			   gboolean allow_deps,
			   gboolean autoremove,
			   GCancellable *cancellable,
			   PkProgressCallback progress_callback,
			   gpointer progress_user_data,
			   GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_remove_packages_async (client,
					 transaction_flags,
					 package_ids,
					 allow_deps,
					 autoremove,
					 cancellable,
					 progress_callback,
					 progress_user_data,
					 (GAsyncReadyCallback) pk_client_generic_finish_sync,
					 &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_refresh_cache:
 * @client: a valid #PkClient instance
 * @force: if we should aggressively drop caches
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Refresh the cache, i.e. download new metadata from a remote URL so that
 * package lists are up to date.
 * This action may take a few minutes and should be done when the session and
 * system are idle.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_refresh_cache (PkClient *client, gboolean force, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_refresh_cache_async (client, force, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_install_packages:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.8.1
 **/
PkResults *
pk_client_install_packages (PkClient *client,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    GCancellable *cancellable,
			    PkProgressCallback progress_callback,
			    gpointer progress_user_data,
			    GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_install_packages_async (client, transaction_flags, package_ids, cancellable, progress_callback, progress_user_data,
					  (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_install_signature:
 * @client: a valid #PkClient instance
 * @type: the signature type, e.g. %PK_SIGTYPE_ENUM_GPG
 * @key_id: a key ID such as "0df23df"
 * @package_id: a signature_id structure such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Install a software repository signature of the newest and most correct version.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_install_signature (PkClient *client, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id, GCancellable *cancellable,
			     PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_install_signature_async (client, type, key_id, package_id, cancellable, progress_callback, progress_user_data,
					   (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_update_packages:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @package_ids: (array zero-terminated=1): a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Update specific packages to the newest available versions.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.8.1
 **/
PkResults *
pk_client_update_packages (PkClient *client,
			   PkBitfield transaction_flags,
			   gchar **package_ids,
			   GCancellable *cancellable,
			   PkProgressCallback progress_callback,
			   gpointer progress_user_data,
			   GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_update_packages_async (client, transaction_flags, package_ids, cancellable, progress_callback, progress_user_data,
					 (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_install_files:
 * @client: a valid #PkClient instance
 * @transaction_flags: a transaction type bitfield
 * @files: (array zero-terminated=1): a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.8.1
 **/
PkResults *
pk_client_install_files (PkClient *client,
			 PkBitfield transaction_flags,
			 gchar **files,
			 GCancellable *cancellable,
			 PkProgressCallback progress_callback,
			 gpointer progress_user_data,
			 GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_install_files_async (client, transaction_flags, files, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_accept_eula:
 * @client: a valid #PkClient instance
 * @eula_id: the <literal>eula_id</literal> we are agreeing to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * We may want to agree to a EULA dialog if one is presented.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_accept_eula (PkClient *client, const gchar *eula_id, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_accept_eula_async (client, eula_id, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_get_repo_list:
 * @client: a valid #PkClient instance
 * @filters: a #PkBitfield such as %PK_FILTER_ENUM_DEVELOPMENT or %PK_FILTER_ENUM_NONE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the list of repositories installed on the system.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_get_repo_list (PkClient *client, PkBitfield filters, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_repo_list_async (client, filters, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_repo_enable:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @enabled: if we should enable the repository
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Enable or disable the repository.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_repo_enable (PkClient *client, const gchar *repo_id, gboolean enabled, GCancellable *cancellable,
		       PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_repo_enable_async (client, repo_id, enabled, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_repo_set_data:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @parameter: the parameter to change
 * @value: what we should change it to
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * We may want to set a repository parameter.
 * NOTE: this is free text, and is left to the backend to define a format.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_repo_set_data (PkClient *client, const gchar *repo_id, const gchar *parameter, const gchar *value, GCancellable *cancellable,
			 PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_repo_set_data_async (client, repo_id, parameter, value, cancellable, progress_callback, progress_user_data,
				       (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_repo_remove:
 * @client: a valid #PkClient instance
 * @transaction_flags: transaction flags
 * @repo_id: a repo_id structure such as "livna-devel"
 * @autoremove: If packages should be auto-removed
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Removes a repo and optionally the packages installed from it.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.9.1
 **/
PkResults *
pk_client_repo_remove (PkClient *client,
		       PkBitfield transaction_flags,
		       const gchar *repo_id,
		       gboolean autoremove,
		       GCancellable *cancellable,
		       PkProgressCallback progress_callback,
		       gpointer progress_user_data,
		       GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_repo_remove_async (client,
				     transaction_flags,
				     repo_id,
				     autoremove,
				     cancellable,
				     progress_callback,
				     progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync,
				     &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_upgrade_system:
 * @client: a valid #PkClient instance
 * @distro_id: a distro ID such as "fedora-14"
 * @transaction_flags: transaction flags
 * @upgrade_kind: a #PkUpgradeKindEnum such as %PK_UPGRADE_KIND_ENUM_COMPLETE
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * This transaction will upgrade the distro to the next version, which may
 * involve just downloading the installer and setting up the boot device,
 * or may involve doing an on-line upgrade.
 *
 * The backend will decide what is best to do.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 1.0.10
 **/
PkResults *
pk_client_upgrade_system (PkClient *client, PkBitfield transaction_flags,
			  const gchar *distro_id, PkUpgradeKindEnum upgrade_kind,
			  GCancellable *cancellable,
			  PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_upgrade_system_async (client, transaction_flags, distro_id, upgrade_kind,
					cancellable, progress_callback, progress_user_data,
					(GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_repair_system:
 * @client: a valid #PkClient instance
 * @transaction_flags: if only trusted packages should be installed
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * This transaction will try to recover from a broken package management system:
 * e.g. the installation of a package with unsatisfied dependencies has
 * been forced by using a low level tool (rpm or dpkg) or the
 * system was shutdown during processing an installation.
 *
 * The backend will decide what is best to do.
 *
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.8.1
 **/
PkResults *
pk_client_repair_system (PkClient *client,
			 PkBitfield transaction_flags,
			 GCancellable *cancellable,
			 PkProgressCallback progress_callback,
			 gpointer progress_user_data,
			 GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_repair_system_async (client,
				       transaction_flags,
				       cancellable,
				       progress_callback,
				       progress_user_data,
				       (GAsyncReadyCallback) pk_client_generic_finish_sync,
				       &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/**
 * pk_client_adopt:
 * @client: a valid #PkClient instance
 * @transaction_id: a transaction ID such as "/21_ebcbdaae_data"
 * @cancellable: a #GCancellable or %NULL
 * @progress_callback: (scope call): the function to run when the progress changes
 * @progress_user_data: data to pass to @progress_callback
 * @error: the #GError to store any failure, or %NULL
 *
 * Adopt a transaction.
 *
 * Warning: this function is synchronous, and will block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkResults *
pk_client_adopt (PkClient *client, const gchar *transaction_id, GCancellable *cancellable,
		 PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper helper;
	PkResults *results;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_adopt_async (client, transaction_id, cancellable, progress_callback, progress_user_data,
			       (GAsyncReadyCallback) pk_client_generic_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	results = helper.results;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return results;
}

/*
 * pk_client_get_progress_finish_sync:
 **/
static void
pk_client_get_progress_finish_sync (PkClient *client, GAsyncResult *res, PkClientHelper *helper)
{
	PkProgress *progress;
	progress = pk_client_get_progress_finish (client, res, helper->error);
	if (progress != NULL) {
		helper->progress = g_object_ref (G_OBJECT(progress));
		g_object_unref (progress);
	}
	g_main_loop_quit (helper->loop);
}

/**
 * pk_client_get_progress:
 * @client: a valid #PkClient instance
 * @transaction_id: The transaction id
 * @cancellable: a #GCancellable or %NULL
 * @error: the #GError to store any failure, or %NULL
 *
 * Get the progress of a transaction.
 *
 * Warning: this function is synchronous, and will block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): a #PkResults object, or %NULL for error
 *
 * Since: 0.5.3
 **/
PkProgress *
pk_client_get_progress (PkClient *client, const gchar *transaction_id, GCancellable *cancellable, GError **error)
{
	PkClientHelper helper;
	PkProgress *progress;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkClientHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_client_get_progress_async (client, transaction_id, cancellable,
				      (GAsyncReadyCallback) pk_client_get_progress_finish_sync, &helper);

	g_main_loop_run (helper.loop);

	progress = helper.progress;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return progress;
}

