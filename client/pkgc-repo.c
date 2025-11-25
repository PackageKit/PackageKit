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

	results = pk_task_generic_finish (PK_TASK (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGC_EXIT_FAILURE;

		goto out;
	}

	if (results) {
		g_autoptr(GPtrArray) array = NULL;

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
static gint
pkgc_repo_list (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgctl repo-list */
		_("List all configured package repositories."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* run */
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
static gint
pkgc_repo_enable (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	const gchar *repo_id;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"REPO-ID",
		/* TRANSLATORS: Description for pkgctl repo-enable */
		_("Enable the specified repository."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

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

	if (ctx->exit_code == PKGC_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("Repository '%s' enabled"), repo_id);

	return ctx->exit_code;
}

/**
 * pkgc_repo_disable:
 *
 * Disable a repository
 */
static gint
pkgc_repo_disable (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	const gchar *repo_id;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"REPO-ID",
		/* TRANSLATORS: Description for pkgctl repo-disable */
		_("Disable the specified repository."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

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

	if (ctx->exit_code == PKGC_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("Repository '%s' disabled"), repo_id);

	return ctx->exit_code;
}

/**
 * pkgc_repo_remove:
 *
 * Remove a repository
 */
static gint
pkgc_repo_remove (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	gboolean autoremove = FALSE;
	g_autoptr(GOptionContext) option_context = NULL;

	const GOptionEntry options[] = {
		{ "autoremove",
		  0,		  0,
		  G_OPTION_ARG_NONE,	  &autoremove,
		  N_ ("Automatically remove orphaned packages"),
		  NULL	},
		{ NULL,	0, 0, 0, NULL, NULL,NULL }
	};

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"REPO-ID",
		/* TRANSLATORS: Description for pkgctl repo-remove */
		_("Remove the specified repository."));
	g_option_context_add_main_entries (option_context, options, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

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

	if (ctx->exit_code == PKGC_EXIT_SUCCESS)
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
	pkgc_context_register_command (
		ctx,
		"repo-list",
		pkgc_repo_list,
		_("List repositories"));

	pkgc_context_register_command (
		ctx,
		"repo-enable",
		pkgc_repo_enable,
		_("Enable a repository"));

	pkgc_context_register_command (
		ctx,
		"repo-disable",
		pkgc_repo_disable,
		_("Disable a repository"));

	pkgc_context_register_command (
		ctx,
		"repo-remove",
		pkgc_repo_remove,
		_("Remove a repository"));
}
