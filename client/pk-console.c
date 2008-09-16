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

#include "egg-debug.h"
#include "egg-string.h"

#include <pk-package-ids.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-package-id.h>
#include <pk-common.h>
#include <pk-connection.h>
#include <pk-update-detail-obj.h>
#include <pk-distro-upgrade-obj.h>

#include "pk-tools-common.h"

#define PROGRESS_BAR_SIZE 15

static GMainLoop *loop = NULL;
static PkBitfield roles;
static gboolean is_console = FALSE;
static gboolean has_output_bar = FALSE;
static gboolean need_requeue = FALSE;
static gboolean nowait = FALSE;
static gboolean awaiting_space = FALSE;
static gboolean trusted = TRUE;
static guint timer_id = 0;
static guint percentage_last = 0;
static gchar **files_cache = NULL;
static PkControl *control = NULL;
static PkClient *client = NULL;
static PkClient *client_task = NULL;
static PkClient *client_install_files = NULL;
static PkClient *client_signature = NULL;

typedef struct {
	gint position;
	gboolean move_forward;
} PulseState;

/**
 * pk_console_bar:
 **/
static void
pk_console_bar (guint subpercentage)
{
	guint section;
	guint i;

	/* don't pretty print */
	if (!is_console) {
		return;
	}
	if (!has_output_bar) {
		return;
	}
	/* restore cursor */
	g_print ("%c8", 0x1B);

	section = (guint) ((gfloat) PROGRESS_BAR_SIZE / (gfloat) 100.0 * (gfloat) subpercentage);
	g_print ("[");
	for (i=0; i<section; i++) {
		g_print ("=");
	}
	for (i=0; i<PROGRESS_BAR_SIZE-section; i++) {
		g_print (" ");
	}
	g_print ("] ");
	if (percentage_last != PK_CLIENT_PERCENTAGE_INVALID) {
		g_print ("(%i%%)", percentage_last);
	} else {
		g_print ("       ");
	}
	awaiting_space = TRUE;
}

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

	if (data == NULL) {
		return g_strnfill (length, ' ');
	}

	/* ITS4: ignore, only used for formatting */
	data_len = strlen (data);

	/* calculate */
	size = (length - data_len);
	if (size <= 0) {
		return g_strdup (data);
	}

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

/**
 * pk_console_start_bar:
 **/
static void
pk_console_start_bar (const gchar *text)
{
	gchar *text_pad;

	/* make these all the same length */
	text_pad = pk_strpad (text, 50);
	g_print ("%s", text_pad);
	g_free (text_pad);
	has_output_bar = TRUE;

	/* save cursor in new position */
	g_print ("%c7", 0x1B);
	pk_console_bar (0);
}

/**
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (PkClient *client, const PkPackageObj *obj, gpointer data)
{
	PkRoleEnum role;
	gchar *package = NULL;
	gchar *package_pad = NULL;
	gchar *info_pad = NULL;
	gchar *text = NULL;

	/* make these all the same length */
	info_pad = pk_strpad (pk_info_enum_to_text (obj->info), 12);

	/* don't pretty print */
	if (!is_console) {
		g_print ("%s %s-%s.%s\n", info_pad, obj->id->name, obj->id->version, obj->id->arch);
		goto out;
	}

	/* pad the name-version */
	if (egg_strzero (obj->id->version)) {
		package = g_strdup (obj->id->name);
	} else {
		package = g_strdup_printf ("%s-%s", obj->id->name, obj->id->version);
	}
	package_pad = pk_strpad (package, 40);

	/* mark previous complete */
	if (has_output_bar) {
		pk_console_bar (100);
	}

	if (awaiting_space) {
		g_print ("\n");
	}

	pk_client_get_role (client, &role, NULL, NULL);
	if (role == PK_ROLE_ENUM_SEARCH_NAME ||
	    role == PK_ROLE_ENUM_SEARCH_GROUP ||
	    role == PK_ROLE_ENUM_SEARCH_FILE ||
	    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
	    role == PK_ROLE_ENUM_GET_PACKAGES ||
	    role == PK_ROLE_ENUM_GET_DEPENDS ||
	    role == PK_ROLE_ENUM_GET_REQUIRES ||
	    role == PK_ROLE_ENUM_GET_UPDATES) {
		/* don't do the bar */
		g_print ("%s\t%s\t%s\n", info_pad, package_pad, obj->summary);
		goto out;
	}

	text = g_strdup_printf ("%s\t%s", info_pad, package);
	pk_console_start_bar (text);
	g_free (text);

out:
	/* free all the data */
	g_free (package);
	g_free (package_pad);
	g_free (info_pad);
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
	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("Transaction  : %s\n", tid);
	g_print (" timespec    : %s\n", timespec);
	g_print (" succeeded   : %i\n", succeeded);
	g_print (" role        : %s\n", role_text);
	g_print (" duration    : %i (seconds)\n", duration);
	g_print (" data        : %s\n", data);
}

/**
 * pk_console_distro_upgrade_cb:
 **/
static void
pk_console_distro_upgrade_cb (PkClient *client, const PkDistroUpgradeObj *obj, gpointer user_data)
{
	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("Distro       : %s\n", obj->name);
	g_print (" type        : %s\n", pk_update_state_enum_to_text (obj->state));
	g_print (" summary     : %s\n", obj->summary);
}

/**
 * pk_console_update_detail_cb:
 **/
