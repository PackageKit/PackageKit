/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-package-id.h>
#include <pk-enum-list.h>
#include <pk-common.h>
#include <pk-connection.h>

#define PROGRESS_BAR_PADDING 22
#define MINIMUM_COLUMNS (PROGRESS_BAR_PADDING + 5)

static GMainLoop *loop = NULL;
static PkEnumList *role_list = NULL;
static gboolean is_console = FALSE;
static gboolean has_output = FALSE;
static gboolean printed_bar = FALSE;
static guint timer_id = 0;
static PkClient *client = NULL;
static PkClient *client_task = NULL;

typedef struct {
	gint position;
	gboolean move_forward;
} PulseState;

/**
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id, const gchar *summary, gpointer data)
{
	PkPackageId *ident;
	PkPackageId *spacing;
	gchar *info_text;
	guint extra = 0;

	/* if on console, clear the progress bar line */
	if (is_console && printed_bar && has_output == FALSE) {
		g_print ("\r");
	}

	/* pass this out */
	info_text = pk_strpad (pk_info_enum_to_text (info), 12);

	/* split */
	ident = pk_package_id_new_from_string (package_id);
	if (ident == NULL) {
		pk_warning (_("Could not get valid ident from %s"), package_id);
		return;
	}

	/* these numbers are guesses */
	extra = 0;
	spacing = pk_package_id_new ();
	spacing->name = pk_strpad_extra (ident->name, 20, &extra);
	spacing->arch = pk_strpad_extra (ident->arch, 7, &extra);
	spacing->version = pk_strpad_extra (ident->version, 15, &extra);
	spacing->data = pk_strpad_extra (ident->data, 12, &extra);

	/* pretty print */
	g_print ("%s %s %s %s %s %s\n", info_text, spacing->name,
		 spacing->arch, spacing->version, spacing->data, summary);

	/* free all the data */
	g_free (info_text);
	pk_package_id_free (ident);
	pk_package_id_free (spacing);

	/* don't do the percentage bar from now on */
	has_output = TRUE;
}

/**
 * pk_console_transaction_cb:
 **/
static void
pk_console_transaction_cb (PkClient *client, const gchar *tid, const gchar *timespec,
			   gboolean succeeded, PkRoleEnum role, guint duration,
			   const gchar *data, gpointer user_data)
{
	const gchar *role_text;
	role_text = pk_role_enum_to_text (role);
	g_print ("Transaction  : %s\n", tid);
	g_print (" timespec    : %s\n", timespec);
	g_print (" succeeded   : %i\n", succeeded);
	g_print (" role        : %s\n", role_text);
	g_print (" duration    : %i (seconds)\n", duration);
	g_print (" data        : %s\n", data);
}

/**
 * pk_console_update_detail_cb:
 **/
static void
pk_console_update_detail_cb (PkClient *client, const gchar *package_id,
			     const gchar *updates, const gchar *obsoletes,
			     const gchar *vendor_url, const gchar *bugzilla_url,
			     const gchar *cve_url, PkRestartEnum restart,
			     const gchar *update_text, gpointer data)
{
	g_print (_("Update detail\n"));
	g_print ("  package:    '%s'\n", package_id);
	if (pk_strzero (updates) == FALSE) {
		g_print ("  updates:    '%s'\n", updates);
	}
	if (pk_strzero (obsoletes) == FALSE) {
		g_print ("  obsoletes:  '%s'\n", obsoletes);
	}
	if (pk_strzero (vendor_url) == FALSE) {
		g_print ("  vendor URL: '%s'\n", vendor_url);
	}
	if (pk_strzero (bugzilla_url) == FALSE) {
		g_print ("  bug URL:    '%s'\n", bugzilla_url);
	}
	if (pk_strzero (cve_url) == FALSE) {
		g_print ("  cve URL:    '%s'\n", cve_url);
	}
	if (restart != PK_RESTART_ENUM_NONE) {
		g_print ("  restart:    '%s'\n", pk_restart_enum_to_text (restart));
	}
	if (pk_strzero (update_text) == FALSE) {
		g_print ("  update_text:'%s'\n", update_text);
	}
}

/**
 * pk_console_repo_detail_cb:
 **/
