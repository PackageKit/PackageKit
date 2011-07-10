/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-desktop.h>
#include <packagekit-glib2/pk-common.h>
#include <gio/gdesktopappinfo.h>
#include <sqlite3.h>

#include "pk-transaction-extra.h"
#include "pk-shared.h"
#include "pk-marshal.h"
#include "pk-backend.h"
#include "pk-lsof.h"
#include "pk-proc.h"
#include "pk-conf.h"

/* for when parsing /etc/login.defs fails */
#define PK_TRANSACTION_EXTRA_UID_MIN_DEFALT	500

#define PK_POST_TRANS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_POST_TRANS, PkTransactionExtraPrivate))

struct PkTransactionExtraPrivate
{
	PkBackend		*backend;
	GMainLoop		*loop;
	GPtrArray		*list;
	PkLsof			*lsof;
	PkProc			*proc;
	PkConf			*conf;
	guint			 finished_id;
	guint			 package_id;
	GPtrArray		*files_list;
	GPtrArray		*pids;
};

enum {
	PK_POST_TRANS_STATUS_CHANGED,
	PK_POST_TRANS_PROGRESS_CHANGED,
	PK_POST_TRANS_LAST_SIGNAL
};

static guint signals [PK_POST_TRANS_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (PkTransactionExtra, pk_transaction_extra, G_TYPE_OBJECT)

/**
 * pk_transaction_extra_finished_cb:
 **/
static void
pk_transaction_extra_finished_cb (PkBackend *backend, PkExitEnum exit_enum, PkTransactionExtra *extra)
{
	if (g_main_loop_is_running (extra->priv->loop)) {
		if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
			g_warning ("%s failed with exit code: %s",
				     pk_role_enum_to_string (pk_backend_get_role (backend)),
				     pk_exit_enum_to_string (exit_enum));
		}
		g_main_loop_quit (extra->priv->loop);
	}
}

/**
 * pk_transaction_extra_package_cb:
 **/
static void
pk_transaction_extra_package_cb (PkBackend *backend, PkPackage *package, PkTransactionExtra *extra)
{
	g_ptr_array_add (extra->priv->list, g_object_ref (package));
}

/**
 * pk_transaction_extra_set_status_changed:
 **/
static void
pk_transaction_extra_set_status_changed (PkTransactionExtra *extra, PkStatusEnum status)
{
	g_debug ("emiting status-changed %s", pk_status_enum_to_string (status));
	g_signal_emit (extra, signals [PK_POST_TRANS_STATUS_CHANGED], 0, status);
}

/**
 * pk_transaction_extra_set_progress_changed:
 **/
static void
pk_transaction_extra_set_progress_changed (PkTransactionExtra *extra, guint percentage)
{
	g_debug ("emiting progress-changed %i", percentage);
	g_signal_emit (extra, signals [PK_POST_TRANS_PROGRESS_CHANGED], 0, percentage, 0, 0, 0);
}

/**
 * pk_transaction_extra_get_installed_package_for_file:
 **/
static PkPackage *
pk_transaction_extra_get_installed_package_for_file (PkTransactionExtra *extra, const gchar *filename)
{
	PkPackage *package = NULL;
	gchar **filenames;

	/* use PK to find the correct package */
	if (extra->priv->list->len > 0)
		g_ptr_array_remove_range (extra->priv->list, 0, extra->priv->list->len);
	pk_backend_reset (extra->priv->backend);
	filenames = g_strsplit (filename, "|||", -1);
	pk_backend_search_files (extra->priv->backend, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), filenames);
	g_strfreev (filenames);

	/* wait for finished */
	g_main_loop_run (extra->priv->loop);

	/* check that we only matched one package */
	if (extra->priv->list->len != 1) {
		g_warning ("not correct size, %i", extra->priv->list->len);
		goto out;
	}

	/* get the package */
	package = g_ptr_array_index (extra->priv->list, 0);
	if (package == NULL) {
		g_warning ("cannot get package");
		goto out;
	}
out:
	return package;
}

/**
 * pk_transaction_extra_update_files_check_running_cb:
 **/
static void
pk_transaction_extra_update_files_check_running_cb (PkBackend *backend, PkFiles *files, PkTransactionExtra *extra)
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
		ret = g_file_test (filenames[i], G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_EXECUTABLE | G_FILE_TEST_EXISTS);
		if (!ret)
			continue;

		/* running? */
		ret = pk_proc_find_exec (extra->priv->proc, filenames[i]);
		if (!ret)
			continue;

		/* TODO: findout if the executable has a desktop file, and if so,
		 * suggest an application restart instead */

		/* send signal about session restart */
		g_debug ("package %s updated, and %s is running", package_id, filenames[i]);
		pk_backend_require_restart (extra->priv->backend, PK_RESTART_ENUM_SESSION, package_id);
	}
	g_strfreev (filenames);
	g_free (package_id);
}

/**
 * pk_transaction_extra_check_running_process:
 **/