static void
pk_console_update_detail_cb (PkClient *client, const PkUpdateDetailObj *detail, gpointer data)
{
	gchar *issued;
	gchar *updated;

	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("%s\n", _("Update detail"));
	g_print ("  package:    '%s-%s.%s'\n", detail->id->name, detail->id->version, detail->id->arch);
	if (!egg_strzero (detail->updates)) {
		g_print ("  updates:    '%s'\n", detail->updates);
	}
	if (!egg_strzero (detail->obsoletes)) {
		g_print ("  obsoletes:  '%s'\n", detail->obsoletes);
	}
	if (!egg_strzero (detail->vendor_url)) {
		g_print ("  vendor URL: '%s'\n", detail->vendor_url);
	}
	if (!egg_strzero (detail->bugzilla_url)) {
		g_print ("  bug URL:    '%s'\n", detail->bugzilla_url);
	}
	if (!egg_strzero (detail->cve_url)) {
		g_print ("  cve URL:    '%s'\n", detail->cve_url);
	}
	if (detail->restart != PK_RESTART_ENUM_NONE) {
		g_print ("  restart:    '%s'\n", pk_restart_enum_to_text (detail->restart));
	}
	if (!egg_strzero (detail->update_text)) {
		g_print ("  update_text:'%s'\n", detail->update_text);
	}
	if (!egg_strzero (detail->changelog)) {
		g_print ("  changelog:  '%s'\n", detail->changelog);
	}
	if (detail->state != PK_UPDATE_STATE_ENUM_UNKNOWN) {
		g_print ("  state:      '%s'\n", pk_update_state_enum_to_text (detail->state));
	}
	issued = pk_iso8601_from_date (detail->issued);
	if (!egg_strzero (issued)) {
		g_print ("  issued:     '%s'\n", issued);
	}
	updated = pk_iso8601_from_date (detail->updated);
	if (!egg_strzero (updated)) {
		g_print ("  updated:    '%s'\n", updated);
	}
	g_free (issued);
	g_free (updated);
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
	if (awaiting_space) {
		g_print ("\n");
	}
	if (enabled) {
		g_print ("  enabled   %s %s\n", repo, description);
	} else {
		g_print ("  disabled  %s %s\n", repo, description);
	}
	g_free (repo);
}

/**
 * pk_console_pulse_bar:
 **/
static gboolean
pk_console_pulse_bar (PulseState *pulse_state)
{
	guint i;

	if (!has_output_bar) {
		return TRUE;
	}

	/* restore cursor */
	g_print ("%c8", 0x1B);

	if (pulse_state->move_forward) {
		if (pulse_state->position == PROGRESS_BAR_SIZE - 1) {
			pulse_state->move_forward = FALSE;
		} else {
			pulse_state->position++;
		}
	} else if (!pulse_state->move_forward) {
		if (pulse_state->position == 1) {
			pulse_state->move_forward = TRUE;
		} else {
			pulse_state->position--;
		}
	}

	g_print ("[");
	for (i=0; i<pulse_state->position-1; i++) {
		g_print (" ");
	}
	printf("==");
	for (i=0; i<PROGRESS_BAR_SIZE-pulse_state->position-1; i++) {
		g_print (" ");
	}
	g_print ("] ");
	if (percentage_last != PK_CLIENT_PERCENTAGE_INVALID) {
		g_print ("(%i%%)", percentage_last);
	} else {
		g_print ("        ");
	}

	return TRUE;
}

/**
 * pk_console_draw_pulse_bar:
 **/
static void
pk_console_draw_pulse_bar (void)
{
	static PulseState pulse_state;

	/* have we already got zero percent? */
	if (timer_id != 0) {
		return;
	}
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
	if (!is_console) {
		if (percentage != PK_CLIENT_PERCENTAGE_INVALID) {
			g_print ("percentage: %i%%\n", percentage);
		} else {
			g_print ("percentage: unknown\n");
		}
		return;
	}
	percentage_last = percentage;
	if (subpercentage == PK_CLIENT_PERCENTAGE_INVALID) {
		pk_console_bar (0);
		pk_console_draw_pulse_bar ();
	} else {
		if (timer_id != 0) {
			g_source_remove (timer_id);
			timer_id = 0;
		}
		pk_console_bar (subpercentage);
	}
}

/**
 * pk_console_signature_finished_cb:
 **/
static void
pk_console_signature_finished_cb (PkClient *client_signature, PkExitEnum exit, guint runtime, gpointer data)
{
	gboolean ret;
	GError *error = NULL;

	egg_debug ("trying to requeue");
	ret = pk_client_requeue (client, &error);
	if (!ret) {
		egg_warning ("failed to requeue action: %s", error->message);
		g_error_free (error);
		g_main_loop_quit (loop);
	}
}

/**
 * pk_console_install_files_finished_cb:
 **/
