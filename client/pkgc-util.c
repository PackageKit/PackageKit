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
#include <packagekit.h>
#include <pk-json-private.h>

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
#define SYMBOL_CIRCLE  "○"
#define SYMBOL_PACKAGE "▣"
#define SYMBOL_UP      "▲"
#define SYMBOL_DOWN    "▼"

/* indentation of the text below a "Description:" label */
#define PKGC_TEXT_INDENT 2

/* the update details are indented by one, so text below their labels by one more */
#define PKGC_UPDATE_TEXT_INDENT (PKGC_TEXT_INDENT + 1)

/* longer text lines are wrapped even on wide terminals, to stay readable */
#define PKGC_TEXT_WIDTH_MAX 100

/* indentation of the detail line below a package or repository */
#define PKGC_DETAIL_INDENT 4

/* Emoji symbols - no single-cell width, so we can only use them sparingly */
#define SYMBOL_PACKAGE_EMOJI "📦"

/**
 * pkgc_util_setup_proxy:
 */
gboolean
pkgc_util_setup_proxy (PkgcliContext *ctx, GError **error)
{
	const gchar *http_proxy = NULL;
	const gchar *https_proxy = NULL;
	const gchar *ftp_proxy = NULL;
	const gchar *pac = NULL;

	/* check if any proxy is set */
	http_proxy = g_getenv ("http_proxy");
	https_proxy = g_getenv ("https_proxy");
	ftp_proxy = g_getenv ("ftp_proxy");
	pac = g_getenv ("pac");

	if (!http_proxy && !https_proxy && !ftp_proxy && !pac)
		return TRUE;

	/* set proxy configuration */
	return pk_control_set_proxy2 (ctx->control,
				      http_proxy,
				      https_proxy,
				      ftp_proxy,
				      g_getenv ("all_proxy"),
				      g_getenv ("no_proxy"),
				      pac,
				      ctx->cancellable,
				      error);
}

/**
 * pkgc_util_check_connection:
 */
gboolean
pkgc_util_check_connection (PkgcliContext *ctx, GError **error)
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
 * pkgc_util_make_sorted_shallow_copy:
 * @array: an array of objects
 * @func: the function to sort by
 *
 * Get a sorted copy of @array, without changing the order of the original.
 *
 * Returns: (transfer container): a sorted shallow copy, @array keeps owning the elements
 */
GPtrArray *
pkgc_util_make_sorted_shallow_copy (GPtrArray *array, GCompareFunc func)
{
	GPtrArray *sorted = g_ptr_array_copy (array, NULL, NULL);

	g_ptr_array_set_free_func (sorted, NULL);
	g_ptr_array_sort (sorted, func);
	return sorted;
}

/**
 * pkgc_util_get_wrap_width:
 *
 * Get the line width to wrap text at, which is the console width, but
 * never more than PKGC_TEXT_WIDTH_MAX columns of text.
 */
static guint
pkgc_util_get_wrap_width (guint indent)
{
	return MIN (pk_console_get_width (), PKGC_TEXT_WIDTH_MAX + indent);
}

/**
 * pkgc_util_dedent:
 *
 * Remove the leading spaces all non-empty lines of a text have in common,
 * so it can be indented consistently.
 *
 * Returns: (transfer full): the dedented text
 */
static gchar *
pkgc_util_dedent (const gchar *text)
{
	g_auto(GStrv) lines = g_strsplit (text, "\n", -1);
	GString *str = g_string_new (NULL);
	guint common = G_MAXUINT;

	for (guint i = 0; lines[i] != NULL; i++) {
		guint n = strspn (lines[i], " ");
		if (lines[i][n] != '\0')
			common = MIN (common, n);
	}

	for (guint i = 0; lines[i] != NULL; i++) {
		if (i > 0)
			g_string_append_c (str, '\n');
		/* blank lines may be shorter than the common indentation */
		g_string_append (str, lines[i] + MIN (strlen (lines[i]), common));
	}

	return g_string_free (str, FALSE);
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
 * pkgc_can_use_colors:
 */
static gboolean
pkgc_can_use_colors (PkgcliContext *ctx)
{
	return !ctx->no_color && ctx->is_tty;
}

/**
 * pkgc_get_color:
 */
static const gchar *
pkgc_get_color (PkgcliContext *ctx, const gchar *color)
{
	if (!pkgc_can_use_colors (ctx))
		return "";

	return color;
}

/**
 * pkgc_get_reset_color:
 */
static const gchar *
pkgc_get_reset_color (PkgcliContext *ctx)
{
	return pkgc_get_color (ctx, COLOR_RESET);
}

/**
 * pkgc_get_ansi_color:
 *
 * Returns the ANSI color code for a given color.
 */
const gchar *
pkgc_get_ansi_color (PkgcliContext *ctx, PkgcColor color)
{
	switch (color) {
	case PKGC_COLOR_RESET:
		return pkgc_get_reset_color (ctx);
	case PKGC_COLOR_BOLD:
		return pkgc_get_color (ctx, COLOR_BOLD);
	case PKGC_COLOR_RED:
		return pkgc_get_color (ctx, COLOR_RED);
	case PKGC_COLOR_GREEN:
		return pkgc_get_color (ctx, COLOR_GREEN);
	case PKGC_COLOR_YELLOW:
		return pkgc_get_color (ctx, COLOR_YELLOW);
	case PKGC_COLOR_BLUE:
		return pkgc_get_color (ctx, COLOR_BLUE);
	case PKGC_COLOR_MAGENTA:
		return pkgc_get_color (ctx, COLOR_MAGENTA);
	case PKGC_COLOR_CYAN:
		return pkgc_get_color (ctx, COLOR_CYAN);
	case PKGC_COLOR_GRAY:
		return pkgc_get_color (ctx, COLOR_GRAY);
	default:
		return "";
	}
}

/**
 * pkgc_print_json:
 *
 * Print a JSON object as a single compact line.
 */
void
pkgc_print_json (json_t *root)
{
	g_autofree gchar *json_str = json_dumps (root, JSON_COMPACT);
	if (json_str)
		g_print ("%s\n", json_str);
}

static void
print_colored (PkgcliContext *ctx, const gchar *color, const gchar *format, va_list args)
    G_GNUC_PRINTF (3, 0);

/**
 * print_colored:
 */
static void
print_colored (PkgcliContext *ctx, const gchar *color, const gchar *format, va_list args)
{
	g_autofree gchar *message = g_strdup_vprintf (format, args);

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		/* JSON output handled separately */
		return;
	}

	g_print ("%s%s%s\n", pkgc_get_color (ctx, color), message, pkgc_get_reset_color (ctx));
}

