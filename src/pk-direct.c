/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#include <locale.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/pk-debug.h>
#include <packagekit-glib2/pk-error.h>
#include <packagekit-glib2/pk-item-progress.h>
#include <packagekit-glib2/pk-package.h>

#include "pk-cleanup.h"
#include "pk-backend.h"
#include "pk-shared.h"

#define PK_ERROR			1
#define PK_ERROR_INVALID_ARGUMENTS	0
#define PK_ERROR_NO_SUCH_CMD		1

typedef struct {
	GMainLoop		*loop;
	GOptionContext		*context;
	GPtrArray		*cmd_array;
	PkBackend		*backend;
	PkBackendJob		*job;
	gboolean		 value_only;
} PkDirectPrivate;

typedef gboolean (*PkDirectCommandCb)	(PkDirectPrivate	*util,
					 gchar		**values,
					 GError		**error);

typedef struct {
	gchar			*name;
	gchar			*arguments;
	gchar			*description;
	PkDirectCommandCb	 callback;
} PkDirectItem;

/**
 * pk_direct_item_free:
 **/
static void
pk_direct_item_free (PkDirectItem *item)
{
	g_free (item->name);
	g_free (item->arguments);
	g_free (item->description);
	g_free (item);
}

/**
 * pk_sort_command_name_cb:
 **/
static gint
pk_sort_command_name_cb (PkDirectItem **item1, PkDirectItem **item2)
{
	return g_strcmp0 ((*item1)->name, (*item2)->name);
}

/**
 * pk_direct_add:
 **/
static void
pk_direct_add (GPtrArray *array,
	     const gchar *name,
	     const gchar *arguments,
	     const gchar *description,
	     PkDirectCommandCb callback)
{
	PkDirectItem *item;
	guint i;
	_cleanup_strv_free_ gchar **names = NULL;

	g_return_if_fail (name != NULL);
	g_return_if_fail (description != NULL);
	g_return_if_fail (callback != NULL);

	/* add each one */
	names = g_strsplit (name, ",", -1);
	for (i = 0; names[i] != NULL; i++) {
		item = g_new0 (PkDirectItem, 1);
		item->name = g_strdup (names[i]);
		if (i == 0) {
			item->description = g_strdup (description);
		} else {
			/* TRANSLATORS: this is a command alias */
			item->description = g_strdup_printf (_("Alias to %s"),
							     names[0]);
		}
		item->arguments = g_strdup (arguments);
		item->callback = callback;
		g_ptr_array_add (array, item);
	}
}

/**
 * pk_direct_get_descriptions:
 **/
static gchar *
pk_direct_get_descriptions (GPtrArray *array)
{
	guint i;
	guint j;
	guint len;
	const guint max_len = 35;
	PkDirectItem *item;
	GString *string;

	/* print each command */
	string = g_string_new ("");
	for (i = 0; i < array->len; i++) {
		item = g_ptr_array_index (array, i);
		g_string_append (string, "  ");
		g_string_append (string, item->name);
		len = strlen (item->name) + 2;
		if (item->arguments != NULL) {
			g_string_append (string, " ");
			g_string_append (string, item->arguments);
			len += strlen (item->arguments) + 1;
		}
		if (len < max_len) {
			for (j = len; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		} else {
			g_string_append_c (string, '\n');
			for (j = 0; j < max_len + 1; j++)
				g_string_append_c (string, ' ');
			g_string_append (string, item->description);
			g_string_append_c (string, '\n');
		}
	}

	/* remove trailing newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * pk_direct_run:
 **/
static gboolean
pk_direct_run (PkDirectPrivate *priv, const gchar *command, gchar **values, GError **error)
{
	PkDirectItem *item;
	guint i;
	_cleanup_string_free_ GString *string = NULL;

	/* find command */
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		if (g_strcmp0 (item->name, command) == 0)
			return item->callback (priv, values, error);
	}

	/* not found */
	string = g_string_new ("");
	g_string_append_printf (string, "%s\n",
				_("Command not found, valid commands are:"));
	for (i = 0; i < priv->cmd_array->len; i++) {
		item = g_ptr_array_index (priv->cmd_array, i);
		g_string_append_printf (string, " * %s %s\n",
					item->name,
					item->arguments ? item->arguments : "");
	}
	g_set_error_literal (error, PK_ERROR, PK_ERROR_NO_SUCH_CMD, string->str);
	return FALSE;
}

/**
 * pk_direct_refresh:
 **/
static gboolean
pk_direct_refresh (PkDirectPrivate *priv, gchar **values, GError **error)
{
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_refresh_cache (priv->backend, priv->job, FALSE);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_refresh_force:
 **/
static gboolean
pk_direct_refresh_force (PkDirectPrivate *priv, gchar **values, GError **error)
{
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_refresh_cache (priv->backend, priv->job, TRUE);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_search_names:
 **/
static gboolean
pk_direct_search_names (PkDirectPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     PK_ERROR,
				     PK_ERROR_INVALID_ARGUMENTS,
				     "Not enough arguments, expected: <search>");
		return FALSE;
	}
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_search_names (priv->backend, priv->job, 0, values);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_search_details:
 **/
static gboolean
pk_direct_search_details (PkDirectPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     PK_ERROR,
				     PK_ERROR_INVALID_ARGUMENTS,
				     "Not enough arguments, expected: <search>");
		return FALSE;
	}
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_search_details (priv->backend, priv->job, 0, values);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_search_files:
 **/
static gboolean
pk_direct_search_files (PkDirectPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     PK_ERROR,
				     PK_ERROR_INVALID_ARGUMENTS,
				     "Not enough arguments, expected: <search>");
		return FALSE;
	}
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_search_files (priv->backend, priv->job, 0, values);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_install:
 **/
