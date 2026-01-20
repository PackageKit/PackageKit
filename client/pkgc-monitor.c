/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
*
 * Copyright (C) 2012-2025 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>

#include "pkgc-monitor.h"
#include "pkgc-util.h"

static PkgcliContext *g_context = NULL;

static void
pkgc_monitor_installed_changed_cb (PkControl *control, gpointer data)
{
	PkgcliContext *ctx = g_context;
	g_print ("%s%s%s%s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
		 "● Installed packages changed",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
}

static void
pkgc_monitor_repo_list_changed_cb (PkControl *control, gpointer data)
{
	PkgcliContext *ctx = g_context;
	g_print ("%s%s%s%s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
		 "● Repository list changed",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
}

static void
pkgc_monitor_updates_changed_cb (PkControl *control, gpointer data)
{
	PkgcliContext *ctx = g_context;
	g_print ("%s%s%s%s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_YELLOW),
		 "● Updates changed",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
}

static void
pkgc_monitor_notify_connected_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	PkgcliContext *ctx = g_context;
	gboolean connected;
	const gchar *color;

	g_object_get (control, "connected", &connected, NULL);
	color = connected ? pkgc_get_ansi_color (ctx, PKGC_COLOR_GREEN) : pkgc_get_ansi_color (ctx, PKGC_COLOR_RED);
	g_print ("%s%s%s%s %s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 color,
		 connected ? "✓" : "✗",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
		 connected ? "Connected" : "Disconnected");
}

static void
pkgc_monitor_notify_locked_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	PkgcliContext *ctx = g_context;
	gboolean locked;
	const gchar *color;
	g_object_get (control, "locked", &locked, NULL);
	color = locked ? pkgc_get_ansi_color (ctx, PKGC_COLOR_YELLOW) : pkgc_get_ansi_color (ctx, PKGC_COLOR_GRAY);
	g_print ("%s%s%s%s %s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 color,
		 locked ? "🔒" : "🔓",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
		 locked ? "Locked" : "Unlocked");
}

static void
pkgc_monitor_notify_network_status_cb (PkControl *control, GParamSpec *pspec, gpointer data)
{
	PkgcliContext *ctx = g_context;
	PkNetworkEnum state;
	const gchar *color;
	g_object_get (control, "network-state", &state, NULL);
	color = (state == PK_NETWORK_ENUM_ONLINE)? pkgc_get_ansi_color (ctx, PKGC_COLOR_GREEN)
			: pkgc_get_ansi_color (ctx, PKGC_COLOR_YELLOW);
	g_print ("%s%s● Network:%s %s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 color,
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
		 pk_network_enum_to_string (state));
}

static void
pkgc_monitor_media_change_required_cb (PkMediaChangeRequired *item, const gchar *transaction_id)
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
pkgc_monitor_adopt_cb (PkClient *_client, GAsyncResult *res, gpointer user_data)
{
	PkgcliContext *ctx = g_context;
	PkExitEnum exit_enum;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *transaction_id = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(PkProgress) progress = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) media_array = NULL;
	const gchar *exit_color;

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT (ctx->task), res, &error);
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

	/* Color based on exit code */
	if (exit_enum == PK_EXIT_ENUM_SUCCESS)
		exit_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_GREEN);
	else if (exit_enum == PK_EXIT_ENUM_CANCELLED)
		exit_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_YELLOW);
	else
		exit_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_RED);

	g_print ("%s%s%s  %sexit:%s %s%s%s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
		 transaction_id,
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 exit_color,
		 pk_exit_enum_to_string (exit_enum),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));

	/* media change required */
	media_array = pk_results_get_media_change_required_array (results);
	g_ptr_array_foreach (media_array, (GFunc) pkgc_monitor_media_change_required_cb, transaction_id);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_print ("%s%s%s  %serror:%s %s%s%s - %s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
			 transaction_id,
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RED),
			 pk_error_enum_to_string (pk_error_get_code (error_code)),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
			 pk_error_get_details (error_code));
	}
}

