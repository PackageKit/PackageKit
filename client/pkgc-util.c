/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2025 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
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
#include <pwd.h>
#include <packagekit-glib2/packagekit.h>
#include <jansson.h>

#include "pkgc-util.h"

/* ANSI color codes we want to use */
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_GRAY    "\033[90m"

/* Useful unicode symbols */
#define SYMBOL_RIGHT   "▶"
#define SYMBOL_CHECK   "✔"
#define SYMBOL_CROSS   "✘"
#define SYMBOL_DOT     "●"
#define SYMBOL_PACKAGE "⧉"
#define SYMBOL_UP      "▲"
#define SYMBOL_DOWN    "▼"


/* Emoji symbols - no single-cell width, so we can only use them sparingly */
#define SYMBOL_PACKAGE_EMOJI "📦"

/**
 * pkgc_util_setup_proxy:
 */
gboolean
pkgc_util_setup_proxy (PkgctlContext *ctx, GError **error)
{
	const gchar *http_proxy = NULL;
	const gchar *ftp_proxy = NULL;

	/* Check if any proxy is set */
	http_proxy = g_getenv ("http_proxy");
	ftp_proxy = g_getenv ("ftp_proxy");

	if (!http_proxy && !ftp_proxy)
		return TRUE;

	/* Set proxy configuration */
	return pk_control_set_proxy2 (ctx->control,
				      http_proxy,
				      g_getenv ("https_proxy"),
				      ftp_proxy,
				      g_getenv ("all_proxy"),
				      g_getenv ("no_proxy"),
				      g_getenv ("pac"),
				      ctx->cancellable,
				      error);
}

/**
 * pkgc_util_check_connection:
 */
gboolean
pkgc_util_check_connection (PkgctlContext *ctx, GError **error)
{
	gboolean connected = FALSE;

	g_object_get (ctx->control, "connected", &connected, NULL);

	if (!connected) {
		g_set_error_literal (error, 1, 0, _("Not connected to PackageKit daemon"));
		return FALSE;
	}

	return TRUE;
}

/**
 * pkgc_util_format_size:
 */
gchar *
pkgc_util_format_size (guint64 size)
{
	const char *units[] = { "B", "KB", "MB", "GB", "TB" };
	guint unit_index = 0;
	double size_double = (double) size;

	while (size_double >= 1024.0 && unit_index < G_N_ELEMENTS (units) - 1) {
		size_double /= 1024.0;
		unit_index++;
	}

	if (unit_index == 0) {
		return g_strdup_printf ("%lu %s", (gulong) size, units[unit_index]);
	} else {
		return g_strdup_printf ("%.1f %s", size_double, units[unit_index]);
	}
}

/**
 * pkgc_util_format_time:
 */
gchar *
pkgc_util_format_time (guint seconds)
{
	if (seconds < 60) {
		/* TRANSLATORS: A duration in seconds */
		return g_strdup_printf (_("%u seconds"), seconds);
	} else if (seconds < 3600) {
		guint minutes = seconds / 60;
		guint remaining_seconds = seconds % 60;
		if (remaining_seconds > 0) {
			/* TRANSLATORS: A duration in minutes & seconds */
			return g_strdup_printf (_("%u min %u sec"), minutes, remaining_seconds);
		} else {
			/* TRANSLATORS: A duration in minutes */
			return g_strdup_printf (_("%u min"), minutes);
		}
	} else if (seconds < 86400) {
		guint hours = seconds / 3600;
		guint remaining_minutes = (seconds % 3600) / 60;
		if (remaining_minutes > 0) {
			/* TRANSLATORS: A duration in hours & remaining minutes */
			return g_strdup_printf (_("%u h %u min"), hours, remaining_minutes);
		} else {
			/* TRANSLATORS: A duration in hours */
			return g_strdup_printf (_("%u h"), hours);
		}
	} else {
		guint days = seconds / 86400;
		guint remaining_hours = (seconds % 86400) / 3600;
		if (remaining_hours > 0) {
			/* TRANSLATORS: A duration in days & remaining hours */
			return g_strdup_printf (_("%u days %u h"), days, remaining_hours);
		} else {
			/* TRANSLATORS: A duration in days */
			return g_strdup_printf (_("%u days"), days);
		}
	}
}

