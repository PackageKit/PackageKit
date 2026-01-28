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
#include "pkgc-manage.h"

#include <glib.h>

#include "pkgc-util.h"

/* Options for various management operations */
static gboolean opt_download_only = FALSE;
static gboolean opt_allow_downgrade = FALSE;
static gboolean opt_allow_reinstall = FALSE;
static gboolean opt_allow_untrusted = FALSE;
static gboolean opt_no_autoremove = FALSE;
static gint opt_cache_age = -1;

static const GOptionEntry option_download_only[] = {
	{ "download-only", 'd', 0, G_OPTION_ARG_NONE, &opt_download_only,
		/* TRANSLATORS: command line argument, do we just download or apply changes */
	  N_("Prepare the transaction by downloading packages only"), NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

static const GOptionEntry option_allow_downgrade[] = {
	{ "allow-downgrade", 0, 0, G_OPTION_ARG_NONE, &opt_allow_downgrade,
		/* TRANSLATORS: command line argument, do we allow package downgrades */
	  N_("Allow package downgrades"), NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

static const GOptionEntry option_allow_reinstall[] = {
	{ "allow-reinstall", 0, 0, G_OPTION_ARG_NONE, &opt_allow_reinstall,
		/* TRANSLATORS: command line argument, do we allow package re-installations */
	  N_("Allow package re-installations"), NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

static const GOptionEntry option_allow_untrusted[] = {
	{ "allow-untrusted", 0, 0, G_OPTION_ARG_NONE, &opt_allow_untrusted,
		/* TRANSLATORS: command line argument */
	  N_("Allow installation of untrusted packages"), NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

static const GOptionEntry option_no_autoremove[] = {
	{ "no-autoremove", 0, 0, G_OPTION_ARG_NONE, &opt_no_autoremove,
		/* TRANSLATORS: command line argument */
		N_("Do not automatically remove unused dependencies"), NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

static const GOptionEntry option_cache_age[] = {
	{ "cache-age", 'c', 0, G_OPTION_ARG_INT, &opt_cache_age,
		/* TRANSLATORS: command line argument */
	  N_("Maximum metadata cache age in seconds (default: 3 days)"), N_("SECONDS") },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

/**
 * pkgc_manage_reset_options:
 *
 * Reset all option flags to their default values.
 */
static void
pkgc_manage_reset_options (void)
{
	opt_download_only = FALSE;
	opt_allow_downgrade = FALSE;
	opt_allow_reinstall = FALSE;
	opt_allow_untrusted = FALSE;
	opt_no_autoremove = FALSE;
	opt_cache_age = PKGC_DEFAULT_CACHE_AGE_SEC;
}

/**
 * pkgc_manage_apply_options:
 * @ctx: a valid #PkgctlContext
 *
 * Apply the parsed option flags to the context.
 */
static void
pkgc_manage_apply_options (PkgcliContext *ctx)
{
	ctx->only_download = opt_download_only;
	ctx->allow_downgrade = opt_allow_downgrade;
	ctx->allow_reinstall = opt_allow_reinstall;
	ctx->allow_untrusted = opt_allow_untrusted;
	if (opt_cache_age == 0)
		ctx->cache_age = 1; /* shortest possible cache-age, 0 is not allowed */
	else
		ctx->cache_age = (opt_cache_age < 0)? G_MAXUINT : (guint)opt_cache_age;

	pkgc_context_apply_settings (ctx);
}

/**
 * pkgc_manage_on_task_finished_cb:
 */
static void
pkgc_manage_on_task_finished_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	PkgcliContext *ctx = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) pk_error = NULL;

	ctx->exit_code = PKGC_EXIT_SUCCESS;
	results = pk_task_generic_finish (PK_TASK (source_object), res, &error);

	if (ctx->progressbar != NULL && ctx->is_tty)
		pk_progress_bar_end (ctx->progressbar);

	if (error) {
		pkgc_print_error (ctx, "%s", error->message);
		ctx->exit_code = PKGC_EXIT_TRANSACTION_FAILED;

		goto out;
	}

	if (results == NULL)
		return;

	/* check exit code from results */
	pk_error = pk_results_get_error_code (results);
	if (pk_error) {
		pkgc_print_error (ctx, "%s", pk_error_get_details (pk_error));
		ctx->exit_code = PKGC_EXIT_TRANSACTION_FAILED;
	} else {
		g_autoptr(GPtrArray) pkgs = NULL;
		g_autoptr(GPtrArray) tas = NULL;
		g_autoptr(GPtrArray) repos = NULL;

		/* show packages that were processed */
		pkgs = pk_results_get_package_array (results);
		for (guint i = 0; i < pkgs->len; i++) {
			PkPackage *package = PK_PACKAGE (g_ptr_array_index (pkgs, i));
			pkgc_print_package (ctx, package);
		}

		/* show transactions */
		tas = pk_results_get_transaction_array (results);
		for (guint i = 0; i < tas->len; i++) {
			PkTransactionPast *transaction = PK_TRANSACTION_PAST (
			    g_ptr_array_index (tas, i));
			pkgc_print_transaction (ctx, transaction);
		}

		/* repo_detail */
		repos = pk_results_get_repo_detail_array (results);
		for (guint i = 0; i < repos->len; i++) {
			PkRepoDetail *repo = PK_REPO_DETAIL (g_ptr_array_index (repos, i));
			pkgc_print_repo (ctx, repo);
		}
	}

out:
	g_main_loop_quit (ctx->loop);
}

/**
 * pkgc_refresh:
 *
 * Refresh package metadata cache.
 */
static gint
pkgc_refresh (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	gboolean force = FALSE;
	g_autoptr(GOptionContext) option_context = NULL;

	pkgc_manage_reset_options ();

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[force]",
		/* TRANSLATORS: Description for pkgcli refresh */
		_("Refresh the package metadata cache."));
	g_option_context_add_main_entries (option_context, option_cache_age, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Check for force flag */
	if (argc >= 2 && strcmp (argv[1], "force") == 0) {
		force = TRUE;
	}

	/* Apply options to context */
	pkgc_manage_apply_options (ctx);

	/* Refresh cache */
	pk_task_refresh_cache_async (PK_TASK (ctx->task),
				     force,
				     ctx->cancellable,
				     pkgc_context_on_progress_cb,
				     ctx,
				     pkgc_manage_on_task_finished_cb,
				     ctx);

	g_main_loop_run (ctx->loop);

	if (ctx->exit_code == PKGC_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("Package metadata refreshed"));

	return ctx->exit_code;
}

/**
 * pkgc_install:
 *
 * Install packages or local package files.
 */
static gint
pkgc_install (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	gboolean has_files;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	pkgc_manage_reset_options ();

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgcli install */
		_("Install one or more packages or local package files."));

	g_option_context_add_main_entries (option_context, option_download_only, NULL);
	g_option_context_add_main_entries (option_context, option_allow_downgrade, NULL);
	g_option_context_add_main_entries (option_context, option_allow_reinstall, NULL);
	g_option_context_add_main_entries (option_context, option_allow_untrusted, NULL);
	g_option_context_add_main_entries (option_context, option_no_autoremove, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* apply options to context */
	pkgc_manage_apply_options (ctx);

	/* check if we have files or package names */
	has_files = FALSE;
	for (gint i = 1; i < argc; i++) {
		if (pkgc_is_local_package (argv[i])) {
			has_files = TRUE;
			break;
		}
	}

	if (has_files) {
		/* install local files */
		pk_task_install_files_async (PK_TASK (ctx->task),
					     argv + 1,
					     ctx->cancellable,
					     pkgc_context_on_progress_cb,
					     ctx,
					     pkgc_manage_on_task_finished_cb,
					     ctx);
	} else {
		/* install packages by name */
		g_auto(GStrv) package_ids = NULL;

		/* assume arch filter unless specified otherwise */
		if (!pk_bitfield_contain (ctx->filters, PK_FILTER_ENUM_ARCH) &&
			!pk_bitfield_contain (ctx->filters, PK_FILTER_ENUM_NOT_ARCH))
			pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_ARCH);

		/* assume non-source packages unless specified */
		if (!pk_bitfield_contain (ctx->filters, PK_FILTER_ENUM_SOURCE) &&
			!pk_bitfield_contain (ctx->filters, PK_FILTER_ENUM_NOT_SOURCE))
			pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_SOURCE);

		pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);
		if (!opt_allow_reinstall)
			pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);

		/* resolve package names to IDs */
		package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
		if (package_ids == NULL) {
			/* the the error was not "no package found", or we did allow reinstallations
			 * (and therefore didn't filter out already installed packages) we show the
			 * emitted error immediately. */
			if (g_error_matches (error, PKGC_ERROR, PK_ERROR_ENUM_PACKAGE_NOT_FOUND) ||
				 opt_allow_reinstall) {
				pkgc_print_error (ctx,
							/* TRANSLATORS: There was an error finding a package
							 * for installation. The detailed error follows. */
							_("Could not find any available package: %s"), error->message);
				return PKGC_EXIT_NOT_FOUND;
			}

			/* If we are here, the package may exist, but may already be installed.
			 * Check for that to provide a better error */
			pk_bitfield_remove (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
			pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_INSTALLED);
			package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, NULL);
			if (package_ids == NULL) {
				/* the package does not exist at all */
				pkgc_print_error (ctx,
							/* TRANSLATORS: We were unable to find a package for installation. */
							_("Could not find any available package: %s"), error->message);
				return PKGC_EXIT_NOT_FOUND;
			} else {
				/* the package exists, but is already installed */
				pkgc_print_info (ctx,
							_("The selected package is already installed."));
				return PKGC_EXIT_SUCCESS;
			}

			return PKGC_EXIT_FAILURE;
		}

		/* install packages */
		pk_task_install_packages_async (PK_TASK (ctx->task),
						package_ids,
						ctx->cancellable,
						pkgc_context_on_progress_cb,
						ctx,
						pkgc_manage_on_task_finished_cb,
						ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_remove:
 *
 * Remove packages from the system.
 */
static gint
pkgc_remove (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;

	pkgc_manage_reset_options ();

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"PACKAGE...",
		/* TRANSLATORS: Description for pkgcli remove */
		_("Remove one or more packages from the system."));
	g_option_context_add_main_entries (option_context, option_no_autoremove, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 2))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* apply options to context */
	pkgc_manage_apply_options (ctx);

	/* only look at installed packages */
	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_INSTALLED);

	/* resolve package names to IDs */
	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
	if (package_ids == NULL) {
		if (error) {
			pkgc_print_error (ctx,
					  _("Could not find installed packages: %s"), error->message);
		}
		return PKGC_EXIT_FAILURE;
	}

	/* remove packages */
	pk_task_remove_packages_async (PK_TASK (ctx->task),
				       package_ids,
				       TRUE, /* allow deps */
				       !opt_no_autoremove, /* autoremove */
				       ctx->cancellable,
				       pkgc_context_on_progress_cb,
				       ctx,
				       pkgc_manage_on_task_finished_cb,
				       ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_download:
 *
 * Download packages to the specified directory without installing.
 */
static gint
pkgc_download (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	const char *directory = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GError) error = NULL;

	pkgc_manage_reset_options ();

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"DIRECTORY PACKAGE...",
		/* TRANSLATORS: Description for pkgcli download */
		_("Download packages to the specified directory without installing."));
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 3))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Apply options to context */
	pkgc_manage_apply_options (ctx);

	directory = argv[1];
	package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 2, &error);
	if (package_ids == NULL) {
		if (error) {
			/* TRANSLATORS: There was an error getting the
			 * details about the package. The detailed error follows */
			pkgc_print_error (ctx,
					  _("Could not find packages: %s"), error->message);
		}

		return PKGC_EXIT_FAILURE;
	}

	/* Check if directory exists */
	if (!g_file_test (directory, G_FILE_TEST_IS_DIR)) {
		pkgc_print_error (ctx, _("Directory does not exist: %s"), directory);
		return PKGC_EXIT_SYNTAX_ERROR;
	}

	/* Download packages */
	pk_task_download_packages_async (PK_TASK (ctx->task),
					 package_ids,
					 directory,
					 ctx->cancellable,
					 pkgc_context_on_progress_cb,
					 ctx,
					 pkgc_manage_on_task_finished_cb,
					 ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

static gboolean
pkgc_update_system_filter_helper (PkPackage *package, gpointer user_data)
{
	PkInfoEnum package_enum = pk_package_get_info (package);
	return package_enum != PK_INFO_ENUM_BLOCKED;
}

/**
 * pkgc_update:
 *
 * Update all packages or specific packages to their latest versions.
 */
static gint
pkgc_update (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_autoptr(GError) error = NULL;

	pkgc_manage_reset_options ();

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[PACKAGE...]",
		/* TRANSLATORS: Description for pkgcli update */
		_("Update all packages or specific packages to their latest versions."));
	g_option_context_add_main_entries (option_context, option_download_only, NULL);
	g_option_context_add_main_entries (option_context, option_allow_downgrade, NULL);

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* apply options to context */
	pkgc_manage_apply_options (ctx);

	if (argc >= 2) {
		/* update specific packages */
		g_auto(GStrv) package_ids = NULL;

		pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
		pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_SOURCE);
		pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);

		package_ids = pkgc_resolve_packages (ctx, ctx->filters, argv + 1, &error);
		if (package_ids == NULL) {
			if (error)
				pkgc_print_error (ctx,
						  _("Could not find packages to update: %s"), error->message);
			return PKGC_EXIT_FAILURE;
		}

		pk_task_update_packages_async (PK_TASK (ctx->task),
					       package_ids,
					       ctx->cancellable,
					       pkgc_context_on_progress_cb,
					       ctx,
					       pkgc_manage_on_task_finished_cb,
					       ctx);
	} else {
		/* update all packages - get updates first */
		g_autoptr(PkResults) results = NULL;
		g_autoptr(PkPackageSack) sack = NULL;
		g_auto(GStrv) package_ids = NULL;

		/* clear any previous error */
		g_clear_error (&error);

		/* get current updates */
		pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);
		results = pk_task_get_updates_sync (PK_TASK (ctx->task),
						    ctx->filters,
						    ctx->cancellable,
						    pkgc_context_on_progress_cb,
						    ctx,
						    &error);
		if (results == NULL) {
			pkgc_print_error (ctx, _("Failed to get updates: %s"), error->message);
			ctx->exit_code = PKGC_EXIT_FAILURE;
			return ctx->exit_code;
		}

		/* drop blocked packages from the update set */
		sack = pk_results_get_package_sack (results);
		pk_package_sack_remove_by_filter (sack, &pkgc_update_system_filter_helper, NULL);

		package_ids = pk_package_sack_get_ids (sack);

		if (package_ids == NULL || g_strv_length (package_ids) == 0) {
			pkgc_print_info (ctx, _("No packages require updating"));
			return PKGC_EXIT_SUCCESS;
		}

		/* now update them */
		pk_task_update_packages_async (PK_TASK (ctx->task),
					       package_ids,
					       ctx->cancellable,
					       pkgc_context_on_progress_cb,
					       ctx,
					       pkgc_manage_on_task_finished_cb,
					       ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_upgrade:
 *
 * Upgrade the system to a new distribution version or do an advanced update.
 */
static gint
pkgc_upgrade (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	const char *distro_name = NULL;
	PkUpgradeKindEnum upgrade_kind = PK_UPGRADE_KIND_ENUM_DEFAULT;
	g_autoptr(GError) error = NULL;

	pkgc_manage_reset_options ();

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[DISTRO] [TYPE]",
		/* TRANSLATORS: Description of pkgcli upgrade.
		 * No not translate "minimal, default, complete", those are parameters */
		_("Upgrade all packages or perform a distribution upgrade.\n\n"
		  "Types: minimal, default, complete"));

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Apply options to context */
	pkgc_manage_apply_options (ctx);

	/* Parse optional distro name and upgrade type */
	if (argc >= 2)
		distro_name = argv[1];


	if (argc >= 3) {
		if (g_strcmp0 (argv[2], "minimal") == 0)
			upgrade_kind = PK_UPGRADE_KIND_ENUM_MINIMAL;
		else if (g_strcmp0 (argv[2], "complete") == 0)
			upgrade_kind = PK_UPGRADE_KIND_ENUM_COMPLETE;
		else if (g_strcmp0 (argv[2], "default") == 0)
			upgrade_kind = PK_UPGRADE_KIND_ENUM_DEFAULT;
	}

	/* Upgrade system */
	if (distro_name) {
		pk_task_upgrade_system_async (PK_TASK (ctx->task),
					      distro_name,
					      upgrade_kind,
					      ctx->cancellable,
					      pkgc_context_on_progress_cb,
					      ctx,
					      pkgc_manage_on_task_finished_cb,
					      ctx);
	} else {
		/* Just update all packages */
		g_autoptr(PkResults) results = NULL;
		g_autoptr(PkPackageSack) sack = NULL;
		g_auto(GStrv) package_ids = NULL;

		/* Clear any previous error */
		g_clear_error (&error);

		/* Get current updates */
		results = pk_task_get_updates_sync (PK_TASK (ctx->task),
						    ctx->filters,
						    ctx->cancellable,
						    pkgc_context_on_progress_cb,
						    ctx,
						    &error);
		if (results == NULL) {
			pkgc_print_error (ctx, _("Failed to get updates: %s"), error->message);
			ctx->exit_code = PKGC_EXIT_FAILURE;
			return ctx->exit_code;
		}

		sack = pk_results_get_package_sack (results);
		package_ids = pk_package_sack_get_ids (sack);

		if (package_ids == NULL || g_strv_length (package_ids) == 0) {
			pkgc_print_info (ctx, _("No packages require updating"));
			return PKGC_EXIT_SUCCESS;
		}

		/* Now update them */
		pk_task_update_packages_async (PK_TASK (ctx->task),
					       package_ids,
					       ctx->cancellable,
					       pkgc_context_on_progress_cb,
					       ctx,
					       pkgc_manage_on_task_finished_cb,
					       ctx);
	}

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_print_offline_update_status:
 *
 * Helper for pkgc_offline_update().
 */
static gint
pkgc_print_offline_update_status (PkgcliContext *ctx)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(PkError) pk_error = NULL;
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GPtrArray) packages = NULL;
	PkOfflineAction action;

	/* Check if offline update is armed */
	action = pk_offline_get_action (&error);
	if (action == PK_OFFLINE_ACTION_UNKNOWN) {
		pkgc_print_error (ctx, _("Failed to read offline update action: %s"), error->message);
		return PKGC_EXIT_FAILURE;
	}

	if (action == PK_OFFLINE_ACTION_UNSET) {
		if (ctx->output_mode != PKGCLI_MODE_JSON)
			g_print ("%s%s%s",
				 pkgc_get_ansi_color (ctx, PKGC_COLOR_BLUE),
				 "⏾ ",
				 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
		pkgc_print_info (ctx, _("Offline update is not triggered."));
		g_print ("\n");
	} else {
		if (ctx->output_mode != PKGCLI_MODE_JSON)
			g_print ("%s%s%s",
				 pkgc_get_ansi_color (ctx, PKGC_COLOR_YELLOW),
				 "⚠ ",
				 pkgc_get_ansi_color (ctx, PKGC_COLOR_RESET));
		pkgc_print_info (ctx, _("Offline update is triggered. Action after update: %s"),
			pk_offline_action_to_string(action));
		g_print ("\n");
	}

	g_clear_error (&error);

	/* Check if there are prepared updates */
	package_ids = pk_offline_get_prepared_ids (&error);
	if (package_ids != NULL) {
		/* TRANSLATORS: Packages that were prepared for an offline update */
		pkgc_print_info (ctx, _("Prepared packages:"));
		for (guint i = 0; package_ids[i] != NULL; i++) {
			g_autofree gchar *printable = pk_package_id_to_printable (package_ids[i]);
			if (ctx->output_mode == PKGCLI_MODE_JSON) {
				json_t *root = json_object ();
				json_object_set_new (root, "pkid", json_string (package_ids[i]));
				pkgc_print_json_decref (root);
			} else {
				g_print ("  %s\n", printable);
			}
		}
		g_print ("\n");
	} else if (error != NULL) {
		if (error->code == PK_OFFLINE_ERROR_NO_DATA) {
			pkgc_print_info (ctx, _("No offline update is prepared."));
		} else {
			pkgc_print_error (ctx, _("Failed to read prepared offline updates: %s"), error->message);
			return PKGC_EXIT_FAILURE;
		}
	}

	/* Check if there are results from last update */
	g_clear_error (&error);
	results = pk_offline_get_results (&error);

	if (!results) {
		pkgc_print_info (ctx, _("No results from last offline update available."));
		return PKGC_EXIT_SUCCESS;
	}

	pk_error = pk_results_get_error_code (results);
	if (pk_error) {
		pkgc_print_error (ctx,
				  _("Last offline update failed: %s: %s"),
					pk_error_enum_to_string (pk_error_get_code (pk_error)),
					pk_error_get_details (pk_error));
		return PKGC_EXIT_TRANSACTION_FAILED;
	}

	packages = pk_results_get_package_array (results);
	pkgc_print_success (ctx, _("Last offline update completed successfully"));
	for (guint i = 0; i < packages->len; i++) {
		PkPackage *pkg = PK_PACKAGE (g_ptr_array_index (packages, i));
		g_autofree gchar *printable = pk_package_id_to_printable (
			pk_package_get_id (pkg));
		if (ctx->output_mode == PKGCLI_MODE_JSON) {
			json_t *root = json_object ();
			json_object_set_new (root, "pkid", json_string (pk_package_get_id (pkg)));
			pkgc_print_json_decref (root);
		} else {
			g_print ("  ");
			g_print (_("Updated: %s"), printable);
			g_print ("\n");
		}
	}

	return PKGC_EXIT_SUCCESS;
}

/**
 * pkgc_trigger_offline_update:
 *
 * Helper for pkgc_offline_update().
 */
static gint
pkgc_trigger_offline_update (PkgcliContext *ctx)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = pk_offline_trigger_with_flags (PK_OFFLINE_ACTION_REBOOT, PK_OFFLINE_FLAGS_INTERACTIVE, NULL, &error);
	if (!ret) {
		pkgc_print_error (ctx, _("Failed to trigger offline update: %s"), error->message);
		return PKGC_EXIT_FAILURE;
	}

	pkgc_print_success (ctx, _("Offline update scheduled. System will update on next reboot."));
	return PKGC_EXIT_SUCCESS;
}