/**
 * pkgc_print_error:
 *
 * Print an error message to stderr, or return it as JSON.
 */
void
pkgc_print_error (PkgcliContext *ctx, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	pkgc_context_stop_progress_bar (ctx);
	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
		json_object_set_new (root, "error", json_string (message));
		pkgc_print_json (root);
	} else {
		g_printerr ("%s%s%s:%s %s\n",
			    pkgc_get_color (ctx, COLOR_BOLD),
			    pkgc_get_color (ctx, COLOR_RED),
			    _("Error"), pkgc_get_reset_color (ctx), message);
	}
}

/**
 * pkgc_print_warning:
 *
 * Print a warning message.
 */
void
pkgc_print_warning (PkgcliContext *ctx, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	pkgc_context_stop_progress_bar (ctx);
	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
		json_object_set_new (root, "warning", json_string (message));
		pkgc_print_json (root);

		return;
	}

	g_print ("%s%s %s%s %s\n",
		 pkgc_get_color (ctx, COLOR_BOLD),
		 pkgc_get_color (ctx, COLOR_YELLOW),
		 /* TRANSLATORS: A warning message prefix, displayed on the command-line */
		 _("Warning:"), pkgc_get_reset_color (ctx), message);
}

/**
 * pkgc_print_info:
 *
 * Print an informational message.
 */
void
pkgc_print_info (PkgcliContext *ctx, const gchar *format, ...)
{
	va_list args;

	pkgc_context_stop_progress_bar (ctx);
	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
		g_autofree gchar *message = NULL;

		va_start (args, format);
		message = g_strdup_vprintf (format, args);
		va_end (args);

		json_object_set_new (root, "info", json_string (message));
		pkgc_print_json (root);

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
pkgc_print_success (PkgcliContext *ctx, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	pkgc_context_stop_progress_bar (ctx);
	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
		json_object_set_new (root, "success", json_string (message));
		pkgc_print_json (root);
	} else if (ctx->output_mode != PKGCLI_MODE_QUIET) {
		g_print ("%s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_GREEN),
			 SYMBOL_CHECK,
			 pkgc_get_reset_color (ctx),
			 message);
	}
}

/**
 * pkgc_println:
 *
 * Print a formatted line to stdout.
 */