/**
 * get_color:
 */
static const gchar *
get_color (PkgctlContext *ctx, const gchar *color)
{
	if (ctx->no_color || !ctx->is_tty)
		return "";

	return color;
}

/**
 * get_reset_color:
 */
static const gchar *
get_reset_color (PkgctlContext *ctx)
{
	return get_color (ctx, COLOR_RESET);
}

/**
 * pkgc_get_ansi_color:
 *
 * Returns the ANSI color code for a given color.
 */
const gchar *
pkgc_get_ansi_color (PkgctlContext *ctx, PkgcColor color)
{
	switch (color) {
	case PKGC_COLOR_RESET:
		return get_reset_color (ctx);
	case PKGC_COLOR_BOLD:
		return get_color (ctx, COLOR_BOLD);
	case PKGC_COLOR_RED:
		return get_color (ctx, COLOR_RED);
	case PKGC_COLOR_GREEN:
		return get_color (ctx, COLOR_GREEN);
	case PKGC_COLOR_YELLOW:
		return get_color (ctx, COLOR_YELLOW);
	case PKGC_COLOR_BLUE:
		return get_color (ctx, COLOR_BLUE);
	case PKGC_COLOR_MAGENTA:
		return get_color (ctx, COLOR_MAGENTA);
	case PKGC_COLOR_CYAN:
		return get_color (ctx, COLOR_CYAN);
	case PKGC_COLOR_GRAY:
		return get_color (ctx, COLOR_GRAY);
	default:
		return "";
	}
}

/**
 * pkgc_print_json_decref:
 *
 * Print a JSON object and decrease its reference count.
 */
void
pkgc_print_json_decref (json_t *root)
{
	g_autofree gchar *json_str = json_dumps (root, JSON_COMPACT);
	if (json_str)
		g_print ("%s\n", json_str);
	json_decref (root);
}

static void
print_colored (PkgctlContext *ctx, const gchar *color, const gchar *format, va_list args)
    G_GNUC_PRINTF (3, 0);

/**
 * print_colored:
 */
static void
print_colored (PkgctlContext *ctx, const gchar *color, const gchar *format, va_list args)
{
	g_autofree gchar *message = g_strdup_vprintf (format, args);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		/* JSON output handled separately */
		return;
	}

	g_print ("%s%s%s\n", get_color (ctx, color), message, get_reset_color (ctx));
}

/**
 * pkgc_print_error:
 *
 * Print an error message to stderr, or return it as JSON.
 */
void
pkgc_print_error (PkgctlContext *ctx, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "error", json_string (message));
		pkgc_print_json_decref (root);
	} else {
		g_printerr ("%s%s%s:%s %s\n",
			    get_color (ctx, COLOR_BOLD),
			    get_color (ctx, COLOR_RED),
			    _("Error"), get_reset_color (ctx), message);
	}
}

/**
 * pkgc_print_warning:
 *
 * Print a warning message.
 */
void
pkgc_print_warning (PkgctlContext *ctx, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = NULL;
		root = json_object ();
		json_object_set_new (root, "warning", json_string (message));
		pkgc_print_json_decref (root);

		return;
	}

	g_print ("%s%s %s%s %s\n",
		 get_color (ctx, COLOR_BOLD),
		 get_color (ctx, COLOR_YELLOW),
		 /* TRANSLATORS: A warning message prefix, displayed on the command-line */
		 _("Warning:"), get_reset_color (ctx), message);
}

/**
 * pkgc_print_info:
 *
 * Print an informational message.
 */