static void
pk_console_repo_detail_cb (PkClient *client, const gchar *repo_id,
			   const gchar *description, gboolean enabled, gpointer data)
{
	gchar *repo;
	repo = pk_strpad (repo_id, 28);
	if (enabled) {
		g_print ("  enabled   %s %s\n", repo, description);
	} else {
		g_print ("  disabled  %s %s\n", repo, description);
	}
	g_free (repo);
}

/**
 * pk_console_get_terminal_columns:
 **/
static guint
pk_console_get_terminal_columns (void)
{
	struct winsize ws;

	ioctl (1, TIOCGWINSZ, &ws);
	if (ws.ws_col < MINIMUM_COLUMNS) {
		return MINIMUM_COLUMNS;
	}

	return ws.ws_col;
}

/**
 * pk_console_draw_progress_bar:
 **/
static void
pk_console_draw_progress_bar (guint percentage, guint remaining_time)
{
	guint i;
	guint progress_bar_size = pk_console_get_terminal_columns () - PROGRESS_BAR_PADDING;
	guint progress = (gint) (progress_bar_size * (gfloat) (percentage) / 100);
	guint remaining = progress_bar_size - progress;

	/* have we already been spinning? */
	if (timer_id != 0) {
		g_source_remove (timer_id);
		timer_id = 0;
	}

	/* we need to do an extra line */
	printed_bar = TRUE;

	g_print ("\r    [");
	for (i = 0; i < progress; i++) {
		g_print ("=");
	}
	for (i = 0; i < remaining; i++) {
		g_print (".");
	}
	g_print ("]  %3i%%", percentage);
	if (remaining_time != 0) {
		if (remaining_time > 60) {
			guint remaining_minutes = remaining_time / 60;
			if (remaining_minutes > 60) {
				guint remaining_hours = remaining_time / 3600;
				g_print (" (%2ih eta)", remaining_hours);
			} else {
				g_print (" (%2im eta)", remaining_minutes);
			}
		} else {
			g_print (" (%2is eta)", remaining_time);
		}
	} else {
		g_print ("          ");
	}
	if (percentage == 100) {
		g_print ("\n");
	}
}

/**
 * pk_console_pulse_bar:
 **/
static gboolean
pk_console_pulse_bar (PulseState *pulse_state)
{
	guint i;
	guint progress_bar_size = pk_console_get_terminal_columns () - PROGRESS_BAR_PADDING;
	gchar *padding;

	/* don't spin if we have had output */
	if (has_output) {
		return FALSE;
	}

	/* we need to do an extra line */
	printed_bar = TRUE;

	/* the clever pulse code */
	printf("\r    [");
	for (i = 0; i < pulse_state->position - 1; i++) {
		g_print (".");
	}
	printf("===");
	for (i = pulse_state->position; i < progress_bar_size - 2; i++) {
		g_print (".");
	}
	g_print ("]");

	if (pulse_state->move_forward) {
		if (pulse_state->position == progress_bar_size - 2) {
			pulse_state->move_forward = FALSE;
			pulse_state->position--;
		} else {
			pulse_state->position++;
		}
	} else if (pulse_state->move_forward == FALSE) {
		if (pulse_state->position == 1) {
			pulse_state->move_forward = TRUE;
			pulse_state->position++;
		} else {
			pulse_state->position--;
		}
	}

	/* Move the cursor off the screen. */
	padding = g_strnfill (PROGRESS_BAR_PADDING - 6, ' ');
	g_print ("%s", padding);
	g_free (padding);

	return TRUE;
}

/**
 * pk_console_draw_progress_bar:
 **/
static void
pk_console_draw_pulse_bar (void)
{
	static PulseState pulse_state;

	/* have we already got zero percent? */
	if (timer_id != 0) {
		return;
	}
	has_output = FALSE;
	if (is_console) {
		pulse_state.position = 1;
		pulse_state.move_forward = TRUE;
		timer_id = g_timeout_add (40, (GSourceFunc) pk_console_pulse_bar, &pulse_state);
	}
}

/**
 * pk_console_progress_changed_cb:
 **/
static void
pk_console_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, gpointer data)
{
	if (is_console) {
		if (percentage == PK_CLIENT_PERCENTAGE_INVALID) {
			pk_console_draw_pulse_bar ();
		} else {
			pk_console_draw_progress_bar (percentage, remaining);
		}
	} else {
		g_print ("%i%%\n", percentage);
	}
}

