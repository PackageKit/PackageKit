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
#include <string.h>
#include <stdlib.h>

#include <pk-plugin.h>

#include "pk-cache.h"
#include "pk-lsof.h"

/* for when parsing /etc/login.defs fails */
#define PK_TRANSACTION_EXTRA_UID_MIN_DEFALT	500

struct PkPluginPrivate {
	GMainLoop		*loop;
	GPtrArray		*list;
	GPtrArray		*pids;
	GPtrArray		*files_list;
	PkLsof			*lsof;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "checks for any shared libraries in use after a security update";
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
	plugin->priv->list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	plugin->priv->files_list = g_ptr_array_new_with_free_func (g_free);
	plugin->priv->lsof = pk_lsof_new ();
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	g_main_loop_unref (plugin->priv->loop);
	g_ptr_array_unref (plugin->priv->list);
	g_object_unref (plugin->priv->lsof);
	g_ptr_array_unref (plugin->priv->files_list);
	if (plugin->priv->pids != NULL)
		g_ptr_array_free (plugin->priv->pids, TRUE);
}

/**
 * pk_plugin_finished_cb:
 **/
static void
pk_plugin_finished_cb (PkBackendJob *job,
		       PkExitEnum exit_enum,
		       PkPlugin *plugin)
{
	if (!g_main_loop_is_running (plugin->priv->loop))
		return;
	g_main_loop_quit (plugin->priv->loop);
}

/**
 * pk_plugin_get_installed_package_for_file:
 **/
static PkPackage *
pk_plugin_get_installed_package_for_file (PkPlugin *plugin,
					  PkTransaction *transaction,
					  const gchar *filename)
{
	PkPackage *package = NULL;
	gchar **filenames;

	/* use PK to find the correct package */
	g_ptr_array_set_size (plugin->priv->list, 0);
	pk_backend_reset_job (plugin->backend, plugin->job);
	filenames = g_strsplit (filename, "|||", -1);
	pk_backend_search_files (plugin->backend,
				 plugin->job,
				 pk_bitfield_value (PK_FILTER_ENUM_INSTALLED),
				 filenames);

	/* wait for finished */
	g_main_loop_run (plugin->priv->loop);

	/* check that we only matched one package */
	if (plugin->priv->list->len != 1) {
		g_warning ("not correct size, %i", plugin->priv->list->len);
		goto out;
	}

	/* get the package */
	package = g_ptr_array_index (plugin->priv->list, 0);
	if (package == NULL) {
		g_warning ("cannot get package");
		goto out;
	}
out:
	g_strfreev (filenames);
	return package;
}

/**
 * pk_plugin_files_cb:
 **/
static void
pk_plugin_files_cb (PkBackendJob *job,
		    PkFiles *files,
		    PkPlugin *plugin)
{
	guint i;
	guint len;
	gchar **filenames = NULL;

	/* get data */
	g_object_get (files,
		      "files", &filenames,
		      NULL);

	/* check each file to see if it's a system shared library */
	len = g_strv_length (filenames);
	for (i=0; i<len; i++) {
		/* not a system library */
		if (strstr (filenames[i], "/lib") == NULL)
			continue;

		/* not a shared object */
		if (strstr (filenames[i], ".so") == NULL)
			continue;

		/* add as it matches the criteria */
		g_debug ("adding filename %s", filenames[i]);
		g_ptr_array_add (plugin->priv->files_list,
				 g_strdup (filenames[i]));
	}
	g_strfreev (filenames);
}

/**
 * pk_plugin_get_cmdline:
 **/
static gchar *
pk_plugin_get_cmdline (PkTransaction *transaction, guint pid)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		g_warning ("failed to get cmdline: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (filename);
	return cmdline;
}

/**
 * pk_plugin_get_uid:
 **/
static gint
pk_plugin_get_uid (PkTransaction *transaction, guint pid)
{
	gboolean ret;
	gchar *endptr = NULL;
	gchar *filename = NULL;
	gchar *text = NULL;
	guint64 value_raw;
	gint uid = -1;

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/loginuid", pid);
	ret = g_file_get_contents (filename, &text, NULL, NULL);
	if (!ret)
		goto out;

	/* parse */
	value_raw = g_ascii_strtoull (text, &endptr, 10);

	/* parsing error */
	if (endptr == text)
		goto out;

	/* out of range */
	if (value_raw > G_MAXUINT)
		goto out;

	/* cast back down to value */
	uid = (gint) value_raw;
out:
	g_free (filename);
	g_free (text);
	return uid;
}

