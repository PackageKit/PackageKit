/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>

#include "egg-debug.h"

static gboolean verbose = FALSE;
static PkClient *client = NULL;
static GPtrArray *array = NULL;

/**
 * pk_monitor_repo_list_changed_cb:
 **/
static void
pk_monitor_repo_list_changed_cb (PkControl *control, gpointer data)
{
	g_print ("repo-list-changed\n");
}

/**
 * pk_monitor_updates_changed_cb:
 **/
static void
pk_monitor_updates_changed_cb (PkControl *control, gpointer data)
{
	g_print ("updates-changed\n");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkControl *control, gboolean connected, gpointer data)
{
	g_print ("daemon connected=%i\n", connected);
}

/**
 * pk_monitor_locked_cb:
 **/
static void
pk_monitor_locked_cb (PkControl *control, gboolean is_locked, gpointer data)
{
	if (is_locked)
		g_print ("backend locked\n");
	else
		g_print ("backend unlocked\n");
}

/**
 * pk_monitor_adopt_cb:
 **/
static void
pk_monitor_adopt_cb (PkClient *_client, GAsyncResult *res, const gchar *tid)
{
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_warning ("failed to adopt: %s", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	g_print ("%s\texit code: %s\n", tid, pk_exit_enum_to_text (exit_enum));
out:
	if (results != NULL)
		g_object_unref (results);
}

/**
 * pk_monitor_progress_cb:
 **/
static void
pk_monitor_progress_cb (PkProgress *progress, PkProgressType type, const gchar *tid)
{
	PkRoleEnum role;
	PkStatusEnum status;
	guint percentage;
	gboolean allow_cancel;
	gchar *package_id;

	/* get data */
	g_object_get (progress,
		      "role", &role,
		      "status", &status,
		      "percentage", &percentage,
		      "allow-cancel", &allow_cancel,
		      "package-id", &package_id,
		      NULL);

	if (type == PK_PROGRESS_TYPE_ROLE) {
		g_print ("%s\trole         %s\n", tid, pk_role_enum_to_text (role));
	} else if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_print ("%s\tpackage      %s\n", tid, package_id);
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_print ("%s\tpercentage   %i\n", tid, percentage);
	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		g_print ("%s\tallow_cancel %i\n", tid, allow_cancel);
	} else if (type == PK_PROGRESS_TYPE_STATUS) {
		g_print ("%s\tstatus       %s\n", tid, pk_status_enum_to_text (status));
	}
	g_free (package_id);
}

/**
 * pk_monitor_list_add:
 **/
static void
pk_monitor_list_add (const gchar *transaction_id)
{
	gchar *tid;

	/* adopt client */
	tid = g_strdup (transaction_id);
	pk_client_adopt_async (client, transaction_id, NULL,
			       (PkProgressCallback) pk_monitor_progress_cb, tid,
			       (GAsyncReadyCallback) pk_monitor_adopt_cb, tid);
	/* add tid to array */
	g_ptr_array_add (array, tid);
}

/**
 * pk_monitor_in_array:
 **/
static gboolean
pk_monitor_in_array (GPtrArray *_array, const gchar *text)
{
	guint i;
	const gchar *tmp;
	for (i=0; i<_array->len; i++) {
		tmp = g_ptr_array_index (_array, i);
		if (g_strcmp0 (text, tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_monitor_list_print:
 **/
static void
pk_monitor_list_print (gchar **list)
{
	guint i;
	gboolean ret;

	g_print ("Transactions:\n");
	if (list[0] == NULL) {
		g_print (" [none]\n");
		return;
	}
	for (i=0; list[i] != NULL; i++) {
		g_print (" %i\t%s\n", i+1, list[i]);

		/* check to see if tid is in array */
		ret = pk_monitor_in_array (array, list[i]);
		if (!ret)
			pk_monitor_list_add (list[i]);
	}
}

/**
 * pk_monitor_get_transaction_list_cb:
 **/
static void
pk_monitor_get_transaction_list_cb (PkControl *control, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	gchar **list;

	/* get the result */
	list = pk_control_get_transaction_list_finish (control, res, &error);
	if (list == NULL) {
		g_print ("%s: %s", _("Failed to get transaction list"), error->message);
		g_error_free (error);
		return;
	}
	pk_monitor_list_print (list);
	g_strfreev (list);
}

/**
 * pk_monitor_get_transaction_list:
 **/
static void
pk_monitor_get_transaction_list (PkControl *control)
{
	egg_debug ("refreshing task list");
	pk_control_get_transaction_list_async (control, NULL,
					       (GAsyncReadyCallback) pk_monitor_get_transaction_list_cb, NULL);
}

/**
 * pk_monitor_get_daemon_state_cb:
 **/
static void
pk_monitor_get_daemon_state_cb (PkControl *control, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	gchar *state;

	/* get the result */
	state = pk_control_get_daemon_state_finish (control, res, &error);
	if (state == NULL) {
		g_print ("%s: %s", _("Failed to get daemon state"), error->message);
		g_error_free (error);
		goto out;
	}
	g_print ("Daemon state: '%s'\n", state);
out:
	g_free (state);
}

/**
 * pk_monitor_get_daemon_state:
 **/
static void
pk_monitor_get_daemon_state (PkControl *control)
{
	pk_control_get_daemon_state_async (control, NULL,
					   (GAsyncReadyCallback) pk_monitor_get_daemon_state_cb, NULL);
}


/**
 * pk_monitor_task_list_changed_cb:
 **/
static void
pk_monitor_task_list_changed_cb (PkControl *control)
{
	pk_monitor_get_transaction_list (control);

	/* only print state when verbose */
	if (verbose)
		pk_monitor_get_daemon_state (control);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean program_version = FALSE;
	GOptionContext *context;
	gint retval = EXIT_SUCCESS;
	PkControl *control;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			_("Show the program version and exit"), NULL},
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: this is a program that monitors PackageKit */
	g_option_context_set_summary (context, _("PackageKit Monitor"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		goto out;
	}

	egg_debug_init (verbose);

	loop = g_main_loop_new (NULL, FALSE);

	control = pk_control_new ();
	client = pk_client_new ();
	array = g_ptr_array_new_with_free_func (g_free);
	g_signal_connect (control, "locked",
			  G_CALLBACK (pk_monitor_locked_cb), NULL);
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (pk_monitor_repo_list_changed_cb), NULL);
	g_signal_connect (control, "updates-changed",
			  G_CALLBACK (pk_monitor_updates_changed_cb), NULL);
	g_signal_connect (control, "transaction-list-changed",
			  G_CALLBACK (pk_monitor_task_list_changed_cb), NULL);
	g_signal_connect (control, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), NULL);

	/* coldplug */
	pk_monitor_get_transaction_list (control);

	/* only print state when verbose */
	if (verbose)
		pk_monitor_get_daemon_state (control);

	/* spin */
	g_main_loop_run (loop);

	g_object_unref (control);
	g_object_unref (client);
	g_ptr_array_unref (array);
out:
	return retval;
}
