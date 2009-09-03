/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

//#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>
#include <sys/types.h>
#include <pwd.h>
#include <locale.h>

#include "egg-debug.h"
//#include "egg-string.h"

#include "pk-text.h"
#include "pk-progress-bar.h"

#define PK_EXIT_CODE_SYNTAX_INVALID	3
#define PK_EXIT_CODE_FILE_NOT_FOUND	4

static GMainLoop *loop = NULL;
static PkBitfield roles;
static gboolean is_console = FALSE;
static gboolean nowait = FALSE;
static PkControlSync *control = NULL;
static PkTask *task = NULL;
PkProgressBar *progressbar = NULL;

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
 * pk_package_id_get_printable:
 **/
static gchar *
pk_package_id_get_printable (const gchar *package_id)
{
	gchar **split = NULL;
	gchar *value = NULL;

	/* invalid */
	if (package_id == NULL)
		goto out;

	/* split */
	split = g_strsplit (package_id, ";", -1);
	if (g_strv_length (split) != 4)
		goto out;
	value = g_strdup_printf ("%s-%s.%s", split[0], split[1], split[2]);
out:
	g_strfreev (split);
	return value;
}

/**
 * pk_console_package_cb:
 **/
static void
pk_console_package_cb (const PkResultItemPackage *obj, gpointer data)
{
	gchar *package = NULL;
	gchar *package_pad = NULL;
	gchar *info_pad = NULL;
	gchar **split = NULL;

	/* ignore finished */
	if (obj->info_enum == PK_INFO_ENUM_FINISHED)
		goto out;

	/* split */
	split = g_strsplit (obj->package_id, ";", -1);
	if (g_strv_length (split) != 4)
		goto out;

	/* make these all the same length */
	info_pad = pk_strpad (pk_info_enum_to_text (obj->info_enum), 12);

	/* create printable */
	package = pk_package_id_get_printable (obj->package_id);

	/* don't pretty print */
	if (!is_console) {
		g_print ("%s %s\n", info_pad, package);
		goto out;
	}

	/* pad the name-version */
	package_pad = pk_strpad (package, 40);
	g_print ("%s\t%s\t%s\n", info_pad, package_pad, obj->summary);
out:
	/* free all the data */
	g_free (package);
	g_free (package_pad);
	g_free (info_pad);
	g_strfreev (split);
}

/**
 * pk_console_transaction_cb:
 **/
static void
pk_console_transaction_cb (const PkResultItemTransaction *obj, gpointer user_data)
{
	struct passwd *pw;
	const gchar *role_text;
	gchar **lines;
	gchar **parts;
	guint i, lines_len;
	gchar *package = NULL;

	role_text = pk_role_enum_to_text (obj->role);
	/* TRANSLATORS: this is an atomic transaction */
	g_print ("%s: %s\n", _("Transaction"), obj->tid);
	/* TRANSLATORS: this is the time the transaction was started in system timezone */
	g_print (" %s: %s\n", _("System time"), obj->timespec);
	/* TRANSLATORS: this is if the transaction succeeded or not */
	g_print (" %s: %s\n", _("Succeeded"), obj->timespec ? _("True") : _("False"));
	/* TRANSLATORS: this is the transactions role, e.g. "update-system" */
	g_print (" %s: %s\n", _("Role"), role_text);

	/* only print if not null */
	if (obj->duration > 0) {
		/* TRANSLATORS: this is The duration of the transaction */
		g_print (" %s: %i %s\n", _("Duration"), obj->duration, _("(seconds)"));
	}

	/* TRANSLATORS: this is The command line used to do the action */
	g_print (" %s: %s\n", _("Command line"), obj->cmdline);
	/* TRANSLATORS: this is the user ID of the user that started the action */
	g_print (" %s: %i\n", _("User ID"), obj->uid);

	/* query real name */
	pw = getpwuid (obj->uid);
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
	lines = g_strsplit (obj->data, "\n", -1);
	lines_len = g_strv_length (lines);
	if (lines_len > 0)
		g_print (" %s\n", _("Affected packages:"));
	else
		g_print (" %s\n", _("Affected packages: None"));
	for (i=0; i<lines_len; i++) {
		parts = g_strsplit (lines[i], "\t", 3);

		/* create printable */
		package = pk_package_id_get_printable (parts[1]);
		g_print (" - %s %s", parts[0], package);
		g_free (package);
		g_strfreev (parts);
	}
	g_strfreev (lines);
}
#if 0

/**
 * pk_console_print_deps_list_info:
 **/
static guint
pk_console_print_deps_list_info (PkPackageSack *list, PkInfoEnum info, const gchar *header)
{
	const PkResultItemPackage *obj;
	gboolean ret = FALSE;
	guint found = 0;
	guint i;
	guint length;

	length = pk_package_list_get_size (list);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (list, i);

		/* are we interested in this type */
		if (obj->info != info)
			continue;

		/* is this package already local */
		if (g_strcmp0 (obj->id->data, "local") == 0)
			continue;

		/* print header if it's not been done before */
		if (!ret) {
			g_print ("%s\n", header);
			ret = TRUE;
		}

		/* print package */
		g_print ("%i\t%s\n", ++found, package);
	}
	return found;
}

/**
 * pk_console_print_deps_list:
 **/
static guint
pk_console_print_deps_list (PkPackageSack *list)
{
	guint found = 0;

	/* TRANSLATORS: When processing, we might have to remove other dependencies */
	found += pk_console_print_deps_list_info (list, PK_INFO_ENUM_REMOVING, _("The following packages have to be removed:"));

	/* TRANSLATORS: When processing, we might have to install other dependencies */
	found += pk_console_print_deps_list_info (list, PK_INFO_ENUM_INSTALLING, _("The following packages have to be installed:"));

	/* TRANSLATORS: When processing, we might have to update other dependencies */
	found += pk_console_print_deps_list_info (list, PK_INFO_ENUM_UPDATING, _("The following packages have to be updated:"));

	/* TRANSLATORS: When processing, we might have to reinstall other dependencies */
	found += pk_console_print_deps_list_info (list, PK_INFO_ENUM_REINSTALLING, _("The following packages have to be reinstalled:"));

	/* TRANSLATORS: When processing, we might have to downgrade other dependencies */
	found += pk_console_print_deps_list_info (list, PK_INFO_ENUM_DOWNGRADING, _("The following packages have to be downgraded:"));

	return found;
}
#endif

/**
 * pk_console_distro_upgrade_cb:
 **/
static void
pk_console_distro_upgrade_cb (const PkResultItemDistroUpgrade *obj, gpointer user_data)
{
	/* TRANSLATORS: this is the distro, e.g. Fedora 10 */
	g_print ("%s: %s\n", _("Distribution"), obj->name);
	/* TRANSLATORS: this is type of update, stable or testing */
	g_print (" %s: %s\n", _("Type"), pk_update_state_enum_to_text (obj->state));
	/* TRANSLATORS: this is any summary text describing the upgrade */
	g_print (" %s: %s\n", _("Summary"), obj->summary);
}

/**
 * pk_console_category_cb:
 **/
