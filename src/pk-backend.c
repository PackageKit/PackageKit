/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gmodule.h>
#include <glib/gprintf.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-common.h>

#include "pk-conf.h"
#include "pk-network.h"
#include "pk-marshal.h"
#include "pk-backend.h"
#include "pk-conf.h"
#include "pk-store.h"
#include "pk-shared.h"
#include "pk-time.h"
#include "pk-notify.h"

#define PK_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND, PkBackendPrivate))

/**
 * PK_BACKEND_PERCENTAGE_DEFAULT:
 *
 * The default percentage value, should never be emitted, but should be
 * used so we can work out if a backend just calls NoPercentageUpdates
 */
#define PK_BACKEND_PERCENTAGE_DEFAULT		102

/**
 * PK_BACKEND_FINISHED_ERROR_TIMEOUT:
 *
 * The time in ms the backend has to call Finished() after ErrorCode()
 * If backends do not do this, they will be Finished() manually,
 * and a Message() will be sent to warn the developer
 */
#define PK_BACKEND_FINISHED_ERROR_TIMEOUT	2000 /* ms */

/**
 * PK_BACKEND_FINISHED_TIMEOUT_GRACE:
 *
 * The time in ms the backend waits after receiving Finished() before
 * propagating the signal to the other components.
 * This delay is required as some threads may take some time to cancel or a
 * spawned executable to disappear of the system DBUS.
 */
#define PK_BACKEND_FINISHED_TIMEOUT_GRACE	10 /* ms */

/**
 * PK_BACKEND_CANCEL_ACTION_TIMEOUT:
 *
 * The time in ms we cancel the transaction ourselves if the backend is ignoring
 * us. This means the backend will still be running, but results will not be
 * sent over the dbus interface.
 */
#define PK_BACKEND_CANCEL_ACTION_TIMEOUT	2000 /* ms */

struct PkBackendPrivate
{
	gboolean		 during_initialize;
	gboolean		 finished;
	gboolean		 has_sent_package;
	gboolean		 opened;
	gboolean		 set_error;
	gboolean		 set_eula;
	gboolean		 set_signature;
	gboolean		 simultaneous;
	gboolean		 use_time;
	gboolean		 use_threads;
	gboolean		 keep_environment;
	gchar			*transaction_id;
	gchar			*locale;
	gchar			*frontend_socket;
	guint			 cache_age;
	gchar			*name;
	gchar			*proxy_http;
	gchar			*proxy_https;
	gchar			*proxy_ftp;
	gchar			*proxy_socks;
	gchar			*no_proxy;
	gchar			*pac;
	gchar			*root;
	gpointer		 file_changed_data;
	guint			 download_files;
	guint			 percentage;
	guint			 remaining;
	guint			 subpercentage;
	guint			 signal_error_timeout;
	guint			 signal_finished;
	guint			 speed;
	GHashTable		*eulas;
	GModule			*handle;
	GThread			*thread;
	PkBackendDesc		*desc;
	PkBackendFileChanged	 file_changed_func;
	PkHintEnum		 background;
	PkHintEnum		 interactive;
	PkHintEnum		 allow_cancel;
	PkBitfield		 roles;
	PkConf			*conf;
	PkExitEnum		 exit;
	GFileMonitor		*monitor;
	PkPackage		*last_package;
	PkNetwork		*network;
	PkResults		*results;
	PkRoleEnum		 role; /* this never changes for the lifetime of a transaction */
	PkRoleEnum		 transaction_role;
	PkStatusEnum		 status; /* this changes */
	PkStore			*store;
	PkTime			*time;
	guint			 uid;
	gchar			*cmdline;
};

G_DEFINE_TYPE (PkBackend, pk_backend, G_TYPE_OBJECT)
static gpointer pk_backend_object = NULL;

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_DETAILS,
	SIGNAL_FILES,
	SIGNAL_DISTRO_UPGRADE,
	SIGNAL_PACKAGE,
	SIGNAL_UPDATE_DETAIL,
	SIGNAL_ERROR_CODE,
	SIGNAL_REPO_SIGNATURE_REQUIRED,
	SIGNAL_EULA_REQUIRED,
	SIGNAL_REQUIRE_RESTART,
	SIGNAL_MESSAGE,
	SIGNAL_CHANGE_TRANSACTION_DATA,
	SIGNAL_FINISHED,
	SIGNAL_ALLOW_CANCEL,
	SIGNAL_REPO_DETAIL,
	SIGNAL_CATEGORY,
	SIGNAL_MEDIA_CHANGE_REQUIRED,
	SIGNAL_ITEM_PROGRESS,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_BACKGROUND,
	PROP_INTERACTIVE,
	PROP_STATUS,
	PROP_ROLE,
	PROP_TRANSACTION_ID,
	PROP_SPEED,
	PROP_PERCENTAGE,
	PROP_SUBPERCENTAGE,
	PROP_REMAINING,
	PROP_UID,
	PROP_CMDLINE,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

/**
 * pk_backend_get_groups:
 **/
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->opened != FALSE, PK_GROUP_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->priv->desc->get_groups == NULL)
		return PK_GROUP_ENUM_UNKNOWN;
	return backend->priv->desc->get_groups (backend);
}

/**
 * pk_backend_get_mime_types:
 **/
gchar *
pk_backend_get_mime_types (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->opened != FALSE, NULL);

	/* not compulsory */
	if (backend->priv->desc->get_mime_types == NULL)
		return g_strdup ("");
	return backend->priv->desc->get_mime_types (backend);
}

/**
 * pk_backend_get_filters:
 **/
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_FILTER_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->opened != FALSE, PK_FILTER_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->priv->desc->get_filters == NULL)
		return PK_FILTER_ENUM_UNKNOWN;
	return backend->priv->desc->get_filters (backend);
}

/**
 * pk_backend_get_role:
 **/
PkRoleEnum
pk_backend_get_role (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	return backend->priv->role;
}

/**
 * pk_backend_get_roles:
 **/
PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	PkBitfield roles = 0;
	PkBackendDesc *desc;

	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->opened != FALSE, PK_ROLE_ENUM_UNKNOWN);

	/* optimise */
	if (backend->priv->roles != 0)
		goto out;

	/* not compulsory, but use it if we've got it */
	if (backend->priv->desc->get_roles != NULL) {
		backend->priv->roles = backend->priv->desc->get_roles (backend);
		goto out;
	}

	/* lets reduce pointer dereferences... */
	desc = backend->priv->desc;
	if (desc->cancel != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_CANCEL);
	if (desc->get_depends != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DEPENDS);
	if (desc->get_details != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DETAILS);
	if (desc->get_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_FILES);
	if (desc->get_requires != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_REQUIRES);
	if (desc->get_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_PACKAGES);
	if (desc->what_provides != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_WHAT_PROVIDES);
	if (desc->get_updates != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_UPDATES);
	if (desc->get_update_detail != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	if (desc->install_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_INSTALL_PACKAGES);
	if (desc->install_signature != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_INSTALL_SIGNATURE);
	if (desc->install_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_INSTALL_FILES);
	if (desc->refresh_cache != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REFRESH_CACHE);
	if (desc->remove_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REMOVE_PACKAGES);
	if (desc->download_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_DOWNLOAD_PACKAGES);
	if (desc->resolve != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_RESOLVE);
	if (desc->rollback != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_ROLLBACK);
	if (desc->search_details != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_DETAILS);
	if (desc->search_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_FILE);
	if (desc->search_groups != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_GROUP);
	if (desc->search_names != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SEARCH_NAME);
	if (desc->update_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_UPDATE_PACKAGES);
	if (desc->update_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_UPDATE_SYSTEM);
	if (desc->get_repo_list != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_REPO_LIST);
	if (desc->repo_enable != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_ENABLE);
	if (desc->repo_set_data != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_SET_DATA);
	if (desc->get_distro_upgrades != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	if (desc->get_categories != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_CATEGORIES);
	if (desc->simulate_install_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_INSTALL_FILES);
	if (desc->simulate_install_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES);
	if (desc->simulate_remove_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES);
	if (desc->simulate_update_packages != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES);
	if (desc->upgrade_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_UPGRADE_SYSTEM);
	if (desc->repair_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPAIR_SYSTEM);
	if (desc->simulate_repair_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_SIMULATE_REPAIR_SYSTEM);
	backend->priv->roles = roles;
out:
	return backend->priv->roles;
}

/**
 * pk_backend_is_implemented:
 **/
gboolean
pk_backend_is_implemented (PkBackend *backend, PkRoleEnum role)
{
	PkBitfield roles;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	roles = pk_backend_get_roles (backend);
	return pk_bitfield_contain (roles, role);
}

/**
 * pk_backend_implement:
 **/
void
pk_backend_implement (PkBackend *backend, PkRoleEnum role)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (role != PK_ROLE_ENUM_UNKNOWN);
	pk_bitfield_add (backend->priv->roles, role);
}

/**
 * pk_backend_set_string:
 **/
gboolean
pk_backend_set_string (PkBackend *backend, const gchar *key, const gchar *data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_string (backend->priv->store, key, data);
}

/**
 * pk_backend_set_strv:
 **/
gboolean
pk_backend_set_strv (PkBackend *backend, const gchar *key, gchar **data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_strv (backend->priv->store, key, data);
}

/**
 * pk_backend_set_array:
 **/
gboolean
pk_backend_set_array (PkBackend *backend, const gchar *key, GPtrArray *data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_array (backend->priv->store, key, data);
}

/**
 * pk_backend_set_uint:
 **/
gboolean
pk_backend_set_uint (PkBackend *backend, const gchar *key, guint data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_uint (backend->priv->store, key, data);
}

/**
 * pk_backend_set_bool:
 **/
gboolean
pk_backend_set_bool (PkBackend *backend, const gchar *key, gboolean data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_bool (backend->priv->store, key, data);
}

/**
 * pk_backend_set_pointer:
 **/
gboolean
pk_backend_set_pointer (PkBackend *backend, const gchar *key, gpointer data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_set_pointer (backend->priv->store, key, data);
}

/**
 * pk_backend_get_string:
 **/
const gchar *
pk_backend_get_string (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_string (backend->priv->store, key);
}

/**
 * pk_backend_get_strv:
 *
 * Returns: (transfer none):
 **/
gchar **
pk_backend_get_strv (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_strv (backend->priv->store, key);
}

/**
 * pk_backend_get_array:
 **/
const GPtrArray *
pk_backend_get_array (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_array (backend->priv->store, key);
}

/**
 * pk_backend_get_uint:
 **/
guint
pk_backend_get_uint (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), 0);
	return pk_store_get_uint (backend->priv->store, key);
}

/**
 * pk_backend_get_bool:
 **/
