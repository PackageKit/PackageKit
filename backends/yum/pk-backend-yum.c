/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#include <gio/gio.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>
#include <string.h>
#include <zif.h>

#define PREUPGRADE_BINARY			"/usr/bin/preupgrade"
#define YUM_REPOS_DIRECTORY			"/etc/yum.repos.d"
#define YUM_BACKEND_LOCKING_RETRIES		10
#define YUM_BACKEND_LOCKING_DELAY		2 /* seconds */

static gboolean use_zif = TRUE;

typedef struct {
	PkBackendSpawn	*spawn;
	GFileMonitor	*monitor;
	GCancellable	*cancellable;
	ZifDownload	*download;
	ZifConfig	*config;
	ZifStoreLocal	*store_local;
	ZifRepos	*repos;
	ZifGroups	*groups;
	ZifCompletion	*completion;
	ZifLock		*lock;
	GTimer		*timer;
} PkBackendYumPrivate;

static PkBackendYumPrivate *priv;

/**
 * backend_stderr_cb:
 */
static gboolean
backend_stderr_cb (PkBackend *backend, const gchar *output)
{
	/* unsigned rpm, this will be picked up by yum and and exception will be thrown */
	if (strstr (output, "NOKEY") != NULL)
		return FALSE;
	if (strstr (output, "GPG") != NULL)
		return FALSE;
	if (strstr (output, "DeprecationWarning") != NULL)
		return FALSE;
	return TRUE;
}

/**
 * backend_stdout_cb:
 */
static gboolean
backend_stdout_cb (PkBackend *backend, const gchar *output)
{
	return TRUE;
}

/**
 * backend_yum_repos_changed_cb:
 **/
static void
backend_yum_repos_changed_cb (GFileMonitor *monitor_, GFile *file, GFile *other_file, GFileMonitorEvent event_type, PkBackend *backend)
{
	pk_backend_repo_list_changed (backend);
}

static void
backend_completion_percentage_changed_cb (ZifCompletion *completion, guint percentage, PkBackend *backend)
{
	pk_backend_set_percentage (backend, percentage);
}

static void
backend_completion_subpercentage_changed_cb (ZifCompletion *completion, guint subpercentage, PkBackend *backend)
{
	pk_backend_set_sub_percentage (backend, subpercentage);
}

/**
 * backend_profile:
 */
static void
backend_profile (const gchar *title)
{
	gdouble elapsed;

	/* just reset?  */
	if (title == NULL)
		goto out;
	elapsed = g_timer_elapsed (priv->timer, NULL);
	g_print ("PROFILE: %ims\t%s\n", (guint) (elapsed * 1000.0f), title);
out:
	g_timer_reset (priv->timer);
}

/**
 * backend_setup_network:
 */
static void
backend_setup_network (PkBackend *backend)
{
	gboolean ret;
	gchar *http_proxy = NULL;

	/* get network state */
	ret = pk_backend_is_online (backend);
	if (!ret) {
		zif_config_set_local (priv->config, "network", "false", NULL);
		goto out;
	}

	/* tell ZifConfig it's okay to contact the network */
	zif_config_set_local (priv->config, "network", "true", NULL);

	/* set the proxy */
	http_proxy = pk_backend_get_proxy_http (backend);
	zif_download_set_proxy (priv->download, http_proxy, NULL);
out:
	g_free (http_proxy);
}

/**
 * backend_get_lock:
 */
static gboolean
backend_get_lock (PkBackend *backend)
{
	guint i;
	guint pid;
	gboolean ret = FALSE;
	GError *error = NULL;

	for (i=0; i<YUM_BACKEND_LOCKING_RETRIES; i++) {

		/* try to lock */
		ret = zif_lock_set_locked (priv->lock, &pid, &error);
		if (ret)
			break;

		/* we're now waiting */
		pk_backend_set_status (backend, PK_STATUS_ENUM_WAITING_FOR_LOCK);

		/* now wait */
		egg_debug ("Failed to lock on try %i of %i, already locked by PID %i (sleeping for %i seconds): %s\n",
			   i+1, YUM_BACKEND_LOCKING_RETRIES, pid, YUM_BACKEND_LOCKING_DELAY, error->message);
		g_clear_error (&error);
		g_usleep (YUM_BACKEND_LOCKING_DELAY * G_USEC_PER_SEC);
	}

	/* we failed */
	if (!ret)
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_GET_LOCK, "failed to get lock, held by PID: %i", pid);

	return ret;
}

/**
 * backend_is_all_installed:
 */
static gboolean
backend_is_all_installed (gchar **package_ids)
{
	guint i;
	gboolean ret = TRUE;

	/* check if we can use zif */
	for (i=0; package_ids[i] != NULL; i++) {
		if (!g_str_has_suffix (package_ids[i], ";installed")) {
			ret = FALSE;
			break;
		}
	}
	return ret;
}

/**
 * backend_unlock:
 */
static gboolean
backend_unlock (PkBackend *backend)
{
	gboolean ret;
	GError *error = NULL;

	/* try to unlock */
	ret = zif_lock_set_unlocked (priv->lock, &error);
	if (!ret) {
		egg_warning ("failed to unlock: %s", error->message);
		g_error_free (error);
	}
	return ret;
}


/**
 * backend_add_package_array:
 **/
static gboolean
backend_add_package_array (GPtrArray *array, GPtrArray *add)
{
	guint i;
	ZifPackage *package;

	for (i=0;i<add->len;i++) {
		package = g_ptr_array_index (add, i);
		g_ptr_array_add (array, g_object_ref (package));
	}
	return TRUE;
}