static void
pk_console_category_cb (const PkResultItemCategory *obj, gpointer user_data)
{
	/* TRANSLATORS: this is the group category name */
	g_print ("%s: %s\n", _("Category"), obj->name);
	/* TRANSLATORS: this is group identifier */
	g_print (" %s: %s\n", _("ID"), obj->cat_id);
	if (obj->parent_id != NULL) {
		/* TRANSLATORS: this is the parent group */
		g_print (" %s: %s\n", _("Parent"), obj->parent_id);
	}
	/* TRANSLATORS: this is the name of the parent group */
	g_print (" %s: %s\n", _("Name"), obj->name);
	if (obj->summary != NULL) {
		/* TRANSLATORS: this is the summary of the group */
		g_print (" %s: %s\n", _("Summary"), obj->summary);
	}
	/* TRANSLATORS: this is preferred icon for the group */
	g_print (" %s: %s\n", _("Icon"), obj->icon);
}

/**
 * pk_console_update_detail_cb:
 **/
static void
pk_console_update_detail_cb (const PkResultItemUpdateDetail *detail, gpointer data)
{
	gchar *issued;
	gchar *updated;
	gchar *package = NULL;

	/* TRANSLATORS: this is a header for the package that can be updated */
	g_print ("%s\n", _("Details about the update:"));

	/* create printable */
	package = pk_package_id_get_printable (detail->package_id);

	/* TRANSLATORS: details about the update, package name and version */
	g_print (" %s: %s\n", _("Package"), package);
	if (detail->updates != NULL) {
		/* TRANSLATORS: details about the update, any packages that this update updates */
		g_print (" %s: %s\n", _("Updates"), detail->updates);
	}
	if (detail->obsoletes != NULL) {
		/* TRANSLATORS: details about the update, any packages that this update obsoletes */
		g_print (" %s: %s\n", _("Obsoletes"), detail->obsoletes);
	}
	if (detail->vendor_url != NULL) {
		/* TRANSLATORS: details about the update, the vendor URLs */
		g_print (" %s: %s\n", _("Vendor"), detail->vendor_url);
	}
	if (detail->bugzilla_url != NULL) {
		/* TRANSLATORS: details about the update, the bugzilla URLs */
		g_print (" %s: %s\n", _("Bugzilla"), detail->bugzilla_url);
	}
	if (detail->cve_url != NULL) {
		/* TRANSLATORS: details about the update, the CVE URLs */
		g_print (" %s: %s\n", _("CVE"), detail->cve_url);
	}
	if (detail->restart_enum != PK_RESTART_ENUM_NONE) {
		/* TRANSLATORS: details about the update, if the package requires a restart */
		g_print (" %s: %s\n", _("Restart"), pk_restart_enum_to_text (detail->restart_enum));
	}
	if (detail->update_text != NULL) {
		/* TRANSLATORS: details about the update, any description of the update */
		g_print (" %s: %s\n", _("Update text"), detail->update_text);
	}
	if (detail->changelog != NULL) {
		/* TRANSLATORS: details about the update, the changelog for the package */
		g_print (" %s: %s\n", _("Changes"), detail->changelog);
	}
	if (detail->state_enum != PK_UPDATE_STATE_ENUM_UNKNOWN) {
		/* TRANSLATORS: details about the update, the ongoing state of the update */
		g_print (" %s: %s\n", _("State"), pk_update_state_enum_to_text (detail->state_enum));
	}
	issued = pk_iso8601_from_date (detail->issued);
	if (issued != NULL) {
		/* TRANSLATORS: details about the update, date the update was issued */
		g_print (" %s: %s\n", _("Issued"), issued);
	}
	updated = pk_iso8601_from_date (detail->updated);
	if (updated != NULL) {
		/* TRANSLATORS: details about the update, date the update was updated */
		g_print (" %s: %s\n", _("Updated"), updated);
	}
	g_free (issued);
	g_free (updated);
	g_free (package);
}

/**
 * pk_console_repo_detail_cb:
 **/
static void
pk_console_repo_detail_cb (const PkResultItemRepoDetail *obj, gpointer data)
{
	gchar *enabled_pad;
	gchar *repo_pad;

	if (obj->enabled) {
		/* TRANSLATORS: if the repo is enabled */
		enabled_pad = pk_strpad (_("Enabled"), 10);
	} else {
		/* TRANSLATORS: if the repo is disabled */
		enabled_pad = pk_strpad (_("Disabled"), 10);
	}

	repo_pad = pk_strpad (obj->repo_id, 25);
	g_print (" %s %s %s\n", enabled_pad, repo_pad, obj->description);
	g_free (enabled_pad);
	g_free (repo_pad);
}

/**
 * pk_console_require_restart_cb:
 **/
static void
pk_console_require_restart_cb (const PkResultItemRequireRestart *obj, gpointer data)
{
	gchar *package = NULL;

	/* create printable */
	package = pk_package_id_get_printable (obj->package_id);

	if (obj->restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: a package requires the system to be restarted */
		g_print ("%s %s\n", _("System restart required by:"), package);
	} else if (obj->restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: a package requires the session to be restarted */
		g_print ("%s %s\n", _("Session restart required:"), package);
	} else if (obj->restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: a package requires the system to be restarted due to a security update*/
		g_print ("%s %s\n", _("System restart (security) required by:"), package);
	} else if (obj->restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: a package requires the session to be restarted due to a security update */
		g_print ("%s %s\n", _("Session restart (security) required:"), package);
	} else if (obj->restart == PK_RESTART_ENUM_APPLICATION) {
		/* TRANSLATORS: a package requires the application to be restarted */
		g_print ("%s %s\n", _("Application restart required by:"), package);
	}
	g_free (package);
}

/**
 * pk_console_details_cb:
 **/
static void
pk_console_details_cb (const PkResultItemDetails *obj, gpointer data)
{
	gchar *package = NULL;

	/* create printable */
	package = pk_package_id_get_printable (obj->package_id);

	/* TRANSLATORS: This a list of details about the package */
	g_print ("%s\n", _("Package description"));
	g_print ("  package:     %s\n", package);
	g_print ("  license:     %s\n", obj->license);
	g_print ("  group:       %s\n", pk_group_enum_to_text (obj->group_enum));
	g_print ("  description: %s\n", obj->description);
	g_print ("  size:        %lu bytes\n", (long unsigned int) obj->size);
	g_print ("  url:         %s\n", obj->url);

	g_free (package);
}

/**
 * pk_console_message_cb:
 **/
static void
pk_console_message_cb (const PkResultItemMessage *obj, gpointer data)
{
	/* TRANSLATORS: This a message (like a little note that may be of interest) from the transaction */
	g_print ("%s %s: %s\n", _("Message:"), pk_message_enum_to_text (obj->message), obj->details);
}

/**
 * pk_console_files_cb:
 **/
static void
pk_console_files_cb (PkResultItemFiles *obj, gpointer data)
{
	guint i;
#if 0
	PkRoleEnum role;

	/* don't print if we are DownloadPackages */
	pk_client_get_role (PK_CLIENT(task), &role, NULL, NULL);
	if (role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		egg_debug ("ignoring ::files");
		return;
	}
#endif
	/* empty */
	if (obj->files == NULL || obj->files[0] == NULL) {
		/* TRANSLATORS: This where the package has no files */
		g_print ("%s\n", _("No files"));
		return;
	}

	/* TRANSLATORS: This a list files contained in the package */
	g_print ("%s\n", _("Package files"));
	for (i=0; obj->files[i] != NULL; i++) {
		g_print ("  %s\n", obj->files[i]);
	}
}