static const gchar *summary =
	"PackageKit Console Interface\n"
	"\n"
	"Subcommands:\n"
	"  search name|details|group|file data\n"
	"  install <package_id>\n"
	"  install-file <file>\n"
	"  remove <package_id>\n"
	"  update <package_id>\n"
	"  refresh\n"
	"  resolve\n"
	"  force-refresh\n"
	"  update-system\n"
	"  get updates\n"
	"  get depends <package_id>\n"
	"  get requires <package_id>\n"
	"  what-provides <search>\n"
	"  get description <package_id>\n"
	"  get files <package_id>\n"
	"  get updatedetail <package_id>\n"
	"  get actions\n"
	"  get groups\n"
	"  get filters\n"
	"  get transactions\n"
	"  get repos\n"
	"  enable-repo <repo_id>\n"
	"  disable-repo <repo_id>\n"
	"  set-repo-data <repo_id> <parameter> <value>\n"
	"\n"
	"  package_id is typically gimp;2:2.4.0-0.rc1.1.fc8;i386;development";

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	PkRoleEnum role;
	gchar *blanking;
	const gchar *role_text;
	gfloat time;
	PkRestartEnum restart;

	/* cancel the spinning */
	if (timer_id != 0) {
		g_source_remove (timer_id);
	}

	/* if on console, clear the progress bar line */
	if (is_console && printed_bar && has_output == FALSE) {
		g_print ("\r");
		blanking = g_strnfill (pk_console_get_terminal_columns (), ' ');
		g_print ("%s", blanking);
		g_free (blanking);
		g_print ("\r");
	}

	pk_client_get_role (client, &role, NULL, NULL);
	role_text = pk_role_enum_to_text (role);
	time = (gfloat) runtime / 1000.0;
	g_print ("%s runtime was %.1f seconds\n", role_text, time);

	/* is there any restart to notify the user? */
	restart = pk_client_get_require_restart (client);
	if (restart != PK_RESTART_ENUM_NONE) {
		g_print (_("Requires restart: %s\n"), pk_restart_enum_to_text (restart));
	}
}

/**
 * pk_console_get_number:
 **/
static guint
pk_console_get_number (const gchar *question, guint maxnum)
{
	gint answer = 0;
	gint retval;

	/* pretty print */
	g_print ("%s", question);

	do {
		/* get a number */
		retval = scanf("%u", &answer);

		/* positive */
		if (retval == 1 && answer > 0 && answer <= maxnum) {
			return answer;
		}
		g_print (_("Please enter a number from 1 to %i: "), maxnum);
	} while (TRUE);

	/* keep GCC happy */
	return 0;
}

/**
 * pk_console_perhaps_resolve:
 **/
static gchar *
pk_console_perhaps_resolve (PkClient *client, PkFilterEnum filter, const gchar *package, GError **error)
{
	gboolean ret;
	gboolean valid;
	const gchar *filter_text;
	guint i;
	guint length;
	PkPackageItem *item;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid) {
		return g_strdup (package);
	}

	/* we need to resolve it */
	filter_text = pk_filter_enum_to_text (filter);
	ret = pk_client_resolve (client_task, filter_text, package, error);
	if (ret == FALSE) {
		pk_warning (_("Resolve is not supported in this backend"));
		return NULL;
	}

	/* get length of items found */
	length = pk_client_package_buffer_get_size (client_task);

	/* didn't resolve to anything, try to get a provide */
	if (length == 0) {
		ret = pk_client_reset (client_task, error);
		if (ret == FALSE) {
			pk_warning ("failed to reset client task");
			return NULL;
		}
		ret = pk_client_what_provides (client_task, filter_text, PK_PROVIDES_ENUM_ANY, package, error);
		if (ret == FALSE) {
			pk_warning (_("WhatProvides is not supported in this backend"));
			return NULL;
		}
	}

	/* get length of items found again (we might have has success) */
	length = pk_client_package_buffer_get_size (client_task);
	if (length == 0) {
		pk_warning (_("Could not find a package match"));
		return NULL;
	}

	/* only found one, great! */
	if (length == 1) {
		item = pk_client_package_buffer_get_item (client_task, 0);
		return g_strdup (item->package_id);
	}

	/* else list the options if multiple matches found */
	g_print (_("There are multiple matches\n"));
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client_task, i);
		g_print ("%i. %s\n", i+1, item->package_id);
	}

	/* find out what package the user wants to use */
	i = pk_console_get_number (_("Please enter the package number: "), length);
	item = pk_client_package_buffer_get_item (client_task, i-1);
	pk_debug ("package_id = %s", item->package_id);
	return g_strdup (item->package_id);
}

