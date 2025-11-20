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
#include "pkgc-repo.h"

#include <glib.h>

#include "pkgc-util.h"


/**
 * pkgc_repo_on_task_finished_cb:
 */
static void
pkgc_repo_on_task_finished_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	PkgctlContext *ctx = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	results = pk_task_generic_finish (PK_TASK (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGCTL_EXIT_FAILURE;

		goto out;
	}

	if (results) {
		/* Process repo details */
		array = pk_results_get_repo_detail_array (results);
		for (guint i = 0; i < array->len; i++) {
			PkRepoDetail *repo = g_ptr_array_index (array, i);
			pkgc_print_repo (ctx, repo);
		}
	}

out:
	g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_repo_list:
 *
 * List configured repositories.
 */
static int
pkgc_repo_list (PkgctlContext *ctx, int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = g_option_context_new (NULL);
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	pk_task_get_repo_list_async (PK_TASK (ctx->task),
				     ctx->filters,
				     ctx->cancellable,
				     pkgc_context_on_progress_cb,
				     ctx,
				     pkgc_repo_on_task_finished_cb,
				     ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_repo_enable:
 *
 * Enable a repository
 */
static int
pkgc_repo_enable (PkgctlContext *ctx, int argc, char **argv)
{
	const char *repo_id;

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s REPO-ID"), "pkgctl repo-enable");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	repo_id = argv[1];

	/* Enable repository */
	pk_task_repo_enable_async (PK_TASK (ctx->task),
				   repo_id,
				   TRUE, /* enable */
				   ctx->cancellable,
				   pkgc_context_on_progress_cb,
				   ctx,
				   pkgc_repo_on_task_finished_cb,
				   ctx);

	g_main_loop_run (ctx->loop);

	if (ctx->exit_code == PKGCTL_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("Repository '%s' enabled"), repo_id);

	return ctx->exit_code;
}

/**
 * pkgc_repo_disable:
 *
 * Disable a repository
 */
static int
pkgc_repo_disable (PkgctlContext *ctx, int argc, char **argv)
{
	const char *repo_id;

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s REPO-ID"), "pkgctl repo-disable");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	repo_id = argv[1];

	/* Disable repository */
	pk_task_repo_enable_async (PK_TASK (ctx->task),
				   repo_id,
				   FALSE, /* disable */
				   ctx->cancellable,
				   pkgc_context_on_progress_cb,
				   ctx,
				   pkgc_repo_on_task_finished_cb,
				   ctx);

	g_main_loop_run (ctx->loop);

	if (ctx->exit_code == PKGCTL_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("Repository '%s' disabled"), repo_id);

	return ctx->exit_code;
}

/**
 * pkgc_repo_remove:
 *
 * Remove a repository
 */
static int
pkgc_repo_remove (PkgctlContext *ctx, int argc, char **argv)
{
	gboolean autoremove = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	const GOptionEntry options[] = {
		{ "autoremove",
		  0,		  0,
		  G_OPTION_ARG_NONE,	  &autoremove,
		  N_ ("Automatically remove orphaned packages"),
		  NULL	},
		{ NULL,	0, 0, 0, NULL, NULL,NULL }
	};

	/* Parse options */
	option_context = g_option_context_new ("REPO-ID");
	g_option_context_set_help_enabled (option_context, TRUE);
	g_option_context_add_main_entries (option_context, options, NULL);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s REPO-ID"), "pkgctl repo-remove");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	/* Remove repository */
	pk_client_repo_remove_async (PK_CLIENT (ctx->task),
				     PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED,
				     argv[1], /* repo_id */
				     autoremove,
				     ctx->cancellable,
				     pkgc_context_on_progress_cb,
				     ctx,
				     pkgc_repo_on_task_finished_cb,
				     ctx);

	g_main_loop_run (ctx->loop);

	if (ctx->exit_code == PKGCTL_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("Repository '%s' removed"), argv[1]);

	return ctx->exit_code;
}

/**
 * pkgc_register_repo_commands:
 *
 * Register repository commands
 */
void
pkgc_register_repo_commands (PkgctlContext *ctx)
{
	pkgc_context_register_command (ctx,
				       "repo-list",
				       pkgc_repo_list,
				       N_ ("List repositories"),
				       N_ ("Usage: pkgctl repo-list\n"
					   "List all configured package repositories."));

	pkgc_context_register_command (ctx,
				       "repo-enable",
				       pkgc_repo_enable,
				       N_ ("Enable a repository"),
				       N_ ("Usage: pkgctl repo-enable REPO-ID\n"
					   "Enable the specified repository."));

	pkgc_context_register_command (ctx,
				       "repo-disable",
				       pkgc_repo_disable,
				       N_ ("Disable a repository"),
				       N_ ("Usage: pkgctl repo-disable REPO-ID\n"
					   "Disable the specified repository."));

	pkgc_context_register_command (ctx,
				       "repo-remove",
				       pkgc_repo_remove,
				       N_ ("Remove a repository"),
				       N_ ("Usage: pkgctl repo-remove REPO-ID [--autoremove]\n"
					   "Remove the specified repository."));
}
