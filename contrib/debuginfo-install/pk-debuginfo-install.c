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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* Test with ./pk-debuginfo-install bzip2-libs-1.0.5-5.fc11.i586 glib2-2.20.3-1.fc11.i586 */

#include "config.h"

#include <string.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"

typedef struct {
	GPtrArray *enabled;
	GPtrArray *disabled;
	PkClient *client;
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
 * pk_debuginfo_install_repo_details_cb:
 **/
static void
pk_debuginfo_install_repo_details_cb (PkClient *client, const gchar *repo_id, const gchar *description, gboolean enabled, PkDebuginfoInstallPrivate *priv)
{
	if (enabled)
		g_ptr_array_add (priv->enabled, g_strdup (repo_id));
	else
		g_ptr_array_add (priv->disabled, g_strdup (repo_id));
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
	const gchar *repo_id;
	GError *error_local = NULL;

	/* enable all debuginfo repos we found */
	for (i=0; i<array->len; i++) {
		repo_id = g_ptr_array_index (array, i);

		/* reset client */
		ret = pk_client_reset (priv->client, &error_local);
		if (!ret) {
			*error = g_error_new (1, 0, "failed to reset: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* enable this repo */
		ret = pk_client_repo_enable (priv->client, repo_id, enable, &error_local);
		if (!ret) {
			*error = g_error_new (1, 0, "failed to enable %s: %s", repo_id, error_local->message);
			g_error_free (error_local);
			goto out;
		}
		egg_debug ("setting %s: %i", repo_id, enable);
	}
out:
	return ret;
}

/**
 * pk_debuginfo_install_packages_install:
 **/
static gboolean
pk_debuginfo_install_packages_install (PkDebuginfoInstallPrivate *priv, GPtrArray *array, GError **error)
{
	gboolean ret = TRUE;
	gchar **package_ids;
	GError *error_local = NULL;

	/* mush back into a char** */
	package_ids = pk_package_ids_from_array (array);

	/* reset client */
	ret = pk_client_reset (priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* enable this repo */
	ret = pk_client_install_packages (priv->client, TRUE, package_ids, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to install packages: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_debuginfo_install_resolve_name_to_id:
 **/
static gchar *
pk_debuginfo_install_resolve_name_to_id (PkDebuginfoInstallPrivate *priv, const gchar *package_name, GError **error)
{
	gboolean ret;
	const PkPackageObj *obj;
	const PkPackageId *id;
	gchar *package_id = NULL;
	PkPackageList *list = NULL;
	GError *error_local = NULL;
	gchar **names;
	guint len;

	/* resolve takes a char** */
	names = g_strsplit (package_name, ";", -1);

	/* reset client */
	ret = pk_client_reset (priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* resolve */
	ret = pk_client_resolve (priv->client, pk_bitfield_from_enums (PK_FILTER_ENUM_NEWEST, -1), names, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to resolve: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check we only got one match */
	list = pk_client_get_package_list (priv->client);
	len = PK_OBJ_LIST(list)->len;
	if (len == 0) {
		*error = g_error_new (1, 0, "no package %s found", package_name);
		goto out;
	}
	if (len > 1) {
		*error = g_error_new (1, 0, "more than one package found for %s", package_name);
		goto out;
	}

	/* get the package id */
	obj = pk_package_list_get_obj (list, 0);
	id = pk_package_obj_get_id (obj);
	package_id = pk_package_id_to_string (id);
out:
	if (list != NULL)
		g_object_unref (list);
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
	PkPackageId *id;
	const gchar *package_id;

	for (i=0; i<array->len; i++) {
		package_id = g_ptr_array_index (array, i);
		id = pk_package_id_new_from_string (package_id);
		g_print ("%i\t%s-%s(%s)\t%s\n", i+1, id->name, id->version, id->arch, id->data);
		pk_package_id_free (id);
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
	gboolean ret;
	const PkPackageObj *obj;
	const PkPackageId *id;
	gchar *package_id = NULL;
	PkPackageList *list = NULL;
	GError *error_local = NULL;
	gchar **package_ids;
	gchar *name_debuginfo;
	guint len;
	guint i;

	/* reset client */
	ret = pk_client_reset (priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get depends for them all, not adding dup's */
	package_ids = pk_package_ids_from_array (packages_search);
	ret = pk_client_get_depends (priv->client, PK_FILTER_ENUM_NONE, package_ids, TRUE, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to get_depends: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* add dependant packages */
	list = pk_client_get_package_list (priv->client);
	len = PK_OBJ_LIST(list)->len;
	for (i=0; i<len; i++) {
		obj = pk_package_list_get_obj (list, 0);
		id = pk_package_obj_get_id(obj);

		/* add -debuginfo */
		name_debuginfo = pk_debuginfo_install_name_to_debuginfo (id->name);

		/* resolve name */
		egg_debug ("resolving: %s", name_debuginfo);
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
			egg_debug ("going to try to install (for deps): %s", package_id);
			g_ptr_array_add (packages_results, g_strdup (package_id));
		}

		g_free (package_id);
		g_free (name_debuginfo);
	}
out:
	if (list != NULL)
		g_object_unref (list);
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_console_progress_changed_cb:
 **/
static void
pk_console_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				guint elapsed, guint remaining, PkDebuginfoInstallPrivate *priv)
{
	PkRoleEnum role;
	pk_client_get_role (client, &role, NULL, NULL);

	/* ignore everything except InstallPackages */
	if (role != PK_ROLE_ENUM_INSTALL_PACKAGES) {
		egg_debug ("ignoring %s progress", pk_role_enum_to_text (role));
		goto out;
	}

	if (percentage != PK_CLIENT_PERCENTAGE_INVALID)
		g_print ("%s: %i%%\n", _("Percentage"), percentage);
	else
		g_print ("%s: %s\n", _("Percentage"), _("Unknown"));
out:
	return;
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
	gchar *package_id;
	gchar *name;
	gchar *name_debuginfo;
	gboolean verbose = FALSE;
	gboolean simulate = FALSE;
	gboolean no_depends = FALSE;
	GOptionContext *context;
	const gchar *repo_id;
	gchar *repo_id_debuginfo;
	PkDebuginfoInstallPrivate _priv;
	PkDebuginfoInstallPrivate *priv = &_priv;
	guint step = 1;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ "simulate", 's', 0, G_OPTION_ARG_NONE, &simulate,
		   /* command line argument, simulate what would be done, but don't actually do it */
		  _("Don't actually install any packages, only simulate"), NULL },
		{ "--no-depends", 'n', 0, G_OPTION_ARG_NONE, &no_depends,
		   /* command line argument, do we skip packages that depend on the ones specified */
		  _("Do not install dependencies of the core packages"), NULL },
		{ NULL}
	};

	/* clear private struct */
	memset (priv, 0, sizeof (PkDebuginfoInstallPrivate));

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
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);

	/* no input */
	if (argv[1] == NULL) {
		/* TRANSLATORS: the use needs to specify a list of package names on the command line */
		g_print (_("ERROR: Specify package names to install."));
		g_print ("\n");
		goto out;
	}

	/* store as strings */
	priv->enabled = g_ptr_array_new ();
	priv->disabled = g_ptr_array_new ();
	added_repos = g_ptr_array_new ();
	package_ids_to_install = g_ptr_array_new ();
	package_ids_recognised = g_ptr_array_new ();

	/* create #PkClient */
	priv->client = pk_client_new ();
	g_signal_connect (priv->client, "repo-detail", G_CALLBACK (pk_debuginfo_install_repo_details_cb), priv);
	g_signal_connect (priv->client, "progress-changed", G_CALLBACK (pk_console_progress_changed_cb), priv);
	pk_client_set_synchronous (priv->client, TRUE, NULL);
	pk_client_set_use_buffer (priv->client, TRUE, NULL);

	/* starting this section */
	g_print ("%i. ", step++);

	/* TRANSLATORS: we are getting the list of repositories */
	g_print (_("Getting sources list"));
	g_print ("...");

	/* get all enabled repos */
	ret = pk_client_get_repo_list (priv->client, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		g_print ("failed to get repo list: %s", error->message);
		g_error_free (error);
		goto out;
	}

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

	/* find all debuginfo repos for repos that are enabled */
	for (i=0; i<priv->enabled->len; i++) {

		/* is already a -debuginfo */
		repo_id = g_ptr_array_index (priv->enabled, i);
		if (g_str_has_suffix (repo_id, "-debuginfo")) {
			egg_debug ("already enabled: %s", repo_id);
			continue;
		}

		/* has a debuginfo repo */
		repo_id_debuginfo = g_strjoin ("-", repo_id, "debuginfo", NULL);
		ret = pk_debuginfo_install_in_array (priv->disabled, repo_id_debuginfo);
		if (ret) {
			/* add to list to change back at the end */
			g_ptr_array_add (added_repos, g_strdup (repo_id_debuginfo));
		} else {
			egg_debug ("no debuginfo repo for %s", repo_id_debuginfo);
		}

		g_free (repo_id_debuginfo);
	}

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

	/* enable all debuginfo repos we found */
	ret = pk_debuginfo_install_enable_repos (priv, added_repos, TRUE, &error);
	if (!ret) {
		/* TRANSLATORS: operation was not successful */
		g_print ("%s ", _("FAILED."));

		/* TRANSLATORS: we're failed to enable the sources, detailed error follows */
		g_print ("Failed to enable debugging sources: %s", error->message);
		g_print ("\n");
		g_error_free (error);
		goto out;
	}

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
			egg_debug ("going to try to install: %s", package_id);
			g_ptr_array_add (package_ids_recognised, g_strdup (package_id));
		} else {
			goto not_found;
		}

		/* convert into basename */
		name_debuginfo = pk_debuginfo_install_name_to_debuginfo (name);
		egg_debug ("install %s [%s]", argv[i], name_debuginfo);

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
			egg_debug ("going to try to install: %s", package_id);
			g_ptr_array_add (package_ids_to_install, g_strdup (package_id));
		}

		g_free (name_debuginfo);
not_found:
		g_free (package_id);
		g_free (name);
	}

	/* no packages? */
	if (package_ids_to_install->len == 0) {
		/* TRANSLATORS: operation was not successful */
		g_print ("%s ", _("FAILED."));

		/* TRANSLATORS: no debuginfo packages could be found to be installed */
		g_print (_("Found no packages to install."));
		g_print ("\n");
		goto out;
	}

	/* TRANSLATORS: all completed 100% */
	g_print ("%s ", _("OK."));

	/* TRANSLATORS: tell the user we found some packages, and then list them */
	g_print (_("Found %i packages:"), package_ids_to_install->len);
	g_print ("\n");

	/* optional */
	if (!no_depends) {

		/* save for later logic */
		i = package_ids_to_install->len;

		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: tell the user we are searching for deps */
		g_print (_("Finding packages that depend on these packages"));
		g_print ("...");

		ret = pk_debuginfo_install_add_deps (priv, package_ids_recognised, package_ids_to_install, &error);
		if (!ret) {
			/* TRANSLATORS: operation was not successful */
			g_print ("%s ", _("FAILED."));

			/* TRANSLATORS: could not install, detailed error follows */
			g_print (_("Could not find dependant packages: %s"), error->message);
			g_print ("\n");
			g_error_free (error);
			goto out;
		}

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

	/* TRANSLATORS: tell the user we found some packages (and deps), and then list them */
	g_print (_("Found %i packages to install:"), package_ids_to_install->len);
	g_print ("\n");

	/* print list */
	pk_debuginfo_install_print_array (package_ids_to_install);

	/* simulate mode for testing */
	if (simulate) {
		/* TRANSLATORS: simulate mode is a testing mode where we quit before the action */
		g_print (_("Not installing packages in simulate mode"));
		g_print ("\n");
		goto out;
	}

	/* starting this section */
	g_print ("%i. ", step++);

	/* TRANSLATORS: we are now installing the debuginfo packages we found earlier */
	g_print (_("Installing packages"));
	g_print ("...");

	/* install */
	ret = pk_debuginfo_install_packages_install (priv, package_ids_to_install, &error);
	if (!ret) {
		/* TRANSLATORS: operation was not successful */
		g_print ("%s ", _("FAILED."));

		/* TRANSLATORS: coul dnot install, detailed error follows */
		g_print (_("Could not install packages: %s"), error->message);
		g_print ("\n");
		g_error_free (error);
		goto out;
	}

	/* TRANSLATORS: all completed 100% */
	g_print (_("OK."));
	g_print ("\n");
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

		/* starting this section */
		g_print ("%i. ", step++);

		/* TRANSLATORS: we are now disabling all debuginfo repos we previously enabled */
		g_print (_("Disabling sources previously enabled"));
		g_print ("...");

		/* disable all debuginfo repos we previously enabled */
		ret = pk_debuginfo_install_enable_repos (priv, added_repos, FALSE, &error);
		if (!ret) {
			/* TRANSLATORS: operation was not successful */
			g_print ("%s ", _("FAILED."));

			/* TRANSLATORS: no debuginfo packages could be found to be installed, detailed error follows */
			g_print (_("Could not disable the debugging sources: %s"), error->message);
			g_print ("\n");
			g_error_free (error);
		} else {

			/* TRANSLATORS: all completed 100% */
			g_print ("%s ", _("OK."));

			/* TRANSLATORS: we disabled all the debugging repos that we enabled before */
			g_print (_("Disabled %i debugging sources."), added_repos->len);
			g_print ("\n");
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
	return 0;
}

