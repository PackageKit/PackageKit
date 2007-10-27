/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#define PROGRESS_BAR_PADDING 22
#define MINIMUM_COLUMNS (PROGRESS_BAR_PADDING + 5)

static GMainLoop *loop = NULL;
static gboolean is_console = FALSE;
static gboolean has_output = FALSE;
static gboolean printed_bar = FALSE;
static guint timer_id = 0;

typedef struct {
	gint position;
	gboolean move_forward;
} PulseState;

/**
 * pk_console_pad_string:
 **/
static gchar *
pk_console_pad_string (const gchar *data, guint length, guint *extra)
{
	gint size;
	gchar *text;
	gchar *padding;

	if (extra != NULL) {
		*extra = 0;
	}
	size = length;
	if (data != NULL) {
		/* ITS4: ignore, only used for formatting */
		size = (length - strlen(data));
		if (size < 0) {
			if (extra != NULL) {
				*extra = -size;
			}
			size = 0;
		}
	}
	padding = g_strnfill (size, ' ');
	if (data == NULL) {
		return padding;
	}
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

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
	if (is_console == TRUE && printed_bar == TRUE && has_output == FALSE) {
		g_print ("\r");
	}

	/* pass this out */
	info_text = pk_console_pad_string (pk_info_enum_to_text (info), 12, NULL);

	spacing = pk_package_id_new ();
	ident = pk_package_id_new_from_string (package_id);

	/* these numbers are guesses */
	extra = 0;
	spacing->name = pk_console_pad_string (ident->name, 20, &extra);
	spacing->arch = pk_console_pad_string (ident->arch, 7-extra, &extra);
	spacing->version = pk_console_pad_string (ident->version, 15-extra, &extra);
	spacing->data = pk_console_pad_string (ident->data, 12-extra, &extra);

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
			     const gchar *url, const gchar *restart,
			     const gchar *update_text, gpointer data)
{
	g_print ("Update detail\n");
	g_print ("  package:    '%s'\n", package_id);
	g_print ("  updates:    '%s'\n", updates);
	g_print ("  obsoletes:  '%s'\n", obsoletes);
	g_print ("  url:        '%s'\n", url);
	g_print ("  restart:    '%s'\n", restart);
	g_print ("  update_text:'%s'\n", update_text);
}

/**
 * pk_console_repo_detail_cb:
 **/
