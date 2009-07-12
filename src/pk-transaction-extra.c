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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <packagekit-glib/packagekit.h>
#include <gio/gdesktopappinfo.h>
#include <sqlite3.h>

#include "egg-debug.h"

#include "pk-transaction-extra.h"
#include "pk-shared.h"
#include "pk-marshal.h"
#include "pk-backend-internal.h"
#include "pk-lsof.h"

#define PK_POST_TRANS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_POST_TRANS, PkTransactionExtraPrivate))

struct PkTransactionExtraPrivate
{
	sqlite3			*db;
	PkBackend		*backend;
	GMainLoop		*loop;
	PkObjList		*running_exec_list;
	PkPackageList		*list;
	PkLsof			*lsof;
	guint			 finished_id;
	guint			 package_id;
	GHashTable		*hash;
	GPtrArray		*files_list;
};

enum {
	PK_POST_TRANS_STATUS_CHANGED,
	PK_POST_TRANS_PROGRESS_CHANGED,
	PK_POST_TRANS_REQUIRE_RESTART,
	PK_POST_TRANS_LAST_SIGNAL
};

static guint signals [PK_POST_TRANS_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (PkTransactionExtra, pk_transaction_extra, G_TYPE_OBJECT)

/**
 * pk_transaction_extra_finished_cb:
 **/
static void
pk_transaction_extra_finished_cb (PkBackend *backend, PkExitEnum exit_enum, PkTransactionExtra *post)
{
	if (g_main_loop_is_running (post->priv->loop))
		g_main_loop_quit (post->priv->loop);
}

/**
 * pk_transaction_extra_package_cb:
 **/
static void
pk_transaction_extra_package_cb (PkBackend *backend, const PkPackageObj *obj, PkTransactionExtra *post)
{
	pk_obj_list_add (PK_OBJ_LIST(post->priv->list), obj);
}

/**
 * pk_transaction_extra_set_require_restart:
 **/
static void
pk_transaction_extra_set_require_restart (PkTransactionExtra *post, PkRestartEnum restart, const gchar *package_id)
{
	egg_debug ("emit require-restart %s, %s", pk_restart_enum_to_text (restart), package_id);
	g_signal_emit (post, signals [PK_POST_TRANS_REQUIRE_RESTART], 0, restart, package_id);
}

/**
 * pk_transaction_extra_set_status_changed:
 **/
static void
pk_transaction_extra_set_status_changed (PkTransactionExtra *post, PkStatusEnum status)
{
	egg_debug ("emiting status-changed %s", pk_status_enum_to_text (status));
	g_signal_emit (post, signals [PK_POST_TRANS_STATUS_CHANGED], 0, status);
}

/**
 * pk_transaction_extra_set_progress_changed:
 **/
static void
pk_transaction_extra_set_progress_changed (PkTransactionExtra *post, guint percentage)
{
	egg_debug ("emiting progress-changed %i", percentage);
	g_signal_emit (post, signals [PK_POST_TRANS_PROGRESS_CHANGED], 0, percentage, 0, 0, 0);
}

/**
 * pk_transaction_extra_get_installed_package_for_file:
 **/
static const PkPackageObj *
pk_transaction_extra_get_installed_package_for_file (PkTransactionExtra *post, const gchar *filename)
{
	guint size;
	const PkPackageObj *obj = NULL;
	PkStore *store;

	/* use PK to find the correct package */
	pk_obj_list_clear (PK_OBJ_LIST(post->priv->list));
	pk_backend_reset (post->priv->backend);
	store = pk_backend_get_store (post->priv->backend);
	pk_store_set_uint (store, "filters", pk_bitfield_value (PK_FILTER_ENUM_INSTALLED));
	pk_store_set_string (store, "search", filename);
	post->priv->backend->desc->search_file (post->priv->backend, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), filename);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	/* check that we only matched one package */
	size = pk_package_list_get_size (post->priv->list);
	if (size != 1) {
		egg_warning ("not correct size, %i", size);
		goto out;
	}

	/* get the obj */
	obj = pk_package_list_get_obj (post->priv->list, 0);
	if (obj == NULL) {
		egg_warning ("cannot get obj");
		goto out;
	}
out:
	return obj;
}

