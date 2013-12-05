/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <glib-unix.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>
#include <sys/types.h>
#include <pwd.h>
#include <locale.h>

#define PK_EXIT_CODE_SYNTAX_INVALID	3
#define PK_EXIT_CODE_FILE_NOT_FOUND	4
#define PK_EXIT_CODE_NOTHING_USEFUL	5
#define PK_EXIT_CODE_CANNOT_SETUP	6
#define PK_EXIT_CODE_TRANSACTION_FAILED	7

#define PK_CONSOLE_ERROR	1

typedef struct {
	GCancellable	*cancellable;
	GMainLoop	*loop;
	PkBitfield	 roles;
	PkControl	*control;
	PkProgressBar	*progressbar;
	PkTaskText	*task;
	gboolean	 is_console;
	gint		 retval;
	PkBitfield	 filters;
	guint		 defered_status_id;
	PkStatusEnum	 defered_status;
} PkConsoleCtx;

/**
 * pk_strpad:
 * @data: the input string
 * @length: the desired length of the output string, with padding
 *
 * Returns the text padded to a length with spaces. If the string is
 * longer than length then a longer string is returned.
 *
 * Return value: The padded string
 **/
static gchar *
pk_strpad (const gchar *data, guint length)
{
	gint size;
	guint data_len;
	gchar *text;
	gchar *padding;

	if (data == NULL)
		return g_strnfill (length, ' ');

	/* ITS4: ignore, only used for formatting */
	data_len = strlen (data);

	/* calculate */
	size = (length - data_len);
	if (size <= 0)
		return g_strdup (data);

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

/**
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (PkPackage *package, PkConsoleCtx *ctx)
{
	gchar *printable = NULL;
	gchar *printable_pad = NULL;
	gchar *package_id = NULL;
	gchar *summary = NULL;
	gchar *info_pad = NULL;
	gchar **split = NULL;
	PkInfoEnum info;

	/* get data */
	g_object_get (package,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* ignore finished */
	if (info == PK_INFO_ENUM_FINISHED)
		goto out;

	/* split */
	split = pk_package_id_split (package_id);
	if (split == NULL)
		goto out;

	/* make these all the same length */
	info_pad = pk_strpad (pk_info_enum_to_localised_past (info), 12);

	/* create printable */
	printable = pk_package_id_to_printable (package_id);

	/* don't pretty print */
	if (!ctx->is_console) {
		g_print ("%s %s\n", info_pad, printable);
		goto out;
	}

	/* pad the name-version */
	printable_pad = pk_strpad (printable, 40);
	g_print ("%s\t%s\t%s\n", info_pad, printable_pad, summary);
out:
	/* free all the data */
	g_free (printable);
	g_free (printable_pad);
	g_free (info_pad);
	g_strfreev (split);
}

/**
 * pk_console_transaction_cb:
 **/
static void
pk_console_transaction_cb (PkTransactionPast *item, PkConsoleCtx *ctx)
{
	struct passwd *pw;
	const gchar *role_text;
	gchar **lines;
	gchar **parts;
	guint i, lines_len;
	gchar *package = NULL;
	gchar *tid;
	gchar *timespec;
	gboolean succeeded;
	guint duration;
	gchar *cmdline;
	guint uid;
	gchar *data;
	PkRoleEnum role;

	/* get data */
	g_object_get (item,
		      "role", &role,
		      "tid", &tid,
		      "timespec", &timespec,
		      "succeeded", &succeeded,
		      "duration", &duration,
		      "cmdline", &cmdline,
		      "uid", &uid,
		      "data", &data,
		      NULL);

	role_text = pk_role_enum_to_string (role);
	/* TRANSLATORS: this is an atomic transaction */
	g_print ("%s: %s\n", _("Transaction"), tid);
	/* TRANSLATORS: this is the time the transaction was started in system timezone */
	g_print (" %s: %s\n", _("System time"), timespec);
	/* TRANSLATORS: this is if the transaction succeeded or not */
	g_print (" %s: %s\n", _("Succeeded"), succeeded ? _("True") : _("False"));
	/* TRANSLATORS: this is the transactions role, e.g. "update-packages" */
	g_print (" %s: %s\n", _("Role"), role_text);

	/* only print if not null */
	if (duration > 0) {
		/* TRANSLATORS: this is The duration of the transaction */
		g_print (" %s: %i %s\n", _("Duration"), duration, _("(seconds)"));
	}

	/* TRANSLATORS: this is The command line used to do the action */
	g_print (" %s: %s\n", _("Command line"), cmdline);
	/* TRANSLATORS: this is the user ID of the user that started the action */
	g_print (" %s: %i\n", _("User ID"), uid);

	/* query real name */
	pw = getpwuid (uid);
	if (pw != NULL) {
		if (pw->pw_name != NULL) {
			/* TRANSLATORS: this is the username, e.g. hughsie */
			g_print (" %s: %s\n", _("Username"), pw->pw_name);
		}
		if (pw->pw_gecos != NULL) {
			/* TRANSLATORS: this is the users real name, e.g. "Richard Hughes" */
			g_print (" %s: %s\n", _("Real name"), pw->pw_gecos);
		}
	}

	lines = g_strsplit (data, "\n", -1);
	lines_len = g_strv_length (lines);
	if (lines_len > 0) {
		/* TRANSLATORS: these are packages touched by the transaction */
		g_print (" %s\n", _("Affected packages:"));
	} else {
		/* TRANSLATORS: these are packages touched by the transaction */
		g_print (" %s\n", _("Affected packages: None"));
	}
	for (i=0; i<lines_len; i++) {
		parts = g_strsplit (lines[i], "\t", 3);

		/* create printable */
		package = pk_package_id_to_printable (parts[1]);
		g_print (" - %s %s\n", parts[0], package);
		g_free (package);
		g_strfreev (parts);
	}
	g_free (tid);
	g_free (timespec);
	g_free (cmdline);
	g_free (data);
	g_strfreev (lines);
}

/**
 * pk_console_distro_upgrade_cb:
 **/
static void
pk_console_distro_upgrade_cb (PkDistroUpgrade *item, gpointer user_data)
{
	gchar *name;
	gchar *summary;
	PkDistroUpgradeEnum state;

	/* get data */
	g_object_get (item,
		      "name", &name,
		      "state", &state,
		      "summary", &summary,
		      NULL);

	/* TRANSLATORS: this is the distro, e.g. Fedora 10 */
	g_print ("%s: %s\n", _("Distribution"), name);
	/* TRANSLATORS: this is type of update, stable or testing */
	g_print (" %s: %s\n", _("Type"), pk_update_state_enum_to_string (state));
	/* TRANSLATORS: this is any summary text describing the upgrade */
	g_print (" %s: %s\n", _("Summary"), summary);

	g_free (name);
	g_free (summary);
}

/**
 * pk_console_category_cb:
 **/
static void
pk_console_category_cb (PkCategory *item, gpointer user_data)
{
	gchar *name;
	gchar *cat_id;
	gchar *parent_id;
	gchar *summary;
	gchar *icon;

	/* get data */
	g_object_get (item,
		      "name", &name,
		      "cat_id", &cat_id,
		      "parent_id", &parent_id,
		      "summary", &summary,
		      "icon", &icon,
		      NULL);

	/* TRANSLATORS: this is the group category name */
	g_print ("%s: %s\n", _("Category"), name);
	/* TRANSLATORS: this is group identifier */
	g_print (" %s: %s\n", _("ID"), cat_id);
	if (parent_id != NULL) {
		/* TRANSLATORS: this is the parent group */
		g_print (" %s: %s\n", _("Parent"), parent_id);
	}
	/* TRANSLATORS: this is the name of the parent group */
	g_print (" %s: %s\n", _("Name"), name);
	if (summary != NULL) {
		/* TRANSLATORS: this is the summary of the group */
		g_print (" %s: %s\n", _("Summary"), summary);
	}
	/* TRANSLATORS: this is preferred icon for the group */
	g_print (" %s: %s\n", _("Icon"), icon);

	g_free (name);
	g_free (cat_id);
	g_free (parent_id);
	g_free (summary);
	g_free (icon);
}

/**
 * pk_console_update_detail_cb:
 **/
static void
pk_console_update_detail_cb (PkUpdateDetail *item, gpointer data)
{
	gchar *package = NULL;
	gchar *package_id;
	gchar **updates;
	gchar **obsoletes;
	gchar **vendor_urls;
	gchar **bugzilla_urls;
	gchar **cve_urls;
	PkRestartEnum restart;
	gchar *update_text;
	gchar *changelog;
	PkUpdateStateEnum state;
	gchar *issued;
	gchar *updated;
	gchar *tmp;

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "updates", &updates,
		      "obsoletes", &obsoletes,
		      "vendor-urls", &vendor_urls,
		      "bugzilla-urls", &bugzilla_urls,
		      "cve-urls", &cve_urls,
		      "restart", &restart,
		      "update-text", &update_text,
		      "changelog", &changelog,
		      "state", &state,
		      "issued", &issued,
		      "updated", &updated,
		      NULL);

	/* TRANSLATORS: this is a header for the package that can be updated */
	g_print ("%s\n", _("Details about the update:"));

	/* create printable */
	package = pk_package_id_to_printable (package_id);

	/* TRANSLATORS: details about the update, package name and version */
	g_print (" %s: %s\n", _("Package"), package);
	if (updates != NULL) {
		tmp = g_strjoinv (", ", updates);
		/* TRANSLATORS: details about the update, any packages that
		 * this update updates */
		g_print (" %s: %s\n", _("Updates"), tmp);
		g_free (tmp);
	}
	if (obsoletes != NULL) {
		tmp = g_strjoinv (", ", obsoletes);
		/* TRANSLATORS: details about the update, any packages that
		 * this update obsoletes */
		g_print (" %s: %s\n", _("Obsoletes"), tmp);
		g_free (tmp);
	}
	if (vendor_urls != NULL) {
		tmp = g_strjoinv (", ", vendor_urls);
		/* TRANSLATORS: details about the update, the vendor URLs */
		g_print (" %s: %s\n", _("Vendor"), tmp);
		g_free (tmp);
	}
	if (bugzilla_urls != NULL) {
		tmp = g_strjoinv (", ", bugzilla_urls);
		/* TRANSLATORS: details about the update, the bugzilla URLs */
		g_print (" %s: %s\n", _("Bugzilla"), tmp);
		g_free (tmp);
	}
	if (cve_urls != NULL) {
		tmp = g_strjoinv (", ", cve_urls);
		/* TRANSLATORS: details about the update, the CVE URLs */
		g_print (" %s: %s\n", _("CVE"), tmp);
		g_free (tmp);
	}
	if (restart != PK_RESTART_ENUM_NONE) {
		/* TRANSLATORS: details about the update, if the package
		 * requires a restart */
		g_print (" %s: %s\n", _("Restart"), pk_restart_enum_to_string (restart));
	}
	if (update_text != NULL) {
		/* TRANSLATORS: details about the update, any description of
		 * the update */
		g_print (" %s: %s\n", _("Update text"), update_text);
	}
	if (changelog != NULL) {
		/* TRANSLATORS: details about the update, the changelog for
		 * the package */
		g_print (" %s: %s\n", _("Changes"), changelog);
	}
	if (state != PK_UPDATE_STATE_ENUM_UNKNOWN) {
		/* TRANSLATORS: details about the update, the ongoing state
		 * of the update */
		g_print (" %s: %s\n", _("State"), pk_update_state_enum_to_string (state));
	}
	if (issued != NULL) {
		/* TRANSLATORS: details about the update, date the update
		 * was issued */
		g_print (" %s: %s\n", _("Issued"), issued);
	}
	if (updated != NULL) {
		/* TRANSLATORS: details about the update, date the update
		 * was updated */
		g_print (" %s: %s\n", _("Updated"), updated);
	}
	g_free (package);
	g_free (package_id);
	g_strfreev (updates);
	g_strfreev (obsoletes);
	g_strfreev (vendor_urls);
	g_strfreev (bugzilla_urls);
	g_strfreev (cve_urls);
	g_free (update_text);
	g_free (changelog);
	g_free (issued);
	g_free (updated);
}