static void
pk_console_repo_detail_cb (PkClient *client, const gchar *repo_id,
			   const gchar *description, gboolean enabled, gpointer data)
{
	gchar *repo;
	repo = pk_console_pad_string (repo_id, 28, NULL);
	if (enabled == TRUE) {
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
	if (has_output == TRUE) {
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

	if (pulse_state->move_forward == TRUE) {
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
	if (is_console == TRUE) {
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
	if (is_console == TRUE) {
		if (percentage == PK_CLIENT_PERCENTAGE_INVALID) {
			pk_console_draw_pulse_bar ();
		} else {
			pk_console_draw_progress_bar (percentage, remaining);
		}
	} else {
		g_print ("%i%%\n", percentage);
	}
}

const gchar *summary =
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
	"\n"
	"  package_id is typically gimp;2:2.4.0-0.rc1.1.fc8;i386;development";

/**
 * pk_client_wait:
 **/
static gboolean
pk_client_wait (void)
{
	pk_debug ("starting loop");
	g_main_loop_run (loop);
	return TRUE;
}

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (PkClient *client, PkStatusEnum status, guint runtime, gpointer data)
{
	PkRoleEnum role;
	gchar *blanking;
	const gchar *role_text;
	gfloat time;

	/* cancel the spinning */
	if (timer_id != 0) {
		g_source_remove (timer_id);
	}

	/* if on console, clear the progress bar line */
	if (is_console == TRUE && printed_bar == TRUE && has_output == FALSE) {
		g_print ("\r");
		blanking = g_strnfill (pk_console_get_terminal_columns (), ' ');
		g_print ("%s", blanking);
		g_free (blanking);
		g_print ("\r");
	}

	pk_client_get_role (client, &role, NULL);
	role_text = pk_role_enum_to_text (role);
	time = (gfloat) runtime / 1000.0;
	g_print ("%s runtime was %.1f seconds\n", role_text, time);
	if (loop != NULL) {
		g_main_loop_quit (loop);
	}
}

/**
 * pk_console_perhaps_resolve:
 **/
static gchar *
pk_console_perhaps_resolve (PkClient *client, PkFilterEnum filter, const gchar *package)
{
	gboolean ret;
	gboolean valid;
	PkClient *client_resolve;
	const gchar *filter_text;
	guint i;
	guint length;
	PkPackageItem *item;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid == TRUE) {
		return g_strdup (package);
	}

	/* we need to resolve it */
	client_resolve = pk_client_new ();
	g_signal_connect (client_resolve, "finished",
	/* TODO: send local loop */
			  G_CALLBACK (pk_console_finished_cb), NULL);
	filter_text = pk_filter_enum_to_text (filter);
	pk_client_set_use_buffer (client_resolve, TRUE);
	ret = pk_client_resolve (client_resolve, filter_text, package);
	if (ret == FALSE) {
		pk_warning ("Resolve is not supported in this backend");
		return NULL;
	} else {
		g_main_loop_run (loop);
	}

	/* get length of items found */
	length = pk_client_package_buffer_get_size (client_resolve);

	/* only found one, great! */
	if (length == 1) {
		item = pk_client_package_buffer_get_item (client_resolve, 0);
		return item->package_id;
	}

	/* else list the options */
	g_print ("There are multiple matches\n");
	for (i=0; i<length; i++) {
		item = pk_client_package_buffer_get_item (client_resolve, i);
		g_print ("%i. %s\n", i+1, item->package_id);
	}
	return NULL;
}

/**
 * pk_console_install_package:
 **/
static gboolean
pk_console_install_package (PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_AVAILABLE, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to install\n");
		return FALSE;
	}
	ret = pk_client_install_package (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_remove_package:
 **/
static gboolean
pk_console_remove_package (PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to remove\n");
		return FALSE;
	}
	ret = pk_client_remove_package (client, package_id, FALSE);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_update_package:
 **/
static gboolean
pk_console_update_package (PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to update\n");
		return FALSE;
	}
	ret = pk_client_update_package (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_requires:
 **/
static gboolean
pk_console_get_requires(PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to get requires\n");
		return FALSE;
	}
	ret = pk_client_get_requires (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_depends:
 **/
static gboolean
pk_console_get_depends(PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to get depends\n");
		return FALSE;
	}
	ret = pk_client_get_depends (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_description:
 **/
static gboolean
pk_console_get_description(PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to get description\n");
		return FALSE;
	}
	ret = pk_client_get_description (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_files:
 **/
static gboolean
pk_console_get_files (PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to get files\n");
		return FALSE;
	}
	ret = pk_client_get_files (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_update_detail
 **/
static gboolean
pk_console_get_update_detail(PkClient *client, const gchar *package)
{
	gboolean ret;
	gchar *package_id;
	package_id = pk_console_perhaps_resolve (client, PK_FILTER_ENUM_INSTALLED, package);
	if (package_id == NULL) {
		g_print ("Could not find a package with that name to get update details\n");
		return FALSE;
	}
	ret = pk_client_get_update_detail (client, package_id);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_process_commands:
 **/
static gboolean
pk_console_process_commands (PkClient *client, int argc, char *argv[], gboolean wait_override, GError **error)
{
	const gchar *mode;
	const gchar *value = NULL;
	const gchar *details = NULL;
	gboolean wait = FALSE;
	PkEnumList *elist;

	mode = argv[1];
	if (argc > 2) {
		value = argv[2];
	}
	if (argc > 3) {
		details = argv[3];
	}

	if (strcmp (mode, "search") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a search type");
			return FALSE;
		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_name (client, "none", details);
			}
		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_details (client, "none", details);
			}
		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_group (client, "none", details);
			}
		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_client_search_file (client, "none", details);
			}
		} else {
			g_set_error (error, 0, 0, "invalid search type");
		}
	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to install");
			return FALSE;
		} else {
			wait = pk_console_install_package (client, value);
		}
	} else if (strcmp (mode, "install-file") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to install");
			return FALSE;
		} else {
			wait = pk_client_install_file (client, value);
		}
	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to remove");
			return FALSE;
		} else {
			wait = pk_console_remove_package (client, value);
		}
	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package to update");
			return FALSE;
		} else {
			wait = pk_console_update_package (client, value);
		}
	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a package name to resolve");
			return FALSE;
		} else {
			wait = pk_client_resolve (client, "none", value);
		}
	} else if (strcmp (mode, "enable-repo") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a repo name");
			return FALSE;
		} else {
			pk_client_repo_enable (client, value, TRUE);
		}
	} else if (strcmp (mode, "disable-repo") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a repo name");
			return FALSE;
		} else {
			wait = pk_client_repo_enable (client, value, FALSE);
		}
	} else if (strcmp (mode, "get") == 0) {
		if (value == NULL) {
			g_set_error (error, 0, 0, "you need to specify a get type");
			return FALSE;
		} else if (strcmp (value, "depends") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_console_get_depends (client, details);
			}
		} else if (strcmp (value, "updatedetail") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_console_get_update_detail (client, details);
			}
		} else if (strcmp (value, "requires") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a search term");
				return FALSE;
			} else {
				wait = pk_console_get_requires (client, details);
			}
		} else if (strcmp (value, "description") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a package to find the description for");
				return FALSE;
			} else {
				wait = pk_console_get_description (client, details);
			}
		} else if (strcmp (value, "files") == 0) {
			if (details == NULL) {
				g_set_error (error, 0, 0, "you need to specify a package to find the files for");
				return FALSE;
			} else {
				wait = pk_console_get_files (client, details);
			}
		} else if (strcmp (value, "updates") == 0) {
			wait = pk_client_get_updates (client);
		} else if (strcmp (value, "actions") == 0) {
			elist = pk_client_get_actions (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "filters") == 0) {
			elist = pk_client_get_filters (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "repos") == 0) {
			wait = pk_client_get_repo_list (client);
		} else if (strcmp (value, "groups") == 0) {
			elist = pk_client_get_groups (client);
			pk_enum_list_print (elist);
			g_object_unref (elist);
		} else if (strcmp (value, "transactions") == 0) {
			wait = pk_client_get_old_transactions (client, 10);
		} else {
			g_set_error (error, 0, 0, "invalid get type");
		}
	} else if (strcmp (mode, "update-system") == 0) {
		wait = pk_client_update_system (client);
	} else if (strcmp (mode, "refresh") == 0) {
		wait = pk_client_refresh_cache (client, FALSE);
	} else if (strcmp (mode, "force-refresh") == 0) {
		wait = pk_client_refresh_cache (client, TRUE);
	} else {
		g_set_error (error, 0, 0, "option not yet supported");
	}

	/* only wait if success */
	if (wait == TRUE && wait_override == TRUE) {
		pk_client_wait ();
	}
	return TRUE;
}