/**
 * pk_transaction_extra_string_list_new:
 **/
static PkObjList *
pk_transaction_extra_string_list_new ()
{
	PkObjList *list;
	list = pk_obj_list_new ();
	pk_obj_list_set_compare (list, (PkObjListCompareFunc) g_strcmp0);
	pk_obj_list_set_copy (list, (PkObjListCopyFunc) g_strdup);
	pk_obj_list_set_free (list, (PkObjListFreeFunc) g_free);
	pk_obj_list_set_to_string (list, (PkObjListToStringFunc) g_strdup);
	pk_obj_list_set_from_string (list, (PkObjListFromStringFunc) g_strdup);
	return list;
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
pk_transaction_extra_sqlite_remove_filename (PkTransactionExtra *post, const gchar *filename)
{
	gchar *statement;
	gint rc;

	statement = g_strdup_printf ("DELETE FROM cache WHERE filename = '%s'", filename);
	rc = sqlite3_exec (post->priv->db, statement, NULL, NULL, NULL);
	g_free (statement);
	return rc;
}

/**
 * pk_transaction_extra_sqlite_add_filename_details:
 **/
static gint
pk_transaction_extra_sqlite_add_filename_details (PkTransactionExtra *post, const gchar *filename, const gchar *package, const gchar *md5)
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
	sqlite3_exec (post->priv->db, statement, NULL, NULL, NULL);
	g_free (statement);

	/* prepare the query, as we don't escape it */
	rc = sqlite3_prepare_v2 (post->priv->db, "INSERT INTO cache (filename, package, show, md5) VALUES (?, ?, ?, ?)", -1, &sql_statement, NULL);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL failed to prepare: %s", sqlite3_errmsg (post->priv->db));
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
pk_transaction_extra_sqlite_add_filename (PkTransactionExtra *post, const gchar *filename, const gchar *md5_opt)
{
	gchar *md5 = NULL;
	gchar *package = NULL;
	gint rc = -1;
	const PkPackageObj *obj;

	/* if we've got it, use old data */
	if (md5_opt != NULL)
		md5 = g_strdup (md5_opt);
	else
		md5 = pk_transaction_extra_get_filename_md5 (filename);

	/* resolve */
	obj = pk_transaction_extra_get_installed_package_for_file (post, filename);
	if (obj == NULL) {
		egg_warning ("failed to get list");
		goto out;
	}

	/* add */
	rc = pk_transaction_extra_sqlite_add_filename_details (post, filename, obj->id->name, md5);
out:
	g_free (md5);
	g_free (package);
	return rc;
}

/**
 * pk_transaction_extra_sqlite_cache_rescan_cb:
 **/
static gint
pk_transaction_extra_sqlite_cache_rescan_cb (void *data, gint argc, gchar **argv, gchar **col_name)
{
	PkTransactionExtra *post = PK_POST_TRANS (data);
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
		pk_transaction_extra_sqlite_remove_filename (post, filename);
		goto out;
	}

	/* we've checked the file */
	g_hash_table_insert (post->priv->hash, g_strdup (filename), GUINT_TO_POINTER (1));

	/* check md5 is same */
	if (g_strcmp0 (md5, md5_calc) != 0) {
		egg_debug ("add of %s as md5 invalid (%s vs %s)", filename, md5, md5_calc);
		pk_transaction_extra_sqlite_add_filename (post, filename, md5_calc);
	}

	egg_debug ("existing filename %s valid, md5=%s", filename, md5);
out:
	g_free (md5_calc);
	return 0;
}

/**
 * pk_transaction_extra_import_desktop_files:
 **/
