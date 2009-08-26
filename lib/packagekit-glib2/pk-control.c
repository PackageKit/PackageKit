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
 * SECTION:pk-control
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

#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"

static void     pk_control_finalize	(GObject     *object);

#define PK_CONTROL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONTROL, PkControlPrivate))

/**
 * PkControlPrivate:
 *
 * Private #PkControl data
 **/
struct _PkControlPrivate
{
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
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

G_DEFINE_TYPE (PkControl, pk_control, G_TYPE_OBJECT)

typedef struct {
	PkControl		*control;
	GCancellable		*cancellable;
	gchar			*tid;
	GSimpleAsyncResult	*result;
	DBusGProxyCall		*call;
} PkControlState;

/**
 * pk_control_get_tid_state_finish:
 **/
static void
pk_control_get_tid_state_finish (PkControlState *state, GError *error)
{
//	PkControlPrivate *priv;

	if (state->control != NULL) {
//		priv = state->control->priv;
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);
	}

	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	if (state->tid != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->result, g_strdup (state->tid), g_free);
	} else {
		g_simple_async_result_set_from_error (state->result, error);
		g_error_free (error);
	}

	g_simple_async_result_complete_in_idle (state->result);
	g_object_unref (state->result);
	g_slice_free (PkControlState, state);
}


/**
 * pk_control_get_tid_cb:
 **/
static void
pk_control_get_tid_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
//	PkControl *control = PK_CONTROL (state->control);
	GError *error = NULL;
	gchar *tid = NULL;
	gboolean ret;

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     G_TYPE_STRING, &tid,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed: %s", error->message);
		pk_control_get_tid_state_finish (state, error);
		goto out;
	}

	/* finished this call */
	state->call = NULL;

	/* TODO: set locale */
	state->tid = g_strdup (tid);

	/* we're done */
	pk_control_get_tid_state_finish (state, error);
out:
	g_free (tid);
}

/**
 * pk_control_get_tid_async:
 * @control: a valid #PkControl instance
 * @cancellable: a #GCancellable or %NULL
 * @callback: the function to run on completion
 * @user_data: the data to pass to @callback
 *
 * Gets a transacton ID from the daemon.
 **/
void
pk_control_get_tid_async (PkControl *control, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *result;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	result = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_tid_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->result = g_object_ref (result);
	state->cancellable = cancellable;
	state->control = control;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus method async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTid",
					       (DBusGProxyCallNotify) pk_control_get_tid_cb, state,
					       NULL, G_TYPE_INVALID);
	g_object_unref (result);
}

/**
 * pk_control_get_tid_finish:
 * @control: a valid #PkControl instance
 * @result: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function. 
 *
 * Return value: the ID, or %NULL if unset
 **/
gchar *
pk_control_get_tid_finish (PkControl *control, GAsyncResult *result, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_tid_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/**
 * pk_control_get_property:
 **/
static void
pk_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
//	PkControl *control = PK_CONTROL (object);
//	PkControlPrivate *priv = control->priv;

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
 * pk_control_set_property:
 **/
static void
pk_control_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
//	PkControl *control = PK_CONTROL (object);
//	PkControlPrivate *priv = control->priv;

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
 * pk_control_class_init:
 * @klass: The PkControlClass
 **/
static void
pk_control_class_init (PkControlClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_control_get_property;
	object_class->set_property = pk_control_set_property;
	object_class->finalize = pk_control_finalize;

	/**
	 * PkControl:id:
	 */
	pspec = g_param_spec_string ("id", NULL,
				     "The full control_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * PkControl::changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the control data may have changed.
	 **/
	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkControlPrivate));
}

/**
 * pk_control_init:
 * @control: This class instance
 **/
static void
pk_control_init (PkControl *control)
{
	GError *error = NULL;
	control->priv = PK_CONTROL_GET_PRIVATE (control);

	/* check dbus connections, exit if not valid */
	control->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* get a connection to the engine object */
	control->priv->proxy = dbus_g_proxy_new_for_name (control->priv->connection,
							  PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL)
		egg_error ("Cannot connect to PackageKit.");

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (control->priv->proxy, INT_MAX);
}

/**
 * pk_control_finalize:
 * @object: The object to finalize
 **/
static void
pk_control_finalize (GObject *object)
{
	PkControl *control = PK_CONTROL (object);
	PkControlPrivate *priv = control->priv;

	g_object_unref (G_OBJECT (priv->proxy));

	G_OBJECT_CLASS (pk_control_parent_class)->finalize (object);
}

/**
 * pk_control_new:
 *
 * Return value: a new PkControl object.
 **/
PkControl *
pk_control_new (void)
{
	PkControl *control;
	control = g_object_new (PK_TYPE_CONTROL, NULL);
	return PK_CONTROL (control);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
pk_control_test_get_tid_cb (GObject *object, GAsyncResult *result, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	const gchar *tid = NULL;

	tid = pk_control_get_tid_finish (control, result, &error);
	if (tid == NULL) {
		egg_test_failed (test, "failed to get transaction: %s", error->message);
		g_error_free (error);
		return;
	}

	egg_debug ("tid = %s", tid);
	egg_test_loop_quit (test);
}

void
pk_control_test (EggTest *test)
{
	PkControl *control;

	if (!egg_test_start (test, "PkControl"))
		return;

	/************************************************************/
	egg_test_title (test, "get control");
	control = pk_control_new ();
	egg_test_assert (test, control != NULL);

	/************************************************************/
	egg_test_title (test, "get TID async");
	pk_control_get_tid_async (control, NULL, (GAsyncReadyCallback) pk_control_test_get_tid_cb, test);
	egg_test_loop_wait (test, 5000);
	egg_test_success (test, "got tid in %i", egg_test_elapsed (test));

	g_object_unref (control);
out:
	egg_test_end (test);
}
#endif

