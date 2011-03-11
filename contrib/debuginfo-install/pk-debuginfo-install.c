/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

/* Test with ./pk-debuginfo-install bzip2-libs-1.0.5-5.fc11.i586 glib2-2.20.3-1.fc11.i586 */

#include "config.h"

#include <string.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>

/* Reserved exit codes:
 * 1		miscellaneous errors, such as "divide by zero"
 * 2		misuse of shell builtins
 * 126		command invoked cannot execute
 * 127		"command not found"
 * 128		invalid argument to exit
 * 128+n	fatal error signal "n"
 * 130		script terminated by Control-C
 * 255		exit status out of range
 */
#define PK_DEBUGINFO_EXIT_CODE_FAILED				1
#define PK_DEBUGINFO_EXIT_CODE_FAILED_TO_GET_REPOLIST		3
#define PK_DEBUGINFO_EXIT_CODE_FAILED_TO_ENABLE			4
#define PK_DEBUGINFO_EXIT_CODE_NOTHING_TO_DO			5
#define PK_DEBUGINFO_EXIT_CODE_FAILED_TO_FIND_DEPS		6
#define PK_DEBUGINFO_EXIT_CODE_FAILED_TO_INSTALL		7
#define PK_DEBUGINFO_EXIT_CODE_FAILED_TO_DISABLE		8

typedef struct {
	GPtrArray		*enabled;
	GPtrArray		*disabled;
	PkClient		*client;
	PkProgressBar		*progress_bar;
} PkDebuginfoInstallPrivate;

/**
 * pk_get_package_name_from_nevra:
 **/
static gchar *
pk_get_package_name_from_nevra (const gchar *nevra)
{
	gchar *name = NULL;
	gchar **split;
	guint len;

	/* hal-info-data-version-arch */
	split = g_strsplit (nevra, "-", -1);
	len = g_strv_length (split);

	/* just the package name specified */
	if (len == 1) {
		name = g_strdup (split[0]);
		goto out;
	}

	/* ignore the version */
	g_free (split[len-2]);
	split[len-2] = NULL;

	/* ignore the arch */
	g_free (split[len-1]);
	split[len-1] = NULL;

	/* join up name elements */
	name = g_strjoinv ("-", split);
out:
	g_strfreev (split); 
	return name;
}

/**
 * pk_debuginfo_install_in_array:
 **/
