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
#include <jansson.h>

#include "pkgc-query.h"
#include "pkgc-util.h"


/**
 * pkgc_query_on_task_finished_cb:
 */
static void
pkgc_query_on_task_finished_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
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
		ctx->exit_code = PKGC_EXIT_FAILURE;
		g_main_loop_quit (ctx->loop);
		return;
	}

	if (results == NULL)
		goto out;

	/* Process packages */
	array = pk_results_get_package_array (results);
	for (guint i = 0; i < array->len; i++) {
		PkPackage *package = PK_PACKAGE (g_ptr_array_index (array, i));
		pkgc_print_package (ctx, package);
	}

	/* Process details */
	g_clear_pointer (&array, g_ptr_array_unref);
	array = pk_results_get_details_array (results);
	for (guint i = 0; i < array->len; i++) {
		PkDetails *details = PK_DETAILS (g_ptr_array_index (array, i));
		pkgc_print_package_detail (ctx, details);
	}

	/* Process files */
	g_clear_pointer (&array, g_ptr_array_unref);
	array = pk_results_get_files_array (results);
	for (guint i = 0; i < array->len; i++) {
		PkFiles *files = PK_FILES (g_ptr_array_index (array, i));
		gchar **filelist = NULL;
		const char *package_id;

		package_id = pk_files_get_package_id (files);
		filelist = pk_files_get_files (files);

		if (ctx->output_mode == PKGCTL_MODE_JSON) {
			json_t *root;
			json_t *files_array;

			root = json_object ();
			files_array = json_array ();

			json_object_set_new (root, "package", json_string (package_id));

			for (guint j = 0; filelist && filelist[j] != NULL; j++)
				json_array_append_new (files_array, json_string (filelist[j]));

			json_object_set_new (root, "files", files_array);

			pkgc_print_json_decref (root);
		} else {
			for (guint j = 0; filelist && filelist[j] != NULL; j++)
				g_print ("%s\n", filelist[j]);
		}
	}

out:
	g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_query_on_client_finished_cb:
 */