gboolean
pk_transaction_extra_import_desktop_files (PkTransactionExtra *post)
{
	gchar *statement;
	gchar *error_msg = NULL;
	gint rc;
	GError *error = NULL;
	GDir *dir;
	const gchar *filename;
	gpointer data;
	gchar *path;
	GPtrArray *array;
	gfloat step;
	guint i;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);
	g_return_val_if_fail (post->priv->db != NULL, FALSE);

	if (post->priv->backend->desc->search_file == NULL) {
		egg_debug ("cannot search files");
		return FALSE;
	}

	/* use a local backend instance */
	pk_backend_reset (post->priv->backend);
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_SCAN_APPLICATIONS);

	/* reset hash */
	g_hash_table_remove_all (post->priv->hash);
	pk_transaction_extra_set_progress_changed (post, 101);

	/* first go through the existing data, and look for modifications and removals */
	statement = g_strdup ("SELECT filename, md5 FROM cache");
	rc = sqlite3_exec (post->priv->db, statement, pk_transaction_extra_sqlite_cache_rescan_cb, post, &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		egg_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
	}

	/* open directory */
	dir = g_dir_open (PK_DESKTOP_DEFAULT_APPLICATION_DIR, 0, &error);
	if (dir == NULL) {
		egg_warning ("failed to open file %s: %s", PK_DESKTOP_DEFAULT_APPLICATION_DIR, error->message);
		g_error_free (error);
		goto out;
	}

	/* go through desktop files and add them to an array if not present */
	filename = g_dir_read_name (dir);
	array = g_ptr_array_new ();
	while (filename != NULL) {
		if (g_str_has_suffix (filename, ".desktop")) {
			path = g_build_filename (PK_DESKTOP_DEFAULT_APPLICATION_DIR, filename, NULL);
			data = g_hash_table_lookup (post->priv->hash, path);
			if (data == NULL) {
				egg_debug ("add of %s as not present in db", path);
				g_ptr_array_add (array, g_strdup (path));
			}
			g_free (path);
		}
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);

	step = 100.0f / array->len;
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);

	/* process files in an array */
	for (i=0; i<array->len; i++) {
		pk_transaction_extra_set_progress_changed (post, i * step);
		path = g_ptr_array_index (array, i);
		pk_transaction_extra_sqlite_add_filename (post, path, NULL);
	}
	g_ptr_array_foreach (array, (GFunc) g_free, NULL);
	g_ptr_array_free (array, TRUE);

out:
	pk_transaction_extra_set_progress_changed (post, 100);
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_FINISHED);
	return TRUE;
}

/**
 * pk_transaction_extra_update_package_list:
 **/
gboolean
pk_transaction_extra_update_package_list (PkTransactionExtra *post)
{
	gboolean ret;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

	if (post->priv->backend->desc->get_packages == NULL) {
		egg_debug ("cannot get packages");
		return FALSE;
	}

	egg_debug ("updating package lists");

	/* clear old list */
	pk_obj_list_clear (PK_OBJ_LIST(post->priv->list));

	/* update UI */
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_transaction_extra_set_progress_changed (post, 101);

	/* get the new package list */
	pk_backend_reset (post->priv->backend);
	pk_store_set_uint (pk_backend_get_store (post->priv->backend), "filters", pk_bitfield_value (PK_FILTER_ENUM_NONE));
	post->priv->backend->desc->get_packages (post->priv->backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	/* update UI */
	pk_transaction_extra_set_progress_changed (post, 90);

	/* convert to a file */
	ret = pk_obj_list_to_file (PK_OBJ_LIST(post->priv->list), PK_SYSTEM_PACKAGE_LIST_FILENAME);
	if (!ret)
		egg_warning ("failed to save to file");

	/* update UI */
	pk_transaction_extra_set_progress_changed (post, 100);
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_FINISHED);

	return ret;
}

/**
 * pk_transaction_extra_clear_firmware_requests:
 **/
