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

#include "pk-proc.h"

struct PkPluginPrivate {
	PkProc			*proc;
	GMainLoop		*loop;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "Checks for running processes during update for session restarts";
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
	plugin->priv->proc = pk_proc_new ();
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	g_main_loop_unref (plugin->priv->loop);
	g_object_unref (plugin->priv->proc);
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
	gchar *package_id = NULL;

	/* get data */
	g_object_get (files,
		      "package-id", &package_id,
		      "files", &filenames,
		      NULL);

	/* check each file */
	len = g_strv_length (filenames);
	for (i=0; i<len; i++) {
		/* executable? */
		ret = g_file_test (filenames[i],
				   G_FILE_TEST_IS_REGULAR |
				   G_FILE_TEST_IS_EXECUTABLE |
				   G_FILE_TEST_EXISTS);
		if (!ret)
			continue;

		/* running? */
		ret = pk_proc_find_exec (plugin->priv->proc, filenames[i]);
		if (!ret)
			continue;

		/* TODO: findout if the executable has a desktop file, and if so,
		 * suggest an application restart instead */

		/* send signal about session restart */
		g_debug ("package %s updated, and %s is running",
			 package_id, filenames[i]);
		pk_backend_require_restart (backend,
					    PK_RESTART_ENUM_SESSION,
					    package_id);
	}
	g_strfreev (filenames);
	g_free (package_id);
}

/**
 * pk_plugin_transaction_finished_results:
 */
void
pk_plugin_transaction_finished_results (PkPlugin *plugin,
					PkTransaction *transaction)
{
	gboolean ret;
	gchar **package_ids = NULL;
	gchar *package_id_tmp;
	GPtrArray *array = NULL;
	GPtrArray *list = NULL;
	guint finished_id = 0;
	guint i;
	PkConf *conf;
	PkPackage *item;
	PkResults *results;
	PkRoleEnum role;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "UpdateCheckProcesses");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_UPDATE_SYSTEM &&
	    role != PK_ROLE_ENUM_UPDATE_PACKAGES)
		goto out;

	/* check we can do the action */
	if (!pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		goto out;
	}
	finished_id = g_signal_connect (plugin->backend, "finished",
					G_CALLBACK (pk_plugin_finished_cb), plugin);
	pk_backend_set_vfunc (plugin->backend, PK_BACKEND_SIGNAL_FILES, (PkBackendVFunc) pk_plugin_files_cb, plugin);

	/* get results */
	results = pk_transaction_get_results (transaction);
	array = pk_results_get_package_array (results);

	/* filter on UPDATING */
	list = g_ptr_array_new_with_free_func (g_free);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		if (pk_package_get_info (item) != PK_INFO_ENUM_UPDATING)
			continue;
		/* we convert the package_id data to be 'installed' as this means
		 * we can use the local package database for GetFiles rather than
		 * downloading new remote metadata */
		package_id_tmp = pk_package_id_build (pk_package_get_name (item),
						      pk_package_get_version (item),
						      pk_package_get_arch (item),
						      "installed");
		g_ptr_array_add (list, package_id_tmp);
	}

	/* process file lists on these packages */
	if (list->len == 0)
		goto out;

	/* get all the running processes */
	pk_proc_refresh (plugin->priv->proc);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (plugin->backend);
	pk_backend_set_status (plugin->backend, PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES);
	pk_backend_set_percentage (plugin->backend, 101);
	package_ids = pk_ptr_array_to_strv (list);
	pk_backend_get_files (plugin->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (plugin->priv->loop);

	pk_backend_set_percentage (plugin->backend, 100);

out:
	g_strfreev (package_ids);
	if (finished_id > 0)
		g_signal_handler_disconnect (plugin->backend, finished_id);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (list != NULL)
		g_ptr_array_unref (list);
}