gboolean
pk_backend_get_bool (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	return pk_store_get_bool (backend->priv->store, key);
}

/**
 * pk_backend_get_pointer:
 *
 * Returns: (transfer none):
 **/
gpointer
pk_backend_get_pointer (PkBackend *backend, const gchar *key)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return pk_store_get_pointer (backend->priv->store, key);
}

/**
 * pk_backend_build_library_path:
 **/
static gchar *
pk_backend_build_library_path (PkBackend *backend, const gchar *name)
{
	gchar *path;
	gchar *filename;
#if PK_BUILD_LOCAL
	const gchar *directory;
#endif
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	filename = g_strdup_printf ("libpk_backend_%s.so", name);
#if PK_BUILD_LOCAL
	/* test_spawn, test_dbus, test_fail, etc. are in the 'test' folder */
	directory = name;
	if (g_str_has_prefix (name, "test_"))
		directory = "test";

	/* prefer the local version */
	path = g_build_filename ("..", "backends", directory, ".libs", filename, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		g_debug ("local backend not found '%s'", path);
		g_free (path);
		path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
	}
#else
	path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
#endif
	g_free (filename);
	g_debug ("dlopening '%s'", path);

	return path;
}

/**
 * pk_backend_sort_backends_cb:
 **/
static gint
pk_backend_sort_backends_cb (const gchar **store1,
			     const gchar **store2)
{
	return g_strcmp0 (*store2, *store1);
}

/**
 * pk_backend_get_auto_array:
 **/
static GPtrArray *
pk_backend_get_auto_array (GError **error)
{
	const gchar *tmp;
	GDir *dir = NULL;
	GPtrArray *array = NULL;

	dir = g_dir_open (LIBDIR "/packagekit-backend", 0, error);
	if (dir == NULL)
		goto out;
	array = g_ptr_array_new_with_free_func (g_free);
	do {
		tmp = g_dir_read_name (dir);
		if (tmp == NULL)
			break;
		if (!g_str_has_suffix (tmp, G_MODULE_SUFFIX))
			continue;
		if (g_strstr_len (tmp, -1, "pk_backend_dummy"))
			continue;
		if (g_strstr_len (tmp, -1, "pk_backend_test"))
			continue;
		g_ptr_array_add (array,
				 g_build_filename (LIBDIR,
						   "packagekit-backend",
						   tmp,
						   NULL));
	} while (1);

	/* need to sort by id predictably */
	g_ptr_array_sort (array,
			  (GCompareFunc) pk_backend_sort_backends_cb);

out:
	if (dir != NULL)
		g_dir_close (dir);
	return array;
}

/**
 * pk_backend_set_name:
 **/
gboolean
pk_backend_set_name (PkBackend *backend, const gchar *backend_name, GError **error)
{
	GModule *handle;
	gchar *path = NULL;
	gboolean ret = FALSE;
	gpointer func = NULL;
	GPtrArray *auto_backends = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend_name != NULL, FALSE);

	/* have we already been set? */
	if (backend->priv->name != NULL) {
		g_set_error (error, 1, 0, "already set name to %s", backend->priv->name);
		goto out;
	}

	/* deal with auto */
	g_debug ("Trying to load : %s", backend_name);
	if (g_strcmp0 (backend_name, "auto")  == 0) {
		auto_backends = pk_backend_get_auto_array (error);
		if (auto_backends == NULL)
			goto out;
		if (auto_backends->len == 0) {
			g_set_error (error, 1, 0,
				     "failed to find any files in %s",
				     LIBDIR "/packagekit-backend");
			goto out;
		}
		/* just pick the last to avoid 'dummy' */
		path = g_strdup (g_ptr_array_index (auto_backends, 0));
		g_debug ("using backend 'auto'=>'%s'", path);
	} else {
		path = pk_backend_build_library_path (backend, backend_name);
	}

	/* can we load it? */
	handle = g_module_open (path, 0);
	if (handle == NULL) {
		g_set_error (error, 1, 0, "opening module %s failed : %s",
			     backend_name, g_module_error ());
		goto out;
	}

	/* first check for the table of vfuncs */
	ret = g_module_symbol (handle, "pk_backend_desc", (gpointer) &func);
	if (ret) {
		g_warning ("using table-of-vfuncs compatibility mode");
		backend->priv->desc = func;
	} else {
		/* then check for the new style exported functions */
		ret = g_module_symbol (handle, "pk_backend_get_description", (gpointer *)&func);
		if (ret) {
			PkBackendDesc *desc;
			PkBackendGetCompatStringFunc backend_vfunc;
			desc = g_new0 (PkBackendDesc, 1);

			/* connect up exported methods */
			g_module_symbol (handle, "pk_backend_cancel", (gpointer *)&desc->cancel);
			g_module_symbol (handle, "pk_backend_destroy", (gpointer *)&desc->destroy);
			g_module_symbol (handle, "pk_backend_download_packages", (gpointer *)&desc->download_packages);
			g_module_symbol (handle, "pk_backend_get_categories", (gpointer *)&desc->get_categories);
			g_module_symbol (handle, "pk_backend_get_depends", (gpointer *)&desc->get_depends);
			g_module_symbol (handle, "pk_backend_get_details", (gpointer *)&desc->get_details);
			g_module_symbol (handle, "pk_backend_get_distro_upgrades", (gpointer *)&desc->get_distro_upgrades);
			g_module_symbol (handle, "pk_backend_get_files", (gpointer *)&desc->get_files);
			g_module_symbol (handle, "pk_backend_get_filters", (gpointer *)&desc->get_filters);
			g_module_symbol (handle, "pk_backend_get_groups", (gpointer *)&desc->get_groups);
			g_module_symbol (handle, "pk_backend_get_mime_types", (gpointer *)&desc->get_mime_types);
			g_module_symbol (handle, "pk_backend_get_packages", (gpointer *)&desc->get_packages);
			g_module_symbol (handle, "pk_backend_get_repo_list", (gpointer *)&desc->get_repo_list);
			g_module_symbol (handle, "pk_backend_get_requires", (gpointer *)&desc->get_requires);
			g_module_symbol (handle, "pk_backend_get_roles", (gpointer *)&desc->get_roles);
			g_module_symbol (handle, "pk_backend_get_update_detail", (gpointer *)&desc->get_update_detail);
			g_module_symbol (handle, "pk_backend_get_updates", (gpointer *)&desc->get_updates);
			g_module_symbol (handle, "pk_backend_initialize", (gpointer *)&desc->initialize);
			g_module_symbol (handle, "pk_backend_install_files", (gpointer *)&desc->install_files);
			g_module_symbol (handle, "pk_backend_install_packages", (gpointer *)&desc->install_packages);
			g_module_symbol (handle, "pk_backend_install_signature", (gpointer *)&desc->install_signature);
			g_module_symbol (handle, "pk_backend_refresh_cache", (gpointer *)&desc->refresh_cache);
			g_module_symbol (handle, "pk_backend_remove_packages", (gpointer *)&desc->remove_packages);
			g_module_symbol (handle, "pk_backend_repo_enable", (gpointer *)&desc->repo_enable);
			g_module_symbol (handle, "pk_backend_repo_set_data", (gpointer *)&desc->repo_set_data);
			g_module_symbol (handle, "pk_backend_resolve", (gpointer *)&desc->resolve);
			g_module_symbol (handle, "pk_backend_rollback", (gpointer *)&desc->rollback);
			g_module_symbol (handle, "pk_backend_search_details", (gpointer *)&desc->search_details);
			g_module_symbol (handle, "pk_backend_search_files", (gpointer *)&desc->search_files);
			g_module_symbol (handle, "pk_backend_search_groups", (gpointer *)&desc->search_groups);
			g_module_symbol (handle, "pk_backend_search_names", (gpointer *)&desc->search_names);
			g_module_symbol (handle, "pk_backend_simulate_install_files", (gpointer *)&desc->simulate_install_files);
			g_module_symbol (handle, "pk_backend_simulate_install_packages", (gpointer *)&desc->simulate_install_packages);
			g_module_symbol (handle, "pk_backend_simulate_remove_packages", (gpointer *)&desc->simulate_remove_packages);
			g_module_symbol (handle, "pk_backend_simulate_update_packages", (gpointer *)&desc->simulate_update_packages);
			g_module_symbol (handle, "pk_backend_transaction_start", (gpointer *)&desc->transaction_start);
			g_module_symbol (handle, "pk_backend_transaction_stop", (gpointer *)&desc->transaction_stop);
			g_module_symbol (handle, "pk_backend_transaction_reset", (gpointer *)&desc->transaction_reset);
			g_module_symbol (handle, "pk_backend_update_packages", (gpointer *)&desc->update_packages);
			g_module_symbol (handle, "pk_backend_update_system", (gpointer *)&desc->update_system);
			g_module_symbol (handle, "pk_backend_what_provides", (gpointer *)&desc->what_provides);
			g_module_symbol (handle, "pk_backend_upgrade_system", (gpointer *)&desc->upgrade_system);
			g_module_symbol (handle, "pk_backend_repair_system", (gpointer *)&desc->repair_system);
			g_module_symbol (handle, "pk_backend_simulate_repair_system", (gpointer *)&desc->simulate_repair_system);

			/* get old static string data */
			ret = g_module_symbol (handle, "pk_backend_get_author", (gpointer *)&backend_vfunc);
			if (ret)
				desc->author = backend_vfunc (backend);
			ret = g_module_symbol (handle, "pk_backend_get_description", (gpointer *)&backend_vfunc);
			if (ret)
				desc->description = backend_vfunc (backend);

			/* make available */
			backend->priv->desc = desc;
		}
	}
	if (!ret) {
		g_module_close (handle);
		g_set_error (error, 1, 0, "could not find description in plugin %s, not loading", backend_name);
		goto out;
	}

	/* save the backend name and handle */
	g_free (backend->priv->name);
	backend->priv->name = g_strdup (backend_name);
	backend->priv->handle = handle;
out:
	if (auto_backends != NULL)
		g_ptr_array_unref (auto_backends);
	g_free (path);
	return ret;
}

/**
 * pk_backend_set_proxy:
 **/
gboolean
pk_backend_set_proxy (PkBackend	*backend,
		      const gchar *proxy_http,
		      const gchar *proxy_https,
		      const gchar *proxy_ftp,
		      const gchar *proxy_socks,
		      const gchar *no_proxy,
		      const gchar *pac)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_free (backend->priv->proxy_http);
	g_free (backend->priv->proxy_https);
	g_free (backend->priv->proxy_ftp);
	g_free (backend->priv->proxy_socks);
	g_free (backend->priv->no_proxy);
	g_free (backend->priv->pac);
	backend->priv->proxy_http = g_strdup (proxy_http);
	backend->priv->proxy_https = g_strdup (proxy_https);
	backend->priv->proxy_ftp = g_strdup (proxy_ftp);
	backend->priv->proxy_socks = g_strdup (proxy_socks);
	backend->priv->no_proxy = g_strdup (no_proxy);
	backend->priv->pac = g_strdup (pac);
	return TRUE;
}

