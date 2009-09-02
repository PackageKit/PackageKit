/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Shishir Goel <crazyontheedge@gmail.com>
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

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-text.h"
#include "pk-tools-common.h"

static guint last_percentage = 0;
static PkServicePack *pack = NULL;

/**
 * pk_generate_pack_get_filename:
 **/
static gchar *
pk_generate_pack_get_filename (const gchar *name, const gchar *directory)
{
	gchar *filename = NULL;
	gchar *distro_id;
	gchar *iso_time = NULL;

	distro_id = pk_get_distro_id ();
	if (name != NULL) {
		filename = g_strdup_printf ("%s/%s-%s.%s", directory, name, distro_id, PK_SERVICE_PACK_FILE_EXTENSION);
	} else {
		iso_time = pk_iso8601_present ();
		/* don't include the time, just use the date prefix */
		iso_time[10] = '\0';
		filename = g_strdup_printf ("%s/updates-%s-%s.%s", directory, iso_time, distro_id, PK_SERVICE_PACK_FILE_EXTENSION);
	}
	g_free (distro_id);
	g_free (iso_time);
	return filename;
}

/**
 * pk_generate_pack_package_resolve:
 **/
static gchar *
pk_generate_pack_package_resolve (PkClient *client, PkBitfield filter, const gchar *package, GError **error)
{
	PkPackageList *list;
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

	/* ask the user to select the right one */
	package_id = pk_console_resolve_package_id (list, error);
out:
	if (list != NULL)
		g_object_unref (list);
	return package_id;
}

/**
 * pk_generate_pack_package_cb:
 **/
static void
pk_generate_pack_package_cb (PkServicePack *pack_, const PkPackageObj *obj, gpointer data)
{
	g_return_if_fail (obj != NULL);
	/* TRANSLATORS: This is the state of the transaction */
	g_print ("%i%%\t%s %s-%s.%s\n", last_percentage, _("Downloading"), obj->id->name, obj->id->version, obj->id->arch);
}

/**
 * pk_generate_pack_percentage_cb:
 **/
static void
pk_generate_pack_percentage_cb (PkServicePack *pack_, guint percentage, gpointer data)
{
	last_percentage = percentage;
}

/**
 * pk_generate_pack_status_cb:
 **/
static void
pk_generate_pack_status_cb (PkServicePack *pack_, PkServicePackStatus status, gpointer data)
{
	if (status == PK_SERVICE_PACK_STATUS_DOWNLOAD_PACKAGES) {
		/* TRANSLATORS: This is when the main packages are being downloaded */
		g_print ("%s\n", _("Downloading packages"));
		return;
	}
	if (status == PK_SERVICE_PACK_STATUS_DOWNLOAD_DEPENDENCIES) {
		/* TRANSLATORS: This is when the dependency packages are being downloaded */
		g_print ("%s\n", _("Downloading dependencies"));
		return;
	}
}

/**
 * pk_generate_pack_sigint_cb:
 **/
