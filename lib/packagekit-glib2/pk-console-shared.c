/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-package-id.h>

#include "pk-client-sync.h"
#include "pk-console-shared.h"

/**
 * pk_console_get_number:
 **/
guint
pk_console_get_number (const gchar *question, guint maxnum)
{
	gint answer = 0;
	gint retval;

	/* pretty print */
	g_print ("%s", question);

	do {
		char buffer[64];

		/* swallow the \n at end of line too */
		if (!fgets (buffer, 64, stdin))
			break;

		/* get a number */
		retval = sscanf(buffer, "%u", &answer);

		/* positive */
		if (answer > 0 && answer <= (gint) maxnum)
			break;
		g_print (_("Please enter a number from 1 to %i: "), maxnum);
	} while (TRUE);
	return answer;
}

/**
 * pk_console_getchar_unbuffered:
 **/
static gchar
pk_console_getchar_unbuffered (void)
{
	gchar c = '\0';
	struct termios org_opts, new_opts;
	gint res = 0;

	/* store old settings */
	res = tcgetattr (STDIN_FILENO, &org_opts);
	if (res != 0)
		g_warning ("failed to set terminal");

	/* set new terminal parms */
	memcpy (&new_opts, &org_opts, sizeof(new_opts));
	new_opts.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
	tcsetattr (STDIN_FILENO, TCSANOW, &new_opts);
	c = getc (stdin);

	/* restore old settings */
	res = tcsetattr (STDIN_FILENO, TCSANOW, &org_opts);
	if (res != 0)
		g_warning ("failed to set terminal");
	return c;
}

/**
 * pk_console_get_prompt:
 **/
gboolean
pk_console_get_prompt (const gchar *question, gboolean defaultyes)
{
	gchar answer;
	gboolean ret = FALSE;

	/* pretty print */
	g_print ("%s", question);
	if (defaultyes)
		g_print (" [Y/n] ");
	else
		g_print (" [N/y] ");

	do {
		/* get the unbuffered char */
		answer = pk_console_getchar_unbuffered ();

		/* positive */
		if (answer == 'y' || answer == 'Y') {
			ret = TRUE;
			break;
		}
		/* negative */
		if (answer == 'n' || answer == 'N')
			break;

		/* default choice */
		if (answer == '\n' && defaultyes) {
			ret = TRUE;
			break;
		}
		if (answer == '\n' && !defaultyes)
			break;
	} while (TRUE);
	return ret;
}

/**
 * pk_console_resolve_package:
 **/