void
pkgc_println (const char *format, ...)
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
GOptionContext *
pkgc_option_context_for_command (PkgcliContext *ctx,
				 PkgcliCommand *cmd,
				 const gchar *parameter_summary,
				 const gchar *description)
{
	GOptionContext *option_context = NULL;
	g_autofree gchar *alias_note = NULL;
	g_autofree gchar *full_description = NULL;

	if (description == NULL)
		description = cmd->summary;
	if (cmd->alias != NULL) {
		/* TRANSLATORS: Shown in the help of a command, %s is a shorter alternative name for it */
		alias_note = g_strdup_printf (_("Alias: %s"), cmd->alias);
		full_description = g_strconcat (description, "\n", alias_note, NULL);
		description = full_description;
	}

	option_context = g_option_context_new (parameter_summary);
	g_option_context_set_help_enabled (option_context, TRUE);
	g_option_context_set_description (option_context, description);

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
pkgc_parse_command_options (PkgcliContext *ctx,
			    PkgcliCommand *cmd,
			    GOptionContext *option_context,
			    gint *argc,
			    gchar ***argv,
			    gint min_arg_count)
{
	g_autoptr(GError) error = NULL;

	if (!g_option_context_parse (option_context, argc, argv, &error)) {
		/* TRANSLATORS: Failed to parse command-line options in pkgcli */
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return FALSE;
	}

	if (*argc < min_arg_count) {
		pkgc_print_error (
		    ctx,
		    /* TRANSLATORS: Usage summary in pkgcli if the user has provided the wrong number of parameters */
		    _("Usage: %s %s %s"), "pkgcli", cmd->name, cmd->param_summary);
		return FALSE;
	}

	return TRUE;
}

/**
 * pkgc_is_local_package:
 *
 * Check if a package name refers to a local package file.
 */
gboolean
pkgc_is_local_package (const gchar *package_name)
{
	/* without dot there is no file-extension, so the package cannot be local */
	if (g_strstr_len (package_name, -1, ".") == NULL)
		return FALSE;

	/* if we have a slash, it's always a path to a local file, as no package-manager permits slashes
	 * in package names */
	if (g_strstr_len (package_name, -1, "/") != NULL)
		return TRUE;

	/* if we made it here, check if the file exists to be sure */
	return g_file_test (package_name, G_FILE_TEST_EXISTS);
}

/**
 * pkgc_package_get_style:
 *
 * Get the color and symbol representing the state of a package, and the
 * color and label of its update severity.
 */
static void
pkgc_package_get_style (PkPackage *package,
			const gchar **info_color,
			const gchar **info_symbol,
			const gchar **severity_color,
			const gchar **severity_text)
{
	PkInfoEnum info = pk_package_get_info (package);
	PkSeverityEnum severity = pk_package_get_update_severity (package);

	*info_color = COLOR_RESET;
	*info_symbol = SYMBOL_PACKAGE;
	*severity_color = NULL;
	*severity_text = NULL;

	/* set color & symbol based on package state */
	switch (info) {
	case PK_INFO_ENUM_INSTALLED:
		*info_color = COLOR_GREEN;
		*info_symbol = SYMBOL_CHECK;
		break;
	case PK_INFO_ENUM_AVAILABLE:
		*info_color = COLOR_BLUE;
		*info_symbol = SYMBOL_PACKAGE;
		break;
	case PK_INFO_ENUM_RESIDUAL:
		*info_color = COLOR_YELLOW;
		*info_symbol = SYMBOL_PACKAGE;
		break;
	case PK_INFO_ENUM_UPDATE:
	case PK_INFO_ENUM_UPDATE_SECURITY:
	case PK_INFO_ENUM_UPDATE_ENHANCEMENT:
	case PK_INFO_ENUM_UPDATING:
		*info_color = COLOR_CYAN;
		*info_symbol = SYMBOL_UP;
		break;
	case PK_INFO_ENUM_DOWNGRADE:
		*info_color = COLOR_RED;
		*info_symbol = SYMBOL_DOWN;
		break;
	case PK_INFO_ENUM_INSTALL:
	case PK_INFO_ENUM_INSTALLING:
		*info_color = COLOR_CYAN;
		*info_symbol = SYMBOL_DOT;
		break;
	case PK_INFO_ENUM_REMOVE:
	case PK_INFO_ENUM_REMOVING:
		*info_color = COLOR_RED;
		*info_symbol = SYMBOL_CROSS;
		break;
	case PK_INFO_ENUM_BLOCKED:
		*info_color = COLOR_GRAY;
		*info_symbol = SYMBOL_CROSS;
		break;
	default:
		break;
	}

	/* the severity of an update is shown by color, urgent ones are labeled as well */
	switch (severity) {
	case PK_SEVERITY_ENUM_LOW:
		*severity_color = COLOR_GRAY;
		break;
	case PK_SEVERITY_ENUM_HIGH:
		*severity_color = COLOR_YELLOW;
		/* TRANSLATORS: the severity of a package update, keep this short */
		*severity_text = _("high");
		break;
	case PK_SEVERITY_ENUM_CRITICAL:
		*severity_color = COLOR_RED;
		/* TRANSLATORS: the severity of a package update, keep this short */
		*severity_text = _("critical");
		break;
	default:
		break;
	}
	if (*severity_color != NULL && info != PK_INFO_ENUM_BLOCKED)
		*info_color = *severity_color;
}

/**
 * pkgc_print_package_json:
 */
static void
pkgc_print_package_json (PkPackage *package)
{
	PkInfoEnum info = pk_package_get_info (package);
	PkSeverityEnum severity = pk_package_get_update_severity (package);
	g_autoptr(json_t) root = json_object ();

	json_object_set_new (root, "name", json_string (pk_package_get_name (package)));
	json_object_set_new (root, "version", json_string (pk_package_get_version (package)));
	json_object_set_new (root, "arch", json_string (pk_package_get_arch (package)));
	json_object_set_new (root, "repo", json_string (pk_package_get_origin (package)));
	json_object_set_new (root, "data", json_string (pk_package_get_data (package)));
	json_object_set_new (root, "state", json_string (pk_info_enum_to_string (info)));
	if (severity != PK_SEVERITY_ENUM_NONE)
		json_object_set_new (root,
				     "severity",
				     json_string (pk_severity_enum_to_string (severity)));
	pkgc_print_json (root);
}

/**
 * pkgc_print_packages:
 *
 * Print a list of packages sorted by name, one line per package with an
 * optional summary line below, or one JSON object per package in JSON mode.
 */
void
pkgc_print_packages (PkgcliContext *ctx, GPtrArray *packages, gboolean show_summary)
{
	g_autoptr(GPtrArray) sorted = NULL;
	g_autoptr(PkConsoleTable) table = NULL;
	g_autofree gchar *rendered = NULL;
	guint wrap_width = pkgc_util_get_wrap_width (PKGC_DETAIL_INDENT);

	if (packages == NULL || packages->len == 0)
		return;

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		for (guint i = 0; i < packages->len; i++)
			pkgc_print_package_json (g_ptr_array_index (packages, i));
		return;
	}

	/* the columns are the state symbol, name, version, architecture, origin
	 * and update severity; very long values must not push the following
	 * columns of all other lines far to the right */
	table = pk_console_table_new (6);
	pk_console_table_set_spacing (table, 0, 1);
	pk_console_table_set_max_width (table, 1, PK_CONSOLE_NAME_WIDTH_MAX);
	pk_console_table_set_max_width (table, 2, PK_CONSOLE_VERSION_WIDTH_MAX);
	pk_console_table_set_max_width (table, 3, PK_CONSOLE_ARCH_WIDTH_MAX);
	pk_console_table_set_max_width (table, 4, PK_CONSOLE_ORIGIN_WIDTH_MAX);

	sorted = pkgc_util_make_sorted_shallow_copy (packages, pk_console_package_sort_func);
	for (guint i = 0; i < sorted->len; i++) {
		PkPackage *package = g_ptr_array_index (sorted, i);
		const gchar *summary = pk_package_get_summary (package);
		const gchar *info_color, *info_symbol, *severity_color, *severity_text;

		pkgc_package_get_style (package,
					&info_color,
					&info_symbol,
					&severity_color,
					&severity_text);

		pk_console_table_add_row (table,
					  info_symbol,
					  pk_package_get_name (package),
					  pk_package_get_version (package),
					  pk_package_get_arch (package),
					  pk_package_get_origin (package),
					  severity_text);
		pk_console_table_set_color (table, 0, info_color);
		pk_console_table_set_color (table, 1, COLOR_BOLD);
		for (guint c = 2; c <= 4; c++)
			pk_console_table_set_color (table, c, COLOR_GRAY);
		pk_console_table_set_color (table, 5, severity_color);

		if (show_summary && summary != NULL && summary[0] != '\0') {
			g_autofree gchar *wrapped = pk_console_text_wrap (summary,
									  wrap_width,
									  PKGC_DETAIL_INDENT);
			g_autofree gchar *detail = g_strdup_printf ("%*s%s",
								    PKGC_DETAIL_INDENT,
								    "",
								    wrapped);
			pk_console_table_add_text (table, detail);
		}
	}

	rendered = pk_console_table_render (table,
					    pk_console_get_width (),
					    pkgc_can_use_colors (ctx));
	g_print ("%s", rendered);
}

