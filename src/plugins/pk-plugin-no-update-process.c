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
#include <pk-plugin.h>

#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-files.h>

#include "pk-proc.h"

struct PkPluginPrivate {
	GMainLoop		*loop;
	GPtrArray		*files_list;
	gchar			**no_update;
	PkProc			*proc;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "Updates the package lists after refresh";
}

/**
 * pk_plugin_finished_cb:
 **/
static void
pk_plugin_finished_cb (PkBackend *backend,
		       PkExitEnum exit_enum,
		       PkPlugin *plugin)
{
	if (!g_main_loop_is_running (plugin->priv->loop))
		return;
	g_main_loop_quit (plugin->priv->loop);
}

/**
 * pk_plugin_initialize:
 */
void
pk_plugin_initialize (PkPlugin *plugin)
{
	/* create private area */
	plugin->priv = PK_TRANSACTION_PLUGIN_GET_PRIVATE (PkPluginPrivate);
	plugin->priv->loop = g_main_loop_new (NULL, FALSE);
	plugin->priv->files_list = g_ptr_array_new_with_free_func (g_free);
	plugin->priv->proc = pk_proc_new ();
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	g_main_loop_unref (plugin->priv->loop);
	g_strfreev (plugin->priv->no_update);
	g_ptr_array_unref (plugin->priv->files_list);
	g_object_unref (plugin->priv->proc);
}

/**
 * pk_plugin_match_running_file:
 *
 * Only if the pattern matches the old and new names we refuse to run
 **/
static gboolean
pk_plugin_match_running_file (PkPlugin *plugin, const gchar *filename)
{
	guint i;
	gchar **list;
	gboolean ret;

	/* compare each pattern */
	list = plugin->priv->no_update;
	for (i=0; list[i] != NULL; i++) {

		/* does the package filename match */
		ret = g_pattern_match_simple (list[i], filename);
		if (ret) {
			/* is there a running process that also matches */
			ret = pk_proc_find_exec (plugin->priv->proc, list[i]);
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
		    PkPlugin *plugin)
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
		ret = pk_plugin_match_running_file (plugin, filenames[i]);
		if (!ret)
			continue;

		/* add as it matches the criteria */
		g_debug ("adding filename %s", filenames[i]);
		g_ptr_array_add (plugin->priv->files_list, g_strdup (filenames[i]));
	}
	g_strfreev (filenames);
}

/**
 * pk_plugin_transaction_run:
 */
void
pk_plugin_transaction_run (PkPlugin *plugin,
			   PkTransaction *transaction)
{
	const gchar *file;
	gboolean ret;
	gchar **files = NULL;
	gchar **package_ids;
	gchar *process = NULL;
	guint files_id = 0;
	guint finished_id = 0;
	PkConf *conf;
	PkRoleEnum role;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_UPDATE_PACKAGES)
		goto out;

	/* check we can do the action */
	if (!pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		goto out;
	}

	/* get the list of processes we should neverupdate when running */
	conf = pk_transaction_get_conf (transaction);
	if (plugin->priv->no_update == NULL) {
		plugin->priv->no_update = pk_conf_get_strv (conf, "NoUpdateProcessList");
	}

	/* check we have entry */
	if (plugin->priv->no_update == NULL ||
	    plugin->priv->no_update[0] == NULL) {
		g_debug ("no processes to watch");
		goto out;
	}

	/* reset */
	g_ptr_array_set_size (plugin->priv->files_list, 0);

	/* set status */
	pk_backend_set_status (plugin->backend,
			       PK_STATUS_ENUM_SCAN_PROCESS_LIST);
	pk_backend_set_percentage (plugin->backend, 101);

	/* get list from proc */
	ret = pk_proc_refresh (plugin->priv->proc);
	if (!ret) {
		g_warning ("failed to refresh");
		/* non-fatal */
		goto out;
	}

	/* set status */
	pk_backend_set_status (plugin->backend,
			       PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES);

	files_id = g_signal_connect (plugin->backend, "files",
				     G_CALLBACK (pk_plugin_files_cb), plugin);
	finished_id = g_signal_connect (plugin->backend, "finished",
					G_CALLBACK (pk_plugin_finished_cb), plugin);

	/* get all the files touched in the packages we just updated */
	package_ids = pk_transaction_get_package_ids (transaction);
	pk_backend_reset (plugin->backend);
	pk_backend_get_files (plugin->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (plugin->priv->loop);
	pk_backend_set_percentage (plugin->backend, 100);

	/* there is a file we can't COW */
	if (plugin->priv->files_list->len != 0) {
		file = g_ptr_array_index (plugin->priv->files_list, 0);
		pk_backend_error_code (plugin->backend,
				       PK_ERROR_ENUM_UPDATE_FAILED_DUE_TO_RUNNING_PROCESS,
				       "failed to run as %s is running", file);
		goto out;
	}
out:
	if (files_id > 0)
		g_signal_handler_disconnect (plugin->backend, files_id);
	if (finished_id > 0)
		g_signal_handler_disconnect (plugin->backend, finished_id);
	g_strfreev (files);
	g_free (process);
}