gchar *
pk_console_resolve_package (PkClient *client, PkBitfield filter, const gchar *package_name, GError **error)
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

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package_name);
	if (valid)
		return g_strdup (package_name);

	/* split */
	tmp = g_strsplit (package_name, ",", -1);

	/* get the list of possibles */
	results = pk_client_resolve (client, filter, tmp, NULL, NULL, NULL, error);
	if (results == NULL)
		goto out;

	/* get the packages returned */
	array = pk_results_get_package_array (results);
	if (array == NULL) {
		g_set_error (error, 1, 0, "did not get package struct for %s", package_name);
		goto out;
	}

	/* nothing found */
	if (array->len == 0) {
		g_set_error (error, 1, 0, "could not find %s", package_name);
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

	/* TRANSLATORS: more than one package could be found that matched, to follow is a list of possible packages  */
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
gchar **
pk_console_resolve_packages (PkClient *client, PkBitfield filter, gchar **packages, GError **error)
{
	gchar **package_ids;
	guint i;
	guint len;

	/* get length */
	len = g_strv_length (packages);
	g_debug ("resolving %i packages", len);

	/* create output array*/
	package_ids = g_new0 (gchar *, len+1);

	/* resolve each package */
	for (i=0; i<len; i++) {
		package_ids[i] = pk_console_resolve_package (client, filter, packages[i], error);
		if (package_ids[i] == NULL) {
			/* destroy state */
			g_strfreev (package_ids);
			package_ids = NULL;
			break;
		}
	}
	return package_ids;
}

/**
 * pk_status_enum_to_localised_text:
 **/
const gchar *
pk_status_enum_to_localised_text (PkStatusEnum status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_STATUS_ENUM_UNKNOWN:
		/* TRANSLATORS: This is when the transaction status is not known */
		text = _("Unknown state");
		break;
	case PK_STATUS_ENUM_SETUP:
		/* TRANSLATORS: transaction state, the daemon is in the process of starting */
		text = _("Starting");
		break;
	case PK_STATUS_ENUM_WAIT:
		/* TRANSLATORS: transaction state, the transaction is waiting for another to complete */
		text = _("Waiting in queue");
		break;
	case PK_STATUS_ENUM_RUNNING:
		/* TRANSLATORS: transaction state, just started */
		text = _("Running");
		break;
	case PK_STATUS_ENUM_QUERY:
		/* TRANSLATORS: transaction state, is querying data */
		text = _("Querying");
		break;
	case PK_STATUS_ENUM_INFO:
		/* TRANSLATORS: transaction state, getting data from a server */
		text = _("Getting information");
		break;
	case PK_STATUS_ENUM_REMOVE:
		/* TRANSLATORS: transaction state, removing packages */
		text = _("Removing packages");
		break;
	case PK_STATUS_ENUM_DOWNLOAD:
		/* TRANSLATORS: transaction state, downloading package files */
		text = _("Downloading packages");
		break;
	case PK_STATUS_ENUM_INSTALL:
		/* TRANSLATORS: transaction state, installing packages */
		text = _("Installing packages");
		break;
	case PK_STATUS_ENUM_REFRESH_CACHE:
		/* TRANSLATORS: transaction state, refreshing internal lists */
		text = _("Refreshing software list");
		break;
	case PK_STATUS_ENUM_UPDATE:
		/* TRANSLATORS: transaction state, installing updates */
		text = _("Installing updates");
		break;
	case PK_STATUS_ENUM_CLEANUP:
		/* TRANSLATORS: transaction state, removing old packages, and cleaning config files */
		text = _("Cleaning up packages");
		break;
	case PK_STATUS_ENUM_OBSOLETE:
		/* TRANSLATORS: transaction state, obsoleting old packages */
		text = _("Obsoleting packages");
		break;
	case PK_STATUS_ENUM_DEP_RESOLVE:
		/* TRANSLATORS: transaction state, checking the transaction before we do it */
		text = _("Resolving dependencies");
		break;
	case PK_STATUS_ENUM_SIG_CHECK:
		/* TRANSLATORS: transaction state, checking if we have all the security keys for the operation */
		text = _("Checking signatures");
		break;
	case PK_STATUS_ENUM_ROLLBACK:
		/* TRANSLATORS: transaction state, when we return to a previous system state */
		text = _("Rolling back");
		break;
	case PK_STATUS_ENUM_TEST_COMMIT:
		/* TRANSLATORS: transaction state, when we're doing a test transaction */
		text = _("Testing changes");
		break;
	case PK_STATUS_ENUM_COMMIT:
		/* TRANSLATORS: transaction state, when we're writing to the system package database */
		text = _("Committing changes");
		break;
	case PK_STATUS_ENUM_REQUEST:
		/* TRANSLATORS: transaction state, requesting data from a server */
		text = _("Requesting data");
		break;
	case PK_STATUS_ENUM_FINISHED:
		/* TRANSLATORS: transaction state, all done! */
		text = _("Finished");
		break;
	case PK_STATUS_ENUM_CANCEL:
		/* TRANSLATORS: transaction state, in the process of cancelling */
		text = _("Cancelling");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading repository information");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading list of packages");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading file lists");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading lists of changes");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_GROUP:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading groups");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading update information");
		break;
	case PK_STATUS_ENUM_REPACKAGING:
		/* TRANSLATORS: transaction state, repackaging delta files */
		text = _("Repackaging files");
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
		/* TRANSLATORS: transaction state, loading databases */
		text = _("Loading cache");
		break;
	case PK_STATUS_ENUM_SCAN_APPLICATIONS:
		/* TRANSLATORS: transaction state, scanning for running processes */
		text = _("Scanning applications");
		break;
	case PK_STATUS_ENUM_GENERATE_PACKAGE_LIST:
		/* TRANSLATORS: transaction state, generating a list of packages installed on the system */
		text = _("Generating package lists");
		break;
	case PK_STATUS_ENUM_WAITING_FOR_LOCK:
		/* TRANSLATORS: transaction state, when we're waiting for the native tools to exit */
		text = _("Waiting for package manager lock");
		break;
	case PK_STATUS_ENUM_WAITING_FOR_AUTH:
		/* TRANSLATORS: transaction state, waiting for user to type in a password */
		text = _("Waiting for authentication");
		break;
	case PK_STATUS_ENUM_SCAN_PROCESS_LIST:
		/* TRANSLATORS: transaction state, we are updating the list of processes */
		text = _("Updating running applications");
		break;
	case PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES:
		/* TRANSLATORS: transaction state, we are checking executable files currently in use */
		text = _("Checking applications in use");
		break;
	case PK_STATUS_ENUM_CHECK_LIBRARIES:
		/* TRANSLATORS: transaction state, we are checking for libraries currently in use */
		text = _("Checking libraries in use");
		break;
	case PK_STATUS_ENUM_COPY_FILES:
		/* TRANSLATORS: transaction state, we are copying package files before or after the transaction */
		text = _("Copying files");
		break;
	default:
		g_warning ("status unrecognised: %s", pk_status_enum_to_string (status));
	}
	return text;
}

/**
 * pk_info_enum_to_localised_text:
 **/