/**
 * pkgc_print_package_detail:
 *
 * Print detailed package information based on the output mode.
 */
void
pkgc_print_package_detail (PkgcliContext *ctx, PkDetails *details)
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
		      "package-id",
		      &package_id,
		      "license",
		      &license,
		      "description",
		      &description,
		      "url",
		      &url,
		      "summary",
		      &summary,
		      "group",
		      &group,
		      "size",
		      &install_size,
		      "download-size",
		      &download_size,
		      NULL);

	split = pk_package_id_split (package_id);
	if (split == NULL)
		return;

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
		json_object_set_new (root, "name", json_string (split[PK_PACKAGE_ID_NAME]));
		json_object_set_new (root, "version", json_string (split[PK_PACKAGE_ID_VERSION]));
		json_object_set_new (root, "summary", json_string (summary ? summary : ""));
		json_object_set_new (root,
				     "description",
				     json_string (description ? description : ""));
		json_object_set_new (root, "license", json_string (license ? license : ""));
		json_object_set_new (root, "url", json_string (url ? url : ""));
		json_object_set_new (root,
				     "install_size",
				     json_integer ((json_int_t) install_size));
		json_object_set_new (root,
				     "download_size",
				     json_integer ((json_int_t) download_size));
		pkgc_print_json (root);
	} else {
		g_print ("%s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for the package name in package details */
			 _("Package:"), pkgc_get_reset_color (ctx), split[PK_PACKAGE_ID_NAME]);
		g_print ("%s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for the package version in package details */
			 _("Version:"), pkgc_get_reset_color (ctx), split[PK_PACKAGE_ID_VERSION]);

		if (summary && summary[0] != '\0') {
			g_print ("%s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package summary in package details */
				 _("Summary:"), pkgc_get_reset_color (ctx), summary);
		}

		if (description && description[0] != '\0') {
			g_autofree gchar *wrapped = pk_console_text_wrap (
			    description,
			    pkgc_util_get_wrap_width (PKGC_TEXT_INDENT),
			    PKGC_TEXT_INDENT);
			g_print (
			    "%s%s%s\n%*s%s\n",
			    pkgc_get_color (ctx, COLOR_BOLD),
			    /* TRANSLATORS: Label for the package description in package details */
			    _("Description:"),
			      pkgc_get_reset_color (ctx),
			      PKGC_TEXT_INDENT,
			      "",
			      wrapped);
		}

		if (license && license[0] != '\0') {
			g_print ("%s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package license in package details */
				 _("License:"), pkgc_get_reset_color (ctx), license);
		}

		if (url && url[0] != '\0') {
			g_print ("%s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package URL in package details */
				 _("URL:"), pkgc_get_reset_color (ctx), url);
		}

		if (group != PK_GROUP_ENUM_UNKNOWN) {
			g_print ("%s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package group in package details */
				 _("Group:"),
				   pkgc_get_reset_color (ctx),
				   pk_group_enum_to_string (group));
		}

		if (install_size > 0) {
			g_autofree gchar *size_str = pkgc_util_format_size (install_size);
			g_print ("%s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the package size in package details */
				 _("Installed Size:"), pkgc_get_reset_color (ctx), size_str);
		}

		if (download_size > 0) {
			g_autofree gchar *size_str = pkgc_util_format_size (download_size);
			g_print (
			    "%s%s%s %s\n",
			    pkgc_get_color (ctx, COLOR_BOLD),
			    /* TRANSLATORS: Label for the package download size in package details */
			    _("Download Size:"), pkgc_get_reset_color (ctx), size_str);
		}
	}
}