/**
 * backend_filter_package_array_newest:
 *
 * This function needs to scale well, and be fast to process 50,000 packages in
 * less than one second. If it looks overcomplicated, it's because it needs to
 * be O(n) not O(n*n).
 **/
static gboolean
backend_filter_package_array_newest (GPtrArray *array)
{
	gchar **split;
	const gchar *package_id;
	gboolean installed;
	gchar *key;
	GHashTable *hash;
	gint retval;
	guint i;
	ZifPackage *found;
	ZifPackage *package;

	/* as an indexed hash table for speed */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	for (i=0; i<array->len; i++) {

		/* get the current package */
		package = g_ptr_array_index (array, i);
		package_id = zif_package_get_id (package);
		installed = zif_package_is_installed (package);

		/* generate enough data to be specific */
		split = pk_package_id_split (package_id);
		key = g_strdup_printf ("%s-%s-%i", split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_ARCH], installed);
		g_strfreev (split);

		/* we've not already come across this package */
		found = g_hash_table_lookup (hash, key);
		if (found == NULL) {
			g_hash_table_insert (hash, key, g_object_ref (package));
			continue;
		}

		/* compare one package vs the other package */
		retval = zif_package_compare (package, found);

		/* the package is older than the one we have stored */
		if (retval <= 0) {
			g_free (key);
			g_object_unref (package);
			g_ptr_array_remove (array, package);
			continue;
		}

		/* the package is newer than what we have stored, delete the old store, and add this one */
		g_hash_table_remove (hash, found);
		g_hash_table_insert (hash, key, g_object_ref (package));
	}

	g_hash_table_unref (hash);
	return TRUE;
}

/**
 * backend_filter_package_array:
 **/
static GPtrArray *
backend_filter_package_array (GPtrArray *array, PkBitfield filters)
{
	guint i;
	ZifPackage *package;
	GPtrArray *result = NULL;

	result = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* pre-result */
	for (i=0;i<array->len;i++) {
		package = g_ptr_array_index (array, i);

		/* installed */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			if (!zif_package_is_installed (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			if (zif_package_is_installed (package))
				continue;
		}

		/* development */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
			if (!zif_package_is_devel (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			if (zif_package_is_devel (package))
				continue;
		}

		/* gui */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
			if (!zif_package_is_gui (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
			if (zif_package_is_gui (package))
				continue;
		}

		/* free */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE)) {
			if (!zif_package_is_free (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE)) {
			if (zif_package_is_free (package))
				continue;
		}

		/* arch */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_ARCH)) {
			if (!zif_package_is_native (package))
				continue;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_ARCH)) {
			if (zif_package_is_native (package))
				continue;
		}

		/* add to array so we can post process */
		g_ptr_array_add (result, g_object_ref (package));
	}

	/* do newest filtering */
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NEWEST))
		backend_filter_package_array_newest (result);

	return result;
}

/**
 * backend_emit_package_array:
 **/
static gboolean
backend_emit_package_array (PkBackend *backend, GPtrArray *array)
{
	guint i;
	gboolean installed;
	PkInfoEnum info;
	const gchar *package_id;
	ZifString *summary;
	ZifPackage *package;

	g_return_val_if_fail (array != NULL, FALSE);

	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		installed = zif_package_is_installed (package);
		package_id = zif_package_get_package_id (package);
		summary = zif_package_get_summary (package, NULL);
		info = installed ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
//		/* hack until we have update details */
//		if (strstr (package_id, "update") != NULL)
//			info = PK_INFO_ENUM_NORMAL;
		pk_backend_package (backend, info, package_id, zif_string_get_value (summary));
		zif_string_unref (summary);
	}
	return TRUE;
}

/**
 * backend_search_thread_get_array:
 */
static GPtrArray *
backend_search_thread_get_array (PkBackend *backend, GPtrArray *store_array, const gchar *search, ZifCompletion *completion, GError **error)
{
	PkRoleEnum role;
	GPtrArray *array = NULL;

	role = pk_backend_get_role (backend);
	if (role == PK_ROLE_ENUM_SEARCH_NAME)
		array = zif_store_array_search_name (store_array, search, priv->cancellable, completion, error);
	else if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
		array = zif_store_array_search_details (store_array, search, priv->cancellable, completion, error);
	else if (role == PK_ROLE_ENUM_SEARCH_GROUP)
		array = zif_store_array_search_category (store_array, search, priv->cancellable, completion, error);
	else if (role == PK_ROLE_ENUM_SEARCH_FILE)
		array = zif_store_array_search_file (store_array, search, priv->cancellable, completion, error);
	else if (role == PK_ROLE_ENUM_RESOLVE)
		array = zif_store_array_resolve (store_array, search, priv->cancellable, completion, error);
	else if (role == PK_ROLE_ENUM_WHAT_PROVIDES)
		array = zif_store_array_what_provides (store_array, search, priv->cancellable, completion, error);
	else
		g_set_error (error, 1, 0, "does not support: %s", pk_role_enum_to_string (role));
	return array;
}

/**
 * backend_get_default_store_array_for_filter:
 */