static void
pk_generate_pack_sigint_cb (int sig)
{
	gboolean ret;
	GError *error = NULL;
	egg_debug ("Handling SIGINT");

	/* restore default */
	signal (SIGINT, SIG_DFL);

	/* cancel downloads */
	ret = pk_service_pack_cancel (pack, &error);
	if (!ret) {
		egg_warning ("failed to cancel: %s", error->message);
		g_error_free (error);
	}

	/* kill ourselves */
	egg_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;
	gchar *options_help;
	gboolean ret;
	gchar *filename = NULL;
	PkClient *client = NULL;
	PkControl *control = NULL;
	PkBitfield roles;
	gchar *tempdir = NULL;
	gboolean exists;
	gboolean overwrite;
	PkPackageList *list = NULL;
	gchar *package_id = NULL;

	gboolean verbose = FALSE;
	gchar *directory = NULL;
	gchar *package_list = NULL;
	gchar *package = NULL;
	gboolean updates = FALSE;
	gint retval = 1;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			_("Show extra debugging information"), NULL },
		{ "with-package-list", 'l', 0, G_OPTION_ARG_STRING, &package_list,
			/* TRANSLATORS: we can exclude certain packages (glibc) when we know they'll exist on the target */
			_("Set the file name of dependencies to be excluded"), NULL},
		{ "output", 'o', 0, G_OPTION_ARG_STRING, &directory,
			/* TRANSLATORS: the output location */
			_("The output file or directory (the current directory is used if ommitted)"), NULL},
		{ "package", 'p', 0, G_OPTION_ARG_STRING, &package,
			/* TRANSLATORS: put a list of packages in the pack */
			_("The package to be put into the service pack"), NULL},
		{ "updates", 'u', 0, G_OPTION_ARG_NONE, &updates,
			/* TRANSLATORS: put all pending updates in the pack */
			_("Put all updates available in the service pack"), NULL},
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
	signal (SIGINT, pk_generate_pack_sigint_cb);

	context = g_option_context_new ("PackageKit Pack Generator");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);
	egg_debug_init (verbose);

	/* neither options selected */
	if (package == NULL && !updates) {
		/* TRANSLATORS: This is when the user fails to supply the correct arguments */
		g_print ("%s\n", _("Neither --package or --updates option selected."));
		retval = 1;
		goto out;
	}

	/* both options selected */
	if (package != NULL && updates) {
		/* TRANSLATORS: This is when the user fails to supply just one argument */
		g_print ("%s\n", _("Both options selected."));
		retval = 1;
		goto out;
	}

	/* no argument given to --package */
	if (package != NULL && egg_strzero (package)) {
		/* TRANSLATORS: This is when the user fails to supply the package name */
		g_print ("%s\n", _("A package name is required"));
		retval = 1;
		goto out;
	}

	/* no argument given to --output */
	if (directory != NULL && egg_strzero (directory)) {
		/* TRANSLATORS: This is when the user fails to supply the output */
		g_print ("%s\n", _("A output directory or file name is required"));
		retval = 1;
		goto out;
	}

	/* fall back to the system copy */
	if (package_list == NULL)
		package_list = g_strdup (PK_SYSTEM_PACKAGE_LIST_FILENAME);

	/* fall back to CWD */
	if (directory == NULL)
		directory = g_get_current_dir ();

	/* are we dumb and can't do some actions */
	control = pk_control_new ();
	roles = pk_control_get_actions (control, NULL);
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_GET_DEPENDS)) {
		/* TRANSLATORS: This is when the backend doesn't have the capability to get-depends */
		g_print ("%s (GetDepends)\n", _("The package manager cannot perform this type of operation."));
		retval = 1;
		goto out;
	}
	if (!pk_bitfield_contain (roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES)) {
		/* TRANSLATORS: This is when the backend doesn't have the capability to download */
		g_print ("%s (DownloadPackage)\n", _("The package manager cannot perform this type of operation."));
		retval = 1;
		goto out;
	}

#ifndef HAVE_ARCHIVE_H
	/* TRANSLATORS: This is when the distro didn't include libarchive support into PK */
	g_print ("%s\n", _("Service packs cannot be created as PackageKit was not built with libarchive support."));
	goto out;