/**
 * pkgc_print_update_detail:
 *
 * Print detailed update information based on the output mode.
 */
void
pkgc_print_update_detail (PkgcliContext *ctx, PkUpdateDetail *update)
{
	PkRestartEnum restart;
	PkUpdateStateEnum state;
	g_autofree gchar *changelog = NULL;
	g_autofree gchar *time_issued = NULL;
	g_autofree gchar *package_id = NULL;
	g_autofree gchar *package = NULL;
	g_autofree gchar *update_text = NULL;
	g_auto(GStrv) obsoletes = NULL;
	g_auto(GStrv) updates = NULL;
	g_autoptr(GPtrArray) issues = NULL;
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
		      "issues",
		      &issues,
		      "restart",
		      &restart,
		      "update-text",
		      &update_text,
		      "changelog",
		      &changelog,
		      "state",
		      &state,
		      "time-issued",
		      &time_issued,
		      NULL);

	package = pk_package_id_to_printable (package_id);

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
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

		if (issues != NULL && issues->len > 0) {
			json_t *issues_array = json_array ();
			for (guint i = 0; i < issues->len; i++) {
				PkIssue *issue = g_ptr_array_index (issues, i);
				json_t *obj = json_object ();
				json_object_set_new (obj,
						     "kind",
						     json_string (pk_issue_kind_enum_to_string (
							 pk_issue_get_kind (issue))));
				json_object_set_new (obj,
						     "url",
						     json_string (pk_issue_get_url (issue)));
				if (pk_issue_get_id (issue) != NULL)
					json_object_set_new (obj,
							     "id",
							     json_string (pk_issue_get_id (issue)));
				if (pk_issue_get_title (issue) != NULL)
					json_object_set_new (
					    obj,
					    "title",
					    json_string (pk_issue_get_title (issue)));
				json_array_append_new (issues_array, obj);
			}
			json_object_set_new (root, "issues", issues_array);
		}

		if (update_text)
			json_object_set_new (root, "update_text", json_string (update_text));

		if (restart != PK_RESTART_ENUM_NONE)
			json_object_set_new (root,
					     "restart",
					     json_string (pk_restart_enum_to_string (restart)));

		pkgc_print_json (root);
	} else {
		g_print ("%s%s%s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Header for the update details section */
			 _("Update Details:"), pkgc_get_reset_color (ctx));
		g_print (" %s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for package update infos */
			 _("Package:"), pkgc_get_reset_color (ctx), package);

		if (updates && updates[0]) {
			tmp = g_strjoinv (", ", updates);
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for update details */
				 _("Updates:"), pkgc_get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (obsoletes && obsoletes[0]) {
			tmp = g_strjoinv (", ", obsoletes);
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for obsoleted packages in update details */
				 _("Obsoletes:"), pkgc_get_reset_color (ctx), tmp);
			g_free (tmp);
		}

		if (issues != NULL && issues->len > 0) {
			/* clang-format off */
			const struct {
				PkIssueKindEnum kind;
				const gchar *label;
			} groups[] = {
				/* TRANSLATORS: Label for issue tracker entries in update details */
				{ PK_ISSUE_KIND_ENUM_GENERIC, N_("Issues:") },
				/* TRANSLATORS: Label for CVE records in update details */
				{ PK_ISSUE_KIND_ENUM_CVE, N_("CVEs:") },
				/* TRANSLATORS: Label for the distributor's advisory in update details */
				{ PK_ISSUE_KIND_ENUM_ADVISORY, N_("Advisory:") },
				/* TRANSLATORS: Label for references of unknown kind in update details */
				{ PK_ISSUE_KIND_ENUM_UNKNOWN, N_("References:") },
			};
			/* clang-format on */
			for (guint g = 0; g < G_N_ELEMENTS (groups); g++) {
				gboolean first = TRUE;
				for (guint i = 0; i < issues->len; i++) {
					PkIssue *issue = g_ptr_array_index (issues, i);
					const gchar *id = pk_issue_get_id (issue);
					const gchar *title = pk_issue_get_title (issue);
					if (pk_issue_get_kind (issue) != groups[g].kind)
						continue;
					if (first) {
						g_print (" %s%s%s",
							 pkgc_get_color (ctx, COLOR_BOLD),
							 _(groups[g].label),
							   pkgc_get_reset_color (ctx));
						first = FALSE;
					} else {
						g_print (",");
					}
					if (id != NULL && title != NULL)
						g_print (" %s (%s)", id, title);
					else if (id != NULL)
						g_print (" %s", id);
					else if (title != NULL)
						g_print (" %s", title);
					g_print (" <%s>", pk_issue_get_url (issue));
				}
				if (!first)
					g_print ("\n");
			}
		}

		if (restart != PK_RESTART_ENUM_NONE) {
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for restart information in update details */
				 _("Restart:"),
				   pkgc_get_reset_color (ctx),
				   pk_restart_enum_to_string (restart));
		}

		if (update_text && update_text[0] != '\0') {
			g_autofree gchar *dedented = pkgc_util_dedent (g_strchomp (update_text));
			g_autofree gchar *wrapped = NULL;

			wrapped = pk_console_text_wrap (
			    dedented,
			    pkgc_util_get_wrap_width (PKGC_UPDATE_TEXT_INDENT),
			    PKGC_UPDATE_TEXT_INDENT);
			g_print (" %s%s%s\n%*s%s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for update text in update details */
				 _("Update text:"),
				   pkgc_get_reset_color (ctx),
				   PKGC_UPDATE_TEXT_INDENT,
				   "",
				   wrapped);
		}

		if (changelog && changelog[0] != '\0') {
			g_autofree gchar *dedented = pkgc_util_dedent (g_strchomp (changelog));
			g_autofree gchar *wrapped = NULL;

			wrapped = pk_console_text_wrap (
			    dedented,
			    pkgc_util_get_wrap_width (PKGC_UPDATE_TEXT_INDENT),
			    PKGC_UPDATE_TEXT_INDENT);
			g_print (" %s%s%s\n%*s%s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for changelog in update details */
				 _("Changes:"),
				   pkgc_get_reset_color (ctx),
				   PKGC_UPDATE_TEXT_INDENT,
				   "",
				   wrapped);
		}

		if (state != PK_UPDATE_STATE_ENUM_UNKNOWN) {
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for update state in update details */
				 _("State:"),
				   pkgc_get_reset_color (ctx),
				   pk_update_state_enum_to_string (state));
		}

		if (time_issued && time_issued[0] != '\0') {
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for issued date in update details */
				 _("Issued:"), pkgc_get_reset_color (ctx), time_issued);
		}
	}
}