/**
 * pk_console_repo_detail_cb:
 **/
static void
pk_console_repo_detail_cb (PkRepoDetail *item, gpointer data)
{
	gchar *enabled_pad;
	gchar *repo_pad;
	gchar *repo_id;
	gboolean enabled;
	gchar *description;

	/* get data */
	g_object_get (item,
		      "repo-id", &repo_id,
		      "enabled", &enabled,
		      "description", &description,
		      NULL);

	if (enabled) {
		/* TRANSLATORS: if the repo is enabled */
		enabled_pad = pk_strpad (_("Enabled"), 10);
	} else {
		/* TRANSLATORS: if the repo is disabled */
		enabled_pad = pk_strpad (_("Disabled"), 10);
	}

	repo_pad = pk_strpad (repo_id, 25);
	g_print (" %s %s %s\n", enabled_pad, repo_pad, description);
	g_free (enabled_pad);
	g_free (repo_pad);
	g_free (repo_id);
	g_free (description);
}

/**
 * pk_console_require_restart_cb:
 **/
static void
pk_console_require_restart_cb (PkRequireRestart *item, gpointer data)
{
	gchar *package = NULL;
	gchar *package_id;
	PkRestartEnum restart;

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "restart", &restart,
		      NULL);

	/* create printable */
	package = pk_package_id_to_printable (package_id);

	if (restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: a package requires the system to be restarted */
		g_print ("%s %s\n", _("System restart required by:"), package);
	} else if (restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: a package requires the session to be restarted */
		g_print ("%s %s\n", _("Session restart required:"), package);
	} else if (restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: a package requires the system to be restarted
		 * due to a security update*/
		g_print ("%s %s\n", _("System restart (security) required by:"), package);
	} else if (restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: a package requires the session to be restarted
		 * due to a security update */
		g_print ("%s %s\n", _("Session restart (security) required:"), package);
	} else if (restart == PK_RESTART_ENUM_APPLICATION) {
		/* TRANSLATORS: a package requires the application to be restarted */
		g_print ("%s %s\n", _("Application restart required by:"), package);
	}

	g_free (package_id);
	g_free (package);
}

/**
 * pk_console_details_cb:
 **/
static void
pk_console_details_cb (PkDetails *item, gpointer data)
{
	gchar *package = NULL;
	gchar *package_id;
	gchar *license;
	gchar *description;
	gchar *url;
	PkGroupEnum group;
	guint64 size;

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "license", &license,
		      "description", &description,
		      "url", &url,
		      "group", &group,
		      "size", &size,
		      NULL);

	/* create printable */
	package = pk_package_id_to_printable (package_id);

	/* TRANSLATORS: This a list of details about the package */
	g_print ("%s\n", _("Package description"));
	g_print ("  package:     %s\n", package);
	g_print ("  license:     %s\n", license);
	g_print ("  group:       %s\n", pk_group_enum_to_string (group));
	g_print ("  description: %s\n", description);
	g_print ("  size:	%lu bytes\n", (long unsigned int) size);
	g_print ("  url:	 %s\n", url);

	g_free (package_id);
	g_free (license);
	g_free (description);
	g_free (url);
	g_free (package);
}

/**
 * pk_console_files_cb:
 **/
