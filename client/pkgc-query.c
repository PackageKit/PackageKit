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
		ctx->exit_code = PKGCTL_EXIT_FAILURE;
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
	array = pk_results_get_details_array (results);
	for (guint i = 0; i < array->len; i++) {
		PkDetails *details = PK_DETAILS (g_ptr_array_index (array, i));
		pkgc_print_package_detail (ctx, details);
	}

	/* Process files */
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
			gchar *json_str;

			root = json_object ();
			files_array = json_array ();

			json_object_set_new (root, "package", json_string (package_id));

			for (guint j = 0; filelist && filelist[j] != NULL; j++)
				json_array_append_new (files_array, json_string (filelist[j]));

			json_object_set_new (root, "files", files_array);

			json_str = json_dumps (root, JSON_COMPACT);
			if (json_str) {
				g_print ("%s\n", json_str);
				free (json_str);
			}
			json_decref (root);
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
	g_autoptr(GPtrArray) array = NULL;

	results = pk_client_generic_finish (PK_CLIENT (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGCTL_EXIT_FAILURE;

		goto out;
	}

	if (results) {
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
static int
pkgc_backend_info (PkgctlContext *ctx, int argc, char **argv)
{
	g_autofree gchar *backend_name = NULL;
	g_autofree gchar *backend_description = NULL;
	g_autofree gchar *backend_author = NULL;
	g_autofree gchar *roles_str;
	PkBitfield roles = 0;

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
		gchar *json_str;

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

		json_str = json_dumps (root, JSON_COMPACT);
		if (json_str) {
			g_print ("%s\n", json_str);
			g_free (json_str);
		}
		json_decref (root);
	} else {
		g_print ("%sStatus:%s\n",
			 pkgc_get_ansi_color_from_name (ctx, "bold"),
			 pkgc_get_ansi_color_from_name (ctx, "reset"));

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

	return PKGCTL_EXIT_SUCCESS;
}

/**
 * pkgc_history:
 *
 * Print transaction history.
 */
static int
pkgc_history (PkgctlContext *ctx, int argc, char **argv)
{
	guint limit = 10;

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
static int
pkgc_query_search (PkgctlContext *ctx, int argc, char **argv)
{
	const char *search_mode = "details";
	const char **search_terms;
	guint search_count;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = g_option_context_new ("[MODE] PATTERN...");
	g_option_context_set_help_enabled (option_context, TRUE);
	g_option_context_set_description(option_context,
		_("Search for packages matching the given patterns. If MODE is not specified, \n"
				   "'details' search is performed.\n"
				   "Possible search MODEs are:\n"
				   "  name    - search by package name\n"
				   "  details - search by package details (default)\n"
				   "  file    - search by file name\n"
				   "  group   - search by package group"));

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s [TYPE] PATTERN..."), "pkgctl search");
		pkgc_print_info (ctx, _("Types: name, details, file, group"));
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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
		return PKGCTL_EXIT_SYNTAX_ERROR;
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
static int
pkgc_query_list (PkgctlContext *ctx, int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = g_option_context_new ("[PATTERN]");
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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
static int
pkgc_query_show (PkgctlContext *ctx, int argc, char **argv)
{
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s PACKAGE..."), "pkgctl info");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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

			return PKGCTL_EXIT_FAILURE;
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
static int
pkgc_query_depends_on (PkgctlContext *ctx, int argc, char **argv)
{
	gboolean recursive = FALSE;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GOptionContext) option_context = NULL;
	g_autoptr(GError) error = NULL;

	const GOptionEntry options[] = {
		{ "recursive", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &recursive,
		  N_("Check dependencies recursively"), NULL },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};

	/* parse options */
	option_context = g_option_context_new ("PACKAGE...");
	g_option_context_set_help_enabled (option_context, TRUE);
	g_option_context_add_main_entries (option_context, options, NULL);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s PACKAGE..."), "pkgctl depends");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		pkgc_print_error (ctx, _("Could not resolve packages: %s"), error->message);
		return PKGCTL_EXIT_FAILURE;
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
static int
pkgc_query_what_provides (PkgctlContext *ctx, int argc, char **argv)
{
	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s CAPABILITY"), "pkgctl provides");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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
static int
pkgc_query_files (PkgctlContext *ctx, int argc, char **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_autoptr(GError) error = NULL;

	/* parse options */
	option_context = g_option_context_new ("PACKAGE...");
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s PACKAGE..."), "pkgctl files");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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
			return PKGCTL_EXIT_FAILURE;
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
	g_autoptr(GPtrArray) array = NULL;

	results = pk_task_generic_finish (PK_TASK (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error != NULL) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGCTL_EXIT_FAILURE;
		g_main_loop_quit (ctx->loop);
		return;
	}

	if (results != NULL) {
		/* process packages for list-updates */
		array = pk_results_get_package_array (results);
		for (guint i = 0; i < array->len; i++) {
			PkPackage *package = g_ptr_array_index (array, i);
			pkgc_print_package (ctx, package);
		}

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
static int
pkgc_updates_list_updates (PkgctlContext *ctx, int argc, char **argv)
{
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
static int
pkgc_updates_show_update (PkgctlContext *ctx, int argc, char **argv)
{
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s PACKAGE..."), "pkgctl get-update-detail");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		pkgc_print_error (ctx, _("Could not resolve packages: %s"), error->message);
		return PKGCTL_EXIT_FAILURE;
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
static int
pkgc_query_resolve (PkgctlContext *ctx, int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;
	PkBitfield filters;

	/* parse options */
	option_context = g_option_context_new ("PACKAGE...");
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s PACKAGE..."), "pkgctl resolve");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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
static int
pkgc_query_required_by (PkgctlContext *ctx, int argc, char **argv)
{
	gboolean recursive = FALSE;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	const GOptionEntry options[] = {
		{ "recursive", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &recursive,
		  N_("Check dependencies recursively"), NULL },
		{ NULL, 0, 0, 0, NULL, NULL, NULL }
	};

	/* parse options */
	option_context = g_option_context_new ("PACKAGE...");
	g_option_context_set_help_enabled (option_context, TRUE);
	g_option_context_add_main_entries (option_context, options, NULL);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc < 2) {
		pkgc_print_error (ctx, _("Usage: %s PACKAGE..."), "pkgctl required-by");
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		pkgc_print_error (ctx, _("Could not resolve packages: %s"), error->message);
		return PKGCTL_EXIT_FAILURE;
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
static int
pkgc_query_organization (PkgctlContext *ctx, int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;
	g_autofree gchar *text = NULL;
	PkBitfield groups;

	/* Parse options */
	option_context = g_option_context_new (NULL);
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	/* print available filters */
	g_print ("%s%s%s\n",
			 pkgc_get_ansi_color_from_name (ctx, "bold"),
			 /* TRANSLATORS: Header for list of available package filters */
			 _("Filters:"),
			 pkgc_get_ansi_color_from_name (ctx, "reset"));
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
			 pkgc_get_ansi_color_from_name (ctx, "bold"),
			 /* TRANSLATORS: Header for list of available package groups */
			 _("Groups:"),
			 pkgc_get_ansi_color_from_name (ctx, "reset"));
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
			 pkgc_get_ansi_color_from_name (ctx, "bold"),
			 /* TRANSLATORS: Header for list of available package categories */
			 _("Categories:"),
			 pkgc_get_ansi_color_from_name (ctx, "reset"));
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
 * pkgc_query_get_distro_upgrades:
 */
static int
pkgc_query_get_distro_upgrades (PkgctlContext *ctx, int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* Parse options */
	option_context = g_option_context_new (NULL);
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

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
		gchar *json_str;

		root = json_object ();
		json_object_set_new (root, "time_sec", json_integer (time_s));

		json_str = json_dumps (root, JSON_COMPACT);
		if (json_str) {
			g_print ("%s\n", json_str);
			g_free (json_str);
		}
		json_decref (root);
	} else {
		/* TRANSLATORS: this is the time since this role was used */
		g_print ("%s: %is\n", _("Elapsed time"), time_s);
	}

out:
		g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_get_time:
 *
 * Get time since last action.
 */
static int
pkgc_get_time (PkgctlContext *ctx, int argc, char **argv)
{
	const gchar *value = NULL;
	PkRoleEnum role = PK_ROLE_ENUM_UNKNOWN;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* Parse options */
	option_context = g_option_context_new ("[ROLE]");
	g_option_context_set_help_enabled (option_context, TRUE);

	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		pkgc_print_error (ctx, _("Failed to parse options: %s"), error->message);
		return PKGCTL_EXIT_SYNTAX_ERROR;
	}

	if (argc >= 2)
		value = argv[1];

	if (value == NULL) {
		pkgc_print_error (ctx,
				/* TRANSLATORS: The user didn't specify what action to use */
				 "%s", _("An action, e.g. 'update-packages' is required"));
		return PKGCTL_EXIT_FAILURE;
	}
	role = pk_role_enum_from_string (value);
	if (role == PK_ROLE_ENUM_UNKNOWN) {
		pkgc_print_error (ctx,
				/* TRANSLATORS: The user specified an invalid action */
			 "%s", _("A correct role is required"));
		return PKGCTL_EXIT_FAILURE;
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
	pkgc_context_register_command (ctx,
						"backend",
						pkgc_backend_info,
						N_ ("Show backend information"),
						N_ ("Usage: pkgctl backend\n"
						"Show PackageKit backend information and status."));

	pkgc_context_register_command (ctx,
						"history",
						pkgc_history,
						N_ ("Show transaction history"),
						N_ ("Usage: pkgctl history [LIMIT]\n"
						"Show recent package management transactions."));

	pkgc_context_register_command (ctx,
						"search",
						pkgc_query_search,
						N_ ("Search for packages"),
						N_ ("Usage: pkgctl search [TYPE] PATTERN...\n"
						"Search for packages matching the given pattern.\n\n"
						"Types: name, details, file, group (default: name)"));

	pkgc_context_register_command (ctx,
						"list",
						pkgc_query_list,
						N_ ("List packages"),
						N_ ("Usage: pkgctl list [PATTERN]\n"
						"List all packages or those matching a pattern."));

	pkgc_context_register_command (ctx,
						"show",
						pkgc_query_show,
						N_ ("Show package information"),
						N_ ("Usage: pkgctl show PACKAGE...\n"
						"Show information about one or more packages."));

	pkgc_context_register_command (ctx,
						"depends-on",
						pkgc_query_depends_on,
						N_ ("Show package dependencies"),
						N_ ("Usage: pkgctl depends-on PACKAGE...\n"
						"Show dependencies for one or more packages."));

	pkgc_context_register_command (
						ctx,
						"what-provides",
						pkgc_query_what_provides,
						N_ ("Show packages providing a capability"),
						N_ ("Usage: pkgctl provides CAPABILITY\n"
						"Show which packages provide the specified capability."));

	pkgc_context_register_command (ctx,
						"files",
						pkgc_query_files,
						N_ ("Show files in package"),
						N_ ("Usage: pkgctl files PACKAGE...\n"
						"List all files contained in one or more packages."));

	pkgc_context_register_command (ctx,
						"list-updates",
						pkgc_updates_list_updates,
						N_ ("Get available updates"),
						N_ ("Usage: pkgctl list-updates\n"
						"List all available package updates."));

	pkgc_context_register_command (
						ctx,
						"show-update",
						pkgc_updates_show_update,
						N_ ("Get update details"),
						N_ ("Usage: pkgctl show-update PACKAGE...\n"
						"Show detailed information about available updates for packages."));

	pkgc_context_register_command (ctx,
						"resolve",
						pkgc_query_resolve,
						N_ ("Resolve package names"),
						N_ ("Usage: pkgctl resolve PACKAGE...\n"
						"Resolve package names to package IDs."));

	pkgc_context_register_command (ctx,
						"required-by",
						pkgc_query_required_by,
						N_ ("Show packages requiring this package"),
						N_ ("Usage: pkgctl required-by PACKAGE...\n"
						"Show which packages require the specified packages."));

	pkgc_context_register_command (ctx,
						"organization",
						pkgc_query_organization,
						N_ ("List available filters and categories"),
						N_ ("Usage: pkgctl organization\n"
						"List all available filters, groups and categories for package organization."));

	pkgc_context_register_command (ctx,
						"get-distro-upgrades",
						pkgc_query_get_distro_upgrades,
						N_ ("Get available distribution upgrades"),
						N_ ("Usage: pkgctl get-distro-upgrades\n"
						"Show available distribution version upgrades."));

	pkgc_context_register_command (ctx,
					   "get-time",
					   pkgc_get_time,
					   N_ ("Get time since last action"),
					   N_ ("Usage: pkgctl get-time [ROLE]\n"
					   "Get time in seconds since the last action."));
}