/**
 * pk_console_repo_signature_required_cb:
 **/
static void
pk_console_repo_signature_required_cb (const PkResultItemRepoSignatureRequired *obj, gpointer data)
{
//	gboolean import;
//	gboolean ret;
//	GError *error = NULL;
	gchar *package = NULL;

	/* create printable */
	package = pk_package_id_get_printable (obj->package_id);

	/* TRANSLATORS: This a request for a GPG key signature from the backend, which the client will prompt for later */
	g_print ("%s\n", _("Repository signature required"));
	g_print ("Package:     %s\n", package);
	g_print ("Name:        %s\n", obj->repository_name);
	g_print ("URL:         %s\n", obj->key_url);
	g_print ("User:        %s\n", obj->key_userid);
	g_print ("ID:          %s\n", obj->key_id);
	g_print ("Fingerprint: %s\n", obj->key_fingerprint);
	g_print ("Timestamp:   %s\n", obj->key_timestamp);

#if 0
	/* TRANSLATORS: This a prompt asking the user to import the security key */
	import = pk_console_get_prompt (_("Do you accept this signature?"), FALSE);
	if (!import) {
		need_requeue = FALSE;
		/* TRANSLATORS: This is where the user declined the security key */
		g_print ("%s\n", _("The signature was not accepted."));
		return;
	}

	/* install signature */
	egg_debug ("install signature %s", key_id);
	ret = pk_client_install_signature (client_secondary, PK_SIGTYPE_ENUM_GPG,
					   key_id, package_id, &error);
	/* we succeeded, so wait for the requeue */
	if (!ret) {
		egg_warning ("failed to install signature: %s", error->message);
		g_error_free (error);
		return;
	}
#endif

	g_free (package);
}

/**
 * pk_console_eula_required_cb:
 **/
static void
pk_console_eula_required_cb (const PkResultItemEulaRequired *obj, gpointer data)
{
//	gboolean import;
//	gboolean ret;
//	GError *error = NULL;
	gchar *package = NULL;

	/* create printable */
	package = pk_package_id_get_printable (obj->package_id);

	/* TRANSLATORS: This a request for a EULA */
	g_print ("%s\n", _("End user license agreement required"));
	g_print ("Eula:        %s\n", obj->eula_id);
	g_print ("Package:     %s\n", package);
	g_print ("Vendor:      %s\n", obj->vendor_name);
	g_print ("Agreement:   %s\n", obj->license_agreement);

#if 0
	/* TRANSLATORS: This a prompt asking the user to agree to the license */
	import = pk_console_get_prompt (_("Do you agree to this license?"), FALSE);
	if (!import) {
		need_requeue = FALSE;
		/* TRANSLATORS: This is where the user declined the license */
		g_print ("%s\n", _("The license was refused."));
		return;
	}

	/* accept eula */
	egg_debug ("accept eula %s", eula_id);
	ret = pk_client_accept_eula (client_secondary, eula_id, &error);
	/* we succeeded, so wait for the requeue */
	if (!ret) {
		egg_warning ("failed to accept eula: %s", error->message);
		g_error_free (error);
		return;
	}

	/* we accepted eula */
	need_requeue = TRUE;
#endif

	g_free (package);
}