/**
 * pk_backend_get_proxy_http:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_get_proxy_http (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->proxy_http);
}

/**
 * pk_backend_get_proxy_https:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_get_proxy_https (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->proxy_https);
}

/**
 * pk_backend_get_proxy_ftp:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_get_proxy_ftp (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->proxy_ftp);
}

/**
 * pk_backend_get_proxy_socks:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_get_proxy_socks (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->proxy_socks);
}

/**
 * pk_backend_get_no_proxy:
 *
 * Return value: comma seporated value of proxy exlude string
 **/
gchar *
pk_backend_get_no_proxy (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->no_proxy);
}

/**
 * pk_backend_get_pac:
 *
 * Return value: proxy PAC filename
 **/
gchar *
pk_backend_get_pac (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->pac);
}

/**
 * pk_backend_set_root:
 **/
gboolean
pk_backend_set_root (PkBackend	*backend, const gchar *root)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* NULL is actually the default, which is '/' */
	if (root == NULL)
		root = "/";

	g_free (backend->priv->root);
	backend->priv->root = g_strdup (root);
	g_debug ("install root now %s", backend->priv->root);
	return TRUE;
}

/**
 * pk_backend_set_cmdline:
 **/
gboolean
pk_backend_set_cmdline (PkBackend *backend, const gchar *cmdline)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	g_free (backend->priv->cmdline);
	backend->priv->cmdline = g_strdup (cmdline);
	g_debug ("install cmdline now %s", backend->priv->cmdline);
	return TRUE;
}

/**
 * pk_backend_set_uid:
 **/
gboolean
pk_backend_set_uid (PkBackend *backend, guint uid)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	backend->priv->uid = uid;
	g_debug ("install uid now %i", backend->priv->uid);
	return TRUE;
}

/**
 * pk_backend_get_root:
 *
 * Return value: root to use for installing, or %NULL
 **/
const gchar *
pk_backend_get_root (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return backend->priv->root;
}

/**
 * pk_backend_open:
 *
 * Responsible for initialising the external backend object.
 *
 * Typically this will involve taking database locks for exclusive package access.
 * This method should only be called from the engine, unless the backend object
 * is used in self-check code, in which case the lock and unlock will have to
 * be done manually.
 **/
gboolean
pk_backend_open (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->desc != NULL, FALSE);

	if (backend->priv->opened) {
		g_debug ("already open (nonfatal)");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->priv->desc->initialize != NULL) {
		backend->priv->during_initialize = TRUE;
		backend->priv->desc->initialize (backend);
		backend->priv->during_initialize = FALSE;
	}
	backend->priv->opened = TRUE;
	return TRUE;
}

/**
 * pk_backend_close:
 *
 * Responsible for finalising the external backend object.
 *
 * Typically this will involve releasing database locks for any other access.
 * This method should only be called from the engine, unless the backend object
 * is used in self-check code, in which case it will have to be done manually.
 **/
gboolean
pk_backend_close (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	if (backend->priv->opened == FALSE) {
		g_debug ("already closed (nonfatal)");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->priv->desc == NULL) {
		g_warning ("not yet opened backend, try pk_backend_open()");
		return FALSE;
	}
	if (backend->priv->desc->destroy != NULL)
		backend->priv->desc->destroy (backend);
	backend->priv->opened = FALSE;
	return TRUE;
}

/**
 * pk_backend_set_percentage:
 **/
gboolean
pk_backend_set_percentage (PkBackend *backend, guint percentage)
{
	guint remaining;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: percentage %i", percentage);
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->percentage == percentage) {
		g_debug ("duplicate set of %i", percentage);
		return FALSE;
	}

	/* check over */
	if (percentage > PK_BACKEND_PERCENTAGE_INVALID) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "percentage value is invalid: %i", percentage);
		return FALSE;
	}

	/* check under */
	if (percentage < 100 &&
	    backend->priv->percentage < 100 &&
	    percentage < backend->priv->percentage) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "percentage value is going down to %i from %i",
				    percentage, backend->priv->percentage);
		return FALSE;
	}

	/* save in case we need this from coldplug */
	backend->priv->percentage = percentage;
	g_object_notify (G_OBJECT (backend), "percentage");

	/* only compute time if we have data */
	if (percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		/* needed for time remaining calculation */
		pk_time_add_data (backend->priv->time, percentage);

		/* lets try this and print as debug */
		remaining = pk_time_get_remaining (backend->priv->time);
		g_debug ("this will now take ~%i seconds", remaining);

		/* value cached from config file */
		if (backend->priv->use_time)
			backend->priv->remaining = remaining;
		g_object_notify (G_OBJECT (backend), "remaining");
	}

	return TRUE;
}

/**
 * pk_backend_set_speed:
 **/
gboolean
pk_backend_set_speed (PkBackend *backend, guint speed)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: speed %i", speed);
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->speed == speed) {
		g_debug ("duplicate set of %i", speed);
		return FALSE;
	}

	/* set new value */
	backend->priv->speed = speed;
	g_object_notify (G_OBJECT (backend), "speed");
	return TRUE;
}

/**
 * pk_backend_get_runtime:
 *
 * Returns time running in ms
 */
guint
pk_backend_get_runtime (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), 0);
	g_return_val_if_fail (backend->priv->opened != FALSE, 0);
	return pk_time_get_elapsed (backend->priv->time);
}

/**
 * pk_backend_set_item_progress:
 **/
gboolean
pk_backend_set_item_progress (PkBackend *backend,
			      const gchar *package_id,
			      guint percentage)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: item-percentage %i", percentage);
		return FALSE;
	}

	/* invalid number? */
	if (percentage > 100 && percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		g_debug ("invalid number %i", percentage);
		return FALSE;
	}

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_ITEM_PROGRESS], 0,
		       package_id, percentage);
	return TRUE;
}

/**
 * pk_backend_set_sub_percentage:
 **/
gboolean
pk_backend_set_sub_percentage (PkBackend *backend, guint percentage)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: sub-percentage %i", percentage);
		return FALSE;
	}

	/* set the same twice? */
	if (backend->priv->subpercentage == percentage) {
		g_debug ("duplicate set of %i", percentage);
		return FALSE;
	}

	/* invalid number? */
	if (percentage > 100 && percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		g_debug ("invalid number %i", percentage);
		return FALSE;
	}

	/* save in case we need this from coldplug */
	backend->priv->subpercentage = percentage;

	/* emit the progress changed signal */
	g_object_notify (G_OBJECT (backend), "subpercentage");
	return TRUE;
}

/**
 * pk_backend_set_status:
 **/
gboolean
pk_backend_set_status (PkBackend *backend, PkStatusEnum status)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* already this? */
	if (backend->priv->status == status) {
		g_debug ("already set same status");
		return TRUE;
	}

	/* have we already set an error? */
	if (backend->priv->set_error && status != PK_STATUS_ENUM_FINISHED) {
		g_warning ("already set error, cannot process: status %s", pk_status_enum_to_string (status));
		return FALSE;
	}

	/* backends don't do this */
	if (status == PK_STATUS_ENUM_WAIT) {
		g_warning ("backend tried to WAIT, only the runner should set this value");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s shouldn't use STATUS_WAIT", pk_role_enum_to_string (backend->priv->role));
		return FALSE;
	}

	/* do we have to enumate a running call? */
	if (status != PK_STATUS_ENUM_RUNNING && status != PK_STATUS_ENUM_SETUP) {
		if (backend->priv->status == PK_STATUS_ENUM_SETUP) {
			/* emit */
			g_signal_emit (backend, signals[SIGNAL_STATUS_CHANGED], 0, PK_STATUS_ENUM_RUNNING);
		}
	}

	backend->priv->status = status;

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_STATUS_CHANGED], 0, status);
	return TRUE;
}

/**
 * pk_backend_package_emulate_finished:
 **/
static gboolean
pk_backend_package_emulate_finished (PkBackend *backend)
{
	gboolean ret = FALSE;
	PkPackage *item;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	/* simultaneous handles this on it's own */
	if (backend->priv->simultaneous)
		goto out;

	/* first package in transaction */
	item = backend->priv->last_package;
	if (item == NULL)
		goto out;

	/* get data */
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* already finished */
	if (info == PK_INFO_ENUM_FINISHED)
		goto out;

	/* only makes sense for some values */
	if (info == PK_INFO_ENUM_DOWNLOADING ||
	    info == PK_INFO_ENUM_UPDATING ||
	    info == PK_INFO_ENUM_INSTALLING ||
	    info == PK_INFO_ENUM_REMOVING ||
	    info == PK_INFO_ENUM_CLEANUP ||
	    info == PK_INFO_ENUM_OBSOLETING ||
	    info == PK_INFO_ENUM_REINSTALLING ||
	    info == PK_INFO_ENUM_DOWNGRADING) {
		pk_backend_package (backend, PK_INFO_ENUM_FINISHED, package_id, summary);
		ret = TRUE;
	}
out:
	g_free (package_id);
	g_free (summary);
	return ret;
}

/**
 * pk_backend_package_emulate_finished_for_package:
 **/
static gboolean
pk_backend_package_emulate_finished_for_package (PkBackend *backend, PkPackage *item)
{
	gboolean ret = FALSE;

	/* simultaneous handles this on it's own */
	if (backend->priv->simultaneous) {
		g_debug ("backend handling finished");
		goto out;
	}

	/* first package in transaction */
	if (backend->priv->last_package == NULL) {
		g_debug ("first package, so no finished");
		goto out;
	}

	/* same package, just info change */
	if (pk_package_equal_id (backend->priv->last_package, item)) {
		g_debug ("same package_id, ignoring");
		goto out;
	}

	/* emit the old package as finished */
	ret = pk_backend_package_emulate_finished (backend);
out:
	return ret;
}

/**
 * pk_backend_strsafe:
 * @text: The input text to make safe
 *
 * Replaces chars in the text that may be dangerous, or that may print
 * incorrectly. These chars include new lines, tabs and line feed, and are
 * replaced by spaces.
 *
 * Return value: the new string with no insane chars
 **/
static gchar *
pk_backend_strsafe (const gchar *text)
{
	gchar *text_safe;
	gboolean ret;
	const gchar *delimiters;

	if (text == NULL)
		return NULL;

	/* is valid UTF8? */
	ret = g_utf8_validate (text, -1, NULL);
	if (!ret) {
		g_warning ("text '%s' was not valid UTF8!", text);
		return NULL;
	}

	/* rip out any insane characters */
	delimiters = "\\\f\r\t";
	text_safe = g_strdup (text);
	g_strdelimit (text_safe, delimiters, ' ');
	return text_safe;
}