static guint
pk_plugin_get_uid_min (void)
{
	gboolean ret;
	guint i;
	gchar *data = NULL;
	gchar **split = NULL;
	GError *error = NULL;
	guint uid_min = G_MAXUINT;

	/* get contents */
	ret = g_file_get_contents ("/etc/login.defs", &data, NULL, &error);
	if (!ret) {
		g_warning ("failed to get login UID_MIN: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}
	split = g_strsplit (data, "\n", -1);
	for (i = 0; split[i] != NULL; i++) {
		if (!g_str_has_prefix (split[i], "UID_MIN"))
			continue;
		uid_min = atoi (g_strchug (split[i]+7));
		break;
	}
out:
	g_free (data);
	g_strfreev (split);
	return uid_min;
}

/**
 * pk_plugin_transaction_run:
 *
 * This function does the following things:
 *  1) Refreshes the list of open files
 *  2) Gets the list of library files from the security updates
 *  3) Gets a list of pids that are using the libraries
 *  4) The list of pids are converted to a list of files
 *  5) The list of files is converted to a list of packages
 *  6) For each package, emit a RequireRestart of the correct type (according to the UID)
 */
void
pk_plugin_transaction_run (PkPlugin *plugin,
			   PkTransaction *transaction)
{
	gboolean ret;
	gchar **files = NULL;
	gchar *package_id;
	gchar **package_ids;
	gchar **package_ids_security = NULL;
	GPtrArray *updates = NULL;
	guint i;
	guint j = 0;
	guint length = 0;
	PkConf *conf;
	PkInfoEnum info;
	PkPackage *item;
	PkResults *results;
	PkRoleEnum role;
	PkCache *cache = NULL;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "CheckSharedLibrariesInUse");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_UPDATE_SYSTEM &&
	    role != PK_ROLE_ENUM_UPDATE_PACKAGES &&
	    role != PK_ROLE_ENUM_INSTALL_PACKAGES)
		goto out;

	/* check we can do the action */
	if (!pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		goto out;
	}
	pk_backend_job_set_vfunc (plugin->job,
			      PK_BACKEND_SIGNAL_FILES,
			      (PkBackendJobVFunc) pk_plugin_files_cb,
			      plugin);
	pk_backend_job_set_vfunc (plugin->job,
			      PK_BACKEND_SIGNAL_FINISHED,
			      (PkBackendJobVFunc) pk_plugin_finished_cb,
			      plugin);

	/* do we have a cache */
	cache = pk_cache_new ();
	results = pk_cache_get_results (cache, PK_ROLE_ENUM_GET_UPDATES);
	if (results == NULL) {
		g_warning ("no updates cache");
		goto out;
	}

	/* find security update packages */
	updates = pk_results_get_package_array (results);
	for (i=0; i<updates->len; i++) {
		item = g_ptr_array_index (updates, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      NULL);
		if (info == PK_INFO_ENUM_SECURITY) {
			g_debug ("security update: %s", package_id);
			length++;
		}
		g_free (package_id);
	}

	/* nothing to scan for */
	if (length == 0) {
		g_debug ("no security updates");
		goto out;
	}

	/* create list of security packages */
	package_ids_security = g_new0 (gchar *, length+1);
	for (i=0; i<updates->len; i++) {
		item = g_ptr_array_index (updates, i);
		g_object_get (item,
			      "info", &info,
			      "package-id", &package_id,
			      NULL);
		if (info == PK_INFO_ENUM_SECURITY)
			package_ids_security[j++] = g_strdup (package_id);
		g_free (package_id);
	}

	/* is a security update we are installing */
	package_ids = pk_transaction_get_package_ids (transaction);
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		ret = FALSE;

		/* do any of the packages we are updating match */
		for (i=0; package_ids_security[i] != NULL; i++) {
			for (j=0; package_ids[j] != NULL; j++) {
				if (g_strcmp0 (package_ids_security[i],
					       package_ids[j]) == 0) {
					ret = TRUE;
					break;
				}
			}
		}

		/* nothing matched */
		if (!ret) {
			g_debug ("not installing a security update package");
			goto out;
		}
	}

	/* reset */
	g_ptr_array_set_size (plugin->priv->files_list, 0);

	if (plugin->priv->pids != NULL) {
		g_ptr_array_free (plugin->priv->pids, TRUE);
		plugin->priv->pids = NULL;
	}

	/* set status */
	pk_backend_job_set_status (plugin->job, PK_STATUS_ENUM_SCAN_PROCESS_LIST);
	pk_backend_job_set_percentage (plugin->job, 101);

	/* get list from lsof */
	ret = pk_lsof_refresh (plugin->priv->lsof);
	if (!ret) {
		g_warning ("failed to refresh");
		goto out;
	}

	/* get all the files touched in the packages we just updated */
	pk_backend_reset_job (plugin->backend, plugin->job);
	pk_backend_job_set_status (plugin->job,
				   PK_STATUS_ENUM_CHECK_LIBRARIES);
	pk_backend_get_files (plugin->backend,
			      plugin->job,
			      package_ids_security);

	/* wait for finished */
	g_main_loop_run (plugin->priv->loop);

	/* nothing to do */
	if (plugin->priv->files_list->len == 0) {
		g_debug ("no files");
		goto out;
	}

	/* get the list of PIDs */
	files = pk_ptr_array_to_strv (plugin->priv->files_list);
	plugin->priv->pids = pk_lsof_get_pids_for_filenames (plugin->priv->lsof, files);

	/* nothing depends on these libraries */
	if (plugin->priv->pids == NULL) {
		g_warning ("failed to get process list");
		goto out;
	}

	/* nothing depends on these libraries */
	if (plugin->priv->pids->len == 0) {
		g_debug ("no processes depend on these libraries");
		goto out;
	}

	/* don't emit until we've run the transaction and it's success */
	pk_backend_job_set_percentage (plugin->job, 100);
