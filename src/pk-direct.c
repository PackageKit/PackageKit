/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include <locale.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/pk-debug.h>

#include "pk-cleanup.h"
#include "pk-backend.h"
#include "pk-shared.h"

typedef struct {
	GMainLoop	*loop;
} PkDirectHelper;

/**
 * pk_direct_sigint_cb:
 **/
static gboolean
pk_direct_sigint_cb (gpointer user_data)
{
	PkDirectHelper *helper = (PkDirectHelper *) user_data;
	g_debug ("Handling SIGINT");
	g_main_loop_quit (helper->loop);
	return FALSE;
}

/**
 * pk_direct_finished_cb:
 **/
static void
pk_direct_finished_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkExitEnum exit_enum = GPOINTER_TO_UINT (object);
	PkDirectHelper *helper = (PkDirectHelper *) user_data;

	g_print ("Exit code: %s\n", pk_exit_enum_to_string (exit_enum));
	g_main_loop_quit (helper->loop);
}

/**
 * pk_direct_percentage_cb:
 **/
static void
pk_direct_percentage_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	guint percentage = GPOINTER_TO_UINT (object);
	g_print ("Done: %i%%\n", percentage);
}

/**
 * pk_direct_status_changed_cb:
 **/
static void
pk_direct_status_changed_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkStatusEnum status_enum = GPOINTER_TO_UINT (object);
	g_print ("Status: %s\n", pk_status_enum_to_string (status_enum));
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GOptionContext *context;
	PkDirectHelper helper;
	gboolean ret = TRUE;
	gint retval = EXIT_SUCCESS;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *backend_name = NULL;
	_cleanup_free_ gchar *conf_filename = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	_cleanup_object_unref_ PkBackend *backend = NULL;
	_cleanup_object_unref_ PkBackendJob *job = NULL;

	const GOptionEntry options[] = {
		{ "backend", '\0', 0, G_OPTION_ARG_STRING, &backend_name,
		  /* TRANSLATORS: a backend is the system package tool, e.g. yum, apt */
		  _("Packaging backend to use, e.g. dummy"), NULL },
		{ NULL }
	};

	helper.loop = NULL;
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* TRANSLATORS: describing the service that is running */
	context = g_option_context_new (_("PackageKit"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, pk_debug_get_option_group ());
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* get values from the config file */
	conf = g_key_file_new ();
	conf_filename = pk_util_get_config_filename ();
	ret = g_key_file_load_from_file (conf, conf_filename,
					 G_KEY_FILE_NONE, &error);
	if (!ret) {
		g_print ("Failed to load config file: %s\n", error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* override the backend name */
	if (backend_name != NULL)
		g_key_file_set_string (conf, "Daemon", "DefaultBackend", backend_name);

	/* resolve 'auto' to an actual name */
	backend_name = g_key_file_get_string (conf, "Daemon", "DefaultBackend", NULL);
	if (g_strcmp0 (backend_name, "auto") == 0) {
		if (!pk_util_set_auto_backend (conf, &error)) {
			g_print ("Failed to resolve auto: %s\n", error->message);
			retval = EXIT_FAILURE;
			goto out;
		}
	}

	/* do stuff on ctrl-c */
	helper.loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add_full (G_PRIORITY_DEFAULT, SIGINT,
				pk_direct_sigint_cb, &helper, NULL);

	/* load the backend */
	backend = pk_backend_new (conf);
	ret = pk_backend_load (backend, &error);
	if (!ret) {
		/* TRANSLATORS: cannot load the backend the user specified */
		g_print ("%s: %s", _("Failed to load the backend"), error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* just support refreshing the cache for now */
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);
	pk_backend_job_set_vfunc (job, PK_BACKEND_SIGNAL_FINISHED,
				  pk_direct_finished_cb, &helper);
	pk_backend_job_set_vfunc (job, PK_BACKEND_SIGNAL_PERCENTAGE,
				  pk_direct_percentage_cb, &helper);
	pk_backend_job_set_vfunc (job, PK_BACKEND_SIGNAL_STATUS_CHANGED,
				  pk_direct_status_changed_cb, &helper);
	g_print ("Refreshing cache...\n");
	pk_backend_start_job (backend, job);
	pk_backend_refresh_cache (backend, job, TRUE);
	g_main_loop_run (helper.loop);
	pk_backend_stop_job (backend, job);
	g_print ("Done!\n");
out:
	if (helper.loop != NULL)
		g_main_loop_unref (helper.loop);
	return retval;
}