void
pkgc_print_info (PkgctlContext *ctx, const gchar *format, ...)
{
	va_list args;

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = NULL;
		g_autofree gchar *message = NULL;

		va_start (args, format);
		message = g_strdup_vprintf (format, args);
		va_end (args);

		root = json_object ();
		json_object_set_new (root, "info", json_string (message));
		pkgc_print_json_decref (root);

		return;
	}

	va_start (args, format);
	print_colored (ctx, COLOR_BLUE, format, args);
	va_end (args);
}

/**
 * pkgc_print_success:
 *
 * Print a success message, return it as JSON in JSON mode.
 */
void
pkgc_print_success (PkgctlContext *ctx, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "success", json_string (message));
		pkgc_print_json_decref (root);
	} else if (ctx->output_mode != PKGCTL_MODE_QUIET) {
		g_print ("%s%s%s %s\n",
			 get_color (ctx, COLOR_GREEN),
			 SYMBOL_CHECK,
			 get_reset_color (ctx),
			 message);
	}
}

/**
 * pkgc_println:
 *
 * Print a formatted line to stdout.
 */
void pkgc_println (const char* format, ...)
{
	va_list args;
	g_autofree gchar *str = NULL;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_print ("%s\n", str);
}

/**
 * pkgc_option_context_for_command:
 *
 * Create a GOptionContext for a specific command.
 */
GOptionContext*
pkgc_option_context_for_command(PkgctlContext *ctx,
								PkgctlCommand *cmd,
								const gchar *parameter_summary,
								const gchar *description)
{
	GOptionContext *option_context = NULL;

	option_context = g_option_context_new (parameter_summary);
	g_option_context_set_help_enabled (option_context, TRUE);
	g_option_context_set_description (option_context, (description != NULL)? description : cmd->summary);

	if (parameter_summary == NULL)
		parameter_summary = "";
	g_free (cmd->param_summary);
	cmd->param_summary = g_strdup (parameter_summary);

	return option_context;
}

/**
 * pkgc_parse_command_options:
 *
 * Parse command options and check for minimum argument count.
 */
gboolean
pkgc_parse_command_options(PkgctlContext	*ctx,
						   PkgctlCommand	*cmd,
						   GOptionContext	*option_context,
						   gint				*argc,
						   gchar			***argv,
						   gint				min_arg_count)
{
	g_autoptr(GError) error = NULL;

	if (!g_option_context_parse (option_context, argc, argv, &error)) {
		/* TRANSLATORS: Failed to parse command-line options in pkgctl */
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return FALSE;
	}

	if (*argc < min_arg_count) {
		/* TRANSLATORS: Usage summary in pkgctl if the user has provided the wrong number of parameters */
		pkgc_print_error (ctx, _("Usage: %s %s %s"), "pkgctl", cmd->name, cmd->param_summary);
		return FALSE;
	}

	return TRUE;
}

/**
 * pkgc_print_package:
 *
 * Print package information based on the output mode.
 */