/**
 * pkgc_offline_update:
 *
 * Manage offline updates (for update installation on next (soft)reboot).
 */
static gint
pkgc_offline_update (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	const gchar *cmd_description = NULL;
	const gchar *request = NULL;
	gboolean ret;
	g_autoptr(GError) error = NULL;

	/* TRANSLATORS: Description of the pkgcli offline-update command.
	 * The request values (trigger, prepare, etc.) are parameters and MUST NOT be translated. */
	cmd_description = _("Trigger & manage offline system updates.\n\n"
						"You can select one of these requests:\n"
						"  prepare - prepare an offline update and trigger it (default)\n"
						"  trigger - trigger a (manually prepared) offline update\n"
						"  cancel  - cancel a planned offline update\n"
						"  status  - show status information about a prepared or finished offline update");

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"[REQUEST]",
		cmd_description);
	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	request = (argc >= 2)? argv[1] : "prepare";

	if (g_strcmp0 (request, "trigger") == 0) {
		return pkgc_trigger_offline_update (ctx);
	}

	if (g_strcmp0 (request, "cancel") == 0) {
		ret = pk_offline_cancel_with_flags (PK_OFFLINE_FLAGS_INTERACTIVE, NULL, &error);
		if (!ret) {
			pkgc_print_error (ctx, _("Failed to cancel offline update: %s"), error->message);
			return PKGC_EXIT_FAILURE;
		}

		pkgc_print_success (ctx, _("Offline update cancelled"));
		return PKGC_EXIT_SUCCESS;
	}

	if (g_strcmp0 (request, "status") == 0) {
		return pkgc_print_offline_update_status (ctx);
	}

	if (g_strcmp0 (request, "prepare") == 0) {
		/* Update all packages - get updates first */
		g_autoptr(PkResults) results = NULL;
		g_autoptr(PkPackageSack) sack = NULL;
		g_auto(GStrv) package_ids = NULL;

		/* set parameters to prepare offline updates */
		ctx->only_download = TRUE;
		ctx->allow_downgrade = FALSE;
		ctx->allow_untrusted = FALSE;
		pkgc_context_apply_settings (ctx);

		/* download all updates */
		results = pk_task_get_updates_sync (PK_TASK (ctx->task),
							ctx->filters,
							ctx->cancellable,
							pkgc_context_on_progress_cb,
							ctx,
							&error);
		if (results == NULL) {
			pkgc_print_error (ctx, _("Failed to get updates: %s"), error->message);
			ctx->exit_code = PKGC_EXIT_FAILURE;
			return ctx->exit_code;
		}

		sack = pk_results_get_package_sack (results);
		package_ids = pk_package_sack_get_ids (sack);
		if (package_ids == NULL || g_strv_length (package_ids) == 0) {
			pkgc_print_info (ctx, _("No packages require updating"));
			return PKGC_EXIT_SUCCESS;
		}

		/* download packages */
		pk_task_update_packages_async (PK_TASK (ctx->task),
						   package_ids,
						   ctx->cancellable,
						   pkgc_context_on_progress_cb,
						   ctx,
						   pkgc_manage_on_task_finished_cb,
						   ctx);
		g_main_loop_run (ctx->loop);

		/* don't trigger offline update if download failed */
		if (ctx->exit_code != PKGC_EXIT_SUCCESS)
			return ctx->exit_code;

		return pkgc_trigger_offline_update (ctx);
	}

	pkgc_print_error (ctx, _("Unknown offline-update request: %s"), request);
	return PKGC_EXIT_SYNTAX_ERROR;
}

