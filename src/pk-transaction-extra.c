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

#include "egg-debug.h"

#include "pk-transaction-extra.h"
#include "pk-shared.h"
#include "pk-marshal.h"
#include "pk-backend.h"
#include "pk-lsof.h"
#include "pk-proc.h"
#include "pk-conf.h"

#define PK_POST_TRANS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_POST_TRANS, PkTransactionExtraPrivate))

struct PkTransactionExtraPrivate
{
	sqlite3			*db;
	PkBackend		*backend;
	GMainLoop		*loop;
	GPtrArray		*list;
	PkLsof			*lsof;
	PkProc			*proc;
	PkConf			*conf;
	guint			 finished_id;
	guint			 package_id;
	gchar			**no_update_process_list;
	GHashTable		*hash;
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
			egg_warning ("%s failed with exit code: %s",
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
	egg_debug ("emiting status-changed %s", pk_status_enum_to_string (status));
	g_signal_emit (extra, signals [PK_POST_TRANS_STATUS_CHANGED], 0, status);
}

/**
 * pk_transaction_extra_set_progress_changed:
 **/
static void
pk_transaction_extra_set_progress_changed (PkTransactionExtra *extra, guint percentage)
{
	egg_debug ("emiting progress-changed %i", percentage);
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
		egg_warning ("not correct size, %i", extra->priv->list->len);
		goto out;
	}

	/* get the package */
	package = g_ptr_array_index (extra->priv->list, 0);
	if (package == NULL) {
		egg_warning ("cannot get package");
		goto out;
	}
out:
	return package;
}

/**
 * pk_transaction_extra_get_filename_md5:
 **/
static gchar *
pk_transaction_extra_get_filename_md5 (const gchar *filename)
{
	gchar *md5 = NULL;
	gchar *data = NULL;
	gsize length;
	GError *error = NULL;
	gboolean ret;

	/* check is no longer exists */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret)
		goto out;

	/* get data */
	ret = g_file_get_contents (filename, &data, &length, &error);
	if (!ret) {
		egg_warning ("failed to open file %s: %s", filename, error->message);
		g_error_free (error);
		goto out;
	}

	/* check md5 is same */
	md5 = g_compute_checksum_for_data (G_CHECKSUM_MD5, (const guchar *) data, length);
out:
	g_free (data);
	return md5;
}

/**
 * pk_transaction_extra_sqlite_remove_filename:
 **/
static gint
pk_transaction_extra_sqlite_remove_filename (PkTransactionExtra *extra, const gchar *filename)
{
	gchar *statement;
	gint rc;

	statement = g_strdup_printf ("DELETE FROM cache WHERE filename = '%s'", filename);
	rc = sqlite3_exec (extra->priv->db, statement, NULL, NULL, NULL);
	g_free (statement);
	return rc;
}

/**
 * pk_transaction_extra_sqlite_add_filename_details:
 **/
static gint
pk_transaction_extra_sqlite_add_filename_details (PkTransactionExtra *extra, const gchar *filename, const gchar *package, const gchar *md5)
{
	gchar *statement;
	gchar *error_msg = NULL;
	sqlite3_stmt *sql_statement = NULL;
	gint rc = -1;
	gint show;
	GDesktopAppInfo *info;

	/* find out if we should show desktop file in menus */
	info = g_desktop_app_info_new_from_filename (filename);
	if (info == NULL) {
		egg_warning ("could not load desktop file %s", filename);
		goto out;
	}
	show = g_app_info_should_show (G_APP_INFO (info));
	g_object_unref (info);

	egg_debug ("add filename %s from %s with md5: %s (show: %i)", filename, package, md5, show);

	/* the row might already exist */
	statement = g_strdup_printf ("DELETE FROM cache WHERE filename = '%s'", filename);
	sqlite3_exec (extra->priv->db, statement, NULL, NULL, NULL);
	g_free (statement);

	/* prepare the query, as we don't escape it */
	rc = sqlite3_prepare_v2 (extra->priv->db, "INSERT INTO cache (filename, package, show, md5) VALUES (?, ?, ?, ?)", -1, &sql_statement, NULL);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL failed to prepare: %s", sqlite3_errmsg (extra->priv->db));
		goto out;
	}

	/* add data */
	sqlite3_bind_text (sql_statement, 1, filename, -1, SQLITE_STATIC);
	sqlite3_bind_text (sql_statement, 2, package, -1, SQLITE_STATIC);
	sqlite3_bind_int (sql_statement, 3, show);
	sqlite3_bind_text (sql_statement, 4, md5, -1, SQLITE_STATIC);

	/* save this */
	sqlite3_step (sql_statement);
	rc = sqlite3_finalize (sql_statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

out:
	return rc;
}

