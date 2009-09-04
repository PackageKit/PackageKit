/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <string.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-control-sync.h>
#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-version.h>

#include "egg-debug.h"

static void     pk_control_sync_finalize	(GObject     *object);

#define PK_CONTROL_SYNC_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONTROL_SYNC, PkControlSyncPrivate))

/**
 * PkControlSyncPrivate:
 *
 * Private #PkControlSync data
 **/
struct _PkControlSyncPrivate
{
	DBusGProxy		*proxy;
};

static gpointer pk_control_sync_object = NULL;

G_DEFINE_TYPE (PkControlSync, pk_control_sync, PK_TYPE_CONTROL)

/**
 * pk_control_sync_fixup_dbus_error:
 **/
static void
pk_control_sync_fixup_dbus_error (GError *error)
{
	g_return_if_fail (error != NULL);

	/* hardcode domain */
	error->domain = PK_CONTROL_ERROR;

	/* find a better failure code */
	if (error->code == DBUS_GERROR_SPAWN_CHILD_EXITED)
		error->code = PK_CONTROL_ERROR_CANNOT_START_DAEMON;
	else
		error->code = PK_CONTROL_ERROR_FAILED;
}

/**
 * pk_control_sync_get_daemon_state:
 * @control_sync: a valid #PkControlSync instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The engine state debugging output
 *
 * Return value: a string of debugging data of unspecified format, unref wih g_free()
 **/
gchar *
pk_control_sync_get_daemon_state (PkControlSync *control, GError **error)
{
	gboolean ret;
	gchar *state = NULL;

	g_return_val_if_fail (PK_IS_CONTROL_SYNC (control), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* call D-Bus sync */
	ret = dbus_g_proxy_call (control->priv->proxy, "GetDaemonState", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &state,
				 G_TYPE_INVALID);
	if (!ret) {
		/* fix up the D-Bus error */
		if (error != NULL)
			pk_control_sync_fixup_dbus_error (*error);
		goto out;
	}
out:
	return state;
}

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	gboolean	 ret;
} PkControlSyncHelper;

/**
 * pk_control_sync_properties_cb:
 **/
static void
pk_control_sync_properties_cb (PkControlSync *control, GAsyncResult *res, PkControlSyncHelper *sync)
{
	/* get the result */
	sync->ret = pk_control_get_properties_finish (PK_CONTROL(control), res, sync->error);
	g_main_loop_quit (sync->loop);
}

/**
 * pk_control_sync_get_properties:
 * @control: a valid #PkControlSync instance
 * @error: A #GError or %NULL
 *
 * Gets the properties the daemon supports.
 * Warning: this function is synchronous, and may block. Do not use it in GUI
 * applications.
 *
 * Return value: %TRUE if the properties were set correctly
 **/
gboolean
pk_control_sync_get_properties (PkControlSync *control, GError **error)
{
	gboolean ret;
	PkControlSyncHelper *sync;

	g_return_val_if_fail (PK_IS_CONTROL_SYNC (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	sync = g_new0 (PkControlSyncHelper, 1);
	sync->loop = g_main_loop_new (NULL, FALSE);
	sync->error = error;

	/* run async method */
	pk_control_get_properties_async (PK_CONTROL(control), NULL, (GAsyncReadyCallback) pk_control_sync_properties_cb, sync);
	g_main_loop_run (sync->loop);

	ret = sync->ret;

	/* free temp object */
	g_main_loop_unref (sync->loop);
	g_free (sync);

	return ret;
}

/**
 * pk_control_sync_class_init:
 * @klass: The PkControlSyncClass
 **/
static void
pk_control_sync_class_init (PkControlSyncClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_control_sync_finalize;
	g_type_class_add_private (klass, sizeof (PkControlSyncPrivate));
}

/**
 * pk_control_sync_init:
 * @control_sync: This class instance
 **/
static void
pk_control_sync_init (PkControlSync *control)
{
	DBusGConnection *connection;
	GError *error = NULL;

	control->priv = PK_CONTROL_SYNC_GET_PRIVATE (control);

	/* get connection */
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* get a connection to the main interface */
	control->priv->proxy = dbus_g_proxy_new_for_name (connection,
							  PK_DBUS_SERVICE, PK_DBUS_PATH,
							  PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL)
		egg_error ("Cannot connect to PackageKit.");
}

/**
 * pk_control_sync_finalize:
 * @object: The object to finalize
 **/
static void
pk_control_sync_finalize (GObject *object)
{
	PkControlSync *control = PK_CONTROL_SYNC (object);
	g_object_unref (G_OBJECT (control->priv->proxy));
	G_OBJECT_CLASS (pk_control_sync_parent_class)->finalize (object);
}

/**
 * pk_control_sync_new:
 *
 * Return value: a new PkControlSync object.
 **/
PkControlSync *
pk_control_sync_new (void)
{
	if (pk_control_sync_object != NULL) {
		g_object_ref (pk_control_sync_object);
	} else {
		pk_control_sync_object = g_object_new (PK_TYPE_CONTROL_SYNC, NULL);
		g_object_add_weak_pointer (pk_control_sync_object, &pk_control_sync_object);
	}
	return PK_CONTROL_SYNC (pk_control_sync_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_control_sync_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkControlSync *control;
	GError *error = NULL;
	gboolean ret;
	gchar *text;
	PkBitfield roles;

	if (!egg_test_start (test, "PkControlSync"))
		return;

	/************************************************************/
	egg_test_title (test, "get control_sync");
	control = pk_control_sync_new ();
	egg_test_assert (test, control != NULL);

	/************************************************************/
	egg_test_title (test, "get properties sync");
	ret = pk_control_sync_get_properties (control, &error);
	if (!ret)
		egg_test_failed (test, "failed to get properties: %s", error->message);

	/* get data */
	g_object_get (control,
		      "roles", &roles,
		      NULL);

	/* check data */
	text = pk_role_bitfield_to_text (roles);
	if (g_strcmp0 (text, "cancel;get-depends;get-details;get-files;get-packages;get-repo-list;"
			     "get-requires;get-update-detail;get-updates;install-files;install-packages;"
			     "refresh-cache;remove-packages;repo-enable;repo-set-data;resolve;rollback;"
			     "search-details;search-file;search-group;search-name;update-packages;update-system;"
			     "what-provides;download-packages;get-distro-upgrades;simulate-install-packages;"
			     "simulate-remove-packages;simulate-update-packages") != 0) {
		egg_test_failed (test, "data incorrect: %s", text);
	}
	egg_test_success (test, "got correct roles");
	g_free (text);

	g_object_unref (control);
	egg_test_end (test);
}
#endif

