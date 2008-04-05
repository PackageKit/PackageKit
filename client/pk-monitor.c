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

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-common.h>
#include <pk-client.h>
#include <pk-notify.h>
#include <pk-task-list.h>
#include <pk-connection.h>

/**
 * pk_monitor_task_list_changed_cb:
 **/
static void
pk_monitor_task_list_changed_cb (PkTaskList *tlist, gpointer data)
{
	pk_task_list_print (tlist);
}

/**
 * pk_monitor_error_code_cb:
 **/
static void
pk_monitor_error_code_cb (PkClient *client, PkErrorCodeEnum error_code, const gchar *details, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tError: %s, %s\n", tid, pk_error_enum_to_text (error_code), details);
	g_free (tid);
}

/**
 * pk_monitor_message_cb:
 **/
static void
pk_monitor_message_cb (PkClient *client, PkMessageEnum message, const gchar *details, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tMessage: %s, %s\n", tid, pk_message_enum_to_text (message), details);
	g_free (tid);
}

/**
 * pk_monitor_require_restart_cb:
 **/
static void
pk_monitor_require_restart_cb (PkClient *client, PkRestartEnum restart, const gchar *details, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tRequireRestart: %s, %s\n", tid, pk_restart_enum_to_text (restart), details);
	g_free (tid);
}

/**
 * pk_monitor_status_changed_cb:
 **/
static void
pk_monitor_status_changed_cb (PkClient *client, PkStatusEnum status, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tStatus: %s\n", tid, pk_status_enum_to_text (status));
	g_free (tid);
}

/**
 * pk_monitor_package_cb:
 **/
static void
pk_monitor_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
		       const gchar *summary, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tPackage: %s\t%s\t%s\n", tid, pk_info_enum_to_text (info), package_id, summary);
	g_free (tid);
}

/**
 * pk_monitor_allow_cancel_cb:
 **/
static void
pk_monitor_allow_cancel_cb (PkClient *client, gboolean allow_cancel, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tAllow Cancel: %i\n", tid, allow_cancel);
	g_free (tid);
}

/**
 * pk_monitor_repo_signature_required_cb:
 **/
static void
pk_monitor_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
				       const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				       const gchar *key_fingerprint, const gchar *key_timestamp,
				       PkSigTypeEnum type, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("emitting RepoSignatureRequired package_id=%s, tid:%s, %s, %s, %s, %s, %s, %s, %s\n",
		 package_id, tid, repository_name, key_url, key_userid, key_id,
		 key_fingerprint, key_timestamp, pk_sig_type_enum_to_text (type));
	g_free (tid);
}

/**
 * pk_monitor_finished_cb:
 **/
static void
pk_monitor_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	gchar *tid = pk_client_get_tid (client);
	g_print ("%s\tFinished: %s, %ims\n", tid, pk_exit_enum_to_text (exit), runtime);
	g_free (tid);
}

/**
 * pk_monitor_repo_list_changed_cb:
 **/
static void
pk_monitor_repo_list_changed_cb (PkNotify *notify, gpointer data)
{
	g_print ("repo-list-changed\n");
}

/**
 * pk_monitor_updates_changed_cb:
 **/
static void
pk_monitor_updates_changed_cb (PkNotify *notify, gpointer data)
{
	g_print ("updates-changed\n");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, gpointer data)
{
	pk_debug ("connected=%i", connected);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkTaskList *tlist;
	PkClient *client;
	PkNotify *notify;
	gboolean ret;
	GMainLoop *loop;
	PkConnection *pconnection;
	gboolean connected;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			_("Show the program version and exit"), NULL},
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, _("PackageKit Monitor"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	pk_debug_init (verbose);

	loop = g_main_loop_new (NULL, FALSE);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), loop);
	connected = pk_connection_valid (pconnection);
	pk_debug ("connected=%i", connected);

	client = pk_client_new ();
	pk_client_set_promiscuous (client, TRUE, NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_monitor_finished_cb), NULL);
	g_signal_connect (client, "error-code",
			  G_CALLBACK (pk_monitor_error_code_cb), NULL);
	g_signal_connect (client, "message",
			  G_CALLBACK (pk_monitor_message_cb), NULL);
	g_signal_connect (client, "require-restart",
			  G_CALLBACK (pk_monitor_require_restart_cb), NULL);
	g_signal_connect (client, "status-changed",
			  G_CALLBACK (pk_monitor_status_changed_cb), NULL);
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_monitor_package_cb), NULL);
	g_signal_connect (client, "allow-cancel",
			  G_CALLBACK (pk_monitor_allow_cancel_cb), NULL);
	g_signal_connect (client, "repo-signature-required",
			  G_CALLBACK (pk_monitor_repo_signature_required_cb), NULL);

	notify = pk_notify_new ();
	g_signal_connect (notify, "repo-list-changed",
			  G_CALLBACK (pk_monitor_repo_list_changed_cb), NULL);
	g_signal_connect (notify, "updates-changed",
			  G_CALLBACK (pk_monitor_updates_changed_cb), NULL);

	tlist = pk_task_list_new ();
	g_signal_connect (tlist, "task-list-changed",
			  G_CALLBACK (pk_monitor_task_list_changed_cb), NULL);

	pk_debug ("refreshing task list");
	ret = pk_task_list_refresh (tlist);
	if (ret == FALSE) {
		g_error ("cannot refresh transaction list");
	}
	pk_task_list_print (tlist);

	g_main_loop_run (loop);

	g_object_unref (client);
	g_object_unref (notify);
	g_object_unref (tlist);
	g_object_unref (pconnection);

	return 0;
}
