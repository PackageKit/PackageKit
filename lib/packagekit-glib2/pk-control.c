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
#include <packagekit-glib2/pk-version.h>

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
	DBusGProxyCall		*call;
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	gboolean		 version_major;
	gboolean		 version_minor;
	gboolean		 version_micro;
};

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_VERSION_MAJOR,
	PROP_VERSION_MINOR,
	PROP_VERSION_MICRO,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkControl, pk_control, G_TYPE_OBJECT)

typedef struct {
	PkControl		*control;
	GCancellable		*cancellable;
	gchar			*tid;
	GSimpleAsyncResult	*res;
	DBusGProxyCall		*call;
} PkControlState;

/***************************************************************************************************/

/**
 * pk_control_get_tid_state_finish:
 **/
static void
pk_control_get_tid_state_finish (PkControlState *state, GError *error)
{
	/* remove weak ref */
	if (state->control != NULL)
		g_object_remove_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* cancel */
	if (state->cancellable != NULL) {
		g_cancellable_cancel (state->cancellable);
		g_object_unref (state->cancellable);
	}

	/* get result */
	if (state->tid != NULL) {
		g_simple_async_result_set_op_res_gpointer (state->res, g_strdup (state->tid), g_free);
	} else {
		g_simple_async_result_set_from_error (state->res, error);
		g_error_free (error);
	}

	/* complete */
	g_simple_async_result_complete_in_idle (state->res);

	/* deallocate */
	g_object_unref (state->res);
	g_slice_free (PkControlState, state);
}

/**
 * pk_control_get_tid_cb:
 **/
static void
pk_control_get_tid_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControlState *state)
{
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
	GSimpleAsyncResult *res;
	PkControlState *state;

	g_return_if_fail (PK_IS_CONTROL (control));
	g_return_if_fail (callback != NULL);

	res = g_simple_async_result_new (G_OBJECT (control), callback, user_data, pk_control_get_tid_async);

	/* save state */
	state = g_slice_new0 (PkControlState);
	state->res = g_object_ref (res);
	state->cancellable = cancellable;
	state->control = control;
	state->call = NULL;
	state->tid = NULL;
	g_object_add_weak_pointer (G_OBJECT (state->control), (gpointer) &state->control);

	/* call D-Bus method async */
	state->call = dbus_g_proxy_begin_call (control->priv->proxy, "GetTid",
					       (DBusGProxyCallNotify) pk_control_get_tid_cb, state,
					       NULL, G_TYPE_INVALID);
	g_object_unref (res);
}

/**
 * pk_control_get_tid_finish:
 * @control: a valid #PkControl instance
 * @res: the #GAsyncResult
 * @error: A #GError or %NULL
 *
 * Gets the result from the asynchronous function. 
 *
 * Return value: the ID, or %NULL if unset
 **/
gchar *
pk_control_get_tid_finish (PkControl *control, GAsyncResult *res, GError **error)
{
	GSimpleAsyncResult *simple;
	gpointer source_tag;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (res), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (res);
	source_tag = g_simple_async_result_get_source_tag (simple);

	g_return_val_if_fail (source_tag == pk_control_get_tid_async, NULL);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (simple);
}

/***************************************************************************************************/

/**
 * pk_control_set_properties_collect_cb:
 **/
static void
pk_control_set_properties_collect_cb (const char *key, const GValue *value, PkControl *control)
{
	if (g_strcmp0 (key, "version-major") == 0)
		control->priv->version_major = g_value_get_uint (value);
	else if (g_strcmp0 (key, "version-minor") == 0)
		control->priv->version_minor = g_value_get_uint (value);
	else if (g_strcmp0 (key, "version-micro") == 0)
		control->priv->version_micro = g_value_get_uint (value);
	else
		egg_warning ("unhandled property '%s'", key);
}

/**
 * pk_control_set_properties_cb:
 **/