static gboolean
pk_debuginfo_install_in_array (GPtrArray *array, const gchar *text)
{
	guint i;
	gboolean ret = FALSE;
	const gchar *possible;

	/* compare each */
	for (i=0; i<array->len; i++) {
		possible = g_ptr_array_index (array, i);
		if (g_strcmp0 (text, possible) == 0) {
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * pk_debuginfo_install_enable_repos:
 **/
static gboolean
pk_debuginfo_install_enable_repos (PkDebuginfoInstallPrivate *priv, GPtrArray *array, gboolean enable, GError **error)
{
	guint i;
	gboolean ret = TRUE;
	PkResults *results = NULL;
	const gchar *repo_id;
	GError *error_local = NULL;
	PkError *error_code = NULL;

	/* enable all debuginfo repos we found */
	for (i=0; i<array->len; i++) {
		repo_id = g_ptr_array_index (array, i);

		/* enable this repo */
		results = pk_client_repo_enable (priv->client, repo_id, enable, NULL, NULL, NULL, &error_local);
		if (results == NULL) {
			*error = g_error_new (1, 0, "failed to enable %s: %s", repo_id, error_local->message);
			g_error_free (error_local);
			ret = FALSE;
			goto out;
		}

		/* check error code */
		error_code = pk_results_get_error_code (results);
		if (error_code != NULL) {
			*error = g_error_new (1, 0, "failed to enable repo: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
			ret = FALSE;
			goto out;
		}

		g_debug ("setting %s: %i", repo_id, enable);
		g_object_unref (results);
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	return ret;
}

/**
 * pk_debuginfo_install_progress_cb:
 **/
static void
pk_debuginfo_install_progress_cb (PkProgress *progress, PkProgressType type, PkDebuginfoInstallPrivate *priv)
{
	gint percentage;
	gchar *package_id = NULL;

	if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		g_object_get (progress, "percentage", &percentage, NULL);
		pk_progress_bar_set_percentage (priv->progress_bar, percentage);
		goto out;
	}
	if (type == PK_PROGRESS_TYPE_PACKAGE_ID) {
		g_object_get (progress, "package-id", &package_id, NULL);
		g_debug ("now downloading %s", package_id);
		goto out;
	}
out:
	g_free (package_id);
}

/**
 * pk_debuginfo_install_packages_install:
 **/
static gboolean
pk_debuginfo_install_packages_install (PkDebuginfoInstallPrivate *priv, GPtrArray *array, GError **error)
{
	gboolean ret = TRUE;
	PkResults *results = NULL;
	gchar **package_ids;
	GError *error_local = NULL;
	PkError *error_code = NULL;

	/* mush back into a char** */
	package_ids = pk_ptr_array_to_strv (array);

	/* TRANSLATORS: we are starting to install the packages */
	pk_progress_bar_start (priv->progress_bar, _("Starting install"));

	/* enable this repo */
	results = pk_task_install_packages_sync (PK_TASK(priv->client), package_ids, NULL,
						 (PkProgressCallback) pk_debuginfo_install_progress_cb, priv, &error_local);
	if (results == NULL) {
		*error = g_error_new (1, 0, "failed to install packages: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		*error = g_error_new (1, 0, "failed to resolve: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		ret = FALSE;
		goto out;
	}

	/* end progressbar output */
	pk_progress_bar_end (priv->progress_bar);
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_debuginfo_install_resolve_name_to_id:
 **/
static gchar *
pk_debuginfo_install_resolve_name_to_id (PkDebuginfoInstallPrivate *priv, const gchar *package_name, GError **error)
{
	PkResults *results = NULL;
	PkPackage *item;
	gchar *package_id = NULL;
	GPtrArray *list = NULL;
	GError *error_local = NULL;
	gchar **names;
	PkError *error_code = NULL;

	/* resolve takes a char** */
	names = g_strsplit (package_name, ";", -1);

	/* resolve */
	results = pk_client_resolve (priv->client, pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, -1), names, NULL, NULL, NULL, &error_local);
	if (results == NULL) {
		*error = g_error_new (1, 0, "failed to resolve: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		*error = g_error_new (1, 0, "failed to resolve: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* check we only got one match */
	list = pk_results_get_package_array (results);
	if (list->len == 0) {
		*error = g_error_new (1, 0, "no package %s found", package_name);
		goto out;
	}
	if (list->len > 1) {
		*error = g_error_new (1, 0, "more than one package found for %s", package_name);
		goto out;
	}

	/* get the package id */
	item = g_ptr_array_index (list, 0);
	package_id = g_strdup (pk_package_get_id (item));
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	if (list != NULL)
		g_ptr_array_unref (list);
	g_strfreev (names);
	return package_id;
}

/**
 * pk_debuginfo_install_remove_suffix:
 **/
static gboolean
pk_debuginfo_install_remove_suffix (gchar *name, const gchar *suffix)
{
	gboolean ret = FALSE;
	guint slen, len;

	if (!g_str_has_suffix (name, suffix))
		goto out;

	/* get lengths */
	len = strlen (name);
	slen = strlen (suffix);

	/* same string */
	if (len == slen)
		goto out;

	/* truncate */
	name[len-slen] = '\0';
	ret = TRUE;
out:
	return ret;
}

/**
 * pk_debuginfo_install_print_array:
 **/
static void
pk_debuginfo_install_print_array (GPtrArray *array)
{
	guint i;
	const gchar *package_id;
	gchar **split;

	for (i=0; i<array->len; i++) {
		package_id = g_ptr_array_index (array, i);
		split = pk_package_id_split (package_id);
		g_print ("%i\t%s-%s(%s)\t%s\n", i+1,
			 split[PK_PACKAGE_ID_NAME],
			 split[PK_PACKAGE_ID_VERSION],
			 split[PK_PACKAGE_ID_ARCH],
			 split[PK_PACKAGE_ID_DATA]);
		g_strfreev (split);
	}
}

/**
 * pk_debuginfo_install_name_to_debuginfo:
 **/
static gchar *
pk_debuginfo_install_name_to_debuginfo (const gchar *name)
{
	gchar *name_debuginfo = NULL;
	gchar *name_tmp = NULL;

	/* nothing */
	if (name == NULL)
		goto out;

	name_tmp = g_strdup (name);

	/* remove suffix */
	pk_debuginfo_install_remove_suffix (name_tmp, "-libs");

	/* append -debuginfo */
	name_debuginfo = g_strjoin ("-", name_tmp, "debuginfo", NULL);
out:
	g_free (name_tmp);
	return name_debuginfo;
}

/**
 * pk_debuginfo_install_add_deps:
 **/
static gboolean
pk_debuginfo_install_add_deps (PkDebuginfoInstallPrivate *priv, GPtrArray *packages_search, GPtrArray *packages_results, GError **error)
{
	gboolean ret = TRUE;
	PkResults *results = NULL;
	PkPackage *item;
	gchar *package_id = NULL;
	GPtrArray *list = NULL;
	GError *error_local = NULL;
	gchar **package_ids = NULL;
	gchar *name_debuginfo;
	guint i;
	gchar **split;
	PkError *error_code = NULL;

	/* get depends for them all, not adding dup's */
	package_ids = pk_ptr_array_to_strv (packages_search);
	results = pk_client_get_depends (priv->client, pk_bitfield_value (PK_FILTER_ENUM_NONE), package_ids, TRUE, NULL, NULL, NULL, &error_local);
	if (results == NULL) {
		*error = g_error_new (1, 0, "failed to get_depends: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		*error = g_error_new (1, 0, "failed to get depends: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		ret = FALSE;
		goto out;
	}

	/* add dependant packages */
	list = pk_results_get_package_array (results);
	for (i=0; i<list->len; i++) {
		item = g_ptr_array_index (list, i);
		split = pk_package_id_split (pk_package_get_id (item));
		/* add -debuginfo */
		name_debuginfo = pk_debuginfo_install_name_to_debuginfo (split[PK_PACKAGE_ID_NAME]);
		g_strfreev (split);

		/* resolve name */
		g_debug ("resolving: %s", name_debuginfo);
		package_id = pk_debuginfo_install_resolve_name_to_id (priv, name_debuginfo, &error_local);
		if (package_id == NULL) {
			/* TRANSLATORS: we couldn't find the package name, non-fatal */
			g_print (_("Failed to find the package %s, or already installed: %s"), name_debuginfo, error_local->message);
			g_print ("\n");
			g_error_free (error_local);
			/* don't quit, this is non-fatal */
			error = NULL;
		}

		/* add to array to install */
		if (package_id != NULL && !g_str_has_suffix (package_id, "installed")) {
			g_debug ("going to try to install (for deps): %s", package_id);
			g_ptr_array_add (packages_results, g_strdup (package_id));
		}

		g_free (package_id);
		g_free (name_debuginfo);
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	if (list != NULL)
		g_ptr_array_unref (list);
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_debuginfo_install_get_repo_list:
 **/
static gboolean
pk_debuginfo_install_get_repo_list (PkDebuginfoInstallPrivate *priv, GError **error)
{
	gboolean ret = FALSE;
	PkResults *results = NULL;
	guint i;
	GPtrArray *array;
	GError *error_local = NULL;
	PkRepoDetail *item;
	PkError *error_code = NULL;
	gboolean enabled;
	gchar *repo_id;

	/* get all repo details */
	results = pk_client_get_repo_list (priv->client, pk_bitfield_value (PK_FILTER_ENUM_NONE), NULL, NULL, NULL, &error_local);
	if (results == NULL) {
		*error = g_error_new (1, 0, "failed to get repo list: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		*error = g_error_new (1, 0, "failed to get repo list: %s, %s", pk_error_enum_to_string (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get results */
	array = pk_results_get_repo_detail_array (results);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_object_get (item,
			      "enabled", &enabled,
			      "repo-id", &repo_id,
			      NULL);
		if (enabled)
			g_ptr_array_add (priv->enabled, repo_id);
		else
			g_ptr_array_add (priv->disabled, repo_id);
	}
	ret = TRUE;
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	return ret;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	GError *error = NULL;
	GPtrArray *added_repos = NULL;
	GPtrArray *package_ids_recognised = NULL;
	GPtrArray *package_ids_to_install = NULL;
	guint i;
	guint retval = 0;
	gchar *package_id;
	gchar *name;
	gchar *name_debuginfo;
	gboolean simulate = FALSE;
	gboolean no_depends = FALSE;
	gboolean quiet = FALSE;
	gboolean noninteractive = FALSE;
	GOptionContext *context;
	const gchar *repo_id;
	gchar *repo_id_debuginfo;
	PkDebuginfoInstallPrivate *priv = NULL;
	guint step = 1;

	const GOptionEntry options[] = {
		{ "simulate", 's', 0, G_OPTION_ARG_NONE, &simulate,
		   /* command line argument, simulate what would be done, but don't actually do it */
		  _("Don't actually install any packages, only simulate what would be installed"), NULL },
		{ "no-depends", 'n', 0, G_OPTION_ARG_NONE, &no_depends,
		   /* command line argument, do we skip packages that depend on the ones specified */
		  _("Do not install dependencies of the core packages"), NULL },
		{ "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet,
		   /* command line argument, do we operate quietly */
		  _("Do not display information or progress"), NULL },
		{ "noninteractive", 'y', 0, G_OPTION_ARG_NONE, &noninteractive,
		   /* command line argument, do we ask questions */
		  _("Install the packages without asking for confirmation"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: tool that gets called when the command is not found */
	g_option_context_set_summary (context, _("PackageKit Debuginfo Installer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, pk_debug_get_option_group ());
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* new private struct */
	priv = g_new0 (PkDebuginfoInstallPrivate, 1);

	/* no input */
	if (argv[1] == NULL) {
		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: the use needs to specify a list of package names on the command line */
			g_print (_("ERROR: Specify package names to install."));
			g_print ("\n");
		}
		/* return correct failure retval */
		retval = PK_DEBUGINFO_EXIT_CODE_FAILED;
		goto out;
	}

	/* store as strings */
	priv->enabled = g_ptr_array_new ();
	priv->disabled = g_ptr_array_new ();
	added_repos = g_ptr_array_new ();
	package_ids_to_install = g_ptr_array_new ();
	package_ids_recognised = g_ptr_array_new ();

	/* create #PkClient */
	priv->client = PK_CLIENT(pk_task_text_new ());

	/* we are not asking questions, so it's pointless simulating */
	if (noninteractive) {
		g_object_set (priv->client,
			      "simulate", FALSE,
			      NULL);
	}

	/* use text progressbar */
	priv->progress_bar = pk_progress_bar_new ();
	pk_progress_bar_set_size (priv->progress_bar, 25);
	pk_progress_bar_set_padding (priv->progress_bar, 60);

	/* should be vocal? */
	if (!quiet) {
		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: we are getting the list of repositories */
		g_print (_("Getting sources list"));
		g_print ("...");
	}

	/* get all enabled repos */
	ret = pk_debuginfo_install_get_repo_list (priv, &error);
	if (!ret) {
		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: operation was not successful */
			g_print ("%s ", _("FAILED."));
		}
		/* TRANSLATORS: we're failed to enable the sources, detailed error follows */
		g_print ("Failed to enable sources list: %s", error->message);
		g_print ("\n");
		g_error_free (error);

		/* return correct failure retval */
		retval = PK_DEBUGINFO_EXIT_CODE_FAILED_TO_ENABLE;
		goto out;
	}

	/* should be vocal? */
	if (!quiet) {
		/* TRANSLATORS: all completed 100% */
		g_print ("%s ", _("OK."));

		/* TRANSLATORS: tell the user what we found */
		g_print (_("Found %i enabled and %i disabled sources."), priv->enabled->len, priv->disabled->len);
		g_print ("\n");

		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: we're finding repositories that match out pattern */
		g_print (_("Finding debugging sources"));
		g_print ("...");
	}

	/* find all debuginfo repos for repos that are enabled */
	for (i=0; i<priv->enabled->len; i++) {

		/* is already a -debuginfo */
		repo_id = g_ptr_array_index (priv->enabled, i);
		if (g_str_has_suffix (repo_id, "-debuginfo")) {
			g_debug ("already enabled: %s", repo_id);
			continue;
		}

		/* has a debuginfo repo */
		repo_id_debuginfo = g_strjoin ("-", repo_id, "debuginfo", NULL);
		ret = pk_debuginfo_install_in_array (priv->disabled, repo_id_debuginfo);
		if (ret) {
			/* add to list to change back at the end */
			g_ptr_array_add (added_repos, g_strdup (repo_id_debuginfo));
		} else {
			g_debug ("no debuginfo repo for %s", repo_id_debuginfo);
		}

		g_free (repo_id_debuginfo);
	}

	/* should be vocal? */
	if (!quiet) {
		/* TRANSLATORS: all completed 100% */
		g_print ("%s ", _("OK."));

		/* TRANSLATORS: tell the user what we found */
		g_print (_("Found %i disabled debuginfo repos."), added_repos->len);
		g_print ("\n");

		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: we're now enabling all the debug sources we found */
		g_print (_("Enabling debugging sources"));
		g_print ("...");
	}

	/* enable all debuginfo repos we found */
	ret = pk_debuginfo_install_enable_repos (priv, added_repos, TRUE, &error);
	if (!ret) {
		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: operation was not successful */
			g_print ("%s ", _("FAILED."));
		}
		/* TRANSLATORS: we're failed to enable the sources, detailed error follows */
		g_print ("Failed to enable debugging sources: %s", error->message);
		g_print ("\n");
		g_error_free (error);

		/* return correct failure retval */
		retval = PK_DEBUGINFO_EXIT_CODE_FAILED_TO_ENABLE;
		goto out;
	}

	/* should be vocal? */
	if (!quiet) {
		/* TRANSLATORS: all completed 100% */
		g_print ("%s ", _("OK."));

		/* TRANSLATORS: tell the user how many we enabled */
		g_print (_("Enabled %i debugging sources."), added_repos->len);
		g_print ("\n");

		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: we're now finding packages that match in all the repos */
		g_print (_("Finding debugging packages"));
		g_print ("...");
	}

	/* parse arguments and resolve to packages */
	for (i=1; argv[i] != NULL; i++) {
		name = pk_get_package_name_from_nevra (argv[i]);

		/* resolve name */
		package_id = pk_debuginfo_install_resolve_name_to_id (priv, name, &error);
		if (package_id == NULL) {
			/* TRANSLATORS: we couldn't find the package name, non-fatal */
			g_print (_("Failed to find the package %s: %s"), name, error->message);
			g_print ("\n");
			g_error_free (error);
			/* don't quit, this is non-fatal */
			error = NULL;
		}

		/* add to array to install */
		if (package_id != NULL) {
			g_debug ("going to try to install: %s", package_id);
			g_ptr_array_add (package_ids_recognised, package_id);
		} else {
			goto not_found;
		}

		/* convert into basename */
		name_debuginfo = pk_debuginfo_install_name_to_debuginfo (name);
		g_debug ("install %s [%s]", argv[i], name_debuginfo);

		/* resolve name */
		package_id = pk_debuginfo_install_resolve_name_to_id (priv, name_debuginfo, &error);
		if (package_id == NULL) {
			/* TRANSLATORS: we couldn't find the debuginfo package name, non-fatal */
			g_print (_("Failed to find the debuginfo package %s: %s"), name_debuginfo, error->message);
			g_print ("\n");
			g_error_free (error);
			/* don't quit, this is non-fatal */
			error = NULL;
		}

		/* add to array to install */
		if (package_id != NULL && !g_str_has_suffix (package_id, "installed")) {
			g_debug ("going to try to install: %s", package_id);
			g_ptr_array_add (package_ids_to_install, g_strdup (package_id));
		}

		g_free (name_debuginfo);
not_found:
		g_free (package_id);
		g_free (name);
	}

	/* no packages? */
	if (package_ids_to_install->len == 0) {
		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: operation was not successful */
			g_print ("%s ", _("FAILED."));
		}

		/* TRANSLATORS: no debuginfo packages could be found to be installed */
		g_print (_("Found no packages to install."));
		g_print ("\n");

		/* return correct failure retval */
		retval = PK_DEBUGINFO_EXIT_CODE_NOTHING_TO_DO;
		goto out;
	}

	/* should be vocal? */
	if (!quiet) {
		/* TRANSLATORS: all completed 100% */
		g_print ("%s ", _("OK."));

		/* TRANSLATORS: tell the user we found some packages, and then list them */
		g_print (_("Found %i packages:"), package_ids_to_install->len);
		g_print ("\n");
	}

	/* optional */
	if (!no_depends) {

		/* save for later logic */
		i = package_ids_to_install->len;

		/* should be vocal? */
		if (!quiet) {
			/* starting this section */
			g_print ("%i. ", step++);

			/* TRANSLATORS: tell the user we are searching for deps */
			g_print (_("Finding packages that depend on these packages"));
			g_print ("...");
		}

		ret = pk_debuginfo_install_add_deps (priv, package_ids_recognised, package_ids_to_install, &error);
		if (!ret) {

			/* should be vocal? */
			if (!quiet) {
				/* TRANSLATORS: operation was not successful */
				g_print ("%s ", _("FAILED."));
			}
			/* TRANSLATORS: could not install, detailed error follows */
			g_print (_("Could not find dependant packages: %s"), error->message);
			g_print ("\n");
			g_error_free (error);

			/* return correct failure retval */
			retval = PK_DEBUGINFO_EXIT_CODE_FAILED_TO_FIND_DEPS;
			goto out;
		}

		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: all completed 100% */
			g_print ("%s ", _("OK."));

			if (i < package_ids_to_install->len) {
				/* TRANSLATORS: tell the user we found some more packages */
				g_print (_("Found %i extra packages."), package_ids_to_install->len - i);
				g_print ("\n");
			} else {
				/* TRANSLATORS: tell the user we found some more packages */
				g_print (_("No extra packages required."));
				g_print ("\n");
			}
		}
	}

	/* should be vocal? */
	if (!quiet) {
		/* TRANSLATORS: tell the user we found some packages (and deps), and then list them */
		g_print (_("Found %i packages to install:"), package_ids_to_install->len);
		g_print ("\n");
	}

	/* print list */
	if (!quiet)
		pk_debuginfo_install_print_array (package_ids_to_install);

	/* simulate mode for testing */
	if (simulate) {
		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: simulate mode is a testing mode where we quit before the action */
			g_print (_("Not installing packages in simulate mode"));
			g_print ("\n");
		}
		goto out;
	}

	/* should be vocal? */
	if (!quiet) {
		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: we are now installing the debuginfo packages we found earlier */
		g_print (_("Installing packages"));
		g_print ("...\n");
	}

	/* install */
	ret = pk_debuginfo_install_packages_install (priv, package_ids_to_install, &error);
	if (!ret) {
		/* should be vocal? */
		if (!quiet) {
			/* TRANSLATORS: operation was not successful */
			g_print ("%s ", _("FAILED."));
		}
		/* TRANSLATORS: could not install, detailed error follows */
		g_print (_("Could not install packages: %s"), error->message);
		g_print ("\n");
		g_error_free (error);

		/* return correct failure retval */
		retval = PK_DEBUGINFO_EXIT_CODE_FAILED_TO_INSTALL;
		goto out;
	}

	/* should be vocal? */
	if (!quiet) {
		/* TRANSLATORS: all completed 100% */
		g_print (_("OK."));
		g_print ("\n");
	}
out:
	if (package_ids_to_install != NULL) {
		g_ptr_array_foreach (package_ids_to_install, (GFunc) g_free, NULL);
		g_ptr_array_free (package_ids_to_install, TRUE);
	}
	if (package_ids_recognised != NULL) {
		g_ptr_array_foreach (package_ids_recognised, (GFunc) g_free, NULL);
		g_ptr_array_free (package_ids_recognised, TRUE);
	}
	if (added_repos != NULL) {

		/* should be vocal? */
		if (!quiet) {
			/* starting this section */
			g_print ("%i. ", step);

			/* TRANSLATORS: we are now disabling all debuginfo repos we previously enabled */
			g_print (_("Disabling sources previously enabled"));
			g_print ("...");
		}
		/* disable all debuginfo repos we previously enabled */
		ret = pk_debuginfo_install_enable_repos (priv, added_repos, FALSE, &error);
		if (!ret) {
			/* should be vocal? */
			if (!quiet) {
				/* TRANSLATORS: operation was not successful */
				g_print ("%s ", _("FAILED."));
			}
			/* TRANSLATORS: no debuginfo packages could be found to be installed, detailed error follows */
			g_print (_("Could not disable the debugging sources: %s"), error->message);
			g_print ("\n");
			g_error_free (error);

			/* return correct failure retval */
			retval = PK_DEBUGINFO_EXIT_CODE_FAILED_TO_DISABLE;

		} else {

			/* should be vocal? */
			if (!quiet) {
				/* TRANSLATORS: all completed 100% */
				g_print ("%s ", _("OK."));

				/* TRANSLATORS: we disabled all the debugging repos that we enabled before */
				g_print (_("Disabled %i debugging sources."), added_repos->len);
				g_print ("\n");
			}
		}

		g_ptr_array_foreach (added_repos, (GFunc) g_free, NULL);
		g_ptr_array_free (added_repos, TRUE);
	}
	if (priv->enabled != NULL) {
		g_ptr_array_foreach (priv->enabled, (GFunc) g_free, NULL);
		g_ptr_array_free (priv->enabled, TRUE);
	}
	if (priv->disabled != NULL) {
		g_ptr_array_foreach (priv->disabled, (GFunc) g_free, NULL);
		g_ptr_array_free (priv->disabled, TRUE);
	}
	if (priv->client != NULL)
		g_object_unref (priv->client);
	if (priv->progress_bar != NULL)
		g_object_unref (priv->progress_bar);
	g_free (priv);
	return retval;
}