/**
 * pkgc_print_os_upgrades:
 *
 * Print the available OS upgrades, or one JSON object per upgrade in JSON mode.
 */
void
pkgc_print_os_upgrades (PkgcliContext *ctx, GPtrArray *upgrades)
{
	for (guint i = 0; i < upgrades->len; i++) {
		PkSystemUpgrade *upgrade = g_ptr_array_index (upgrades, i);
		const gchar *id = pk_system_upgrade_get_id (upgrade);
		const gchar *summary = pk_system_upgrade_get_summary (upgrade);
		const gchar *state = pk_release_kind_enum_to_string (
		    pk_system_upgrade_get_state (upgrade));

		if (ctx->output_mode == PKGCLI_MODE_JSON) {
			g_autoptr(json_t) root = json_object ();

			json_object_set_new (root, "id", json_string (id));
			json_object_set_new (root, "state", json_string (state));
			json_object_set_new (root, "summary", json_string (summary ? summary : ""));
			pkgc_print_json (root);
			continue;
		}

		if (i > 0)
			g_print ("\n"); /* add some visual spacing */

		g_print ("%s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for the ID of an OS upgrade, e.g. "fedora-44" */
			 _("Distribution:"), pkgc_get_reset_color (ctx), id);
		g_print ("%s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for the kind of an OS upgrade, e.g. "stable" */
			 _("Type:"), pkgc_get_reset_color (ctx), state);

		if (summary && summary[0] != '\0') {
			g_print ("%s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for the summary of an OS upgrade */
				 _("Summary:"), pkgc_get_reset_color (ctx), summary);
		}
	}
}

static gint
pkgc_repo_sort_func (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (pk_repo_detail_get_id (*(PkRepoDetail **) a),
			  pk_repo_detail_get_id (*(PkRepoDetail **) b));
}

/**
 * pkgc_repo_get_name:
 *
 * Get the name to show for a repository, falling back to its ID.
 */
static const gchar *
pkgc_repo_get_name (PkRepoDetail *repo)
{
	const gchar *description = pk_repo_detail_get_description (repo);

	if (description == NULL || description[0] == '\0')
		return pk_repo_detail_get_id (repo);
	return description;
}

/**
 * pkgc_print_repos_json:
 */
static void
pkgc_print_repos_json (GPtrArray *repos)
{
	for (guint i = 0; i < repos->len; i++) {
		PkRepoDetail *repo = g_ptr_array_index (repos, i);
		const gchar *description = pk_repo_detail_get_description (repo);
		g_autoptr(json_t) root = json_object ();

		json_object_set_new (root, "id", json_string (pk_repo_detail_get_id (repo)));
		json_object_set_new (root,
				     "description",
				     json_string (description ? description : ""));
		json_object_set_new (root,
				     "enabled",
				     json_boolean (pk_repo_detail_get_enabled (repo)));
		pkgc_print_json (root);
	}
}

/**
 * pkgc_print_repos:
 *
 * Print a list of repositories sorted by ID, with the name and status on
 * one line and the ID below, or one JSON object per repository in JSON mode.
 */
void
pkgc_print_repos (PkgcliContext *ctx, GPtrArray *repos)
{
	g_autoptr(GPtrArray) sorted = NULL;

	if (repos == NULL || repos->len == 0)
		return;

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		pkgc_print_repos_json (repos);
		return;
	}

	sorted = pkgc_util_make_sorted_shallow_copy (repos, pkgc_repo_sort_func);

	for (guint i = 0; i < sorted->len; i++) {
		PkRepoDetail *repo = g_ptr_array_index (sorted, i);
		gboolean enabled = pk_repo_detail_get_enabled (repo);
		const gchar *status;

		if (enabled)
			/* TRANSLATORS: state of a repository in the repository list */
			status = _("enabled");
		else
			/* TRANSLATORS: state of a repository in the repository list */
			status = _("disabled");

		g_print ("%s%s%s [%s%s%s]\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 pkgc_repo_get_name (repo),
			 pkgc_get_reset_color (ctx),
			 pkgc_get_color (ctx, enabled ? COLOR_GREEN : COLOR_RED),
			 status,
			 pkgc_get_reset_color (ctx));
		if (g_strcmp0 (pk_repo_detail_get_id (repo), "") != 0) {
			g_print ("%*s%s%s%s\n",
				 PKGC_DETAIL_INDENT,
				 "",
				 pkgc_get_color (ctx, COLOR_GRAY),
				 pk_repo_detail_get_id (repo),
				 pkgc_get_reset_color (ctx));
		}
	}
}