/**
 * pkgc_install_sig:
 *
 * Install a package signature (for GPG verification).
 */
static gint
pkgc_install_sig (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		"TYPE KEY_ID PACKAGE_ID",
		/* TRANSLATORS: Description for pkgcli install-sig */
		_("Install a package signature for GPG verification."));

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 4))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* Install signature */
	pk_client_install_signature_async (PK_CLIENT (ctx->task),
					   PK_SIGTYPE_ENUM_GPG,
					   argv[2], /* key_id */
					   argv[3], /* package_id */
					   ctx->cancellable,
					   pkgc_context_on_progress_cb,
					   ctx,
					   pkgc_manage_on_task_finished_cb,
					   ctx);

	g_main_loop_run (ctx->loop);
	return ctx->exit_code;
}

/**
 * pkgc_repair:
 *
 * Repair broken package management.
 */
static gint
pkgc_repair (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgcli repair */
		_("Attempt to repair the package management system."));

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	/* repair system */
	pk_task_repair_system_async (PK_TASK (ctx->task),
				     ctx->cancellable,
				     pkgc_context_on_progress_cb,
				     ctx,
				     pkgc_manage_on_task_finished_cb,
				     ctx);

	g_main_loop_run (ctx->loop);

	if (ctx->exit_code == PKGC_EXIT_SUCCESS)
		pkgc_print_success (ctx, _("System repaired successfully"));

	return ctx->exit_code;
}