/**
 * pk_console_error_code_cb:
 **/
static void
pk_console_error_code_cb (PkClient *client, PkErrorCodeEnum error_code, const gchar *details, gpointer data)
{
	/* if on console, clear the progress bar line */
	if (is_console == TRUE && printed_bar == TRUE) {
		g_print ("\n");
	}
	g_print ("Error: %s : %s\n", pk_error_enum_to_text (error_code), details);
}

/**
 * pk_console_description_cb:
 **/
static void
pk_console_description_cb (PkClient *client, const gchar *package_id,
			   const gchar *licence, PkGroupEnum group,
			   const gchar *description, const gchar *url,
			   gulong size, const gchar *filelist, gpointer data)
{
	/* if on console, clear the progress bar line */
	if (is_console == TRUE && printed_bar == TRUE) {
		g_print ("\n");
	}
	g_print ("Package description\n");
	g_print ("  package:     '%s'\n", package_id);
	g_print ("  licence:     '%s'\n", licence);
	g_print ("  group:       '%s'\n", pk_group_enum_to_text (group));
	g_print ("  description: '%s'\n", description);
	g_print ("  size:        '%ld' bytes\n", size);
	/* filelist is probably too long to just dump to the screen */
	/* g_print ("  files:       '%s'\n", filelist); */
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
	if (is_console == TRUE && printed_bar == TRUE) {
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
pk_console_repo_signature_required_cb (PkClient *client, const gchar *repository_name, const gchar *key_url,
				       const gchar *key_userid, const gchar *key_id, const gchar *key_fingerprint,
				       const gchar *key_timestamp, PkSigTypeEnum type, gpointer data)
{
	g_print ("Signature Required\n");
	g_print ("  repo name:       '%s'\n", repository_name);
	g_print ("  key url:         '%s'\n", key_url);
	g_print ("  key userid:      '%s'\n", key_userid);
	g_print ("  key id:          '%s'\n", key_id);
	g_print ("  key fingerprint: '%s'\n", key_fingerprint);
	g_print ("  key timestamp:   '%s'\n", key_timestamp);
	g_print ("  key type:        '%s'\n", pk_sig_type_enum_to_text (type));

}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	DBusGConnection *system_connection;
	GError *error = NULL;
	PkClient *client;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	gboolean nowait = FALSE;
	GOptionContext *context;
	gchar *options_help;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			"Show the program version and exit", NULL},
		{ "nowait", 'n', 0, G_OPTION_ARG_NONE, &nowait,
			"Exit without waiting for actions to complete", NULL},
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	/* check if we are on console */
	if (isatty (fileno (stdout)) == 1) {
		is_console = TRUE;
	}

	/* check dbus connections, exit if not valid */
	system_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	context = g_option_context_new (_("SUBCOMMAND"));
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	if (program_version == TRUE) {
		g_print (VERSION "\n");
		return 0;
	}

	if (argc < 2) {
		g_print (options_help);
		return 1;
	}

	pk_debug_init (verbose);
	loop = g_main_loop_new (NULL, FALSE);

	client = pk_client_new ();
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

	/* run the commands */
	pk_console_process_commands (client, argc, argv, !nowait, &error);
	if (error != NULL) {
		g_print ("Error:\n  %s\n\n", error->message);
		g_error_free (error);
		g_print (options_help);
	}

	g_free (options_help);
	g_object_unref (client);

	return 0;
}