/**
 * pk_transaction_extra_sqlite_add_filename:
 **/
static gint
pk_transaction_extra_sqlite_add_filename (PkTransactionExtra *extra, const gchar *filename, const gchar *md5_opt)
{
	gchar *md5 = NULL;
	gint rc = -1;
	PkPackage *package;
	gchar **parts = NULL;

	/* if we've got it, use old data */
	if (md5_opt != NULL)
		md5 = g_strdup (md5_opt);
	else
		md5 = pk_transaction_extra_get_filename_md5 (filename);

	/* resolve */
	package = pk_transaction_extra_get_installed_package_for_file (extra, filename);
	if (package == NULL) {
		egg_warning ("failed to get list");
		goto out;
	}

	/* add */
	parts = pk_package_id_split (pk_package_get_id (package));
	rc = pk_transaction_extra_sqlite_add_filename_details (extra, filename, parts[PK_PACKAGE_ID_NAME], md5);
out:
	g_strfreev (parts);
	g_free (md5);
	return rc;
}

/**
 * pk_transaction_extra_sqlite_cache_rescan_cb:
 **/
static gint
pk_transaction_extra_sqlite_cache_rescan_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkTransactionExtra *extra = PK_POST_TRANS (data);
	const gchar *filename = NULL;
	const gchar *md5 = NULL;
	gchar *md5_calc = NULL;
	gint i;

	/* add the filename data to the array */
	for (i=0; i<argc; i++) {
		if (g_strcmp0 (col_name[i], "filename") == 0 && argv[i] != NULL)
			filename = argv[i];
		else if (g_strcmp0 (col_name[i], "md5") == 0 && argv[i] != NULL)
			md5 = argv[i];
	}

	/* sanity check */
	if (filename == NULL || md5 == NULL) {
		egg_warning ("filename %s and md5 %s)", filename, md5);
		goto out;
	}

	/* get md5 */
	md5_calc = pk_transaction_extra_get_filename_md5 (filename);
	if (md5_calc == NULL) {
		egg_debug ("remove of %s as no longer found", filename);
		pk_transaction_extra_sqlite_remove_filename (extra, filename);
		goto out;
	}

	/* we've checked the file */
	g_hash_table_insert (extra->priv->hash, g_strdup (filename), GUINT_TO_POINTER (1));

	/* check md5 is same */
	if (g_strcmp0 (md5, md5_calc) != 0) {
		egg_debug ("add of %s as md5 invalid (%s vs %s)", filename, md5, md5_calc);
		pk_transaction_extra_sqlite_add_filename (extra, filename, md5_calc);
	}

	egg_debug ("existing filename %s valid, md5=%s", filename, md5);
out:
	g_free (md5_calc);
	return 0;
}

/**
 * pk_transaction_extra_get_desktop_files:
 **/
static void
pk_transaction_extra_get_desktop_files (PkTransactionExtra *extra,
					const gchar *app_dir,
					GPtrArray *array)
{
	GError *error = NULL;
	GDir *dir;
	const gchar *filename;
	gpointer data;
	gchar *path;

	/* open directory */
	dir = g_dir_open (app_dir, 0, &error);
	if (dir == NULL) {
		egg_warning ("failed to open directory %s: %s", app_dir, error->message);
		g_error_free (error);
		return;
	}

	/* go through desktop files and add them to an array if not present */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		path = g_build_filename (app_dir, filename, NULL);
		if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
			pk_transaction_extra_get_desktop_files (extra, path, array);
		} else if (g_str_has_suffix (filename, ".desktop")) {
			data = g_hash_table_lookup (extra->priv->hash, path);
			if (data == NULL) {
				egg_debug ("add of %s as not present in db", path);
				g_ptr_array_add (array, g_strdup (path));
			}
		}
		g_free (path);
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);
}

/**
 * pk_transaction_extra_import_desktop_files:
 **/