void
pkgc_print_package (PkgctlContext *ctx, PkPackage *package)
{
	const gchar *package_id;
	PkInfoEnum info;
	g_auto(GStrv) split = NULL;
	const gchar *name, *version, *arch, *data;
	const gchar *info_color = COLOR_RESET;
	const gchar *info_symbol = SYMBOL_PACKAGE;

	if (!package)
		return;

	package_id = pk_package_get_id (package);
	info = pk_package_get_info (package);
	split = pk_package_id_split (package_id);

	if (split == NULL || split[0] == NULL)
		return;

	name = split[PK_PACKAGE_ID_NAME];
	version = split[PK_PACKAGE_ID_VERSION];
	arch = split[PK_PACKAGE_ID_ARCH];
	data = split[PK_PACKAGE_ID_DATA];

	/* set color & symbol based on package state */
	switch (info) {
	case PK_INFO_ENUM_INSTALLED:
		info_color = COLOR_GREEN;
		info_symbol = SYMBOL_CHECK;
		break;
	case PK_INFO_ENUM_AVAILABLE:
		info_color = COLOR_BLUE;
		info_symbol = SYMBOL_PACKAGE;
		break;
	case PK_INFO_ENUM_NORMAL:
	case PK_INFO_ENUM_BUGFIX:
	case PK_INFO_ENUM_IMPORTANT:
	case PK_INFO_ENUM_SECURITY:
	case PK_INFO_ENUM_CRITICAL:
	case PK_INFO_ENUM_UPDATING:
		info_color = COLOR_CYAN;
		info_symbol = SYMBOL_UP;
		break;
	case PK_INFO_ENUM_DOWNGRADE:
		info_color = COLOR_RED;
		info_symbol = SYMBOL_DOWN;
		break;
	case PK_INFO_ENUM_INSTALL:
	case PK_INFO_ENUM_INSTALLING:
		info_color = COLOR_CYAN;
		info_symbol = SYMBOL_DOT;
		break;
	case PK_INFO_ENUM_REMOVE:
	case PK_INFO_ENUM_REMOVING:
		info_color = COLOR_RED;
		info_symbol = SYMBOL_CROSS;
		break;
	default:
		info_color = COLOR_RESET;
		break;
	}

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "name", json_string (name));
		json_object_set_new (root, "version", json_string (version));
		json_object_set_new (root, "arch", json_string (arch));
		json_object_set_new (root, "repo", json_string (data));
		json_object_set_new (root, "state", json_string (pk_info_enum_to_string (info)));
		pkgc_print_json_decref (root);

		return;
	}

	/* print package info */
	g_print ("%s%s%s %s%s%s",
		 get_color (ctx, info_color),
		 info_symbol,
		 get_reset_color (ctx),
		 get_color (ctx, COLOR_BOLD),
		 name,
		 get_reset_color (ctx));

	g_print (" %s%s%s",
		 get_color (ctx, COLOR_GRAY),
		 version,
		 get_reset_color (ctx));

	if (arch != NULL && g_strcmp0 (arch, "") != 0) {
		g_print (".%s%s%s",
			 get_color (ctx, COLOR_GRAY),
			 arch,
			 get_reset_color (ctx));
	}

	if (data != NULL && g_strcmp0 (data, "") != 0) {
		g_print (" [%s%s%s]",
			 get_color (ctx, COLOR_GRAY),
			 data,
			 get_reset_color (ctx));
	}

	g_print ("\n");
}

/**
 * pkgc_print_package_detail:
 *
 * Print detailed package information based on the output mode.
 */
void
pkgc_print_package_detail (PkgctlContext *ctx, PkDetails *details)
{
	const gchar *package_id = NULL;
	const gchar *summary = NULL;
	const gchar *description = NULL;
	const gchar *license = NULL;
	const gchar *url = NULL;
	PkGroupEnum group;
	guint64 install_size;
	guint64 download_size;
	g_auto(GStrv) split = NULL;

	if (!details)
		return;

	g_object_get (details,
			  "package-id", &package_id,
			  "license", &license,
			  "description", &description,
			  "url", &url,
			  "summary", &summary,
			  "group", &group,
			  "size", &install_size,
			  "download-size", &download_size,
			  NULL);

	split = pk_package_id_split (package_id);
	if (split == NULL)
		return;

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "name", json_string (split[PK_PACKAGE_ID_NAME]));
		json_object_set_new (root, "version", json_string (split[PK_PACKAGE_ID_VERSION]));
		json_object_set_new (root,
					 "summary",
					 json_string (summary ? summary : ""));
		json_object_set_new (root,
				     "description",
				     json_string (description ? description : ""));
		json_object_set_new (root, "license", json_string (license ? license : ""));
		json_object_set_new (root, "url", json_string (url ? url : ""));
		json_object_set_new (root, "install_size", json_integer ((json_int_t) install_size));
		json_object_set_new (root, "download_size", json_integer ((json_int_t) download_size));
		pkgc_print_json_decref (root);
	} else {
		g_print ("%s%s%s %s\n",
			 get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for the package name in package details */
			 _("Package:"), get_reset_color (ctx), split[PK_PACKAGE_ID_NAME]);
		g_print ("%s%s%s %s\n",
			 get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for the package version in package details */
			 _("Version:"), get_reset_color (ctx), split[PK_PACKAGE_ID_VERSION]);

		if (summary && summary[0] != '\0') {
			g_print (
				"%s%s%s %s\n",
				get_color (ctx, COLOR_BOLD),
				/* TRANSLATORS: Label for the package summary in package details */
				_("Summary:"), get_reset_color (ctx), summary);
		}

		if (description && description[0] != '\0') {
			g_print (
			    "%s%s%s %s\n",
			    get_color (ctx, COLOR_BOLD),
			    /* TRANSLATORS: Label for the package description in package details */
			    _("Description:"), get_reset_color (ctx), description);
		}

		if (license && license[0] != '\0') {
			g_print ("%s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package license in package details */
				 _("License:"), get_reset_color (ctx), license);
		}

		if (url && url[0] != '\0') {
			g_print ("%s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package URL in package details */
				 _("URL:"), get_reset_color (ctx), url);
		}

		if (group != PK_GROUP_ENUM_UNKNOWN) {
			g_print ("%s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package group in package details */
				 _("Group:"), get_reset_color (ctx), pk_group_enum_to_string (group));
		}

		if (install_size > 0) {
			g_autofree gchar *size_str = pkgc_util_format_size (install_size);
			g_print ("%s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package size in package details */
				 _("Installed Size:"), get_reset_color (ctx), size_str);
		}

		if (download_size > 0) {
			g_autofree gchar *size_str = pkgc_util_format_size (download_size);
			g_print ("%s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package download size in package details */
				 _("Download Size:"), get_reset_color (ctx), size_str);
		}
	}
}

