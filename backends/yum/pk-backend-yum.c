/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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
#include <pk-backend.h>
#include <pk-backend-spawn.h>
#include <string.h>
#include <packagekit-glib2/pk-debug.h>

#define PREUPGRADE_BINARY			"/usr/bin/preupgrade"
#define YUM_REPOS_DIRECTORY			"/etc/yum.repos.d"
#define YUM_BACKEND_LOCKING_RETRIES		10
#define YUM_BACKEND_LOCKING_DELAY		2 /* seconds */
#define PACKAGE_MEDIA_REPO_FILENAME		"/etc/yum.repos.d/packagekit-media.repo"

typedef struct {
	PkBackendSpawn	*spawn;
	GFileMonitor	*monitor;
	GCancellable	*cancellable;
	GTimer		*timer;
	GVolumeMonitor	*volume_monitor;
} PkBackendYumPrivate;

static PkBackendYumPrivate *priv;

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "YUM";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Richard Hughes <richard@hughsie.com>";
}

/**
 * pk_backend_stderr_cb:
 */
static gboolean
pk_backend_stderr_cb (PkBackendJob *job, const gchar *output)
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
 * pk_backend_stdout_cb:
 */
static gboolean
pk_backend_stdout_cb (PkBackendJob *job, const gchar *output)
{
	return TRUE;
}

/**
 * pk_backend_yum_repos_changed_cb:
 **/
static void
pk_backend_yum_repos_changed_cb (GFileMonitor *monitor_, GFile *file, GFile *other_file, GFileMonitorEvent event_type, PkBackend *backend)
{
	gchar *filename;

	/* ignore the packagekit-media.repo file */
	filename = g_file_get_path (file);
	if (g_str_has_prefix (filename, PACKAGE_MEDIA_REPO_FILENAME))
		goto out;

	/* emit signal */
	pk_backend_repo_list_changed (backend);
out:
	g_free (filename);
}

/**
 * pk_backend_enable_media_repo:
 */