static GPtrArray *
backend_get_default_store_array_for_filter (PkBackend *backend, PkBitfield filters, ZifCompletion *completion, GError **error)
{
	GPtrArray *store_array;
	ZifStore *store;
	GPtrArray *array;
	GError *error_local = NULL;

	store_array = zif_store_array_new ();

	/* add local packages to the store_array */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		store = ZIF_STORE (zif_store_local_new ());
		zif_store_array_add_store (store_array, store);
		g_object_unref (store);
	}

	/* add remote packages to the store_array */
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		array = zif_repos_get_stores_enabled (priv->repos, priv->cancellable, completion, &error_local);
		if (array == NULL) {
			g_set_error (error, 1, 0, "failed to get enabled stores: %s", error_local->message);
			g_error_free (error_local);
			g_ptr_array_unref (store_array);
			store_array = NULL;
			goto out;
		}
		zif_store_array_add_stores (store_array, array);
		g_ptr_array_unref (array);
	}
out:
	return store_array;
}

/**
 * backend_search_thread:
 */
static gboolean
backend_search_thread (PkBackend *backend)
{
	gboolean ret;
	GPtrArray *store_array = NULL;
	GPtrArray *array = NULL;
	GPtrArray *result;
	PkBitfield filters;
	PkRoleEnum role;
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	GError *error = NULL;
	gchar **search;
	const gchar *search_tmp;
	guint i;
	filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	role = pk_backend_get_role (backend);

	/* get lock */
	ret = backend_get_lock (backend);
	if (!ret) {
		egg_warning ("failed to get lock");
		goto out;
	}

	/* set the network state */
	backend_setup_network (backend);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup completion */
	zif_completion_reset (priv->completion);
	zif_completion_set_number_steps (priv->completion, 3);

	/* get default store_array */
	completion_local = zif_completion_get_child (priv->completion);
	store_array = backend_get_default_store_array_for_filter (backend, filters, completion_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (priv->completion);

	/* do get action */
	if (role == PK_ROLE_ENUM_GET_PACKAGES) {
		completion_local = zif_completion_get_child (priv->completion);
		array = zif_store_array_get_packages (store_array, priv->cancellable, completion_local, &error);
		if (array == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get packages: %s", error->message);
			g_error_free (error);
			goto out;
		}
	} else {
		/* treat these all the same */
		search = pk_backend_get_strv (backend, "search");
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

		completion_local = zif_completion_get_child (priv->completion);
		zif_completion_set_number_steps (completion_local, g_strv_length (search));

		/* do OR search */
		for (i=0; search[i] != NULL; i++) {
			/* make loop deeper */
			completion_loop = zif_completion_get_child (completion_local);

			/* strip off the prefix '@' */
			search_tmp = search[i];
			if (g_str_has_prefix (search_tmp, "@"))
				search_tmp = search_tmp+1;

			/* get the results */
			egg_debug ("searching for: %s", search_tmp);
			result = backend_search_thread_get_array (backend, store_array, search_tmp, completion_loop, &error);
			if (result == NULL) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to search: %s", error->message);
				g_error_free (error);
				goto out;
			}

			/* this section done */
			zif_completion_done (completion_local);

			backend_add_package_array (array, result);
			g_ptr_array_unref (result);
		}
	}

	/* this section done */
	zif_completion_done (priv->completion);

	/* filter */
	result = backend_filter_package_array (array, filters);

	/* this section done */
	zif_completion_done (priv->completion);

	/* done */
	pk_backend_set_percentage (backend, 100);

	/* emit */
	backend_emit_package_array (backend, result);
out:
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	if (array != NULL)
		g_ptr_array_unref (array);
	backend_unlock (backend);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	gboolean ret;
	GFile *file;
	GError *error = NULL;

	/* create private area */
	priv = g_new0 (PkBackendYumPrivate, 1);

	egg_debug ("backend: initialize");
	priv->spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_filter_stderr (priv->spawn, backend_stderr_cb);
	pk_backend_spawn_set_filter_stdout (priv->spawn, backend_stdout_cb);
	pk_backend_spawn_set_name (priv->spawn, "yum");
	pk_backend_spawn_set_allow_sigkill (priv->spawn, FALSE);

	/* setup a file monitor on the repos directory */
	file = g_file_new_for_path (YUM_REPOS_DIRECTORY);
	priv->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, &error);
	if (priv->monitor != NULL) {
		g_signal_connect (priv->monitor, "changed", G_CALLBACK (backend_yum_repos_changed_cb), backend);
	} else {
		egg_warning ("failed to setup monitor: %s", error->message);
		g_error_free (error);
	}

	/* it seems some people are not ready for the awesomeness */
	if (!use_zif)
		goto out;

	/* use a timer for profiling */
	priv->timer = g_timer_new ();

	/* init rpm */
	zif_init ();

	/* profile */
	backend_profile ("zif init");

	/* TODO: hook up errors */
	priv->cancellable = g_cancellable_new ();

	/* ZifCompletion */
	priv->completion = zif_completion_new ();
	g_signal_connect (priv->completion, "percentage-changed", G_CALLBACK (backend_completion_percentage_changed_cb), backend);
	g_signal_connect (priv->completion, "subpercentage-changed", G_CALLBACK (backend_completion_subpercentage_changed_cb), backend);

	/* ZifConfig */
	priv->config = zif_config_new ();
	ret = zif_config_set_filename (priv->config, "/etc/yum.conf", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING, "failed to set config: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	backend_profile ("read config_file");

	/* ZifDownload */
	priv->download = zif_download_new ();

	/* ZifLock */
	priv->lock = zif_lock_new ();

	/* ZifStoreLocal */
	priv->store_local = zif_store_local_new ();
	ret = zif_store_local_set_prefix (priv->store_local, "/", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to set prefix: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	backend_profile ("read local store");

	/* ZifRepos */
	priv->repos = zif_repos_new ();
	ret = zif_repos_set_repos_dir (priv->repos, "/etc/yum.repos.d", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "failed to set repos dir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	backend_profile ("read repos");

	/* ZifGroups */
	priv->groups = zif_groups_new ();
	ret = zif_groups_set_mapping_file (priv->groups, "/usr/share/PackageKit/helpers/yum/yum-comps-groups.conf", &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_LIST_INVALID, "failed to set mapping file: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	backend_profile ("read groups");
out:
	g_object_unref (file);
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	egg_debug ("backend: destroy");
	g_object_unref (priv->spawn);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
	if (priv->config != NULL)
		g_object_unref (priv->config);
	if (priv->download != NULL)
		g_object_unref (priv->download);
	if (priv->completion != NULL)
		g_object_unref (priv->completion);
	if (priv->repos != NULL)
		g_object_unref (priv->repos);
	if (priv->groups != NULL)
		g_object_unref (priv->groups);
	if (priv->store_local != NULL)
		g_object_unref (priv->store_local);
	if (priv->lock != NULL)
		g_object_unref (priv->lock);
	if (priv->timer != NULL)
		g_timer_destroy (priv->timer);
	g_free (priv);
}

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	GError *error = NULL;
	PkBitfield groups;

	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		groups = pk_bitfield_from_enums (
			PK_GROUP_ENUM_COLLECTIONS,
			PK_GROUP_ENUM_NEWEST,
			PK_GROUP_ENUM_ADMIN_TOOLS,
			PK_GROUP_ENUM_DESKTOP_GNOME,
			PK_GROUP_ENUM_DESKTOP_KDE,
			PK_GROUP_ENUM_DESKTOP_XFCE,
			PK_GROUP_ENUM_DESKTOP_OTHER,
			PK_GROUP_ENUM_EDUCATION,
			PK_GROUP_ENUM_FONTS,
			PK_GROUP_ENUM_GAMES,
			PK_GROUP_ENUM_GRAPHICS,
			PK_GROUP_ENUM_INTERNET,
			PK_GROUP_ENUM_LEGACY,
			PK_GROUP_ENUM_LOCALIZATION,
			PK_GROUP_ENUM_MULTIMEDIA,
			PK_GROUP_ENUM_OFFICE,
			PK_GROUP_ENUM_OTHER,
			PK_GROUP_ENUM_PROGRAMMING,
			PK_GROUP_ENUM_PUBLISHING,
			PK_GROUP_ENUM_SERVERS,
			PK_GROUP_ENUM_SYSTEM,
			PK_GROUP_ENUM_VIRTUALIZATION,
			-1);
		goto out;
	}

	/* get the dynamic group list */
	groups = zif_groups_get_groups (priv->groups, &error);
	if (groups == 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_LIST_INVALID, "failed to get the list of groups: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* add the virtual groups */
	pk_bitfield_add (groups, PK_GROUP_ENUM_COLLECTIONS);
	pk_bitfield_add (groups, PK_GROUP_ENUM_NEWEST);
out:
	return groups;
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_BASENAME,
		PK_FILTER_ENUM_FREE,
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_ARCH,
		-1);
}

/**
 * backend_get_roles:
 */
static PkBitfield
backend_get_roles (PkBackend *backend)
{
	PkBitfield roles;
	roles = pk_bitfield_from_enums (
		PK_ROLE_ENUM_CANCEL,
		PK_ROLE_ENUM_GET_DEPENDS,
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_FILES,
		PK_ROLE_ENUM_GET_REQUIRES,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_WHAT_PROVIDES,
		PK_ROLE_ENUM_GET_UPDATES,
		PK_ROLE_ENUM_GET_UPDATE_DETAIL,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_INSTALL_FILES,
		PK_ROLE_ENUM_INSTALL_SIGNATURE,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
		PK_ROLE_ENUM_RESOLVE,
		PK_ROLE_ENUM_SEARCH_DETAILS,
		PK_ROLE_ENUM_SEARCH_FILE,
		PK_ROLE_ENUM_SEARCH_GROUP,
		PK_ROLE_ENUM_SEARCH_NAME,
		PK_ROLE_ENUM_UPDATE_PACKAGES,
		PK_ROLE_ENUM_UPDATE_SYSTEM,
		PK_ROLE_ENUM_GET_REPO_LIST,
		PK_ROLE_ENUM_REPO_ENABLE,
		PK_ROLE_ENUM_REPO_SET_DATA,
		PK_ROLE_ENUM_GET_CATEGORIES,
		PK_ROLE_ENUM_SIMULATE_INSTALL_FILES,
		PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES,
		PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES,
		PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES,
		-1);

	/* only add GetDistroUpgrades if the binary is present */
	if (g_file_test (PREUPGRADE_BINARY, G_FILE_TEST_EXISTS))
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);

	return roles;
}