/**
 * pkgc_print_update_detail:
 *
 * Print detailed update information based on the output mode.
 */
void
pkgc_print_update_detail (PkgctlContext *ctx, PkUpdateDetail *update)
{
	PkRestartEnum restart;
	PkUpdateStateEnum state;
	g_autofree gchar *changelog = NULL;
	g_autofree gchar *issued = NULL;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *package = NULL;
	g_autofree gchar *updated = NULL;
	g_autofree gchar *update_text = NULL;
	g_auto(GStrv) bugzilla_urls = NULL;
	g_auto(GStrv) cve_urls = NULL;
	g_auto(GStrv) obsoletes = NULL;
	g_auto(GStrv) updates = NULL;
	g_auto(GStrv) vendor_urls = NULL;
	gchar *tmp;

	if (!update)
		return;

	/* Get data */
	g_object_get (update,
		      "package-id",
		      &package_id,
		      "updates",
		      &updates,
		      "obsoletes",
		      &obsoletes,
		      "vendor-urls",
		      &vendor_urls,
		      "bugzilla-urls",
		      &bugzilla_urls,
		      "cve-urls",
		      &cve_urls,
		      "restart",
		      &restart,
		      "update-text",
		      &update_text,
		      "changelog",
		      &changelog,
		      "state",
		      &state,
		      "issued",
		      &issued,
		      "updated",
		      &updated,
		      NULL);

	package = pk_package_id_to_printable (package_id);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "package", json_string (package));

		if (updates && updates[0]) {
			json_t *updates_array = json_array ();
			for (gchar **p = updates; *p != NULL; p++) {
				json_array_append_new (updates_array, json_string (*p));
			}
			json_object_set_new (root, "updates", updates_array);
		}

		if (obsoletes && obsoletes[0]) {
			json_t *obsoletes_array = json_array ();
			for (gchar **p = obsoletes; *p != NULL; p++) {
				json_array_append_new (obsoletes_array, json_string (*p));
			}
			json_object_set_new (root, "obsoletes", obsoletes_array);
		}

		if (update_text)
			json_object_set_new (root, "update_text", json_string (update_text));

		if (restart != PK_RESTART_ENUM_NONE)
			json_object_set_new (root,
					     "restart",
					     json_string (pk_restart_enum_to_string (restart)));

		pkgc_print_json_decref (root);
	} else {
		g_print ("%s%s%s\n",
			 get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Header for the update details section */
			 _("Update Details:"), get_reset_color (ctx));
		g_print (" %s%s%s %s\n",
			 get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for package update infos */
			 _("Package:"), get_reset_color (ctx), package);

		if (updates && updates[0]) {
			tmp = g_strjoinv (", ", updates);
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for update details */
				 _("Updates:"), get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (obsoletes && obsoletes[0]) {
			tmp = g_strjoinv (", ", obsoletes);
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for obsoleted packages in update details */
				 _("Obsoletes:"), get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (vendor_urls && vendor_urls[0]) {
			tmp = g_strjoinv (", ", vendor_urls);
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for vendor in update details */
				 _("Vendor:"), get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (bugzilla_urls && bugzilla_urls[0]) {
			tmp = g_strjoinv (", ", bugzilla_urls);
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for issue-tracker in update details */
				 _("Issue Tracker:"), get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (cve_urls && cve_urls[0]) {
			tmp = g_strjoinv (", ", cve_urls);
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for CVE information in update details */
				 _("CVE:"), get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (restart != PK_RESTART_ENUM_NONE) {
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for restart information in update details */
				 _("Restart:"),
				   get_reset_color (ctx),
				   pk_restart_enum_to_string (restart));
		}

		if (update_text && update_text[0] != '\0') {
			g_print (" %s%s%s\n%s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for update text in update details */
				 _("Update text:"), get_reset_color (ctx), update_text);
		}

		if (changelog && changelog[0] != '\0') {
			g_print (" %s%s%s\n%s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for changelog in update details */
				 _("Changes:"), get_reset_color (ctx), changelog);
		}

		if (state != PK_UPDATE_STATE_ENUM_UNKNOWN) {
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for update state in update details */
				 _("State:"),
				   get_reset_color (ctx),
				   pk_update_state_enum_to_string (state));
		}

		if (issued && issued[0] != '\0') {
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for issued date in update details */
				 _("Issued:"), get_reset_color (ctx), issued);
		}

		if (updated && updated[0] != '\0') {
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for updated date in update details */
				 _("Updated:"), get_reset_color (ctx), updated);
		}
	}
}