static gboolean
pk_direct_install (PkDirectPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     PK_ERROR,
				     PK_ERROR_INVALID_ARGUMENTS,
				     "Not enough arguments, expected: <pkgid>");
		return FALSE;
	}
	if (!pk_package_id_check (values[0])) {
		g_set_error (error,
			     PK_ERROR,
			     PK_ERROR_INVALID_ARGUMENTS,
			     "Not a package-id: %s", values[0]);
		return FALSE;
	}
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_install_packages (priv->backend, priv->job, 0, values);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_remove:
 **/
static gboolean
pk_direct_remove (PkDirectPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) < 1) {
		g_set_error_literal (error,
				     PK_ERROR,
				     PK_ERROR_INVALID_ARGUMENTS,
				     "Not enough arguments, expected: <pkgid>");
		return FALSE;
	}
	if (!pk_package_id_check (values[0])) {
		g_set_error (error,
			     PK_ERROR,
			     PK_ERROR_INVALID_ARGUMENTS,
			     "Not a package-id: %s", values[0]);
		return FALSE;
	}
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_remove_packages (priv->backend, priv->job, 0, values, FALSE, FALSE);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_repo_set_data:
 **/
static gboolean
pk_direct_repo_set_data (PkDirectPrivate *priv, gchar **values, GError **error)
{
	if (g_strv_length (values) != 3) {
		g_set_error_literal (error,
				     PK_ERROR,
				     PK_ERROR_INVALID_ARGUMENTS,
				     "Not enough arguments, "
				     "expected: [id] [key] [value]");
		return FALSE;
	}
	pk_backend_start_job (priv->backend, priv->job);
	pk_backend_repo_set_data (priv->backend, priv->job,
				  values[0], values[1], values[2]);
	g_main_loop_run (priv->loop);
	pk_backend_stop_job (priv->backend, priv->job);
	return TRUE;
}

/**
 * pk_direct_sigint_cb:
 **/
static gboolean
pk_direct_sigint_cb (gpointer user_data)
{
	PkDirectPrivate *priv = (PkDirectPrivate *) user_data;
	g_debug ("Handling SIGINT");
	g_main_loop_quit (priv->loop);
	return FALSE;
}

/**
 * pk_direct_finished_cb:
 **/
static void
pk_direct_finished_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkExitEnum exit_enum = GPOINTER_TO_UINT (object);
	PkDirectPrivate *priv = (PkDirectPrivate *) user_data;

	g_print ("Exit code: %s\n", pk_exit_enum_to_string (exit_enum));
	g_main_loop_quit (priv->loop);
}

/**
 * pk_direct_percentage_cb:
 **/
static void
pk_direct_percentage_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	guint percentage = GPOINTER_TO_UINT (object);
	g_print ("Done: %i%%\n", percentage);
}

