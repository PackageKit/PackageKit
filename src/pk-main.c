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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <packagekit-glib2/pk-debug.h>

#if GLIB_CHECK_VERSION(2,29,19)
 #include <glib-unix.h>
#endif

#include "egg-dbus-monitor.h"

#include "pk-conf.h"
#include "pk-engine.h"
#include "pk-syslog.h"
#include "pk-transaction.h"
#include "pk-backend.h"
#include "org.freedesktop.PackageKit.h"

static guint exit_idle_time;
static GMainLoop *loop;

/**
 * pk_object_register:
 * @connection: What we want to register to
 * @object: The GObject we want to register
 *
 * Register org.freedesktop.PackageKit on the system bus.
 * This function MUST be called before DBUS service will work.
 *
 * Return value: success
 **/
G_GNUC_WARN_UNUSED_RESULT static gboolean
pk_object_register (DBusGConnection *connection, GObject *object, GError **error)
{
	DBusGProxy *bus_proxy = NULL;
	guint request_name_result;
	gboolean ret;
	gchar *message;

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

	/* free the bus_proxy */
	g_object_unref (G_OBJECT (bus_proxy));

	/* abort as the DBUS method failed */
	if (!ret) {
		g_warning ("RequestName failed!");
		g_clear_error (error);
		message = g_strdup_printf ("%s\n%s\n* %s\n* %s '%s'\n",
					   /* TRANSLATORS: failed due to DBus security */
					   _("Startup failed due to security policies on this machine."),
					   /* TRANSLATORS: only two ways this can fail... */
					   _("This can happen for two reasons:"),
					   /* TRANSLATORS: only allowed to be owned by root */
					   _("The correct user is not launching the executable (usually root)"),
					   /* TRANSLATORS: or we are installed in a prefix */
					   _("The org.freedesktop.PackageKit.conf file is not "
					     "installed in the system directory:"),
					     "/etc/dbus-1/system.d");
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_DENIED, "%s", message);
		g_free (message);
		return FALSE;
	}

	/* already running */
 	if (request_name_result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_DENIED,
			     "Already running on this machine");
		return FALSE;
	}

	dbus_g_object_type_install_info (PK_TYPE_ENGINE, &dbus_glib_pk_engine_object_info);
	dbus_g_error_domain_register (PK_ENGINE_ERROR, NULL, PK_ENGINE_TYPE_ERROR);
	dbus_g_error_domain_register (PK_TRANSACTION_ERROR, NULL, PK_TRANSACTION_TYPE_ERROR);
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
timed_exit_cb (GMainLoop *mainloop)
{
	g_main_loop_quit (mainloop);
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
	g_debug ("idle is %i", idle);
	if (idle > exit_idle_time) {
		g_warning ("exit!!");
		g_main_loop_quit (loop);
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_main_quit_cb:
 **/
static void
pk_main_quit_cb (PkEngine *engine, GMainLoop *mainloop)
{
	g_debug ("engine quit");
	g_main_loop_quit (mainloop);
}

#if GLIB_CHECK_VERSION(2,29,19)

/**
 * pk_main_sigint_cb:
 **/
static gboolean
pk_main_sigint_cb (gpointer user_data)
{
	g_debug ("Handling SIGINT");
	g_main_loop_quit (loop);
	return FALSE;
}

#else

/**
 * pk_main_sigint_handler:
 **/
static void
pk_main_sigint_handler (int sig)
{
	g_debug ("Handling SIGINT");

	/* restore default ASAP, as the finalisers might hang */
	signal (SIGINT, SIG_DFL);

	/* exit loop */
	g_main_loop_quit (loop);
}

#endif

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	DBusGConnection *system_connection;
	EggDbusMonitor *monitor;
	gboolean ret;
	gboolean disable_timer = FALSE;
	gboolean version = FALSE;
	gboolean use_daemon = FALSE;
	gboolean timed_exit = FALSE;
	gboolean immediate_exit = FALSE;
	gboolean keep_environment = FALSE;
	gboolean do_logging = FALSE;
	gchar *backend_name = NULL;
	gchar **backend_names = NULL;
	guint i;
	PkEngine *engine = NULL;
	PkBackend *backend = NULL;
	PkConf *conf = NULL;
	PkSyslog *syslog = NULL;
	GError *error = NULL;
	GOptionContext *context;
	guint timer_id = 0;

	const GOptionEntry options[] = {
		{ "backend", '\0', 0, G_OPTION_ARG_STRING, &backend_name,
		  /* TRANSLATORS: a backend is the system package tool, e.g. yum, apt */
		  _("Packaging backend to use, e.g. dummy"), NULL },
		{ "daemonize", '\0', 0, G_OPTION_ARG_NONE, &use_daemon,
		  /* TRANSLATORS: if we should run in the background */
		  _("Daemonize and detach from the terminal"), NULL },
		{ "disable-timer", '\0', 0, G_OPTION_ARG_NONE, &disable_timer,
		  /* TRANSLATORS: if we should not monitor how long we are inactive for */
		  _("Disable the idle timer"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &version,
		  /* TRANSLATORS: show version */
		  _("Show version and exit"), NULL },
		{ "timed-exit", '\0', 0, G_OPTION_ARG_NONE, &timed_exit,
		  /* TRANSLATORS: exit after we've started up, used for user profiling */
		  _("Exit after a small delay"), NULL },
		{ "immediate-exit", '\0', 0, G_OPTION_ARG_NONE, &immediate_exit,
		  /* TRANSLATORS: exit straight away, used for automatic profiling */
		  _("Exit after the engine has loaded"), NULL },
		{ "keep-environment", '\0', 0, G_OPTION_ARG_NONE, &keep_environment,
		  /* TRANSLATORS: don't unset environment variables, used for debugging */
		  _("Don't clear environment on startup"), NULL },
		{ NULL }
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();

	/* TRANSLATORS: describing the service that is running */
	context = g_option_context_new (_("PackageKit service"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, pk_debug_get_option_group ());
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (version) {
		g_print ("Version %s\n", VERSION);
		goto exit_program;
	}

	/* check if an instance is already running */
	monitor = egg_dbus_monitor_new ();
	egg_dbus_monitor_assign (monitor, EGG_DBUS_MONITOR_SYSTEM, PK_DBUS_SERVICE);
	ret = egg_dbus_monitor_is_connected (monitor);
	g_object_unref (monitor);
	if (ret) {
		g_print ("Already running service which provides %s\n", PK_DBUS_SERVICE);
		goto exit_program;
	}

#if GLIB_CHECK_VERSION(2,29,19)
	/* do stuff on ctrl-c */
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				pk_main_sigint_cb,
				loop,
				NULL);
#else
	signal (SIGINT, pk_main_sigint_handler);
#endif

	/* we need to daemonize before we get a system connection */
	if (use_daemon && daemon (0, 0)) {
		g_print ("Could not daemonize: %s\n", g_strerror (errno));
		goto exit_program;
	}

	/* don't let GIO start it's own session bus: http://bugzilla.gnome.org/show_bug.cgi?id=526454 */
	setenv ("GIO_USE_VFS", "local", 1);

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		/* TRANSLATORS: fatal error, dbus is not running */
		g_print ("%s: %s\n", _("Cannot connect to the system bus"), error->message);
		g_error_free (error);
		goto exit_program;
	}

	/* we don't actually need to do this, except it rules out the
	 * 'it works from the command line but not service activation' bugs */
#ifdef HAVE_CLEARENV
	g_debug ("keep_environment: %i\n", keep_environment);
	if (!keep_environment)
		clearenv ();
#endif

	/* get values from the config file */
	conf = pk_conf_new ();

	/* log the startup */
	syslog = pk_syslog_new ();
	pk_syslog_add (syslog, PK_SYSLOG_TYPE_INFO, "daemon start");

	/* do we log? */
	do_logging = pk_conf_get_bool (conf, "TransactionLogging");
	g_debug ("Log all transactions: %i", do_logging);

	/* after how long do we timeout? */
	exit_idle_time = pk_conf_get_int (conf, "ShutdownTimeout");
	g_debug ("daemon shutdown set to %i seconds", exit_idle_time);

	if (backend_name == NULL) {
		backend_name = pk_conf_get_string (conf, "DefaultBackend");
		g_debug ("using default backend %s", backend_name);
	}
	backend_names = g_strsplit (backend_name, ",", -1);

	/* try to load our chosen backends in order */
	backend = pk_backend_new ();
	for (i=0; backend_names[i] != NULL; i++) {
		ret = pk_backend_set_name (backend, backend_names[i], &error);
		if (ret)
			break;
		g_warning ("backend %s invalid: %s",
			   backend_names[i],
			   error->message);
		g_clear_error (&error);
	}
	if (!ret) {
		/* TRANSLATORS: cannot load the backend the user specified */
		g_print ("%s: %s\n",
			 _("Failed to load any of the specified backends:"),
			 backend_name);
		goto out;
	}

	pk_backend_set_keep_environment (backend, keep_environment);

	loop = g_main_loop_new (NULL, FALSE);

	/* create a new engine object */
	engine = pk_engine_new ();
	g_signal_connect (engine, "quit",
			  G_CALLBACK (pk_main_quit_cb), loop);

	if (!pk_object_register (system_connection, G_OBJECT (engine), &error)) {
		/* TRANSLATORS: cannot register on system bus, unknown reason -- geeky error follows */
		g_print ("%s %s\n", _("Error trying to start:"), error->message);
		g_error_free (error);
		goto out;
	}

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (timed_exit)
		g_timeout_add_seconds (20, (GSourceFunc) timed_exit_cb, loop);

	/* only poll every 10 seconds when we are alive */
	if (exit_idle_time != 0 && !disable_timer) {
		timer_id = g_timeout_add_seconds (5, (GSourceFunc) pk_main_timeout_check_cb, engine);
#if GLIB_CHECK_VERSION(2,25,8)
		g_source_set_name_by_id (timer_id, "[PkMain] main poll");
#endif
	}

	/* immediatly exit */
	if (immediate_exit)
		g_timeout_add (50, (GSourceFunc) timed_exit_cb, loop);

	/* run until quit */
	g_main_loop_run (loop);

out:
	/* log the shutdown */
	pk_syslog_add (syslog, PK_SYSLOG_TYPE_INFO, "daemon quit");

	if (timer_id > 0)
		g_source_remove (timer_id);

	if (loop != NULL)
		g_main_loop_unref (loop);
	g_object_unref (syslog);
	g_object_unref (conf);
	if (engine != NULL)
		g_object_unref (engine);
	g_object_unref (backend);
	g_strfreev (backend_names);
	g_free (backend_name);

exit_program:
	return 0;
}