/**
 * pk_backend_package:
 **/
gboolean
pk_backend_package (PkBackend *backend, PkInfoEnum info, const gchar *package_id, const gchar *summary)
{
	gchar *summary_safe = NULL;
	PkPackage *item = NULL;
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* check we are valid */
	item = pk_package_new ();
	ret = pk_package_set_id (item, package_id, &error);
	if (!ret) {
		g_warning ("package_id %s invalid and cannot be processed: %s",
			   package_id, error->message);
		g_error_free (error);
		goto out;
	}

	/* replace unsafe chars */
	summary_safe = pk_backend_strsafe (summary);

	/* fix up available and installed when doing simulate roles */
	if (backend->priv->transaction_role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES ||
	    backend->priv->transaction_role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES ||
	    backend->priv->transaction_role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES ||
	    backend->priv->transaction_role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES) {
		if (info == PK_INFO_ENUM_AVAILABLE)
			info = PK_INFO_ENUM_INSTALLING;
		else if (info == PK_INFO_ENUM_INSTALLED)
			info = PK_INFO_ENUM_REMOVING;
	}

	/* create a new package object AFTER we emulate the info value */
	g_object_set (item,
		      "info", info,
		      "summary", summary_safe,
		      NULL);

	/* is it the same? */
	ret = (backend->priv->last_package != NULL && pk_package_equal (backend->priv->last_package, item));
	if (ret) {
		g_debug ("skipping duplicate %s", package_id);
		ret = FALSE;
		goto out;
	}

	/* simulate the finish here when required */
	if (info != PK_INFO_ENUM_FINISHED)
		pk_backend_package_emulate_finished_for_package (backend, item);

	/* update the 'last' package */
	if (backend->priv->last_package != NULL)
		g_object_unref (backend->priv->last_package);
	backend->priv->last_package = g_object_ref (item);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: package %s", package_id);
		ret = FALSE;
		goto out;
	}

	/* we automatically set the transaction status for some PkInfoEnums if running
	 * in non-simultaneous transaction mode */
	if (!backend->priv->simultaneous) {
		if (info == PK_INFO_ENUM_DOWNLOADING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
		else if (info == PK_INFO_ENUM_UPDATING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		else if (info == PK_INFO_ENUM_INSTALLING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
		else if (info == PK_INFO_ENUM_REMOVING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
		else if (info == PK_INFO_ENUM_CLEANUP)
			pk_backend_set_status (backend, PK_STATUS_ENUM_CLEANUP);
		else if (info == PK_INFO_ENUM_OBSOLETING)
			pk_backend_set_status (backend, PK_STATUS_ENUM_OBSOLETE);
	}

	/* we've sent a package for this transaction */
	backend->priv->has_sent_package = TRUE;

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_PACKAGE], 0, item);

	/* add to results if meaningful */
	if (info != PK_INFO_ENUM_FINISHED)
		pk_results_add_package (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (summary_safe);
	return ret;
}

/**
 * pk_backend_update_detail:
 **/
gboolean
pk_backend_update_detail (PkBackend *backend, const gchar *package_id,
			  const gchar *updates, const gchar *obsoletes,
			  const gchar *vendor_url, const gchar *bugzilla_url,
			  const gchar *cve_url, PkRestartEnum restart,
			  const gchar *update_text, const gchar	*changelog,
			  PkUpdateStateEnum state, const gchar *issued_text,
			  const gchar *updated_text)
{
	gchar *update_text_safe = NULL;
	PkUpdateDetail *item = NULL;
	GTimeVal timeval;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: update_detail %s", package_id);
		goto out;
	}

	/* check the dates are not empty */
	if (issued_text != NULL && issued_text[0] == '\0')
		issued_text = NULL;
	if (updated_text != NULL && updated_text[0] == '\0')
		updated_text = NULL;

	/* check the issued dates are valid */
	if (issued_text != NULL) {
		ret = g_time_val_from_iso8601 (issued_text, &timeval);
		if (!ret)
			g_warning ("failed to parse issued '%s'", issued_text);
	}
	if (updated_text != NULL) {
		ret = g_time_val_from_iso8601 (updated_text, &timeval);
		if (!ret)
			g_warning ("failed to parse updated '%s'", updated_text);
	}

	/* replace unsafe chars */
	update_text_safe = pk_backend_strsafe (update_text);

	/* form PkUpdateDetail struct */
	item = pk_update_detail_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "updates", updates,
		      "obsoletes", obsoletes,
		      "vendor-url", vendor_url,
		      "bugzilla-url", bugzilla_url,
		      "cve-url", cve_url,
		      "restart", restart,
		      "update-text", update_text_safe,
		      "changelog", changelog,
		      "state", state,
		      "issued", issued_text,
		      "updated", updated_text,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_UPDATE_DETAIL], 0, item);
	pk_results_add_update_detail (backend->priv->results, item);

	/* we parsed okay */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (update_text_safe);
	return ret;
}

/**
 * pk_backend_require_restart:
 **/
gboolean
pk_backend_require_restart (PkBackend *backend, PkRestartEnum restart, const gchar *package_id)
{
	gboolean ret = FALSE;
	PkRequireRestart *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: require-restart %s", pk_restart_enum_to_string (restart));
		goto out;
	}

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		g_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* form PkRequireRestart struct */
	item = pk_require_restart_new ();
	g_object_set (item,
		      "restart", restart,
		      "package-id", package_id,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_REQUIRE_RESTART], 0, item);
	pk_results_add_require_restart (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_message:
 **/
gboolean
pk_backend_message (PkBackend *backend, PkMessageEnum message, const gchar *format, ...)
{
	gboolean ret = FALSE;
	va_list args;
	gchar *buffer = NULL;
	PkMessage *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error && message != PK_MESSAGE_ENUM_BACKEND_ERROR) {
		g_warning ("already set error, cannot process: message %s", pk_message_enum_to_string (message));
		goto out;
	}

	/* we've deprecated this */
	if (message == PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE) {
		g_warning ("Do not use %s, instead indicate the package using Package(untrusted,package_id,summary)",
			   pk_message_enum_to_string (message));
		goto out;
	}

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* form PkMessage struct */
	item = pk_message_new ();
	g_object_set (item,
		      "type", message,
		      "details", buffer,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_MESSAGE], 0, item);
	pk_results_add_message (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	g_free (buffer);
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_set_transaction_data:
 **/
gboolean
pk_backend_set_transaction_data (PkBackend *backend, const gchar *data)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process");
		return FALSE;
	}

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_CHANGE_TRANSACTION_DATA], 0, data);
	return TRUE;
}

/**
 * pk_backend_set_simultaneous_mode:
 **/
gboolean
pk_backend_set_simultaneous_mode (PkBackend *backend, gboolean simultaneous)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	backend->priv->simultaneous = simultaneous;
	if (simultaneous)
		g_warning ("simultaneous mode is not well tested, use with caution");
	return TRUE;
}

/**
 * pk_backend_get_locale:
 *
 * Return value: session locale, e.g. en_GB
 **/
gchar *
pk_backend_get_locale (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->locale);
}

/**
 * pk_backend_set_locale:
 **/
gboolean
pk_backend_set_locale (PkBackend *backend, const gchar *code)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (code != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	g_debug ("locale changed to %s", code);
	g_free (backend->priv->locale);
	backend->priv->locale = g_strdup (code);

	return TRUE;
}

/**
 * pk_backend_get_frontend_socket:
 *
 * Return value: session frontend_socket, e.g. /tmp/socket.345
 **/
gchar *
pk_backend_get_frontend_socket (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return g_strdup (backend->priv->frontend_socket);
}

/**
 * pk_backend_set_frontend_socket:
 **/
gboolean
pk_backend_set_frontend_socket (PkBackend *backend, const gchar *frontend_socket)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	g_debug ("frontend_socket changed to %s", frontend_socket);
	g_free (backend->priv->frontend_socket);
	backend->priv->frontend_socket = g_strdup (frontend_socket);

	return TRUE;
}

/**
 * pk_backend_get_cache_age:
 *
 * Gets the maximum cache age in seconds.
 *
 * Return value: the cache age in seconds, or 0 for unset or %G_MAXUINT for 'infinity'
 **/
guint
pk_backend_get_cache_age (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), 0);
	return backend->priv->cache_age;
}

/**
 * pk_backend_set_cache_age:
 **/
void
pk_backend_set_cache_age (PkBackend *backend, guint cache_age)
{
	const guint cache_age_offset = 60 * 30;

	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->opened != FALSE);

	/* We offset the cache age by 30 minutes if possible to
	 * account for the possible delay in running the transaction,
	 * for example:
	 *
	 * Update check set to once per 3 days
	 * GUI starts checking for updates on Monday at 12:00
	 * Update check completes on Monday at 12:01
	 * GUI starts checking for updates on Thursday at 12:00 (exactly 3 days later)
	 * Cache is 2 days 23 hours 59 minutes old
	 * Backend sees it's not 3 days old, does nothing
	 */
	if (cache_age > cache_age_offset)
		cache_age -= cache_age_offset;

	g_debug ("cache-age changed to %i", cache_age);
	backend->priv->cache_age = cache_age;
}

/**
 * pk_backend_details:
 **/
gboolean
pk_backend_details (PkBackend *backend, const gchar *package_id,
		    const gchar *license, PkGroupEnum group,
		    const gchar *description, const gchar *url, gulong size)
{
	gchar *description_safe = NULL;
	PkDetails *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: details %s", package_id);
		goto out;
	}

	/* replace unsafe chars */
	description_safe = pk_backend_strsafe (description);

	/* form PkDetails struct */
	item = pk_details_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "license", license,
		      "group", group,
		      "description", description_safe,
		      "url", url,
		      "size", (guint64) size,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_DETAILS], 0, item);
	pk_results_add_details (backend->priv->results, item);

	/* we parsed okay */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (description_safe);
	return ret;
}

/**
 * pk_backend_files:
 *
 * package_id is NULL when we are using this as a calback from DownloadPackages
 **/
gboolean
pk_backend_files (PkBackend *backend, const gchar *package_id, const gchar *filelist)
{
	gboolean ret;
	PkFiles *item = NULL;
	gchar **files = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (filelist != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: files %s", package_id);
		return FALSE;
	}

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		g_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* form PkFiles struct */
	files = g_strsplit (filelist, ";", -1);
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_FILES], 0, item);
	pk_results_add_files (backend->priv->results, item);

	/* success */
	backend->priv->download_files++;
	ret = TRUE;
out:
	g_strfreev (files);
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_distro_upgrade:
 **/