gboolean
pk_transaction_extra_clear_firmware_requests (PkTransactionExtra *post)
{
	gboolean ret;
	gchar *filename;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

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
pk_transaction_extra_update_files_check_running_cb (PkBackend *backend, const gchar *package_id,
					     const gchar *filelist, PkTransactionExtra *post)
{
	guint i;
	guint len;
	gboolean ret;
	gchar **files;
	PkPackageId *id;

	id = pk_package_id_new_from_string (package_id);
	files = g_strsplit (filelist, ";", 0);

	/* check each file */
	len = g_strv_length (files);
	for (i=0; i<len; i++) {
		/* executable? */
		ret = g_file_test (files[i], G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_EXECUTABLE | G_FILE_TEST_EXISTS);
		if (!ret)
			continue;

		/* running? */
		ret = pk_obj_list_exists (PK_OBJ_LIST(post->priv->running_exec_list), files[i]);
		if (!ret)
			continue;

		/* TODO: findout if the executable has a desktop file, and if so,
		 * suggest an application restart instead */

		/* send signal about session restart */
		egg_debug ("package %s updated, and %s is running", id->name, files[i]);
		pk_backend_require_restart (post->priv->backend, PK_RESTART_ENUM_SESSION, package_id);
	}
	g_strfreev (files);
	pk_package_id_free (id);
}

#ifdef USE_SECURITY_POLKIT
/**
 * dkp_post_trans_get_cmdline:
 **/
static gchar *
dkp_post_trans_get_cmdline (guint pid)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		egg_debug ("failed to get cmdline: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (filename);
	return cmdline;
}
#endif

/**
 * pk_transaction_extra_update_process_list:
 **/
static gboolean
pk_transaction_extra_update_process_list (PkTransactionExtra *post)
{
	GDir *dir;
	const gchar *name;
	gchar *offset;
	gchar *uid_file;
	gchar *contents;
	gboolean ret;
	guint uid;
	pid_t pid;
	gchar *exec;

	uid = getuid ();
	dir = g_dir_open ("/proc", 0, NULL);
	name = g_dir_read_name (dir);
	pk_obj_list_clear (PK_OBJ_LIST(post->priv->running_exec_list));
	while (name != NULL) {
		contents = NULL;
		uid_file = g_build_filename ("/proc", name, "loginuid", NULL);

		/* is a process file */
		if (!g_file_test (uid_file, G_FILE_TEST_EXISTS))
			goto out;

		/* able to get contents */
		ret = g_file_get_contents (uid_file, &contents, 0, NULL);
		if (!ret)
			goto out;

		/* is run by our UID */
		uid = atoi (contents);

		/* get the exec for the pid */
		pid = atoi (name);
#ifdef USE_SECURITY_POLKIT
		exec = dkp_post_trans_get_cmdline (pid);
		if (exec == NULL)
			goto out;
#else
		goto out;
#endif

		/* can be /usr/libexec/notification-daemon.#prelink#.9sOhao */
		offset = g_strrstr (exec, ".#prelink#.");
		if (offset != NULL)
			*(offset) = '\0';
		egg_debug ("uid=%i, pid=%i, exec=%s", uid, pid, exec);
		pk_obj_list_add (PK_OBJ_LIST(post->priv->running_exec_list), exec);
out:
		g_free (uid_file);
		g_free (contents);
		name = g_dir_read_name (dir);
	}
	g_dir_close (dir);
	return TRUE;
}

/**
 * pk_transaction_extra_check_running_process:
 **/
gboolean
pk_transaction_extra_check_running_process (PkTransactionExtra *post, gchar **package_ids)
{
	PkStore *store;
	guint signal_files = 0;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

	if (post->priv->backend->desc->get_files == NULL) {
		egg_debug ("cannot get files");
		return FALSE;
	}

	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES);
	pk_transaction_extra_set_progress_changed (post, 101);

	store = pk_backend_get_store (post->priv->backend);
	pk_transaction_extra_update_process_list (post);

	signal_files = g_signal_connect (post->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_update_files_check_running_cb), post);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (post->priv->backend);
	pk_store_set_strv (store, "package_ids", package_ids);
	post->priv->backend->desc->get_files (post->priv->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	g_signal_handler_disconnect (post->priv->backend, signal_files);
	pk_transaction_extra_set_progress_changed (post, 100);
	return TRUE;
}

/**
 * pk_transaction_extra_update_files_check_desktop_cb:
 **/