gboolean
pk_transaction_extra_check_running_process (PkTransactionExtra *extra, gchar **package_ids)
{
	guint signal_files = 0;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	if (!pk_backend_is_implemented (extra->priv->backend, PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		return FALSE;
	}

	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES);
	pk_transaction_extra_set_progress_changed (extra, 101);

	pk_proc_refresh (extra->priv->proc);

	signal_files = g_signal_connect (extra->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_update_files_check_running_cb), extra);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (extra->priv->backend);
	pk_backend_get_files (extra->priv->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (extra->priv->loop);

	g_signal_handler_disconnect (extra->priv->backend, signal_files);
	pk_transaction_extra_set_progress_changed (extra, 100);
	return TRUE;
}

/**
 * pk_transaction_extra_files_check_library_restart_cb:
 **/
static void
pk_transaction_extra_files_check_library_restart_cb (PkBackend *backend, PkFiles *files, PkTransactionExtra *extra)
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
		g_ptr_array_add (extra->priv->files_list, g_strdup (filenames[i]));
	}
	g_strfreev (filenames);
}

/**
 * pk_transaction_extra_get_cmdline:
 **/
static gchar *
pk_transaction_extra_get_cmdline (PkTransactionExtra *extra, guint pid)
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
 * pk_transaction_extra_get_uid:
 **/
static gint
pk_transaction_extra_get_uid (PkTransactionExtra *extra, guint pid)
{
	gboolean ret;
	gint uid = -1;
	gchar *filename = NULL;
	gchar *uid_text = NULL;

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/loginuid", pid);
	ret = g_file_get_contents (filename, &uid_text, NULL, NULL);
	if (!ret)
		goto out;

	/* convert from text */
	ret = egg_strtoint (uid_text, &uid);
	if (!ret)
		goto out;
out:
	g_free (filename);
	g_free (uid_text);
	return uid;
}

static guint
pk_transaction_extra_get_uid_min (void)
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
 * pk_transaction_extra_check_library_restart:
 **/
gboolean
pk_transaction_extra_check_library_restart (PkTransactionExtra *extra)
{
	gint uid;
	guint i;
	guint pid;
	gchar *filename;
	gchar *cmdline;
	gchar *cmdline_full;
	GPtrArray *files_session;
	GPtrArray *files_system;
	PkPackage *package;
	GPtrArray *pids;
	guint uid_min;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	/* create arrays */
	files_session = g_ptr_array_new ();
	files_system = g_ptr_array_new ();

	/* get local array */
	pids = extra->priv->pids;
	if (pids == NULL)
		goto out;

	/* set status */
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_CHECK_LIBRARIES);

	/* get user UID range */
	uid_min = pk_transaction_extra_get_uid_min ();
	if (uid_min == G_MAXUINT)
		uid_min = PK_TRANSACTION_EXTRA_UID_MIN_DEFALT;

	/* find the package name of each pid */
	for (i=0; i<pids->len; i++) {
		pid = GPOINTER_TO_INT (g_ptr_array_index (pids, i));

		/* get user */
		uid = pk_transaction_extra_get_uid (extra, pid);
		if (uid < 0)
			continue;

		/* get command line */
		cmdline = pk_transaction_extra_get_cmdline (extra, pid);
		if (cmdline == NULL)
			continue;

		/* prepend path if it does not already exist */
		if (cmdline[0] == '/')
			cmdline_full = g_strdup (cmdline);
		else
			cmdline_full = g_strdup_printf ("/usr/bin/%s", cmdline);

		g_debug ("pid=%i: %s (%i)", pid, cmdline_full, uid);
		if (uid < 500)
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

		package = pk_transaction_extra_get_installed_package_for_file (extra, filename);
		if (package == NULL) {
			g_debug ("failed to find package for %s", filename);
			continue;
		}
		pk_backend_require_restart (extra->priv->backend, PK_RESTART_ENUM_SECURITY_SESSION, pk_package_get_id (package));
	}

	/* process all system restarts */
	for (i=0; i<files_system->len; i++) {
		filename = g_ptr_array_index (files_system, i);

		package = pk_transaction_extra_get_installed_package_for_file (extra, filename);
		if (package == NULL) {
			g_debug ("failed to find package for %s", filename);
			continue;
		}
		pk_backend_require_restart (extra->priv->backend, PK_RESTART_ENUM_SECURITY_SYSTEM, pk_package_get_id (package));
	}

out:
	g_ptr_array_foreach (files_session, (GFunc) g_free, NULL);
	g_ptr_array_foreach (files_system, (GFunc) g_free, NULL);
	g_ptr_array_free (files_session, TRUE);
	g_ptr_array_free (files_system, TRUE);
	return TRUE;
}

/**
 * pk_transaction_extra_check_library_restart_pre:
 * @package_ids: the list of security updates
 *
 * This function does the following things:
 *  1) Refreshes the list of open files
 *  2) Gets the list of library files from the security updates
 *  3) Gets a list of pids that are using the libraries
 *  4) The list of pids are converted to a list of files
 *  5) The list of files is converted to a list of packages
 *  6) For each package, emit a RequireRestart of the correct type (according to the UID)
 *
 * Return value: success, so %TRUE means the library check completed okay
 **/