/**
 * pk_console_install_package:
 **/
static gboolean
pk_console_install_package (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_NOT_INSTALLED, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to install, or package already installed\n"));
		return FALSE;
	}
	ret = pk_client_install_package (client, package_id, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_remove_only:
 **/
static gboolean
pk_console_remove_only (PkClient *client, const gchar *package_id, gboolean force, gboolean autoremove, GError **error)
{
	gboolean ret;

	pk_debug ("remove %s", package_id);
	ret = pk_client_reset (client, error);
	if (!ret) {
		return ret;
	}
	pk_client_set_synchronous (client, TRUE, NULL);
	return pk_client_remove_package (client, package_id, force, autoremove, error);
}

/**
 * pk_console_get_prompt:
 **/
static gboolean
pk_console_get_prompt (const gchar *question, gboolean defaultyes)
{
	gchar answer = '\0';

	/* pretty print */
	g_print ("%s", question);
	if (defaultyes) {
		g_print (" [Y/n] ");
	} else {
		g_print (" [N/y] ");
	}

	do {
		/* get one char */
		answer = (gchar) getchar();

		/* positive */
		if (answer == 'y' || answer == 'Y') {
			return TRUE;
		}
		/* negative */
		if (answer == 'n' || answer == 'N') {
			return FALSE;
		}

		/* default choice */
		if (answer == '\n' && defaultyes) {
			return TRUE;
		}
		if (answer == '\n' && defaultyes == FALSE) {
			return FALSE;
		}
	} while (TRUE);

	/* keep GCC happy */
	return FALSE;
}

/**
 * pk_console_remove_package:
 **/
static gboolean
pk_console_remove_package (PkClient *client, const gchar *package, GError **error)
{
	gchar *package_id;
	gboolean ret;
	guint length;
	PkPackageItem *item;
	PkPackageId *ident;
	guint i;
	gboolean remove;

	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to remove\n"));
		return FALSE;
	}

	/* are we dumb and can't check for requires? */
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_REQUIRES) == FALSE) {
		/* no, just try to remove it without deps */
		ret = pk_console_remove_only (client, package_id, FALSE, FALSE, error);
		g_free (package_id);
		return ret;
	}

	/* see if any packages require this one */
	ret = pk_client_reset (client_task, error);
	if (!ret) {
		pk_warning ("failed to reset");
		return FALSE;
	}

	pk_debug (_("Getting installed requires for %s"), package_id);
	ret = pk_client_get_requires (client_task, "installed", package_id, TRUE, error);
	if (!ret) {
		return FALSE;
	}

	/* see how many packages there are */
	length = pk_client_package_buffer_get_size (client_task);

	/* if there are no required packages, just do the remove */
	if (length == 0) {
		pk_debug ("no requires");
		ret = pk_console_remove_only (client, package_id, FALSE, FALSE, error);
		g_free (package_id);
		return ret;
	}

	/* present this to the user */
	g_print (_("The following packages have to be removed:\n"));
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client_task, i);
		ident = pk_package_id_new_from_string (item->package_id);
		g_print ("%i\t%s-%s\n", i, ident->name, ident->version);
		pk_package_id_free (ident);
	}

	/* get user input */
	remove = pk_console_get_prompt (_("Okay to remove additional packages?"), FALSE);

	/* we chickened out */
	if (remove == FALSE) {
		g_print ("Cancelled!\n");
		g_free (package_id);
		return FALSE;
	}

	/* remove all the stuff */
	ret = pk_console_remove_only (client, package_id, TRUE, FALSE, error);
	g_free (package_id);

	return ret;
}

/**
 * pk_console_update_package:
 **/