gboolean
pk_backend_distro_upgrade (PkBackend *backend, PkDistroUpgradeEnum state, const gchar *name, const gchar *summary)
{
	gboolean ret = FALSE;
	gchar *name_safe = NULL;
	gchar *summary_safe = NULL;
	PkDistroUpgrade *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (state != PK_DISTRO_UPGRADE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (summary != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: distro-upgrade");
		goto out;
	}

	/* replace unsafe chars */
	name_safe = pk_backend_strsafe (name);
	summary_safe = pk_backend_strsafe (summary);

	/* form PkDistroUpgrade struct */
	item = pk_distro_upgrade_new ();
	g_object_set (item,
		      "state", state,
		      "name", name_safe,
		      "summary", summary_safe,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_DISTRO_UPGRADE], 0, item);
	pk_results_add_distro_upgrade (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (name_safe);
	g_free (summary_safe);
	return ret;
}

/**
 * pk_backend_repo_signature_required:
 **/
gboolean
pk_backend_repo_signature_required (PkBackend *backend, const gchar *package_id,
				    const gchar *repository_name, const gchar *key_url,
				    const gchar *key_userid, const gchar *key_id, const gchar *key_fingerprint,
				    const gchar *key_timestamp, PkSigTypeEnum type)
{
	gboolean ret = FALSE;
	PkRepoSignatureRequired *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (repository_name != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: repo-sig-reqd");
		goto out;
	}

	/* check we don't do this more than once */
	if (backend->priv->set_signature) {
		g_warning ("already asked for a signature, cannot process");
		goto out;
	}

	/* form PkRepoSignatureRequired struct */
	item = pk_repo_signature_required_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "repository-name", repository_name,
		      "key-url", key_url,
		      "key-userid", key_userid,
		      "key-id", key_id,
		      "key-fingerprint", key_fingerprint,
		      "key-timestamp", key_timestamp,
		      "type", type,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_REPO_SIGNATURE_REQUIRED], 0, item);
	pk_results_add_repo_signature_required (backend->priv->results, item);

	/* success */
	backend->priv->set_signature = TRUE;
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_eula_required:
 **/
gboolean
pk_backend_eula_required (PkBackend *backend, const gchar *eula_id, const gchar *package_id,
			  const gchar *vendor_name, const gchar *license_agreement)
{
	PkEulaRequired *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (vendor_name != NULL, FALSE);
	g_return_val_if_fail (license_agreement != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: eula required");
		goto out;
	}

	/* check we don't do this more than once */
	if (backend->priv->set_eula) {
		g_warning ("already asked for a signature, cannot process");
		goto out;
	}

	/* form PkEulaRequired struct */
	item = pk_eula_required_new ();
	g_object_set (item,
		      "eula-id", eula_id,
		      "package-id", package_id,
		      "vendor-name", vendor_name,
		      "license-agreement", license_agreement,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_EULA_REQUIRED], 0, item);
	pk_results_add_eula_required (backend->priv->results, item);

	/* success */
	backend->priv->set_eula = TRUE;
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_media_change_required:
 **/
gboolean
pk_backend_media_change_required (PkBackend *backend,
				  PkMediaTypeEnum media_type,
				  const gchar *media_id,
				  const gchar *media_text)
{
	PkMediaChangeRequired *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (media_id != NULL, FALSE);
	g_return_val_if_fail (media_text != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: media change required");
		goto out;
	}

	/* form PkMediaChangeRequired struct */
	item = pk_media_change_required_new ();
	g_object_set (item,
		      "media-type", media_type,
		      "media-id", media_id,
		      "media-text", media_text,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_MEDIA_CHANGE_REQUIRED], 0, item);
	pk_results_add_media_change_required (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	return ret;
}

/**
 * pk_backend_repo_detail:
 **/
gboolean
pk_backend_repo_detail (PkBackend *backend, const gchar *repo_id,
			const gchar *description, gboolean enabled)
{
	gchar *description_safe = NULL;
	PkRepoDetail *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: repo-detail %s", repo_id);
		goto out;
	}

	/* replace unsafe chars */
	description_safe = pk_backend_strsafe (description);

	/* form PkRepoDetail struct */
	item = pk_repo_detail_new ();
	g_object_set (item,
		      "repo-id", repo_id,
		      "description", description,
		      "enabled", enabled,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_REPO_DETAIL], 0, item);
	pk_results_add_repo_detail (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (description_safe);
	return ret;
}

/**
 * pk_backend_category:
 **/
gboolean
pk_backend_category (PkBackend *backend, const gchar *parent_id, const gchar *cat_id, const gchar *name, const gchar *summary, const gchar *icon)
{
	gchar *summary_safe = NULL;
	PkCategory *item = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (cat_id != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error) {
		g_warning ("already set error, cannot process: category %s", cat_id);
		goto out;
	}

	/* replace unsafe chars */
	summary_safe = pk_backend_strsafe (summary);

	/* form PkCategory struct */
	item = pk_category_new ();
	g_object_set (item,
		      "parent-id", parent_id,
		      "cat-id", cat_id,
		      "name", name,
		      "summary", summary,
		      "icon", icon,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_CATEGORY], 0, item);
	pk_results_add_category (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (summary_safe);
	return ret;
}

/**
 * pk_backend_repo_list_changed:
 **/
gboolean
pk_backend_repo_list_changed (PkBackend *backend)
{
	PkNotify *notify;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	notify = pk_notify_new ();
	pk_notify_repo_list_changed (notify);
	g_object_unref (notify);

	return TRUE;
}

/**
 * pk_backend_get_is_finished:
 **/
gboolean
pk_backend_get_is_finished (PkBackend *backend)
{
	return backend->priv->finished;
}

/**
 * pk_backend_get_is_error_set:
 **/
gboolean
pk_backend_get_is_error_set (PkBackend *backend)
{
	return backend->priv->set_error;
}

/**
 * pk_backend_error_timeout_delay_cb:
 *
 * We have to call Finished() within PK_BACKEND_FINISHED_ERROR_TIMEOUT of ErrorCode(), enforce this.
 **/
static gboolean
pk_backend_error_timeout_delay_cb (gpointer data)
{
	PkBackend *backend = PK_BACKEND (data);
	PkMessage *item = NULL;

	/* check we have not already finished */
	if (backend->priv->finished) {
		g_warning ("consistency error");
		goto out;
	}

	/* form PkMessage struct */
	item = pk_message_new ();
	g_object_set (item,
		      "type", PK_MESSAGE_ENUM_BACKEND_ERROR,
		      "details", "ErrorCode() has to be followed immediately with Finished()!\n"
		      "Failure to do so, results in PK assuming the thread has hung, and desparately "
		      " starting another backend thread to process future requests: be warned, "
		      " your code is about to break in exotic ways.",
		      NULL);

	/* warn the backend developer that they've done something worng
	 * - we can't use pk_backend_message here as we have already set
	 * backend->priv->set_error to TRUE and hence the message would be ignored */
	g_signal_emit (backend, signals[SIGNAL_MESSAGE], 0, item);

	pk_backend_finished (backend);
out:
	if (item != NULL)
		g_object_unref (item);
	backend->priv->signal_error_timeout = 0;
	return FALSE;
}

/**
 * pk_backend_error_code_is_need_untrusted:
 **/
static gboolean
pk_backend_error_code_is_need_untrusted (PkErrorEnum error_code)
{
	gboolean ret = FALSE;
	switch (error_code) {
		case PK_ERROR_ENUM_GPG_FAILURE:
		case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
		case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
		case PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED:
		case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
			ret = TRUE;
			break;
		default:
			break;
	}
	return ret;
}

/**
 * pk_backend_error_code:
 **/
gboolean
pk_backend_error_code (PkBackend *backend, PkErrorEnum error_code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer;
	gboolean ret = TRUE;
	gboolean need_untrusted;
	PkError *item = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	/* check we are not doing Init() */
	if (backend->priv->during_initialize) {
		g_warning ("set during init: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* did we set a duplicate error? */
	if (backend->priv->set_error) {
		g_warning ("More than one error emitted! You tried to set '%s'", buffer);
		ret = FALSE;
		goto out;
	}
	backend->priv->set_error = TRUE;

	/* we only allow a short time to send finished after error_code */
	backend->priv->signal_error_timeout = g_timeout_add (PK_BACKEND_FINISHED_ERROR_TIMEOUT,
							     pk_backend_error_timeout_delay_cb, backend);
	g_source_set_name_by_id (backend->priv->signal_error_timeout, "[PkBackend] error-code");

	/* some error codes have a different exit code */
	need_untrusted = pk_backend_error_code_is_need_untrusted (error_code);
	if (need_untrusted)
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_NEED_UNTRUSTED);
	else if (error_code == PK_ERROR_ENUM_CANCELLED_PRIORITY)
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_CANCELLED_PRIORITY);
	else
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_FAILED);

	/* set the hint that RepairSystem is needed */
	if (error_code == PK_ERROR_ENUM_UNFINISHED_TRANSACTION) {
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_REPAIR_REQUIRED);
	}

	/* form PkError struct */
	item = pk_error_new ();
	g_object_set (item,
		      "code", error_code,
		      "details", buffer,
		      NULL);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_ERROR_CODE], 0, item);
	pk_results_set_error_code (backend->priv->results, item);

	/* success */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (buffer);
	return ret;
}

/**
 * pk_backend_has_set_error_code:
 **/
gboolean
pk_backend_has_set_error_code (PkBackend *backend)
{
	return backend->priv->set_error;
}

/**
 * pk_backend_set_allow_cancel:
 **/
gboolean
pk_backend_set_allow_cancel (PkBackend *backend, gboolean allow_cancel)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->desc != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* have we already set an error? */
	if (backend->priv->set_error && allow_cancel) {
		g_warning ("already set error, cannot process: allow-cancel %i", allow_cancel);
		return FALSE;
	}

	/* same as last state? */
	if (backend->priv->allow_cancel == (PkHintEnum) allow_cancel) {
		g_debug ("ignoring same allow-cancel state");
		return FALSE;
	}

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_ALLOW_CANCEL], 0, allow_cancel);
	backend->priv->allow_cancel = allow_cancel;
	return TRUE;
}

/**
 * pk_backend_get_allow_cancel:
 **/
gboolean
pk_backend_get_allow_cancel (PkBackend *backend)
{
	gboolean allow_cancel = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* return FALSE if we never set state */
	if (backend->priv->allow_cancel != PK_HINT_ENUM_UNSET)
		allow_cancel = backend->priv->allow_cancel;

	return allow_cancel;
}

/**
 * pk_backend_set_keep_environment:
 **/
gboolean
pk_backend_set_keep_environment (PkBackend *backend, gboolean keep_environment)
{
	g_return_val_if_fail (PK_IS_BACKEND(backend), FALSE);

	backend->priv->keep_environment = keep_environment;
	return TRUE;
}