static void
pk_console_install_files_finished_cb (PkClient *client_signature, PkExitEnum exit, guint runtime, gpointer data)
{
	g_main_loop_quit (loop);
}

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	PkRoleEnum role;
	const gchar *role_text;
	gfloat time;
	PkRestartEnum restart;

	pk_client_get_role (client, &role, NULL, NULL);

	/* mark previous complete */
	if (has_output_bar) {
		pk_console_bar (100);
	}

	/* cancel the spinning */
	if (timer_id != 0) {
		g_source_remove (timer_id);
	}

	role_text = pk_role_enum_to_text (role);
	time = (gfloat) runtime / 1000.0;

	/* do we need to new line? */
	if (awaiting_space) {
		g_print ("\n");
	}
	egg_debug ("%s runtime was %.1f seconds", role_text, time);

	/* is there any restart to notify the user? */
	restart = pk_client_get_require_restart (client);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		g_print ("%s\n", _("A system restart is required"));
	} else if (restart == PK_RESTART_ENUM_SESSION) {
		g_print ("%s\n", _("A logout and login is required"));
	} else if (restart == PK_RESTART_ENUM_APPLICATION) {
		g_print ("%s\n", _("An application restart is required"));
	}

	if (role == PK_ROLE_ENUM_INSTALL_FILES &&
	    exit == PK_EXIT_ENUM_FAILED && need_requeue) {
		egg_warning ("waiting for second install file to finish");
		return;
	}

	/* have we failed to install, and the gpg key is now installed */
	if (exit == PK_EXIT_ENUM_KEY_REQUIRED && need_requeue) {
		egg_debug ("key now installed");
		return;
	}

	/* have we failed to install, and the eula key is now installed */
	if (exit == PK_EXIT_ENUM_EULA_REQUIRED && need_requeue) {
		egg_debug ("eula now agreed");
		return;
	}

	/* close the loop */
	g_main_loop_quit (loop);
}

/**
 * pk_console_perhaps_resolve:
 **/
static gchar *
pk_console_perhaps_resolve (PkClient *client, PkBitfield filter, const gchar *package, GError **error)
{
	gboolean ret;
	gboolean valid;
	guint i;
	guint length;
	const PkPackageObj *obj;
	PkPackageList *list;
	gchar **packages;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid) {
		return g_strdup (package);
	}

	ret = pk_client_reset (client_task, error);
	if (!ret) {
		egg_warning ("failed to reset client task");
		return NULL;
	}

	/* we need to resolve it */
	packages = pk_package_ids_from_id (package);
	ret = pk_client_resolve (client_task, filter, packages, error);
	g_strfreev (packages);
	if (!ret) {
		egg_warning ("Resolve is not supported in this backend");
		return NULL;
	}

	/* get length of items found */
	list = pk_client_get_package_list (client_task);
	length = pk_package_list_get_size (list);
	g_object_unref (list);

	/* didn't resolve to anything, try to get a provide */
	if (length == 0) {
		ret = pk_client_reset (client_task, error);
		if (!ret)
			return NULL;
		ret = pk_client_what_provides (client_task, filter, PK_PROVIDES_ENUM_ANY, package, error);
		if (!ret)
			return NULL;
	}

	/* get length of items found again (we might have had success) */
	list = pk_client_get_package_list (client_task);
	length = pk_package_list_get_size (list);
	if (length == 0) {
		egg_debug ("Could not find a package match");
		return NULL;
	}

	/* only found one, great! */
	if (length == 1) {
		obj = pk_package_list_get_obj (list, 0);
		return pk_package_id_to_string (obj->id);
	}

	/* else list the options if multiple matches found */
	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("%s\n", _("There are multiple package matches"));
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		g_print ("%i. %s-%s.%s\n", i+1, obj->id->name, obj->id->version, obj->id->arch);
	}

	/* find out what package the user wants to use */
	i = pk_console_get_number (_("Please enter the package number: "), length);
	obj = pk_package_list_get_obj (list, i-1);
	g_object_unref (list);

	return pk_package_id_to_string (obj->id);
}

/**
 * pk_console_install_stuff:
 **/
static gboolean
pk_console_install_stuff (PkClient *client, gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gboolean is_local;
	gchar *package_id = NULL;
	gchar **package_ids = NULL;
	gchar **files = NULL;
	guint i;
	guint length;
	GPtrArray *array_packages;
	GPtrArray *array_files;

	array_packages = g_ptr_array_new ();
	array_files = g_ptr_array_new ();
	length = g_strv_length (packages);
	for (i=2; i<length; i++) {
		/* are we a local file */
		is_local = g_file_test (packages[i], G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR);
		if (is_local) {
			g_ptr_array_add (array_files, g_strdup (packages[i]));
		} else {
			package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), packages[i], error);
			if (package_id == NULL) {
				*error = g_error_new (1, 0, "%s: %s", _("Could not find package to install"), packages[i]);
				ret = FALSE;
				break;
			}
			g_ptr_array_add (array_packages, package_id);
		}
	}

	/* one of the resolves failed */
	if (!ret) {
		egg_warning ("resolve failed");
		goto out;
	}


	/* any to process? */
	if (array_packages->len > 0) {
		/* convert to strv */
		package_ids = pk_ptr_array_to_argv (array_packages);

		/* reset */
		ret = pk_client_reset (client, error);
		if (!ret) {
			egg_warning ("failed to reset");
			goto out;
		}

		ret = pk_client_install_packages (client, package_ids, error);
		if (!ret) {
			egg_warning ("failed to install packages");
			goto out;
		}
	}

	/* any to process? */
	if (array_files->len > 0) {
		/* convert to strv */
		files = pk_ptr_array_to_argv (array_files);

		/* save for untrusted callback */
		g_strfreev (files_cache);
		files_cache = g_strdupv (files);

		/* reset */
		ret = pk_client_reset (client, error);
		if (!ret) {
			egg_warning ("failed to reset");
			goto out;
		}

		ret = pk_client_install_files (client, trusted, files, error);
		if (!ret) {
			egg_warning ("failed to install files");
			goto out;
		}
	}

