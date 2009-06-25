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

#include "config.h"

#include <string.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"

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

typedef struct {
	GPtrArray *enabled;
	GPtrArray *disabled;
	PkClient *client;
} PkPriv;

/**
 * repo_details_cb:
 **/
static void
repo_details_cb (PkClient *client, const gchar *repo_id, const gchar *description, gboolean enabled, PkPriv *priv)
{
	if (enabled)
		g_ptr_array_add (priv->enabled, g_strdup (repo_id));
	else
		g_ptr_array_add (priv->disabled, g_strdup (repo_id));
}

/**
 * is_in_array:
 **/
static gboolean
is_in_array (GPtrArray *array, const gchar *text)
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
 * enable_repos:
 **/
static gboolean
enable_repos (PkPriv *priv, GPtrArray *array, gboolean enable, GError **error)
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
		egg_warning ("setting %s: %i", repo_id, enable);
	}
out:
	return ret;
}

/**
 * install_packages:
 **/
static gboolean
install_packages (PkPriv *priv, GPtrArray *array, GError **error)
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
 * resolve_to_id:
 **/
static gchar *
resolve_to_id (PkPriv *priv, const gchar *package_name, GError **error)
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
	ret = pk_client_resolve (priv->client, pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED, -1), names, &error_local);
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
	id = pk_package_obj_get_id(obj);
	package_id = pk_package_id_to_string (id);
out:
	if (list != NULL)
		g_object_unref (list);
	g_strfreev (names);
	return package_id;
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
	GPtrArray *package_ids_to_install = NULL;
	guint i;
	gchar *package_id;
	gchar *name;
	gchar *name_debuginfo;
	gboolean verbose = FALSE;
	GOptionContext *context;
	const gchar *repo_id;
	gchar *repo_id_debuginfo;
	PkPriv _priv;
	PkPriv *priv = &_priv;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show extra debugging information"), NULL },
		{ NULL}
	};

	memset (priv, 0, sizeof (PkPriv));

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: tool that gets called when the command is not found */
	g_option_context_set_summary (context, _("PackageKit Command Not Found"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	egg_debug_init (verbose);

	/* no input */
	if (argv[1] == NULL) {
		g_print ("specify packages to install\n");
		goto out;
	}

	/* store as strings */
	priv->enabled = g_ptr_array_new ();
	priv->disabled = g_ptr_array_new ();
	added_repos = g_ptr_array_new ();
	package_ids_to_install = g_ptr_array_new ();

	/* create #PkClient */
	priv->client = pk_client_new ();
	g_signal_connect (priv->client, "repo-detail", G_CALLBACK (repo_details_cb), priv);
	pk_client_set_synchronous (priv->client, TRUE, NULL);
	pk_client_set_use_buffer (priv->client, TRUE, NULL);

	/* get all enabled repos */
	g_print ("1. Getting sources list...");
	ret = pk_client_get_repo_list (priv->client, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		g_print ("failed to get repo list: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_print ("OK. Found %i enabled and %i disabled sources.\n", priv->enabled->len, priv->disabled->len);

	/* find all debuginfo repos for repos that are enabled */
	g_print ("2. Finding debug sources...");
	for (i=0; i<priv->enabled->len; i++) {

		/* is already a -debuginfo */
		repo_id = g_ptr_array_index (priv->enabled, i);
		if (g_str_has_suffix (repo_id, "-debuginfo")) {
			egg_debug ("already enabled: %s", repo_id);
			continue;
		}

		/* has a debuginfo repo */
		repo_id_debuginfo = g_strjoin ("-", repo_id, "debuginfo", NULL);
		ret = is_in_array (priv->disabled, repo_id_debuginfo);
		if (ret) {
			/* add to list to change back at the end */
			g_ptr_array_add (added_repos, g_strdup (repo_id_debuginfo));
		} else {
			egg_debug ("no debuginfo repo for %s", repo_id_debuginfo);
		}

		g_free (repo_id_debuginfo);
	}
	g_print ("OK. Found %i non-enabled debuginfo repos.\n", added_repos->len);

	/* enable all debuginfo repos we found */
	g_print ("3. Enabling debug sources...");
	ret = enable_repos (priv, added_repos, TRUE, &error);
	if (!ret) {
		g_print ("failed to enable repos: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_print ("OK. Enabled %i sources.\n", added_repos->len);

//debuginfo-install bzip2-libs-1.0.5-5.fc11.i586 glib2-2.20.3-1.fc11.i586 libacl-2.2.47-4.fc11.i586 libarchive-2.6.2-1.fc11.i586 lzma-libs-4.32.7-2.fc11.i586 sqlite-3.6.12-3.fc11.i586 zlib-1.2.3-22.fc11.i586

	/* parse arguments and resolve to packages */
	g_print ("4. Finding debug packages...");
	for (i=1; argv[i] != NULL; i++) {
		name = pk_get_package_name_from_nevra (argv[i]);
		name_debuginfo = g_strjoin ("-", name, "debuginfo", NULL);
		egg_debug ("install %s [%s]", argv[i], name_debuginfo);

		/* resolve name */
		package_id = resolve_to_id (priv, name_debuginfo, &error);
		if (package_id == NULL) {
			g_print ("failed to map package name to an id: %s\n", error->message);
			g_error_free (error);
			/* don't quit, this is non-fatal */
			error = NULL;
		}

		/* add to array to install */
		if (package_id != NULL) {
			egg_debug ("going to try to install: %s\n", package_id);
			g_ptr_array_add (package_ids_to_install, g_strdup (package_id));
		}

		g_free (name);
		g_free (name_debuginfo);
		g_free (package_id);

	}
	g_print ("OK. Found %i packages.\n", package_ids_to_install->len);

	/* install */
	g_print ("5. Installing packages...");
	ret = install_packages (priv, package_ids_to_install, &error);
	if (!ret) {
		g_print ("failed to install packages: %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_print ("OK.\n");

out:

	if (package_ids_to_install != NULL) {
		g_ptr_array_foreach (package_ids_to_install, (GFunc) g_free, NULL);
		g_ptr_array_free (package_ids_to_install, TRUE);
	}
	if (added_repos != NULL) {

		g_print ("6. Disabling sources...");
		/* disable all debuginfo repos we previously enabled */
		ret = enable_repos (priv, added_repos, FALSE, &error);
		if (!ret) {
			g_print ("failed to enable repos: %s", error->message);
			g_error_free (error);
		}
		g_print ("OK. Disabled %i sources.\n", added_repos->len);

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

