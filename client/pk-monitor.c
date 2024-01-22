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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdlib.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>

static PkClient *client = NULL;

static void
pk_monitor_installed_changed_cb (PkControl *control, gpointer data)
{
	g_print ("installed-changed\n");
}

static void
pk_monitor_repo_list_changed_cb (PkControl *control, gpointer data)
{
	g_print ("repo-list-changed\n");
}

static void
pk_monitor_updates_changed_cb (PkControl *control, gpointer data)
{
	g_print ("updates-changed\n");
}

static void
pk_monitor_notify_connected_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	gboolean connected;
	g_object_get (control, "connected", &connected, NULL);
	g_print ("daemon connected=%i\n", connected);
}

static void
pk_monitor_notify_locked_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	gboolean locked;
	g_object_get (control, "locked", &locked, NULL);
	g_print ("daemon locked=%i\n", locked);
}

static void
pk_monitor_notify_network_status_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	PkNetworkEnum state;
	g_object_get (control, "network-state", &state, NULL);
	g_print ("network status=%s\n", pk_network_enum_to_string (state));
}

static void
pk_monitor_media_change_required_cb (PkMediaChangeRequired *item, const gchar *transaction_id)
{
	PkMediaTypeEnum type;
	g_autofree gchar *id = NULL;
	g_autofree gchar *text = NULL;

	/* get data */
	g_object_get (item,
		      "media-type", &type,
		      "media-id", &id,
		      "media-text", &text,
		      NULL);

	g_print ("%s\tmedia-change-required: %s, %s, %s\n",
		 transaction_id, pk_media_type_enum_to_string (type), id, text);
}

static void
pk_monitor_adopt_cb (PkClient *_client, GAsyncResult *res, gpointer user_data)
{
	PkExitEnum exit_enum;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *transaction_id = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(PkProgress) progress = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) media_array = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_warning ("failed to adopt: %s", error->message);
		return;
	}

	/* get progress data about the transaction */
	g_object_get (results,
		      "progress", &progress,
		      NULL);

	/* get data */
	g_object_get (progress,
		      "transaction-id", &transaction_id,
		      NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_print ("%s\texit code: %s\n", transaction_id, pk_exit_enum_to_string (exit_enum));

	/* media change required */
	media_array = pk_results_get_media_change_required_array (results);
	g_ptr_array_foreach (media_array, (GFunc) pk_monitor_media_change_required_cb, transaction_id);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_print ("%s\terror code: %s, %s\n",
			 transaction_id,
			 pk_error_enum_to_string (pk_error_get_code (error_code)),
			 pk_error_get_details (error_code));
	}
}

static gchar*
pk_monitor_get_caller_info (GDBusProxy *bus_proxy, const gchar *bus_name)
{
	gboolean ret;
	gchar *cmdline = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;
	guint pid = G_MAXUINT;
	g_autoptr(GVariant) value = NULL;

	/* get pid from DBus */
	value = g_dbus_proxy_call_sync (bus_proxy,
					"GetConnectionUnixProcessID",
					g_variant_new ("(s)",
						       bus_name),
					G_DBUS_CALL_FLAGS_NONE,
					2000,
					NULL,
					&error);
	if (value == NULL) {
		g_warning ("Failed to get pid for %s: %s",
			   bus_name, error->message);
		return g_strdup_printf ("bus:%s", bus_name);
	}
	g_variant_get (value, "(u)", &pid);

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		/* we failed to get the command-line, maybe we don't have permission */
		return g_strdup_printf ("pid:%i", pid);
	}
	/* the cmdline has its args nul-separated. We deliberately make use of this to only return
	 * argv[0], i.e. the executable name */
	return cmdline;
}

