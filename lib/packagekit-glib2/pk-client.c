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

/**
 * SECTION:pk-client
 * @short_description: TODO
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <locale.h>
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
#include <gio/gio.h>

#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-marshal.h>

#include "egg-debug.h"

static void     pk_client_finalize	(GObject     *object);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct _PkClientPrivate
{
//	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	PkControl		*control;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	/* TODO: add the other existing properties */
	PROP_ID,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

typedef struct {
	PkClient		*client;
	GCancellable		*cancellable;
	gchar			*tid;
	gchar			**packages;
	GSimpleAsyncResult	*result;
	DBusGProxyCall		*call;
	PkResults		*results;
	DBusGProxy		*proxy;
} PkClientState;

static void pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClientState *state);

/**
 * pk_client_state_finish:
 **/
static void
pk_client_state_finish (PkClientState *state, GError *error)
{
	PkClientPrivate *priv;
	priv = state->client->priv;

	g_strfreev (state->packages);

	if (state->client != NULL) {
		g_object_remove_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);
	}

	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	if (state->proxy != NULL) {
		dbus_g_proxy_disconnect_signal (state->proxy, "Finished",
						G_CALLBACK (pk_client_finished_cb), state->client);
		g_object_unref (G_OBJECT (state->proxy));
	}

	if (state->results != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->result, g_object_ref (state->results), g_object_unref);
	} else {
		g_simple_async_result_set_from_error (state->result, error);
		g_error_free (error);
	}

	g_simple_async_result_complete_in_idle (state->result);
	g_object_unref (state->result);
	g_slice_free (PkClientState, state);
}

/**
 * pk_client_finished_cb:
 */
static void
pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClientState *state)
{
	GError *error = NULL;
	PkExitEnum exit_enum;

	egg_warning ("exit_text=%s", exit_text);

	/* yay */
	exit_enum = pk_exit_enum_from_text (exit_text);
	pk_results_set_exit_code (state->results, exit_enum);

	/* failed */
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		/* TODO: get error code and error message */
		error = g_error_new (1, 0, "Failed to run: %s", exit_text);
		pk_client_state_finish (state, error);
		return;
	}

	/* we're done */
	pk_client_state_finish (state, error);
}

/**
 * pk_client_method_cb:
 **/
static void
pk_client_method_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkClientState *state)
{
//	PkClient *client = PK_CLIENT (state->client);
	GError *error = NULL;
	gboolean ret;

	/* we've sent this async */
	egg_debug ("got reply to request");

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed: %s", error->message);
		pk_client_state_finish (state, error);
		return;
	}

	/* finished this call */
	state->call = NULL;

	/* wait for ::Finished() */
}

/**
 * pk_client_get_tid_cb:
 **/