#if 0
/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (PkExitEnum exit_enum, guint runtime, gpointer data)
{
	PkRoleEnum role;
	const gchar *role_text;
	gfloat time_s;
	PkRestartEnum restart;
	gboolean ret;
	GError *error = NULL;

	pk_client_get_role (PK_CLIENT(task), &role, NULL, NULL);

	role_text = pk_role_enum_to_text (role);
	time_s = (gfloat) runtime / 1000.0;

	/* do we need to new line? */
	egg_debug ("%s runtime was %.1f seconds", role_text, time_s);

	/* is there any restart to notify the user? */
	restart = pk_client_get_require_restart (client);
	if (restart == PK_RESTART_ENUM_SYSTEM) {
		/* TRANSLATORS: a package needs to restart their system */
		g_print ("%s\n", _("Please restart the computer to complete the update."));
	} else if (restart == PK_RESTART_ENUM_SESSION) {
		/* TRANSLATORS: a package needs to restart the session */
		g_print ("%s\n", _("Please logout and login to complete the update."));
	} else if (restart == PK_RESTART_ENUM_APPLICATION) {
		/* TRANSLATORS: a package needs to restart the application */
		g_print ("%s\n", _("Please restart the application as it is being used."));
	} else if (restart == PK_RESTART_ENUM_SECURITY_SYSTEM) {
		/* TRANSLATORS: a package needs to restart their system (due to security) */
		g_print ("%s\n", _("Please restart the computer to complete the update as important security updates have been installed."));
	} else if (restart == PK_RESTART_ENUM_SECURITY_SESSION) {
		/* TRANSLATORS: a package needs to restart the session (due to security) */
		g_print ("%s\n", _("Please logout and login to complete the update as important security updates have been installed."));
	}

	/* need to handle retry with only_trusted=FALSE */
	if (exit_enum == PK_EXIT_ENUM_NEED_UNTRUSTED) {
		egg_debug ("need to handle untrusted");

		/* retry new action with untrusted */
		pk_client_set_only_trusted (PK_CLIENT(task), FALSE);
		ret = pk_client_requeue (PK_CLIENT(task), &error);
		if (!ret) {
			egg_warning ("Failed to requeue: %s", error->message);
			g_error_free (error);
		}
		return;
	}

	if ((role == PK_ROLE_ENUM_INSTALL_FILES || role == PK_ROLE_ENUM_INSTALL_PACKAGES) &&
	    exit_enum == PK_EXIT_ENUM_FAILED && need_requeue) {
		egg_warning ("waiting for second install file to finish");
		return;
	}

	/* have we failed to install, and the gpg key is now installed */
	if (exit_enum == PK_EXIT_ENUM_KEY_REQUIRED && need_requeue) {
		egg_debug ("key now installed");
		return;
	}

	/* have we failed to install, and the eula key is now installed */
	if (exit_enum == PK_EXIT_ENUM_EULA_REQUIRED && need_requeue) {
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
pk_console_perhaps_resolve (PkBitfield filter, const gchar *package, GError **error)
{
	PkPackageSack *list;
	gchar *package_id = NULL;
	gboolean valid;

	/* have we passed a complete package_id? */
	valid = pk_package_id_check (package);
	if (valid)
		return g_strdup (package);

	/* get the list of possibles */
	list = pk_console_resolve (filter, package, error);
	if (list == NULL)
		goto out;

	/* else list the options if multiple matches found */

	/* ask the user to select the right one */
	package_id = pk_console_resolve_package_id (list, error);
out:
	if (list != NULL)
		g_object_unref (list);
	return package_id;
}

/**
 * pk_console_is_installed:
 **/
static gboolean
pk_console_is_installed (const gchar *package)
{
	PkPackageSack *list;
	GError *error;
	gboolean ret = FALSE;

	/* get the list of possibles */
	list = pk_console_resolve (pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package, &error);
	if (list == NULL) {
		egg_debug ("not installed: %s", error->message);
		g_error_free (error);
		goto out;
	}
	/* true if any installed */
	ret = PK_OBJ_LIST(list)->len > 0;
out:
	if (list != NULL)
		g_object_unref (list);
	return ret;
}

/**
 * pk_console_install_stuff:
 **/
static gboolean
pk_console_install_stuff (gchar **packages, GError **error)
{
	gboolean ret = TRUE;
	gboolean installed;
	gboolean is_local;
	gboolean accept_changes;
	gchar *package_id = NULL;
	gchar **package_ids = NULL;
	gchar **files = NULL;
	guint i;
	guint length;
	PkPackageSack *list;
	PkPackageSack *list_single;
	GPtrArray *array_packages;
	GPtrArray *array_files;
	GError *error_local = NULL;

	array_packages = g_ptr_array_new ();
	array_files = g_ptr_array_new ();
	length = g_strv_length (packages);
	list = pk_package_list_new ();

	for (i=2; i<length; i++) {
		/* are we a local file */
		is_local = g_file_test (packages[i], G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR);
		if (is_local) {
			g_ptr_array_add (array_files, g_strdup (packages[i]));
		} else {
			/* if already installed, then abort */
			installed = pk_console_is_installed (packages[i]);
			if (installed) {
				/* TRANSLATORS: The package is already installed on the system */
				*error = g_error_new (1, 0, _("The package %s is already installed"), packages[i]);
				ret = FALSE;
				break;
			}
			/* try and find a package */
			package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), packages[i], &error_local);
			if (package_id == NULL) {
				/* TRANSLATORS: The package name was not found in any software sources. The detailed error follows */
				*error = g_error_new (1, 0, _("The package %s could not be installed: %s"), packages[i], error_local->message);
				g_error_free (error_local);
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
		package_ids = pk_ptr_array_to_strv (array_packages);

		/* can we simulate? */
		if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES)) {
			ret = pk_client_reset (client_sync, &error_local);
			if (!ret) {
				/* TRANSLATORS: There was a programming error that shouldn't happen. The detailed error follows */
				*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
				g_error_free (error_local);
				goto out;
			}

			egg_debug ("Simulating install for %s", package_ids[0]);
			ret = pk_client_simulate_install_packages (client_sync, package_ids, error);
			if (!ret) {
				egg_warning ("failed to simulate a package install");
				goto out;
			}

			/* see how many packages there are */
			list_single = pk_client_get_package_list (client_sync);
			pk_obj_list_add_list (PK_OBJ_LIST(list), PK_OBJ_LIST(list_single));
			g_object_unref (list_single);

			/* one of the simulate-install-packages failed */
			if (!ret)
				goto out;

			/* if there are no required packages, just do the remove */
			length = pk_package_list_get_size (list);
			if (length != 0) {

				/* print the additional deps to the screen */
				pk_console_print_deps_list (list);

				/* TRANSLATORS: We are checking if it's okay to remove a list of packages */
				accept_changes = pk_console_get_prompt (_("Proceed with changes?"), FALSE);

				/* we chickened out */
				if (!accept_changes) {
					/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
					*error = g_error_new (1, 0, "%s", _("The package install was canceled!"));
					ret = FALSE;
					goto out;
				}
			}
		}

		/* reset */
		ret = pk_client_reset (PK_CLIENT(task), &error_local);
		if (!ret) {
			/* TRANSLATORS: There was a programming error that shouldn't happen. The detailed error follows */
			*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		ret = pk_client_install_packages (PK_CLIENT(task), TRUE, package_ids, &error_local);
		if (!ret) {
			/* TRANSLATORS: There was an error installing the packages. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not install the packages: %s"), error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* any to process? */
	if (array_files->len > 0) {
		/* convert to strv */
		files = pk_ptr_array_to_strv (array_files);

		/* can we simulate? */
		if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SIMULATE_INSTALL_FILES)) {
			ret = pk_client_reset (client_sync, &error_local);
			if (!ret) {
				/* TRANSLATORS: There was a programming error that shouldn't happen. The detailed error follows */
				*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
				g_error_free (error_local);
				goto out;
			}

			egg_debug ("Simulating install for %s", files[0]);
			ret = pk_client_simulate_install_files (client_sync, files, error);
			if (!ret) {
				egg_warning ("failed to simulate a package install");
				goto out;
			}

			/* see how many packages there are */
			list_single = pk_client_get_package_list (client_sync);
			pk_obj_list_add_list (PK_OBJ_LIST(list), PK_OBJ_LIST(list_single));
			g_object_unref (list_single);

			/* one of the simulate-install-files failed */
			if (!ret)
				goto out;

			/* if there are no required packages, just do the remove */
			length = pk_package_list_get_size (list);
			if (length != 0) {

				/* print the additional deps to the screen */
				pk_console_print_deps_list (list);

				/* TRANSLATORS: We are checking if it's okay to remove a list of packages */
				accept_changes = pk_console_get_prompt (_("Proceed with changes?"), FALSE);

				/* we chickened out */
				if (!accept_changes) {
					/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
					*error = g_error_new (1, 0, "%s", _("The package install was canceled!"));
					ret = FALSE;
					goto out;
				}
			}
		}

		ret = pk_client_install_files (PK_CLIENT(task), TRUE, files, &error_local);
		if (!ret) {
			/* TRANSLATORS: There was an error installing the files. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not install the files: %s"), error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

out:
	g_object_unref (list);
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
pk_console_remove_only (gchar **package_ids, gboolean force, GError **error)
{
	gboolean ret;

	egg_debug ("remove+ %s", package_ids[0]);
	ret = pk_client_reset (PK_CLIENT(task), error);
	if (!ret)
		return ret;
	return pk_client_remove_packages (PK_CLIENT(task), package_ids, force, FALSE, error);
}

/**
 * pk_console_remove_packages:
 **/
static gboolean
pk_console_remove_packages (gchar **packages, GError **error)
{
	gchar *package_id;
	gboolean ret = TRUE;
	guint i;
	guint length;
	gboolean remove_deps;
	GPtrArray *array;
	gchar **package_ids = NULL;
	PkPackageSack *list;
	PkPackageSack *list_single;
	GError *error_local = NULL;

	array = g_ptr_array_new ();
	list = pk_package_list_new ();
	length = g_strv_length (packages);
	for (i=2; i<length; i++) {
		package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), packages[i], &error_local);
		if (package_id == NULL) {
			/* TRANSLATORS: The package name was not found in the installed list. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not remove %s: %s"), packages[i], error_local->message);
			g_error_free (error_local);
			ret = FALSE;
			break;
		}
		g_ptr_array_add (array, g_strdup (package_id));
		egg_debug ("resolved to %s", package_id);
		g_free (package_id);
	}

	/* one of the resolves failed */
	if (!ret)
		goto out;

	/* convert to strv */
	package_ids = pk_ptr_array_to_strv (array);

	/* are we dumb and can't check for requires? */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES)) {
		/* no, just try to remove it without deps */
		ret = pk_console_remove_only (PK_CLIENT(task), package_ids, FALSE, &error_local);
		if (!ret) {
			/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not remove the packages: %s"), error_local->message);
			g_error_free (error_local);
		}
		goto out;
	}

	ret = pk_client_reset (client_sync, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was a programming error that shouldn't happen. The detailed error follows */
		*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	egg_debug ("Getting installed requires for %s", package_ids[0]);
	/* see if any packages require this one */
	ret = pk_client_simulate_remove_packages (client_sync, package_ids, error);
	if (!ret) {
		egg_warning ("failed to simulate a package removal");
		goto out;
	}

	/* see how many packages there are */
	list_single = pk_client_get_package_list (client_sync);
	pk_obj_list_add_list (PK_OBJ_LIST(list), PK_OBJ_LIST(list_single));
	g_object_unref (list_single);

	/* one of the simulate-remove-packages failed */
	if (!ret)
		goto out;

	/* if there are no required packages, just do the remove */
	length = pk_package_list_get_size (list);
	if (length == 0) {
		egg_debug ("no requires");
		ret = pk_console_remove_only (PK_CLIENT(task), package_ids, FALSE, &error_local);
		if (!ret) {
			/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not remove the packages: %s"), error_local->message);
			g_error_free (error_local);
		}
		goto out;
	}

	/* present this to the user */

	/* print the additional deps to the screen */
	pk_console_print_deps_list (list);

	/* TRANSLATORS: We are checking if it's okay to remove a list of packages */
	remove_deps = pk_console_get_prompt (_("Proceed with additional packages?"), FALSE);

	/* we chickened out */
	if (!remove_deps) {
		/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
		*error = g_error_new (1, 0, "%s", _("The package removal was canceled!"));
		ret = FALSE;
		goto out;
	}

	/* remove all the stuff */
	ret = pk_console_remove_only (PK_CLIENT(task), package_ids, TRUE, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not remove the packages: %s"), error_local->message);
		g_error_free (error_local);
	}

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
pk_console_download_packages (gchar **packages, const gchar *directory, GError **error)
{
	gboolean ret = TRUE;
	gchar *package_id = NULL;
	gchar **package_ids = NULL;
	guint i;
	guint length;
	GPtrArray *array_packages;
	GError *error_local = NULL;

	array_packages = g_ptr_array_new ();
	length = g_strv_length (packages);
	for (i=3; i<length; i++) {
			package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), packages[i], &error_local);
			if (package_id == NULL) {
				/* TRANSLATORS: The package name was not found in any software sources */
				*error = g_error_new (1, 0, _("This tool could not download the package %s as it could not be found"), packages[i]);
				g_error_free (error_local);
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
		package_ids = pk_ptr_array_to_strv (array_packages);

		/* reset */
		ret = pk_client_reset (PK_CLIENT(task), &error_local);
		if (!ret) {
			/* TRANSLATORS: There was a programming error that shouldn't happen. The detailed error follows */
			*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		ret = pk_client_download_packages (PK_CLIENT(task), package_ids, directory, error);
		if (!ret) {
			/* TRANSLATORS: Could not download the packages for some reason. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not download the packages: %s"), error_local->message);
			g_error_free (error_local);
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
pk_console_update_package (const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	guint length;
	GError *error_local = NULL;
	gboolean accept_changes;
	PkPackageSack *list;
	PkPackageSack *list_single;

	list = pk_package_list_new ();
	package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), package, &error_local);
	if (package_id == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not update %s: %s"), package, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);

	/* are we dumb and can't simulate? */
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES)) {
		/* no, just try to update it without deps */
		ret = pk_client_update_packages (PK_CLIENT(task), TRUE, package_ids, error);
		if (!ret) {
			/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
			*error = g_error_new (1, 0, _("This tool could not update %s: %s"), package, error_local->message);
			g_error_free (error_local);
		}
		goto out;
	}

	ret = pk_client_reset (client_sync, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was a programming error that shouldn't happen. The detailed error follows */
		*error = g_error_new (1, 0, _("Internal error: %s"), error_local->message);
		g_error_free (error_local);
		goto out;
	}

	egg_debug ("Simulating update for %s", package_ids[0]);
	ret = pk_client_simulate_update_packages (client_sync, package_ids, error);
	if (!ret) {
		egg_warning ("failed to simulate a package update");
		goto out;
	}

	/* see how many packages there are */
	list_single = pk_client_get_package_list (client_sync);
	pk_obj_list_add_list (PK_OBJ_LIST(list), PK_OBJ_LIST(list_single));
	g_object_unref (list_single);

	/* one of the simulate-update-packages failed */
	if (!ret)
		goto out;

	/* if there are no required packages, just do the remove */
	length = pk_package_list_get_size (list);
	if (length != 0) {

		/* print the additional deps to the screen */
		pk_console_print_deps_list (list);

		/* TRANSLATORS: We are checking if it's okay to remove a list of packages */
		accept_changes = pk_console_get_prompt (_("Proceed with changes?"), FALSE);

		/* we chickened out */
		if (!accept_changes) {
			/* TRANSLATORS: There was an error removing the packages. The detailed error follows */
			*error = g_error_new (1, 0, "%s", _("The package update was canceled!"));
			ret = FALSE;
			goto out;
		}
	}

	ret = pk_client_update_packages (PK_CLIENT(task), TRUE, package_ids, error);
	if (!ret) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not update %s: %s"), package, error_local->message);
		g_error_free (error_local);
	}
out:
	g_object_unref (list);
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_requires:
 **/
static gboolean
pk_console_get_requires (PkBitfield filters, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	GError *error_local = NULL;

	package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), package, &error_local);
	if (package_id == NULL) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get the requirements for %s: %s"), package, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_requires (PK_CLIENT(task), filters, package_ids, TRUE, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get the requirements for %s: %s"), package, error_local->message);
		g_error_free (error_local);
	}
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_depends:
 **/