/**
 * pk_direct_status_changed_cb:
 **/
static void
pk_direct_status_changed_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkStatusEnum status_enum = GPOINTER_TO_UINT (object);
	g_print ("Status: %s\n", pk_status_enum_to_string (status_enum));
}

/**
 * pk_direct_package_cb:
 **/
static void
pk_direct_package_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkPackage *pkg = PK_PACKAGE (object);
	g_print ("Package: %s\t%s\n",
		 pk_info_enum_to_string (pk_package_get_info (pkg)),
		 pk_package_get_id (pkg));
}

/**
 * pk_direct_error_cb:
 **/
static void
pk_direct_error_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkError *err = PK_ERROR_CODE (object);
	g_print ("Error: %s\t%s\n",
		 pk_error_enum_to_string (pk_error_get_code (err)),
		 pk_error_get_details (err));
}

/**
 * pk_direct_item_progress_cb:
 **/
static void
pk_direct_item_progress_cb (PkBackendJob *job, gpointer object, gpointer user_data)
{
	PkItemProgress *ip = PK_ITEM_PROGRESS (object);
	g_print ("ItemProgress: %s\t%i%%\t%s\n",
		 pk_status_enum_to_string (pk_item_progress_get_status (ip)),
		 pk_item_progress_get_percentage (ip),
		 pk_item_progress_get_package_id (ip));
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	PkDirectPrivate *priv = NULL;
	const gchar *destdir;
	gboolean ret = TRUE;
	gint retval = EXIT_SUCCESS;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *backend_name = NULL;
	_cleanup_free_ gchar *cmd_descriptions = NULL;
	_cleanup_free_ gchar *conf_filename = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;

	const GOptionEntry options[] = {
		{ "backend", '\0', 0, G_OPTION_ARG_STRING, &backend_name,
		  /* TRANSLATORS: a backend is the system package tool, e.g. dnf, apt */
		  _("Packaging backend to use, e.g. dummy"), NULL },
		{ NULL }
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* create priv object */
	priv = g_new0 (PkDirectPrivate, 1);

	/* add commands */
	priv->cmd_array = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_direct_item_free);
	pk_direct_add (priv->cmd_array, "refresh", NULL,
		       /* TRANSLATORS: command description */
		       _("Refresh the cache"),
		       pk_direct_refresh);
	pk_direct_add (priv->cmd_array, "refresh-force", NULL,
		       /* TRANSLATORS: command description */
		       _("Refresh the cache (forced)"),
		       pk_direct_refresh_force);
	pk_direct_add (priv->cmd_array, "search-name", "[SEARCH]",
		       /* TRANSLATORS: command description */
		       _("Search by names"),
		       pk_direct_search_names);
	pk_direct_add (priv->cmd_array, "search-detail", "[SEARCH]",
		       /* TRANSLATORS: command description */
		       _("Search by details"),
		       pk_direct_search_details);
	pk_direct_add (priv->cmd_array, "search-file", "[SEARCH]",
		       /* TRANSLATORS: command description */
		       _("Search by files"),
		       pk_direct_search_files);
	pk_direct_add (priv->cmd_array, "install", "[PKGID]",
		       /* TRANSLATORS: command description */
		       _("Install package"),
		       pk_direct_install);
	pk_direct_add (priv->cmd_array, "remove", "[PKGID]",
		       /* TRANSLATORS: command description */
		       _("Remove package"),
		       pk_direct_remove);
	pk_direct_add (priv->cmd_array, "repo-set-data", "[REPO] [KEY] [VALUE]",
		       /* TRANSLATORS: command description */
		       _("Set repository options"),
		       pk_direct_repo_set_data);

	/* sort by command name */
	g_ptr_array_sort (priv->cmd_array,
			  (GCompareFunc) pk_sort_command_name_cb);

	/* get a list of the commands */
	priv->context = g_option_context_new (NULL);
	cmd_descriptions = pk_direct_get_descriptions (priv->cmd_array);
	g_option_context_set_summary (priv->context, cmd_descriptions);

	/* TRANSLATORS: program name */
	g_set_application_name (_("PackageKit"));
	g_option_context_add_main_entries (priv->context, options, NULL);
	g_option_context_add_group (priv->context, pk_debug_get_option_group ());
	ret = g_option_context_parse (priv->context, &argc, &argv, &error);
	if (!ret) {
		/* TRANSLATORS: the user didn't read the man page */
		g_print ("%s: %s\n", _("Failed to parse arguments"), error->message);
		goto out;
	}

	/* get values from the config file */
	conf = g_key_file_new ();
	conf_filename = pk_util_get_config_filename ();
	ret = g_key_file_load_from_file (conf, conf_filename,
					 G_KEY_FILE_NONE, &error);
	if (!ret) {
		/* TRANSLATORS: probably not yet installed */
		g_print ("%s: %s\n", _("Failed to load the config file"), error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* support DESTDIR */
	destdir = g_getenv ("DESTDIR");
	if (destdir != NULL)
		g_key_file_set_string (conf, "Daemon", "DestDir", destdir);

	/* override the backend name */
	if (backend_name != NULL)
		g_key_file_set_string (conf, "Daemon", "DefaultBackend", backend_name);

	/* resolve 'auto' to an actual name */
	backend_name = g_key_file_get_string (conf, "Daemon", "DefaultBackend", NULL);
	if (backend_name == NULL || g_strcmp0 (backend_name, "auto") == 0) {
		if (!pk_util_set_auto_backend (conf, &error)) {
			g_print ("Failed to resolve auto: %s\n", error->message);
			retval = EXIT_FAILURE;
			goto out;
		}
	}

	/* do stuff on ctrl-c */
	priv->loop = g_main_loop_new (NULL, FALSE);
	g_unix_signal_add_full (G_PRIORITY_DEFAULT, SIGINT,
				pk_direct_sigint_cb, &priv, NULL);

	/* load the backend */
	priv->backend = pk_backend_new (conf);
	if (!pk_backend_load (priv->backend, &error)) {
		/* TRANSLATORS: cannot load the backend the user specified */
		g_print ("%s: %s\n", _("Failed to load the backend"), error->message);
		retval = EXIT_FAILURE;
		goto out;
	}

	/* set up the job */
	priv->job = pk_backend_job_new (conf);
	pk_backend_job_set_cache_age (priv->job, G_MAXUINT);
	pk_backend_job_set_backend (priv->job, priv->backend);
	pk_backend_job_set_vfunc (priv->job, PK_BACKEND_SIGNAL_FINISHED,
				  pk_direct_finished_cb, priv);
	pk_backend_job_set_vfunc (priv->job, PK_BACKEND_SIGNAL_PERCENTAGE,
				  pk_direct_percentage_cb, priv);
	pk_backend_job_set_vfunc (priv->job, PK_BACKEND_SIGNAL_STATUS_CHANGED,
				  pk_direct_status_changed_cb, priv);
	pk_backend_job_set_vfunc (priv->job, PK_BACKEND_SIGNAL_PACKAGE,
				  pk_direct_package_cb, priv);
	pk_backend_job_set_vfunc (priv->job, PK_BACKEND_SIGNAL_ERROR_CODE,
				  pk_direct_error_cb, priv);
	pk_backend_job_set_vfunc (priv->job, PK_BACKEND_SIGNAL_ITEM_PROGRESS,
				  pk_direct_item_progress_cb, priv);

	/* run the specified command */
	ret = pk_direct_run (priv, argv[1], (gchar**) &argv[2], &error);
	if (!ret) {
		if (g_error_matches (error, PK_ERROR, PK_ERROR_NO_SUCH_CMD)) {
			_cleanup_free_ gchar *tmp = NULL;
			tmp = g_option_context_get_help (priv->context, TRUE, NULL);
			g_print ("%s", tmp);
		} else {
			g_print ("%s\n", error->message);
		}
		goto out;
	}

	/* unload backend */
	if (!pk_backend_unload (priv->backend)) {
		/* TRANSLATORS: cannot unload the backend the user specified */
		g_print ("%s\n", _("Failed to unload the backend"));
		retval = EXIT_FAILURE;
		goto out;
	}
out:
	if (priv->cmd_array != NULL)
		g_ptr_array_unref (priv->cmd_array);
	if (priv->backend != NULL)
		g_object_unref (priv->backend);
	if (priv->job != NULL)
		g_object_unref (priv->job);
	if (priv->loop != NULL)
		g_main_loop_unref (priv->loop);
	g_option_context_free (priv->context);
	g_free (priv);
	return retval;
}
