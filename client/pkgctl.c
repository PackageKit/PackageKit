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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <packagekit-glib2/packagekit.h>

#include "pkgc-context.h"
#include "pkgc-util.h"

#include "pkgc-query.h"
#include "pkgc-manage.h"
#include "pkgc-repo.h"

/**
 * pkc_handle_sigint:
 */
static gboolean
pkc_handle_sigint (gpointer user_data)
{
	PkgctlContext *ctx = user_data;

	/* Cancel any running transaction */
	if (ctx->cancellable && !g_cancellable_is_cancelled (ctx->cancellable))
		g_cancellable_cancel (ctx->cancellable);

	/* Quit main loop if running */
	if (ctx->loop && g_main_loop_is_running (ctx->loop))
		g_main_loop_quit (ctx->loop);

	return G_SOURCE_REMOVE;
}

/* Global option variables */
static gboolean opt_version = FALSE;
static gboolean opt_help = FALSE;
static gboolean opt_quiet = FALSE;
static gboolean opt_verbose = FALSE;
static gboolean opt_json = FALSE;
static gboolean opt_yes = FALSE;
static gboolean opt_no_color = FALSE;
static gchar *opt_filter_str = NULL;

/* Global options that apply to all commands */
const GOptionEntry pkgc_global_options[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version,
		N_("Show pkgctl version"), NULL },
	{ "help", 'h', 0, G_OPTION_ARG_NONE, &opt_help,
		N_("Show help"), NULL },
	{ "quiet", 'q', 0, G_OPTION_ARG_NONE, &opt_quiet,
		N_("Only provide minimal output"), NULL },
	{ "verbose", 0, 0, G_OPTION_ARG_NONE, &opt_verbose,
		N_("Show more detailed output"), NULL },
	{ "json", 0, 0, G_OPTION_ARG_NONE, &opt_json,
		N_("Output in JSON format"), NULL },
	{ "no-color", 0, 0, G_OPTION_ARG_NONE, &opt_no_color,
		N_("Disable colored output"), NULL },
	{ "yes", 'y', 0, G_OPTION_ARG_NONE, &opt_yes,
	  N_("Answer 'yes' to all questions"), NULL },
	{ "filter", 'f', 0, G_OPTION_ARG_STRING, &opt_filter_str,
	N_("Filter packages (installed, available, etc.)"), N_("FILTER") },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/**
 * pkgc_dispatch_command:
 * @ctx: a valid #PkgctlContext
 * @argc: argument count
 * @argv: argument vector
 *
 * Dispatch the command based on the command-line arguments.
 *
 * Returns: exit code
 */
static int
pkgc_dispatch_command (PkgctlContext *ctx, int argc, char **argv)
{
	const gchar *command_name;
	const PkgctlCommand *cmd;

	if (argc < 2) {
		pkgc_print_error (ctx,
				  _("No command specified. Use --help for usage information."));
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	command_name = argv[1];
	cmd = pkgc_context_find_command (ctx, command_name);

	if (!cmd) {
		pkgc_print_error (ctx, _("Unknown command: %s"), command_name);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	/* call the command handler */
	return cmd->handler (ctx, argc - 1, argv + 1);
}

/**
 * main:
 */
int
main (int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) context = NULL;
	PkgctlContext *ctx = NULL;
	int ret = PKGCTL_EXIT_SUCCESS;

	/* setup locale */
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* create context early so we can register commands */
	ctx = pkgc_context_new ();

	/* register available commands */
	pkgc_register_query_commands (ctx);
	pkgc_register_manage_commands (ctx);
	pkgc_register_repo_commands (ctx);

	/* check if this is a command-specific help request before parsing options */
	if (argc >= 3) {
		for (gint i = 2; i < argc; i++) {
			if (strcmp (argv[i], "--help") == 0 || strcmp (argv[i], "-h") == 0) {
				/* this is command-specific help, skip global parsing */
				goto skip_global_parse;
			}
		}
	}

	/* parse only global options, ignore unknown ones (they're command-specific) */
	context = g_option_context_new ("COMMAND [OPTIONS...]");
	g_option_context_set_help_enabled (context, FALSE); /* We handle help ourselves to show commands */
	g_option_context_add_main_entries (context, pkgc_global_options, NULL);
	g_option_context_set_ignore_unknown_options (context, TRUE);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		/* TRANSLATORS: Failed to parse command-line options in pkgctl */
		g_printerr (_("Failed to parse options: %s"), error->message);
		g_printerr ("\n");
		ret = PKGCTL_EXIT_SYNTAX_ERROR;
		goto out;
	}

	/* show version */
	if (opt_version) {
		g_print (_("Version: %s"), VERSION);
		g_print ("\n");
		ret = PKGCTL_EXIT_SUCCESS;
		goto out;
	}

	/* show help */
	if (opt_help || argc < 2) {
		g_autofree gchar *help_text = NULL;

		help_text = g_option_context_get_help (context, TRUE, NULL);
		g_print ("%s", help_text);

		/* Print available commands */
		g_print ("\n");
		g_print ("%s\n", _("Available Commands:"));
		for (guint i = 0; i < ctx->commands->len; i++) {
			PkgctlCommand *cmd = g_ptr_array_index (ctx->commands, i);
			g_print ("  %-23s %s\n", cmd->name, _(cmd->description));
		}
		g_print ("\n");
		g_print ("%s\n", _("Use 'pkgctl COMMAND --help' for command-specific help."));

		ret = PKGCTL_EXIT_SUCCESS;
		goto out;
	}

skip_global_parse:

	/* setup signal handler */
	g_unix_signal_add_full (G_PRIORITY_DEFAULT, SIGINT, pkc_handle_sigint, ctx, NULL);

	/* parse output mode from global options */
	if (opt_json)
		ctx->output_mode = PKGCTL_MODE_JSON;
	else if (opt_quiet)
		ctx->output_mode = PKGCTL_MODE_QUIET;
	else if (opt_verbose)
		ctx->output_mode = PKGCTL_MODE_VERBOSE;

	if (ctx->output_mode == PKGCTL_MODE_VERBOSE)
		pk_debug_set_verbose (TRUE);

	/* disable colored output if NO_COLOR is present */
	if (g_getenv ("NO_COLOR") != NULL || opt_no_color)
		ctx->no_color = TRUE;

	/* -y flag means non-interactive */
	ctx->noninteractive = opt_yes;

	/* set user-defined filter if we have one */
	if (opt_filter_str != NULL) {
		ctx->filters = pk_filter_bitfield_from_string (opt_filter_str);
		ctx->user_filters_set = TRUE;
		if (ctx->filters == 0) {
			pkgc_print_error(ctx, "%s: %s",
						 /* TRANSLATORS: The user specified
						  * an incorrect filter */
						 _("The filter specified was invalid"),
						 opt_filter_str);
			ret = PKGCTL_EXIT_SYNTAX_ERROR;
			goto out;
		}
	}

	/* connect to PK with the selected parameters */
	if (!pkgc_context_init (ctx, &error)) {
		pkgc_print_error (ctx, _("Failed to connect to PackageKit: %s"), error->message);
		ret = PKGCTL_EXIT_FAILURE;
		goto out;
	}

	/* start polkit agent to listen for authentication requests */
	pk_polkit_agent_open ();

	/* dispatch command */
	ret = pkgc_dispatch_command (ctx, argc, argv);

out:
	/* stop listening for polkit questions */
	pk_polkit_agent_close ();

	pkgc_context_free (ctx);
	g_clear_pointer (&opt_filter_str, g_free);

	return ret;
}