#endif

	/* the user can speciify a complete path */
	ret = g_file_test (directory, G_FILE_TEST_IS_DIR);
	if (ret) {
		filename = pk_generate_pack_get_filename (package, directory);
	} else {
		if (!g_str_has_suffix (directory, PK_SERVICE_PACK_FILE_EXTENSION)) {
			/* TRANSLATORS: the user specified an absolute path, but didn't get the extension correct */
			g_print ("%s .%s \n", _("If specifying a file, the service pack name must end with"), PK_SERVICE_PACK_FILE_EXTENSION);
			retval = 1;
			goto out;
		}
		filename = g_strdup (directory);
	}

	/* download packages to a temporary directory */
	tempdir = g_build_filename (g_get_tmp_dir (), "pack", NULL);

	/* check if file exists before we overwrite it */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);

	/*ask user input*/
	if (exists) {
		/* TRANSLATORS: This is when file already exists */
		overwrite = pk_console_get_prompt (_("A pack with the same name already exists, do you want to overwrite it?"), FALSE);
		if (!overwrite) {
			/* TRANSLATORS: This is when the pack was not overwritten */
			g_print ("%s\n", _("The pack was not overwritten."));
			retval = 1;
			goto out;
		}
	}

	/* get rid of temp directory if it already exists */
	g_rmdir (tempdir);

	/* make the temporary directory */
	retval = g_mkdir_with_parents (tempdir, 0777);
	if (retval != 0) {
		/* TRANSLATORS: This is when the temporary directory cannot be created, the directory name follows */
		g_print ("%s '%s'\n", _("Failed to create directory:"), tempdir);
		retval = 1;
		goto out;
	}

	/* get the exclude list */
	list = pk_package_list_new ();
	ret = pk_obj_list_from_file (PK_OBJ_LIST(list), package_list);
	if (!ret) {
		/* TRANSLATORS: This is when the list of packages from the remote computer cannot be opened */
		g_print ("%s: '%s'\n", _("Failed to open package list."), package_list);
		retval = 1;
		goto out;
	}

	/* resolve package name to package_id */
	if (!updates) {
		client = pk_client_new ();
		pk_client_set_use_buffer (client, TRUE, NULL);
		pk_client_set_synchronous (client, TRUE, NULL);
		/* TRANSLATORS: The package name is being matched up to available packages */
		g_print ("%s\n", _("Finding package name."));
		package_id = pk_generate_pack_package_resolve (client, PK_FILTER_ENUM_NONE, package, &error);
		if (package_id == NULL) {
			/* TRANSLATORS: This is when the package cannot be found in any software source. The detailed error follows */
			g_print (_("Failed to find package '%s': %s"), package, error->message);
			g_error_free (error);
			retval = 1;
			goto out;
		}
	}

	/* create pack and set initial values */
	pack = pk_service_pack_new ();
	g_signal_connect (pack, "package", G_CALLBACK (pk_generate_pack_package_cb), pack);
	g_signal_connect (pack, "percentage", G_CALLBACK (pk_generate_pack_percentage_cb), pack);
	g_signal_connect (pack, "status", G_CALLBACK (pk_generate_pack_status_cb), pack);
	pk_service_pack_set_filename (pack, filename);
	pk_service_pack_set_temp_directory (pack, tempdir);
	pk_service_pack_set_exclude_list (pack, list);

	/* TRANSLATORS: This is telling the user we are in the process of making the pack */
	g_print ("%s\n", _("Creating service pack..."));
	if (updates)
		ret = pk_service_pack_create_for_updates (pack, &error);
	else
		ret = pk_service_pack_create_for_package_id (pack, package_id, &error);
	if (ret) {
		/* TRANSLATORS: we succeeded in making the file */
		g_print (_("Service pack created '%s'"), filename);
		g_print ("\n");
		retval = 0;
	} else {
		/* TRANSLATORS: we failed to make te file */
		g_print (_("Failed to create '%s': %s"), filename, error->message);
		g_print ("\n");
		g_error_free (error);
	}

out:
	/* get rid of temp directory */
	g_rmdir (tempdir);

	if (pack != NULL)
		g_object_unref (pack);
	if (client != NULL)
		g_object_unref (client);
	if (list != NULL)
		g_object_unref (list);
	if (control != NULL)
		g_object_unref (control);
	g_free (tempdir);
	g_free (filename);
	g_free (package_id);
	g_free (directory);
	g_free (package_list);
	g_free (options_help);
	return retval;
}