/**
 * pkgc_print_repo:
 *
 * Print repository information based on the output mode.
 */
void
pkgc_print_repo (PkgctlContext *ctx, PkRepoDetail *repo)
{
	const gchar *repo_id;
	const gchar *description;
	gboolean enabled;

	if (!repo)
		return;

	g_object_get (repo,
		      "repo-id",
		      &repo_id,
		      "description",
		      &description,
		      "enabled",
		      &enabled,
		      NULL);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "id", json_string (repo_id));
		json_object_set_new (root,
				     "description",
				     json_string (description ? description : ""));
		json_object_set_new (root, "enabled", json_boolean (enabled));
		pkgc_print_json_decref (root);
	} else {
		const gchar *status_color = enabled ? COLOR_GREEN : COLOR_RED;
		const gchar *status_text = enabled ? "enabled" : "disabled";

		g_print ("%s%-30s%s [%s%s%s] %s\n",
			 get_color (ctx, COLOR_BOLD),
			 description ? description : repo_id,
			 get_reset_color (ctx),
			 get_color (ctx, status_color),
			 status_text,
			 get_reset_color (ctx),
			 repo_id);
	}
}

/**
 * pkgc_print_transaction:
 *
 * Print transaction information based on the output mode.
 */
void
pkgc_print_transaction (PkgctlContext *ctx, PkTransactionPast *transaction)
{
	PkRoleEnum role;
	const gchar *role_text;
	gboolean succeeded;
	guint duration;
	guint uid;
	guint lines_len;
	struct passwd *pw;
	g_autofree gchar *cmdline = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *tid = NULL;
	g_autofree gchar *timespec = NULL;
	g_auto(GStrv) lines = NULL;

	if (!transaction)
		return;

	/* Get data */
	g_object_get (transaction,
		      "role",
		      &role,
		      "tid",
		      &tid,
		      "timespec",
		      &timespec,
		      "succeeded",
		      &succeeded,
		      "duration",
		      &duration,
		      "cmdline",
		      &cmdline,
		      "uid",
		      &uid,
		      "data",
		      &data,
		      NULL);

	role_text = pk_role_enum_to_string (role);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root = json_object ();
		json_object_set_new (root, "tid", json_string (tid));
		json_object_set_new (root, "role", json_string (role_text));
		json_object_set_new (root, "succeeded", json_boolean (succeeded));
		json_object_set_new (root, "duration", json_integer (duration));
		json_object_set_new (root, "uid", json_integer (uid));
		json_object_set_new (root, "timespec", json_string (timespec ? timespec : ""));

		if (cmdline && cmdline[0] != '\0')
			json_object_set_new (root, "cmdline", json_string (cmdline));

		pkgc_print_json_decref (root);

		return;
	}
	g_print ("%s%s%s %s\n",
		 get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction information */
		 _("Transaction:"), get_reset_color (ctx), tid);
	g_print (" %s%s%s %s\n",
		 get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for system time of the transaction */
		 _("System time:"), get_reset_color (ctx), timespec ? timespec : "");
	g_print (" %s%s%s %s%s%s\n",
		 get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction success status */
		 _("Succeeded:"),
		   get_reset_color (ctx),
		   succeeded ? get_color (ctx, COLOR_GREEN) : get_color (ctx, COLOR_RED),
		   succeeded
		   ? _("True")
		   : _("False"), get_reset_color (ctx));
	g_print (" %s%s%s %s\n",
		 get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction role */
		 _("Role:"), get_reset_color (ctx), role_text);

	if (duration > 0) {
		g_autofree gchar *duration_str = pkgc_util_format_time (duration);
		g_print (" %s%s%s %s\n",
			 get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for transaction duration */
			 _("Duration:"), get_reset_color (ctx), duration_str);
	}

	if (cmdline && cmdline[0] != '\0') {
		g_print (" %s%s%s %s\n",
			 get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for transaction command line */
			 _("Command line:"), get_reset_color (ctx), cmdline);
	}

	g_print (" %s%s%s %u\n",
		 get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction user ID */
		 _("User ID:"), get_reset_color (ctx), uid);

	/* Query real name */
	pw = getpwuid (uid);
	if (pw != NULL) {
		if (pw->pw_name != NULL) {
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for transaction username */
				 _("Username:"), get_reset_color (ctx), pw->pw_name);
		}
		if (pw->pw_gecos != NULL) {
			g_print (" %s%s%s %s\n",
				 get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for transaction real name */
				 _("Real name:"), get_reset_color (ctx), pw->pw_gecos);
		}
	}

	if (data && data[0] != '\0') {
		lines = g_strsplit (data, "\n", -1);
		lines_len = g_strv_length (lines);
		if (lines_len > 0) {
			g_print (
			    " %s%s%s\n",
			    get_color (ctx, COLOR_BOLD),
			    /* TRANSLATORS: Label for affected packages in transaction */
			    _("Affected packages:"), get_reset_color (ctx));
			for (guint i = 0; i < lines_len; i++) {
				g_autofree gchar *package = NULL;
				g_auto(GStrv) parts = NULL;

				if (lines[i] == NULL || lines[i][0] == '\0')
					continue;

				parts = g_strsplit (lines[i], "\t", 3);
				if (parts[1] != NULL) {
					package = pk_package_id_to_printable (parts[1]);
					g_print ("   - %s %s\n", parts[0], package);
				}
			}
		} else {
			g_print (
			    "  %s%s%s %s\n",
			    get_color (ctx, COLOR_BOLD),
			    /* TRANSLATORS: Label for affected packages in transaction */
			    _("Affected packages:"),
			      get_reset_color (ctx),
			      /* TRANSLATORS: No packages were affected by the transaction */
			      _("None"));
		}
	}
}