out:
	g_strfreev (package_ids);
	g_strfreev (files);
	g_ptr_array_foreach (array_files, (GFunc) g_free, NULL);
	g_ptr_array_free (array_files, TRUE);
	g_ptr_array_foreach (array_packages, (GFunc) g_free, NULL);
	g_ptr_array_free (array_packages, TRUE);
	return ret;
}

/**
 * pk_console_remove_only:
 **/
static gboolean
pk_console_remove_only (PkClient *client, gchar **package_ids, gboolean force, GError **error)
{
	gboolean ret;

	egg_debug ("remove+ %s", package_ids[0]);
	ret = pk_client_reset (client, error);
	if (!ret) {
		return ret;
	}
	return pk_client_remove_packages (client, package_ids, force, FALSE, error);
}

/**
 * pk_console_remove_packages:
 **/
static gboolean
pk_console_remove_packages (PkClient *client, gchar **packages, GError **error)
{
	gchar *package_id;
	gboolean ret = TRUE;
	const PkPackageObj *obj;
	guint i;
	guint length;
	gboolean remove;
	GPtrArray *array;
	gchar **package_ids = NULL;
	PkPackageList *list;
	PkPackageList *list_single;

	array = g_ptr_array_new ();
	list = pk_package_list_new ();
	length = g_strv_length (packages);
	for (i=2; i<length; i++) {
		package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), packages[i], error);
		if (package_id == NULL) {
			*error = g_error_new (1, 0, "%s:%s\n", _("Could not find package to remove"), packages[i]);
			ret = FALSE;
			break;
		}
		g_ptr_array_add (array, g_strdup (package_id));
		egg_debug ("resolved to %s", package_id);
		g_free (package_id);
	}

	/* one of the resolves failed */
	if (!ret) {
		goto out;
	}

	/* convert to strv */
	package_ids = pk_ptr_array_to_argv (array);

	/* are we dumb and can't check for requires? */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REQUIRES)) {
		/* no, just try to remove it without deps */
		ret = pk_console_remove_only (client, package_ids, FALSE, error);
		goto out;
	}

	ret = pk_client_reset (client_task, error);
	if (!ret) {
		egg_warning ("failed to reset");
		goto out;
	}

	egg_debug ("Getting installed requires for %s", package_ids[0]);
	/* see if any packages require this one */
	ret = pk_client_get_requires (client_task, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, TRUE, error);
	if (!ret) {
		egg_warning ("failed to get requires");
		goto out;
	}

	/* see how many packages there are */
	list_single = pk_client_get_package_list (client_task);
	pk_package_list_add_list (list, list_single);
	g_object_unref (list_single);

	/* one of the get-requires failed */
	if (!ret) {
		goto out;
	}

	/* if there are no required packages, just do the remove */
	length = pk_package_list_get_size (list);
	if (length == 0) {
		egg_debug ("no requires");
		ret = pk_console_remove_only (client, package_ids, FALSE, error);
		goto out;
	}


	/* present this to the user */
	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("%s:\n", _("The following packages have to be removed"));
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);
		g_print ("%i\t%s-%s.%s\n", i, obj->id->name, obj->id->version, obj->id->arch);
	}

	/* get user input */
	remove = pk_console_get_prompt (_("Okay to remove additional packages?"), FALSE);

	/* we chickened out */
	if (!remove) {
		g_print ("%s\n", _("Cancelled!"));
		ret = FALSE;
		goto out;
	}

	/* remove all the stuff */
	ret = pk_console_remove_only (client, package_ids, TRUE, error);

out:
	g_object_unref (list);
	g_strfreev (package_ids);
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);
	return ret;
}

/**
 * pk_console_download_packages:
 **/
static gboolean
pk_console_download_packages (PkClient *client, gchar **packages, const gchar *directory, GError **error)
{
	gboolean ret = TRUE;
	gchar *package_id = NULL;
	gchar **package_ids = NULL;
	guint i;
	guint length;
	GPtrArray *array_packages;

	array_packages = g_ptr_array_new ();
	length = g_strv_length (packages);
	for (i=3; i<length; i++) {
			package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), packages[i], error);
			if (package_id == NULL) {
				*error = g_error_new (1, 0, "%s: %s", _("Could not find package to download"), packages[i]);
				ret = FALSE;
				break;
			}
			g_ptr_array_add (array_packages, package_id);
		}
	
	/* one of the resolves failed */
	if (!ret) {
		egg_warning ("resolve failed");
		goto out;
	}


	/* any to process? */
	if (array_packages->len > 0) {
		/* convert to strv */
		package_ids = pk_ptr_array_to_argv (array_packages);

		/* reset */
		ret = pk_client_reset (client, error);
		if (!ret) {
			egg_warning ("failed to reset");
			goto out;
		}

		ret = pk_client_download_packages (client, package_ids, directory, error);
		if (!ret) {
			egg_warning ("failed to download the packages");
			goto out;
		}
	}

out:
	g_strfreev (package_ids);
	g_ptr_array_foreach (array_packages, (GFunc) g_free, NULL);
	g_ptr_array_free (array_packages, TRUE);
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
	gchar **package_ids;

	package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package, error);
	if (package_id == NULL) {
		*error = g_error_new (1, 0, "%s: %s", _("Could not find package to update"), package);
		return FALSE;
	}

	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_update_packages (client, package_ids, error);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_requires:
 **/