static void
pk_console_files_cb (PkFiles *item, gpointer data)
{
	guint i;
	gchar **files;

	/* get data */
	g_object_get (item,
		      "files", &files,
		      NULL);

	/* empty */
	if (files == NULL || files[0] == NULL) {
		/* TRANSLATORS: This where the package has no files */
		g_print ("%s\n", _("No files"));
		goto out;
	}

	/* TRANSLATORS: This a list files contained in the package */
	g_print ("%s\n", _("Package files"));
	for (i=0; files[i] != NULL; i++) {
		g_print ("  %s\n", files[i]);
	}
out:
	g_strfreev (files);
}

/**
 * pk_console_defer_status_update_cb:
 **/
static gboolean
pk_console_defer_status_update_cb (gpointer user_data)
{
	PkConsoleCtx *ctx = (PkConsoleCtx *) user_data;
	const gchar *text;

	text = pk_status_enum_to_localised_text (ctx->defered_status);
	pk_progress_bar_start (ctx->progressbar, text);
	g_source_remove (ctx->defered_status_id);
	ctx->defered_status_id = 0;
	return FALSE;
}

/**
 * pk_console_defer_status_update:
 **/
static void
pk_console_defer_status_update (PkConsoleCtx *ctx, PkStatusEnum status)
{
	if (ctx->defered_status_id > 0)
		g_source_remove (ctx->defered_status_id);
	ctx->defered_status = status;
	ctx->defered_status_id = g_timeout_add (50,
						pk_console_defer_status_update_cb,
						ctx);
}

/**
 * pk_console_progress_cb:
 **/
static void
pk_console_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
	gint percentage;
	PkStatusEnum status;
	PkRoleEnum role;
	const gchar *text;
	gchar *package_id = NULL;
	gchar *printable = NULL;
	guint64 transaction_flags;
	PkConsoleCtx *ctx = (PkConsoleCtx *) data;

	/* role */
	if (type == PK_PROGRESS_TYPE_ROLE) {
		g_object_get (progress,
			      "role", &role,
			      "transaction-flags", &transaction_flags,
			      NULL);
		if (role == PK_ROLE_ENUM_UNKNOWN)
			goto out;

		/* don't show the role when simulating */
		if (ctx->defered_status != PK_STATUS_ENUM_UNKNOWN &&
		    pk_bitfield_contain (transaction_flags,
					 PK_TRANSACTION_FLAG_ENUM_SIMULATE))
			goto out;

		/* show new status on the bar */
		text = pk_role_enum_to_localised_present (role);
		if (!ctx->is_console) {
			/* TRANSLATORS: the role is the point of the transaction,
			 * e.g. update-packages */
			g_print ("%s:\t%s\n", _("Transaction"), text);
			goto out;
		}
		pk_progress_bar_start (ctx->progressbar, text);
	}

	/* package-id */
	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_object_get (progress,
			      "package-id", &package_id,
			      NULL);
		if (package_id == NULL)
			goto out;

		if (!ctx->is_console) {
			/* create printable */
			printable = pk_package_id_to_printable (package_id);

			/* TRANSLATORS: the package that is being processed */
			g_print ("%s:\t%s\n", _("Package"), printable);
			goto out;
		}
	}

	/* percentage */
	if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_object_get (progress,
			      "percentage", &percentage,
			      NULL);
		if (!ctx->is_console) {
			/* only print the 10's */
			if (percentage % 10 != 0)
				goto out;

			/* TRANSLATORS: the percentage complete of the transaction */
			g_print ("%s:\t%i\n", _("Percentage"), percentage);
			goto out;
		}
		pk_progress_bar_set_percentage (ctx->progressbar, percentage);
	}

	/* status */
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "role", &role,
			      "status", &status,
			      "transaction-flags", &transaction_flags,
			      NULL);

		/* don't show finished multiple times in the output */
		if (role == PK_ROLE_ENUM_RESOLVE &&
		    status == PK_STATUS_ENUM_FINISHED)
			goto out;

		/* show new status on the bar */
		if (!ctx->is_console) {
			text = pk_status_enum_to_localised_text (status);
			/* TRANSLATORS: the status of the transaction (e.g. downloading) */
			g_print ("%s: \t%s\n", _("Status"), text);
			goto out;
		}

		/* defer most status actions for 50ms */
		if (status != PK_STATUS_ENUM_FINISHED) {
			if (pk_bitfield_contain (transaction_flags,
						 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
				/* don't show progress when doing the simulate pass */
				pk_console_defer_status_update (ctx,
								PK_STATUS_ENUM_TEST_COMMIT);
			} else {
				pk_console_defer_status_update (ctx, status);
			}
		} else {
			text = pk_status_enum_to_localised_text (status);
			pk_progress_bar_start (ctx->progressbar, text);
		}
	}
out:
	g_free (printable);
	g_free (package_id);
}

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (GObject *object, GAsyncResult *res, gpointer data)
{
	const gchar *filename;
	gboolean ret;
	GError *error = NULL;
	GFile *file = NULL;
	GPtrArray *array;
	PkError *error_code = NULL;
	PkPackageSack *sack;
	PkRestartEnum restart;
	PkResults *results;
	PkRoleEnum role;
	PkConsoleCtx *ctx = (PkConsoleCtx *) data;

	/* no more progress */
	if (ctx->is_console) {
		pk_progress_bar_end (ctx->progressbar);
	} else {
		/* TRANSLATORS: the results from the transaction */
		g_print ("%s\n", _("Results:"));
	}

	/* get the results */
	results = pk_task_generic_finish (PK_TASK (ctx->task), res, &error);
	if (results == NULL) {
		/* TRANSLATORS: we failed to get any results, which is pretty
		 * fatal in my book */
		g_print ("%s: %s\n", _("Fatal error"), error->message);
		g_error_free (error);
		ctx->retval = PK_EXIT_CODE_TRANSACTION_FAILED;
		goto out;
	}

	/* get the role */
	g_object_get (G_OBJECT(results), "role", &role, NULL);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {

		/* print an error */
		if (role == PK_ROLE_ENUM_UPDATE_PACKAGES &&
		    pk_error_get_code (error_code) == PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE) {
			/* TRANSLATORS: the user asked to update everything,
			 * but there is nothing that can be updated */
			g_print ("%s\n", _("There are no packages to update."));
		} else {
			/* TRANSLATORS: the transaction failed in a way we could
			 * not expect */
			g_print ("%s: %s, %s\n", _("The transaction failed"), pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}

		/* special case */
		if (pk_error_get_code (error_code) == PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE)
			ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;
		goto out;
	}

	/* get the sack */
	sack = pk_results_get_package_sack (results);
	pk_package_sack_sort (sack, PK_PACKAGE_SACK_SORT_TYPE_NAME);
	array = pk_package_sack_get_array (sack);

	/* package */
	filename = g_object_get_data (G_OBJECT (ctx->task), "PkConsole:list-create-filename");
	if (!ctx->is_console ||
	    (role != PK_ROLE_ENUM_INSTALL_PACKAGES &&
	     role != PK_ROLE_ENUM_UPDATE_PACKAGES &&
	     role != PK_ROLE_ENUM_REMOVE_PACKAGES &&
	     filename == NULL)) {
		g_ptr_array_foreach (array, (GFunc) pk_console_package_cb, ctx);
	}

	/* special case */
	if (array->len == 0 &&
	    (role == PK_ROLE_ENUM_GET_UPDATES ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		/* TRANSLATORS: print a message when there are no updates */
		g_print ("%s\n", _("There are no updates available at this time."));
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;
	}

	g_ptr_array_unref (array);
	g_object_unref (sack);

	/* transaction */
	array = pk_results_get_transaction_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_transaction_cb, ctx);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_OLD_TRANSACTIONS)
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* distro_upgrade */
	array = pk_results_get_distro_upgrade_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_distro_upgrade_cb, ctx);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		g_print ("%s\n", _("There are no upgrades available at this time."));
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;
	}

	g_ptr_array_unref (array);

	/* category */
	array = pk_results_get_category_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_category_cb, ctx);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_CATEGORIES)
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* update_detail */
	array = pk_results_get_update_detail_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_update_detail_cb, ctx);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* repo_detail */
	array = pk_results_get_repo_detail_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_repo_detail_cb, ctx);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_REPO_LIST)
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* require_restart */
	array = pk_results_get_require_restart_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_require_restart_cb, ctx);
	g_ptr_array_unref (array);

	/* details */
	array = pk_results_get_details_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_details_cb, ctx);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_DETAILS)
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* don't print files if we are DownloadPackages */
	if (role != PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		array = pk_results_get_files_array (results);
		g_ptr_array_foreach (array, (GFunc) pk_console_files_cb, ctx);
		g_ptr_array_unref (array);
	}

	/* is there any restart to notify the user? */
	restart = pk_results_get_require_restart_worst (results);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: a package needs to restart their system */
		g_print ("%s\n", _("Please restart the computer to complete the update."));
	} else if (restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: a package needs to restart the session */
		g_print ("%s\n", _("Please logout and login to complete the update."));
	} else if (restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: a package needs to restart their system (due to security) */
		g_print ("%s\n", _("Please restart the computer to complete the update as important security updates have been installed."));
	} else if (restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: a package needs to restart the session (due to security) */
		g_print ("%s\n", _("Please logout and login to complete the update as important security updates have been installed."));
	}

	/* write the sack to disk */
	if (role == PK_ROLE_ENUM_GET_PACKAGES && filename != NULL) {
		file = g_file_new_for_path (filename);
		ret = pk_package_sack_to_file (sack, file, &error);
		if (!ret) {
			g_print ("%s: %s\n", _("Fatal error"), error->message);
			g_error_free (error);
			ctx->retval = PK_EXIT_CODE_TRANSACTION_FAILED;
			goto out;
		}
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (file != NULL)
		g_object_unref (file);
	if (results != NULL)
		g_object_unref (results);
	g_main_loop_quit (ctx->loop);
}