/**
 * backend_get_mime_types:
 */
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm;application/x-servicepack");
}

/**
 * pk_backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	/* try to cancel the thread first */
	g_cancellable_cancel (priv->cancellable);

	/* this feels bad... */
	pk_backend_spawn_kill (priv->spawn);
}

/**
 * backend_download_packages:
 */
static void
backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "download-packages", directory, package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-depends", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * backend_get_details_thread:
 */
static gboolean
backend_get_details_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GPtrArray *store_array = NULL;
	ZifPackage *package;
	ZifCompletion *completion_local;
	const gchar *id;
	guint i;
	guint len;
	GError *error = NULL;
	ZifString *license;
	ZifString *description;
	ZifString *url;
	PkGroupEnum group;
	guint64 size;
	PkBitfield filters = PK_FILTER_ENUM_UNKNOWN;

	/* get lock */
	ret = backend_get_lock (backend);
	if (!ret) {
		egg_warning ("failed to get lock");
		goto out;
	}

	/* set the network state */
	backend_setup_network (backend);

	len = g_strv_length (package_ids);

	/* setup completion */
	zif_completion_reset (priv->completion);
	zif_completion_set_number_steps (priv->completion, len + 1);

	/* find all the packages */
	completion_local = zif_completion_get_child (priv->completion);
	if (backend_is_all_installed (package_ids))
		pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	store_array = backend_get_default_store_array_for_filter (backend, filters, completion_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (priv->completion);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];
		completion_local = zif_completion_get_child (priv->completion);
		package = zif_store_array_find_package (store_array, id, priv->cancellable, completion_local, &error);
		if (package == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "failed to find %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* get data */
		license = zif_package_get_license (package, NULL);
		group = zif_package_get_group (package, NULL);
		description = zif_package_get_description (package, NULL);
		url = zif_package_get_url (package, NULL);
		size = zif_package_get_size (package, NULL);

		/* emit */
		pk_backend_details (backend,
				    package_ids[i],
				    zif_string_get_value (license),
				    group,
				    zif_string_get_value (description),
				    zif_string_get_value (url),
				    (gulong) size);

		/* this section done */
		zif_completion_done (priv->completion);

		/* free */
		zif_string_unref (license);
		zif_string_unref (description);
		zif_string_unref (url);
		g_object_unref (package);
	}