static gboolean
pk_console_update_package (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to update\n"));
		return FALSE;
	}
	ret = pk_client_update_package (client, package_id, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_requires:
 **/
static gboolean
pk_console_get_requires (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_NONE, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to get requires\n"));
		return FALSE;
	}
	ret = pk_client_get_requires (client, "none", package_id, TRUE, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_depends:
 **/
static gboolean
pk_console_get_depends (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_NONE, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to get depends\n"));
		return FALSE;
	}
	ret = pk_client_get_depends (client, "none", package_id, FALSE, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_description:
 **/
static gboolean
pk_console_get_description (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_NONE, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to get description\n"));
		return FALSE;
	}
	ret = pk_client_get_description (client, package_id, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_files:
 **/
static gboolean
pk_console_get_files (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_NONE, package, error);
	if (package_id == NULL) {
		g_print (_("Could not find a package with that name to get files\n"));
		return FALSE;
	}
	ret = pk_client_get_files (client, package_id, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_update_detail
 **/
static gboolean
pk_console_get_update_detail (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package, error);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to get update details\n");
		return FALSE;
	}
	ret = pk_client_get_update_detail (client, package_id, error);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_process_commands:
 **/
static gboolean
pk_console_process_commands (PkClient *client, int argc, char *argv[], GError **error)
{
	const gchar *mode;
	const gchar *value = NULL;
	const gchar *details = NULL;
	const gchar *parameter = NULL;
	PkEnumList *elist;
	gboolean ret = FALSE;

	mode = argv[1];
	if (argc > 2) {
		value = argv[2];
	}
	if (argc > 3) {
		details = argv[3];
	}
	if (argc > 4) {
		parameter = argv[4];
	}

	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a search type"));
			return FALSE;
		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_client_search_name (client, "none", details, error);
			}
		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_client_search_details (client, "none", details, error);
			}
		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_client_search_group (client, "none", details, error);
			}
		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_client_search_file (client, "none", details, error);
			}
		} else {
			g_set_error (error, 0, 0, _("invalid search type"));
		}
	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a package to install"));
			return FALSE;
		} else {
			ret = pk_console_install_package (client, value, error);
		}
	} else if (strcmp (mode, "install-file") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a package to install"));
			return FALSE;
		} else {
			ret = pk_client_install_file (client, value, error);
		}
	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a package to remove"));
			return FALSE;
		} else {
			ret = pk_console_remove_package (client, value, error);
		}
	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a package to update"));
			return FALSE;
		} else {
			ret = pk_console_update_package (client, value, error);
		}
	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a package name to resolve"));
			return FALSE;
		} else {
			ret = pk_client_resolve (client, "none", value, error);
		}
	} else if (strcmp (mode, "enable-repo") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a repo name"));
			return FALSE;
		} else {
			ret = pk_client_repo_enable (client, value, TRUE, error);
		}
	} else if (strcmp (mode, "disable-repo") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a repo name"));
			return FALSE;
		} else {
			ret = pk_client_repo_enable (client, value, FALSE, error);
		}
	} else if (strcmp (mode, "set-repo-data") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			g_set_error (error, 0, 0, _("specify a repo name/parameter and value"));
			return FALSE;
		} else {
			ret = pk_client_repo_set_data (client, value, details, parameter, error);
		}
	} else if (strcmp (mode, "get") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, _("specify a get type"));
			return FALSE;
		} else if (strcmp (value, "time") == 0) {
			PkRoleEnum role;
			guint time;
			gboolean ret;
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			}
			role = pk_role_enum_from_text (details);
			if (role == PK_ROLE_ENUM_UNKNOWN) {
				g_set_error (error, 0, 0, _("specify a correct role"));
				return FALSE;
			}
			ret = pk_client_get_time_since_action (client, role, &time, error);
			if (ret == FALSE) {
				g_set_error (error, 0, 0, _("failed to get last time"));
				return FALSE;
			}
			g_print ("time since %s is %is\n", details, time);
		} else if (strcmp (value, "depends") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_console_get_depends (client, details, error);
			}
		} else if (strcmp (value, "updatedetail") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_console_get_update_detail (client, details, error);
			}
		} else if (strcmp (value, "requires") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_console_get_requires (client, details, error);
			}
		} else if (strcmp (value, "what-provides") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a search term"));
				return FALSE;
			} else {
				ret = pk_client_what_provides (client, "none", PK_PROVIDES_ENUM_CODEC, details, error);
			}
		} else if (strcmp (value, "description") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a package to find the description for"));
				return FALSE;
			} else {
				ret = pk_console_get_description (client, details, error);
			}
		} else if (strcmp (value, "files") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, _("specify a package to find the files for"));
				return FALSE;
			} else {
				ret = pk_console_get_files (client, details, error);
			}
		} else if (strcmp (value, "updates") == 0) {
			ret = pk_client_get_updates (client, "basename", error);
		} else if (strcmp (value, "actions") == 0) {
			elist = pk_client_get_actions (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "filters") == 0) {
			elist = pk_client_get_filters (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "repos") == 0) {
			ret = pk_client_get_repo_list (client, "none", error);
		} else if (strcmp (value, "groups") == 0) {
			elist = pk_client_get_groups (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "transactions") == 0) {
			ret = pk_client_get_old_transactions (client, 10, error);
		} else {
			g_set_error (error, 0, 0, _("invalid get type"));
		}
	} else if (strcmp (mode, "update-system") == 0) {
		ret = pk_client_update_system (client, error);
	} else if (strcmp (mode, "refresh") == 0) {
		ret = pk_client_refresh_cache (client, FALSE, error);
	} else if (strcmp (mode, "force-refresh") == 0) {
		ret = pk_client_refresh_cache (client, TRUE, error);
	} else {
		g_set_error (error, 0, 0, _("option not yet supported"));
	}
	return ret;
}

