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
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <pk-plugin.h>
#include <sqlite3.h>

#include <packagekit-glib2/pk-desktop.h>
#include <packagekit-glib2/pk-package.h>

struct PkPluginPrivate {
	sqlite3			*db;
	GPtrArray		*list;
	GMainLoop		*loop;
	GHashTable		*hash;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "Scans desktop files on refresh and adds them to a database";
}

/**
 * pk_plugin_package_cb:
 **/
static void
pk_plugin_package_cb (PkBackendJob *job,
		      PkPackage *package,
		      PkPlugin *plugin)
{
	g_ptr_array_add (plugin->priv->list, g_object_ref (package));
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
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_warning ("%s failed with exit code: %s",
			   pk_role_enum_to_string (pk_backend_job_get_role (job)),
			   pk_exit_enum_to_string (exit_enum));
	}
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
	plugin->priv->list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	plugin->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	g_ptr_array_unref (plugin->priv->list);
	g_main_loop_unref (plugin->priv->loop);
	g_hash_table_unref (plugin->priv->hash);
	sqlite3_close (plugin->priv->db);
}

/**
 * pk_plugin_get_filename_md5:
 **/
static gchar *
pk_plugin_get_filename_md5 (const gchar *filename)
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
		g_warning ("failed to open file %s: %s",
			   filename, error->message);
		g_error_free (error);
		goto out;
	}

	/* check md5 is same */
	md5 = g_compute_checksum_for_data (G_CHECKSUM_MD5,
					   (const guchar *) data,
					   length);
out:
	g_free (data);
	return md5;
}

/**
 * pk_plugin_sqlite_remove_filename:
 **/
static gint
pk_plugin_sqlite_remove_filename (PkPlugin *plugin,
				  const gchar *filename)
{
	gchar *statement;
	gint rc;

	statement = g_strdup_printf ("DELETE FROM cache WHERE filename = '%s'",
				     filename);
	rc = sqlite3_exec (plugin->priv->db, statement, NULL, NULL, NULL);
	g_free (statement);
	return rc;
}

/**
 * pk_plugin_get_installed_package_for_file:
 **/
static PkPackage *
pk_plugin_get_installed_package_for_file (PkPlugin *plugin,
					  const gchar *filename)
{
	PkPackage *package = NULL;
	gchar **filenames;

	/* use PK to find the correct package */
	if (plugin->priv->list->len > 0)
		g_ptr_array_set_size (plugin->priv->list, 0);
	pk_backend_reset_job (plugin->backend, plugin->job);
	filenames = g_strsplit (filename, "|||", -1);
	pk_backend_search_files (plugin->backend,
				 plugin->job,
				 pk_bitfield_value (PK_FILTER_ENUM_INSTALLED),
				 filenames);
	g_strfreev (filenames);

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
	return package;
}

/**
 * pk_plugin_sqlite_add_filename_details:
 **/
static gint
pk_plugin_sqlite_add_filename_details (PkPlugin *plugin,
				       const gchar *filename,
				       const gchar *package,
				       const gchar *md5)
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
		g_warning ("could not load desktop file %s", filename);
		goto out;
	}
	show = g_app_info_should_show (G_APP_INFO (info));
	g_object_unref (info);

	g_debug ("add filename %s from %s with md5: %s (show: %i)",
		 filename, package, md5, show);

	/* the row might already exist */
	statement = g_strdup_printf ("DELETE FROM cache WHERE filename = '%s'",
				     filename);
	sqlite3_exec (plugin->priv->db, statement, NULL, NULL, NULL);
	g_free (statement);

	/* prepare the query, as we don't escape it */
	rc = sqlite3_prepare_v2 (plugin->priv->db,
				 "INSERT INTO cache (filename, package, show, md5) "
				 "VALUES (?, ?, ?, ?)",
				 -1, &sql_statement, NULL);
	if (rc != SQLITE_OK) {
		g_warning ("SQL failed to prepare: %s",
			   sqlite3_errmsg (plugin->priv->db));
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
		g_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}
out:
	return rc;
}

/**
 * pk_plugin_sqlite_add_filename:
 **/
static gint
pk_plugin_sqlite_add_filename (PkPlugin *plugin,
			       const gchar *filename,
			       const gchar *md5_opt)
{
	gchar *md5 = NULL;
	gint rc = -1;
	PkPackage *package;

	/* if we've got it, use old data */
	if (md5_opt != NULL)
		md5 = g_strdup (md5_opt);
	else
		md5 = pk_plugin_get_filename_md5 (filename);

	/* resolve */
	package = pk_plugin_get_installed_package_for_file (plugin,
							    filename);
	if (package == NULL) {
		g_warning ("failed to get list");
		goto out;
	}

	/* add */
	rc = pk_plugin_sqlite_add_filename_details (plugin,
						    filename,
						    pk_package_get_name (package),
						    md5);
out:
	g_free (md5);
	return rc;
}