static void
pk_backend_enable_media_repo (gboolean enabled)
{
	GKeyFile *keyfile;
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;

	/* load */
	keyfile = g_key_file_new ();
	ret = g_key_file_load_from_file (keyfile, PACKAGE_MEDIA_REPO_FILENAME,
					 G_KEY_FILE_KEEP_COMMENTS, &error);
	if (!ret) {
		g_debug ("failed to load %s: %s",
			 PACKAGE_MEDIA_REPO_FILENAME,
			 error->message);
		g_error_free (error);
		goto out;
	}

	/* set data */
	g_key_file_set_integer (keyfile, "InstallMedia", "enabled", enabled);
	data = g_key_file_to_data (keyfile, NULL, &error);
	if (data == NULL) {
		g_warning ("failed to get data: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save */
	ret = g_file_set_contents (PACKAGE_MEDIA_REPO_FILENAME, data, -1, &error);
	if (!ret) {
		g_warning ("failed to save %s", error->message);
		g_error_free (error);
		goto out;
	}
	g_debug ("%s InstallMedia", enabled ? "enabled" : "disabled");
out:
	g_free (data);
	g_key_file_free (keyfile);
}

/**
 * pk_backend_mount_add:
 */
static void
pk_backend_mount_add (GMount *mount, gpointer user_data)
{
	GFile *root;
	GFile *repo;
	GFile *dest;
	gchar *root_path;
	gchar *repo_path;
	gboolean ret;
	GError *error = NULL;

	/* check if any installed media is an install disk */
	root = g_mount_get_root (mount);
	root_path = g_file_get_path (root);
	repo_path = g_build_filename (root_path, "media.repo", NULL);
	repo = g_file_new_for_path (repo_path);
	dest = g_file_new_for_path (PACKAGE_MEDIA_REPO_FILENAME);

	/* media.repo exists */
	ret = g_file_query_exists (repo, NULL);
	g_debug ("checking for %s: %s", repo_path, ret ? "yes" : "no");
	if (!ret)
		goto out;

	/* copy to the system repo dir */
	ret = g_file_copy (repo, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
	if (!ret) {
		g_warning ("failed to copy: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (root_path);
	g_free (repo_path);
	g_object_unref (dest);
	g_object_unref (root);
	g_object_unref (repo);
}

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	gboolean ret;
	GFile *file = NULL;
	GError *error = NULL;
	GKeyFile *key_file = NULL;
	gchar *config_file = NULL;
	GList *mounts;

	/* use logging */
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	pk_debug_add_log_domain ("Yum");

	/* create private area */
	priv = g_new0 (PkBackendYumPrivate, 1);

	g_debug ("backend: initialize");
	priv->spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_filter_stderr (priv->spawn, pk_backend_stderr_cb);
	pk_backend_spawn_set_filter_stdout (priv->spawn, pk_backend_stdout_cb);
	pk_backend_spawn_set_name (priv->spawn, "yum");
	pk_backend_spawn_set_allow_sigkill (priv->spawn, FALSE);

	/* coldplug the mounts */
	priv->volume_monitor = g_volume_monitor_get ();
	mounts = g_volume_monitor_get_mounts (priv->volume_monitor);
	g_list_foreach (mounts, (GFunc) pk_backend_mount_add, NULL);
	g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
	g_list_free (mounts);

	/* setup a file monitor on the repos directory */
	file = g_file_new_for_path (YUM_REPOS_DIRECTORY);
	priv->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, &error);
	if (priv->monitor != NULL) {
		g_signal_connect (priv->monitor, "changed", G_CALLBACK (pk_backend_yum_repos_changed_cb), backend);
	} else {
		g_warning ("failed to setup monitor: %s", error->message);
		g_error_free (error);
	}

	/* read the config file */
	key_file = g_key_file_new ();
	config_file = g_build_filename (SYSCONFDIR, "PackageKit", "Yum.conf", NULL);
	g_debug ("loading configuration from %s", config_file);
	ret = g_key_file_load_from_file (key_file, config_file, G_KEY_FILE_NONE, &error);
	if (!ret) {
		g_warning ("failed to load Yum.conf: %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (config_file);
	if (key_file != NULL)
		g_key_file_free (key_file);
	if (file != NULL)
		g_object_unref (file);
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
	g_object_unref (priv->spawn);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
	if (priv->volume_monitor != NULL)
		g_object_unref (priv->volume_monitor);
	g_free (priv);
}

/**
 * pk_backend_start_job:
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	if (pk_backend_spawn_is_busy (priv->spawn)) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_LOCK_REQUIRED,
					   "spawned backend requires lock");
		pk_backend_job_finished (job);
		return;
	}
	pk_backend_enable_media_repo (TRUE);
}

/**
 * pk_backend_stop_job:
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_enable_media_repo (FALSE);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
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
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_BASENAME,
		PK_FILTER_ENUM_FREE,
		PK_FILTER_ENUM_NEWEST,
		PK_FILTER_ENUM_ARCH,
		PK_FILTER_ENUM_APPLICATION,
		-1);
}

/**
 * pk_backend_get_roles:
 */
PkBitfield
pk_backend_get_roles (PkBackend *backend)
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
		PK_ROLE_ENUM_GET_REPO_LIST,
		PK_ROLE_ENUM_REPO_ENABLE,
		PK_ROLE_ENUM_GET_CATEGORIES,
		-1);

	/* only add GetDistroUpgrades if the binary is present */
	if (g_file_test (PREUPGRADE_BINARY, G_FILE_TEST_EXISTS))
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);

	return roles;
}

/**
 * pk_backend_get_mime_types:
 */
gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-rpm",
				"application/x-servicepack",
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	/* this feels bad... */
	pk_backend_spawn_kill (priv->spawn);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "download-packages", directory, package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-depends", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-details", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-distro-upgrades", NULL);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, job,  "yumBackend.py", "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-requires", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, job,  "yumBackend.py", "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-packages", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}


/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (priv->spawn, job,
				 "yumBackend.py",
				 "install-packages",
				 transaction_flags_temp,
				 package_ids_temp,
				 NULL);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (priv->spawn, job,
				 "yumBackend.py",
				 "install-files",
				 transaction_flags_temp,
				 package_ids_temp,
				 NULL);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type,
			   const gchar *key_id, const gchar *package_id)
{
	const gchar *type_text;

	type_text = pk_sig_type_enum_to_string (type);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "install-signature", type_text, key_id, package_id, NULL);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (priv->spawn, job,
				 "yumBackend.py",
				 "remove-packages",
				 transaction_flags_temp,
				 package_ids_temp,
				 pk_backend_bool_to_string (allow_deps),
				 pk_backend_bool_to_string (autoremove),
				 NULL);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "search-details", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "search-file", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "search-group", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "search-name", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gchar *package_ids_temp;
	gchar *transaction_flags_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	transaction_flags_temp = pk_transaction_flag_bitfield_to_string (transaction_flags);
	pk_backend_spawn_helper (priv->spawn, job,
				 "yumBackend.py",
				 "update-packages",
				 transaction_flags_temp,
				 package_ids_temp,
				 NULL);
	g_free (package_ids_temp);
	g_free (transaction_flags_temp);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	filters_text = pk_filter_bitfield_to_string (filters);
	package_ids_temp = pk_package_ids_to_string (packages);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "resolve", filters_text, package_ids_temp, NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-repo-list", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, gboolean enabled)
{
	if (enabled == TRUE) {
		pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "repo-enable", repo_id, "true", NULL);
	} else {
		pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "repo-enable", repo_id, "false", NULL);
	}
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	gchar *search_tmp;
	gchar *filters_text;
	const gchar *provides_text;

	provides_text = pk_provides_enum_to_string (provides);
	filters_text = pk_filter_bitfield_to_string (filters);
	search_tmp = g_strjoinv ("&", values);
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "what-provides", filters_text, provides_text, search_tmp, NULL);
	g_free (filters_text);
	g_free (search_tmp);
}

/**
 * pk_backend_get_categories:
 */
void
pk_backend_get_categories (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "get-categories", NULL);
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend, PkBackendJob *job, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
	pk_backend_spawn_helper (priv->spawn, job, "yumBackend.py", "update-system", NULL);
}
