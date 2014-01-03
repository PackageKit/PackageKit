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
#include <packagekit-glib2/pk-control.h>

#include "pk-control-sync.h"

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainContext	*context;
	GMainLoop	*loop;
	gboolean	 ret;
	guint		 seconds;
	gchar		**transaction_list;
} PkControlHelper;

/**
 * pk_control_get_properties_cb:
 **/
static void
pk_control_get_properties_cb (PkControl *control, GAsyncResult *res, PkControlHelper *helper)
{
	/* get the result */
	helper->ret = pk_control_get_properties_finish (control, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_control_get_properties:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the properties the daemon supports.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the properties were set correctly
 *
 * Since: 0.5.3
 **/
gboolean
pk_control_get_properties (PkControl *control, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	PkControlHelper helper;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	memset (&helper, 0, sizeof (PkControlHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_control_get_properties_async (control, cancellable, (GAsyncReadyCallback) pk_control_get_properties_cb, &helper);
	g_main_loop_run (helper.loop);

	ret = helper.ret;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return ret;
}

/**
 * pk_control_get_transaction_list_cb:
 **/
static void
pk_control_get_transaction_list_cb (PkControl *control, GAsyncResult *res, PkControlHelper *helper)
{
	/* get the result */
	helper->transaction_list = pk_control_get_transaction_list_finish (control, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_control_get_transaction_list:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Gets the transaction list in progress.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: (transfer full): The list of transaction id's, or %NULL, free with g_strfreev()
 *
 * Since: 0.5.3
 **/
gchar **
pk_control_get_transaction_list (PkControl *control, GCancellable *cancellable, GError **error)
{
	gchar **transaction_list;
	PkControlHelper helper;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* create temp object */
	memset (&helper, 0, sizeof (PkControlHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_control_get_transaction_list_async (control, cancellable, (GAsyncReadyCallback) pk_control_get_transaction_list_cb, &helper);
	g_main_loop_run (helper.loop);

	transaction_list = helper.transaction_list;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return transaction_list;
}

/**
 * pk_control_suggest_daemon_quit_cb:
 **/
static void
pk_control_suggest_daemon_quit_cb (PkControl *control, GAsyncResult *res, PkControlHelper *helper)
{
	/* get the result */
	helper->ret = pk_control_suggest_daemon_quit_finish (control, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_control_suggest_daemon_quit:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Suggests to the daemon that it should quit as soon as possible.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the suggestion was sent
 *
 * Since: 0.6.2
 **/
gboolean
pk_control_suggest_daemon_quit (PkControl *control, GCancellable *cancellable, GError **error)
{
	gboolean ret;
	PkControlHelper helper;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	memset (&helper, 0, sizeof (PkControlHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_control_suggest_daemon_quit_async (control, cancellable, (GAsyncReadyCallback) pk_control_suggest_daemon_quit_cb, &helper);
	g_main_loop_run (helper.loop);

	ret = helper.ret;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return ret;
}

/**
 * pk_control_set_proxy_cb:
 **/
static void
pk_control_set_proxy_cb (PkControl *control, GAsyncResult *res, PkControlHelper *helper)
{
	/* get the result */
	helper->ret = pk_control_set_proxy_finish (control, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_control_set_proxy2:
 * @control: a valid #PkControl instance
 * @proxy_http: the HTTP proxy server
 * @proxy_ftp: the FTP proxy server
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Sets the network proxy to use in the daemon.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the proxy was set correctly
 *
 * Since: 0.6.13
 **/
gboolean
pk_control_set_proxy2 (PkControl *control,
		       const gchar *proxy_http,
		       const gchar *proxy_https,
		       const gchar *proxy_ftp,
		       const gchar *proxy_socks,
		       const gchar *no_proxy,
		       const gchar *pac,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret;
	PkControlHelper helper;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	memset (&helper, 0, sizeof (PkControlHelper));
	helper.context = g_main_context_new ();
	helper.loop = g_main_loop_new (helper.context, FALSE);
	helper.error = error;

	g_main_context_push_thread_default (helper.context);

	/* run async method */
	pk_control_set_proxy2_async (control,
				     proxy_http,
				     proxy_https,
				     proxy_ftp,
				     proxy_socks,
				     no_proxy,
				     pac,
				     cancellable,
				     (GAsyncReadyCallback) pk_control_set_proxy_cb,
				     &helper);
	g_main_loop_run (helper.loop);

	ret = helper.ret;

	g_main_context_pop_thread_default (helper.context);

	/* free temp object */
	g_main_loop_unref (helper.loop);
	g_main_context_unref (helper.context);

	return ret;
}


/**
 * pk_control_set_proxy:
 * @control: a valid #PkControl instance
 * @proxy_http: the HTTP proxy server
 * @proxy_ftp: the FTP proxy server
 * @cancellable: a #GCancellable or %NULL
 * @error: A #GError or %NULL
 *
 * Sets the network proxy to use in the daemon.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the proxy was set correctly
 *
 * NOTE: This is just provided for backwards compatibility.
 * Clients should really be using pk_control_set_proxy2().
 *
 * Since: 0.6.3
 **/
gboolean
pk_control_set_proxy (PkControl *control,
		      const gchar *proxy_http,
		      const gchar *proxy_ftp,
		      GCancellable *cancellable,
		      GError **error)
{
	return pk_control_set_proxy2 (control,
				      proxy_http,
				      NULL,
				      proxy_ftp,
				      NULL,
				      NULL,
				      NULL,
				      cancellable,
				      error);
}