static void
pkgc_query_on_client_finished_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	PkgctlContext *ctx = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;

	results = pk_client_generic_finish (PK_CLIENT (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGC_EXIT_FAILURE;

		goto out;
	}

	if (results) {
		g_autoptr(GPtrArray) array = NULL;

		/* Process transactions */
		array = pk_results_get_transaction_array (results);
		for (guint i = 0; i < array->len; i++) {
			PkTransactionPast *transaction = g_ptr_array_index (array, i);
			pkgc_print_transaction (ctx, transaction);
			g_print("\n"); /* add some visual spacing */
		}
	}

out:
	g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_backend_info:
 *
 * Show system status.
 */
static gint
pkgc_backend_info (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_autofree gchar *backend_name = NULL;
	g_autofree gchar *backend_description = NULL;
	g_autofree gchar *backend_author = NULL;
	g_autofree gchar *roles_str;
	PkBitfield roles = 0;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgctl backend */
		_("Show PackageKit backend information."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* get control properties */
	g_object_get (ctx->control,
		      "backend-name",
		      &backend_name,
		      "backend-description",
		      &backend_description,
		      "backend-author",
		      &backend_author,
		      "roles",
		      &roles,
		      NULL);

	roles_str = pk_role_bitfield_to_string (roles);

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root;
		json_t *backend_obj;

		root = json_object ();
		backend_obj = json_object ();

		json_object_set_new (backend_obj,
				     "name",
				     json_string (backend_name ? backend_name : ""));
		json_object_set_new (backend_obj,
				     "description",
				     json_string (backend_description ? backend_description : ""));
		json_object_set_new (backend_obj,
				     "author",
				     json_string (backend_author ? backend_author : ""));
		json_object_set_new (root, "backend", backend_obj);

		json_object_set_new (root, "roles", json_string (roles_str));

		pkgc_print_json_decref (root);
	} else {
		g_print ("%sStatus:%s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));

		if (backend_name != NULL)
			pkgc_println (_("Backend: %s"), backend_name);

		if (backend_description != NULL)
			pkgc_println (_("Description: %s"), backend_description);

		if (backend_author != NULL)
			pkgc_println (_("Author: %s"), backend_author);

		if (roles_str != NULL) {
			g_print ("\n"); /* add some extra space before the potentially long roles list */
			/* TRANSLATORS: List of backend-roles */
			pkgc_println (_("Roles: %s"), roles_str);
		}
	}

	return PKGC_EXIT_SUCCESS;
}

/**
 * pkgc_history:
 *
 * Print transaction history.
 */
static gint
pkgc_history (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	guint limit = 10;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[LIMIT]",
		/* TRANSLATORS: Description for pkgctl history */
		_("Show recent package management transactions."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Parse optional limit */
	if (argc >= 2) {
		limit = atoi (argv[1]);
		if (limit == 0) {
			limit = 10;
		}
	}

	/* Get transaction history */
	pk_client_get_old_transactions_async (PK_CLIENT (ctx->task),
					      limit,
					      ctx->cancellable,
					      pkgc_context_on_progress_cb,
					      ctx,
					      pkgc_query_on_client_finished_cb,
					      ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_search:
 */
static gint
pkgc_query_search (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	const gchar *search_mode = "details";
	const gchar **search_terms;
	const gchar *cmd_description;
	guint search_count;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* TRANSLATORS: Description of the pkgctl search command. MODE values must not be translated! */
	cmd_description = _("Search for packages matching the given patterns. If MODE is not specified, \n"
						"'details' search is performed.\n"
						"Possible search MODEs are:\n"
						"  name    - search by package name\n"
						"  details - search by package details (default)\n"
						"  file    - search by file name\n"
						"  group   - search by package group");

	/* parse options */
	option_context = pkgc_option_context_for_command (ctx, cmd,
												  "[MODE] PATTERN...",
												  cmd_description);
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Check if first argument is a search type */
	if (argc >= 3 && (strcmp (argv[1], "name") == 0 || strcmp (argv[1], "details") == 0 ||
			  strcmp (argv[1], "file") == 0 || strcmp (argv[1], "group") == 0)) {
		search_mode = argv[1];
		search_terms = (const char **) (argv + 2);
		search_count = argc - 2;
	} else {
		search_terms = (const char **) (argv + 1);
		search_count = argc - 1;
	}

	if (search_count == 0) {
		pkgc_print_error (ctx, _("No search pattern specified"));
		return PKGC_EXIT_SYNTAX_ERROR;
	}

	/* Perform search based on type */
	if (g_strcmp0 (search_mode, "name") == 0) {
		pk_task_search_names_async (PK_TASK (ctx->task),
					    ctx->filters,
					    (gchar **) search_terms,
					    ctx->cancellable,
					    pkgc_context_on_progress_cb,
					    ctx,
					    pkgc_query_on_task_finished_cb,
					    ctx);
	} else if (g_strcmp0 (search_mode, "details") == 0) {
		pk_task_search_details_async (PK_TASK (ctx->task),
					      ctx->filters,
					      (gchar **) search_terms,
					      ctx->cancellable,
					      pkgc_context_on_progress_cb,
					      ctx,
					      pkgc_query_on_task_finished_cb,
					      ctx);
	} else if (g_strcmp0 (search_mode, "file") == 0) {
		pk_task_search_files_async (PK_TASK (ctx->task),
					    ctx->filters,
					    (gchar **) search_terms,
					    ctx->cancellable,
					    pkgc_context_on_progress_cb,
					    ctx,
					    pkgc_query_on_task_finished_cb,
					    ctx);
	} else if (g_strcmp0 (search_mode, "group") == 0) {
		pk_task_search_groups_async (PK_TASK (ctx->task),
					     ctx->filters,
					     (gchar **) search_terms,
					     ctx->cancellable,
					     pkgc_context_on_progress_cb,
					     ctx,
					     pkgc_query_on_task_finished_cb,
					     ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_list:
 */
static gint
pkgc_query_list (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[PATTERN]",
		/* TRANSLATORS: Description for pkgctl list */
		_("List all packages or those matching a pattern."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* if patterns provided, search by name */
	if (argc >= 2) {
		pk_task_search_names_async (PK_TASK (ctx->task),
					    ctx->filters,
					    argv + 1,
					    ctx->cancellable,
					    pkgc_context_on_progress_cb,
					    ctx,
					    pkgc_query_on_task_finished_cb,
					    ctx);
	} else {
		/* list all packages */
		pk_task_get_packages_async (PK_TASK (ctx->task),
					    ctx->filters,
					    ctx->cancellable,
					    pkgc_context_on_progress_cb,
					    ctx,
					    pkgc_query_on_task_finished_cb,
					    ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_show:
 */
static gint
pkgc_query_show (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgctl show */
		_("Show information about one or more packages."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* TODO: Do we support mixed local packages and remote ones? Local handling should be improved... */
	if (g_file_test ((argv + 1)[0], G_FILE_TEST_EXISTS)) {
		/* get details for local package files */
		pk_client_get_details_local_async (PK_CLIENT (ctx->task),
						   argv + 1,
						   ctx->cancellable,
						   pkgc_context_on_progress_cb,
						   ctx,
						   pkgc_query_on_client_finished_cb,
						   ctx);
	} else{
		package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
		if (package_ids == NULL) {
			if (error) {
				/* TRANSLATORS: There was an error getting the
				 * details about the package. The detailed error follows */
				pkgc_print_error (ctx,
						  _("Could not find packages: %s"), error->message);
			}

			return PKGC_EXIT_FAILURE;
		}

		/* get package details */
		pk_task_get_details_async (PK_TASK (ctx->task),
					   package_ids,
					   ctx->cancellable,
					   pkgc_context_on_progress_cb,
					   ctx,
					   pkgc_query_on_task_finished_cb,
					   ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_depends_on:
 *
 * Display which other packages this package depends on.
 */
static gint
pkgc_query_depends_on (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	gboolean recursive = FALSE;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GOptionContext) option_context = NULL;
	g_autoptr(GError) error = NULL;

	const GOptionEntry options[] = {
		{ "recursive", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &recursive,
		  _("Check dependencies recursively"), NULL },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgctl depends-on */
		_("Show dependencies for one or more packages."));
	g_option_context_add_main_entries (option_context, options, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		pkgc_print_error (ctx, _("Could not resolve packages: %s"), error->message);
		return PKGC_EXIT_FAILURE;
	}

	/* get dependencies */
	pk_task_depends_on_async (PK_TASK (ctx->task),
				  ctx->filters,
				  package_ids,
				  recursive,
				  ctx->cancellable,
				  pkgc_context_on_progress_cb,
				  ctx,
				  pkgc_query_on_task_finished_cb,
				  ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_what_provides:
 */
static gint
pkgc_query_what_provides (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"CAPABILITY...",
		/* TRANSLATORS: Description for pkgctl what-provides */
		_("Show which packages provide the specified capability."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	pk_task_what_provides_async (PK_TASK (ctx->task),
				     ctx->filters,
				     argv + 1,
				     ctx->cancellable,
				     pkgc_context_on_progress_cb,
				     ctx,
				     pkgc_query_on_task_finished_cb,
				     ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_files:
 */
static gint
pkgc_query_files (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_autoptr(GError) error = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgctl files */
		_("List all files contained in one or more packages."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* TODO: Do we support mixed local packages and remote ones? Local handling should be improved... */
	if (g_file_test ((argv + 1)[0], G_FILE_TEST_EXISTS)) {
		/* get file list from local package files */
		pk_client_get_files_local_async (PK_CLIENT (ctx->task),
						 argv + 1,
						 ctx->cancellable,
						 pkgc_context_on_progress_cb,
						 ctx,
						 pkgc_query_on_client_finished_cb,
						 ctx);
	} else {
		g_auto(GStrv) package_ids = NULL;

		package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
		if (package_ids == NULL) {
			pkgc_print_error (ctx, _("Could not resolve packages: %s"), error->message);
			return PKGC_EXIT_FAILURE;
		}

		/* get files list */
		pk_task_get_files_async (PK_TASK (ctx->task),
					 package_ids,
					 ctx->cancellable,
					 pkgc_context_on_progress_cb,
					 ctx,
					 pkgc_query_on_task_finished_cb,
					 ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_on_updates_finished_cb:
 */
static void
pkgc_on_updates_finished_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	PkgctlContext *ctx = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;

	results = pk_task_generic_finish (PK_TASK (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error != NULL) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGC_EXIT_FAILURE;
		g_main_loop_quit (ctx->loop);
		return;
	}

	if (results != NULL) {
		g_autoptr(GPtrArray) array = NULL;

		/* process packages for list-updates */
		array = pk_results_get_package_array (results);
		for (guint i = 0; i < array->len; i++) {
			PkPackage *package = g_ptr_array_index (array, i);
			pkgc_print_package (ctx, package);
		}

		g_clear_pointer (&array, g_ptr_array_unref);

		/* process update details for get-update-detail */
		array = pk_results_get_update_detail_array (results);
		for (guint i = 0; i < array->len; i++) {
			PkUpdateDetail *update = g_ptr_array_index (array, i);
			pkgc_print_update_detail (ctx, update);
		}
	}

	g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_updates_list_updates:
 */
static gint
pkgc_updates_list_updates (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgctl list-updates */
		_("List all currently available package updates."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* get available updates */
	pk_task_get_updates_async (PK_TASK (ctx->task),
				   ctx->filters,
				   ctx->cancellable,
				   pkgc_context_on_progress_cb,
				   ctx,
				   pkgc_on_updates_finished_cb,
				   ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_updates_show_update:
 */
static gint
pkgc_updates_show_update (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgctl show-update */
		_("Show detailed information about the specified package update."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		pkgc_print_error (ctx, _("Could not resolve packages: %s"), error->message);
		return PKGC_EXIT_FAILURE;
	}

	/* get update details for specific packages */
	pk_task_get_update_detail_async (PK_TASK (ctx->task),
					 package_ids,
					 ctx->cancellable,
					 pkgc_context_on_progress_cb,
					 ctx,
					 pkgc_on_updates_finished_cb,
					 ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_resolve:
 */
static gint
pkgc_query_resolve (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	PkBitfield filters;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgctl resolve */
		_("Resolve package names to package IDs."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* we run this without our default filters, unless the user has explicitly specified some */
	filters = ctx->user_filters_set? ctx->filters : 0;

	/* resolve package names to package IDs */
	pk_task_resolve_async (PK_TASK (ctx->task),
			       filters,
			       argv + 1,
			       ctx->cancellable,
			       pkgc_context_on_progress_cb,
			       ctx,
			       pkgc_query_on_task_finished_cb,
			       ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_required_by:
 */
static gint
pkgc_query_required_by (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	gboolean recursive = FALSE;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	const GOptionEntry options[] = {
		{ "recursive", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &recursive,
		  _("Check dependencies recursively"), NULL },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgctl required-by */
		_("Show which packages require the specified packages."));
	g_option_context_add_main_entries (option_context, options, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_INSTALLED);
	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		pkgc_print_error (ctx, _("Could not find packages: %s"), error->message);
		return PKGC_EXIT_FAILURE;
	}

	/* get packages that require this package */
	pk_task_required_by_async (PK_TASK (ctx->task),
				   ctx->filters,
				   package_ids,
				   recursive,
				   ctx->cancellable,
				   pkgc_context_on_progress_cb,
				   ctx,
				   pkgc_query_on_task_finished_cb,
				   ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_organization:
 */
static gint
pkgc_query_organization (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_autofree gchar *text = NULL;
	PkBitfield groups;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgctl organization */
		_("List all available filters, groups and categories for package organization."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* print available filters */
	g_print ("%s%s%s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 /* TRANSLATORS: Header for list of available package filters */
			 _("Filters:"),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
	g_object_get (ctx->control,
				  "filters", &ctx->filters,
				  NULL);
	text = pk_filter_bitfield_to_string (ctx->filters);
	g_strdelimit (text, ";", '\n');
	g_print ("%s\n", text);
	g_clear_pointer (&text, g_free);

	/* print available groups */
	g_print ("\n");
	g_print ("%s%s%s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 /* TRANSLATORS: Header for list of available package groups */
			 _("Groups:"),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
	g_object_get (ctx->control,
				  "groups", &groups,
				  NULL);
	text = pk_group_bitfield_to_string (groups);
	g_strdelimit (text, ";", '\n');
	g_print ("%s\n", text);
	g_clear_pointer (&text, g_free);

	/* print available categories, if we have any */
	g_print ("\n");
	g_print ("%s%s%s\n",
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_BOLD),
			 /* TRANSLATORS: Header for list of available package categories */
			 _("Categories:"),
			 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
	pk_task_get_categories_async (PK_TASK (ctx->task),
				      ctx->cancellable,
				      pkgc_context_on_progress_cb,
				      ctx,
				      pkgc_query_on_task_finished_cb,
				      ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_show_os_upgrade:
 */
static gint
pkgc_query_show_os_upgrade (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgctl show-distro-upgrade */
		_("Show distribution version upgrades, if any are available."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Get available distribution upgrades */
	pk_client_get_distro_upgrades_async (PK_CLIENT (ctx->task),
					     ctx->cancellable,
					     pkgc_context_on_progress_cb,
					     ctx,
					     pkgc_query_on_client_finished_cb,
					     ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_query_get_time_since_action_cb:
 */
static void
pkgc_query_get_time_since_action_cb (GObject *object, GAsyncResult *res, gpointer data)
{
	guint time_s;
	PkgctlContext *ctx = (PkgctlContext *) data;
	g_autoptr(GError) error = NULL;

	/* get the results */
	time_s = pk_control_get_time_since_action_finish (ctx->control, res, &error);
	if (time_s == 0) {
		/* TRANSLATORS: we keep a database updated with the time that an
		 * action was last executed */
		g_print ("%s: %s\n", _("Failed to get the time since this action was last completed"), error->message);
		goto out;
	}

	if (ctx->output_mode == PKGCTL_MODE_JSON) {
		json_t *root;

		root = json_object ();
		json_object_set_new (root, "time_sec", json_integer (time_s));

		pkgc_print_json_decref (root);
	} else {
		/* TRANSLATORS: this is the time since this role was used */
		g_print ("%s: %is\n", _("Elapsed time"), time_s);
	}

out:
		g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_query_last_time:
 *
 * Get time since last action.
 */
static gint
pkgc_query_last_time (PkgctlContext *ctx, PkgctlCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	const gchar *value = NULL;
	PkRoleEnum role = PK_ROLE_ENUM_UNKNOWN;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[ROLE]",
		/* TRANSLATORS: Description for pkgctl last-time */
		_("Get time in seconds since the last specified action."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	if (argc >= 2)
		value = argv[1];

	if (value == NULL) {
		pkgc_print_error (ctx,
				/* TRANSLATORS: The user didn't specify what action to use */
				 "%s", _("An action, e.g. 'update-packages' is required"));
		return PKGC_EXIT_FAILURE;
	}
	role = pk_role_enum_from_string (value);
	if (role == PK_ROLE_ENUM_UNKNOWN) {
		pkgc_print_error (ctx,
				/* TRANSLATORS: The user specified an invalid action */
			 "%s", _("A correct role is required"));
		return PKGC_EXIT_FAILURE;
	}
	pk_control_get_time_since_action_async (ctx->control,
						role,
						ctx->cancellable,
						pkgc_query_get_time_since_action_cb, ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_register_query_commands:
 */
void
pkgc_register_query_commands (PkgctlContext *ctx)
{
	pkgc_context_register_command (
		ctx,
		"backend",
		pkgc_backend_info,
		_("Show backend information"));

	pkgc_context_register_command (
		ctx,
		"history",
		pkgc_history,
		_("Show transaction history"));

	pkgc_context_register_command (
		ctx,
		"search",
		pkgc_query_search,
		_("Search for packages"));

	pkgc_context_register_command (
		ctx,
		"list",
		pkgc_query_list,
		_("List packages"));

	pkgc_context_register_command (
		ctx,
		"show",
		pkgc_query_show,
		_("Show package information"));

	pkgc_context_register_command (
		ctx,
		"depends-on",
		pkgc_query_depends_on,
		_("Show package dependencies"));

	pkgc_context_register_command (
		ctx,
		"required-by",
		pkgc_query_required_by,
		_("Show packages requiring this package"));

	pkgc_context_register_command (
		ctx,
		"what-provides",
		pkgc_query_what_provides,
		_("Show packages providing a capability"));

	pkgc_context_register_command (
		ctx,
		"files",
		pkgc_query_files,
		_("Show files in package"));

	pkgc_context_register_command (
		ctx,
		"list-updates",
		pkgc_updates_list_updates,
		_("Get available updates"));

	pkgc_context_register_command (
		ctx,
		"show-update",
		pkgc_updates_show_update,
		_("Get update details"));

	pkgc_context_register_command (
		ctx,
		"resolve",
		pkgc_query_resolve,
		_("Resolve package names"));

	pkgc_context_register_command (
		ctx,
		"organization",
		pkgc_query_organization,
		_("List available filters and categories"));

	pkgc_context_register_command (
		ctx,
		"show-os-upgrade",
		pkgc_query_show_os_upgrade,
		_("Show available distribution upgrades"));

	pkgc_context_register_command (
		ctx,
		"last-time",
		pkgc_query_last_time,
		_("Get time since last action"));
}