/**
 * pk_plugin_sqlite_cache_rescan_cb:
 **/
static gint
pk_plugin_sqlite_cache_rescan_cb (void *data,
				  gint argc,
				  gchar **argv,
				  gchar **col_name)
{
	PkPlugin *plugin = (PkPlugin*) data;
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
		g_warning ("filename %s and md5 %s)", filename, md5);
		goto out;
	}

	/* get md5 */
	md5_calc = pk_plugin_get_filename_md5 (filename);
	if (md5_calc == NULL) {
		g_debug ("remove of %s as no longer found", filename);
		pk_plugin_sqlite_remove_filename (plugin, filename);
		goto out;
	}

	/* we've checked the file */
	g_hash_table_insert (plugin->priv->hash,
			     g_strdup (filename),
			     GUINT_TO_POINTER (1));

	/* check md5 is same */
	if (g_strcmp0 (md5, md5_calc) != 0) {
		g_debug ("add of %s as md5 invalid (%s vs %s)",
			 filename, md5, md5_calc);
		pk_plugin_sqlite_add_filename (plugin,
					       filename,
					       md5_calc);
	}

	g_debug ("existing filename %s valid, md5=%s", filename, md5);
out:
	g_free (md5_calc);
	return 0;
}

/**
 * pk_plugin_get_desktop_files:
 **/
static void
pk_plugin_get_desktop_files (PkPlugin *plugin,
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
		g_warning ("failed to open directory %s: %s",
			   app_dir, error->message);
		g_error_free (error);
		return;
	}

	/* go through desktop files and add them to an array
	 * if not present */
	filename = g_dir_read_name (dir);
	while (filename != NULL) {
		path = g_build_filename (app_dir, filename, NULL);
		if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
			pk_plugin_get_desktop_files (plugin,
						     path, array);
		} else if (g_str_has_suffix (filename, ".desktop")) {
			data = g_hash_table_lookup (plugin->priv->hash, path);
			if (data == NULL) {
				g_debug ("add of %s as not present in db",
					 path);
				g_ptr_array_add (array, g_strdup (path));
			}
		}
		g_free (path);
		filename = g_dir_read_name (dir);
	}
	g_dir_close (dir);
}

/**
 * pk_transaction_plugin_load_db:
 */
static void
pk_transaction_plugin_load_db (PkPlugin *plugin,
			       PkTransaction *transaction)
{
	const gchar *statement_create;
	gboolean ret;
	gchar *error_msg = NULL;
	gint rc;
	PkConf *conf;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "ScanDesktopFiles");
	if (!ret)
		goto out;

	/* check if database exists */
	ret = g_file_test (PK_DESKTOP_DEFAULT_DATABASE,
			   G_FILE_TEST_EXISTS);

	g_debug ("trying to open database '%s'",
		 PK_DESKTOP_DEFAULT_DATABASE);
	rc = sqlite3_open (PK_DESKTOP_DEFAULT_DATABASE, &plugin->priv->db);
	if (rc != 0) {
		g_warning ("Can't open desktop database: %s\n",
			   sqlite3_errmsg (plugin->priv->db));
		sqlite3_close (plugin->priv->db);
		plugin->priv->db = NULL;
		goto out;
	}

	/* create if not exists */
	if (!ret) {
		g_debug ("creating database cache in %s",
			 PK_DESKTOP_DEFAULT_DATABASE);
		statement_create = "CREATE TABLE cache ("
				   "filename TEXT,"
				   "package TEXT,"
				   "show INTEGER,"
				   "md5 TEXT);";
		rc = sqlite3_exec (plugin->priv->db, statement_create,
				   NULL, NULL, &error_msg);
		if (rc != SQLITE_OK) {
			g_warning ("SQL error: %s\n", error_msg);
			sqlite3_free (error_msg);
			goto out;
		}
	}

	/* we don't need to keep syncing */
	sqlite3_exec (plugin->priv->db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
out:
	return;
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
	gchar *error_msg = NULL;
	gchar *path;
	gchar *statement;
	gfloat step;
	gint rc;
	GPtrArray *array = NULL;
	guint i;
	PkRoleEnum role;

	/* load */
	if (plugin->priv->db == NULL)
		pk_transaction_plugin_load_db (plugin, transaction);

	/* no database */
	if (plugin->priv->db == NULL)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_REFRESH_CACHE)
		goto out;

	/* connect to backend */
	if (!pk_backend_is_implemented (plugin->backend,
					PK_ROLE_ENUM_SEARCH_FILE)) {
		g_debug ("cannot search files");
		goto out;
	}

	/* use a local backend instance */
	pk_backend_reset_job (plugin->backend, plugin->job);
	pk_backend_job_set_vfunc (plugin->job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  (PkBackendJobVFunc) pk_plugin_finished_cb,
				  plugin);
	pk_backend_job_set_vfunc (plugin->job,
				  PK_BACKEND_SIGNAL_PACKAGE,
				  (PkBackendJobVFunc) pk_plugin_package_cb,
				  plugin);
	pk_backend_job_set_status (plugin->job,
				   PK_STATUS_ENUM_SCAN_APPLICATIONS);

	/* reset hash */
	g_hash_table_remove_all (plugin->priv->hash);
	pk_backend_job_set_percentage (plugin->job, 101);

	/* first go through the existing data, and look for
	 * modifications and removals */
	statement = g_strdup ("SELECT filename, md5 FROM cache");
	rc = sqlite3_exec (plugin->priv->db,
			   statement,
			   pk_plugin_sqlite_cache_rescan_cb,
			   plugin,
			   &error_msg);
	g_free (statement);
	if (rc != SQLITE_OK) {
		g_warning ("SQL error: %s\n", error_msg);
		sqlite3_free (error_msg);
		goto out;
	}

	array = g_ptr_array_new_with_free_func (g_free);
	pk_plugin_get_desktop_files (plugin,
				     PK_DESKTOP_DEFAULT_APPLICATION_DIR,
				     array);

	if (array->len) {
		step = 100.0f / array->len;
		pk_backend_job_set_status (plugin->job,
				       PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);

		/* process files in an array */
		for (i=0; i<array->len; i++) {
			pk_backend_job_set_percentage (plugin->job, i * step);
			path = g_ptr_array_index (array, i);
			pk_plugin_sqlite_add_filename (plugin,
						       path,
						       NULL);
		}
	}

	pk_backend_job_set_percentage (plugin->job, 100);
	pk_backend_job_set_status (plugin->job, PK_STATUS_ENUM_FINISHED);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
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

		g_debug ("adding filename %s", filenames[i]);
		md5 = pk_plugin_get_filename_md5 (filenames[i]);
		pk_plugin_sqlite_add_filename_details (plugin,
						       filenames[i],
						       package[PK_PACKAGE_ID_NAME],
						       md5);
		g_free (md5);
	}
	g_strfreev (filenames);
	g_strfreev (package);
	g_free (package_id);
}

