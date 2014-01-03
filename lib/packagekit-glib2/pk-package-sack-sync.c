/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
#include <packagekit-glib2/pk-package-sack.h>

#include "pk-package-sack-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainContext	*context;
	GMainLoop	*loop;
	gboolean	 ret;
} PkPackageSackHelper;

/**
 * pk_package_sack_generic_cb:
 **/
static void
pk_package_sack_generic_cb (PkPackageSack *package_sack, GAsyncResult *res, PkPackageSackHelper *helper)
{
	/* get the result */
	helper->ret = pk_package_sack_merge_generic_finish (package_sack, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_package_sack_resolve:
 * @package_sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the properties the daemon supports.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the properties were set correctly
 *
 * Since: 0.6.0
 **/
gboolean
pk_package_sack_resolve (PkPackageSack *package_sack, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	PkPackageSackHelper helper;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (package_sack), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	memset (&helper, 0, sizeof (PkPackageSackHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_package_sack_resolve_async (package_sack, cancellable, NULL, NULL, (GAsyncReadyCallback) pk_package_sack_generic_cb, &helper);
	g_main_loop_run (helper.loop);

	ret = helper.ret;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return ret;
}

/**
 * pk_package_sack_get_details:
 * @package_sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the properties the daemon supports.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the properties were set correctly
 *
 * Since: 0.6.0
 **/
gboolean
pk_package_sack_get_details (PkPackageSack *package_sack, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	PkPackageSackHelper helper;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (package_sack), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	memset (&helper, 0, sizeof (PkPackageSackHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_package_sack_get_details_async (package_sack, cancellable, NULL, NULL, (GAsyncReadyCallback) pk_package_sack_generic_cb, &helper);
	g_main_loop_run (helper.loop);

	ret = helper.ret;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return ret;
}

/**
 * pk_package_sack_get_update_detail:
 * @package_sack: a valid #PkPackageSack instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the properties the daemon supports.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the properties were set correctly
 *
 * Since: 0.6.0
 **/
gboolean
pk_package_sack_get_update_detail (PkPackageSack *package_sack, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	PkPackageSackHelper helper;

	g_return_val_if_fail (PK_IS_PACKAGE_SACK (package_sack), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	memset (&helper, 0, sizeof (PkPackageSackHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_package_sack_get_update_detail_async (package_sack, cancellable, NULL, NULL, (GAsyncReadyCallback) pk_package_sack_generic_cb, &helper);
	g_main_loop_run (helper.loop);

	ret = helper.ret;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return ret;
}