static gboolean
pk_console_get_depends (PkBitfield filters, const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	GError *error_local = NULL;

	package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), package, &error_local);
	if (package_id == NULL) {
		/* TRANSLATORS: There was an error getting the dependencies for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get the dependencies for %s: %s"), package, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_depends (PK_CLIENT(task), filters, package_ids, FALSE, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was an error getting the dependencies for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get the dependencies for %s: %s"), package, error_local->message);
		g_error_free (error_local);
	}
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_details:
 **/
static gboolean
pk_console_get_details (const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	GError *error_local = NULL;

	package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), package, &error_local);
	if (package_id == NULL) {
		/* TRANSLATORS: There was an error getting the details about the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get package details for %s: %s"), package, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_details (PK_CLIENT(task), package_ids, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was an error getting the details about the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get package details for %s: %s"), package, error_local->message);
		g_error_free (error_local);
	}
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_get_files:
 **/
static gboolean
pk_console_get_files (const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	GError *error_local = NULL;

	package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NONE), package, &error_local);
	if (package_id == NULL) {
		/* TRANSLATORS: The package name was not found in any software sources. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find the files for %s: %s"), package, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_files (PK_CLIENT(task), package_ids, error);
	if (!ret) {
		/* TRANSLATORS: There was an error getting the list of files for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get the file list for %s: %s"), package, error_local->message);
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
pk_console_get_update_detail (const gchar *package, GError **error)
{
	gboolean ret;
	gchar *package_id;
	gchar **package_ids;
	GError *error_local = NULL;

	package_id = pk_console_perhaps_resolve (PK_CLIENT(task), pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), package, &error_local);
	if (package_id == NULL) {
		/* TRANSLATORS: The package name was not found in any software sources. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not find the update details for %s: %s"), package, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}
	package_ids = pk_package_ids_from_id (package_id);
	ret = pk_client_get_update_detail (PK_CLIENT(task), package_ids, &error_local);
	if (!ret) {
		/* TRANSLATORS: There was an error getting the details about the update for the package. The detailed error follows */
		*error = g_error_new (1, 0, _("This tool could not get the update details for %s: %s"), package, error_local->message);
		g_error_free (error_local);
	}
	g_strfreev (package_ids);
	g_free (package_id);
	return ret;
}