/**
 * pk_backend_get_keep_environment:
 **/
gboolean
pk_backend_get_keep_environment (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND(backend), FALSE);

	return backend->priv->keep_environment;
}

/**
 * pk_backend_set_role_internal:
 **/
static gboolean
pk_backend_set_role_internal (PkBackend *backend, PkRoleEnum role)
{
	/* Should only be called once... */
	if (backend->priv->role != PK_ROLE_ENUM_UNKNOWN &&
	    backend->priv->role != role) {
		g_warning ("cannot set role to %s, already %s",
			     pk_role_enum_to_string (role),
			     pk_role_enum_to_string (backend->priv->role));
		return FALSE;
	}

	/* reset the timer */
	pk_time_reset (backend->priv->time);

	g_debug ("setting role to %s", pk_role_enum_to_string (role));
	backend->priv->role = role;
	backend->priv->status = PK_STATUS_ENUM_WAIT;
	g_signal_emit (backend, signals[SIGNAL_STATUS_CHANGED], 0, backend->priv->status);
	return TRUE;
}

/**
 * pk_backend_set_role:
 **/
gboolean
pk_backend_set_role (PkBackend *backend, PkRoleEnum role)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* the role of the transaction can be different to the role of the backend if:
	 * - we reuse the backend for instance searching for files before UpdatePackages
	 * - we are simulating the SimulateInstallPackages with a GetDepends call */
	g_debug ("setting transaction role to %s", pk_role_enum_to_string (role));
	backend->priv->transaction_role = role;
	return pk_backend_set_role_internal (backend, role);
}

/**
 * pk_backend_set_exit_code:
 *
 * Should only be used internally, or from PkRunner when setting CANCELLED.
 **/
gboolean
pk_backend_set_exit_code (PkBackend *backend, PkExitEnum exit_enum)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	if (backend->priv->exit != PK_EXIT_ENUM_UNKNOWN) {
		g_warning ("already set exit status: old=%s, new=%s",
			    pk_exit_enum_to_string (backend->priv->exit),
			    pk_exit_enum_to_string (exit_enum));
		return FALSE;
	}

	/* new value */
	backend->priv->exit = exit_enum;
	return TRUE;
}

/**
 * pk_backend_get_exit_code:
 **/
PkExitEnum
pk_backend_get_exit_code (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_EXIT_ENUM_UNKNOWN);
	return backend->priv->exit;
}

/**
 * pk_backend_transaction_start:
 *
 * This is called just before the threaded transaction method, and in
 * the newly created thread context. e.g.
 *
 * >>> desc->transaction_start(backend)
 *     (locked backend)
 * >>> desc->transaction_reset(backend)
 * >>> desc->backend_method_we_want_to_run(backend)
 * <<< ::Package(PK_INFO_ENUM_INSTALLING,"hal;0.1.1;i386;fedora","Hardware Stuff")
 * >>> desc->transaction_stop(backend)
 *     (unlocked backend)
 * <<< ::Finished()
 *
 * or in the case of backend_method_we_want_to_run() failure:
 * >>> desc->transaction_start(backend)
 *     (locked backend)
 * >>> desc->transaction_reset(backend)
 * >>> desc->backend_method_we_want_to_run(backend)
 * <<< ::ErrorCode(PK_ERROR_ENUM_FAILED_TO_FIND,"no package")
 * >>> desc->transaction_stop(backend)
 *     (unlocked backend)
 * <<< ::Finished()
 *
 * or in the case of transaction_start() failure:
 * >>> desc->transaction_start(backend)
 *     (failed to lock backend)
 * <<< ::ErrorCode(PK_ERROR_ENUM_FAILED_TO_LOCK,"no pid file")
 * >>> desc->transaction_stop(backend)
 * <<< ::Finished()
 *
 * It is *not* called for non-threaded backends, as multiple processes
 * would be inherently racy.
 */
void
pk_backend_transaction_start (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));

	/* no transaction setup is perfectly fine */
	if (backend->priv->desc->transaction_start == NULL) {
		g_debug ("no transaction start vfunc");
		return;
	}

	/* run the transaction setup */
	pk_backend_transaction_reset (backend);
	backend->priv->desc->transaction_start (backend);
}

/**
 * pk_backend_transaction_stop:
 *
 * Always run for each transaction, *even* when the transaction_start()
 * vfunc fails.
 *
 * This method has no return value as the ErrorCode should have already
 * been set.
 */
void
pk_backend_transaction_stop (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));

	/* no transaction setup is perfectly fine */
	if (backend->priv->desc->transaction_stop == NULL) {
		g_debug ("no transaction stop vfunc");
		goto out;
	}

	/* run the transaction setup */
	pk_backend_transaction_reset (backend);
	backend->priv->desc->transaction_stop (backend);
out:
	return;
}

/**
 * pk_backend_transaction_reset:
 */
void
pk_backend_transaction_reset (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));

	/* no transaction setup is perfectly fine */
	if (backend->priv->desc->transaction_reset == NULL) {
		g_debug ("no transaction reset vfunc");
		goto out;
	}

	/* run the transaction setup */
	backend->priv->desc->transaction_reset (backend);
out:
	return;
}

/**
 * pk_backend_finished_delay:
 *
 * We can call into this function if we *know* it's safe.
 **/
static gboolean
pk_backend_finished_delay (gpointer data)
{
	PkBackend *backend = PK_BACKEND (data);

	/* this wasn't set otherwise, assume success */
	if (backend->priv->exit == PK_EXIT_ENUM_UNKNOWN)
		pk_backend_set_exit_code (backend, PK_EXIT_ENUM_SUCCESS);

	/* emit */
	g_signal_emit (backend, signals[SIGNAL_FINISHED], 0, backend->priv->exit);
	backend->priv->signal_finished = 0;
	return FALSE;
}

/**
 * pk_backend_finished:
 **/
gboolean
pk_backend_finished (PkBackend *backend)
{
	const gchar *role_text;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* check we are not doing Init() */
	if (backend->priv->during_initialize) {
		g_warning ("finished during init");
		return FALSE;
	}

	/* safe to check now */
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* find out what we just did */
	role_text = pk_role_enum_to_string (backend->priv->role);
	g_debug ("finished role %s", role_text);

	/* are we trying to finish in init? */
	if (backend->priv->during_initialize) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "%s can't call pk_backend_finished in backend_initialize!", role_text);
		return FALSE;
	}

	/* check we have not already finished */
	if (backend->priv->finished) {
		g_warning ("already finished");
		return FALSE;
	}

	/* ensure threaded backends get stop vfuncs fired */
	if (backend->priv->thread != NULL)
		pk_backend_transaction_stop (backend);

	/* check we got a Package() else the UI will suck */
	if (!backend->priv->set_error &&
	    !backend->priv->has_sent_package &&
	    (backend->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES ||
	     backend->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES ||
	     backend->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send a Package() for %s!", role_text);
	}

	/* ensure the same number of ::Files() were sent as packages for DownloadPackages */
	if (!backend->priv->set_error &&
	    backend->priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    backend->priv->download_files == 0) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send multiple Files() for each package_id!");
	}

	/* if we set an error code notifier, clear */
	if (backend->priv->signal_error_timeout != 0) {
		g_source_remove (backend->priv->signal_error_timeout);
		backend->priv->signal_error_timeout = 0;
	}

	/* check we sent at least one status calls */
	if (backend->priv->set_error == FALSE &&
	    backend->priv->status == PK_STATUS_ENUM_SETUP) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send status <value> signals for %s!", role_text);
		g_warning ("GUI will remain unchanged!");
	}

	/* emulate the last finished package if not done already */
	pk_backend_package_emulate_finished (backend);

	/* make any UI insensitive */
	pk_backend_set_allow_cancel (backend, FALSE);

	/* mark as finished for the UI that might only be watching status */
	pk_backend_set_status (backend, PK_STATUS_ENUM_FINISHED);

	/* we can't ever be re-used */
	backend->priv->finished = TRUE;

	/* we have to run this idle as the command may finish before the transaction
	 * has been sent to the client. I love async... */
	g_debug ("adding finished %p to timeout loop", backend);
	backend->priv->signal_finished = g_timeout_add (PK_BACKEND_FINISHED_TIMEOUT_GRACE,
							pk_backend_finished_delay, backend);
	g_source_set_name_by_id (backend->priv->signal_finished, "[PkBackend] finished");
	return TRUE;
}

/**
 * pk_backend_thread_finished_cb:
 **/
static gboolean
pk_backend_thread_finished_cb (PkBackend *backend)
{
	pk_backend_finished (backend);
	return FALSE;
}

/**
 * pk_backend_thread_finished:
 **/
void
pk_backend_thread_finished (PkBackend *backend)
{
	guint idle_id;
	idle_id = g_idle_add ((GSourceFunc) pk_backend_thread_finished_cb, backend);
	g_source_set_name_by_id (idle_id, "[PkBackend] finished");
}

/**
 * pk_backend_bool_to_string:
 */
const gchar *
pk_backend_bool_to_string (gboolean value)
{
	if (value)
		return "yes";
	return "no";
}

/**
 * pk_backend_not_implemented_yet:
 **/
gboolean
pk_backend_not_implemented_yet (PkBackend *backend, const gchar *method)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (method != NULL, FALSE);
	g_return_val_if_fail (backend->priv->opened != FALSE, FALSE);

	/* this function is only valid when we have a running transaction */
	if (backend->priv->transaction_id != NULL)
		g_warning ("only valid when we have a running transaction");
	pk_backend_error_code (backend, PK_ERROR_ENUM_NOT_SUPPORTED, "the method '%s' is not implemented yet", method);
	/* don't wait, do this now */
	pk_backend_finished_delay (backend);
	return TRUE;
}

/**
 * pk_backend_is_online:
 **/
gboolean
pk_backend_is_online (PkBackend *backend)
{
	PkNetworkEnum state;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	state = pk_network_get_network_state (backend->priv->network);
	if (state == PK_NETWORK_ENUM_ONLINE ||
	    state == PK_NETWORK_ENUM_MOBILE ||
	    state == PK_NETWORK_ENUM_WIFI ||
	    state == PK_NETWORK_ENUM_WIRED)
		return TRUE;
	return FALSE;
}

/**
 * pk_backend_use_background:
 **/
gboolean
pk_backend_use_background (PkBackend *backend)
{
	gboolean ret;

	/* check we are allowed */
	ret = pk_conf_get_bool (backend->priv->conf, "UseIdleBandwidth");
	if (!ret)
		return FALSE;

	/* the session has set it one way or the other */
	if (backend->priv->background == PK_HINT_ENUM_TRUE)
		return TRUE;
	if (backend->priv->background == PK_HINT_ENUM_FALSE)
		return FALSE;

	/* use a metric to try to guess a correct value */
	if (backend->priv->role == PK_ROLE_ENUM_GET_UPDATES ||
	    backend->priv->role == PK_ROLE_ENUM_REFRESH_CACHE)
		return TRUE;
	return FALSE;
}