static void
pk_transaction_extra_update_files_check_desktop_cb (PkBackend *backend, const gchar *package_id,
					     const gchar *filelist, PkTransactionExtra *post)
{
	guint i;
	guint len;
	gboolean ret;
	gchar **files;
	gchar **package;
	PkPackageId *id;
	gchar *md5;

	id = pk_package_id_new_from_string (package_id);
	files = g_strsplit (filelist, ";", 0);
	package = g_strsplit (package_id, ";", 0);

	/* check each file */
	len = g_strv_length (files);
	for (i=0; i<len; i++) {
		/* exists? */
		ret = g_file_test (files[i], G_FILE_TEST_EXISTS);
		if (!ret)
			continue;

		/* .desktop file? */
		ret = g_str_has_suffix (files[i], ".desktop");
		if (!ret)
			continue;

		egg_debug ("adding filename %s", files[i]);
		md5 = pk_transaction_extra_get_filename_md5 (files[i]);
		pk_transaction_extra_sqlite_add_filename_details (post, files[i], package[0], md5);
		g_free (md5);
	}
	g_strfreev (files);
	g_strfreev (package);
	pk_package_id_free (id);
}

/**
 * pk_transaction_extra_check_desktop_files:
 **/
gboolean
pk_transaction_extra_check_desktop_files (PkTransactionExtra *post, gchar **package_ids)
{
	PkStore *store;
	guint signal_files = 0;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

	if (post->priv->backend->desc->get_files == NULL) {
		egg_debug ("cannot get files");
		return FALSE;
	}

	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_SCAN_APPLICATIONS);
	pk_transaction_extra_set_progress_changed (post, 101);

	store = pk_backend_get_store (post->priv->backend);
	signal_files = g_signal_connect (post->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_update_files_check_desktop_cb), post);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (post->priv->backend);
	pk_store_set_strv (store, "package_ids", package_ids);
	post->priv->backend->desc->get_files (post->priv->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	g_signal_handler_disconnect (post->priv->backend, signal_files);
	pk_transaction_extra_set_progress_changed (post, 100);
	return TRUE;
}

/**
 * pk_transaction_extra_files_check_library_restart_cb:
 **/
static void
pk_transaction_extra_files_check_library_restart_cb (PkBackend *backend, const gchar *package_id,
					      const gchar *filelist, PkTransactionExtra *post)
{
	guint i;
	guint len;
	gchar **files = NULL;

	files = g_strsplit (filelist, ";", 0);

	/* check each file to see if it's a system shared library */
	len = g_strv_length (files);
	for (i=0; i<len; i++) {
		/* not a system library */
		if (strstr (files[i], "/lib") == NULL)
			continue;

		/* not a shared object */
		if (strstr (files[i], ".so") == NULL)
			continue;

		/* add as it matches the criteria */
		egg_debug ("adding filename %s", files[i]);
		g_ptr_array_add (post->priv->files_list, g_strdup (files[i]));
	}
}

/**
 * pk_transaction_extra_get_cmdline:
 **/