static const gchar *
pk_info_enum_to_localised_text (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		/* TRANSLATORS: The type of update */
		text = _("Trivial");
		break;
	case PK_INFO_ENUM_NORMAL:
		/* TRANSLATORS: The type of update */
		text = _("Normal");
		break;
	case PK_INFO_ENUM_IMPORTANT:
		/* TRANSLATORS: The type of update */
		text = _("Important");
		break;
	case PK_INFO_ENUM_SECURITY:
		/* TRANSLATORS: The type of update */
		text = _("Security");
		break;
	case PK_INFO_ENUM_BUGFIX:
		/* TRANSLATORS: The type of update */
		text = _("Bug fix ");
		break;
	case PK_INFO_ENUM_ENHANCEMENT:
		/* TRANSLATORS: The type of update */
		text = _("Enhancement");
		break;
	case PK_INFO_ENUM_BLOCKED:
		/* TRANSLATORS: The type of update */
		text = _("Blocked");
		break;
	case PK_INFO_ENUM_INSTALLED:
	case PK_INFO_ENUM_COLLECTION_INSTALLED:
		/* TRANSLATORS: The state of a package */
		text = _("Installed");
		break;
	case PK_INFO_ENUM_AVAILABLE:
	case PK_INFO_ENUM_COLLECTION_AVAILABLE:
		/* TRANSLATORS: The state of a package, i.e. not installed */
		text = _("Available");
		break;
	default:
		g_warning ("info unrecognised: %s", pk_info_enum_to_string (info));
	}
	return text;
}

/**
 * pk_info_enum_to_localised_present:
 **/
const gchar *
pk_info_enum_to_localised_present (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_DOWNLOADING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Downloading");
		break;
	case PK_INFO_ENUM_UPDATING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Updating");
		break;
	case PK_INFO_ENUM_INSTALLING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Installing");
		break;
	case PK_INFO_ENUM_REMOVING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Removing");
		break;
	case PK_INFO_ENUM_CLEANUP:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Cleaning up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Obsoleting");
		break;
	case PK_INFO_ENUM_REINSTALLING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = _("Reinstalling");
		break;
	default:
		text = pk_info_enum_to_localised_text (info);
	}
	return text;
}

/**
 * pk_info_enum_to_localised_past:
 **/
const gchar *
pk_info_enum_to_localised_past (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_DOWNLOADING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Downloaded");
		break;
	case PK_INFO_ENUM_UPDATING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Updated");
		break;
	case PK_INFO_ENUM_INSTALLING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Installed");
		break;
	case PK_INFO_ENUM_REMOVING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Removed");
		break;
	case PK_INFO_ENUM_CLEANUP:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Cleaned up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Obsoleted");
		break;
	case PK_INFO_ENUM_REINSTALLING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = _("Reinstalled");
		break;
	default:
		text = pk_info_enum_to_localised_text (info);
	}
	return text;
}

/**
 * pk_role_enum_to_localised_present:
 **/
const gchar *
pk_role_enum_to_localised_present (PkRoleEnum role)
{
	const gchar *text = NULL;
	switch (role) {
	case PK_ROLE_ENUM_UNKNOWN:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Unknown role type");
		break;
	case PK_ROLE_ENUM_GET_DEPENDS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting dependencies");
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting update details");
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting details");
		break;
	case PK_ROLE_ENUM_GET_REQUIRES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting requires");
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting updates");
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Searching by details");
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Searching by file");
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Searching groups");
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Searching by name");
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Removing");
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Installing");
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Installing files");
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Refreshing cache");
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Updating packages");
		break;
	case PK_ROLE_ENUM_UPDATE_SYSTEM:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Updating system");
		break;
	case PK_ROLE_ENUM_CANCEL:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Canceling");
		break;
	case PK_ROLE_ENUM_ROLLBACK:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Rolling back");
		break;
	case PK_ROLE_ENUM_GET_REPO_LIST:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting repositories");
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Enabling repository");
		break;
	case PK_ROLE_ENUM_REPO_SET_DATA:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Setting data");
		break;
	case PK_ROLE_ENUM_RESOLVE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Resolving");
		break;
	case PK_ROLE_ENUM_GET_FILES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting file list");
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting provides");
		break;
	case PK_ROLE_ENUM_INSTALL_SIGNATURE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Installing signature");
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting packages");
		break;
	case PK_ROLE_ENUM_ACCEPT_EULA:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Accepting EULA");
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Downloading packages");
		break;
	case PK_ROLE_ENUM_GET_DISTRO_UPGRADES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting upgrades");
		break;
	case PK_ROLE_ENUM_GET_CATEGORIES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting categories");
		break;
	case PK_ROLE_ENUM_GET_OLD_TRANSACTIONS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Getting transactions");
		break;
	case PK_ROLE_ENUM_SIMULATE_INSTALL_FILES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Simulating install");
		break;
	case PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Simulating install");
		break;
	case PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Simulating remove");
		break;
	case PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = _("Simulating update");
		break;
	default:
		g_warning ("role unrecognised: %s", pk_role_enum_to_string (role));
	}
	return text;
}