/* simple helper to work around the GThread one pointer limit */
typedef struct {
	PkBackend		*backend;
	PkBackendThreadFunc	 func;
} PkBackendThreadHelper;

/**
 * pk_backend_thread_setup:
 **/
static gpointer
pk_backend_thread_setup (gpointer thread_data)
{
	gboolean ret;
	PkBackendThreadHelper *helper = (PkBackendThreadHelper *) thread_data;

	/* call setup */
	pk_backend_transaction_start (helper->backend);

	/* run original function */
	ret = helper->func (helper->backend);
	if (!ret) {
		g_debug ("transaction setup failed, going straight to finished");
		pk_backend_transaction_stop (helper->backend);
	}

	/* destroy helper */
	g_object_unref (helper->backend);
	g_free (helper);

	/* no return value */
	return NULL;
}

/**
 * pk_backend_thread_create:
 *
 * @func: (scope call):
 **/
gboolean
pk_backend_thread_create (PkBackend *backend, PkBackendThreadFunc func)
{
	gboolean ret = TRUE;
	PkBackendThreadHelper *helper = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (backend->priv->thread != NULL) {
		g_warning ("already has thread");
		return FALSE;
	}

	/* backend isn't threadsafe */
	if (!backend->priv->use_threads) {
		g_warning ("not using threads, so daemon will block");
		ret = func (backend);
		goto out;
	}

	/* create a helper object to allow us to call a _setup() function */
	helper = g_new0 (PkBackendThreadHelper, 1);
	helper->backend = g_object_ref (backend);
	helper->func = func;

	/* create a thread */
#if GLIB_CHECK_VERSION(2,31,0)
	backend->priv->thread = g_thread_new ("PK-Backend",
					      pk_backend_thread_setup,
					      helper);
#else
	backend->priv->thread = g_thread_create (pk_backend_thread_setup,
						 helper,
						 FALSE,
						 NULL);
#endif
	if (backend->priv->thread == NULL) {
		g_warning ("failed to create thread");
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * pk_backend_get_name:
 **/
gchar *
pk_backend_get_name (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->opened != FALSE, NULL);
	return g_strdup (backend->priv->name);
}

/**
 * pk_backend_get_description:
 **/
gchar *
pk_backend_get_description (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->opened != FALSE, NULL);
	return g_strdup (backend->priv->desc->description);
}

/**
 * pk_backend_get_author:
 **/
gchar *
pk_backend_get_author (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->opened != FALSE, NULL);
	return g_strdup (backend->priv->desc->author);
}

/**
 * pk_backend_accept_eula:
 */
gboolean
pk_backend_accept_eula (PkBackend *backend, const gchar *eula_id)
{
	gpointer present;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_ACCEPT_EULA);

	g_debug ("eula_id %s", eula_id);
	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL) {
		g_debug ("already added %s to accepted list", eula_id);
		return FALSE;
	}
	g_hash_table_insert (backend->priv->eulas, g_strdup (eula_id), GINT_TO_POINTER (1));
	return TRUE;
}

/**
 * pk_backend_is_eula_valid:
 */
gboolean
pk_backend_is_eula_valid (PkBackend *backend, const gchar *eula_id)
{
	gpointer present;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL)
		return TRUE;
	return FALSE;
}

/**
 * pk_backend_is_eula_valid:
 */
gchar *
pk_backend_get_accepted_eula_string (PkBackend *backend)
{
	GString *string;
	gchar *result = NULL;
	GList *keys = NULL;
	GList *l;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* optimise for the common case */
	if (g_hash_table_size (backend->priv->eulas) == 0)
		goto out;

	/* create a string of the accepted EULAs */
	string = g_string_new ("");
	keys = g_hash_table_get_keys (backend->priv->eulas);
	for (l=keys; l != NULL; l=l->next)
		g_string_append_printf (string, "%s;", (const gchar *) l->data);

	/* remove the trailing ';' */
	g_string_set_size (string, string->len -1);
	result = g_string_free (string, FALSE);
out:
	g_list_free (keys);
	return result;
}

/**
 * pk_backend_file_monitor_changed_cb:
 **/
static void
pk_backend_file_monitor_changed_cb (GFileMonitor *monitor,
				    GFile *file,
				    GFile *other_file,
				    GFileMonitorEvent event_type,
				    PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_debug ("config file changed");
	backend->priv->file_changed_func (backend, backend->priv->file_changed_data);
}

/**
 * pk_backend_watch_file:
 *
 * @func: (scope call):
 */