static gchar*
pkgc_monitor_get_caller_info (GDBusProxy *bus_proxy, const gchar *bus_name)
{
	gboolean ret;
	gchar *cmdline = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;
	guint pid = G_MAXUINT;
	g_autoptr(GVariant) value = NULL;

	/* get pid from D-Bus */
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
pkgc_monitor_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkgcliContext *ctx = g_context;
	GDBusProxy *bus_proxy = G_DBUS_PROXY (user_data);
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
	const gchar *tid_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN);
	const gchar *bold = pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD);
	const gchar *color_reset = pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET);

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
		g_print ("%s%s%s  %srole:%s %s%s\n",
			 bold, tid_color, transaction_id, color_reset, bold,
			 pk_role_enum_to_string (role), color_reset);
	} else if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_print ("%s%s%s  %spackage-id:%s %s%s\n",
			 bold, tid_color, transaction_id, color_reset, bold, package_id, color_reset);
	} else if (type == PK_PROGRESS_TYPE_PACKAGE) {
		const gchar *info_color;
		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id_tmp,
			      "summary", &summary,
			      NULL);
		/* Color based on package info type */
		if (info == PK_INFO_ENUM_INSTALLING || info == PK_INFO_ENUM_UPDATING)
			info_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_GREEN);
		else if (info == PK_INFO_ENUM_REMOVING)
			info_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_RED);
		else if (info == PK_INFO_ENUM_DOWNLOADING)
			info_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN);
		else
			info_color = color_reset;

		g_print ("%s%s%s  %s%s%s %s %s %s\n",
			 bold, tid_color, transaction_id, color_reset,
			 info_color, pk_info_enum_to_string (info), color_reset,
			 package_id_tmp,
			 summary ? summary : "");
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		if (percentage <= 100) {
			g_print ("%s%s%s  %s[%3d%%]%s\n",
				 bold, tid_color, transaction_id, color_reset,
				 percentage, color_reset);
		}
	} else if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL) {
		/* Don't print allow_cancel as it's not very interesting */
	} else if (type == PK_PROGRESS_TYPE_STATUS) {
		const gchar *status_color;
		/* Color based on status */
		if (status == PK_STATUS_ENUM_FINISHED)
			status_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_GREEN);
		else if (status == PK_STATUS_ENUM_DOWNLOAD || status == PK_STATUS_ENUM_INSTALL ||
				 status == PK_STATUS_ENUM_UPDATE || status == PK_STATUS_ENUM_REMOVE)
			status_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_YELLOW);
		else
			status_color = pkgc_get_ansi_color (ctx, PKGC_COLOR_GRAY);

		g_print ("%s%s%s  %s%s%s%s\n",
			 bold, tid_color, transaction_id, color_reset,
			 status_color, pk_status_enum_to_string (status), color_reset);
	} else if (type == PK_PROGRESS_TYPE_ITEM_PROGRESS) {
		g_print ("%s%s%s  %sitem: %s [%d%%, %s]%s\n",
			 bold, tid_color, transaction_id, color_reset,
			 pk_item_progress_get_package_id (item_progress),
			 pk_item_progress_get_percentage (item_progress),
			 pk_status_enum_to_string (pk_item_progress_get_status (item_progress)),
			 color_reset);
	} else if (type == PK_PROGRESS_TYPE_SENDER) {
		g_autofree gchar *cmdline = pkgc_monitor_get_caller_info (bus_proxy, sender);
		g_print ("%s%s%s  %ssender:%s %s%s\n",
			 bold, tid_color, transaction_id, color_reset, bold, cmdline, color_reset);
	}
}