/**
 * pk_console_error_code_cb:
 **/
static void
pk_console_error_code_cb (PkErrorCodeEnum error_code, const gchar *details, gpointer data)
{
	PkRoleEnum role;

	pk_client_get_role (PK_CLIENT(task), &role, NULL, NULL);

	/* handled */
	if (need_requeue) {
		if (error_code == PK_ERROR_ENUM_NO_LICENSE_AGREEMENT ||
		    pk_error_code_is_need_untrusted (error_code)) {
			egg_debug ("ignoring %s error as handled", pk_error_enum_to_text (error_code));
			return;
		}
		egg_warning ("set requeue, but did not handle error");
	}

	/* TRANSLATORS: This was an unhandled error, and we don't have _any_ context */
	g_print ("%s %s: %s\n", _("Error:"), pk_error_enum_to_text (error_code), details);
}

#endif

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkControl *control_, gboolean connected, gpointer data)
{
	/* if the daemon crashed, don't hang around */
	if (!connected) {
		/* TRANSLATORS: This is when the daemon crashed, and we are up shit creek without a paddle */
		g_print ("%s\n", _("The daemon crashed mid-transaction!"));
		_exit (2);
	}
}

/**
 * pk_console_sigint_handler:
 **/
static void
pk_console_sigint_handler (int sig)
{
//	PkRoleEnum role;
//	gboolean ret;
//	GError *error = NULL;
	egg_debug ("Handling SIGINT");

	/* restore default ASAP, as the cancels might hang */
	signal (SIGINT, SIG_DFL);

#if 0
	/* cancel any tasks */
	pk_client_get_role (PK_CLIENT(task), &role, NULL, NULL);
	if (role != PK_ROLE_ENUM_UNKNOWN) {
		ret = pk_client_cancel (PK_CLIENT(task), &error);
		if (!ret) {
			egg_warning ("failed to cancel normal client: %s", error->message);
			g_error_free (error);
			error = NULL;
		}
	}
	pk_client_get_role (client_sync, &role, NULL, NULL);
	if (role != PK_ROLE_ENUM_UNKNOWN) {
		ret = pk_client_cancel (client_sync, &error);
		if (!ret) {
			egg_warning ("failed to cancel task client: %s", error->message);
			g_error_free (error);
		}
	}
#endif

	/* kill ourselves */
	egg_debug ("Retrying SIGINT");
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
	g_string_append_printf (string, "  %s\n", "get-actions");
	g_string_append_printf (string, "  %s\n", "get-groups");
	g_string_append_printf (string, "  %s\n", "get-filters");
	g_string_append_printf (string, "  %s\n", "get-transactions");
	g_string_append_printf (string, "  %s\n", "get-time");

	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_NAME) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_DETAILS) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_GROUP) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_SEARCH_FILE))
		g_string_append_printf (string, "  %s\n", "search [name|details|group|file] [data]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_PACKAGES) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_FILES))
		g_string_append_printf (string, "  %s\n", "install [packages|files]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES))
		g_string_append_printf (string, "  %s\n", "download [directory] [packages]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_INSTALL_SIGNATURE))
		g_string_append_printf (string, "  %s\n", "install-sig [type] [key_id] [package_id]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REMOVE_PACKAGES))
		g_string_append_printf (string, "  %s\n", "remove [package]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_SYSTEM) ||
	    pk_bitfield_contain (roles, PK_ROLE_ENUM_UPDATE_PACKAGES))
		g_string_append_printf (string, "  %s\n", "update <package>");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REFRESH_CACHE))
		g_string_append_printf (string, "  %s\n", "refresh");
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
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_ROLLBACK))
		g_string_append_printf (string, "  %s\n", "rollback");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_REPO_LIST))
		g_string_append_printf (string, "  %s\n", "repo-list");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_ENABLE))
		g_string_append_printf (string, "  %s\n", "repo-enable [repo_id]");
		g_string_append_printf (string, "  %s\n", "repo-disable [repo_id]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_REPO_SET_DATA))
		g_string_append_printf (string, "  %s\n", "repo-set-data [repo_id] [parameter] [value];");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_WHAT_PROVIDES))
		g_string_append_printf (string, "  %s\n", "what-provides [search]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_ACCEPT_EULA))
		g_string_append_printf (string, "  %s\n", "accept-eula [eula-id]");
	if (pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_CATEGORIES))
		g_string_append_printf (string, "  %s\n", "get-categories");
	return g_string_free (string, FALSE);
}

/**
 * pk_console_progress_cb:
 **/
static void
pk_console_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
	gint percentage;
	PkRoleEnum role;
	PkStatusEnum status;
	gchar *package_id;
	gchar *text;

	/* packages */
	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_object_get (progress,
			      "role", &role,
			      NULL);
		if (role == PK_ROLE_ENUM_SEARCH_NAME ||
		    role == PK_ROLE_ENUM_SEARCH_DETAILS ||
		    role == PK_ROLE_ENUM_SEARCH_GROUP ||
		    role == PK_ROLE_ENUM_SEARCH_FILE ||
		    role == PK_ROLE_ENUM_RESOLVE ||
		    role == PK_ROLE_ENUM_GET_UPDATES ||
		    role == PK_ROLE_ENUM_WHAT_PROVIDES ||
		    role == PK_ROLE_ENUM_GET_PACKAGES)
			return;
		g_object_get (progress,
			      "package-id", &package_id,
			      NULL);
		text = pk_package_id_get_printable (package_id);
		pk_progress_bar_start (progressbar, text);
		g_free (package_id);
		g_free (text);
	}

	/* percentage */
	if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_object_get (progress,
			      "percentage", &percentage,
			      NULL);
		pk_progress_bar_set_percentage (progressbar, percentage);
	}

	/* status */
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
			      "status", &status,
			      NULL);
		/* TODO: translate */
		pk_progress_bar_start (progressbar, pk_status_enum_to_text (status));
	}
}

/**
 * pk_console_finished_cb:
 **/