static gboolean
pk_console_get_requires (PkClient *client, PkBitfield filters, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), package, error);
	if (package_id == NULL) {
		*error = g_error_new (1, 0, "%s %s", _("Could not find what packages require"), package);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_requires (client, filters, package_ids, TRUE, error);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_depends:
 **/
static gboolean
pk_console_get_depends (PkClient *client, PkBitfield filters, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), package, error);
	if (package_id == NULL) {
		*error = g_error_new (1, 0, "%s %s", _("Could not get dependencies for"), package);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_depends (client, filters, package_ids, FALSE, error);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_details:
 **/
static gboolean
pk_console_get_details (PkClient *client, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), package, error);
	if (package_id == NULL) {
		*error = g_error_new (1, 0, "%s %s", _("Could not find details for"), package);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_details (client, package_ids, error);
	g_strfreev (package_ids);
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
	gchar **package_ids;
	GError *error_local = NULL;

	package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), package, &error_local);
	if (package_id == NULL) {
		*error = g_error_new (1, 0, "%s (%s)", _("Could not find the files for this package"), error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_files (client, package_ids, error);
	if (!ret) {
		*error = g_error_new (1, 0, "%s (%s)", _("Could not get the file list"), error_local->message);
		g_error_free (error_local);
	}
	g_strfreev (package_ids);
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
	gchar **package_ids;
	package_id = pk_console_perhaps_resolve (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package, error);
	if (package_id == NULL) {
		*error = g_error_new (1, 0, "%s %s", _("Could not find the update details for"), package);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_update_detail (client, package_ids, error);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_error_code_cb:
 **/
static void
pk_console_error_code_cb (PkClient *client, PkErrorCodeEnum error_code, const gchar *details, gpointer data)
{
	gboolean ret;
	PkRoleEnum role;
	GError *error = NULL;

	pk_client_get_role (client, &role, NULL, NULL);

	/* handled */
	if (need_requeue) {
		if (error_code == PK_ERROR_ENUM_GPG_FAILURE ||
		    error_code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT) {
			egg_debug ("ignoring %s error as handled", pk_error_enum_to_text (error_code));
			return;
		}
		egg_warning ("set requeue, but did not handle error");
	}

	/* do we need to do the untrusted action */
	if (role == PK_ROLE_ENUM_INSTALL_FILES &&
	    error_code == PK_ERROR_ENUM_MISSING_GPG_SIGNATURE && trusted) {
		egg_debug ("need to try again with trusted FALSE");
		trusted = FALSE;
		ret = pk_client_install_files (client_install_files, trusted, files_cache, &error);
		/* we succeeded, so wait for the requeue */
		if (!ret) {
			egg_warning ("failed to install file second time: %s", error->message);
			g_error_free (error);
		}
		need_requeue = ret;
	}
	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("Error: %s : %s\n", pk_error_enum_to_text (error_code), details);
}

/**
 * pk_console_details_cb:
 **/
static void
pk_console_details_cb (PkClient *client, const PkDetailsObj *details, gpointer data)
{
	/* if on console, clear the progress bar line */
	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("%s\n", _("Package description"));
	g_print ("  package:     '%s-%s.%s'\n", details->id->name, details->id->version, details->id->arch);
	g_print ("  license:     '%s'\n", details->license);
	g_print ("  group:       '%s'\n", pk_group_enum_to_text (details->group));
	g_print ("  description: '%s'\n", details->description);
	g_print ("  size:        '%lu' bytes\n", (long unsigned int) details->size);
	g_print ("  url:         '%s'\n", details->url);
}

/**
 * pk_console_files_cb:
 **/
static void
pk_console_files_cb (PkClient *client, const gchar *package_id,
		     const gchar *filelist, gpointer data)
{
	PkRoleEnum role;
	gchar **filevector;

	/* don't print if we are DownloadPackages */
	pk_client_get_role (client, &role, NULL, NULL);
	if (role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		egg_debug ("ignoring ::files");
		return;
	}

	filevector = g_strsplit (filelist, ";", 0);

	if (awaiting_space) {
		g_print ("\n");
	}

	if (*filevector != NULL) {
		g_print ("%s\n", _("Package files"));
		gchar **current_file = filevector;

		while (*current_file != NULL) {
			g_print ("  %s\n", *current_file);
			current_file++;
		}
	} else {
	    g_print ("%s\n", _("No files"));
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
	gboolean ret;
	GError *error = NULL;

	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("%s\n", "Repository signature required");
	g_print ("Package:     %s\n", package_id);
	g_print ("Name:        %s\n", repository_name);
	g_print ("URL:         %s\n", key_url);
	g_print ("User:        %s\n", key_userid);
	g_print ("ID:          %s\n", key_id);
	g_print ("Fingerprint: %s\n", key_fingerprint);
	g_print ("Timestamp:   %s\n", key_timestamp);

	/* get user input */
	import = pk_console_get_prompt (_("Okay to import key?"), FALSE);
	if (!import) {
		need_requeue = FALSE;
		g_print ("%s\n", _("Did not import key"));
		return;
	}

	/* install signature */
	egg_debug ("install signature %s", key_id);
	ret = pk_client_install_signature (client_signature, PK_SIGTYPE_ENUM_GPG,
					   key_id, package_id, &error);
	/* we succeeded, so wait for the requeue */
	if (!ret) {
		egg_warning ("failed to install signature: %s", error->message);
		g_error_free (error);
		return;
	}

	/* we imported a signature */
	need_requeue = TRUE;
}

/**
 * pk_console_eula_required_cb:
 **/
static void
pk_console_eula_required_cb (PkClient *client, const gchar *eula_id, const gchar *package_id,
			     const gchar *vendor_name, const gchar *license_agreement, gpointer data)
{
	gboolean import;
	gboolean ret;
	GError *error = NULL;

	if (awaiting_space) {
		g_print ("\n");
	}
	g_print ("%s\n", "Eula required");
	g_print ("Eula:        %s\n", eula_id);
	g_print ("Package:     %s\n", package_id);
	g_print ("Vendor:      %s\n", vendor_name);
	g_print ("Agreement:   %s\n", license_agreement);

	/* get user input */
	import = pk_console_get_prompt (_("Do you agree?"), FALSE);
	if (!import) {
		need_requeue = FALSE;
		g_print ("%s\n", _("Did not agree to licence, task will fail"));
		return;
	}

	/* accept eula */
	egg_debug ("accept eula %s", eula_id);
	ret = pk_client_accept_eula (client_signature, eula_id, &error);
	/* we succeeded, so wait for the requeue */
	if (!ret) {
		egg_warning ("failed to accept eula: %s", error->message);
		g_error_free (error);
		return;
	}

	/* we accepted eula */
	need_requeue = TRUE;
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, gpointer data)
{
	/* if the daemon crashed, don't hang around */
	if (awaiting_space) {
		g_print ("\n");
	}
	if (!connected) {
		g_print ("%s\n", _("The daemon crashed mid-transaction!"));
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
	egg_debug ("Handling SIGINT");

	/* restore default ASAP, as the cancels might hang */
	signal (SIGINT, SIG_DFL);

	/* cancel any tasks */
	pk_client_get_role (client, &role, NULL, NULL);
	if (role != PK_ROLE_ENUM_UNKNOWN) {
		ret = pk_client_cancel (client, &error);
		if (!ret) {
			egg_warning ("failed to cancel normal client: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}
	pk_client_get_role (client_task, &role, NULL, NULL);
	if (role != PK_ROLE_ENUM_UNKNOWN) {
		ret = pk_client_cancel (client_task, &error);
		if (!ret) {
			egg_warning ("failed to cancel task client: %s", error->message);
			g_error_free (error);
		}
	}

	/* kill ourselves */
	egg_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/**
 * pk_console_get_summary:
 **/
static gchar *
pk_console_get_summary (PkBitfield roles)
{
	GString *string;
	string = g_string_new ("");

	/* header */
	g_string_append_printf (string, "%s\n\n%s\n", _("PackageKit Console Interface"), _("Subcommands:"));

	/* always */
	g_string_append_printf (string, "  %s\n", "get-actions");
	g_string_append_printf (string, "  %s\n", "get-groups");
	g_string_append_printf (string, "  %s\n", "get-filters");
	g_string_append_printf (string, "  %s\n", "get-transactions");
	g_string_append_printf (string, "  %s\n", "get-time");

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_NAME) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_DETAILS) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE)) {
		g_string_append_printf (string, "  %s\n", "search [name|details|group|file] [data]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_PACKAGES) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_FILES)) {
		g_string_append_printf (string, "  %s\n", "install [packages|files]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES)) {
		g_string_append_printf (string, "  %s\n", "download [directory] [packages]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_SIGNATURE)) {
		g_string_append_printf (string, "  %s\n", "install-sig [type] [key_id] [package_id]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REMOVE_PACKAGES)) {
		g_string_append_printf (string, "  %s\n", "remove [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		g_string_append_printf (string, "  %s\n", "update <package>");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REFRESH_CACHE)) {
		g_string_append_printf (string, "  %s\n", "refresh");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_RESOLVE)) {
		g_string_append_printf (string, "  %s\n", "resolve [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATES)) {
		g_string_append_printf (string, "  %s\n", "get-updates");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		g_string_append_printf (string, "  %s\n", "get-depends [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REQUIRES)) {
		g_string_append_printf (string, "  %s\n", "get-requires [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DETAILS)) {
		g_string_append_printf (string, "  %s\n", "get-details [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES)) {
		g_string_append_printf (string, "  %s\n", "get-distro-upgrades");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_FILES)) {
		g_string_append_printf (string, "  %s\n", "get-files [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL)) {
		g_string_append_printf (string, "  %s\n", "get-update-detail [package]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_PACKAGES)) {
		g_string_append_printf (string, "  %s\n", "get-packages");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST)) {
		g_string_append_printf (string, "  %s\n", "repo-list");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_ENABLE)) {
		g_string_append_printf (string, "  %s\n", "repo-enable [repo_id]");
		g_string_append_printf (string, "  %s\n", "repo-disable [repo_id]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_SET_DATA)) {
		g_string_append_printf (string, "  %s\n", "repo-set-data [repo_id] [parameter] [value];");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_WHAT_PROVIDES)) {
		g_string_append_printf (string, "  %s\n", "what-provides [search]");
	}
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_ACCEPT_EULA)) {
		g_string_append_printf (string, "  %s\n", "accept-eula [eula-id]");
	}
	return g_string_free (string, FALSE);
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
	GOptionContext *context;
	gchar *options_help;
	gchar *filter = NULL;
	gchar *summary;
	gchar **package_ids;
	gboolean ret;
	const gchar *mode;
	const gchar *value = NULL;
	const gchar *details = NULL;
	const gchar *parameter = NULL;
	PkBitfield groups;
	gchar *text;
	ret = FALSE;
	gboolean maybe_sync = TRUE;
	PkBitfield filters = 0;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			_("Show the program version and exit"), NULL},
		{ "filter", '\0', 0, G_OPTION_ARG_STRING, &filter,
			_("Set the filter, e.g. installed"), NULL},
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
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error (_("Could not connect to system DBUS."));
	}

	/* we need the roles early, as we only show the user only what they can do */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	summary = pk_console_get_summary (roles);

	context = g_option_context_new ("PackageKit Console Program");
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	/* we are now parsed */
	egg_debug_init (verbose);

	if (program_version) {
		g_print (VERSION "\n");
		return 0;
	}

	if (argc < 2) {
		g_print ("%s", options_help);
		return 1;
	}

	loop = g_main_loop_new (NULL, FALSE);

	pconnection = pk_connection_new ();
	g_signal_connect (pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), loop);

	client = pk_client_new ();
	pk_client_set_use_buffer (client, TRUE, NULL);
	g_signal_connect (client, "package",
			  G_CALLBACK (pk_console_package_cb), NULL);
	g_signal_connect (client, "transaction",
			  G_CALLBACK (pk_console_transaction_cb), NULL);
	g_signal_connect (client, "distro-upgrade",
			  G_CALLBACK (pk_console_distro_upgrade_cb), NULL);
	g_signal_connect (client, "details",
			  G_CALLBACK (pk_console_details_cb), NULL);
	g_signal_connect (client, "files",
			  G_CALLBACK (pk_console_files_cb), NULL);
	g_signal_connect (client, "repo-signature-required",
			  G_CALLBACK (pk_console_repo_signature_required_cb), NULL);
	g_signal_connect (client, "eula-required",
			  G_CALLBACK (pk_console_eula_required_cb), NULL);
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

	client_install_files = pk_client_new ();
	g_signal_connect (client_install_files, "finished",
			  G_CALLBACK (pk_console_install_files_finished_cb), NULL);
	g_signal_connect (client_install_files, "error-code",
			  G_CALLBACK (pk_console_error_code_cb), NULL);

	client_signature = pk_client_new ();
	g_signal_connect (client_signature, "finished",
			  G_CALLBACK (pk_console_signature_finished_cb), NULL);

	if (filter != NULL)
		filters = pk_filter_bitfield_from_text (filter);
	egg_debug ("filter=%s, filters=%" PK_BITFIELD_FORMAT, filter, filters);

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
			error = g_error_new (1, 0, "%s", _("You need to specify a search type, e.g. name"));
			goto out;

		} else if (strcmp (value, "name") == 0) {
			if (details == NULL) {
				error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
				goto out;
			}
			ret = pk_client_search_name (client, filters, details, &error);

		} else if (strcmp (value, "details") == 0) {
			if (details == NULL) {
				error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
				goto out;
			}
			ret = pk_client_search_details (client, filters, details, &error);

		} else if (strcmp (value, "group") == 0) {
			if (details == NULL) {
				error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
				goto out;
			}
			ret = pk_client_search_group (client, filters, details, &error);

		} else if (strcmp (value, "file") == 0) {
			if (details == NULL) {
				error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
				goto out;
			}
			ret = pk_client_search_file (client, filters, details, &error);
		} else {
			error = g_error_new (1, 0, "%s", _("Invalid search type"));
		}

	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a package or file to install"));
			goto out;
		}
		ret = pk_console_install_stuff (client, argv, &error);

	} else if (strcmp (mode, "install-sig") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a type, key_id and package_id"));
			goto out;
		}
		ret = pk_client_install_signature (client, PK_SIGTYPE_ENUM_GPG, details, parameter, &error);

	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a package to remove"));
			goto out;
		}
		ret = pk_console_remove_packages (client, argv, &error);
	} else if (strcmp (mode, "download") == 0) {
		if (value == NULL || details == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify the destination directory and then the packages to download"));
			goto out;
		}
		ret = g_file_test (value, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR);
		if (!ret) {
			error = g_error_new (1, 0, "%s: %s", _("Directory not found"), value);
			goto out;
		}
		ret = pk_console_download_packages (client, argv, value, &error);
	} else if (strcmp (mode, "accept-eula") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a eula-id"));
			goto out;
		}
		ret = pk_client_accept_eula (client, value, &error);
		maybe_sync = FALSE;

	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			/* do the system update */
			ret = pk_client_update_system (client, &error);
		} else {
			ret = pk_console_update_package (client, value, &error);
		}

	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a package name to resolve"));
			goto out;
		}
		package_ids = pk_package_ids_from_id (value);
		ret = pk_client_resolve (client, filters, package_ids, &error);
		g_strfreev (package_ids);

	} else if (strcmp (mode, "repo-enable") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a repo name"));
			goto out;
		}
		ret = pk_client_repo_enable (client, value, TRUE, &error);

	} else if (strcmp (mode, "repo-disable") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a repo name"));
			goto out;
		}
		ret = pk_client_repo_enable (client, value, FALSE, &error);

	} else if (strcmp (mode, "repo-set-data") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a repo name/parameter and value"));
			goto out;
		}
		ret = pk_client_repo_set_data (client, value, details, parameter, &error);

	} else if (strcmp (mode, "repo-list") == 0) {
		ret = pk_client_get_repo_list (client, filters, &error);

	} else if (strcmp (mode, "get-time") == 0) {
		PkRoleEnum role;
		guint time;
		gboolean ret;
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a time term"));
			goto out;
		}
		role = pk_role_enum_from_text (value);
		if (role == PK_ROLE_ENUM_UNKNOWN) {
			error = g_error_new (1, 0, "%s", _("You need to specify a correct role"));
			goto out;
		}
		ret = pk_control_get_time_since_action (control, role, &time, &error);
		if (!ret) {
			error = g_error_new (1, 0, "%s", _("Failed to get last time"));
			goto out;
		}
		g_print ("time since %s is %is\n", value, time);
		maybe_sync = FALSE;

	} else if (strcmp (mode, "get-depends") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
			goto out;
		}
		ret = pk_console_get_depends (client, filters, value, &error);

	} else if (strcmp (mode, "get-distro-upgrades") == 0) {
		ret = pk_client_get_distro_upgrades (client, &error);

	} else if (strcmp (mode, "get-update-detail") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
			goto out;
		}
		ret = pk_console_get_update_detail (client, value, &error);

	} else if (strcmp (mode, "get-requires") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
			goto out;
		}
		ret = pk_console_get_requires (client, filters, value, &error);

	} else if (strcmp (mode, "what-provides") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a search term"));
			goto out;
		}
		ret = pk_client_what_provides (client, filters, PK_PROVIDES_ENUM_CODEC, value, &error);

	} else if (strcmp (mode, "get-details") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a package to find the details for"));
			goto out;
		}
		ret = pk_console_get_details (client, value, &error);

	} else if (strcmp (mode, "get-files") == 0) {
		if (value == NULL) {
			error = g_error_new (1, 0, "%s", _("You need to specify a package to find the files for"));
			goto out;
		}
		ret = pk_console_get_files (client, value, &error);

	} else if (strcmp (mode, "get-updates") == 0) {
		ret = pk_client_get_updates (client, filters, &error);

	} else if (strcmp (mode, "get-packages") == 0) {
		ret = pk_client_get_packages (client, filters, &error);

	} else if (strcmp (mode, "get-actions") == 0) {
		text = pk_role_bitfield_to_text (roles);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		maybe_sync = FALSE;
		/* these can never fail */
		ret = TRUE;

	} else if (strcmp (mode, "get-filters") == 0) {
		filters = pk_control_get_filters (control, NULL);
		text = pk_filter_bitfield_to_text (filters);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		maybe_sync = FALSE;
		/* these can never fail */
		ret = TRUE;

	} else if (strcmp (mode, "get-groups") == 0) {
		groups = pk_control_get_groups (control, NULL);
		text = pk_group_bitfield_to_text (groups);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		maybe_sync = FALSE;
		/* these can never fail */
		ret = TRUE;

	} else if (strcmp (mode, "get-transactions") == 0) {
		ret = pk_client_get_old_transactions (client, 10, &error);

	} else if (strcmp (mode, "refresh") == 0) {
		/* special case - this takes a long time, and doesn't do packages */
		pk_console_start_bar ("refresh-cache");
		ret = pk_client_refresh_cache (client, FALSE, &error);

	} else {
		error = g_error_new (1, 0, _("Option '%s' not supported"), mode);
	}

	/* do we wait for the method? */
	if (maybe_sync && !nowait && ret) {
		g_main_loop_run (loop);
	}