out:
	backend_unlock (backend);
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	/* check if we can use zif */
	if (!use_zif) {
		gchar *package_ids_temp;
		package_ids_temp = pk_package_ids_to_string (package_ids);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-details", package_ids_temp, NULL);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_thread_create (backend, backend_get_details_thread);
}

/**
  * backend_get_distro_upgrades_thread:
  */
static gboolean
backend_get_distro_upgrades_thread (PkBackend *backend)
{
	gboolean ret;
	gchar *distro_id = NULL;
	gchar *filename = NULL;
	gchar **groups = NULL;
	gchar *name = NULL;
	gchar *proxy = NULL;
	gchar **split = NULL;
	guint i;
	guint last_version = 0;
	guint newest = G_MAXUINT;
	guint version;
	GError *error = NULL;
	GKeyFile *file = NULL;
	ZifCompletion *child;

	/* download, then parse */
	zif_completion_reset (priv->completion);
	zif_completion_set_number_steps (priv->completion, 2);

	/* set proxy */
	proxy = pk_backend_get_proxy_http (backend);
	ret = zif_download_set_proxy (priv->download, proxy, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "failed to set proxy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* download new file */
	filename = g_build_filename ("/var/cache/PackageKit", "releases.txt", NULL);
	child = zif_completion_get_child (priv->completion);
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO);
	ret = zif_download_file (priv->download, "http://mirrors.fedoraproject.org/releases.txt", filename, NULL, child, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "failed to download %s: %s", filename, error->message);
		g_error_free (error);
		goto out;
	}
	zif_completion_done (priv->completion);

	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "failed to open %s: %s", filename, error->message);
		g_error_free (error);
		goto out;
	}

	/* get all entries */
	groups = g_key_file_get_groups (file, NULL);
	for (i=0; groups[i] != NULL; i++) {
		/* we only care about stable versions */
		if (!g_key_file_get_boolean (file, groups[i], "stable", NULL))
			goto out;
		version = g_key_file_get_integer (file, groups[i], "version", NULL);
		egg_debug ("%s is update to version %i", groups[i], version);
		if (version > last_version) {
			newest = i;
			last_version = version;
		}
	}

	/* nothing found */
	if (newest == G_MAXUINT) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING, "could not get latest distro data");
		goto out;
	}

	/* are we already on the latest version */
	version = zif_config_get_uint (priv->config, "releasever", &error);
	if (version == G_MAXUINT) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_FAILED_CONFIG_PARSING, "could not get distro present version");
		goto out;
	}

	/* all okay, nothing to show */
	if (version >= last_version)
		goto out;

	/* if we have an upgrade candidate then pass back data to daemon */
	split = g_strsplit (groups[newest], " ", -1);
	name = g_ascii_strdown (split[0], -1);
	distro_id = g_strdup_printf ("%s-%s", name, split[1]);
	pk_backend_distro_upgrade (backend, PK_DISTRO_UPGRADE_ENUM_STABLE, distro_id, groups[newest]);

	/* we're done */
	zif_completion_done (priv->completion);
out:
	pk_backend_finished (backend);
	g_free (distro_id);
	g_free (filename);
	g_free (name);
	g_free (proxy);
	if (file != NULL)
		g_key_file_free (file);
	g_strfreev (groups);
	g_strfreev (split);
	return TRUE;
}

/**
 * backend_get_distro_upgrades:
 */
static void
backend_get_distro_upgrades (PkBackend *backend)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-distro-upgrades", NULL);
		return;
	}

	pk_backend_thread_create (backend, backend_get_distro_upgrades_thread);
}

/**
 * backend_get_files_thread:
 */