static void
pk_console_finished_cb (GObject *object, GAsyncResult *res, gpointer data)
{
//	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	const PkResults *results;
	PkExitEnum exit_enum;
	const PkResultItemErrorCode *error_item;
	GPtrArray *array;

	/* no more progress */
	pk_progress_bar_end (progressbar);

	/* get the results */
	results = pk_client_generic_finish (PK_CLIENT(task), res, &error);
	if (results == NULL) {
		g_print ("Failed to complete: %s\n", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
//	if (exit_enum != PK_EXIT_ENUM_CANCELLED)
//		egg_test_failed (test, "failed to cancel search: %s", pk_exit_enum_to_text (exit_enum));

	/* check error code */
	error_item = pk_results_get_error_code (results);
//	if (error_item->code != PK_ERROR_ENUM_TRANSACTION_CANCELLED)
//		egg_test_failed (test, "failed to get error code: %i", error_item->code);
//	if (g_strcmp0 (error_item->details, "The task was stopped successfully") != 0)
//		egg_test_failed (test, "failed to get error message: %s", error_item->details);

	/* package */
	array = pk_results_get_package_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_package_cb, NULL);
	g_ptr_array_unref (array);

	/* transaction */
	array = pk_results_get_transaction_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_transaction_cb, NULL);
	g_ptr_array_unref (array);

	/* distro_upgrade */
	array = pk_results_get_distro_upgrade_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_distro_upgrade_cb, NULL);
	g_ptr_array_unref (array);

	/* category */
	array = pk_results_get_category_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_category_cb, NULL);
	g_ptr_array_unref (array);

	/* update_detail */
	array = pk_results_get_update_detail_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_update_detail_cb, NULL);
	g_ptr_array_unref (array);

	/* repo_detail */
	array = pk_results_get_repo_detail_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_repo_detail_cb, NULL);
	g_ptr_array_unref (array);

	/* require_restart */
	array = pk_results_get_require_restart_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_require_restart_cb, NULL);
	g_ptr_array_unref (array);

	/* details */
	array = pk_results_get_details_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_details_cb, NULL);
	g_ptr_array_unref (array);

	/* message */
	array = pk_results_get_message_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_message_cb, NULL);
	g_ptr_array_unref (array);

	/* files */
	array = pk_results_get_files_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_files_cb, NULL);
	g_ptr_array_unref (array);

	/* repo_signature_required */
	array = pk_results_get_repo_signature_required_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_repo_signature_required_cb, NULL);
	g_ptr_array_unref (array);

	/* eula_required */
	array = pk_results_get_eula_required_array (results);
	g_ptr_array_foreach (array, (GFunc) pk_console_eula_required_cb, NULL);
	g_ptr_array_unref (array);
out:
	g_main_loop_quit (loop);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GError *error = NULL;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	GOptionContext *context;
	gchar *options_help;
	gchar *filter = NULL;
	gchar *summary = NULL;
	const gchar *mode;
	const gchar *value = NULL;
	const gchar *details = NULL;
	const gchar *parameter = NULL;