/**
 * pk_plugin_transaction_finished_results:
 */
void
pk_plugin_transaction_finished_results (PkPlugin *plugin,
					PkTransaction *transaction)
{
	gchar **package_ids = NULL;
	gchar *package_id_tmp;
	GPtrArray *array = NULL;
	GPtrArray *list = NULL;
	guint i;
	PkInfoEnum info;
	PkPackage *item;
	PkResults *results;
	PkRoleEnum role;

	/* load */
	if (plugin->priv->db == NULL)
		pk_transaction_plugin_load_db (plugin, transaction);

	/* no database */
	if (plugin->priv->db == NULL)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_INSTALL_PACKAGES)
		goto out;

	/* connect to backend */
	if (!pk_backend_is_implemented (plugin->backend,
					PK_ROLE_ENUM_GET_FILES)) {
		g_debug ("cannot get files");
		goto out;
	}

	/* get results */
	results = pk_transaction_get_results (transaction);
	array = pk_results_get_package_array (results);

	/* filter on INSTALLING | UPDATING */
	list = g_ptr_array_new_with_free_func (g_free);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		info = pk_package_get_info (item);
		if (info == PK_INFO_ENUM_INSTALLING ||
		    info == PK_INFO_ENUM_UPDATING) {
			/* we convert the package_id data to be 'installed' */
			package_id_tmp = pk_package_id_build (pk_package_get_name (item),
							      pk_package_get_version (item),
							      pk_package_get_arch (item),
							      "installed");
			g_ptr_array_add (list, package_id_tmp);
		}
	}

	/* process file lists on these packages */
	g_debug ("processing %i packags for desktop files", list->len);
	if (list->len == 0)
		goto out;

	/* get all the files touched in the packages we just installed */
	pk_backend_reset_job (plugin->backend, plugin->job);
	pk_backend_job_set_vfunc (plugin->job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  (PkBackendJobVFunc) pk_plugin_finished_cb,
				  plugin);
	pk_backend_job_set_vfunc (plugin->job,
				  PK_BACKEND_SIGNAL_FILES,
				  (PkBackendJobVFunc) pk_plugin_files_cb,
				  plugin);
	pk_backend_job_set_status (plugin->job, PK_STATUS_ENUM_SCAN_APPLICATIONS);
	pk_backend_job_set_percentage (plugin->job, 101);
	package_ids = pk_ptr_array_to_strv (list);
	pk_backend_get_files (plugin->backend, plugin->job, package_ids);

	/* wait for finished */
	g_main_loop_run (plugin->priv->loop);

	pk_backend_job_set_percentage (plugin->job, 100);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (list != NULL)
		g_ptr_array_unref (list);
	g_strfreev (package_ids);
}