/**
 * pkgc_print_refreshed_repos:
 *
 * Print the sources that were processed while refreshing the metadata,
 * in the order the backend reported them, marked by whether they were
 * fetched or skipped. Backends report skipped sources as disabled.
 */
void
pkgc_print_refreshed_repos (PkgcliContext *ctx, GPtrArray *repos)
{
	if (repos == NULL || repos->len == 0)
		return;

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		pkgc_print_repos_json (repos);
		return;
	}
	if (ctx->output_mode == PKGCLI_MODE_QUIET)
		return;

	for (guint i = 0; i < repos->len; i++) {
		PkRepoDetail *repo = g_ptr_array_index (repos, i);
		gboolean fetched = pk_repo_detail_get_enabled (repo);

		g_print ("%s%s%s %s\n",
			 pkgc_get_color (ctx, fetched ? COLOR_GREEN : COLOR_GRAY),
			 fetched ? SYMBOL_CHECK : SYMBOL_CIRCLE,
			 pkgc_get_reset_color (ctx),
			 pkgc_repo_get_name (repo));
	}
}

/**
 * pkgc_print_transaction:
 *
 * Print all information about a transaction, as JSON in JSON mode.
 */
static void
pkgc_print_transaction (PkgcliContext *ctx, PkHistoryItem *transaction)
{
	PkRoleEnum role;
	const gchar *role_text;
	gboolean succeeded;
	guint duration;
	guint uid;
	struct passwd *pw;
	GPtrArray *packages;
	g_autofree gchar *cmdline = NULL;
	g_autofree gchar *tid = NULL;
	g_autofree gchar *time_started = NULL;

	if (!transaction)
		return;

	/* Get data */
	g_object_get (transaction,
		      "role",
		      &role,
		      "tid",
		      &tid,
		      "time-started",
		      &time_started,
		      "succeeded",
		      &succeeded,
		      "duration",
		      &duration,
		      "cmdline",
		      &cmdline,
		      "uid",
		      &uid,
		      NULL);
	packages = pk_history_item_get_packages (transaction);

	role_text = pk_role_enum_to_string (role);

	if (ctx->output_mode == PKGCLI_MODE_JSON) {
		g_autoptr(json_t) root = json_object ();
		json_object_set_new (root, "tid", json_string (tid));
		json_object_set_new (root, "role", json_string (role_text));
		json_object_set_new (root, "succeeded", json_boolean (succeeded));
		json_object_set_new (root, "duration", json_integer (duration));
		json_object_set_new (root, "uid", json_integer (uid));
		json_object_set_new (root,
				     "time_started",
				     json_string (time_started ? time_started : ""));

		if (cmdline && cmdline[0] != '\0')
			json_object_set_new (root, "cmdline", json_string (cmdline));

		pkgc_print_json (root);

		return;
	}
	g_print ("%s%s%s %s\n",
		 pkgc_get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction information */
		 _("Transaction:"), pkgc_get_reset_color (ctx), tid);
	g_print (" %s%s%s %s\n",
		 pkgc_get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for system time of the transaction */
		 _("System time:"), pkgc_get_reset_color (ctx), time_started ? time_started : "");
	g_print (" %s%s%s %s%s%s\n",
		 pkgc_get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction success status */
		 _("Succeeded:"),
		   pkgc_get_reset_color (ctx),
		   succeeded ? pkgc_get_color (ctx, COLOR_GREEN) : pkgc_get_color (ctx, COLOR_RED),
		   succeeded
		   ? _("True")
		   : _("False"), pkgc_get_reset_color (ctx));
	g_print (" %s%s%s %s\n",
		 pkgc_get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction role */
		 _("Role:"), pkgc_get_reset_color (ctx), role_text);

	if (duration > 0) {
		g_autofree gchar *duration_str = pkgc_util_format_time (duration);
		g_print (" %s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for transaction duration */
			 _("Duration:"), pkgc_get_reset_color (ctx), duration_str);
	}

	if (cmdline && cmdline[0] != '\0') {
		g_print (" %s%s%s %s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for transaction command line */
			 _("Command line:"), pkgc_get_reset_color (ctx), cmdline);
	}

	g_print (" %s%s%s %u\n",
		 pkgc_get_color (ctx, COLOR_BOLD),
		 /* TRANSLATORS: Label for transaction user ID */
		 _("User ID:"), pkgc_get_reset_color (ctx), uid);

	/* Query real name */
	pw = getpwuid (uid);
	if (pw != NULL) {
		if (pw->pw_name != NULL) {
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for transaction username */
				 _("Username:"), pkgc_get_reset_color (ctx), pw->pw_name);
		}
		if (pw->pw_gecos != NULL) {
			g_print (" %s%s%s %s\n",
				 pkgc_get_color (ctx, COLOR_BOLD),
				 /* TRANSLATORS: Label for transaction real name */
				 _("Real name:"), pkgc_get_reset_color (ctx), pw->pw_gecos);
		}
	}

	if (packages != NULL && packages->len > 0) {
		g_autoptr(GPtrArray) sorted = pkgc_util_make_sorted_shallow_copy (
		    packages,
		    pk_console_package_sort_func);

		g_print (" %s%s%s\n",
			 pkgc_get_color (ctx, COLOR_BOLD),
			 /* TRANSLATORS: Label for affected packages in transaction */
			 _("Affected packages:"), pkgc_get_reset_color (ctx));
		for (guint i = 0; i < sorted->len; i++) {
			PkPackage *pkg = g_ptr_array_index (sorted, i);
			g_autofree gchar *package = pk_package_id_to_printable (
			    pk_package_get_id (pkg));
			g_print ("   - %s %s\n",
				 pk_info_enum_to_string (pk_package_get_info (pkg)),
				 package);
		}
	}
}