static void
pkgc_monitor_list_print (PkgcliContext *ctx, PkTransactionList *tlist)
{
	g_auto(GStrv) list = NULL;

	list = pk_transaction_list_get_ids (tlist);
	if (list[0] == NULL) {
		g_print ("%sTransactions:%s %snone%s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_GRAY),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
		return;
	}
	g_print ("%sTransactions:%s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
	for (guint i = 0; list[i] != NULL; i++) {
		g_print ("  %s%i.%s %s%s%s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
			 i+1,
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 list[i],
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
	}
}

static void
pkgc_monitor_get_daemon_state_cb (PkControl *control, GAsyncResult *res, gpointer user_data)
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
pkgc_monitor_get_daemon_state (PkControl *control)
{
	pk_control_get_daemon_state_async (control, NULL,
					   (GAsyncReadyCallback) pkgc_monitor_get_daemon_state_cb, NULL);
}

static void
pkgc_monitor_transaction_list_changed_cb (PkControl *control, gchar **transaction_ids, gpointer user_data)
{
	/* only print state when verbose */
	if (pk_debug_is_verbose ())
		pkgc_monitor_get_daemon_state (control);
}

static void
pkgc_monitor_transaction_list_added_cb (PkTransactionList *tlist, const gchar *transaction_id, gpointer user_data)
{
	PkgcliContext *ctx = g_context;
	g_print ("\n%s%s▶ Transaction started:%s %s%s%s\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_GREEN),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
		 transaction_id,
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));

	pk_client_adopt_async (PK_CLIENT (ctx->task), transaction_id, NULL,
			       (PkProgressCallback) pkgc_monitor_progress_cb, user_data,
			       (GAsyncReadyCallback) pkgc_monitor_adopt_cb, user_data);
	pkgc_monitor_list_print (ctx, tlist);
}

static void
pkgc_monitor_transaction_list_removed_cb (PkTransactionList *tlist, const gchar *transaction_id, gpointer data)
{
	PkgcliContext *ctx = g_context;
	g_print ("%s%s◀ Transaction finished:%s %s%s%s\n\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_BLUE),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_CYAN),
		 transaction_id,
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
	pkgc_monitor_list_print (ctx, tlist);
}

static void
pkgc_control_properties_cb (PkControl *control, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	if (!pk_control_get_properties_finish (control, res, &error))
		g_print ("%s: %s", _("Failed to get properties"), error->message);
}

/**
 * pkgc_cmd_monitor:
 */
static int
pkgc_cmd_monitor (PkgcliContext *ctx, PkgcliCommand *cmd, int argc, char **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_auto(GStrv) transaction_ids = NULL;
	g_autoptr(PkTransactionList) tlist = NULL;
	g_autoptr(GDBusConnection) bus_conn = NULL;
	g_autoptr(GDBusProxy) bus_proxy = NULL;
	g_autoptr(GError) error = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgcli monitor */
		_("Monitor PackageKit D-Bus events"));

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		pkgc_print_error (ctx, "JSON mode is not supported for 'monitor' command");
		return PKGC_EXIT_SYNTAX_ERROR;
	}

	/* use the bus to resolve connection names to PIDs */
	bus_conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (bus_conn == NULL) {
		pkgc_print_error (ctx, "Cannot connect to the system bus: %s", error->message);
		return PKGC_EXIT_FAILURE;
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
		pkgc_print_error (ctx, "Cannot connect to D-Bus: %s", error->message);
		return PKGC_EXIT_FAILURE;
	}

	/* We run on a non-initialized PkgctlContext, as we do not want to stop
	 * the program if the packagekitd connection is lost, and also don't want
	 * to block the signal inspection. So we create bare objects in the context
	 * here.
	 * This is fairly evil, but should be forgiven for this one command... */

	/* create a global reference to the client for use in callbacks */
	g_context = ctx;

	g_clear_object (&ctx->control);
	ctx->control = pk_control_new ();

	/* connect signals */
	g_signal_connect (ctx->control, "installed-changed",
			  G_CALLBACK (pkgc_monitor_installed_changed_cb), NULL);
	g_signal_connect (ctx->control, "repo-list-changed",
			  G_CALLBACK (pkgc_monitor_repo_list_changed_cb), NULL);
	g_signal_connect (ctx->control, "updates-changed",
			  G_CALLBACK (pkgc_monitor_updates_changed_cb), NULL);
	g_signal_connect (ctx->control, "transaction-list-changed",
			  G_CALLBACK (pkgc_monitor_transaction_list_changed_cb), NULL);
	g_signal_connect (ctx->control, "notify::locked",
			  G_CALLBACK (pkgc_monitor_notify_locked_cb), NULL);
	g_signal_connect (ctx->control, "notify::connected",
			  G_CALLBACK (pkgc_monitor_notify_connected_cb), NULL);
	g_signal_connect (ctx->control, "notify::network-state",
			  G_CALLBACK (pkgc_monitor_notify_network_status_cb), NULL);
	pk_control_get_properties_async (ctx->control, NULL,
					 (GAsyncReadyCallback) pkgc_control_properties_cb, NULL);

	tlist = pk_transaction_list_new ();
	g_signal_connect (tlist, "added",
			  G_CALLBACK (pkgc_monitor_transaction_list_added_cb), bus_proxy);
	g_signal_connect (tlist, "removed",
			  G_CALLBACK (pkgc_monitor_transaction_list_removed_cb), NULL);

	g_clear_object (&ctx->task);
	ctx->task = pk_task_text_new ();

	/* coldplug, but shouldn't be needed yet */
	transaction_ids = pk_transaction_list_get_ids (tlist);
	for (guint i = 0; transaction_ids[i] != NULL; i++) {
		g_warning ("need to coldplug %s", transaction_ids[i]);
	}

	pkgc_monitor_list_print (ctx, tlist);

	/* only print state when verbose */
	if (pk_debug_is_verbose ())
		pkgc_monitor_get_daemon_state (ctx->control);

	g_print ("\n%sMonitoring PackageKit events... Press Ctrl+C to stop.%s\n\n",
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_GRAY),
		 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));

	/* spin */
	g_main_loop_run (ctx->loop);

	return ctx->exit_code;
}

/**
 * pkgc_register_monitor_commands:
 */
void
pkgc_register_monitor_commands (PkgcliContext *ctx)
{
	pkgc_context_register_command (
		ctx,
		"monitor",
		pkgc_cmd_monitor,
		/* TRANSLATORS: Summary for pkgcli monitor, the PK D-Bus monitor */
		_("Monitor PackageKit bus events"));
}