static gboolean
backend_get_files_thread (PkBackend *backend)
{
	gboolean ret;
	gchar **package_ids = pk_backend_get_strv (backend, "package_ids");
	GPtrArray *store_array = NULL;
	ZifPackage *package;
	GPtrArray *files;
	ZifCompletion *completion_local;
	const gchar *id;
	guint i, j;
	guint len;
	GError *error = NULL;
	const gchar *file;
	GString *files_str;
	PkBitfield filters = PK_FILTER_ENUM_UNKNOWN;

	/* reset */
	backend_profile (NULL);

	/* get lock */
	ret = backend_get_lock (backend);
	if (!ret) {
		egg_warning ("failed to get lock");
		goto out;
	}

	/* set the network state */
	backend_setup_network (backend);

	/* profile */
	backend_profile ("get lock");

	len = g_strv_length (package_ids);

	/* setup completion */
	zif_completion_reset (priv->completion);
	zif_completion_set_number_steps (priv->completion, len + 1);

	/* find all the packages */
	completion_local = zif_completion_get_child (priv->completion);
	if (backend_is_all_installed (package_ids))
		pk_bitfield_add (filters, PK_FILTER_ENUM_INSTALLED);
	store_array = backend_get_default_store_array_for_filter (backend, filters, completion_local, &error);
	if (store_array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "failed to get stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* profile */
	backend_profile ("add local");

	/* this section done */
	zif_completion_done (priv->completion);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	for (i=0; package_ids[i] != NULL; i++) {
		id = package_ids[i];
		completion_local = zif_completion_get_child (priv->completion);
		package = zif_store_array_find_package (store_array, id, priv->cancellable, completion_local, &error);
		if (package == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, "failed to find %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* profile */
		backend_profile ("find package");

		files = zif_package_get_files (package, &error);
		if (files == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "no files for %s: %s", package_ids[i], error->message);
			g_error_free (error);
			goto out;
		}

		/* profile */
		backend_profile ("get files");

		files_str = g_string_new ("");
		for (j=0; j<files->len; j++) {
			file = g_ptr_array_index (files, j);
			g_string_append_printf (files_str, "%s\n", file);
		}
		pk_backend_files (backend, package_ids[i], files_str->str);

		/* profile */
		backend_profile ("emit files");

		/* this section done */
		zif_completion_done (priv->completion);

		g_string_free (files_str, TRUE);
		g_object_unref (package);
	}
out:
	backend_unlock (backend);
	pk_backend_finished (backend);
	if (store_array != NULL)
		g_ptr_array_unref (store_array);
	return TRUE;
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	gboolean ret;

	/* check if we can use zif */
	ret = backend_is_all_installed (package_ids);
	if (ret && use_zif) {
		pk_backend_thread_create (backend, backend_get_files_thread);
		return;
	}

	/* fall back to spawning */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn,  "yumBackend.py", "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-requires", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn,  "yumBackend.py", "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * backend_get_packages:
 */
static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		filters_text = pk_filter_bitfield_to_string (filters);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-packages", filters_text, NULL);
		g_free (filters_text);
		return;
	}
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_packages:
 */
static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "install-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_remove_packages:
 */
static void
backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-remove-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_update_packages:
 */
static void
backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-update-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_install_packages:
 */
static void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-install-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_files:
 */
static void
backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "install-files", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_signature:
 */
static void
backend_install_signature (PkBackend *backend, PkSigTypeEnum type,
			   const gchar *key_id, const gchar *package_id)
{
	const gchar *type_text;

	type_text = pk_sig_type_enum_to_string (type);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "install-signature", type_text, key_id, package_id, NULL);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

/**
 * pk_backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "remove-packages", pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string (autoremove), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-details", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * pk_backend_search_files:
 */
static void
backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-file", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * pk_backend_search_groups:
 */
static void
backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-group", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * pk_backend_search_names:
 */
static void
backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		gchar *search;
		filters_text = pk_filter_bitfield_to_string (filters);
		search = g_strjoinv ("&", values);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "search-name", filters_text, search, NULL);
		g_free (filters_text);
		g_free (search);
		return;
	}
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * pk_backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "update-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "update-system", pk_backend_bool_to_string (only_trusted), NULL);
}

/**
 * pk_backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		gchar *package_ids_temp;
		filters_text = pk_filter_bitfield_to_string (filters);
		package_ids_temp = pk_package_ids_to_string (packages);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "resolve", filters_text, package_ids_temp, NULL);
		g_free (filters_text);
		g_free (package_ids_temp);
		return;
	}
	pk_backend_set_strv (backend, "search", packages);
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_get_repo_list_thread:
 */
static gboolean
backend_get_repo_list_thread (PkBackend *backend)
{
	gboolean ret;
	PkBitfield filters = (PkBitfield) pk_backend_get_uint (backend, "filters");
	guint i;
	GPtrArray *array = NULL;
	ZifStoreRemote *store;
	ZifCompletion *completion_local;
	const gchar *repo_id;
	const gchar *name;
	gboolean enabled;
	gboolean devel;
	GError *error = NULL;

	/* get lock */
	ret = backend_get_lock (backend);
	if (!ret) {
		egg_warning ("failed to get lock");
		goto out;
	}

	/* set the network state */
	backend_setup_network (backend);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* setup completion */
	zif_completion_reset (priv->completion);
	zif_completion_set_number_steps (priv->completion, 2);

	completion_local = zif_completion_get_child (priv->completion);
	array = zif_repos_get_stores (priv->repos, priv->cancellable, completion_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "failed to find repos: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* none? */
	if (array->len == 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "failed to find any repos");
		goto out;
	}

	/* this section done */
	zif_completion_done (priv->completion);

	/* setup completion */
	completion_local = zif_completion_get_child (priv->completion);
	zif_completion_set_number_steps (completion_local, array->len);

	/* looks at each store */
	for (i=0; i<array->len; i++) {
		store = g_ptr_array_index (array, i);
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			/* TODO: completion */
			devel = zif_store_remote_is_devel (store, priv->cancellable, NULL, NULL);
			if (devel)
				continue;
		}
		repo_id = zif_store_get_id (ZIF_STORE (store));
		/* TODO: completion */
		name = zif_store_remote_get_name (store, priv->cancellable, NULL, NULL);
		/* TODO: completion */
		enabled = zif_store_remote_get_enabled (store, priv->cancellable, NULL, NULL);
		pk_backend_repo_detail (backend, repo_id, name, enabled);

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (priv->completion);
out:
	backend_unlock (backend);
	pk_backend_finished (backend);
	if (array != NULL)
		g_ptr_array_unref (array);
	return TRUE;
}

