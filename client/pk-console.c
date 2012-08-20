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

static GMainLoop *loop = NULL;
static PkBitfield roles = 0;
static gboolean is_console = FALSE;
static gboolean nowait = FALSE;
static PkControl *control = NULL;
static PkTaskText *task = NULL;
static PkProgressBar *progressbar = NULL;
static GCancellable *cancellable = NULL;
static gint retval = EXIT_SUCCESS;

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
pk_console_package_cb (PkPackage *package, gpointer data)
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
	if (!is_console) {
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
pk_console_transaction_cb (PkTransactionPast *item, gpointer user_data)
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

	/* TRANSLATORS: these are packages touched by the transaction */
	lines = g_strsplit (data, "\n", -1);
	lines_len = g_strv_length (lines);
	if (lines_len > 0)
		g_print (" %s\n", _("Affected packages:"));
	else
		g_print (" %s\n", _("Affected packages: None"));
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
		/* TRANSLATORS: details about the update, any packages that this update updates */
		g_print (" %s: %s\n", _("Updates"), tmp);
		g_free (tmp);
	}
	if (obsoletes != NULL) {
		tmp = g_strjoinv (", ", obsoletes);
		/* TRANSLATORS: details about the update, any packages that this update obsoletes */
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
		/* TRANSLATORS: details about the update, if the package requires a restart */
		g_print (" %s: %s\n", _("Restart"), pk_restart_enum_to_string (restart));
	}
	if (update_text != NULL) {
		/* TRANSLATORS: details about the update, any description of the update */
		g_print (" %s: %s\n", _("Update text"), update_text);
	}
	if (changelog != NULL) {
		/* TRANSLATORS: details about the update, the changelog for the package */
		g_print (" %s: %s\n", _("Changes"), changelog);
	}
	if (state != PK_UPDATE_STATE_ENUM_UNKNOWN) {
		/* TRANSLATORS: details about the update, the ongoing state of the update */
		g_print (" %s: %s\n", _("State"), pk_update_state_enum_to_string (state));
	}
	if (issued != NULL) {
		/* TRANSLATORS: details about the update, date the update was issued */
		g_print (" %s: %s\n", _("Issued"), issued);
	}
	if (updated != NULL) {
		/* TRANSLATORS: details about the update, date the update was updated */
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
		/* TRANSLATORS: a package requires the system to be restarted due to a security update*/
		g_print ("%s %s\n", _("System restart (security) required by:"), package);
	} else if (restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: a package requires the session to be restarted due to a security update */
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
	g_print ("  size:        %lu bytes\n", (long unsigned int) size);
	g_print ("  url:         %s\n", url);

	g_free (package_id);
	g_free (license);
	g_free (description);
	g_free (url);
	g_free (package);
}

/**
 * pk_console_message_cb:
 **/