out:
	if (!ret) {
		if (error == NULL) {
			g_print ("Command failed in an unknown way. Please report!\n");
		} else if (g_str_has_prefix (error->message, "org.freedesktop.packagekit."))  {
			g_print ("%s\n", _("You don't have the necessary privileges for this operation"));
		} else {
			g_print ("%s:  %s\n", _("Command failed"), error->message);
			g_error_free (error);
		}
	}

	g_free (options_help);
	g_free (filter);
	g_free (summary);
	g_strfreev (files_cache);
	g_object_unref (control);
	g_object_unref (client);
	g_object_unref (client_task);
	g_object_unref (client_install_files);
	g_object_unref (client_signature);

	return 0;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_console (EggTest *test)
{
	gchar *text_safe;

	if (!egg_test_start (test, "PkConsole"))
		return;

	/************************************************************
	 ****************         Padding          ******************
	 ************************************************************/
	egg_test_title (test, "pad smaller");
	text_safe = pk_strpad ("richard", 10);
	if (egg_strequal (text_safe, "richard   "))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad NULL");
	text_safe = pk_strpad (NULL, 10);
	if (egg_strequal (text_safe, "          "))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad nothing");
	text_safe = pk_strpad ("", 10);
	if (egg_strequal (text_safe, "          "))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad over");
	text_safe = pk_strpad ("richardhughes", 10);
	if (egg_strequal (text_safe, "richardhughes"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad zero");
	text_safe = pk_strpad ("rich", 0);
	if (egg_strequal (text_safe, "rich"))
		egg_test_success (test, NULL);
	else {
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	}
	g_free (text_safe);
	egg_test_end (test);
}
#endif