gboolean
pk_transaction_extra_import_desktop_files (PkTransactionExtra *extra)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	gchar *path;
	GPtrArray *array;
	gfloat step;
	guint i;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	/* no database */
	if (extra->priv->db == NULL) {
		egg_debug ("unable to import: no database");
		return FALSE;
	}

	/* no support */
	if (!pk_backend_is_implemented (extra->priv->backend, PK_ROLE_ENUM_SEARCH_FILE)) {
		egg_debug ("cannot search files");
		return FALSE;
	}

	/* use a local backend instance */
	pk_backend_reset (extra->priv->backend);
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_SCAN_APPLICATIONS);

	/* reset hash */
	g_hash_table_remove_all (extra->priv->hash);
	pk_transaction_extra_set_progress_changed (extra, 101);

	/* first go through the existing data, and look for modifications and removals */
	statement = g_strdup ("SELECT filename, md5 FROM cache");
	rc = sqlite3_exec (extra->priv->db, statement, pk_transaction_extra_sqlite_cache_rescan_cb, extra, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
	}

	array = g_ptr_array_new_with_free_func (g_free);
	pk_transaction_extra_get_desktop_files (extra, PK_DESKTOP_DEFAULT_APPLICATION_DIR, array);

	if (array->len) {
		step = 100.0f / array->len;
		pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);

		/* process files in an array */
		for (i=0; i<array->len; i++) {
			pk_transaction_extra_set_progress_changed (extra, i * step);
			path = g_ptr_array_index (array, i);
			pk_transaction_extra_sqlite_add_filename (extra, path, NULL);
		}
	}
	g_ptr_array_free (array, TRUE);

	pk_transaction_extra_set_progress_changed (extra, 100);
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_FINISHED);
	return TRUE;
}

/**
 * pk_transaction_extra_package_list_to_string:
 **/
static gchar *
pk_transaction_extra_package_list_to_string (GPtrArray *array)
{
	guint i;
	PkPackage *package;
	GString *string;
	PkInfoEnum info;
	gchar *package_id;
	gchar *summary;

	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		g_string_append_printf (string, "%s\t%s\t%s\n", pk_info_enum_to_string (info), package_id, summary);
		g_free (package_id);
		g_free (summary);
	}

	/* remove trailing newline */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);
	return g_string_free (string, FALSE);
}

/**
 * pk_transaction_extra_update_package_list:
 **/
gboolean
pk_transaction_extra_update_package_list (PkTransactionExtra *extra)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	if (!pk_backend_is_implemented (extra->priv->backend, PK_ROLE_ENUM_GET_PACKAGES)) {
		egg_debug ("cannot get packages");
		return FALSE;
	}

	egg_debug ("updating package lists");

	/* clear old list */
	if (extra->priv->list->len > 0)
		g_ptr_array_remove_range (extra->priv->list, 0, extra->priv->list->len);

	/* update UI */
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_transaction_extra_set_progress_changed (extra, 101);

	/* get the new package list */
	pk_backend_reset (extra->priv->backend);
	pk_backend_get_packages (extra->priv->backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (extra->priv->loop);

	/* update UI */
	pk_transaction_extra_set_progress_changed (extra, 90);

	/* convert to a file */
	data = pk_transaction_extra_package_list_to_string (extra->priv->list);
	ret = g_file_set_contents (PK_SYSTEM_PACKAGE_LIST_FILENAME, data, -1, &error);
	if (!ret) {
		egg_warning ("failed to save to file: %s", error->message);
		g_error_free (error);
	}

	/* update UI */
	pk_transaction_extra_set_progress_changed (extra, 100);
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_FINISHED);

	g_free (data);
	return ret;
}

/**
 * pk_transaction_extra_clear_firmware_requests:
 **/