static void
pk_console_message_cb (PkMessage *item, gpointer data)
{
	gchar *details;
	PkMessageEnum type;

	/* get data */
	g_object_get (item,
		      "details", &details,
		      "type", &type,
		      NULL);

	/* TRANSLATORS: This a message (like a little note that may be of interest) from the transaction */
	g_print ("%s %s: %s\n", _("Message:"), pk_message_enum_to_string (type), details);
	g_free (details);
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

	/* role */
	if (type == PK_PROGRESS_TYPE_ROLE) {
		g_object_get (progress,
			      "role", &role,
			      NULL);
		if (role == PK_ROLE_ENUM_UNKNOWN)
			goto out;

		/* show new status on the bar */
		text = pk_role_enum_to_localised_present (role);
		if (!is_console) {
			/* TRANSLATORS: the role is the point of the transaction, e.g. update-packages */
			g_print ("%s:\t%s\n", _("Transaction"), text);
			goto out;
		}
		pk_progress_bar_start (progressbar, text);
	}

	/* package-id */
	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_object_get (progress,
			      "package-id", &package_id,
			      NULL);
		if (package_id == NULL)
			goto out;

		if (!is_console) {
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
		if (!is_console) {
			/* only print the 10's */
			if (percentage % 10 != 0)
				goto out;

			/* TRANSLATORS: the percentage complete of the transaction */
			g_print ("%s:\t%i\n", _("Percentage"), percentage);
			goto out;
		}
		pk_progress_bar_set_percentage (progressbar, percentage);
	}

	/* status */
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		if (status == PK_STATUS_ENUM_FINISHED)
			goto out;

		/* show new status on the bar */
		text = pk_status_enum_to_localised_text (status);
		if (!is_console) {
			/* TRANSLATORS: the status of the transaction (e.g. downloading) */
			g_print ("%s: \t%s\n", _("Status"), text);
			goto out;
		}
		pk_progress_bar_start (progressbar, text);
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
	PkError *error_code = NULL;
	PkResults *results;
	GError *error = NULL;
	GPtrArray *array;
	PkPackageSack *sack;
	PkRestartEnum restart;
	PkRoleEnum role;

	/* no more progress */
	if (is_console) {
		pk_progress_bar_end (progressbar);
	} else {
		/* TRANSLATORS: the results from the transaction */
		g_print ("%s\n", _("Results:"));
	}

	/* get the results */
	results = pk_task_generic_finish (PK_TASK(task), res, &error);
	if (results == NULL) {
		/* TRANSLATORS: we failed to get any results, which is pretty fatal in my book */
		g_print ("%s: %s\n", _("Fatal error"), error->message);
		g_error_free (error);
		retval = PK_EXIT_CODE_TRANSACTION_FAILED;
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
			/* TRANSLATORS: the user asked to update everything, but there is nothing that can be updated */
			g_print ("%s\n", _("There are no packages to update."));
		} else {
			/* TRANSLATORS: the transaction failed in a way we could not expect */
			g_print ("%s: %s, %s\n", _("The transaction failed"), pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		}

		/* special case */
		if (pk_error_get_code (error_code) == PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE)
			retval = PK_EXIT_CODE_NOTHING_USEFUL;
		goto out;
	}

	/* get the sack */
	sack = pk_results_get_package_sack (results);
	pk_package_sack_sort (sack, PK_PACKAGE_SACK_SORT_TYPE_NAME);
	array = pk_package_sack_get_array (sack);

	/* package */
	if (!is_console ||
	    (role != PK_ROLE_ENUM_INSTALL_PACKAGES &&
	     role != PK_ROLE_ENUM_UPDATE_PACKAGES &&
	     role != PK_ROLE_ENUM_REMOVE_PACKAGES)) {
		g_ptr_array_foreach (array, (GFunc) pk_console_package_cb, NULL);
	}

	/* special case */
	if (array->len == 0 &&
	    (role == PK_ROLE_ENUM_GET_UPDATES ||
	     role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		/* TRANSLATORS: print a message when there are no updates */
		g_print ("%s\n", _("There are no updates available at this time."));
		retval = PK_EXIT_CODE_NOTHING_USEFUL;
	}

	g_ptr_array_unref (array);
	g_object_unref (sack);

	/* transaction */
	array = pk_results_get_transaction_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_transaction_cb, NULL);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_OLD_TRANSACTIONS)
		retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* distro_upgrade */
	array = pk_results_get_distro_upgrade_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_distro_upgrade_cb, NULL);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES) {
		g_print ("%s\n", _("There are no upgrades available at this time."));
		retval = PK_EXIT_CODE_NOTHING_USEFUL;
	}

	g_ptr_array_unref (array);

	/* category */
	array = pk_results_get_category_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_category_cb, NULL);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_CATEGORIES)
		retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* update_detail */
	array = pk_results_get_update_detail_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_update_detail_cb, NULL);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* repo_detail */
	array = pk_results_get_repo_detail_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_repo_detail_cb, NULL);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_REPO_LIST)
		retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* require_restart */
	array = pk_results_get_require_restart_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_require_restart_cb, NULL);
	g_ptr_array_unref (array);

	/* details */
	array = pk_results_get_details_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_details_cb, NULL);

	/* special case */
	if (array->len == 0 && role == PK_ROLE_ENUM_GET_DETAILS)
		retval = PK_EXIT_CODE_NOTHING_USEFUL;

	g_ptr_array_unref (array);

	/* message */
	array = pk_results_get_message_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_message_cb, NULL);
	g_ptr_array_unref (array);

	/* don't print files if we are DownloadPackages */
	if (role != PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		array = pk_results_get_files_array (results);
		g_ptr_array_foreach (array, (GFunc) pk_console_files_cb, NULL);
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
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	g_main_loop_quit (loop);
}