static gchar *
pk_transaction_extra_get_cmdline (PkTransactionExtra *post, guint pid)
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
pk_transaction_extra_get_uid (PkTransactionExtra *post, guint pid)
{
	gboolean ret;
	gint uid = -1;
	gchar *filename = NULL;
	gchar *uid_text = NULL;
	GError *error = NULL;

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/loginuid", pid);
	ret = g_file_get_contents (filename, &uid_text, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get cmdline: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* convert from text */
	uid = atoi (uid_text);
out:
	g_free (filename);
	g_free (uid_text);
	return uid;
}

/**
 * pk_transaction_extra_check_library_restart_emit:
 **/
static gboolean
pk_transaction_extra_check_library_restart_emit (PkTransactionExtra *post, GPtrArray *pids)
{
	gint uid;
	guint i;
	guint pid;
	gchar *filename;
	gchar *cmdline;
	gchar *cmdline_full;
	gchar *package_id;
	GPtrArray *files_session;
	GPtrArray *files_system;
	const PkPackageObj *obj;

	/* create arrays */
	files_session = g_ptr_array_new ();
	files_system = g_ptr_array_new ();

	/* find the package name of each pid */
	for (i=0; i<pids->len; i++) {
		pid = GPOINTER_TO_INT (g_ptr_array_index (pids, i));

		/* get user */
		uid = pk_transaction_extra_get_uid (post, pid);
		if (uid < 0)
			continue;

		/* get command line */
		cmdline = pk_transaction_extra_get_cmdline (post, pid);
		if (cmdline == NULL)
			continue;

		/* prepend path if it does not already exist */
		if (cmdline[0] == '/')
			cmdline_full = g_strdup (cmdline);
		else
			cmdline_full = g_strdup_printf ("/usr/bin/%s", cmdline);

		egg_warning ("pid=%i: %s (%i)", pid, cmdline_full, uid);
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

		obj = pk_transaction_extra_get_installed_package_for_file (post, filename);
		if (obj == NULL) {
			egg_warning ("failed to find package for %s", filename);
			continue;
		}

		package_id = pk_package_id_to_string (obj->id);
		pk_transaction_extra_set_require_restart (post, PK_RESTART_ENUM_SECURITY_SESSION, package_id);
		g_free (package_id);
	}

	/* process all system restarts */
	for (i=0; i<files_system->len; i++) {
		filename = g_ptr_array_index (files_system, i);

		obj = pk_transaction_extra_get_installed_package_for_file (post, filename);
		if (obj == NULL) {
			egg_warning ("failed to find package for %s", filename);
			continue;
		}

		package_id = pk_package_id_to_string (obj->id);
		pk_transaction_extra_set_require_restart (post, PK_RESTART_ENUM_SECURITY_SYSTEM, package_id);
		g_free (package_id);
	}

out:
	g_ptr_array_foreach (files_session, (GFunc) g_free, NULL);
	g_ptr_array_foreach (files_system, (GFunc) g_free, NULL);
	g_ptr_array_free (files_session, TRUE);
	g_ptr_array_free (files_system, TRUE);
	return TRUE;
}

/**
 * pk_transaction_extra_check_library_restart:
 **/
gboolean
pk_transaction_extra_check_library_restart (PkTransactionExtra *post, gchar **package_ids)
{
	PkStore *store;
	guint signal_files = 0;
	gboolean ret = TRUE;
	gchar **files = NULL;
	GPtrArray *pids;

	g_return_val_if_fail (PK_IS_POST_TRANS (post), FALSE);

	if (post->priv->backend->desc->get_files == NULL) {
		egg_debug ("cannot get files");
		return FALSE;
	}

	/* reset */
	g_ptr_array_foreach (post->priv->files_list, (GFunc) g_free, NULL);
	g_ptr_array_set_size (post->priv->files_list, 0);

	/* set status */
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_SCAN_PROCESS_LIST);
	pk_transaction_extra_set_progress_changed (post, 101);

	/* get list from lsof */
	ret = pk_lsof_refresh (post->priv->lsof);
	if (!ret) {
		egg_warning ("failed to refresh");
		goto out;
	}

	/* set status */
	pk_transaction_extra_set_status_changed (post, PK_STATUS_ENUM_CHECK_LIBRARIES);

	store = pk_backend_get_store (post->priv->backend);
	signal_files = g_signal_connect (post->priv->backend, "files",
					 G_CALLBACK (pk_transaction_extra_files_check_library_restart_cb), post);

	/* get all the files touched in the packages we just updated */
	pk_backend_reset (post->priv->backend);
	pk_store_set_strv (store, "package_ids", package_ids);
	post->priv->backend->desc->get_files (post->priv->backend, package_ids);

	/* wait for finished */
	g_main_loop_run (post->priv->loop);

	/* nothing to do */
	if (post->priv->files_list->len == 0) {
		egg_warning ("no files");
		goto out;
	}

	/* get the list of PIDs */
	files = pk_ptr_array_to_strv (post->priv->files_list);
	pids = pk_lsof_get_pids_for_filenames (post->priv->lsof, files);

	/* nothing depends on these libraries */
	if (pids == NULL) {
		egg_warning ("failed to get process list");
		goto out;
	}

	/* nothing depends on these libraries */
	if (pids->len == 0) {
		egg_warning ("no processes depend on these libraries");
		goto out;
	}

	/* emit */
	pk_transaction_extra_check_library_restart_emit (post, pids);
	g_ptr_array_free (pids, TRUE);
out:
	pk_transaction_extra_set_progress_changed (post, 100);
	if (signal_files > 0)
		g_signal_handler_disconnect (post->priv->backend, signal_files);
	g_strfreev (files);
	return ret;
}