gboolean
pk_backend_watch_file (PkBackend *backend,
		       const gchar *filename,
		       PkBackendFileChanged func,
		       gpointer data)
{
	gboolean ret = FALSE;
	GError *error = NULL;
	GFile *file = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (backend->priv->file_changed_func != NULL) {
		g_warning ("already set");
		goto out;
	}

	/* monitor config files for changes */
	file = g_file_new_for_path (filename);
	backend->priv->monitor = g_file_monitor_file (file,
						      G_FILE_MONITOR_NONE,
						      NULL,
						      &error);
	if (backend->priv->monitor == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   filename,
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* success */
	ret = TRUE;
	g_signal_connect (backend->priv->monitor, "changed",
			  G_CALLBACK (pk_backend_file_monitor_changed_cb), backend);
	backend->priv->file_changed_func = func;
	backend->priv->file_changed_data = data;
out:
	if (file != NULL)
		g_object_unref (file);
	return ret;
}

/**
 * pk_backend_get_property:
 **/
static void
pk_backend_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkBackend *backend = PK_BACKEND (object);
	PkBackendPrivate *priv = backend->priv;

	switch (prop_id) {
	case PROP_BACKGROUND:
		g_value_set_uint (value, priv->background);
		break;
	case PROP_INTERACTIVE:
		g_value_set_uint (value, priv->interactive);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	case PROP_ROLE:
		g_value_set_uint (value, priv->role);
		break;
	case PROP_TRANSACTION_ID:
		g_value_set_string (value, priv->transaction_id);
		break;
	case PROP_SPEED:
		g_value_set_uint (value, priv->speed);
		break;
	case PROP_PERCENTAGE:
		g_value_set_uint (value, priv->percentage);
		break;
	case PROP_SUBPERCENTAGE:
		g_value_set_uint (value, priv->subpercentage);
		break;
	case PROP_REMAINING:
		g_value_set_uint (value, priv->remaining);
		break;
	case PROP_UID:
		g_value_set_uint (value, priv->uid);
		break;
	case PROP_CMDLINE:
		g_value_set_string (value, priv->cmdline);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_backend_set_property:
 **/
static void
pk_backend_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkBackend *backend = PK_BACKEND (object);
	PkBackendPrivate *priv = backend->priv;

	switch (prop_id) {
	case PROP_BACKGROUND:
		priv->background = g_value_get_uint (value);
		break;
	case PROP_INTERACTIVE:
		priv->interactive = g_value_get_uint (value);
		break;
	case PROP_STATUS:
		priv->status = g_value_get_uint (value);
		break;
	case PROP_ROLE:
		priv->role = g_value_get_uint (value);
		break;
	case PROP_TRANSACTION_ID:
		g_free (priv->transaction_id);
		priv->transaction_id = g_value_dup_string (value);
		g_debug ("setting backend tid as %s", priv->transaction_id);
		break;
	case PROP_SPEED:
		priv->speed = g_value_get_uint (value);
		break;
	case PROP_PERCENTAGE:
		priv->percentage = g_value_get_uint (value);
		break;
	case PROP_SUBPERCENTAGE:
		priv->subpercentage = g_value_get_uint (value);
		break;
	case PROP_REMAINING:
		priv->remaining = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_backend_finalize:
 **/
static void
pk_backend_finalize (GObject *object)
{
	PkBackend *backend;
	g_return_if_fail (PK_IS_BACKEND (object));
	backend = PK_BACKEND (object);

	pk_backend_reset (backend);
	g_free (backend->priv->proxy_http);
	g_free (backend->priv->proxy_https);
	g_free (backend->priv->proxy_ftp);
	g_free (backend->priv->proxy_socks);
	g_free (backend->priv->no_proxy);
	g_free (backend->priv->pac);
	g_free (backend->priv->root);
	g_free (backend->priv->cmdline);
	g_free (backend->priv->name);
	g_free (backend->priv->locale);
	g_free (backend->priv->frontend_socket);
	g_free (backend->priv->transaction_id);
	g_object_unref (backend->priv->results);
	g_object_unref (backend->priv->time);
	g_object_unref (backend->priv->network);
	g_object_unref (backend->priv->store);
	g_object_unref (backend->priv->conf);
	g_hash_table_destroy (backend->priv->eulas);

	if (backend->priv->handle != NULL)
		g_module_close (backend->priv->handle);
	g_debug ("parent_class->finalize");
	G_OBJECT_CLASS (pk_backend_parent_class)->finalize (object);
}

/**
 * pk_backend_class_init:
 **/
static void
pk_backend_class_init (PkBackendClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_finalize;
	object_class->get_property = pk_backend_get_property;
	object_class->set_property = pk_backend_set_property;

	/**
	 * PkBackend:background:
	 */
	pspec = g_param_spec_uint ("background", NULL, NULL,
				   PK_HINT_ENUM_FALSE, PK_HINT_ENUM_UNSET, PK_HINT_ENUM_UNSET,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BACKGROUND, pspec);

	/**
	 * PkBackend:interactive:
	 */
	pspec = g_param_spec_uint ("interactive", NULL, NULL,
				   PK_HINT_ENUM_FALSE, PK_HINT_ENUM_UNSET, PK_HINT_ENUM_UNSET,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	/**
	 * PkBackend:status:
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, G_MAXUINT, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkBackend:role:
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, G_MAXUINT, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkBackend:transaction-id:
	 */
	pspec = g_param_spec_string ("transaction-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TRANSACTION_ID, pspec);

	/**
	 * PkBackend:speed:
	 */
	pspec = g_param_spec_uint ("speed", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SPEED, pspec);

	/**
	 * PkBackend:percentage:
	 */
	pspec = g_param_spec_uint ("percentage", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);

	/**
	 * PkBackend:subpercentage:
	 */
	pspec = g_param_spec_uint ("subpercentage", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUBPERCENTAGE, pspec);

	/**
	 * PkBackend:remaining:
	 */
	pspec = g_param_spec_uint ("remaining", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REMAINING, pspec);

	/**
	 * PkBackend:uid:
	 */
	pspec = g_param_spec_uint ("uid", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_UID, pspec);

	/**
	 * PkBackend:cmdline:
	 */
	pspec = g_param_spec_string ("cmdline", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_CMDLINE, pspec);

	/* properties */
	signals[SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_CHANGE_TRANSACTION_DATA] =
		g_signal_new ("change-transaction-data",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	/* objects */
	signals[SIGNAL_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_DETAILS] =
		g_signal_new ("details",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_DISTRO_UPGRADE] =
		g_signal_new ("distro-upgrade",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_EULA_REQUIRED] =
		g_signal_new ("eula-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_MEDIA_CHANGE_REQUIRED] =
		g_signal_new ("media-change-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_CATEGORY] =
		g_signal_new ("category",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[SIGNAL_ITEM_PROGRESS] =
		g_signal_new ("item-progress",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_UINT,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkBackendPrivate));
}

/**
 * pk_backend_reset:
 **/
gboolean
pk_backend_reset (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* we can't reset when we are running */
	if (backend->priv->status == PK_STATUS_ENUM_RUNNING) {
		g_warning ("cannot reset %s when running", backend->priv->transaction_id);
		return FALSE;
	}

	/* do finish now, as we might be unreffing quickly */
	if (backend->priv->signal_finished != 0) {
		g_source_remove (backend->priv->signal_finished);
		g_debug ("doing unref quickly delay");
		pk_backend_finished_delay (backend);
	}

	/* if we set an error code notifier, clear */
	if (backend->priv->signal_error_timeout != 0) {
		g_source_remove (backend->priv->signal_error_timeout);
		backend->priv->signal_error_timeout = 0;
	}

	if (backend->priv->last_package != NULL) {
		g_object_unref (backend->priv->last_package);
		backend->priv->last_package = NULL;
	}
	backend->priv->set_error = FALSE;
	backend->priv->set_signature = FALSE;
	backend->priv->set_eula = FALSE;
	backend->priv->finished = FALSE;
	backend->priv->has_sent_package = FALSE;
	backend->priv->download_files = 0;
	backend->priv->thread = NULL;
	backend->priv->last_package = NULL;
	backend->priv->allow_cancel = PK_HINT_ENUM_UNSET;
	backend->priv->status = PK_STATUS_ENUM_UNKNOWN;
	backend->priv->exit = PK_EXIT_ENUM_UNKNOWN;
	backend->priv->role = PK_ROLE_ENUM_UNKNOWN;
	backend->priv->transaction_role = PK_ROLE_ENUM_UNKNOWN;
	backend->priv->remaining = 0;
	backend->priv->percentage = PK_BACKEND_PERCENTAGE_DEFAULT;
	backend->priv->subpercentage = PK_BACKEND_PERCENTAGE_DEFAULT;
	backend->priv->speed = 0;
	pk_store_reset (backend->priv->store);
	pk_time_reset (backend->priv->time);

	/* unref then create rather then set zero size, as another object
	 * might have a reference on the data */
	g_object_unref (backend->priv->results);
	backend->priv->results = pk_results_new ();

	/* clear monitor */
	if (backend->priv->monitor != NULL) {
		g_object_unref (backend->priv->monitor);
		backend->priv->monitor = NULL;
	}

	return TRUE;
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));

	/* call into the backend */
	backend->priv->desc->cancel (backend);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->download_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_DOWNLOAD_PACKAGES);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_string (backend->priv->store, "directory", directory);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->download_packages (backend, package_ids, directory);
}

/**
 * pk_pk_backend_get_categories:
 */
void
pk_backend_get_categories (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_categories != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_CATEGORIES);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_categories (backend);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_depends != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_DEPENDS);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "recursive", recursive);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_depends (backend, filters, package_ids, recursive);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_details != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_DETAILS);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_details (backend, package_ids);
}

/**
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_distro_upgrades != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_distro_upgrades (backend);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_files != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_FILES);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_files (backend, package_ids);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_requires != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_REQUIRES);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "recursive", recursive);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_requires (backend, filters, package_ids, recursive);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_update_detail != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_update_detail (backend, package_ids);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_updates != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_UPDATES);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_updates (backend, filters);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->install_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_INSTALL_PACKAGES);
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_set_bool (backend, "hint:simulate", FALSE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->install_packages (backend, only_trusted, package_ids);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkSigTypeEnum type, const gchar *key_id, const gchar *package_id)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->install_signature != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_INSTALL_SIGNATURE);
	pk_store_set_string (backend->priv->store, "key_id", key_id);
	pk_store_set_string (backend->priv->store, "package_id", package_id);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->install_signature (backend, type, key_id, package_id);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->install_files != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_INSTALL_FILES);
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_store_set_strv (backend->priv->store, "full_paths", full_paths);
	pk_backend_set_bool (backend, "hint:simulate", FALSE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->install_files (backend, only_trusted, full_paths);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->refresh_cache != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_REFRESH_CACHE);
	pk_store_set_bool (backend->priv->store, "force", force);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->refresh_cache (backend, force);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->remove_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_REMOVE_PACKAGES);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "allow_deps", allow_deps);
	pk_store_set_bool (backend->priv->store, "autoremove", autoremove);
	pk_backend_set_bool (backend, "hint:simulate", FALSE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->remove_packages (backend, package_ids, allow_deps, autoremove);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->resolve != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_RESOLVE);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->resolve (backend, filters, package_ids);
}

/**
 * pk_backend_rollback:
 */
void
pk_backend_rollback (PkBackend *backend, const gchar *transaction_id)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->rollback != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_ROLLBACK);
	pk_store_set_string (backend->priv->store, "transaction_id", transaction_id);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->rollback (backend, transaction_id);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_details != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SEARCH_DETAILS);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->search_details (backend, filters, values);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_files != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SEARCH_FILE);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->search_files (backend, filters, values);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_groups != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SEARCH_GROUP);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->search_groups (backend, filters, values);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_names != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SEARCH_NAME);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_strv (backend->priv->store, "search", values);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->search_names (backend, filters, values);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->update_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_UPDATE_PACKAGES);
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_set_bool (backend, "hint:simulate", FALSE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->update_packages (backend, only_trusted, package_ids);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->update_system != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_UPDATE_SYSTEM);
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_backend_set_bool (backend, "hint:simulate", FALSE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->update_system (backend, only_trusted);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_repo_list != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_REPO_LIST);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_repo_list (backend, filters);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *repo_id, gboolean enabled)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repo_enable != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_REPO_ENABLE);
	pk_store_set_string (backend->priv->store, "repo_id", repo_id);
	pk_store_set_bool (backend->priv->store, "enabled", enabled);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->repo_enable (backend, repo_id, enabled);
}

/**
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repo_set_data != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_REPO_SET_DATA);
	pk_store_set_string (backend->priv->store, "repo_id", repo_id);
	pk_store_set_string (backend->priv->store, "parameter", parameter);
	pk_store_set_string (backend->priv->store, "value", value);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->repo_set_data (backend, repo_id, parameter, value);
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->what_provides != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_WHAT_PROVIDES);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_store_set_uint (backend->priv->store, "provides", provides);
	pk_store_set_strv (backend->priv->store, "search", values);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->what_provides (backend, filters, provides, values);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_GET_PACKAGES);
	pk_store_set_uint (backend->priv->store, "filters", filters);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->get_packages (backend, filters);
}

/**
 * pk_backend_simulate_install_files:
 */
void
pk_backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->simulate_install_files != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SIMULATE_INSTALL_FILES);
	pk_store_set_strv (backend->priv->store, "full_paths", full_paths);
	pk_backend_set_bool (backend, "hint:simulate", TRUE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->simulate_install_files (backend, full_paths);
}

/**
 * pk_backend_simulate_install_packages:
 */
void
pk_backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->simulate_install_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_set_bool (backend, "hint:simulate", TRUE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->simulate_install_packages (backend, package_ids);
}

/**
 * pk_backend_simulate_remove_packages:
 */
void
pk_backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean	 autoremove)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->simulate_remove_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_store_set_bool (backend->priv->store, "autoremove", autoremove);
	pk_backend_set_bool (backend, "hint:simulate", TRUE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->simulate_remove_packages (backend, package_ids, autoremove);
}

/**
 * pk_backend_simulate_update_packages:
 */
void
pk_backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->simulate_update_packages != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES);
	pk_store_set_strv (backend->priv->store, "package_ids", package_ids);
	pk_backend_set_bool (backend, "hint:simulate", TRUE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->simulate_update_packages (backend, package_ids);
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->upgrade_system != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_UPGRADE_SYSTEM);
	pk_store_set_string (backend->priv->store, "distro_id", distro_id);
	pk_store_set_uint (backend->priv->store, "upgrade_kind", upgrade_kind);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->upgrade_system (backend, distro_id, upgrade_kind);
}

/**
 * pk_backend_repair_system:
 */
void
pk_backend_repair_system (PkBackend *backend, gboolean only_trusted)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repair_system != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_REPAIR_SYSTEM);
	pk_store_set_bool (backend->priv->store, "only_trusted", only_trusted);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->repair_system (backend, only_trusted);
}

/**
 * pk_backend_simulate_repair_system:
 */
void
pk_backend_simulate_repair_system (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repair_system != NULL);
	pk_backend_set_role_internal (backend, PK_ROLE_ENUM_SIMULATE_REPAIR_SYSTEM);
	pk_backend_set_bool (backend, "hint:simulate", TRUE);
	pk_backend_transaction_reset (backend);
	backend->priv->desc->simulate_repair_system (backend);
}

/**
 * pk_backend_init:
 **/
static void
pk_backend_init (PkBackend *backend)
{
	PkConf *conf;

	backend->priv = PK_BACKEND_GET_PRIVATE (backend);
	backend->priv->conf = pk_conf_new ();
	backend->priv->results = pk_results_new ();
	backend->priv->store = pk_store_new ();
	backend->priv->time = pk_time_new ();
	backend->priv->network = pk_network_new ();
	backend->priv->eulas = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* do we use time estimation? */
	conf = pk_conf_new ();
	backend->priv->use_time = pk_conf_get_bool (conf, "UseRemainingTimeEstimation");
	backend->priv->use_threads = pk_conf_get_bool (conf, "UseThreadsInBackend");
	g_object_unref (conf);

	/* initialize keep_environment once */
	backend->priv->keep_environment = FALSE;

	pk_backend_reset (backend);
}

/**
 * pk_backend_new:
 * Return value: A new backend class backend.
 **/
PkBackend *
pk_backend_new (void)
{
	if (pk_backend_object != NULL) {
		g_object_ref (pk_backend_object);
	} else {
		pk_backend_object = g_object_new (PK_TYPE_BACKEND, NULL);
		g_object_add_weak_pointer (pk_backend_object, &pk_backend_object);
	}
	return PK_BACKEND (pk_backend_object);
}