gboolean
pk_transaction_extra_clear_firmware_requests (PkTransactionExtra *extra)
{
	gboolean ret;
	gchar *filename;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	/* clear the firmware requests directory */
	filename = g_build_filename (LOCALSTATEDIR, "run", "PackageKit", "udev", NULL);
	egg_debug ("clearing udev firmware requests at %s", filename);
	ret = pk_directory_remove_contents (filename);
	if (!ret)
		egg_warning ("failed to clear %s", filename);
	g_free (filename);
	return ret;
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
		egg_debug ("package %s updated, and %s is running", package_id, filenames[i]);
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
		egg_debug ("cannot get files");
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
 * pk_transaction_extra_update_files_check_desktop_cb:
 **/
static void
pk_transaction_extra_update_files_check_desktop_cb (PkBackend *backend, PkFiles *files, PkTransactionExtra *extra)
{
	guint i;
	guint len;
	gboolean ret;
	gchar **package;
	gchar *md5;
	gchar **filenames = NULL;
	gchar *package_id = NULL;

	/* get data */
	g_object_get (files,
		      "package-id", &package_id,
		      "files", &filenames,
		      NULL);

	package = pk_package_id_split (package_id);

	/* check each file */
	len = g_strv_length (filenames);
	for (i=0; i<len; i++) {
		/* exists? */
		ret = g_file_test (filenames[i], G_FILE_TEST_EXISTS);
		if (!ret)
			continue;

		/* .desktop file? */
		ret = g_str_has_suffix (filenames[i], ".desktop");
		if (!ret)
			continue;

		egg_debug ("adding filename %s", filenames[i]);
		md5 = pk_transaction_extra_get_filename_md5 (filenames[i]);
		pk_transaction_extra_sqlite_add_filename_details (extra, filenames[i], package[PK_PACKAGE_ID_NAME], md5);
		g_free (md5);
	}
	g_strfreev (filenames);
	g_strfreev (package);
	g_free (package_id);
}

/**
 * pk_transaction_extra_check_desktop_files:
 **/
gboolean
pk_transaction_extra_check_desktop_files (PkTransactionExtra *extra, gchar **package_ids)
{
	guint signal_files = 0;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);

	if (!pk_backend_is_implemented (extra->priv->backend, PK_ROLE_ENUM_GET_FILES)) {
		egg_debug ("cannot get files");
		return FALSE;
	}

	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_SCAN_APPLICATIONS);
	pk_transaction_extra_set_progress_changed (extra, 101);

	signal_files = g_signal_connect (extra->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_update_files_check_desktop_cb), extra);

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
		egg_debug ("adding filename %s", filenames[i]);
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
		egg_warning ("failed to get cmdline: %s", error->message);
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

		egg_debug ("pid=%i: %s (%i)", pid, cmdline_full, uid);
		if (uid < 500)
			g_ptr_array_add (files_system, cmdline_full);
		else
			g_ptr_array_add (files_session, cmdline_full);
		g_free (cmdline);
	}

	/* we found nothing */
	if (files_system->len == 0 && files_session->len == 0) {
		egg_warning ("no pids could be resolved");
		goto out;
	}

	/* process all session restarts */
	for (i=0; i<files_session->len; i++) {
		filename = g_ptr_array_index (files_session, i);

		package = pk_transaction_extra_get_installed_package_for_file (extra, filename);
		if (package == NULL) {
			egg_debug ("failed to find package for %s", filename);
			continue;
		}
		pk_backend_require_restart (extra->priv->backend, PK_RESTART_ENUM_SECURITY_SESSION, pk_package_get_id (package));
	}

	/* process all system restarts */
	for (i=0; i<files_system->len; i++) {
		filename = g_ptr_array_index (files_system, i);

		package = pk_transaction_extra_get_installed_package_for_file (extra, filename);
		if (package == NULL) {
			egg_debug ("failed to find package for %s", filename);
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
 * pk_transaction_extra_match_running_file:
 *
 * Only if the pattern matches the old and new names we refuse to run
 **/
static gboolean
pk_transaction_extra_match_running_file (PkTransactionExtra *extra, const gchar *filename)
{
	guint i;
	gchar **list;
	gboolean ret;

	/* compare each pattern */
	list = extra->priv->no_update_process_list;
	for (i=0; list[i] != NULL; i++) {

		/* does the package filename match */
		ret = g_pattern_match_simple (list[i], filename);
		if (ret) {
			/* is there a running process that also matches */
			ret = pk_proc_find_exec (extra->priv->proc, list[i]);
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
 * pk_transaction_extra_files_check_applications_are_running_cb:
 **/
static void
pk_transaction_extra_files_check_applications_are_running_cb (PkBackend *backend, PkFiles *files, PkTransactionExtra *extra)
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
	egg_debug ("len=%i", len);
	for (i=0; i<len; i++) {

		/* does the package filename match */
		ret = pk_transaction_extra_match_running_file (extra, filenames[i]);
		if (!ret)
			continue;

		/* add as it matches the criteria */
		egg_debug ("adding filename %s", filenames[i]);
		g_ptr_array_add (extra->priv->files_list, g_strdup (filenames[i]));
	}
	g_strfreev (filenames);
}

/**
 * pk_transaction_extra_applications_are_running:
 **/
gboolean
pk_transaction_extra_applications_are_running (PkTransactionExtra *extra, gchar **package_ids, GError **error)
{
	gboolean ret = TRUE;
	const gchar *file;
	gchar **files = NULL;
	gchar *process = NULL;
	guint signal_files = 0;

	g_return_val_if_fail (PK_IS_POST_TRANS (extra), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);

	if (!pk_backend_is_implemented (extra->priv->backend, PK_ROLE_ENUM_GET_FILES)) {
		egg_debug ("cannot get files");
		/* return success, as we're not setting an error */
		return TRUE;
	}

	/* check we have entry */
	if (extra->priv->no_update_process_list == NULL ||
	    extra->priv->no_update_process_list[0] == NULL) {
		egg_debug ("no processes to watch");
		/* return success, as we're not setting an error */
		return TRUE;
	}

	/* reset */
	g_ptr_array_set_size (extra->priv->files_list, 0);

	/* set status */
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_SCAN_PROCESS_LIST);
	pk_transaction_extra_set_progress_changed (extra, 101);

	/* get list from proc */
	ret = pk_proc_refresh (extra->priv->proc);
	if (!ret) {
		egg_warning ("failed to refresh");
		/* non-fatal */
		ret = TRUE;
		goto out;
	}

	/* set status */
	pk_transaction_extra_set_status_changed (extra, PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES);

	signal_files = g_signal_connect (extra->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_files_check_applications_are_running_cb), extra);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (extra->priv->backend);
	pk_backend_get_files (extra->priv->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (extra->priv->loop);

	/* there is a file we can't COW */
	if (extra->priv->files_list->len != 0) {
		file = g_ptr_array_index (extra->priv->files_list, 0);
		g_set_error (error, 1, 0, "failed to run as %s is running", file);
		ret = FALSE;
		goto out;
	}
out:
	pk_transaction_extra_set_progress_changed (extra, 100);
	if (signal_files > 0)
		g_signal_handler_disconnect (extra->priv->backend, signal_files);
	g_strfreev (files);
	g_free (process);
	return ret;
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
		egg_debug ("cannot get files");
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
		egg_warning ("failed to refresh");
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
		egg_debug ("no files");
		goto out;
	}

	/* get the list of PIDs */
	files = pk_ptr_array_to_strv (extra->priv->files_list);
	extra->priv->pids = pk_lsof_get_pids_for_filenames (extra->priv->lsof, files);

	/* nothing depends on these libraries */
	if (extra->priv->pids == NULL) {
		egg_warning ("failed to get process list");
		goto out;
	}

	/* nothing depends on these libraries */
	if (extra->priv->pids->len == 0) {
		egg_debug ("no processes depend on these libraries");
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
	sqlite3_close (extra->priv->db);
	g_hash_table_unref (extra->priv->hash);
	g_ptr_array_unref (extra->priv->files_list);
	g_strfreev (extra->priv->no_update_process_list);

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
	gboolean ret;
	const gchar *statement;
	gchar *error_msg = NULL;
	gint rc;

	extra->priv = PK_POST_TRANS_GET_PRIVATE (extra);
	extra->priv->loop = g_main_loop_new (NULL, FALSE);
	extra->priv->list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	extra->priv->backend = pk_backend_new ();
	extra->priv->lsof = pk_lsof_new ();
	extra->priv->proc = pk_proc_new ();
	extra->priv->db = NULL;
	extra->priv->pids = NULL;
	extra->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	extra->priv->files_list = g_ptr_array_new_with_free_func (g_free);
	extra->priv->conf = pk_conf_new ();

	extra->priv->finished_id =
		g_signal_connect (extra->priv->backend, "finished",
				  G_CALLBACK (pk_transaction_extra_finished_cb), extra);
	extra->priv->package_id =
		g_signal_connect (extra->priv->backend, "package",
				  G_CALLBACK (pk_transaction_extra_package_cb), extra);

	/* get the list of processes we should neverupdate when running */
	extra->priv->no_update_process_list = pk_conf_get_strv (extra->priv->conf, "NoUpdateProcessList");

	/* check if exists */
	ret = g_file_test (PK_DESKTOP_DEFAULT_DATABASE, G_FILE_TEST_EXISTS);

	egg_debug ("trying to open database '%s'", PK_DESKTOP_DEFAULT_DATABASE);
	rc = sqlite3_open (PK_DESKTOP_DEFAULT_DATABASE, &extra->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (extra->priv->db));
		sqlite3_close (extra->priv->db);
		extra->priv->db = NULL;
		return;
	}

	/* create if not exists */
	if (!ret) {
		egg_debug ("creating database cache in %s", PK_DESKTOP_DEFAULT_DATABASE);
		statement = "CREATE TABLE cache ("
			    "filename TEXT,"
			    "package TEXT,"
			    "show INTEGER,"
			    "md5 TEXT);";
		rc = sqlite3_exec (extra->priv->db, statement, NULL, NULL, &error_msg);
		if (rc != SQLITE_OK) {
			egg_warning ("SQL error: %s\n", error_msg);
			sqlite3_free (error_msg);
		}
	}

	/* we don't need to keep syncing */
	sqlite3_exec (extra->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
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