/**
 * pkgc_suggest_quit:
 *
 * Suggest to safely stop the PackageKit daemon.
 */
static gint
pkgc_suggest_quit (PkgcliContext *ctx, PkgcliCommand *cmd, gint argc, gchar **argv)
{
	g_autoptr(GOptionContext) option_context = NULL;
	g_autoptr(GError) error = NULL;

	/* parse options */
	option_context = pkgc_option_context_for_command (
		ctx, cmd,
		NULL,
		/* TRANSLATORS: Description for pkgcli quit */
		_("Safely terminate the PackageKit daemon."));

	if (!pkgc_parse_command_options (ctx, cmd, option_context, &argc, &argv, 1))
		return PKGC_EXIT_SYNTAX_ERROR;

	if (!pk_control_suggest_daemon_quit (ctx->control, ctx->cancellable, &error)) {
		pkgc_print_error (ctx, _("Failed to send daemon quit request: %s"), error->message);
		return PKGC_EXIT_FAILURE;
	}

	return PKGC_EXIT_SUCCESS;
}

/**
 * pkgc_register_manage_commands:
 *
 * Register package management commands
 */
void
pkgc_register_manage_commands (PkgcliContext *ctx)
{
	pkgc_context_register_command (
		ctx,
       "refresh",
       pkgc_refresh,
       _("Refresh package metadata"));

	pkgc_context_register_command (
		ctx,
		"install",
		pkgc_install,
		_("Install packages"));

	pkgc_context_register_command (
		ctx,
		"remove",
		pkgc_remove,
		_("Remove packages"));

	pkgc_context_register_command (
	    ctx,
	    "update",
	    pkgc_update,
	    _("Update packages"));

	pkgc_context_register_command (
	    ctx,
	    "upgrade",
	    pkgc_upgrade,
	    _("Upgrade the system"));

	pkgc_context_register_command (
	    ctx,
	    "download",
	    pkgc_download,
	    _("Download packages"));

	pkgc_context_register_command (
		ctx,
		"offline-update",
		pkgc_offline_update,
		_("Manage offline system updates"));

	pkgc_context_register_command (
		ctx,
		"install-sig",
		pkgc_install_sig,
		_("Install package signature"));

	pkgc_context_register_command (
		ctx,
		"repair",
		pkgc_repair,
		_("Repair package system"));

	pkgc_context_register_command (
		ctx,
		"quit",
		pkgc_suggest_quit,
		_("Safely stop the PackageKit daemon"));
}
