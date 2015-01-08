/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2011 Richard Hughes <richard@hughsie.com>
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
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/pk-debug.h>

#include "pk-cleanup.h"
#include "pk-engine.h"
#include "pk-shared.h"
#include "pk-transaction.h"

typedef struct {
	GMainLoop	*loop;
	PkEngine	*engine;
	guint		 exit_idle_time;
	guint		 timer_id;
} PkMainHelper;

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
pk_main_timeout_check_cb (PkMainHelper *helper)
{
	guint idle;
	idle = pk_engine_get_seconds_idle (helper->engine);
	g_debug ("idle is %i", idle);
	if (idle > helper->exit_idle_time) {
		g_main_loop_quit (helper->loop);
		helper->timer_id = 0;
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

/**
 * pk_main_sigint_cb:
 **/
static gboolean
pk_main_sigint_cb (gpointer user_data)
{
	GMainLoop *mainloop = (GMainLoop *) user_data;
	g_debug ("Handling SIGINT");
	g_main_loop_quit (mainloop);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop = NULL;
	GOptionContext *context;
	gboolean ret = TRUE;
	gboolean disable_timer = FALSE;
	gboolean version = FALSE;
	gboolean timed_exit = FALSE;
	gboolean immediate_exit = FALSE;
	gboolean keep_environment = FALSE;
	gint exit_idle_time;
	guint timer_id = 0;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *backend_name = NULL;
	_cleanup_free_ gchar *conf_filename = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	_cleanup_object_unref_ PkEngine *engine = NULL;

	const GOptionEntry options[] = {
		{ "backend", '\0', 0, G_OPTION_ARG_STRING, &backend_name,
		  /* TRANSLATORS: a backend is the system package tool, e.g. hif, apt */
		  _("Packaging backend to use, e.g. dummy"), NULL },
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
	openlog ("PackageKit", LOG_NDELAY, LOG_USER);

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init ();
#endif

	/* set the default thread explicitly */
	pk_is_thread_default ();

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

	/* we don't actually need to do this, except it rules out the
	 * 'it works from the command line but not service activation' bugs */
#ifdef HAVE_CLEARENV
	if (!keep_environment)
		clearenv ();
#endif

	/* get values from the config file */
	conf = g_key_file_new ();
	conf_filename = pk_util_get_config_filename ();
	ret = g_key_file_load_from_file (conf, conf_filename,
					 G_KEY_FILE_NONE, &error);
	if (!ret) {
		g_print ("Failed to load config file: %s", error->message);
		goto out;
	}
	g_key_file_set_boolean (conf, "Daemon", "KeepEnvironment", keep_environment);

	/* log the startup */
	syslog (LOG_DAEMON | LOG_DEBUG, "daemon start");

	/* after how long do we timeout? */
	exit_idle_time = g_key_file_get_integer (conf, "Daemon", "ShutdownTimeout", NULL);
	g_debug ("daemon shutdown set to %i seconds", exit_idle_time);

	/* override the backend name */
	if (backend_name != NULL) {
		g_key_file_set_string (conf,
				       "Daemon",
				       "DefaultBackend",
				       backend_name);
	}

	/* resolve 'auto' to an actual name */
	backend_name = g_key_file_get_string (conf, "Daemon", "DefaultBackend", NULL);
	if (backend_name == NULL || g_strcmp0 (backend_name, "auto") == 0) {
		ret  = pk_util_set_auto_backend (conf, &error);
		if (!ret) {
			g_print ("Failed to resolve auto: %s", error->message);
			goto out;
		}
	}

	loop = g_main_loop_new (NULL, FALSE);

	/* create a new engine object */
	engine = pk_engine_new (conf);
	g_signal_connect (engine, "quit",
			  G_CALLBACK (pk_main_quit_cb), loop);

	/* do stuff on ctrl-c */
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				pk_main_sigint_cb,
				loop,
				NULL);

	/* load the backend */
	ret = pk_engine_load_backend (engine, &error);
	if (!ret) {
		/* TRANSLATORS: cannot load the backend the user specified */
		g_print ("Failed to load the backend: %s", error->message);
		goto out;
	}

	/* Only timeout and close the mainloop if we have specified it
	 * on the command line */
	if (timed_exit)
		g_timeout_add_seconds (20, (GSourceFunc) timed_exit_cb, loop);

	/* only poll when we are alive */
	if (exit_idle_time > 0 && !disable_timer) {
		PkMainHelper helper;
		helper.engine = engine;
		helper.exit_idle_time = exit_idle_time;
		helper.loop = loop;
		helper.timer_id = g_timeout_add_seconds (5, (GSourceFunc) pk_main_timeout_check_cb, &helper);
		g_source_set_name_by_id (helper.timer_id, "[PkMain] main poll");
		timer_id = helper.timer_id;
	}

	/* immediatly exit */
	if (immediate_exit)
		g_timeout_add (50, (GSourceFunc) timed_exit_cb, loop);

	/* run until quit */
	g_main_loop_run (loop);
out:
	/* log the shutdown */
	syslog (LOG_DAEMON | LOG_DEBUG, "daemon quit");
	closelog ();

	if (timer_id > 0)
		g_source_remove (timer_id);
	if (loop != NULL)
		g_main_loop_unref (loop);
exit_program:
	return 0;
}
