/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <pk-debug.h>
#include "pk-conf.h"
#include "pk-engine.h"
#include "pk-backend-internal.h"
#include "pk-interface.h"

static guint exit_idle_time;
static PkEngine *engine = NULL;
static PkBackend *backend = NULL;

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
G_GNUC_WARN_UNUSED_RESULT static gboolean
pk_object_register (DBusGConnection *connection,
		    GObject	     *object,
		    GError **error)
{
	DBusGProxy *bus_proxy = NULL;
	guint request_name_result;
	gboolean ret;

	bus_proxy = dbus_g_proxy_new_for_name (connection,
					       DBUS_SERVICE_DBUS,
					       DBUS_PATH_DBUS,
					       DBUS_INTERFACE_DBUS);

	ret = dbus_g_proxy_call (bus_proxy, "RequestName", error,
				 G_TYPE_STRING, PK_DBUS_SERVICE,
				 G_TYPE_UINT, 0,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, &request_name_result,
				 G_TYPE_INVALID);
	if (error && *error) {
		pk_debug ("ERROR: %s", (*error)->message);
	}
	if (ret == FALSE) {
		/* abort as the DBUS method failed */
		pk_warning ("RequestName failed!");
		g_clear_error(error);
		g_set_error(error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_DENIED,
			    _("Acquiring D-Bus name %s failed due to security policies on this machine\n"
			      "This can happen for two reasons:\n"
			      "* The correct user is not launching the executable (usually root)\n"
			      "* The org.freedesktop.PackageKit.conf file is "
			      "not installed in the system /etc/dbus-1/system.d directory\n"), PK_DBUS_SERVICE);
		return FALSE;
	}

	/* free the bus_proxy */
	g_object_unref (G_OBJECT (bus_proxy));

	/* already running */
 	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_set_error(error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_DENIED, "Already running on this machine");
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
 * pk_main_timeout_check_cb:
 **/
static gboolean
pk_main_timeout_check_cb (PkEngine *engine)
{
	guint idle;
	idle = pk_engine_get_seconds_idle (engine);
	pk_debug ("idle is %i", idle);
	if (idle > exit_idle_time) {
		pk_warning ("exit!!");
		exit (0);
	}
	return TRUE;
}

/**
 * pk_main_sigint_handler:
 **/
static void
pk_main_sigint_handler (int sig)
{
	gboolean ret;
	pk_debug ("Handling SIGINT");

	/* restore default ASAP, as the finalisers might hang */
	signal (SIGINT, SIG_DFL);

	/* unlock the backend to call destroy - TODO: we shouldn't have to do this! */
	ret = pk_backend_unlock (backend);
	if (!ret) {
		pk_warning ("failed to unlock in finalise!");
	}

	/* cleanup */
	g_object_unref (backend);
	g_object_unref (engine);

	/* give the backend a sporting chance */
	g_usleep (500*1000);

	/* kill ourselves */
	pk_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	DBusGConnection *system_connection;
	gboolean ret;
	gboolean verbose = FALSE;
	gboolean disable_timer = FALSE;
	gboolean version = FALSE;
	gboolean use_daemon = FALSE;
	gboolean timed_exit = FALSE;
	gboolean immediate_exit = FALSE;
	gchar *backend_name = NULL;
	PkConf *conf = NULL;
	GError *error = NULL;
	GOptionContext *context;

	const GOptionEntry options[] = {
		{ "backend", '\0', 0, G_OPTION_ARG_STRING, &backend_name,
		  _("Backend to use"), NULL },
		{ "daemonize", '\0', 0, G_OPTION_ARG_NONE, &use_daemon,
		  _("Daemonize and detach"), NULL },
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "disable-timer", '\0', 0, G_OPTION_ARG_NONE, &disable_timer,
		  _("Disable the idle timer"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
		  _("Show version of installed program and exit"), NULL },
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  _("Exit after a small delay"), NULL },
		{ "immediate-exit", '\0', 0, G_OPTION_ARG_NONE, &immediate_exit,
		  _("Exit after a the engine has loaded"), NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("PackageKit daemon"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);

	if (version == TRUE) {
		g_print ("Version %s\n", VERSION);
		goto exit_program;
	}

	if (!g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();

	/* do stuff on ctrl-c */
	signal (SIGINT, pk_main_sigint_handler);

	/* we need to daemonize before we get a system connection */
	if (use_daemon == TRUE && daemon (0, 0)) {
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

	/* we don't actually need to do this, except it rules out the
	 * 'it works from the command line but not service activation' bugs */
	clearenv ();

	/* get values from the config file */
	conf = pk_conf_new ();
	exit_idle_time = pk_conf_get_int (conf, "ShutdownTimeout");
	pk_debug ("daemon shutdown set to %i seconds", exit_idle_time);

	if (backend_name == NULL) {
		backend_name = pk_conf_get_string (conf, "DefaultBackend");
		pk_debug ("using default backend %s", backend_name);
	}

	/* load our chosen backend */
	backend = pk_backend_new ();
	ret = pk_backend_set_name (backend, backend_name);
	g_free (backend_name);

	/* all okay? */
	if (ret == FALSE) {
		pk_error ("cannot continue, backend invalid");
	}

	/* create a new engine object */
	engine = pk_engine_new ();

	if (!pk_object_register (system_connection, G_OBJECT (engine), &error)) {
		g_print (_("Error trying to start: %s\n"), error->message);
		g_error_free (error);
		goto out;
	}

	loop = g_main_loop_new (NULL, FALSE);

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (timed_exit == TRUE) {
		g_timeout_add_seconds (20, (GSourceFunc) timed_exit_cb, loop);
	}

	/* only poll every 10 seconds when we are alive */
	if (exit_idle_time != 0 && disable_timer == FALSE) {
		g_timeout_add_seconds (5, (GSourceFunc) pk_main_timeout_check_cb, engine);
	}

	/* immediatly exit */
	if (immediate_exit == TRUE) {
		g_timeout_add (50, (GSourceFunc) timed_exit_cb, loop);
	}

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

out:
	g_object_unref (conf);
	g_object_unref (engine);
	g_object_unref (backend);

exit_program:
	return 0;
}