static void
pk_monitor_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkRoleEnum role;
	PkStatusEnum status;
	PkInfoEnum info;
	guint percentage;
	gboolean allow_cancel;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *package_id_tmp = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *transaction_id = NULL;
	g_autofree gchar *sender = NULL;
	g_autoptr(PkItemProgress) item_progress = NULL;
	g_autoptr(PkPackage) package = NULL;
	GDBusProxy *bus_proxy = G_DBUS_PROXY (user_data);

	/* get data */
	g_object_get (progress,
		      "role", &role,
		      "status", &status,
		      "percentage", &percentage,
		      "allow-cancel", &allow_cancel,
		      "package", &package,
		      "item-progress", &item_progress,
		      "package-id", &package_id,
		      "transaction-id", &transaction_id,
		      "sender", &sender,
		      NULL);

	/* don't print before we have properties */
	if (transaction_id == NULL)
		return;

	if (type == PK_PROGRESS_TYPE_ROLE) {
		g_print ("%s\trole         %s\n", transaction_id, pk_role_enum_to_string (role));
	} else if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_print ("%s\tpackage-id   %s\n", transaction_id, package_id);
	} else if (type == PK_PROGRESS_TYPE_PACKAGE) {
		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id_tmp,
			      "summary", &summary,
			      NULL);
		g_print ("%s\tpackage      %s:%s:%s\n",
			 transaction_id,
			 pk_info_enum_to_string (info),
			 package_id_tmp,
			 summary);
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_print ("%s\tpercentage   %i\n", transaction_id, percentage);
	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		g_print ("%s\tallow_cancel %i\n", transaction_id, allow_cancel);
	} else if (type == PK_PROGRESS_TYPE_STATUS) {
		g_print ("%s\tstatus       %s\n", transaction_id, pk_status_enum_to_string (status));
	} else if (type == PK_PROGRESS_TYPE_ITEM_PROGRESS) {
		g_print ("%s\titem-progress %s,%i [%s]\n",
			 transaction_id,
			 pk_item_progress_get_package_id (item_progress),
			 pk_item_progress_get_percentage (item_progress),
			 pk_status_enum_to_string (pk_item_progress_get_status (item_progress)));
	} else if (type == PK_PROGRESS_TYPE_SENDER) {
		g_autofree gchar *cmdline = pk_monitor_get_caller_info (bus_proxy, sender);
		g_print ("%s\tsender       %s\n", transaction_id, cmdline);
	}
}

static void
pk_monitor_list_print (PkTransactionList *tlist)
{
	guint i;
	g_auto(GStrv) list = NULL;

	list = pk_transaction_list_get_ids (tlist);
	g_print ("Transactions:\n");
	if (list[0] == NULL) {
		g_print (" [none]\n");
		return;
	}
	for (i = 0; list[i] != NULL; i++)
		g_print (" %i\t%s\n", i+1, list[i]);
}

static void
pk_monitor_get_daemon_state_cb (PkControl *control, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *state = NULL;

	/* get the result */
	state = pk_control_get_daemon_state_finish (control, res, &error);
	if (state == NULL) {
		g_print ("%s: %s", _("Failed to get daemon state"), error->message);
		return;
	}
	g_print ("Daemon state: '%s'\n", state);
}

static void
pk_monitor_get_daemon_state (PkControl *control)
{
	pk_control_get_daemon_state_async (control, NULL,
					   (GAsyncReadyCallback) pk_monitor_get_daemon_state_cb, NULL);
}

static void
pk_monitor_transaction_list_changed_cb (PkControl *control, gchar **transaction_ids, gpointer user_data)
{
	/* only print state when verbose */
	if (pk_debug_is_verbose ())
		pk_monitor_get_daemon_state (control);
}

static void
pk_monitor_transaction_list_added_cb (PkTransactionList *tlist, const gchar *transaction_id, gpointer user_data)
{
	g_debug ("added: %s", transaction_id);
	pk_client_adopt_async (client, transaction_id, NULL,
			       (PkProgressCallback) pk_monitor_progress_cb, user_data,
			       (GAsyncReadyCallback) pk_monitor_adopt_cb, user_data);
	pk_monitor_list_print (tlist);
}