out:
	g_strfreev (files);
	if (updates != NULL)
		g_ptr_array_unref (updates);
	if (cache != NULL)
		g_object_unref (cache);
	g_strfreev (package_ids_security);
}

/**
 * pk_plugin_transaction_finished_results:
 */
void
pk_plugin_transaction_finished_results (PkPlugin *plugin,
					PkTransaction *transaction)
{
	gboolean ret;
	PkConf *conf;
	PkRoleEnum role;
	gint uid;
	guint i;
	guint pid;
	gchar *filename;
	gchar *cmdline;
	gchar *cmdline_full;
	GPtrArray *files_session = NULL;
	GPtrArray *files_system = NULL;
	PkPackage *package;
	GPtrArray *pids;
	guint uid_min;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "CheckSharedLibrariesInUse");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_GET_UPDATES)
		goto out;

	/* check we can do the action */
	if (!pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_PACKAGES)) {
		g_debug ("cannot get packages");
		goto out;
	}

	/* create arrays */
	files_session = g_ptr_array_new_with_free_func (g_free);
	files_system = g_ptr_array_new_with_free_func (g_free);

	/* get local array */
	pids = plugin->priv->pids;
	if (pids == NULL)
		goto out;

	/* set status */
	pk_backend_job_set_status (plugin->job,
			       PK_STATUS_ENUM_CHECK_LIBRARIES);

	/* get user UID range */
	uid_min = pk_plugin_get_uid_min ();
	if (uid_min == G_MAXUINT)
		uid_min = PK_TRANSACTION_EXTRA_UID_MIN_DEFALT;

	/* find the package name of each pid */
	for (i=0; i<pids->len; i++) {
		pid = GPOINTER_TO_INT (g_ptr_array_index (pids, i));

		/* get user */
		uid = pk_plugin_get_uid (transaction, pid);
		if (uid < 0)
			continue;

		/* get command line */
		cmdline = pk_plugin_get_cmdline (transaction, pid);
		if (cmdline == NULL)
			continue;

		/* prepend path if it does not already exist */
		if (cmdline[0] == '/')
			cmdline_full = g_strdup (cmdline);
		else
			cmdline_full = g_strdup_printf ("/usr/bin/%s", cmdline);

		g_debug ("pid=%i: %s (%i)", pid, cmdline_full, uid);
		if (uid < (gint)uid_min)
			g_ptr_array_add (files_system, cmdline_full);
		else
			g_ptr_array_add (files_session, cmdline_full);
		g_free (cmdline);
	}

	/* we found nothing */
	if (files_system->len == 0 && files_session->len == 0) {
		g_warning ("no pids could be resolved");
		goto out;
	}

	/* process all session restarts */
	for (i=0; i<files_session->len; i++) {
		filename = g_ptr_array_index (files_session, i);

		package = pk_plugin_get_installed_package_for_file (plugin,
								    transaction,
								    filename);
		if (package == NULL) {
			g_debug ("failed to find package for %s", filename);
			continue;
		}
		pk_backend_job_require_restart (plugin->job,
					        PK_RESTART_ENUM_SECURITY_SESSION,
					        pk_package_get_id (package));
	}

	/* process all system restarts */
	for (i=0; i<files_system->len; i++) {
		filename = g_ptr_array_index (files_system, i);

		package = pk_plugin_get_installed_package_for_file (plugin,
								    transaction,
								    filename);
		if (package == NULL) {
			g_debug ("failed to find package for %s", filename);
			continue;
		}
		pk_backend_job_require_restart (plugin->job,
					        PK_RESTART_ENUM_SECURITY_SYSTEM,
					        pk_package_get_id (package));
	}
out:
	if (files_session != NULL)
		g_ptr_array_free (files_session, TRUE);
	if (files_system != NULL)
		g_ptr_array_free (files_system, TRUE);
}