/**
 * pkgc_print_transactions:
 *
 * Print a list of past transactions as a table with one row per
 * transaction, or with all details if @details is set and in JSON mode.
 */
void
pkgc_print_transactions (PkgcliContext *ctx, GPtrArray *transactions, gboolean details)
{
	g_autoptr(PkConsoleTable) table = NULL;
	g_autofree gchar *rendered = NULL;

	if (transactions == NULL || transactions->len == 0)
		return;

	if (ctx->output_mode == PKGCLI_MODE_JSON || details) {
		for (guint i = 0; i < transactions->len; i++) {
			pkgc_print_transaction (ctx, g_ptr_array_index (transactions, i));
			if (ctx->output_mode != PKGCLI_MODE_JSON && i + 1 < transactions->len)
				g_print ("\n"); /* add some visual spacing */
		}
		return;
	}

	table = pk_console_table_new (6);
	if (ctx->output_mode != PKGCLI_MODE_QUIET) {
		/* clang-format off */
		pk_console_table_set_header (table,
					     /* TRANSLATORS: column header in the transaction history */
					     _("Date"),
					     /* TRANSLATORS: column header in the transaction history */
					     _("Action"),
					     "",
					     /* TRANSLATORS: column header in the transaction history */
					     _("Packages"),
					     /* TRANSLATORS: column header in the transaction history */
					     _("Duration"),
					     /* TRANSLATORS: column header in the transaction history */
					     _("User"));
		/* clang-format on */
	}

	for (guint i = 0; i < transactions->len; i++) {
		PkHistoryItem *transaction = g_ptr_array_index (transactions, i);
		GDateTime *time_started = pk_history_item_get_time_started (transaction);
		GPtrArray *packages = pk_history_item_get_packages (transaction);
		gboolean succeeded = pk_history_item_get_succeeded (transaction);
		guint duration = pk_history_item_get_duration (transaction);
		guint uid = pk_history_item_get_uid (transaction);
		struct passwd *pw = getpwuid (uid);
		g_autofree gchar *date_str = NULL;
		g_autofree gchar *n_packages_str = NULL;
		g_autofree gchar *duration_str = NULL;
		g_autofree gchar *user_str = NULL;

		if (time_started != NULL) {
			g_autoptr(GDateTime) local = g_date_time_to_local (time_started);
			date_str = g_date_time_format (local, "%Y-%m-%d %H:%M");
		}
		if (packages != NULL && packages->len > 0)
			n_packages_str = g_strdup_printf ("%u", packages->len);
		if (duration > 0)
			duration_str = pkgc_util_format_time (duration);
		if (pw != NULL && pw->pw_name != NULL)
			user_str = g_strdup (pw->pw_name);
		else
			user_str = g_strdup_printf ("%u", uid);

		pk_console_table_add_row (
		    table,
		    date_str,
		    pk_role_enum_to_string (pk_history_item_get_role (transaction)),
		    succeeded ? SYMBOL_CHECK : SYMBOL_CROSS,
		    n_packages_str,
		    duration_str,
		    user_str);
		pk_console_table_set_color (table, 2, succeeded ? COLOR_GREEN : COLOR_RED);
	}

	rendered = pk_console_table_render (table,
					    pk_console_get_width (),
					    pkgc_can_use_colors (ctx));
	g_print ("%s", rendered);
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
pkgc_resolve_package (PkgcliContext *ctx,
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
				     PKGC_ERROR,
				     pk_error_get_code (error_code),
				     pk_error_get_details (error_code));
		return NULL;
	}

	/* nothing found */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     PKGC_ERROR,
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
			     PKGC_ERROR,
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
		g_print ("%u. %s [%s]\n", i + 1, printable, split[PK_PACKAGE_ID_ORIGIN]);
	}

	/* prompt user for selection */
	g_print (_("Please choose the correct package: "));
	if (scanf ("%u", &idx) != 1 || idx < 1 || idx > array->len) {
		g_set_error_literal (error,
				     PKGC_ERROR,
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
pkgc_resolve_packages (PkgcliContext *ctx, PkBitfield filters, gchar **packages, GError **error)
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
					     PKGC_ERROR,
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
				     PKGC_ERROR,
				     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				     "No packages were found");
		return NULL;
	}

	/* convert to GStrv */
	g_ptr_array_add (array, NULL);
	return g_strdupv ((gchar **) array->pdata);
}