/**
 * pk_console_error_code_cb:
 **/
static void
pk_console_error_code_cb (PkClient *client, PkErrorCodeEnum error_code, const gchar *details, gpointer data)
{
	/* if on console, clear the progress bar line */
	if (is_console && printed_bar) {
		g_print ("\n");
	}
	g_print ("Error: %s : %s\n", pk_error_enum_to_text (error_code), details);
}

/**
 * pk_console_description_cb:
 **/
static void
pk_console_description_cb (PkClient *client, const gchar *package_id,
			   const gchar *license, PkGroupEnum group,
			   const gchar *description, const gchar *url,
			   gulong size, gpointer data)
{
	/* if on console, clear the progress bar line */
	if (is_console && printed_bar) {
		g_print ("\n");
	}
	g_print (_("Package description\n"));
	g_print ("  package:     '%s'\n", package_id);
	g_print ("  license:     '%s'\n", license);
	g_print ("  group:       '%s'\n", pk_group_enum_to_text (group));
	g_print ("  description: '%s'\n", description);
	g_print ("  size:        '%ld' bytes\n", size);
	g_print ("  url:         '%s'\n", url);
}

/**
 * pk_console_files_cb:
 **/
static void
pk_console_files_cb (PkClient *client, const gchar *package_id,
		     const gchar *filelist, gpointer data)
{
	gchar **filevector = g_strsplit (filelist, ";", 0);

	/* if on console, clear the progress bar line */
	if (is_console && printed_bar) {
		g_print ("\n");
	}

	g_print ("Package files\n");

	if (*filevector != NULL) {
		gchar **current_file = filevector;

		while (*current_file != NULL) {
			g_print ("  %s\n", *current_file);
			current_file++;
		}
	} else {
	    g_print ("  no files\n");
	}

	g_strfreev (filevector);
}

/**
 * pk_console_repo_signature_required_cb:
 **/
static void
pk_console_repo_signature_required_cb (PkClient *client, const gchar *package_id, const gchar *repository_name,
				       const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				       const gchar *key_fingerprint, const gchar *key_timestamp,
				       PkSigTypeEnum type, gpointer data)
{
	gboolean import;

	g_print ("Repository Signature Required\n");
	g_print ("Package:     %s\n", package_id);
	g_print ("Name:        %s\n", repository_name);
	g_print ("URL:         %s\n", key_url);
	g_print ("User:        %s\n", key_userid);
	g_print ("ID:          %s\n", key_id);
	g_print ("Fingerprint: %s\n", key_fingerprint);
	g_print ("Timestamp:   %s\n", key_timestamp);

	/* it didn't quite cut it for the release */
	g_debug ("Importing keys is not supported yet. We're working on it!");
	return;

	/* get user input */
	import = pk_console_get_prompt (_("Okay to import key?"), FALSE);
	if (!import) {
		g_print ("%s\n", _("Did not import key, task will fail"));
		return;
	}
	g_print ("TODO: import key\n");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, gpointer data)
{
	/* if the daemon crashed, don't hang around */
	if (connected == FALSE) {
		g_print (_("The daemon crashed mid transaction. This is bad\n"));
		exit (2);
	}
}