/**
 * pk_backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		gchar *filters_text;
		filters_text = pk_filter_bitfield_to_string (filters);
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-repo-list", filters_text, NULL);
		g_free (filters_text);
		return;
	}

	pk_backend_thread_create (backend, backend_get_repo_list_thread);
}

/**
 * backend_repo_enable_thread:
 */
static gboolean
backend_repo_enable_thread (PkBackend *backend)
{
	ZifStoreRemote *repo = NULL;
	gboolean ret;
	GError *error = NULL;
	gchar *warning = NULL;
	gboolean enabled = pk_backend_get_bool (backend, "enabled");
	const gchar *repo_id = pk_backend_get_string (backend, "repo_id");

	/* get lock */
	ret = backend_get_lock (backend);
	if (!ret) {
		egg_warning ("failed to get lock");
		goto out;
	}

	/* set the network state */
	backend_setup_network (backend);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* find the right repo */
	repo = zif_repos_get_store (priv->repos, repo_id, priv->cancellable, priv->completion, &error);
	if (repo == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_NOT_FOUND, "failed to find repo: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set the state */
	ret = zif_store_remote_set_enabled (repo, enabled, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_DISABLE_REPOSITORY, "failed to set enable: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* warn if rawhide */
	if (g_strstr_len (repo_id, -1, "rawhide") != NULL) {
		warning = g_strdup_printf ("These packages are untested and still under development."
					   "This repository is used for development of new releases.\n\n"
					   "This repository can see significant daily turnover and major "
					   "functionality changes which cause unexpected problems with "
					   "other development packages.\n"
					   "Please use these packages if you want to work with the "
					   "Fedora developers by testing these new development packages.\n\n"
					   "If this is not correct, please disable the %s software source.", repo_id);
		pk_backend_message (backend, PK_MESSAGE_ENUM_REPO_FOR_DEVELOPERS_ONLY, warning);
	}
out:
	backend_unlock (backend);
	pk_backend_finished (backend);
	g_free (warning);
	if (repo != NULL)
		g_object_unref (repo);
	return TRUE;
}

/**
 * pk_backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *repo_id, gboolean enabled)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		if (enabled == TRUE) {
			pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "repo-enable", repo_id, "true", NULL);
		} else {
			pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "repo-enable", repo_id, "false", NULL);
		}
		return;
	}
	pk_backend_thread_create (backend, backend_repo_enable_thread);
}

/**
 * pk_backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	/* no operation */
	pk_backend_finished (backend);
}

/**
 * backend_what_provides:
 */
static void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	const gchar *provides_text;
	provides_text = pk_provides_enum_to_string (provides);
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "what-provides", filters_text, provides_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

#define PK_ROLE_ENUM_SEARCH_CATEGORY	(PK_ROLE_ENUM_UNKNOWN + 1)

/**
 * backend_repos_search:
 **/
static GPtrArray *
backend_repos_search (PkBackend *backend, GPtrArray *stores, PkRoleEnum role, const gchar *search, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array = NULL;
	GPtrArray *part;
	ZifStore *store;
	ZifPackage *package;
	GError *error_local = NULL;
	ZifCompletion *completion_local = NULL;

	/* nothing to do */
	if (stores->len == 0) {
		if (error != NULL)
			*error = g_error_new (1, 0, "nothing to do as no stores");
		goto out;
	}

	/* set number of stores */
	zif_completion_set_number_steps (completion, stores->len);

	/* do each one */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<stores->len; i++) {
		store = g_ptr_array_index (stores, i);

		/* create a chain of completions */
		completion_local = zif_completion_get_child (completion);

		/* get results for this store */
		if (role == PK_ROLE_ENUM_RESOLVE)
			part = zif_store_resolve (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_NAME)
			part = zif_store_search_name (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_DETAILS)
			part = zif_store_search_details (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_GROUP)
			part = zif_store_search_group (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_CATEGORY)
			part = zif_store_search_category (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_SEARCH_FILE)
			part = zif_store_search_file (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_GET_PACKAGES)
			part = zif_store_get_packages (store, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_GET_UPDATES)
			part = zif_store_get_updates (store, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_WHAT_PROVIDES)
			part = zif_store_what_provides (store, search, priv->cancellable, completion_local, &error_local);
		else if (role == PK_ROLE_ENUM_GET_CATEGORIES)
			part = zif_store_get_categories (store, priv->cancellable, completion_local, &error_local);
		else
			egg_error ("internal error: %s", pk_role_enum_to_text (role));
		if (part == NULL) {
			/* emit a warning, this isn't fatal */
			pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "failed to %s for repo %s: %s",
					    pk_role_enum_to_text (role),
					    zif_store_get_id (store),
					    error_local->message);
			g_clear_error (&error_local);
			zif_completion_done (completion);
			continue;
		}

		for (j=0; j<part->len; j++) {
			package = g_ptr_array_index (part, j);
			g_ptr_array_add (array, g_object_ref (package));
		}
		g_ptr_array_unref (part);

		/* this section done */
		zif_completion_done (completion);
	}