/**
 * pk_transaction_extra_finalize:
 **/
static void
pk_transaction_extra_finalize (GObject *object)
{
	PkTransactionExtra *post;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_POST_TRANS (object));
	post = PK_POST_TRANS (object);

	g_signal_handler_disconnect (post->priv->backend, post->priv->finished_id);
	g_signal_handler_disconnect (post->priv->backend, post->priv->package_id);

	if (g_main_loop_is_running (post->priv->loop))
		g_main_loop_quit (post->priv->loop);
	g_main_loop_unref (post->priv->loop);
	sqlite3_close (post->priv->db);
	g_hash_table_unref (post->priv->hash);
	g_ptr_array_foreach (post->priv->files_list, (GFunc) g_free, NULL);
	g_ptr_array_free (post->priv->files_list, TRUE);

	g_object_unref (post->priv->backend);
	g_object_unref (post->priv->lsof);
	g_object_unref (post->priv->list);
	g_object_unref (post->priv->running_exec_list);

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
	signals [PK_POST_TRANS_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	g_type_class_add_private (klass, sizeof (PkTransactionExtraPrivate));
}

/**
 * pk_transaction_extra_init:
 *
 * initializes the post_trans class. NOTE: We expect post_trans objects
 * to *NOT* be removed or added during the session.
 * We only control the first post_trans object if there are more than one.
 **/
static void
pk_transaction_extra_init (PkTransactionExtra *post)
{
	gboolean ret;
	const gchar *statement;
	gchar *error_msg = NULL;
	gint rc;

	post->priv = PK_POST_TRANS_GET_PRIVATE (post);
	post->priv->running_exec_list = pk_transaction_extra_string_list_new ();
	post->priv->loop = g_main_loop_new (NULL, FALSE);
	post->priv->list = pk_package_list_new ();
	post->priv->backend = pk_backend_new ();
	post->priv->lsof = pk_lsof_new ();
	post->priv->db = NULL;
	post->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	post->priv->files_list = g_ptr_array_new ();

	post->priv->finished_id =
		g_signal_connect (post->priv->backend, "finished",
				  G_CALLBACK (pk_transaction_extra_finished_cb), post);
	post->priv->package_id =
		g_signal_connect (post->priv->backend, "package",
				  G_CALLBACK (pk_transaction_extra_package_cb), post);

	/* check if exists */
	ret = g_file_test (PK_DESKTOP_DEFAULT_DATABASE, G_FILE_TEST_EXISTS);

	egg_debug ("trying to open database '%s'", PK_DESKTOP_DEFAULT_DATABASE);
	rc = sqlite3_open (PK_DESKTOP_DEFAULT_DATABASE, &post->priv->db);
	if (rc != 0) {
		egg_warning ("Can't open database: %s\n", sqlite3_errmsg (post->priv->db));
		sqlite3_close (post->priv->db);
		post->priv->db = NULL;
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
		rc = sqlite3_exec (post->priv->db, statement, NULL, NULL, &error_msg);
		if (rc != SQLITE_OK) {
			egg_warning ("SQL error: %s\n", error_msg);
			sqlite3_free (error_msg);
		}
	}

	/* we don't need to keep syncing */
	sqlite3_exec (post->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
}

/**
 * pk_transaction_extra_new:
 * Return value: A new post_trans class instance.
 **/
PkTransactionExtra *
pk_transaction_extra_new (void)
{
	PkTransactionExtra *post;
	post = g_object_new (PK_TYPE_POST_TRANS, NULL);
	return PK_POST_TRANS (post);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_post_trans (EggTest *test)
{
	PkTransactionExtra *post;

	if (!egg_test_start (test, "PkTransactionExtra"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	post = pk_transaction_extra_new ();
	egg_test_assert (test, post != NULL);

	g_object_unref (post);

	egg_test_end (test);
}
#endif