/**
 * pk_console_resolve_package:
 **/
static gchar *
pk_console_resolve_package (PkConsoleCtx *ctx, const gchar *package_name, GError **error)
{
	const gchar *package_id_tmp;
	gchar *package_id = NULL;
	gboolean valid;
	gchar **tmp;
	gchar **split = NULL;
	PkResults *results;
	GPtrArray *array = NULL;
	guint i;
	gchar *printable;
	PkPackage *package;
	PkError *error_code = NULL;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package_name);
	if (valid)
		return g_strdup (package_name);

	/* split */
	tmp = g_strsplit (package_name, ",", -1);

	/* get the list of possibles */
	results = pk_client_resolve (PK_CLIENT (ctx->task),
				     ctx->filters, tmp,
				     ctx->cancellable,
				     pk_console_progress_cb, ctx,
				     error);
	if (results == NULL)
		goto out;

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error_literal (error,
				     PK_CONSOLE_ERROR,
				     pk_error_get_code (error_code),
				     pk_error_get_details (error_code));
		goto out;
	}

	/* nothing found */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
			     "could not find %s", package_name);
		goto out;
	}

	/* just one thing found */
	if (array->len == 1) {
		package = g_ptr_array_index (array, 0);
		g_object_get (package,
			      "package-id", &package_id,
			      NULL);
		goto out;
	}

	/* TRANSLATORS: more than one package could be found that matched,
	 * to follow is a list of possible packages  */
	g_print ("%s\n", _("More than one package matches:"));
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		package_id_tmp = pk_package_get_id (package);
		split = pk_package_id_split (package_id_tmp);
		printable = pk_package_id_to_printable (package_id_tmp);
		g_print ("%i. %s [%s]\n", i+1, printable, split[PK_PACKAGE_ID_DATA]);
		g_free (printable);
	}

	/* TRANSLATORS: This finds out which package in the list to use */
	i = pk_console_get_number (_("Please choose the correct package: "), array->len);
	if (i == 0) {
		g_set_error_literal (error,
				     PK_CONSOLE_ERROR,
				     PK_ERROR_ENUM_TRANSACTION_CANCELLED,
				     "User aborted selection");
		goto out;
	}
	package = g_ptr_array_index (array, i-1);
	g_object_get (package,
		      "package-id", &package_id,
		      NULL);
out:
	if (results != NULL)
		g_object_unref (results);
	if (array != NULL)
		g_ptr_array_unref (array);
	g_strfreev (tmp);
	g_strfreev (split);
	return package_id;
}

/**
 * pk_console_resolve_packages:
 **/
static gchar **
pk_console_resolve_packages (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	GPtrArray *array;
	gchar **package_ids = NULL;
	guint i;
	guint len;
	gchar *package_id;
	GError *error_local = NULL;

	/* get length */
	len = g_strv_length (packages);
	g_debug ("resolving %i packages", len);

	/* resolve each package */
	array = g_ptr_array_new ();
	for (i = 0; i < len; i++) {
		package_id = pk_console_resolve_package (ctx,
							 packages[i],
							 &error_local);
		if (package_id == NULL) {
			if (g_error_matches (error_local,
					     PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_PACKAGE_NOT_FOUND)) {
				/* TRANSLATORS: we asked to install a package
				 * that could not be found in any repo */
				g_print ("%s: %s\n", _("Package not found"),
					 packages[i]);
				g_clear_error (&error_local);
				continue;
			} else {
				g_propagate_error (error, error_local);
				goto out;
			}
		}
		g_ptr_array_add (array, package_id);
	}

	/* nothing */
	if (array->len == 0) {
		g_set_error_literal (error,
				     PK_CONSOLE_ERROR,
				     PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				     /* TRANSLATORS: we couldn't find anything */
				     _("No packages were found"));
		goto out;
	}

	/* convert to GStrv */
	g_ptr_array_add (array, NULL);
	package_ids = g_strdupv ((gchar **) array->pdata);
out:
	g_ptr_array_unref (array);
	return package_ids;
}

/**
 * pk_console_install_packages:
 **/