out:
	return array;
}

/**
 * backend_get_unique_categories:
 **/
static GPtrArray *
backend_get_unique_categories (PkBackend *backend, GPtrArray *stores, ZifCompletion *completion, GError **error)
{
	guint i, j;
	GPtrArray *array;
	PkCategory *obj;
	PkCategory *obj_tmp;
	gchar *parent_id;
	gchar *parent_id_tmp;
	gchar *cat_id;
	gchar *cat_id_tmp;

	/* get all results from all repos */
	array = backend_repos_search (backend, stores, PK_ROLE_ENUM_GET_CATEGORIES, NULL, completion, error);
	if (array == NULL)
		goto out;

	/* remove duplicate parents and groups */
	for (i=0; i<array->len; i++) {
		obj = g_ptr_array_index (array, i);
		g_object_get (obj,
			      "parent-id", &parent_id,
			      "cat-id", &cat_id,
			      NULL);
		for (j=0; j<array->len; j++) {
			if (i == j)
				continue;
			obj_tmp = g_ptr_array_index (array, j);
			g_object_get (obj_tmp,
				      "parent-id", &parent_id_tmp,
				      "cat-id", &cat_id_tmp,
				      NULL);
			if (g_strcmp0 (parent_id_tmp, parent_id) == 0 &&
			    g_strcmp0 (cat_id_tmp, cat_id) == 0) {
				egg_warning ("duplicate %s-%s", parent_id, cat_id);
				g_ptr_array_remove_index (array, j);
			}
			g_free (parent_id_tmp);
			g_free (cat_id_tmp);
		}
		g_free (parent_id);
		g_free (cat_id);
	}
out:
	return array;
}

/**
 * backend_get_categories_thread:
 */
static gboolean
backend_get_categories_thread (PkBackend *backend)
{
	gboolean ret;
	guint i;
	GPtrArray *array = NULL;
	GPtrArray *stores = NULL;
	PkCategory *cat;
	gchar *cat_id;

	ZifCompletion *completion_local;
	GError *error = NULL;

	/* get lock */
	ret = backend_get_lock (backend);
	if (!ret) {
		egg_warning ("failed to get lock");
		goto out;
	}

	/* set the network state */
	backend_setup_network (backend);

	/* setup completion */
	zif_completion_reset (priv->completion);
	zif_completion_set_number_steps (priv->completion, 3);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* get enabled repos */
	completion_local = zif_completion_get_child (priv->completion);
	stores = zif_repos_get_stores_enabled (priv->repos, priv->cancellable, completion_local, &error);
	if (stores == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR, "failed to add remote stores: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (priv->completion);

	/* get sorted list of unique categories */
	completion_local = zif_completion_get_child (priv->completion);
	array = backend_get_unique_categories (backend, stores, completion_local, &error);
	if (array == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GROUP_LIST_INVALID, "failed to add get categories: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* this section done */
	zif_completion_done (priv->completion);

	/* emit each cat obj */
	for (i=0; i<array->len; i++) {
		cat = g_ptr_array_index (array, i);
		/* FIXME: in the yum backend, we signify a group with a '@' prefix */
		if (pk_category_get_parent_id (cat) != NULL)
			cat_id = g_strdup_printf ("@%s", pk_category_get_id (cat));
		else
			cat_id = g_strdup (pk_category_get_id (cat));
		pk_backend_category (backend,
				     pk_category_get_parent_id (cat),
				     cat_id,
				     pk_category_get_name (cat),
				     pk_category_get_summary (cat),
				     pk_category_get_icon (cat));
		g_free (cat_id);
	}

	/* this section done */
	zif_completion_done (priv->completion);
out:
	backend_unlock (backend);
	pk_backend_finished (backend);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (stores != NULL)
		g_ptr_array_unref (stores);
	return TRUE;
}

/**
 * pk_backend_get_categories:
 */
static void
backend_get_categories (PkBackend *backend)
{
	/* it seems some people are not ready for the awesomeness */
	if (!use_zif) {
		pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "get-categories", NULL);
		return;
	}
	pk_backend_thread_create (backend, backend_get_categories_thread);
}

/**
 * backend_simulate_install_files:
 */
static void
backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (priv->spawn, "yumBackend.py", "simulate-install-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

PK_BACKEND_OPTIONS (
	"YUM",					/* description */
	"Tim Lauridsen <timlau@fedoraproject.org>, Richard Hughes <richard@hughsie.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	backend_get_roles,			/* get_roles */
	backend_get_mime_types,			/* get_mime_types */
	backend_cancel,				/* cancel */
	backend_download_packages,		/* download_packages */
	backend_get_categories,			/* get_categories */
	backend_get_depends,			/* get_depends */
	backend_get_details,			/* get_details */
	backend_get_distro_upgrades,		/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	backend_get_packages,			/* get_packages */
	backend_get_repo_list,			/* get_repo_list */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_files,			/* install_files */
	backend_install_packages,		/* install_packages */
	backend_install_signature,		/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_files,			/* search_files */
	backend_search_groups,			/* search_groups */
	backend_search_names,			/* search_names */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	backend_what_provides,			/* what_provides */
	backend_simulate_install_files,		/* simulate_install_files */
	backend_simulate_install_packages,	/* simulate_install_packages */
	backend_simulate_remove_packages,	/* simulate_remove_packages */
	backend_simulate_update_packages	/* simulate_update_packages */
);