/**
 * pkgc_resolve_package:
 * @ctx: a valid #PkgctlContext
 * @filters: package filters to apply
 * @package_name: the package name or package_id to resolve
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Resolve a package name to a package ID. If a valid package_id is passed,
 * it is returned as-is. If multiple packages match, the user is prompted
 * to choose one.
 *
 * Returns: (transfer full): the resolved package_id, or %NULL on error
 */
gchar *
pkgc_resolve_package (PkgctlContext *ctx,
		      PkBitfield filters,
		      const gchar *package_name,
		      GError **error)
{
	const gchar *package_id_tmp;
	gboolean valid;
	PkPackage *package;
	guint idx;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(PkResults) results = NULL;
	g_auto(GStrv) tmp = NULL;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package_name);
	if (valid)
		return g_strdup (package_name);

	/* split package name (in case of comma-separated names) */
	tmp = g_strsplit (package_name, ",", -1);

	/* resolve the package name to package_id */
	results = pk_client_resolve (PK_CLIENT (ctx->task),
				     filters,
				     tmp,
				     ctx->cancellable,
				     pkgc_context_on_progress_cb,
				     ctx,
				     error);
	if (results == NULL)
		return NULL;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     pk_error_get_code (error_code),
				     pk_error_get_details (error_code));
		return NULL;
	}

	/* nothing found */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
			     "Could not find package: %s",
			     package_name);
		return NULL;
	}

	/* just one package found */
	if (array->len == 1) {
		package = g_ptr_array_index (array, 0);
		return g_strdup (pk_package_get_id (package));
	}

	/* multiple matches - prompt user to choose */
	if (ctx->noninteractive) {
		g_set_error (error,
			     G_IO_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Multiple packages match '%s' but running non-interactively",
			     package_name);
		return NULL;
	}

	pkgc_print_info (ctx, _("More than one package matches:"));
	for (guint i = 0; i < array->len; i++) {
		g_autofree gchar *printable = NULL;
		g_auto(GStrv) split = NULL;
		package = g_ptr_array_index (array, i);
		package_id_tmp = pk_package_get_id (package);
		split = pk_package_id_split (package_id_tmp);
		printable = pk_package_id_to_printable (package_id_tmp);
		g_print ("%u. %s [%s]\n", i + 1, printable, split[PK_PACKAGE_ID_DATA]);
	}

	/* prompt user for selection */
	g_print (_("Please choose the correct package: "));
	if (scanf ("%u", &idx) != 1 || idx < 1 || idx > array->len) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				     "User aborted selection");
		return NULL;
	}

	package = g_ptr_array_index (array, idx - 1);
	return g_strdup (pk_package_get_id (package));
}