static void
pk_control_set_properties_cb (DBusGProxy *proxy, DBusGProxyCall *call, PkControl *control)
{
	GError *error = NULL;
	gboolean ret;
	GHashTable *hash;

	/* finished call */
	control->priv->call = NULL;

	/* we've sent this async */
	egg_debug ("got reply to request");

	/* get the result */
	ret = dbus_g_proxy_end_call (proxy, call, &error,
				     dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
				     &hash,
				     G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("failed to get properties: %s", error->message);
		return;
	}

	/* process results */
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_control_set_properties_collect_cb, control);
		g_hash_table_unref (hash);
	}
	g_object_unref (proxy);
}

/**
 * pk_control_set_properties:
 **/
static void
pk_control_set_properties (PkControl *control)
{
	DBusGProxy *proxy;

	/* connect to the correct path for properties */
	proxy = dbus_g_proxy_new_for_name (control->priv->connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.DBus.Properties");
	if (proxy == NULL) {
		egg_warning ("Couldn't connect to proxy");
		return;
	}

	/* does an async call, so properties may not be set until some time after the object is setup */
	control->priv->call = dbus_g_proxy_begin_call (proxy, "GetAll",
						       (DBusGProxyCallNotify) pk_control_set_properties_cb, control, NULL,
						       G_TYPE_STRING, "org.freedesktop.PackageKit",
					               G_TYPE_INVALID);
}

/**
 * pk_control_get_property:
 **/
static void
pk_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkControl *control = PK_CONTROL (object);
	PkControlPrivate *priv = control->priv;

	switch (prop_id) {
	case PROP_VERSION_MAJOR:
		g_value_set_uint (value, priv->version_major);
		break;
	case PROP_VERSION_MINOR:
		g_value_set_uint (value, priv->version_minor);
		break;
	case PROP_VERSION_MICRO:
		g_value_set_uint (value, priv->version_micro);
		break;
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
	switch (prop_id) {
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
	 * PkControl:version-major:
	 */
	pspec = g_param_spec_uint ("version-major", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MAJOR, pspec);

	/**
	 * PkControl:version-minor:
	 */
	pspec = g_param_spec_uint ("version-minor", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MINOR, pspec);

	/**
	 * PkControl:version-micro:
	 */
	pspec = g_param_spec_uint ("version-micro", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MICRO, pspec);

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
	control->priv->call = NULL;

	/* check dbus connections, exit if not valid */
	control->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* we maintain a local copy */
	control->priv->version_major = 0;
	control->priv->version_minor = 0;
	control->priv->version_micro = 0;

	/* get a connection to the engine object */
	control->priv->proxy = dbus_g_proxy_new_for_name (control->priv->connection,
							  PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL)
		egg_error ("Cannot connect to PackageKit.");

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (control->priv->proxy, INT_MAX);

	/* get properties async if they exist */
if (0)	pk_control_set_properties (control);
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

	/* if we have a request in flight, cancel it */
	if (control->priv->call != NULL) {
		egg_warning ("cancel in flight call");
		dbus_g_proxy_cancel_call (control->priv->proxy, control->priv->call);
	}

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
pk_control_test_get_tid_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	const gchar *tid = NULL;

	/* get the result */
	tid = pk_control_get_tid_finish (control, res, &error);
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
	guint version;

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

#if 0
	/************************************************************/
	egg_test_title (test, "version major");
	g_object_get (control, "version-major", &version, NULL);
	egg_test_assert (test, (version == PK_MAJOR_VERSION));

	/************************************************************/
	egg_test_title (test, "version minor");
	g_object_get (control, "version-minor", &version, NULL);
	egg_test_assert (test, (version == PK_MINOR_VERSION));

	/************************************************************/
	egg_test_title (test, "version micro");
	g_object_get (control, "version-micro", &version, NULL);
	egg_test_assert (test, (version == PK_MICRO_VERSION));
#endif

	g_object_unref (control);
out:
	egg_test_end (test);
}
#endif