static void
pk_client_get_tid_cb (GObject *object, GAsyncResult *result, PkClientState *state)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	const gchar *tid = NULL;

	tid = pk_control_get_tid_finish (control, result, &error);
	if (tid == NULL) {
		pk_client_state_finish (state, error);
//		egg_test_failed (test, "failed to get transaction: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_debug ("tid = %s", tid);
	state->tid = g_strdup (tid);

	/* get a connection to the tranaction interface */
	state->proxy = dbus_g_proxy_new_for_name (state->client->priv->connection,
						  PK_DBUS_SERVICE, tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (state->proxy == NULL)
		egg_error ("Cannot connect to PackageKit on %s", tid);

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (state->proxy, INT_MAX);

	dbus_g_proxy_add_signal (state->proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (state->proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), state, NULL);

	/* do this async, although this should be pretty fast anyway */
	state->call = dbus_g_proxy_begin_call (state->proxy, "Resolve",
					       (DBusGProxyCallNotify) pk_client_method_cb, state, NULL,
					       G_TYPE_STRING, "installed", //TODO: add filter
					       G_TYPE_STRV, state->packages,
					       G_TYPE_INVALID);

	/* we've sent this async */
	egg_debug ("sent request");

	/* we'll have results from now on */
	state->results = pk_results_new ();
}

/**
 * pk_client_resolve_async:
 * @client: a valid #PkClient instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * TODO
 **/
void
pk_client_resolve_async (PkClient *client, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *result;
	PkClientState *state;

	g_return_if_fail (PK_IS_CLIENT (client));
	g_return_if_fail (callback != NULL);

	result = g_simple_async_result_new (G_OBJECT (client), callback, user_data, pk_client_resolve_async);

	/* save state */
	state = g_slice_new0 (PkClientState);
	state->result = g_object_ref (result);
	state->cancellable = cancellable;
	state->client = client;
	state->results = NULL;
	state->proxy = NULL;
	state->call = NULL;
	state->packages = g_strsplit ("gnome-power-manager,hal", ",", -1); //TODO: add parameter
	g_object_add_weak_pointer (G_OBJECT (state->client), (gpointer) &state->client);

	/* get tid */
	pk_control_get_tid_async (client->priv->control, NULL, (GAsyncReadyCallback) pk_client_get_tid_cb, state);
	g_object_unref (result);
}

/**
 * pk_client_resolve_finish:
 * @client: a valid #PkClient instance
 * @result: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function. 
 *
 * Return value: the ID, or %NULL if unset
 **/
PkResults *
pk_client_resolve_finish (PkClient *client, GAsyncResult *result, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_client_resolve_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/**
 * pk_client_get_property:
 **/
static void
pk_client_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
//	PkClient *client = PK_CLIENT (object);
//	PkClientPrivate *priv = client->priv;

	switch (prop_id) {
//	case PROP_ID:
//		g_value_set_string (value, priv->id);
//		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_set_property:
 **/
static void
pk_client_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
//	PkClient *client = PK_CLIENT (object);
//	PkClientPrivate *priv = client->priv;

	switch (prop_id) {
//	case PROP_INFO:
//		priv->info = g_value_get_uint (value);
//		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_class_init:
 * @klass: The PkClientClass
 **/
static void
pk_client_class_init (PkClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_client_get_property;
	object_class->set_property = pk_client_set_property;
	object_class->finalize = pk_client_finalize;

	/**
	 * PkClient:id:
	 */
	pspec = g_param_spec_string ("id", NULL,
				     "The full client_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * PkClient::changed:
	 * @client: the #PkClient instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the client data may have changed.
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkClientPrivate));
}

/**
 * pk_client_init:
 * @client: This class instance
 **/
static void
pk_client_init (PkClient *client)
{
	GError *error = NULL;
	client->priv = PK_CLIENT_GET_PRIVATE (client);

	/* check dbus connections, exit if not valid */
	client->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* use a control object */
	client->priv->control = pk_control_new ();

	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
}

/**
 * pk_client_finalize:
 * @object: The object to finalize
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client = PK_CLIENT (object);
	PkClientPrivate *priv = client->priv;

	g_object_unref (priv->control);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 *
 * Return value: a new PkClient object.
 **/
PkClient *
pk_client_new (void)
{
	PkClient *client;
	client = g_object_new (PK_TYPE_CLIENT, NULL);
	return PK_CLIENT (client);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_client_test_resolve_cb (GObject *object, GAsyncResult *result, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;

	results = pk_client_resolve_finish (client, result, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		return;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));

	egg_debug ("results exit enum = %s", pk_exit_enum_to_text (exit_enum));
	egg_test_loop_quit (test);
}

void
pk_client_test (EggTest *test)
{
	PkClient *client;

	if (!egg_test_start (test, "PkClient"))
		return;

	/************************************************************/
	egg_test_title (test, "get client");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);

	/************************************************************/
	egg_test_title (test, "get TID async");
	pk_client_resolve_async (client, NULL, (GAsyncReadyCallback) pk_client_test_resolve_cb, test);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "got tid in %i", egg_test_elapsed (test));

	g_object_unref (client);
out:
	egg_test_end (test);
}
#endif