/**
 * pkgc_resolve_packages:
 * @ctx: a valid #PkgctlContext
 * @filters: package filters to apply
 * @packages: array of package names to resolve
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Resolve multiple package names to package IDs.
 *
 * Returns: (transfer full): array of resolved package_ids, or %NULL on error
 */
gchar **
pkgc_resolve_packages (PkgctlContext *ctx, PkBitfield filters, gchar **packages, GError **error)
{
	guint len;
	gchar *package_id;
	GError *error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* get length */
	len = g_strv_length (packages);
	g_debug ("Resolving %u packages", len);

	/* resolve each package */
	array = g_ptr_array_new_with_free_func (g_free);
	for (guint i = 0; i < len; i++) {
		package_id = pkgc_resolve_package (ctx, filters, packages[i], &error_local);
		if (package_id == NULL) {
			if (g_error_matches (error_local,
					     G_IO_ERROR,
					     PK_ERROR_ENUM_PACKAGE_NOT_FOUND)) {
				pkgc_print_warning (ctx, _("Package not found: %s"), packages[i]);
				g_clear_error (&error_local);
				continue;
			} else {
				g_propagate_error (error, error_local);
				return NULL;
			}
		}
		g_ptr_array_add (array, package_id);
	}

	/* nothing resolved */
	if (array->len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				     "No packages were found");
		return NULL;
	}

	/* convert to GStrv */
	g_ptr_array_add (array, NULL);
	return g_strdupv ((gchar **) array->pdata);
}