gboolean
pk_transaction_extra_check_library_restart_pre (PkTransactionExtra *extra, gchar **package_ids)
{
	guint signal_files = 0;
	gboolean ret = TRUE;
	gchar **files = NULL;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	if (!pk_backend_is_implemented (extra->priv->backend, PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		return FALSE;
	}

	/* reset */
	g_ptr_array_set_size (extra->priv->files_list, 0);

	if (extra->priv->pids != NULL) {
		g_ptr_array_free (extra->priv->pids, TRUE);
		extra->priv->pids = NULL;
	}

	/* set status */
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_SCAN_PROCESS_LIST);
	pk_transaction_extra_set_progress_changed (extra, 101);

	/* get list from lsof */
	ret = pk_lsof_refresh (extra->priv->lsof);
	if (!ret) {
		g_warning ("failed to refresh");
		goto out;
	}

	/* set status */
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_CHECK_LIBRARIES);

	signal_files = g_signal_connect (extra->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_files_check_library_restart_cb), extra);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (extra->priv->backend);
	pk_backend_get_files (extra->priv->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (extra->priv->loop);

	/* nothing to do */
	if (extra->priv->files_list->len == 0) {
		g_debug ("no files");
		goto out;
	}

	/* get the list of PIDs */
	files = pk_ptr_array_to_strv (extra->priv->files_list);
	extra->priv->pids = pk_lsof_get_pids_for_filenames (extra->priv->lsof, files);

	/* nothing depends on these libraries */
	if (extra->priv->pids == NULL) {
		g_warning ("failed to get process list");
		goto out;
	}

	/* nothing depends on these libraries */
	if (extra->priv->pids->len == 0) {
		g_debug ("no processes depend on these libraries");
		goto out;
	}

	/* don't emit until we've run the transaction and it's success */
out:
	pk_transaction_extra_set_progress_changed (extra, 100);
	if (signal_files > 0)
		g_signal_handler_disconnect (extra->priv->backend, signal_files);
	g_strfreev (files);
	return ret;
}

/**
 * pk_transaction_extra_finalize:
 **/
static void
pk_transaction_extra_finalize (GObject *object)
{
	PkTransactionExtra *extra;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_POST_TRANS (object));
	extra = PK_POST_TRANS (object);

	g_signal_handler_disconnect (extra->priv->backend, extra->priv->finished_id);
	g_signal_handler_disconnect (extra->priv->backend, extra->priv->package_id);

	if (extra->priv->pids != NULL)
		g_ptr_array_free (extra->priv->pids, TRUE);
	if (g_main_loop_is_running (extra->priv->loop))
		g_main_loop_quit (extra->priv->loop);
	g_main_loop_unref (extra->priv->loop);
	g_ptr_array_unref (extra->priv->files_list);

	g_object_unref (extra->priv->backend);
	g_object_unref (extra->priv->lsof);
	g_object_unref (extra->priv->proc);
	g_object_unref (extra->priv->conf);
	g_ptr_array_unref (extra->priv->list);

	G_OBJECT_CLASS (pk_transaction_extra_parent_class)->finalize (object);
}

/**
 * pk_transaction_extra_class_init:
 **/
static void
pk_transaction_extra_class_init (PkTransactionExtraClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_transaction_extra_finalize;
	signals [PK_POST_TRANS_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_POST_TRANS_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkTransactionExtraPrivate));
}

/**
 * pk_transaction_extra_init:
 *
 * initializes the extra_trans class. NOTE: We expect extra_trans objects
 * to *NOT* be removed or added during the session.
 * We only control the first extra_trans object if there are more than one.
 **/
static void
pk_transaction_extra_init (PkTransactionExtra *extra)
{
	extra->priv = PK_POST_TRANS_GET_PRIVATE (extra);
	extra->priv->loop = g_main_loop_new (NULL, FALSE);
	extra->priv->list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	extra->priv->backend = pk_backend_new ();
	extra->priv->lsof = pk_lsof_new ();
	extra->priv->proc = pk_proc_new ();
	extra->priv->pids = NULL;
	extra->priv->files_list = g_ptr_array_new_with_free_func (g_free);
	extra->priv->conf = pk_conf_new ();

	extra->priv->finished_id =
		g_signal_connect (extra->priv->backend, "finished",
				  G_CALLBACK (pk_transaction_extra_finished_cb), extra);
	extra->priv->package_id =
		g_signal_connect (extra->priv->backend, "package",
				  G_CALLBACK (pk_transaction_extra_package_cb), extra);
}

/**
 * pk_transaction_extra_new:
 * Return value: A new extra_trans class instance.
 **/
PkTransactionExtra *
pk_transaction_extra_new (void)
{
	PkTransactionExtra *extra;
	extra = g_object_new (PK_TYPE_POST_TRANS, NULL);
	return PK_POST_TRANS (extra);
}

