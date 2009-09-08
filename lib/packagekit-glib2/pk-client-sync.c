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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdio.h>
#include <gio/gio.h>
#include <glib.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"

#include "pk-client-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	PkResults	*results;
} PkClientHelper;

/**
 * pk_client_generic_finish_sync:
 **/
static void
pk_client_generic_finish_sync (PkClient *client, GAsyncResult *res, PkClientHelper *helper)
{
	PkResults *results;
	/* get the result */
	results = pk_client_generic_finish (client, res, helper->error);
	if (results != NULL) {
		g_object_unref (results);
		helper->results = g_object_ref (G_OBJECT (results));
	}
	g_main_loop_quit (helper->loop);
}

/**
 * pk_client_resolve_sync:
 * @client: a valid #PkClient instance
 * @error: A #GError or %NULL
 *
 * Resolves a package to a Package ID.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 **/
PkResults *
pk_client_resolve_sync (PkClient *client, PkFilterEnum filter, gchar **packages, GCancellable *cancellable,
			PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper *helper;
	PkResults *results;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper = g_new0 (PkClientHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_client_resolve_async (client, filter, packages, cancellable, progress_callback, progress_user_data,
				 (GAsyncReadyCallback) pk_client_generic_finish_sync, helper);
	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

/**
 * pk_client_search_file_sync:
 * @client: a valid #PkClient instance
 * @error: A #GError or %NULL
 *
 * Resolves a filename to multiple Package IDs.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: a %PkResults object, or NULL for error
 **/
PkResults *
pk_client_search_file_sync (PkClient *client, PkFilterEnum filter, const gchar *filename, GCancellable *cancellable,
			    PkProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	PkClientHelper *helper;
	PkResults *results;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper = g_new0 (PkClientHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_client_search_file_async (client, filter, filename, cancellable, progress_callback, progress_user_data,
				     (GAsyncReadyCallback) pk_client_generic_finish_sync, helper);
	g_main_loop_run (helper->loop);

	results = helper->results;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return results;
}

