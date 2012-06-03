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
#include <errno.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-bitfield.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-package-id.h>

#include "pk-error.h"
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
		if (retval == 1 && answer > 0 && answer <= (gint) maxnum)
			break;
		g_print (_("Please enter a number from 1 to %i: "), maxnum);
	} while (TRUE);
	return answer;
}


/**
 * pk_readline_unbuffered:
 **/
static GString *
pk_readline_unbuffered (const gchar *prompt)
{
	const gchar *tty_name;
	FILE *tty;
	GString *str = NULL;
	struct termios ts, ots;

	tty_name = ctermid (NULL);
	if (tty_name == NULL) {
		g_warning ("Cannot get terminal: %s",
			   strerror (errno));
		goto out;
	}

	tty = fopen (tty_name, "r+");
	if (tty == NULL) {
		g_warning ("Error opening terminal for the process (`%s'): %s",
			   tty_name, strerror (errno));
		goto out;
	}

	fprintf (tty, "%s", prompt);
	fflush (tty);
	setbuf (tty, NULL);

	/* taken from polkitagenttextlistener.c */
	tcgetattr (fileno (tty), &ts);
	ots = ts;
	ts.c_lflag &= ~(ECHONL);
	tcsetattr (fileno (tty), TCSAFLUSH, &ts);

	str = g_string_new (NULL);
	while (TRUE) {
		gint c;
		c = getc (tty);
		if (c == '\n') {
			/* ok, done */
			break;
		} else if (c == EOF) {
			g_warning ("Got unexpected EOF.");
			break;
		} else {
			g_string_append_len (str, (const gchar *) &c, 1);
		}
	}
	tcsetattr (fileno (tty), TCSAFLUSH, &ots);
	putc ('\n', tty);

	fclose (tty);
out:
	return str;
}


/**
 * pk_console_get_prompt:
 **/
gboolean
pk_console_get_prompt (const gchar *question, gboolean defaultyes)
{
	gboolean ret = FALSE;
	gboolean valid = FALSE;
	gchar *prompt;
	GString *string;

	prompt = g_strdup_printf ("%s %s ",
				  question,
				  defaultyes ? "[Y/n]" : "[N/y]");
	while (!valid) {
		string = pk_readline_unbuffered (prompt);
		if (string == NULL)
			break;
		if (string->len == 0) {
			if (defaultyes) {
				valid = TRUE;
				ret = TRUE;
			} else {
				valid = TRUE;
				ret = FALSE;
			}
		}
		if (strcasecmp (string->str, "y") == 0 ||
		    strcasecmp (string->str, "yes") == 0) {
			valid = TRUE;
			ret = TRUE;
		}
		if (strcasecmp (string->str, "n") == 0 ||
		    strcasecmp (string->str, "no") == 0) {
			valid = TRUE;
			ret = FALSE;
		}
		g_string_free (string, TRUE);
	}
	g_free (prompt);
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
	PkError *error_code = NULL;

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

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_set_error (error, 1, 0, "\n%s", pk_error_get_details (error_code));
		goto out;
	}

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

