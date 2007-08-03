/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "pk-debug.h"
#include "pk-engine.h"
#include "pk-interface.h"

/**
 * pk_object_register:
 * @connection: What we want to register to
 * @object: The GObject we want to register
 *
 * Register org.freedesktop.PowerManagement on the session bus.
 * This function MUST be called before DBUS service will work.
 *
 * Return value: success
 **/
static gboolean
pk_object_register (DBusGConnection *connection,
		    GObject	     *object)
{
	DBusGProxy *bus_proxy = NULL;
	GError *error = NULL;
	guint request_name_result;
	gboolean ret;

	bus_proxy = dbus_g_proxy_new_for_name (connection,
					       DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS,
					       DBUS_INTERFACE_DBUS);

	ret = dbus_g_proxy_call (bus_proxy, "RequestName", &error,
				 G_TYPE_STRING, PK_DBUS_SERVICE,
				 G_TYPE_UINT, 0,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &request_name_result,
				 G_TYPE_INVALID);
	if (error) {
		pk_debug ("ERROR: %s", error->message);
		g_error_free (error);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RequestName failed!");
		return FALSE;
	}

	/* free the bus_proxy */
	g_object_unref (G_OBJECT (bus_proxy));

	/* already running */
 	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		return FALSE;
	}

	dbus_g_object_type_install_info (PK_TYPE_ENGINE, &dbus_glib_pk_engine_object_info);
	dbus_g_error_domain_register (PK_ENGINE_ERROR, NULL, PK_ENGINE_TYPE_ERROR);
	dbus_g_connection_register_g_object (connection, PK_DBUS_PATH, object);

	return TRUE;
}

/**
 * timed_exit_cb:
 * @loop: The main loop
 *
 * Exits the main loop, which is helpful for valgrinding g-p-m.
 *
 * Return value: FALSE, as we don't want to repeat this action.
 **/
static gboolean
timed_exit_cb (GMainLoop *loop)
{
	g_main_loop_quit (loop);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	DBusGConnection *system_connection;
	gboolean verbose = FALSE;
	gboolean version = FALSE;
	gboolean no_daemon = FALSE;
	gboolean timed_exit = FALSE;
	gboolean immediate_exit = FALSE;
	PkEngine *engine = NULL;
	GError *error = NULL;
	GOptionContext *context;

	const GOptionEntry options[] = {
		{ "no-daemon", '\0', 0, G_OPTION_ARG_NONE, &no_daemon,
		  "Do not daemonize", NULL },
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
		  "Show version of installed program and exit", NULL },
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  "Exit after a small delay (for debugging)", NULL },
		{ "immediate-exit", '\0', 0, G_OPTION_ARG_NONE, &immediate_exit,
		  "Exit after a the engine has loaded (for debugging)", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("Console kit daemon"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);

	if (version == TRUE) {
		g_print ("Version %s\n", VERSION);
		goto unref_program;
	}

	if (!g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();

#if 0
{
const gchar *command;
command = "/usr/bin/apt-cache search gnome-power-manager";
//command = "/usr/bin/yum search gnome-power-manager";
pk_task_fork_helper (NULL, command);
g_warning ("we forked!!");
}
#endif

	/* we need to daemonize before we get a system connection */
	if (no_daemon == FALSE && daemon (0, 0)) {
		g_error ("Could not daemonize: %s", g_strerror (errno));
	}

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start "
			   "the dbus system service.\n"
			   "It is <b>strongly recommended</b> you reboot "
			   "your computer after starting this service.");
	}

	/* create a new engine object */
	engine = pk_engine_new ();

	if (!pk_object_register (system_connection, G_OBJECT (engine))) {
		g_error ("Already running on this machine");
		return 0;
	}

	loop = g_main_loop_new (NULL, FALSE);

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (timed_exit == TRUE) {
		g_timeout_add (1000 * 20, (GSourceFunc) timed_exit_cb, loop);
	}

	if (immediate_exit == FALSE) {
		g_main_loop_run (loop);
	}

	g_main_loop_unref (loop);
	g_object_unref (engine);

unref_program:
	return 0;
}
