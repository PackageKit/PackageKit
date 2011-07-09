/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#include <config.h>
#include <gio/gio.h>
#include <pk-transaction.h>

#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-files.h>

#include "pk-proc.h"

typedef struct {
	GMainLoop		*loop;
	GPtrArray		*files_list;
	gchar			**no_update_process_list;
	PkProc			*proc;
} PluginPrivate;

static PluginPrivate *priv;

/**
 * pk_transaction_plugin_get_description:
 */
const gchar *
pk_transaction_plugin_get_description (void)
{
	return "Updates the package lists after refresh";
}

/**
 * pk_plugin_finished_cb:
 **/
static void
pk_plugin_finished_cb (PkBackend *backend,
		       PkExitEnum exit_enum,
		       gpointer user_data)
{
	if (!g_main_loop_is_running (priv->loop))
		return;
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_warning ("%s failed with exit code: %s",
			     pk_role_enum_to_string (pk_backend_get_role (backend)),
			     pk_exit_enum_to_string (exit_enum));
	}
	g_main_loop_quit (priv->loop);
}

/**
 * pk_transaction_plugin_initialize:
 */
void
pk_transaction_plugin_initialize (PkTransaction *transaction)
{
	PkConf *conf;

	/* create private area */
	priv = g_new0 (PluginPrivate, 1);
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->files_list = g_ptr_array_new_with_free_func (g_free);
	priv->proc = pk_proc_new ();

	/* get the list of processes we should neverupdate when running */
	conf = pk_transaction_priv_get_conf (transaction);
	priv->no_update_process_list = pk_conf_get_strv (conf, "NoUpdateProcessList");

	g_debug ("plugin: initialize");
}

/**
 * pk_transaction_plugin_destroy:
 */
void
pk_transaction_plugin_destroy (PkTransaction *transaction)
{
	g_debug ("plugin: destroy");
	g_main_loop_unref (priv->loop);
	g_strfreev (priv->no_update_process_list);
	g_ptr_array_unref (priv->files_list);
	g_object_unref (priv->proc);
	g_free (priv);
}

/**
 * pk_plugin_match_running_file:
 *
 * Only if the pattern matches the old and new names we refuse to run
 **/
static gboolean
pk_plugin_match_running_file (gpointer user_data, const gchar *filename)
{
	guint i;
	gchar **list;
	gboolean ret;

	/* compare each pattern */
	list = priv->no_update_process_list;
	for (i=0; list[i] != NULL; i++) {

		/* does the package filename match */
		ret = g_pattern_match_simple (list[i], filename);
		if (ret) {
			/* is there a running process that also matches */
			ret = pk_proc_find_exec (priv->proc, list[i]);
			if (ret)
				goto out;
		}
	}

	/* we failed */
	ret = FALSE;
out:
	return ret;
}

/**
 * pk_plugin_files_cb:
 **/
static void
pk_plugin_files_cb (PkBackend *backend,
		    PkFiles *files,
		    gpointer user_data)
{
	guint i;
	guint len;
	gboolean ret;
	gchar **filenames = NULL;

	/* get data */
	g_object_get (files,
		      "files", &filenames,
		      NULL);

	/* check each file to see if it's a system shared library */
	len = g_strv_length (filenames);
	g_debug ("len=%i", len);
	for (i=0; i<len; i++) {

		/* does the package filename match */
		ret = pk_plugin_match_running_file (backend, filenames[i]);
		if (!ret)
			continue;

		/* add as it matches the criteria */
		g_debug ("adding filename %s", filenames[i]);
		g_ptr_array_add (priv->files_list, g_strdup (filenames[i]));
	}
	g_strfreev (filenames);
}

/**
 * pk_transaction_plugin_run:
 */
void
pk_transaction_plugin_run (PkTransaction *transaction)
{
	const gchar *file;
	gboolean ret;
	gchar **files = NULL;
	gchar **package_ids;
	gchar *process = NULL;
	guint files_id = 0;
	guint finished_id = 0;
	PkBackend *backend = NULL;
	PkRoleEnum role;

	/* check the role */
	role = pk_transaction_priv_get_role (transaction);
	if (role != PK_ROLE_ENUM_UPDATE_PACKAGES)
		goto out;

	/* check we can do the action */
	backend = pk_transaction_priv_get_backend (transaction);
	if (!pk_backend_is_implemented (backend,
	    PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		goto out;
	}

	/* check we have entry */
	if (priv->no_update_process_list == NULL ||
	    priv->no_update_process_list[0] == NULL) {
		g_debug ("no processes to watch");
		goto out;
	}

	/* reset */
	g_ptr_array_set_size (priv->files_list, 0);

	/* set status */
	pk_backend_set_status (backend, PK_STATUS_ENUM_SCAN_PROCESS_LIST);
	pk_backend_set_percentage (backend, 101);

	/* get list from proc */
	ret = pk_proc_refresh (priv->proc);
	if (!ret) {
		g_warning ("failed to refresh");
		/* non-fatal */
		goto out;
	}

	/* set status */
	pk_backend_set_status (backend, PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES);

	files_id = g_signal_connect (backend, "files",
				     G_CALLBACK (pk_plugin_files_cb), NULL);
	finished_id = g_signal_connect (backend, "finished",
					G_CALLBACK (pk_plugin_finished_cb), NULL);

	/* get all the files touched in the packages we just updated */
	package_ids = pk_transaction_priv_get_package_ids (transaction);
	pk_backend_reset (backend);
	pk_backend_get_files (backend, package_ids);

	/* wait for finished */
	g_main_loop_run (priv->loop);
	pk_backend_set_percentage (backend, 100);

	/* there is a file we can't COW */
	if (priv->files_list->len != 0) {
		file = g_ptr_array_index (priv->files_list, 0);
		pk_backend_error_code (backend,
				       PK_ERROR_ENUM_UPDATE_FAILED_DUE_TO_RUNNING_PROCESS,
				       "failed to run as %s is running", file);
		goto out;
	}
out:
	if (files_id > 0)
		g_signal_handler_disconnect (backend, files_id);
	if (finished_id > 0)
		g_signal_handler_disconnect (backend, finished_id);
	g_strfreev (files);
	g_free (process);
}