//	PkBitfield groups;
//	gchar *text;
//	gboolean maybe_sync = TRUE;
	PkBitfield filters = 0;
	gint retval = EXIT_SUCCESS;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line argument, if we should show debugging information */
			_("Show extra debugging information"), NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			/* TRANSLATORS: command line argument, just show the version string */
			_("Show the program version and exit"), NULL},
		{ "filter", '\0', 0, G_OPTION_ARG_STRING, &filter,
			/* TRANSLATORS: command line argument, use a filter to narrow down results */
			_("Set the filter, e.g. installed"), NULL},
		{ "nowait", 'n', 0, G_OPTION_ARG_NONE, &nowait,
			/* TRANSLATORS: command line argument, work asynchronously */
			_("Exit without waiting for actions to complete"), NULL},
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	/* do stuff on ctrl-c */
	signal (SIGINT, pk_console_sigint_handler);

	/* check if we are on console */
	if (isatty (fileno (stdout)) == 1)
		is_console = TRUE;

	/* we need the roles early, as we only show the user only what they can do */
	control = pk_control_sync_new ();
	roles = pk_control_sync_get_roles (control, NULL);
	summary = pk_console_get_summary ();
	progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (progressbar, 25);
	pk_progress_bar_set_padding (progressbar, 20);

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
		goto out_last;
	}

	if (argc < 2) {
		g_print ("%s", options_help);
		retval = PK_EXIT_CODE_SYNTAX_INVALID;
		goto out_last;
	}

	loop = g_main_loop_new (NULL, FALSE);

	/* watch when the daemon aborts */
	g_signal_connect (control, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), loop);

	/* create transactions */
	task = pk_task_new ();

	/* check filter */
	if (filter != NULL) {
		filters = pk_filter_bitfield_from_text (filter);
		if (filters == 0) {
			/* TRANSLATORS: The user specified an incorrect filter */
			error = g_error_new (1, 0, "%s: %s", _("The filter specified was invalid"), filter);
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
	}
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
//			pk_progress_bar_start (progressbar, _("Searching"));
			/* fire off an async request */
			pk_client_search_name_async (PK_CLIENT(task), filters, details, NULL,
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
			pk_client_search_details_async (PK_CLIENT(task), filters, details, NULL,
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
			pk_client_search_group_async (PK_CLIENT(task), filters, details, NULL,
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
			pk_client_search_file_async (PK_CLIENT(task), filters, details, NULL,
						     (PkProgressCallback) pk_console_progress_cb, NULL,
						     (GAsyncReadyCallback) pk_console_finished_cb, NULL);
		} else {
			/* TRANSLATORS: the search type was provided, but invalid */
			error = g_error_new (1, 0, "%s", _("Invalid search type"));
		}
#if 0

	} else if (strcmp (mode, "install") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: the user did not specify what they wanted to install */
			error = g_error_new (1, 0, "%s", _("A package name or filename to install is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_install_stuff (PK_CLIENT(task), argv, &error);
#endif

	} else if (strcmp (mode, "install-sig") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			/* TRANSLATORS: geeky error, 99.9999% of users won't see this */
			error = g_error_new (1, 0, "%s", _("A type, key_id and package_id are required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_install_signature_async (PK_CLIENT(task), PK_SIGTYPE_ENUM_GPG, details, parameter, NULL,
						   (PkProgressCallback) pk_console_progress_cb, NULL,
						   (GAsyncReadyCallback) pk_console_finished_cb, NULL);
#if 0
	} else if (strcmp (mode, "remove") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: the user did not specify what they wanted to remove */
			error = g_error_new (1, 0, "%s", _("A package name to remove is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_remove_packages (PK_CLIENT(task), argv, &error);
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
		ret = pk_console_download_packages (PK_CLIENT(task), argv, value, &error);
#endif
	} else if (strcmp (mode, "accept-eula") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: geeky error, 99.9999% of users won't see this */
			error = g_error_new (1, 0, "%s", _("A licence identifier (eula-id) is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_accept_eula_async (PK_CLIENT(task), value, NULL,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

#if 0
	} else if (strcmp (mode, "rollback") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: geeky error, 99.9999% of users won't see this */
			error = g_error_new (1, 0, "%s", _("A transaction identifier (tid) is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_rollback_async (PK_CLIENT(task), value, NULL,
					  (PkProgressCallback) pk_console_progress_cb, NULL,
					  (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "update") == 0) {
		if (value == NULL) {
			/* do the system update */
			pk_client_update_system_async (PK_CLIENT(task), TRUE, NULL,
						       (PkProgressCallback) pk_console_progress_cb, NULL,
						       (GAsyncReadyCallback) pk_console_finished_cb, NULL);
		} else {
			ret = pk_console_update_package (PK_CLIENT(task), value, &error);
		}
#endif

	} else if (strcmp (mode, "resolve") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not specify a package name */
			error = g_error_new (1, 0, "%s", _("A package name to resolve is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_resolve_async (PK_CLIENT(task), filters, argv+2, NULL,
				         (PkProgressCallback) pk_console_progress_cb, NULL,
					 (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-enable") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not specify a repository (software source) name */
			error = g_error_new (1, 0, "%s", _("A repository name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_repo_enable_async (PK_CLIENT(task), value, TRUE, NULL,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-disable") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not specify a repository (software source) name */
			error = g_error_new (1, 0, "%s", _("A repository name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_repo_enable_async (PK_CLIENT(task), value, FALSE, NULL,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-set-data") == 0) {
		if (value == NULL || details == NULL || parameter == NULL) {
			/* TRANSLATORS: The user didn't provide any data */
			error = g_error_new (1, 0, "%s", _("A repo name, parameter and value are required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_repo_set_data_async (PK_CLIENT(task), value, details, parameter, NULL,
					       (PkProgressCallback) pk_console_progress_cb, NULL,
					       (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "repo-list") == 0) {
		pk_client_get_repo_list_async (PK_CLIENT(task), filters, NULL,
					       (PkProgressCallback) pk_console_progress_cb, NULL,
					       (GAsyncReadyCallback) pk_console_finished_cb, NULL);
#if 0
	} else if (strcmp (mode, "get-time") == 0) {
		PkRoleEnum role;
		guint time_ms;
		if (value == NULL) {
			/* TRANSLATORS: The user didn't specify what action to use */
			error = g_error_new (1, 0, "%s", _("An action, e.g. 'update-system' is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		role = pk_role_enum_from_text (value);
		if (role == PK_ROLE_ENUM_UNKNOWN) {
			/* TRANSLATORS: The user specified an invalid action */
			error = g_error_new (1, 0, "%s", _("A correct role is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_control_get_time_since_action (control, role, &time_ms, &error);
		if (!ret) {
			/* TRANSLATORS: we keep a database updated with the time that an action was last executed */
			error = g_error_new (1, 0, "%s", _("Failed to get the time since this action was last completed"));
			retval = EXIT_FAILURE;
			goto out;
		}
		g_print ("time since %s is %is\n", value, time_ms);

	} else if (strcmp (mode, "get-depends") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_get_depends (PK_CLIENT(task), filters, value, &error);
#endif
	} else if (strcmp (mode, "get-distro-upgrades") == 0) {
		pk_client_get_distro_upgrades_async (PK_CLIENT(task), NULL,
						     (PkProgressCallback) pk_console_progress_cb, NULL,
						     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

#if 0
	} else if (strcmp (mode, "get-update-detail") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_get_update_detail (PK_CLIENT(task), value, &error);

	} else if (strcmp (mode, "get-requires") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_get_requires (PK_CLIENT(task), filters, value, &error);
#endif

	} else if (strcmp (mode, "what-provides") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: each package "provides" certain things, e.g. mime(gstreamer-decoder-mp3), the user didn't specify it */
			error = g_error_new (1, 0, "%s", _("A package provide string is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		pk_client_what_provides_async (PK_CLIENT(task), filters, PK_PROVIDES_ENUM_CODEC, value, NULL,
					       (PkProgressCallback) pk_console_progress_cb, NULL,
					       (GAsyncReadyCallback) pk_console_finished_cb, NULL);
#if 0
	} else if (strcmp (mode, "get-details") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_get_details (PK_CLIENT(task), value, &error);

	} else if (strcmp (mode, "get-files") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user did not provide a package name */
			error = g_error_new (1, 0, "%s", _("A package name is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_get_files (PK_CLIENT(task), value, &error);

	} else if (strcmp (mode, "list-create") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user didn't specify a filename to create as a list */
			error = g_error_new (1, 0, "%s", _("A list file name to create is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_list_create (PK_CLIENT(task), value, &error);

	} else if (strcmp (mode, "list-diff") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user didn't specify a filename to open as a list */
			error = g_error_new (1, 0, "%s", _("A list file to open is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_list_diff (PK_CLIENT(task), value, &error);

	} else if (strcmp (mode, "list-install") == 0) {
		if (value == NULL) {
			/* TRANSLATORS: The user didn't specify a filename to open as a list */
			error = g_error_new (1, 0, "%s", _("A list file to open is required"));
			retval = PK_EXIT_CODE_SYNTAX_INVALID;
			goto out;
		}
		ret = pk_console_list_install (PK_CLIENT(task), value, &error);
#endif
	} else if (strcmp (mode, "get-updates") == 0) {
		pk_client_get_updates_async (PK_CLIENT(task), filters, NULL,
					     (PkProgressCallback) pk_console_progress_cb, NULL,
					     (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-categories") == 0) {
		pk_client_get_categories_async (PK_CLIENT(task), NULL,
						(PkProgressCallback) pk_console_progress_cb, NULL,
						(GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "get-packages") == 0) {
		pk_client_get_packages_async (PK_CLIENT(task), filters, NULL,
					      (PkProgressCallback) pk_console_progress_cb, NULL,
					      (GAsyncReadyCallback) pk_console_finished_cb, NULL);
#if 0
	} else if (strcmp (mode, "get-actions") == 0) {
		text = pk_role_bitfield_to_text (roles);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		/* these can never fail */
		ret = TRUE;

	} else if (strcmp (mode, "get-filters") == 0) {
		filters = pk_control_get_filters (control, NULL);
		text = pk_filter_bitfield_to_text (filters);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		/* these can never fail */
		ret = TRUE;

	} else if (strcmp (mode, "get-groups") == 0) {
		groups = pk_control_get_groups (control, NULL);
		text = pk_group_bitfield_to_text (groups);
		g_strdelimit (text, ";", '\n');
		g_print ("%s\n", text);
		g_free (text);
		/* these can never fail */
		ret = TRUE;
#endif
	} else if (strcmp (mode, "get-transactions") == 0) {
		pk_client_get_old_transactions_async (PK_CLIENT(task), 10, NULL,
						      (PkProgressCallback) pk_console_progress_cb, NULL,
						      (GAsyncReadyCallback) pk_console_finished_cb, NULL);

	} else if (strcmp (mode, "refresh") == 0) {
		/* special case - this takes a long time, and doesn't do packages */
//		pk_console_start_bar ("refresh-cache");
		pk_client_refresh_cache_async (PK_CLIENT(task), FALSE, NULL,
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
//			/* TRANSLATORS: User does not have permission to do this */
//			g_print ("%s\n", _("Incorrect privileges for this operation"));
		/* TRANSLATORS: Generic failure of what they asked to do */
		g_print ("%s:  %s\n", _("Command failed"), error->message);
		if (retval == EXIT_SUCCESS)
			retval = EXIT_FAILURE;
	}

	g_free (options_help);
	g_free (filter);
	g_free (summary);
	g_object_unref (progressbar);
	g_object_unref (control);
	g_object_unref (task);
out_last:
	return retval;
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
	if (g_strcmp0 (text_safe, "richard   ") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad NULL");
	text_safe = pk_strpad (NULL, 10);
	if (g_strcmp0 (text_safe, "          ") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad nothing");
	text_safe = pk_strpad ("", 10);
	if (g_strcmp0 (text_safe, "          ") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad over");
	text_safe = pk_strpad ("richardhughes", 10);
	if (g_strcmp0 (text_safe, "richardhughes") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	g_free (text_safe);

	/************************************************************/
	egg_test_title (test, "pad zero");
	text_safe = pk_strpad ("rich", 0);
	if (g_strcmp0 (text_safe, "rich") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed the padd '%s'", text_safe);
	g_free (text_safe);
	egg_test_end (test);
}
#endif