/**
 * pk_console_sigint_handler:
 **/
static void
pk_console_sigint_handler (int sig)
{
	PkRoleEnum role;
	gboolean ret;
	GError *error = NULL;
	pk_debug ("Handling SIGINT");

	/* restore default ASAP, as the cancels might hang */
	signal (SIGINT, SIG_DFL);

	/* cancel any tasks */
	pk_client_get_role (client, &role, NULL, NULL);
	if (role != PK_ROLE_ENUM_UNKNOWN) {
		ret = pk_client_cancel (client, &error);
		if (!ret) {
			pk_warning ("failed to cancel normal client: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}
	pk_client_get_role (client_task, &role, NULL, NULL);
	if (role != PK_ROLE_ENUM_UNKNOWN) {
		ret = pk_client_cancel (client_task, &error);
		if (!ret) {
			pk_warning ("failed to cancel task client: %s", error->message);
			g_error_free (error);
		}
	}

	/* kill ourselves */
	pk_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	DBusGConnection *system_connection;
	GError *error = NULL;
	PkConnection *pconnection;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	gboolean nowait = FALSE;
	GOptionContext *context;
	gchar *options_help;
	gboolean ret;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			_("Show the program version and exit"), NULL},
		{ "nowait", 'n', 0, G_OPTION_ARG_NONE, &nowait,
			_("Exit without waiting for actions to complete"), NULL},
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	/* do stuff on ctrl-c */
	signal (SIGINT, pk_console_sigint_handler);

	/* check if we are on console */
	if (isatty (fileno (stdout)) == 1) {
		is_console = TRUE;
	}

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error (_("Could not connect to system DBUS."));
	}

	context = g_option_context_new ("PackageKit Console Program");
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	if (argc < 2) {
		g_print ("%s", options_help);
		return 1;
	}

	pk_debug_init (verbose);
	loop = g_main_loop_new (NULL, FALSE);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), loop);

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	pk_client_set_synchronous (client, !nowait, NULL);
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_console_package_cb), NULL);
	g_signal_connect (client, "transaction",
			  G_CALLBACK (pk_console_transaction_cb), NULL);
	g_signal_connect (client, "description",
			  G_CALLBACK (pk_console_description_cb), NULL);
	g_signal_connect (client, "files",
			  G_CALLBACK (pk_console_files_cb), NULL);
	g_signal_connect (client, "repo-signature-required",
			  G_CALLBACK (pk_console_repo_signature_required_cb), NULL);
	g_signal_connect (client, "update-detail",
			  G_CALLBACK (pk_console_update_detail_cb), NULL);
	g_signal_connect (client, "repo-detail",
			  G_CALLBACK (pk_console_repo_detail_cb), NULL);
	g_signal_connect (client, "progress-changed",
			  G_CALLBACK (pk_console_progress_changed_cb), NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_console_finished_cb), NULL);
	g_signal_connect (client, "error-code",
			  G_CALLBACK (pk_console_error_code_cb), NULL);

	client_task = pk_client_new ();
	pk_client_set_use_buffer (client_task, TRUE, NULL);
	pk_client_set_synchronous (client_task, TRUE, NULL);
	g_signal_connect (client_task, "finished",
			  G_CALLBACK (pk_console_finished_cb), NULL);

	role_list = pk_client_get_actions (client);
	pk_debug ("actions=%s", pk_enum_list_to_string (role_list));

	/* run the commands */
	ret = pk_console_process_commands (client, argc, argv, &error);
	if (ret == FALSE) {
		g_print (_("Command failed\n"));
	}
	if (error != NULL) {
		if (g_str_has_prefix (error->message, "org.freedesktop.packagekit."))  {
			g_print (_("You don't have the necessary privileges for this operation\n"));
		}
		else {
			g_print ("Error:\n  %s\n\n", error->message);
			g_error_free (error);
			g_print ("%s", options_help);
		}
	}

	g_free (options_help);
	g_object_unref (client);
	g_object_unref (client_task);

	return 0;
}