/**
 * pk_console_install_packages:
 **/
static gboolean
pk_console_install_packages (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;
	guint i;

	/* test to see if we've been given files, not packages */
	for (i=0; packages[i] != NULL; i++) {
		ret = !g_file_test (packages[i], G_FILE_TEST_EXISTS);
		if (!ret) {
			/* TRANSLATORS: The user used 'pkcon install dave.rpm' rather than 'pkcon install-local dave.rpm' */
			*error = g_error_new (1, 0, _("Expected package name, actually got file. Try using 'pkcon install-local %s' instead."), packages[i]);
			goto out;
		}
	}

	package_ids = pk_console_resolve_packages (PK_CLIENT(task),
						   pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED,
									   PK_FILTER_ENUM_NEWEST,
									   -1),
						   packages,
						   &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find any available package: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		retval = PK_EXIT_CODE_FILE_NOT_FOUND;
		goto out;
	}

	/* do the async action */
	pk_task_install_packages_async (PK_TASK(task), package_ids, cancellable,
				        (PkProgressCallback) pk_console_progress_cb, NULL,
				        (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_remove_packages:
 **/
static gboolean
pk_console_remove_packages (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find the installed package: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_remove_packages_async (PK_TASK(task), package_ids, TRUE, FALSE, cancellable,
				       (PkProgressCallback) pk_console_progress_cb, NULL,
				       (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_download_packages:
 **/
static gboolean
pk_console_download_packages (gchar **packages, const gchar *directory, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find the package: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_download_packages_async (PK_TASK (task),package_ids, directory, cancellable,
				         (PkProgressCallback) pk_console_progress_cb, NULL,
				         (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_update_packages:
 **/
static gboolean
pk_console_update_packages (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find the package: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_update_packages_async (PK_TASK(task), package_ids, cancellable,
				       (PkProgressCallback) pk_console_progress_cb, NULL,
				       (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_update_system:
 **/
static gboolean
pk_console_update_system (PkBitfield filters, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	PkPackageSack *sack = NULL;
	PkResults *results;

	/* get the current updates */
	pk_bitfield_add (filters, PK_FILTER_ENUM_NEWEST);
	results = pk_task_get_updates_sync (PK_TASK (task),
					    filters,
					    cancellable,
					    (PkProgressCallback) pk_console_progress_cb, NULL,
					    error);
	if (results == NULL) {
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	sack = pk_results_get_package_sack (results);
	package_ids = pk_package_sack_get_ids (sack);
	pk_task_update_packages_async (PK_TASK(task), package_ids, cancellable,
				       (PkProgressCallback) pk_console_progress_cb, NULL,
				       (GAsyncReadyCallback) pk_console_finished_cb, NULL);
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
pk_console_get_requires (PkBitfield filters, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find all the packages: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_requires_async (PK_TASK (task),filters, package_ids, TRUE, cancellable,
				    (PkProgressCallback) pk_console_progress_cb, NULL,
				    (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_depends:
 **/
static gboolean
pk_console_get_depends (PkBitfield filters, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the dependencies for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find all the packages: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_depends_async (PK_TASK (task),filters, package_ids, FALSE, cancellable,
				   (PkProgressCallback) pk_console_progress_cb, NULL,
				   (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_details:
 **/
static gboolean
pk_console_get_details (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: There was an error getting the details about the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find all the packages: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_details_async (PK_TASK (task),package_ids, cancellable,
				   (PkProgressCallback) pk_console_progress_cb, NULL,
				   (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_files:
 **/
static gboolean
pk_console_get_files (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: The package name was not found in any software sources. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find all the packages: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_files_async (PK_TASK (task),package_ids, cancellable,
				 (PkProgressCallback) pk_console_progress_cb, NULL,
				 (GAsyncReadyCallback) pk_console_finished_cb, NULL);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_get_update_detail
 **/
static gboolean
pk_console_get_update_detail (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids = NULL;
	GError *error_local = NULL;

	package_ids = pk_console_resolve_packages (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), packages, &error_local);
	if (package_ids == NULL) {
		/* TRANSLATORS: The package name was not found in any software sources. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find all the packages: %s"), error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* do the async action */
	pk_task_get_update_detail_async (PK_TASK (task),package_ids, cancellable,
					 (PkProgressCallback) pk_console_progress_cb, NULL,
					 (GAsyncReadyCallback) pk_console_finished_cb, NULL);
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
	gboolean connected;

	/* if the daemon crashed, don't hang around */
	g_object_get (control_,
		      "connected", &connected,
		      NULL);
	if (!connected) {
		/* TRANSLATORS: This is when the daemon crashed, and we are up shit creek without a paddle */
		g_print ("%s\n", _("The daemon crashed mid-transaction!"));
		_exit (2);
	}
}

/**
 * pk_console_sigint_cb:
 **/
static void
pk_console_sigint_cb (int sig)
{
	g_debug ("Handling SIGINT");

	/* restore default ASAP, as the cancels might hang */
	signal (SIGINT, SIG_DFL);

	/* cancel any tasks still running */
	g_cancellable_cancel (cancellable);

	/* kill ourselves */
	g_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/**
 * pk_console_get_summary:
 **/
static gchar *
pk_console_get_summary (void)
{
	GString *string;
	string = g_string_new ("");

	/* TRANSLATORS: This is the header to the --help menu */
	g_string_append_printf (string, "%s\n\n%s\n", _("PackageKit Console Interface"),
				/* these are commands we can use with pkcon */
				_("Subcommands:"));

	/* always */
	g_string_append_printf (string, "  %s\n", "get-roles");
	g_string_append_printf (string, "  %s\n", "get-groups");
	g_string_append_printf (string, "  %s\n", "get-filters");
	g_string_append_printf (string, "  %s\n", "get-transactions");
	g_string_append_printf (string, "  %s\n", "get-time");

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_NAME) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_DETAILS) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE))
		g_string_append_printf (string, "  %s\n", "search [name|details|group|file] [data]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_PACKAGES))
		g_string_append_printf (string, "  %s\n", "install [packages]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_FILES))
		g_string_append_printf (string, "  %s\n", "install-local [files]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES))
		g_string_append_printf (string, "  %s\n", "download [directory] [packages]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_SIGNATURE))
		g_string_append_printf (string, "  %s\n", "install-sig [type] [key_id] [package_id]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REMOVE_PACKAGES))
		g_string_append_printf (string, "  %s\n", "remove [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES))
		g_string_append_printf (string, "  %s\n", "update <package>");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REFRESH_CACHE))
		g_string_append_printf (string, "  %s\n", "refresh [--force]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_RESOLVE))
		g_string_append_printf (string, "  %s\n", "resolve [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATES))
		g_string_append_printf (string, "  %s\n", "get-updates");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DEPENDS))
		g_string_append_printf (string, "  %s\n", "get-depends [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REQUIRES))
		g_string_append_printf (string, "  %s\n", "get-requires [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DETAILS))
		g_string_append_printf (string, "  %s\n", "get-details [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES))
		g_string_append_printf (string, "  %s\n", "get-distro-upgrades");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_FILES))
		g_string_append_printf (string, "  %s\n", "get-files [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL))
		g_string_append_printf (string, "  %s\n", "get-update-detail [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_PACKAGES))
		g_string_append_printf (string, "  %s\n", "get-packages");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST))
		g_string_append_printf (string, "  %s\n", "repo-list");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_ENABLE)) {
		g_string_append_printf (string, "  %s\n", "repo-enable [repo_id]");
		g_string_append_printf (string, "  %s\n", "repo-disable [repo_id]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_SET_DATA))
		g_string_append_printf (string, "  %s\n", "repo-set-data [repo_id] [parameter] [value];");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_WHAT_PROVIDES))
		g_string_append_printf (string, "  %s\n", "what-provides [search]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_ACCEPT_EULA))
		g_string_append_printf (string, "  %s\n", "accept-eula [eula-id]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_CATEGORIES))
		g_string_append_printf (string, "  %s\n", "get-categories");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPAIR_SYSTEM))
		g_string_append_printf (string, "  %s\n", "repair");
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
//	PkControl *control = PK_CONTROL(object);

	/* get the results */
	time_ms = pk_control_get_time_since_action_finish (control, res, &error);
	if (time_ms == 0) {
		/* TRANSLATORS: we keep a database updated with the time that an action was last executed */
		g_print ("%s: %s\n", _("Failed to get the time since this action was last completed"), error->message);
		g_error_free (error);
		goto out;
	}
	g_print ("time is %is\n", time_ms);
out:
	g_main_loop_quit (loop);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	GError *error = NULL;
	GError *error_local = NULL;
	gboolean background = FALSE;
	gboolean noninteractive = FALSE;
	gboolean only_download = FALSE;
	guint cache_age = 0;
	gboolean plain = FALSE;
	gboolean program_version = FALSE;
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
	PkBitfield filters = 0;

	const GOptionEntry options[] = {
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			/* TRANSLATORS: command line argument, just show the version string */
			_("Show the program version and exit"), NULL},
		{ "filter", '\0', 0, G_OPTION_ARG_STRING, &filter,
			/* TRANSLATORS: command line argument, use a filter to narrow down results */
			_("Set the filter, e.g. installed"), NULL},
		{ "nowait", 'n', 0, G_OPTION_ARG_NONE, &nowait,
			/* TRANSLATORS: command line argument, work asynchronously */
			_("Exit without waiting for actions to complete"), NULL},
		{ "noninteractive", 'y', 0, G_OPTION_ARG_NONE, &noninteractive,
			/* command line argument, do we ask questions */
			_("Install the packages without asking for confirmation"), NULL },
		{ "only-download", 'y', 0, G_OPTION_ARG_NONE, &only_download,
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

#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 31)
	if (! g_thread_supported ())
		g_thread_init (NULL);
#endif
	g_type_init ();

	/* do stuff on ctrl-c */
	signal (SIGINT, pk_console_sigint_cb);

	progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (progressbar, 25);
	pk_progress_bar_set_padding (progressbar, 30);

	cancellable = g_cancellable_new ();
	context = g_option_context_new ("PackageKit Console Program");
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, pk_debug_get_option_group ());
	ret = g_option_context_parse (context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to contact the daemon */
		g_print ("%s: %s\n", _("Failed to parse command line"), error->message);
		g_error_free (error);
		retval = PK_EXIT_CODE_SYNTAX_INVALID;
		goto out_last;
	}

	/* we need the roles early, as we only show the user only what they can do */
	control = pk_control_new ();
	ret = pk_control_get_properties (control, NULL, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to contact the daemon */
		g_print ("%s: %s\n", _("Failed to contact PackageKit"), error->message);
		g_error_free (error);
		retval = PK_EXIT_CODE_CANNOT_SETUP;
		goto out_last;
	}

	/* get data */
	g_object_get (control,
		      "roles", &roles,
		      NULL);

	/* set the summary text based on the available roles */
	summary = pk_console_get_summary ();
	g_option_context_set_summary (context, summary) ;
	options_help = g_option_context_get_help (context, TRUE, NULL);

	/* check if we are on console */
	if (!plain && isatty (fileno (stdout)) == 1)
		is_console = TRUE;

	if (program_version) {
		g_print (VERSION "\n");
		goto out_last;
	}

	if (argc < 2) {
		g_print ("%s", options_help);
		retval = PK_EXIT_CODE_SYNTAX_INVALID;
		goto out_last;
	}

	loop = g_main_loop_new (NULL, FALSE);

	/* watch when the daemon aborts */
	g_signal_connect (control, "notify::connected",
			  G_CALLBACK (pk_console_notify_connected_cb), loop);

	/* create transactions */
	task = pk_task_text_new ();
	g_object_set (task,
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
		ret = pk_control_set_proxy (control, http_proxy, ftp_proxy, NULL, &error_local);
		if (!ret) {
			/* TRANSLATORS: The user specified an incorrect filter */
			error = g_error_new (1, 0, "%s: %s", _("The proxy could not be set"), error_local->message);
			g_error_free (error_local);
			retval = PK_EXIT_CODE_CANNOT_SETUP;
			goto out;
		}
	}

	/* check filter */
	if (filter != NULL) {
		filters = pk_filter_bitfield_from_string (filter);
		if (filters == 0) {
			/* TRANSLATORS: The user specified an incorrect filter */
			error = g_error_new (1, 0, "%s: %s", _("The filter specified was invalid"), filter);
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
	}
	g_debug ("filter=%s, filters=%" PK_BITFIELD_FORMAT, filter, filters);

	mode = argv[1];
	if (argc > 2)
		value = argv[2];
	if (argc > 3)
		details = argv[3];
	if (argc > 4)
		parameter = argv[4];

	/* parse the big list */
	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: a search type can be name, details, file, etc */
			error = g_error_new (1, 0, "%s", _("A search type is required, e.g. name"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;

		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				/* TRANSLATORS: the user needs to provide a search term */
				error = g_error_new (1, 0, "%s", _("A search term is required"));
				retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			/* fire off an async request */
			pk_task_search_names_async (PK_TASK (task),filters, argv+3, cancellable,
						    (PkProgressCallback) pk_console_progress_cb, NULL,
						    (GAsyncReadyCallback) pk_console_finished_cb, NULL);

		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				/* TRANSLATORS: the user needs to provide a search term */
				error = g_error_new (1, 0, "%s", _("A search term is required"));
				retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			/* fire off an async request */
			pk_task_search_details_async (PK_TASK (task),filters, argv+3, cancellable,
						      (PkProgressCallback) pk_console_progress_cb, NULL,
						      (GAsyncReadyCallback) pk_console_finished_cb, NULL);

		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				/* TRANSLATORS: the user needs to provide a search term */
				error = g_error_new (1, 0, "%s", _("A search term is required"));
				retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			/* fire off an async request */
			pk_task_search_groups_async (PK_TASK (task),filters, argv+3, cancellable,
						     (PkProgressCallback) pk_console_progress_cb, NULL,
						     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				/* TRANSLATORS: the user needs to provide a search term */
				error = g_error_new (1, 0, "%s", _("A search term is required"));
				retval = PK_EXIT_CODE_SYNTAX_INVALID;
				goto out;
			}
			/* fire off an async request */
			pk_task_search_files_async (PK_TASK (task),filters, argv+3, cancellable,
						    (PkProgressCallback) pk_console_progress_cb, NULL,
						    (GAsyncReadyCallback) pk_console_finished_cb, NULL);
		} else {
			/* TRANSLATORS: the search type was provided, but invalid */
			error = g_error_new (1, 0, "%s", _("Invalid search type"));
		}

	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: the user did not specify what they wanted to install */
			error = g_error_new (1, 0, "%s", _("A package name to install is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_install_packages (argv+2, &error);

	} else if (strcmp (mode, "install-local") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: the user did not specify what they wanted to install */
			error = g_error_new (1, 0, "%s", _("A filename to install is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_install_files_async (PK_TASK(task), argv+2, cancellable,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "install-sig") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			/* TRANSLATORS: geeky error, 99.9999% of users won't see this */
			error = g_error_new (1, 0, "%s", _("A type, key_id and package_id are required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_install_signature_async (PK_CLIENT(task), PK_SIGTYPE_ENUM_GPG, details, parameter, cancellable,
						   (PkProgressCallback) pk_console_progress_cb, NULL,
						   (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: the user did not specify what they wanted to remove */
			error = g_error_new (1, 0, "%s", _("A package name to remove is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_remove_packages (argv+2, &error);

	} else if (strcmp (mode, "download") == 0) {
		if (value == NULL || details == NULL) {
			/* TRANSLATORS: the user did not specify anything about what to download or where */
			error = g_error_new (1, 0, "%s", _("A destination directory and the package names to download are required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = g_file_test (value, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR);
		if (!ret) {
			/* TRANSLATORS: the directory does not exist, so we can't continue */
			error = g_error_new (1, 0, "%s: %s", _("Directory not found"), value);
			retval = PK_EXIT_CODE_FILE_NOT_FOUND;
			goto out;
		}
		nowait = !pk_console_download_packages (argv+3, value, &error);

	} else if (strcmp (mode, "accept-eula") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: geeky error, 99.9999% of users won't see this */
			error = g_error_new (1, 0, "%s", _("A licence identifier (eula-id) is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_accept_eula_async (PK_CLIENT(task), value, cancellable,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			/* do the system update */
			nowait = !pk_console_update_system (filters, &error);
		} else {
			nowait = !pk_console_update_packages (argv+2, &error);
		}

	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not specify a package name */
			error = g_error_new (1, 0, "%s", _("A package name to resolve is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_resolve_async (PK_TASK (task),filters, argv+2, cancellable,
				       (PkProgressCallback) pk_console_progress_cb, NULL,
				       (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-enable") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not specify a repository (software source) name */
			error = g_error_new (1, 0, "%s", _("A repository name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_repo_enable_async (PK_TASK (task),value, TRUE, cancellable,
					   (PkProgressCallback) pk_console_progress_cb, NULL,
					   (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-disable") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not specify a repository (software source) name */
			error = g_error_new (1, 0, "%s", _("A repository name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_repo_enable_async (PK_TASK (task),value, FALSE, cancellable,
					   (PkProgressCallback) pk_console_progress_cb, NULL,
					   (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-set-data") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			/* TRANSLATORS: The user didn't provide any data */
			error = g_error_new (1, 0, "%s", _("A repo name, parameter and value are required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_repo_set_data_async (PK_CLIENT(task), value, details, parameter, cancellable,
					       (PkProgressCallback) pk_console_progress_cb, NULL,
					       (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-list") == 0) {
		pk_task_get_repo_list_async (PK_TASK (task),filters, cancellable,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-time") == 0) {
		PkRoleEnum role;
		if (value == NULL) {
			/* TRANSLATORS: The user didn't specify what action to use */
			error = g_error_new (1, 0, "%s", _("An action, e.g. 'update-packages' is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		role = pk_role_enum_from_string (value);
		if (role == PK_ROLE_ENUM_UNKNOWN) {
			/* TRANSLATORS: The user specified an invalid action */
			error = g_error_new (1, 0, "%s", _("A correct role is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_control_get_time_since_action_async (control, role, cancellable,
							(GAsyncReadyCallback) pk_console_get_time_since_action_cb, NULL);

	} else if (strcmp (mode, "get-depends") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_get_depends (filters, argv+2, &error);

	} else if (strcmp (mode, "get-distro-upgrades") == 0) {
		pk_client_get_distro_upgrades_async (PK_CLIENT(task), cancellable,
						     (PkProgressCallback) pk_console_progress_cb, NULL,
						     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-update-detail") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_get_update_detail (argv+2, &error);

	} else if (strcmp (mode, "get-requires") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_get_requires (filters, argv+2, &error);

	} else if (strcmp (mode, "what-provides") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: each package "provides" certain things, e.g. mime(gstreamer-decoder-mp3), the user didn't specify it */
			error = g_error_new (1, 0, "%s", _("A package provide string is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_task_what_provides_async (PK_TASK (task),filters, PK_PROVIDES_ENUM_ANY, argv+2, cancellable,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-details") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_get_details (argv+2, &error);

	} else if (strcmp (mode, "get-files") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		nowait = !pk_console_get_files (argv+2, &error);

	} else if (strcmp (mode, "get-updates") == 0) {
		pk_task_get_updates_async (PK_TASK (task),filters, cancellable,
					   (PkProgressCallback) pk_console_progress_cb, NULL,
					   (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-categories") == 0) {
		pk_task_get_categories_async (PK_TASK (task),cancellable,
					      (PkProgressCallback) pk_console_progress_cb, NULL,
					      (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-packages") == 0) {
		pk_task_get_packages_async (PK_TASK (task),filters, cancellable,
					    (PkProgressCallback) pk_console_progress_cb, NULL,
					    (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "upgrade-system") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a distro name */
			error = g_error_new (1, 0, "%s", _("A distribution name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		if (details == NULL) {
			/* TRANSLATORS: The user did not provide an upgrade type */
			error = g_error_new (1, 0, "%s", _("An upgrade type is required, e.g. 'minimal', 'default' or 'complete'"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_upgrade_system_async (PK_CLIENT (task), value,
						pk_upgrade_kind_enum_from_string (details),
						cancellable,
						(PkProgressCallback) pk_console_progress_cb, NULL,
						(GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-roles") == 0) {
		text = pk_role_bitfield_to_string (roles);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		nowait = TRUE;

	} else if (strcmp (mode, "get-filters") == 0) {
		g_object_get (control,
			      "filters", &filters,
			      NULL);
		text = pk_filter_bitfield_to_string (filters);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		nowait = TRUE;

	} else if (strcmp (mode, "get-groups") == 0) {
		g_object_get (control,
			      "groups", &groups,
			      NULL);
		text = pk_group_bitfield_to_string (groups);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		nowait = TRUE;

	} else if (strcmp (mode, "get-transactions") == 0) {
		pk_client_get_old_transactions_async (PK_CLIENT(task), 10, cancellable,
						      (PkProgressCallback) pk_console_progress_cb, NULL,
						      (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "refresh") == 0) {
		gboolean force = value && !strcmp (value, "--force");
		pk_task_refresh_cache_async (PK_TASK (task), force, cancellable,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repair") == 0) {
		pk_task_repair_system_async (PK_TASK(task), cancellable,
		                             (PkProgressCallback) pk_console_progress_cb, NULL,
		                             (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else {
		/* TRANSLATORS: The user tried to use an unsupported option on the command line */
		error = g_error_new (1, 0, _("Option '%s' is not supported"), mode);
	}

	/* do we wait for the method? */
	if (!nowait && error == NULL)
		g_main_loop_run (loop);

out:
	if (error != NULL) {
		/* TRANSLATORS: Generic failure of what they asked to do */
		g_print ("%s: %s\n", _("Command failed"), error->message);
		if (retval == EXIT_SUCCESS)
			retval = EXIT_FAILURE;
	}

	g_free (options_help);
	g_free (filter);
	g_free (summary);
	g_object_unref (progressbar);
	g_object_unref (control);
	g_object_unref (task);
	g_object_unref (cancellable);
out_last:
	g_option_context_free (context);
	return retval;
}

