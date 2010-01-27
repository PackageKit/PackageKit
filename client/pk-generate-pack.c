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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <locale.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <dbus/dbus-glib.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>

#include "egg-debug.h"

static PkProgressBar *progressbar = NULL;
static GCancellable *cancellable = NULL;

/**
 * pk_generate_pack_get_filename:
 **/
static gchar *
pk_generate_pack_get_filename (const gchar *name, const gchar *directory)
{
	gchar *filename = NULL;
	gchar *distro_id;
	gchar *iso_time = NULL;
	PkControl *control;
	gboolean ret;
	GError *error = NULL;

	control = pk_control_new ();
	ret = pk_control_get_properties (control, NULL, &error);
	if (!ret) {
		egg_error ("Failed to contact PackageKit: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	/* get data */
	g_object_get (control,
		      "distro-id", &distro_id,
		      NULL);

	/* delimit with nicer chars then ';' */
	g_strdelimit (distro_id, ";", '-');

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
 * pk_generate_pack_progress_cb:
 **/
static void
pk_generate_pack_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
	gint percentage;
	PkRoleEnum role;
	PkStatusEnum status;
	const gchar *text;

	/* role */
	if (type == PK_PROGRESS_TYPE_ROLE) {
		g_object_get (progress,
			      "role", &role,
			      NULL);

		/* show new role on the bar */
		text = pk_role_enum_to_localised_present (role);
		pk_progress_bar_start (progressbar, text);
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
		if (status == PK_STATUS_ENUM_FINISHED)
			return;

		/* show status on the bar */
		text = pk_status_enum_to_localised_text (status);
		pk_progress_bar_start (progressbar, text);
	}
}

/**
 * pk_generate_pack_sigint_cb:
 **/
static void
pk_generate_pack_sigint_cb (int sig)
{
	egg_debug ("Handling SIGINT");

	/* restore default */
	signal (SIGINT, SIG_DFL);

	/* cancel any tasks still running */
	g_cancellable_cancel (cancellable);

	/* kill ourselves */
	egg_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/* tiny helper to help us do the async operation */
typedef struct {
	GError		**error;
	GMainLoop	*loop;
	gboolean	 ret;
} PkGenpackHelper;

/**
 * pk_generate_pack_generic_cb:
 **/
static void
pk_generate_pack_generic_cb (PkServicePack *pack, GAsyncResult *res, PkGenpackHelper *helper)
{
	/* get the result */
	helper->ret = pk_service_pack_generic_finish (pack, res, helper->error);
	g_main_loop_quit (helper->loop);
}

/**
 * pk_generate_pack_create_for_updates:
 **/
static gboolean
pk_generate_pack_create_for_updates (PkServicePack *pack, const gchar *filename, gchar **excludes, GError **error)
{
	gboolean ret;
	PkGenpackHelper *helper;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper = g_new0 (PkGenpackHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_service_pack_create_for_updates_async (pack, filename, excludes, cancellable,
						  (PkProgressCallback) pk_generate_pack_progress_cb, NULL,
						  (GAsyncReadyCallback) pk_generate_pack_generic_cb, helper);
	g_main_loop_run (helper->loop);

	ret = helper->ret;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return ret;
}

/**
 * pk_generate_pack_create_for_package_ids:
 **/
static gboolean
pk_generate_pack_create_for_package_ids (PkServicePack *pack, const gchar *filename, gchar **package_ids, gchar **excludes, GError **error)
{
	gboolean ret;
	PkGenpackHelper *helper;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create temp object */
	helper = g_new0 (PkGenpackHelper, 1);
	helper->loop = g_main_loop_new (NULL, FALSE);
	helper->error = error;

	/* run async method */
	pk_service_pack_create_for_package_ids_async (pack, filename, package_ids, excludes, cancellable,
						      (PkProgressCallback) pk_generate_pack_progress_cb, NULL,
						      (GAsyncReadyCallback) pk_generate_pack_generic_cb, helper);
	g_main_loop_run (helper->loop);

	ret = helper->ret;

	/* free temp object */
	g_main_loop_unref (helper->loop);
	g_free (helper);

	return ret;
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
	gchar **excludes = NULL;
	gchar *package_id = NULL;
	PkServicePack *pack = NULL;
	gchar *directory = NULL;
	gchar *package_list = NULL;
	gchar *package = NULL;
	gboolean updates = FALSE;
	gint retval = 1;

	const GOptionEntry options[] = {
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
	dbus_g_thread_init ();

	/* do stuff on ctrl-c */
	signal (SIGINT, pk_generate_pack_sigint_cb);

	context = g_option_context_new ("PackageKit Pack Generator");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, egg_debug_get_option_group ());
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	client = pk_client_new ();
	pack = pk_service_pack_new ();
	cancellable = g_cancellable_new ();
	progressbar = pk_progress_bar_new ();
	pk_progress_bar_set_size (progressbar, 25);
	pk_progress_bar_set_padding (progressbar, 20);

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
	if (package != NULL && package[0] == '\0') {
		/* TRANSLATORS: This is when the user fails to supply the package name */
		g_print ("%s\n", _("A package name is required"));
		retval = 1;
		goto out;
	}

	/* no argument given to --output */
	if (directory != NULL && directory[0] == '\0') {
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
	ret = pk_control_get_properties (control, NULL, &error);
	if (!ret) {
		/* TRANSLATORS: This is when the dameon is not-installed/broken and fails to startup */
		g_print ("%s: %s\n", _("The dameon failed to startup"), error->message);
		goto out;
	}

	/* get data */
	g_object_get (control,
		      "roles", &roles,
		      NULL);

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
	pk_service_pack_set_temp_directory (pack, tempdir);

	/* get the exclude list */
	excludes = NULL;
#if 0
	ret = pk_obj_list_from_file (PK_OBJ_LIST(list), package_list);
	if (!ret) {
		/* TRANSLATORS: This is when the list of packages from the remote computer cannot be opened */
		g_print ("%s: '%s'\n", _("Failed to open package list."), package_list);
		retval = 1;
		goto out;
	}
#endif

	/* resolve package name to package_id */
	if (!updates) {
		/* TRANSLATORS: The package name is being matched up to available packages */
		g_print ("%s\n", _("Finding package name."));
		package_id = pk_console_resolve_package (client, PK_FILTER_ENUM_NONE, package, &error);
		if (package_id == NULL) {
			/* TRANSLATORS: This is when the package cannot be found in any software source. The detailed error follows */
			g_print (_("Failed to find package '%s': %s"), package, error->message);
			g_error_free (error);
			retval = 1;
			goto out;
		}
	}

	/* TRANSLATORS: This is telling the user we are in the process of making the pack */
	g_print ("%s\n", _("Creating service pack..."));
	if (updates)
		ret = pk_generate_pack_create_for_updates (pack, filename, excludes, &error);
	else {
		gchar **package_ids;
		package_ids = pk_package_ids_from_id (package_id);
		ret = pk_generate_pack_create_for_package_ids (pack, filename, package_ids, excludes, &error);
		g_strfreev (package_ids);
	}

	/* no more progress */
	pk_progress_bar_end (progressbar);

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

	g_object_unref (cancellable);
	if (progressbar != NULL)
		g_object_unref (progressbar);
	if (pack != NULL)
		g_object_unref (pack);
	if (client != NULL)
		g_object_unref (client);
	if (control != NULL)
		g_object_unref (control);
	g_free (tempdir);
	g_free (filename);
	g_free (package_id);
	g_free (directory);
	g_free (package_list);
	g_free (options_help);
	g_strfreev (excludes);
	return retval;
}