static void
pk_monitor_transaction_list_removed_cb (PkTransactionList *tlist, const gchar *transaction_id, gpointer data)
{
	g_debug ("removed: %s", transaction_id);
	pk_monitor_list_print (tlist);
}

static void
pk_control_properties_cb (PkControl *control, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	if (!pk_control_get_properties_finish (control, res, &error))
		g_print ("%s: %s", _("Failed to get properties"), error->message);
}

int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean program_version = FALSE;
	GOptionContext *context;
	gint retval = EXIT_SUCCESS;
	gchar **transaction_ids;
	guint i;
	g_autoptr(PkControl) control = NULL;
	g_autoptr(PkTransactionList) tlist = NULL;
	g_autoptr(GDBusConnection) bus_conn = NULL;
	g_autoptr(GDBusProxy) bus_proxy = NULL;
	g_autoptr(GError) error = NULL;

	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			_("Show the program version and exit"), NULL},
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (NULL);
	/* TRANSLATORS: this is a program that monitors PackageKit */
	g_option_context_set_summary (context, _("PackageKit Monitor"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, pk_debug_get_option_group ());
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		goto out;
	}

	/* use the bus to resolve connection names to PIDs */
	bus_conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (bus_conn == NULL) {
		g_printerr ("Cannot connect to the system bus: %s\n", error->message);
		retval = EXIT_FAILURE;
		goto out;
	}
	bus_proxy = g_dbus_proxy_new_sync (bus_conn,
				       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
				       G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
				       NULL,
				       "org.freedesktop.DBus",
				       "/org/freedesktop/DBus/Bus",
				       "org.freedesktop.DBus",
				       NULL,
				       &error);
	if (bus_proxy == NULL) {
		g_printerr ("Cannot connect to D-Bus: %s\n", error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	loop = g_main_loop_new (NULL, FALSE);

	control = pk_control_new ();
	g_signal_connect (control, "installed-changed",
			  G_CALLBACK (pk_monitor_installed_changed_cb), NULL);
	g_signal_connect (control, "repo-list-changed",
			  G_CALLBACK (pk_monitor_repo_list_changed_cb), NULL);
	g_signal_connect (control, "updates-changed",
			  G_CALLBACK (pk_monitor_updates_changed_cb), NULL);
	g_signal_connect (control, "transaction-list-changed",
			  G_CALLBACK (pk_monitor_transaction_list_changed_cb), NULL);
	g_signal_connect (control, "notify::locked",
			  G_CALLBACK (pk_monitor_notify_locked_cb), NULL);
	g_signal_connect (control, "notify::connected",
			  G_CALLBACK (pk_monitor_notify_connected_cb), NULL);
	g_signal_connect (control, "notify::network-state",
			  G_CALLBACK (pk_monitor_notify_network_status_cb), NULL);
	pk_control_get_properties_async (control, NULL,
					 (GAsyncReadyCallback) pk_control_properties_cb, NULL);

	tlist = pk_transaction_list_new ();
	g_signal_connect (tlist, "added",
			  G_CALLBACK (pk_monitor_transaction_list_added_cb), bus_proxy);
	g_signal_connect (tlist, "removed",
			  G_CALLBACK (pk_monitor_transaction_list_removed_cb), NULL);

	client = pk_client_new ();

	/* coldplug, but shouldn't be needed yet */
	transaction_ids = pk_transaction_list_get_ids (tlist);
	for (i = 0; transaction_ids[i] != NULL; i++) {
		g_warning ("need to coldplug %s", transaction_ids[i]);
	}
	g_strfreev (transaction_ids);
	pk_monitor_list_print (tlist);

	/* only print state when verbose */
	if (pk_debug_is_verbose ())
		pk_monitor_get_daemon_state (control);

	/* spin */
	g_main_loop_run (loop);
out:
	g_object_unref (client);
	return retval;
}