static gboolean
pk_console_install_packages (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;
	guint i;

	/* test to see if we've been given files, not packages */
	for (i=0; packages[i] != NULL; i++) {
		ret = !g_file_test (packages[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			g_set_error (error,
				     PK_CONSOLE_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
					/* TRANSLATORS: The user used
					 * 'pkcon install dave.rpm' rather than
					 * 'pkcon install-local dave.rpm' */
				     _("Expected package name, actually got file. "
				       "Try using 'pkcon install-local %s' instead."),
				     packages[i]);
			goto out;
		}
	}

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);
	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the list
			      * of files for the package. The detailed error follows */
			     _("This tool could not find any available package: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		ctx->retval = PK_EXIT_CODE_FILE_NOT_FOUND;
		goto out;
	}

	/* do the async action */
	pk_task_install_packages_async (PK_TASK (ctx->task),
					package_ids, ctx->cancellable,
					pk_console_progress_cb, ctx,
					pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_remove_packages:
 **/
static gboolean
pk_console_remove_packages (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_INSTALLED);
	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the list
			      * of files for the package. The detailed error follows */
			     _("This tool could not find the installed package: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_remove_packages_async (PK_TASK (ctx->task),
				       package_ids,
				       TRUE, FALSE,
				       ctx->cancellable,
				       pk_console_progress_cb, ctx,
				       pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_download_packages:
 **/
static gboolean
pk_console_download_packages (PkConsoleCtx *ctx, gchar **packages, const gchar *directory, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the list
			      * of files for the package. The detailed error follows */
			     _("This tool could not find the package: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_download_packages_async (PK_TASK (ctx->task),
					 package_ids,
					 directory,
					 ctx->cancellable,
					 pk_console_progress_cb, ctx,
					 pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_update_packages:
 **/
static gboolean
pk_console_update_packages (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);
	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the list
			      * of files for the package. The detailed error follows */
			     _("This tool could not find the package: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_update_packages_async (PK_TASK (ctx->task),
				       package_ids,
				       ctx->cancellable,
				       pk_console_progress_cb, ctx,
				       pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_update_system:
 **/
static gboolean
pk_console_update_system (PkConsoleCtx *ctx, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	PkPackageSack *sack = NULL;
	PkResults *results;

	/* get the current updates */
	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NEWEST);
	results = pk_task_get_updates_sync (PK_TASK (ctx->task),
					    ctx->filters,
					    ctx->cancellable,
					    pk_console_progress_cb, ctx,
					    error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	sack = pk_results_get_package_sack (results);
	package_ids = pk_package_sack_get_ids (sack);
	if (g_strv_length (package_ids) == 0) {
		pk_progress_bar_end (ctx->progressbar);
		/* TRANSLATORS: there are no updates, so nothing to do */
		g_print ("%s\n", _("No packages require updating to newer versions."));
		ctx->retval = PK_EXIT_CODE_NOTHING_USEFUL;
		ret = FALSE;
		goto out;
	}
	pk_task_update_packages_async (PK_TASK (ctx->task),
				       package_ids,
				       ctx->cancellable,
				       pk_console_progress_cb, ctx,
				       pk_console_finished_cb, ctx);
out:
	if (sack != NULL)
		g_object_unref (sack);
	if (results != NULL)
		g_object_unref (results);
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_requires:
 **/
static gboolean
pk_console_get_requires (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_INSTALLED);
	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the list
			      * of files for the package. The detailed error follows */
			     _("This tool could not find all the packages: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_requires_async (PK_TASK (ctx->task),
				    ctx->filters,
				    package_ids,
				    TRUE,
				    ctx->cancellable,
				    pk_console_progress_cb, ctx,
				    pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_depends:
 **/
static gboolean
pk_console_get_depends (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the
			      * dependencies for the package. The detailed error follows */
			     _("This tool could not find all the packages: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_depends_async (PK_TASK (ctx->task),
				   ctx->filters,
				   package_ids,
				   FALSE,
				   ctx->cancellable,
				   pk_console_progress_cb, ctx,
				   pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_details:
 **/
static gboolean
pk_console_get_details (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: There was an error getting the
			      * details about the package. The detailed error follows */
			      _("This tool could not find all the packages: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_details_async (PK_TASK (ctx->task),
				   package_ids,
				   ctx->cancellable,
				   pk_console_progress_cb, ctx,
				   pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_files:
 **/
static gboolean
pk_console_get_files (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: The package name was not found in
			      * any software sources. The detailed error follows */
			     _("This tool could not find all the packages: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_files_async (PK_TASK (ctx->task),
				 package_ids,
				 ctx->cancellable,
				 pk_console_progress_cb, ctx,
				 pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_update_detail
 **/
static gboolean
pk_console_get_update_detail (PkConsoleCtx *ctx, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	pk_bitfield_add (ctx->filters, PK_FILTER_ENUM_NOT_INSTALLED);
	package_ids = pk_console_resolve_packages (ctx, packages, &error_local);
	if (package_ids == NULL) {
		g_set_error (error,
			     PK_CONSOLE_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     /* TRANSLATORS: The package name was not found in
			      * any software sources. The detailed error follows */
			     _("This tool could not find all the packages: %s"),
			     error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_update_detail_async (PK_TASK (ctx->task),
					 package_ids,
					 ctx->cancellable,
					 pk_console_progress_cb, ctx,
					 pk_console_finished_cb, ctx);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_notify_connected_cb:
 **/
static void
pk_console_notify_connected_cb (PkControl *control_, GParamSpec *pspec, gpointer data)
{
	PkConsoleCtx *ctx = (PkConsoleCtx *) data;
	gboolean connected;

	/* if the daemon crashed, don't hang around */
	g_object_get (control_,
		      "connected", &connected,
		      NULL);
	if (!connected) {
		/* TRANSLATORS: This is when the daemon crashed, and we are up
		 * shit creek without a paddle */
		g_print ("%s\n", _("The daemon crashed mid-transaction!"));
		g_main_loop_quit (ctx->loop);
	}
}


/**
 * pk_console_sigint_cb:
 **/
static gboolean
pk_console_sigint_cb (gpointer user_data)
{
	PkConsoleCtx *ctx = (PkConsoleCtx *) user_data;
	g_debug ("Handling SIGINT");
	g_cancellable_cancel (ctx->cancellable);
	return FALSE;
}

/**
 * pk_console_get_summary:
 **/
static gchar *
pk_console_get_summary (PkConsoleCtx *ctx)
{
	GString *string;
	string = g_string_new ("");

	/* TRANSLATORS: This is the header to the --help menu */
	g_string_append_printf (string, "%s\n\n%s\n", _("PackageKit Console Interface"),
				/* these are commands we can use with pkcon */
				_("Subcommands:"));

	/* always */
	g_string_append_printf (string, "  %s\n", "backend-details");
	g_string_append_printf (string, "  %s\n", "get-ctx->roles");
	g_string_append_printf (string, "  %s\n", "get-groups");
	g_string_append_printf (string, "  %s\n", "get-filters");
	g_string_append_printf (string, "  %s\n", "get-transactions");
	g_string_append_printf (string, "  %s\n", "get-time");

	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_SEARCH_NAME) ||
	    pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_SEARCH_DETAILS) ||
	    pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_SEARCH_GROUP) ||
	    pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_SEARCH_FILE))
		g_string_append_printf (string, "  %s\n", "search [name|details|group|file] [data]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_INSTALL_PACKAGES))
		g_string_append_printf (string, "  %s\n", "install [packages]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_INSTALL_FILES))
		g_string_append_printf (string, "  %s\n", "install-local [files]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES))
		g_string_append_printf (string, "  %s\n", "download [directory] [packages]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_INSTALL_SIGNATURE))
		g_string_append_printf (string, "  %s\n", "install-sig [type] [key_id] [package_id]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_REMOVE_PACKAGES))
		g_string_append_printf (string, "  %s\n", "remove [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_UPDATE_PACKAGES))
		g_string_append_printf (string, "  %s\n", "update <package>");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_REFRESH_CACHE))
		g_string_append_printf (string, "  %s\n", "refresh [force]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_RESOLVE))
		g_string_append_printf (string, "  %s\n", "resolve [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_UPDATES))
		g_string_append_printf (string, "  %s\n", "get-updates");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_DEPENDS))
		g_string_append_printf (string, "  %s\n", "get-depends [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_REQUIRES))
		g_string_append_printf (string, "  %s\n", "get-requires [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_DETAILS))
		g_string_append_printf (string, "  %s\n", "get-details [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES))
		g_string_append_printf (string, "  %s\n", "get-distro-upgrades");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_FILES))
		g_string_append_printf (string, "  %s\n", "get-files [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL))
		g_string_append_printf (string, "  %s\n", "get-update-detail [package]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_PACKAGES))
		g_string_append_printf (string, "  %s\n", "get-packages");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_REPO_LIST))
		g_string_append_printf (string, "  %s\n", "repo-list");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_REPO_ENABLE)) {
		g_string_append_printf (string, "  %s\n", "repo-enable [repo_id]");
		g_string_append_printf (string, "  %s\n", "repo-disable [repo_id]");
	}
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_REPO_SET_DATA))
		g_string_append_printf (string, "  %s\n", "repo-set-data [repo_id] [parameter] [value];");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_WHAT_PROVIDES))
		g_string_append_printf (string, "  %s\n", "what-provides [search]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_ACCEPT_EULA))
		g_string_append_printf (string, "  %s\n", "accept-eula [eula-id]");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_GET_CATEGORIES))
		g_string_append_printf (string, "  %s\n", "get-categories");
	if (pk_bitfield_contain (ctx->roles, PK_ROLE_ENUM_REPAIR_SYSTEM))
		g_string_append_printf (string, "  %s\n", "repair");
#ifdef PK_HAS_OFFLINE_UPDATES
	g_string_append_printf (string, "  %s\n", "offline-get-prepared");
	g_string_append_printf (string, "  %s\n", "offline-trigger");
	g_string_append_printf (string, "  %s\n", "offline-status");
#endif
	return g_string_free (string, FALSE);
}

/**
 * pk_console_get_time_since_action_cb:
 **/
static void
pk_console_get_time_since_action_cb (GObject *object, GAsyncResult *res, gpointer data)
{
	guint time_ms;
	GError *error = NULL;
	PkConsoleCtx *ctx = (PkConsoleCtx *) data;

	/* get the results */
	time_ms = pk_control_get_time_since_action_finish (ctx->control, res, &error);
	if (time_ms == 0) {
		/* TRANSLATORS: we keep a database updated with the time that an
		 * action was last executed */
		g_print ("%s: %s\n", _("Failed to get the time since this action was last completed"), error->message);
		g_error_free (error);
		goto out;
	}
	/* TRANSLATORS: this is the time since this role was used */
	g_print ("%s: %is\n", _("Time since"), time_ms);
out:
	g_main_loop_quit (ctx->loop);
}

/**
 * pk_console_offline_get_prepared:
 **/
static gboolean
pk_console_offline_get_prepared (GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gchar **split = NULL;
	gchar *tmp;
	guint i;

	/* get data */
	ret = g_file_get_contents ("/var/lib/PackageKit/prepared-update",
				   &data, NULL, NULL);
	if (!ret) {
		g_set_error_literal (error,
				     1,
				     PK_EXIT_CODE_FILE_NOT_FOUND,
				     "No offline updates have been prepared");
		goto out;
	}
	split = g_strsplit (data, "\n", -1);
	/* TRANSLATORS: There follows a list of packages downloaded and ready
	 * to be updated */
	g_print ("%s\n", _("Prepared updates:"));
	for (i = 0; split[i] != NULL; i++) {
		tmp = pk_package_id_to_printable (split[i]);
		g_print ("%s\n", tmp);
		g_free (tmp);
	}
out:
	g_free (data);
	return ret;
}

/**
 * pk_console_offline_trigger:
 **/
static gboolean
pk_console_offline_trigger (GError **error)
{
	gboolean ret;
	gchar *cmdline;

	cmdline = g_strdup_printf ("pkexec %s/pk-trigger-offline-update", LIBEXECDIR);
	ret = g_spawn_command_line_sync (cmdline,
					 NULL,
					 NULL,
					 NULL,
					 error);
	g_free (cmdline);
	return ret;
}

#define PK_OFFLINE_UPDATE_RESULTS	"PackageKit Offline Update Results"

/**
 * pk_console_offline_status:
 **/
static gboolean
pk_console_offline_status (GError **error)
{
	gboolean ret;
	gboolean success;
	gchar *data = NULL;
	gchar **split = NULL;
	gchar *tmp;
	GKeyFile *file;
	guint i;

	/* load data */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file,
					 "/var/lib/PackageKit/offline-update-competed",
					 G_KEY_FILE_NONE,
					 NULL);
	if (!ret) {
		g_set_error_literal (error,
				     1,
				     PK_EXIT_CODE_FILE_NOT_FOUND,
				     "No offline updates have been processed");
		goto out;
	}

	/* did it succeed */
	success = g_key_file_get_boolean (file,
					  PK_OFFLINE_UPDATE_RESULTS,
					  "Success",
					  NULL);
	if (!success) {
		g_print ("Status:\tFailed\n");
		tmp = g_key_file_get_string (file,
					     PK_OFFLINE_UPDATE_RESULTS,
					     "ErrorCode",
					     NULL);
		if (tmp != NULL)
			g_print ("ErrorCode:\%s\n", tmp);
		g_free (tmp);
		tmp = g_key_file_get_string (file,
					     PK_OFFLINE_UPDATE_RESULTS,
					     "ErrorDetails",
					     NULL);
		if (tmp != NULL)
			g_print ("ErrorDetails:\%s\n", tmp);
		g_free (tmp);
	} else {
		g_print ("Status:\tSuccess\n");
		data = g_key_file_get_string (file,
					      PK_OFFLINE_UPDATE_RESULTS,
					      "Packages",
					      NULL);
		split = g_strsplit (data, ";", -1);
		for (i = 0; split[i] != NULL; i++) {
			tmp = pk_package_id_to_printable (split[i]);
			g_print ("Updated %s\n", tmp);
			g_free (tmp);
		}
	}
out:
	g_key_file_free (file);
	g_free (data);
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkConsoleCtx *ctx = NULL;
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	gboolean background = FALSE;
	gboolean noninteractive = FALSE;
	gboolean only_download = FALSE;
	guint cache_age = 0;
	gint retval_copy = 0;
	gboolean plain = FALSE;
	gboolean program_version = FALSE;
	gboolean run_mainloop = TRUE;
	GOptionContext *context;
	gchar *options_help;
	gchar *filter = NULL;
	gchar *summary = NULL;
	const gchar *mode;
	const gchar *http_proxy;
	const gchar *ftp_proxy;
	const gchar *value = NULL;
	const gchar *details = NULL;
	const gchar *parameter = NULL;
	PkBitfield groups;
	gchar *text;

	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			/* TRANSLATORS: command line argument, just show the version string */
			_("Show the program version and exit"), NULL},
		{ "filter", '\0', 0, G_OPTION_ARG_STRING, &filter,
			/* TRANSLATORS: command line argument, use a filter to narrow down results */
			_("Set the filter, e.g. installed"), NULL},
		{ "noninteractive", 'y', 0, G_OPTION_ARG_NONE, &noninteractive,
			/* command line argument, do we ask questions */
			_("Install the packages without asking for confirmation"), NULL },
		{ "only-download", 'd', 0, G_OPTION_ARG_NONE, &only_download,
			/* command line argument, do we just download or apply changes */
			_("Prepare the transaction by downloading pakages only"), NULL },
		{ "background", 'n', 0, G_OPTION_ARG_NONE, &background,
			/* TRANSLATORS: command line argument, this command is not a priority */
			_("Run the command using idle network bandwidth and also using less power"), NULL},
		{ "plain", 'p', 0, G_OPTION_ARG_NONE, &plain,
			/* TRANSLATORS: command line argument, just output without fancy formatting */
			_("Print to screen a machine readable output, rather than using animated widgets"), NULL},
		{ "cache-age", 'c', 0, G_OPTION_ARG_INT, &cache_age,
			/* TRANSLATORS: command line argument, just output without fancy formatting */
			_("The maximum metadata cache age. Use -1 for 'never'."), NULL},
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init ();
#endif

	/* do stuff on ctrl-c */
	ctx = g_new0 (PkConsoleCtx, 1);
	ctx->defered_status = PK_STATUS_ENUM_UNKNOWN;
	ctx->loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add_full (G_PRIORITY_DEFAULT,
				SIGINT,
				pk_console_sigint_cb,
				ctx,
				NULL);

	ctx->progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (ctx->progressbar, 25);
	pk_progress_bar_set_padding (ctx->progressbar, 30);

	ctx->cancellable = g_cancellable_new ();
	context = g_option_context_new ("PackageKit Console Program");
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, pk_debug_get_option_group ());
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to contact the daemon */
		g_print ("%s: %s\n", _("Failed to parse command line"), error->message);
		g_error_free (error);
		ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
		goto out_last;
	}

	/* we need the ctx->roles early, as we only show the user only what they can do */
	ctx->control = pk_control_new ();
	ret = pk_control_get_properties (ctx->control, ctx->cancellable, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to contact the daemon */
		g_print ("%s: %s\n", _("Failed to contact PackageKit"), error->message);
		g_error_free (error);
		ctx->retval = PK_EXIT_CODE_CANNOT_SETUP;
		goto out_last;
	}

	/* get data */
	g_object_get (ctx->control,
		      "roles", &ctx->roles,
		      NULL);

	/* set the summary text based on the available ctx->roles */
	summary = pk_console_get_summary (ctx);
	g_option_context_set_summary (context, summary) ;
	options_help = g_option_context_get_help (context, TRUE, NULL);

	/* check if we are on console */
	if (!plain && isatty (fileno (stdout)) == 1)
		ctx->is_console = TRUE;

	if (program_version) {
		g_print (VERSION "\n");
		goto out_last;
	}

	if (argc < 2) {
		g_print ("%s", options_help);
		ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
		goto out_last;
	}

	/* watch when the daemon aborts */
	g_signal_connect (ctx->control, "notify::connected",
			  G_CALLBACK (pk_console_notify_connected_cb), ctx);

	/* create transactions */
	ctx->task = pk_task_text_new ();
	g_object_set (ctx->task,
		      "background", background,
		      "simulate", !noninteractive && !only_download,
		      "interactive", !noninteractive,
		      "only-download", only_download,
		      "cache-age", cache_age,
		      NULL);

	/* set the proxy */
	http_proxy = g_getenv ("http_proxy");
	ftp_proxy = g_getenv ("ftp_proxy");
	if (http_proxy != NULL ||
	    ftp_proxy != NULL) {
		ret = pk_control_set_proxy (ctx->control,
					    http_proxy,
					    ftp_proxy,
					    ctx->cancellable,
					    &error_local);
		if (!ret) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s: %s",
					     /* TRANSLATORS: The user specified
					      * an incorrect filter */
					     _("The proxy could not be set"),
					     error_local->message);
			g_error_free (error_local);
			ctx->retval = PK_EXIT_CODE_CANNOT_SETUP;
			goto out;
		}
	}

	/* check filter */
	if (filter != NULL) {
		ctx->filters = pk_filter_bitfield_from_string (filter);
		if (ctx->filters == 0) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s: %s",
					     /* TRANSLATORS: The user specified
					      * an incorrect filter */
					     _("The filter specified was invalid"),
					     filter);
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
	}
	g_debug ("filter=%s, filters=%" PK_BITFIELD_FORMAT, filter, ctx->filters);

	mode = argv[1];
	if (argc > 2)
		value = argv[2];
	if (argc > 3)
		details = argv[3];
	if (argc > 4)
		parameter = argv[4];

	/* start polkit tty agent to listen for password requests */
	pk_polkit_agent_open ();

	/* parse the big list */
	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: a search type can
					      * be name, details, file, etc */
					     "%s", _("A search type is required, e.g. name"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;

		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				error = g_error_new (PK_CONSOLE_ERROR,
						     PK_ERROR_ENUM_INTERNAL_ERROR,
						     /* TRANSLATORS: the user
						      * needs to provide a search term */
						     "%s", _("A search term is required"));
				ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			pk_task_search_names_async (PK_TASK (ctx->task),
						    ctx->filters,
						    argv + 3,
						    ctx->cancellable,
						    pk_console_progress_cb, ctx,
						    pk_console_finished_cb, ctx);

		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				error = g_error_new (PK_CONSOLE_ERROR,
						     PK_ERROR_ENUM_INTERNAL_ERROR,
						     /* TRANSLATORS: the user needs
						      * to provide a search term */
						     "%s", _("A search term is required"));
				ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			pk_task_search_details_async (PK_TASK (ctx->task),
						      ctx->filters,
						      argv + 3,
						      ctx->cancellable,
						      pk_console_progress_cb, ctx,
						      pk_console_finished_cb, ctx);

		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				error = g_error_new (PK_CONSOLE_ERROR,
						     PK_ERROR_ENUM_INTERNAL_ERROR,
						     /* TRANSLATORS: the user needs
						      * to provide a search term */
						     "%s", _("A search term is required"));
				ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			pk_task_search_groups_async (PK_TASK (ctx->task),
						     ctx->filters,
						     argv + 3,
						     ctx->cancellable,
						     pk_console_progress_cb, ctx,
						     pk_console_finished_cb, ctx);

		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				error = g_error_new (PK_CONSOLE_ERROR,
						     PK_ERROR_ENUM_INTERNAL_ERROR,
						     /* TRANSLATORS: the user needs
						      * to provide a search term */
						     "%s", _("A search term is required"));
				ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			pk_task_search_files_async (PK_TASK (ctx->task),
						    ctx->filters,
						    argv + 3,
						    ctx->cancellable,
						    pk_console_progress_cb, ctx,
						    pk_console_finished_cb, ctx);
		} else {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: the search type was
					      * provided, but invalid */
					     "%s", _("Invalid search type"));
		}

	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: the user did not
					      * specify what they wanted to install */
					     "%s", _("A package name to install is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_install_packages (ctx,
							    argv + 2,
							    &error);

	} else if (strcmp (mode, "install-local") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: the user did not
					      * specify what they wanted to install */
					     "%s", _("A filename to install is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_install_files_async (PK_TASK (ctx->task),
					     argv + 2,
					     ctx->cancellable,
					     pk_console_progress_cb, ctx,
					     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "install-sig") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: geeky error, real
					      * users won't see this */
					     "%s", _("A type, key_id and package_id are required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_install_signature_async (PK_CLIENT (ctx->task),
						   PK_SIGTYPE_ENUM_GPG,
						   details,
						   parameter,
						   ctx->cancellable,
						   pk_console_progress_cb, ctx,
						   pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: the user did not
					      * specify what they wanted to remove */
					     "%s", _("A package name to remove is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_remove_packages (ctx, argv + 2, &error);

	} else if (strcmp (mode, "download") == 0) {
		if (value == NULL || details == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: the user did not
					      * specify anything about what to
					      * download or where */
					     "%s", _("A destination directory and the package names to download are required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = g_file_test (value, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR);
		if (!ret) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: the directory does
					      * not exist, so we can't continue */
					     "%s: %s", _("Directory not found"), value);
			ctx->retval = PK_EXIT_CODE_FILE_NOT_FOUND;
			goto out;
		}
		run_mainloop = pk_console_download_packages (ctx,
							     argv + 3,
							     value,
							     &error);

	} else if (strcmp (mode, "accept-eula") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: geeky error, real
					      * users won't see this */
					     "%s", _("A licence identifier (eula-id) is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_accept_eula_async (PK_CLIENT (ctx->task),
					     value,
					     ctx->cancellable,
					     pk_console_progress_cb, ctx,
					     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			/* do the system update */
			run_mainloop = pk_console_update_system (ctx, &error);
		} else {
			run_mainloop = pk_console_update_packages (ctx,
								   argv + 2,
								   &error);
		}

	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user did not
					      * specify a package name */
					     "%s", _("A package name to resolve is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_resolve_async (PK_TASK (ctx->task),
				       ctx->filters,
				       argv + 2,
				       ctx->cancellable,
				       pk_console_progress_cb, ctx,
				       pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "repo-enable") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user did not
					      * specify a repository name */
					     "%s", _("A repository name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_repo_enable_async (PK_TASK (ctx->task),
					   value,
					   TRUE,
					   ctx->cancellable,
					   pk_console_progress_cb, ctx,
					   pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "repo-disable") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user did not
					      * specify a repository name */
					     "%s", _("A repository name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_repo_enable_async (PK_TASK (ctx->task),
					   value,
					   FALSE,
					   ctx->cancellable,
					   pk_console_progress_cb, ctx,
					   pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "repo-set-data") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			/* TRANSLATORS: The user didn't provide any data */
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s", _("A repo name, parameter and value are required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_repo_set_data_async (PK_CLIENT (ctx->task),
					       value,
					       details,
					       parameter,
					       ctx->cancellable,
					       pk_console_progress_cb, ctx,
					       pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "repo-list") == 0) {
		pk_task_get_repo_list_async (PK_TASK (ctx->task),
					     ctx->filters,
					     ctx->cancellable,
					     pk_console_progress_cb, ctx,
					     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "get-time") == 0) {
		PkRoleEnum role;
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user didn't
					      * specify what action to use */
					     "%s", _("An action, e.g. 'update-packages' is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		role = pk_role_enum_from_string (value);
		if (role == PK_ROLE_ENUM_UNKNOWN) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user specified
					      * an invalid action */
					     "%s", _("A correct role is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_control_get_time_since_action_async (ctx->control,
							role,
							ctx->cancellable,
							pk_console_get_time_since_action_cb, ctx);

	} else if (strcmp (mode, "get-depends") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s", _("A package name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_get_depends (ctx, argv + 2, &error);

	} else if (strcmp (mode, "get-distro-upgrades") == 0) {
		pk_client_get_distro_upgrades_async (PK_CLIENT (ctx->task),
						     ctx->cancellable,
						     pk_console_progress_cb, ctx,
						     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "get-update-detail") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s", _("A package name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_get_update_detail (ctx, argv + 2, &error);

	} else if (strcmp (mode, "get-requires") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user did not
					      * provide a package name */
					     "%s", _("A package name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_get_requires (ctx, argv + 2, &error);

	} else if (strcmp (mode, "what-provides") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: each package
					      * "provides" certain things, e.g.
					      * mime(gstreamer-decoder-mp3),
					      * the user didn't specify it */
					     "%s", _("A package provide string is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_what_provides_async (PK_TASK (ctx->task),
					     ctx->filters,
					     argv + 2,
					     ctx->cancellable,
					     pk_console_progress_cb, ctx,
					     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "get-details") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s", _("A package name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_get_details (ctx, argv + 2, &error);

	} else if (strcmp (mode, "get-files") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "%s", _("A package name is required"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		run_mainloop = pk_console_get_files (ctx, argv + 2, &error);

	} else if (strcmp (mode, "get-updates") == 0) {
		pk_task_get_updates_async (PK_TASK (ctx->task),
					   ctx->filters,
					   ctx->cancellable,
					   pk_console_progress_cb, ctx,
					   pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "get-categories") == 0) {
		pk_task_get_categories_async (PK_TASK (ctx->task),
					      ctx->cancellable,
					      pk_console_progress_cb, ctx,
					      pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "get-packages") == 0) {
		pk_task_get_packages_async (PK_TASK (ctx->task),
					    ctx->filters,
					    ctx->cancellable,
					    pk_console_progress_cb, ctx,
					    pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "get-ctx->roles") == 0) {
		text = pk_role_bitfield_to_string (ctx->roles);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		run_mainloop = FALSE;

	} else if (strcmp (mode, "get-filters") == 0) {
		g_object_get (ctx->control,
			      "filters", &ctx->filters,
			      NULL);
		text = pk_filter_bitfield_to_string (ctx->filters);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		run_mainloop = FALSE;

	} else if (strcmp (mode, "backend-details") == 0) {
		gchar *backend_author = NULL;
		gchar *backend_description = NULL;
		gchar *backend_name = NULL;
		g_object_get (ctx->control,
			      "backend-author", &backend_author,
			      "backend-description", &backend_description,
			      "backend-name", &backend_name,
			      NULL);
		if (backend_name != NULL && backend_name[0] != '\0') {
			/* TRANSLATORS: this is the name of the backend */
			g_print ("%s:\t\t%s\n", _("Name"), backend_name);
		}
		if (backend_description != NULL && backend_description[0] != '\0') {
			/* TRANSLATORS: this is the description of the backend */
			g_print ("%s:\t%s\n", _("Description"), backend_description);
		}
		if (backend_author != NULL && backend_author[0] != '\0') {
			/* TRANSLATORS: this is the author of the backend */
			g_print ("%s:\t%s\n", _("Author"), backend_author);
		}
		g_free (backend_name);
		g_free (backend_description);
		g_free (backend_author);
		run_mainloop = FALSE;

	} else if (strcmp (mode, "get-groups") == 0) {
		g_object_get (ctx->control,
			      "groups", &groups,
			      NULL);
		text = pk_group_bitfield_to_string (groups);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		run_mainloop = FALSE;

	} else if (strcmp (mode, "offline-get-prepared") == 0) {

		run_mainloop = FALSE;
		ret = pk_console_offline_get_prepared (&error);
		if (!ret)
			ctx->retval = error->code;

	} else if (strcmp (mode, "offline-trigger") == 0) {

		run_mainloop = FALSE;
		ret = pk_console_offline_trigger (&error);
		if (!ret)
			ctx->retval = error->code;

	} else if (strcmp (mode, "offline-status") == 0) {

		run_mainloop = FALSE;
		ret = pk_console_offline_status (&error);
		if (!ret)
			ctx->retval = error->code;

	} else if (strcmp (mode, "get-transactions") == 0) {
		pk_client_get_old_transactions_async (PK_CLIENT (ctx->task),
						      10,
						      ctx->cancellable,
						      pk_console_progress_cb, ctx,
						      pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "refresh") == 0) {
		gboolean force = (value != NULL && g_strcmp0 (value, "force") == 0);
		pk_task_refresh_cache_async (PK_TASK (ctx->task),
					     force,
					     ctx->cancellable,
					     pk_console_progress_cb, ctx,
					     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "repair") == 0) {
		pk_task_repair_system_async (PK_TASK (ctx->task), ctx->cancellable,
					     pk_console_progress_cb, ctx,
					     pk_console_finished_cb, ctx);

	} else if (strcmp (mode, "list-create") == 0) {
		if (value == NULL) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: The user did not provide a distro name */
					     "%s", _("You need to specify a list file to create"));
			ctx->retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}

		/* file exists */
		ret = g_file_test (value, G_FILE_TEST_EXISTS);
		if (ret) {
			error = g_error_new (PK_CONSOLE_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     /* TRANSLATORS: There was an error
					      * getting the list of packages.
					      * The filename follows */
					     _("File already exists: %s"), value);
			ctx->retval = PK_EXIT_CODE_FILE_NOT_FOUND;
			goto out;
			return FALSE;
		}

		/* get package list */
		g_object_set_data_full (G_OBJECT (ctx->task),
					"PkConsole:list-create-filename",
					g_strdup (value),
					g_free);
		pk_task_get_packages_async (PK_TASK (ctx->task), ctx->filters, ctx->cancellable,
					    pk_console_progress_cb, ctx,
					    pk_console_finished_cb, ctx);
	} else {
		error = g_error_new (PK_CONSOLE_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     /* TRANSLATORS: The user tried to use an
				      * unsupported option on the command line */
				     _("Option '%s' is not supported"), mode);
	}

	/* do we wait for the method? */
	if (run_mainloop && error == NULL)
		g_main_loop_run (ctx->loop);

out:
	if (error != NULL) {
		/* TRANSLATORS: Generic failure of what they asked to do */
		g_print ("%s: %s\n", _("Command failed"), error->message);
		if (ctx->retval == EXIT_SUCCESS)
			ctx->retval = EXIT_FAILURE;
	}

	/* stop listening for polkit questions */
	pk_polkit_agent_close ();

	g_free (options_help);
	g_free (filter);
	g_free (summary);
	if (ctx != NULL) {
		retval_copy = ctx->retval;
		g_object_unref (ctx->progressbar);
		g_object_unref (ctx->control);
		g_object_unref (ctx->task);
		g_object_unref (ctx->cancellable);
		if (ctx->defered_status_id > 0)
			g_source_remove (ctx->defered_status_id);
		g_free (ctx);
	}
out_last:
	g_option_context_free (context);
	return retval_copy;
}

