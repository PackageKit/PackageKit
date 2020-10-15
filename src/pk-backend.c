/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2014 Richard Hughes <richard@hughsie.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <gmodule.h>
#include <packagekit-glib2/pk-offline-private.h>
#include <packagekit-glib2/pk-package-id.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-common.h>

#include "pk-backend.h"
#include "pk-shared.h"

#define PK_BACKEND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND, PkBackendPrivate))

/**
 * PK_BACKEND_PERCENTAGE_DEFAULT:
 *
 * The default percentage value, should never be emitted, but should be
 * used so we can work out if a backend just calls NoPercentageUpdates
 */
#define PK_BACKEND_PERCENTAGE_DEFAULT		102

typedef struct {
	const gchar	*description;
	const gchar	*author;
	void		(*initialize)			(GKeyFile		*conf,
							 PkBackend	*backend);
	void		(*destroy)			(PkBackend	*backend);
	PkBitfield	(*get_groups)			(PkBackend	*backend);
	PkBitfield	(*get_filters)			(PkBackend	*backend);
	PkBitfield	(*get_roles)			(PkBackend	*backend);
	PkBitfield	(*get_provides)			(PkBackend	*backend);
	gchar		**(*get_mime_types)		(PkBackend	*backend);
	gboolean	(*supports_parallelization)	(PkBackend	*backend);
	void		(*job_start)			(PkBackend	*backend,
							 PkBackendJob	*job);
	void		(*job_stop)			(PkBackend	*backend,
							 PkBackendJob	*job);
	void		(*cancel)			(PkBackend	*backend,
							 PkBackendJob	*job);
	void		(*download_packages)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids,
							 const gchar	*directory);
	void		(*get_categories)		(PkBackend	*backend,
							 PkBackendJob	*job);
	void		(*depends_on)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
	void		(*get_details)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids);
	void		(*get_details_local)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**files);
	void		(*get_files_local)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**files);
	void		(*get_distro_upgrades)		(PkBackend	*backend,
							 PkBackendJob	*job);
	void		(*get_files)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids);
	void		(*get_packages)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters);
	void		(*get_repo_list)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters);
	void		(*required_by)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**package_ids,
							 gboolean	 recursive);
	void		(*get_update_detail)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gchar		**package_ids);
	void		(*get_updates)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters);
	void		(*install_files)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**full_paths);
	void		(*install_packages)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
	void		(*install_signature)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkSigTypeEnum	 type,
							 const gchar	*key_id,
							 const gchar	*package_id);
	void		(*refresh_cache)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 gboolean	 force);
	void		(*remove_packages)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids,
							 gboolean	 allow_deps,
							 gboolean	 autoremove);
	void		(*repo_enable)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 const gchar	*repo_id,
							 gboolean	 enabled);
	void		(*repo_set_data)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 const gchar	*repo_id,
							 const gchar	*parameter,
							 const gchar	*value);
	void		(*repo_remove)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 const gchar	*repo_id,
							 gboolean	 autoremove);
	void		(*resolve)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**packages);
	void		(*search_details)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*search_files)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*search_groups)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*search_names)			(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*update_packages)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 gchar		**package_ids);
	void		(*what_provides)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 filters,
							 gchar		**values);
	void		(*upgrade_system)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags,
							 const gchar	*distro_id,
							 PkUpgradeKindEnum upgrade_kind);
	void		(*repair_system)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 PkBitfield	 transaction_flags);
	void		(*import_pubkey)		(PkBackend	*backend,
							 PkBackendJob	*job,
							 const gchar	*key_file);							 
} PkBackendDesc;

struct PkBackendPrivate
{
	gboolean		 during_initialize;
	gboolean		 loaded;
	gchar			*name;
	gpointer		 file_changed_data;
	GHashTable		*eulas;
	GMutex			 eulas_mutex;
	GModule			*handle;
	PkBackendDesc		*desc;
	PkBackendFileChanged	 file_changed_func;
	PkBitfield		 roles;
	GKeyFile		*conf;
	GFileMonitor		*monitor;
	gboolean		 backend_roles_set;
	gpointer		 user_data;
	GHashTable		*thread_hash;
	GMutex			 thread_hash_mutex;
	gboolean		 transaction_in_progress;
	guint			 transaction_inhibit_end_idle_id;
	guint			 repo_list_changed_id;
	guint			 installed_db_changed_id;
	guint			 updates_changed_id;
};

G_DEFINE_TYPE (PkBackend, pk_backend, G_TYPE_OBJECT)

enum {
	SIGNAL_REPO_LIST_CHANGED,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->loaded, PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (pk_is_thread_default (), PK_GROUP_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->priv->desc->get_groups == NULL)
		return PK_GROUP_ENUM_UNKNOWN;
	return backend->priv->desc->get_groups (backend);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->loaded, NULL);
	g_return_val_if_fail (pk_is_thread_default (), NULL);

	/* not compulsory */
	if (backend->priv->desc->get_mime_types == NULL)
		return g_new0 (gchar *, 1);
	return backend->priv->desc->get_mime_types (backend);
}

gboolean
pk_backend_supports_parallelization (PkBackend	*backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* not compulsory */
	if (backend->priv->desc->supports_parallelization == NULL)
		return FALSE;
	return backend->priv->desc->supports_parallelization (backend);
}

void
pk_backend_thread_start (PkBackend *backend, PkBackendJob *job, gpointer func)
{
	GMutex *mutex;
	gboolean ret;

	g_mutex_lock (&backend->priv->thread_hash_mutex);
	mutex = g_hash_table_lookup (backend->priv->thread_hash, func);
	if (mutex == NULL) {
		mutex = g_new0 (GMutex, 1);
		g_mutex_init (mutex);
		g_hash_table_insert (backend->priv->thread_hash, func, mutex);
	}
	g_mutex_unlock (&backend->priv->thread_hash_mutex);

	ret = g_mutex_trylock (mutex);
	if (!ret) {
		pk_backend_job_set_status (job,
					   PK_STATUS_ENUM_WAITING_FOR_LOCK);
		g_mutex_lock (mutex);
	}
}

void
pk_backend_thread_stop (PkBackend *backend, PkBackendJob *job, gpointer func)
{
	GMutex *mutex;
	mutex = g_hash_table_lookup (backend->priv->thread_hash, func);
	g_assert (mutex);
	g_mutex_unlock (mutex);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_FILTER_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->loaded, PK_FILTER_ENUM_UNKNOWN);
	g_return_val_if_fail (pk_is_thread_default (), PK_FILTER_ENUM_UNKNOWN);

	/* not compulsory */
	if (backend->priv->desc->get_filters == NULL)
		return PK_FILTER_ENUM_UNKNOWN;
	return backend->priv->desc->get_filters (backend);
}

PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	PkBitfield roles = backend->priv->roles;
	PkBackendDesc *desc;

	g_return_val_if_fail (PK_IS_BACKEND (backend), PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (backend->priv->loaded, PK_ROLE_ENUM_UNKNOWN);
	g_return_val_if_fail (pk_is_thread_default (), PK_ROLE_ENUM_UNKNOWN);

	/* optimise - we only skip here if we already loaded backend settings,
	 * so we don't override preexisting settings (e.g. by plugins) */
	if (backend->priv->backend_roles_set)
		goto out;

	/* not compulsory, but use it if we've got it */
	if (backend->priv->desc->get_roles != NULL) {
		backend->priv->roles = backend->priv->desc->get_roles (backend);
		pk_bitfield_add (backend->priv->roles, PK_ROLE_ENUM_GET_OLD_TRANSACTIONS);
		goto out;
	}

	/* lets reduce pointer dereferences... */
	desc = backend->priv->desc;
	if (desc->cancel != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_CANCEL);
	if (desc->depends_on != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_DEPENDS_ON);
	if (desc->get_details != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DETAILS);
	if (desc->get_details_local != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DETAILS_LOCAL);
	if (desc->get_files_local != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_FILES_LOCAL);
	if (desc->get_files != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_FILES);
	if (desc->required_by != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REQUIRED_BY);
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
	if (desc->get_repo_list != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_REPO_LIST);
	if (desc->repo_enable != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_ENABLE);
	if (desc->repo_set_data != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_SET_DATA);
	if (desc->repo_remove != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPO_REMOVE);
	if (desc->get_distro_upgrades != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	if (desc->get_categories != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_GET_CATEGORIES);
	if (desc->upgrade_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_UPGRADE_SYSTEM);
	if (desc->repair_system != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_REPAIR_SYSTEM);
	if (desc->import_pubkey != NULL)
		pk_bitfield_add (roles, PK_ROLE_ENUM_IMPORT_PUBKEY);
	pk_bitfield_add (roles, PK_ROLE_ENUM_GET_OLD_TRANSACTIONS);
	backend->priv->roles = roles;

	backend->priv->backend_roles_set = TRUE;
out:
	return backend->priv->roles;
}

gboolean
pk_backend_is_implemented (PkBackend *backend, PkRoleEnum role)
{
	PkBitfield roles;
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	roles = pk_backend_get_roles (backend);
	return pk_bitfield_contain (roles, role);
}

static gchar *
pk_backend_build_library_path (PkBackend *backend, const gchar *name)
{
	gchar *path;
	g_autofree gchar *filename = NULL;
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
	path = g_build_filename ("backends", directory, filename, NULL);
	if (g_file_test (path, G_FILE_TEST_EXISTS) == FALSE) {
		g_debug ("local backend not found '%s'", path);
		g_free (path);
		path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
	}
#else
	path = g_build_filename (LIBDIR, "packagekit-backend", filename, NULL);
#endif
	g_debug ("dlopening '%s'", path);

	return path;
}

typedef gchar	*(*PkBackendGetCompatStringFunc)	(PkBackend	*backend);

/**
 * pk_backend_load:
 *
 * Responsible for initialising the external backend object.
 *
 * Typically this will involve taking database locks for exclusive package access.
 * This method should only be called from the engine, unless the backend object
 * is used in self-check code, in which case the lock and unlock will have to
 * be done manually.
 **/
gboolean
pk_backend_load (PkBackend *backend, GError **error)
{
	GModule *handle;
	gboolean ret = FALSE;
	gpointer func = NULL;
	g_autofree gchar *backend_name = NULL;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (pk_is_thread_default (), FALSE);

	/* already loaded */
	if (backend->priv->loaded) {
		g_set_error (error, 1, 0,
			     "already set name to %s",
			     backend->priv->name);
		return FALSE;
	}

	/* can we load it? */
	backend_name = g_key_file_get_string (backend->priv->conf,
					      "Daemon",
					      "DefaultBackend",
					      error);
	if (backend_name == NULL)
		return FALSE;

	/* the "hawkey" and "hif" backends are superseded by "dnf" */
	if (g_strcmp0 (backend_name, "hawkey") == 0 ||
	    g_strcmp0 (backend_name, "yum") == 0 ||
	    g_strcmp0 (backend_name, "hif") == 0) {
		g_free (backend_name);
		backend_name = g_strdup ("dnf");
	}

	g_debug ("Trying to load : %s", backend_name);
	path = pk_backend_build_library_path (backend, backend_name);
	handle = g_module_open (path, 0);
	if (handle == NULL) {
		g_set_error (error, 1, 0, "opening module %s failed : %s",
			     backend_name, g_module_error ());
		return FALSE;
	}

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
		g_module_symbol (handle, "pk_backend_depends_on", (gpointer *)&desc->depends_on);
		g_module_symbol (handle, "pk_backend_get_details", (gpointer *)&desc->get_details);
		g_module_symbol (handle, "pk_backend_get_details_local", (gpointer *)&desc->get_details_local);
		g_module_symbol (handle, "pk_backend_get_files_local", (gpointer *)&desc->get_files_local);
		g_module_symbol (handle, "pk_backend_get_distro_upgrades", (gpointer *)&desc->get_distro_upgrades);
		g_module_symbol (handle, "pk_backend_get_files", (gpointer *)&desc->get_files);
		g_module_symbol (handle, "pk_backend_get_filters", (gpointer *)&desc->get_filters);
		g_module_symbol (handle, "pk_backend_get_groups", (gpointer *)&desc->get_groups);
		g_module_symbol (handle, "pk_backend_get_mime_types", (gpointer *)&desc->get_mime_types);
		g_module_symbol (handle, "pk_backend_supports_parallelization", (gpointer *)&desc->supports_parallelization);
		g_module_symbol (handle, "pk_backend_get_packages", (gpointer *)&desc->get_packages);
		g_module_symbol (handle, "pk_backend_get_repo_list", (gpointer *)&desc->get_repo_list);
		g_module_symbol (handle, "pk_backend_required_by", (gpointer *)&desc->required_by);
		g_module_symbol (handle, "pk_backend_get_roles", (gpointer *)&desc->get_roles);
		g_module_symbol (handle, "pk_backend_get_provides", (gpointer *)&desc->get_provides);
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
		g_module_symbol (handle, "pk_backend_repo_remove", (gpointer *)&desc->repo_remove);
		g_module_symbol (handle, "pk_backend_resolve", (gpointer *)&desc->resolve);
		g_module_symbol (handle, "pk_backend_search_details", (gpointer *)&desc->search_details);
		g_module_symbol (handle, "pk_backend_search_files", (gpointer *)&desc->search_files);
		g_module_symbol (handle, "pk_backend_search_groups", (gpointer *)&desc->search_groups);
		g_module_symbol (handle, "pk_backend_search_names", (gpointer *)&desc->search_names);
		g_module_symbol (handle, "pk_backend_start_job", (gpointer *)&desc->job_start);
		g_module_symbol (handle, "pk_backend_stop_job", (gpointer *)&desc->job_stop);
		g_module_symbol (handle, "pk_backend_update_packages", (gpointer *)&desc->update_packages);
		g_module_symbol (handle, "pk_backend_what_provides", (gpointer *)&desc->what_provides);
		g_module_symbol (handle, "pk_backend_upgrade_system", (gpointer *)&desc->upgrade_system);
		g_module_symbol (handle, "pk_backend_repair_system", (gpointer *)&desc->repair_system);
		g_module_symbol (handle, "pk_backend_import_pubkey", (gpointer *)&desc->import_pubkey);

		/* get old static string data */
		ret = g_module_symbol (handle, "pk_backend_get_author", (gpointer *)&backend_vfunc);
		if (ret)
			desc->author = backend_vfunc (backend);
		ret = g_module_symbol (handle, "pk_backend_get_description", (gpointer *)&backend_vfunc);
		if (ret)
			desc->description = backend_vfunc (backend);

		/* make available */
		backend->priv->desc = desc;
	} else {
		g_module_close (handle);
		g_set_error (error, 1, 0,
			     "could not find description in plugin %s, not loading",
			     backend_name);
		return FALSE;
	}

	/* save the backend name and handle */
	g_free (backend->priv->name);
	backend->priv->name = g_strdup (backend_name);
	backend->priv->handle = handle;

	/* initialize if we can */
	if (backend->priv->desc->initialize != NULL) {
		backend->priv->during_initialize = TRUE;
		backend->priv->desc->initialize (backend->priv->conf, backend);
		backend->priv->during_initialize = FALSE;
	}
	backend->priv->loaded = TRUE;
	return TRUE;
}

/**
 * pk_backend_unload:
 *
 * Responsible for finalising the external backend object.
 *
 * Typically this will involve releasing database locks for any other access.
 * This method should only be called from the engine, unless the backend object
 * is used in self-check code, in which case it will have to be done manually.
 **/
gboolean
pk_backend_unload (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (pk_is_thread_default (), FALSE);

	if (backend->priv->loaded == FALSE) {
		g_debug ("already closed (nonfatal)");
		/* we don't return FALSE here, as the action didn't fail */
		return TRUE;
	}
	if (backend->priv->desc == NULL) {
		g_warning ("not yet loaded backend, try pk_backend_load()");
		return FALSE;
	}
	if (backend->priv->desc->destroy != NULL)
		backend->priv->desc->destroy (backend);
	backend->priv->loaded = FALSE;
	return TRUE;
}

static gboolean
pk_backend_repo_list_changed_cb (gpointer user_data)
{
	PkBackend *backend = PK_BACKEND (user_data);

	g_debug ("emitting repo-list-changed");
	g_signal_emit (backend, signals [SIGNAL_REPO_LIST_CHANGED], 0);
	backend->priv->repo_list_changed_id = 0;
	return FALSE;
}

void
pk_backend_repo_list_changed (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->loaded);

	/* already scheduled */
	if (backend->priv->repo_list_changed_id != 0)
		return;

	/* idle add */
	backend->priv->repo_list_changed_id =
		g_idle_add (pk_backend_repo_list_changed_cb, backend);

}
gboolean
pk_backend_updates_changed (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (pk_is_thread_default (), FALSE);

	g_debug ("emitting updates-changed");
	g_signal_emit (backend, signals [SIGNAL_UPDATES_CHANGED], 0);
	return TRUE;
}

static gboolean
pk_backend_finished_updates_changed_cb (gpointer data)
{
	PkBackend *backend = PK_BACKEND (data);
	pk_backend_updates_changed (backend);
	backend->priv->updates_changed_id = 0;
	return FALSE;
}

gboolean
pk_backend_updates_changed_delay (PkBackend *backend, guint timeout)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* check if we did this more than once */
	if (backend->priv->updates_changed_id != 0)
		return FALSE;

	/* schedule */
	backend->priv->updates_changed_id =
		g_timeout_add (timeout,
			       pk_backend_finished_updates_changed_cb,
			       backend);
	g_source_set_name_by_id (backend->priv->updates_changed_id,
				 "[PkBackend] updates-changed");
	return TRUE;
}

static gboolean
pk_backend_installed_db_changed_cb (gpointer user_data)
{
	PkBackend *backend = PK_BACKEND (user_data);
	g_autoptr(GError) error = NULL;

	if (!backend->priv->transaction_in_progress) {
		g_debug ("invalidating offline updates");
		if (!pk_offline_auth_invalidate (&error))
			g_warning ("failed to invalidate: %s", error->message);
	}
	backend->priv->installed_db_changed_id = 0;
	return FALSE;
}

/**
 * pk_backend_installed_db_changed:
 *
 * This method signals PackageKit to drop any internal caches that should be
 * invalidated when something external changes the package database, e.g. a
 * native command line package management tool.
 *
 * Typically, a backend would set up a file monitor and call this method when
 * the package database file has changed. To avoid invalidating caches for
 * transactions done by PackageKit itself, a backend would call
 * pk_backend_transaction_inhibit_start() before each transaction and
 * pk_backend_transaction_inhibit_end() after the transaction has finished.
 *
 * This function can be called on any thread.
 **/
void
pk_backend_installed_db_changed (PkBackend *backend)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->loaded);

	/* already scheduled */
	if (backend->priv->installed_db_changed_id != 0)
		return;

	/* idle add */
	backend->priv->installed_db_changed_id =
		g_idle_add (pk_backend_installed_db_changed_cb, backend);
}

/**
 * pk_backend_transaction_inhibit_start:
 *
 * Call this method before each transaction that touches the installed package
 * database.
 **/
void
pk_backend_transaction_inhibit_start (PkBackend *backend)
{
	backend->priv->transaction_in_progress = TRUE;
}

static gboolean
transaction_inhibit_end_idle (gpointer user_data)
{
	PkBackend *backend = user_data;

	backend->priv->transaction_in_progress = FALSE;
	backend->priv->transaction_inhibit_end_idle_id = 0;

	return G_SOURCE_REMOVE;
}

/**
 * pk_backend_transaction_inhibit_end:
 *
 * Call this method after each transaction that touches the installed package
 * database.
 **/
void
pk_backend_transaction_inhibit_end (PkBackend *backend)
{
	if (backend->priv->transaction_inhibit_end_idle_id > 0)
		g_source_remove (backend->priv->transaction_inhibit_end_idle_id);

	/* delay for 3 seconds in order to cover the 2 second rate limit
           timeout used by the gio file monitor */
	backend->priv->transaction_inhibit_end_idle_id = g_timeout_add_seconds (3,
	                                                                        transaction_inhibit_end_idle,
	                                                                        backend);
}

gboolean
pk_backend_is_transaction_inhibited (PkBackend *backend)
{
	return backend->priv->transaction_in_progress;
}

/**
 * pk_backend_start_job:
 *
 * This is called just before the threaded transaction method, and in
 * the newly created thread context. e.g.
 *
 * >>> desc->job_start(backend)
 *     (locked backend)
 * >>> desc->backend_method_we_want_to_run(backend)
 * <<< ::Package(PK_INFO_ENUM_INSTALLING,"hal;0.1.1;i386;fedora","Hardware Stuff")
 * >>> desc->job_stop(backend)
 *     (unlocked backend)
 * <<< ::Finished()
 *
 * or in the case of backend_method_we_want_to_run() failure:
 * >>> desc->job_start(backend)
 *     (locked backend)
 * >>> desc->backend_method_we_want_to_run(backend)
 * <<< ::ErrorCode(PK_ERROR_ENUM_FAILED_TO_FIND,"no package")
 * >>> desc->job_stop(backend)
 *     (unlocked backend)
 * <<< ::Finished()
 *
 * or in the case of job_start() failure:
 * >>> desc->job_start(backend)
 *     (failed to lock backend)
 * <<< ::ErrorCode(PK_ERROR_ENUM_FAILED_TO_LOCK,"no pid file")
 * >>> desc->job_stop(backend)
 * <<< ::Finished()
 *
 * It is *not* called for non-threaded backends, as multiple processes
 * would be inherently racy.
 */
void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (pk_is_thread_default ());

	/* common stuff */
	pk_backend_job_set_backend (job, backend);

	if (pk_backend_job_get_started (job)) {
		g_warning ("trying to start an already started job again");
		return;
	}

	pk_backend_job_set_started (job, TRUE);

	/* optional */
	if (backend->priv->desc->job_start != NULL)
		backend->priv->desc->job_start (backend, job);
}

/**
 * pk_backend_stop_job:
 *
 * Always run for each transaction, *even* when the job_start()
 * vfunc fails.
 *
 * This method has no return value as the ErrorCode should have already
 * been set.
 */
void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (pk_is_thread_default ());

	if (!pk_backend_job_get_started (job)) {
		g_warning ("trying to stop job, but never started it before");
		return;
	}

	pk_backend_job_set_started (job, FALSE);

	/* optional */
	if (backend->priv->desc->job_stop != NULL)
		backend->priv->desc->job_stop (backend, job);
}

const gchar *
pk_backend_bool_to_string (gboolean value)
{
	if (value)
		return "yes";
	return "no";
}

gboolean
pk_backend_is_online (PkBackend *backend)
{
	GNetworkMonitor *network_monitor;
	network_monitor = g_network_monitor_get_default ();
	return g_network_monitor_get_network_available (network_monitor);
}

/**
 * pk_backend_convert_uri:
 *
 * Our proxy variable is typically 'username:password@server:port'
 * but http_proxy expects 'http://username:password@server:port/'
 **/
gchar *
pk_backend_convert_uri (const gchar *proxy)
{
	GString *string;
	string = g_string_new (proxy);

	/* if we didn't specify a prefix, add a default one */
	if (!g_str_has_prefix (proxy, "http://") &&
	    !g_str_has_prefix (proxy, "https://") &&
	    !g_str_has_prefix (proxy, "ftp://")) {
		g_string_prepend (string, "http://");
	}

	/* if we didn't specify a trailing slash, add one */
	if (!g_str_has_suffix (proxy, "/"))
		g_string_append_c (string, '/');

	return g_string_free (string, FALSE);
}

gchar *
pk_backend_convert_uri_socks (const gchar *proxy)
{
	GString *string;
	string = g_string_new (proxy);

	/* if we didn't specify a prefix, add a default one */
	if (!g_str_has_prefix (proxy, "socks://")) {
		g_string_prepend (string, "socks://");
	}

	/* if we didn't specify a trailing slash, add one */
	if (!g_str_has_suffix (proxy, "/"))
		g_string_append_c (string, '/');

	return g_string_free (string, FALSE);
}

const gchar *
pk_backend_get_name (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->loaded, NULL);
	g_return_val_if_fail (pk_is_thread_default (), NULL);
	return backend->priv->name;
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->loaded, NULL);
	g_return_val_if_fail (pk_is_thread_default (), NULL);
	return backend->priv->desc->description;
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	g_return_val_if_fail (backend->priv->desc != NULL, NULL);
	g_return_val_if_fail (backend->priv->loaded, NULL);
	g_return_val_if_fail (pk_is_thread_default (), NULL);
	return backend->priv->desc->author;
}

void
pk_backend_accept_eula (PkBackend *backend, const gchar *eula_id)
{
	gpointer present;
	g_autoptr(GMutexLocker) locker = NULL;

	locker = g_mutex_locker_new (&backend->priv->eulas_mutex);

	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (eula_id != NULL);

	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL) {
		g_debug ("already added %s to accepted list", eula_id);
		return;
	}
	g_hash_table_insert (backend->priv->eulas, g_strdup (eula_id), GINT_TO_POINTER (1));
}

gboolean
pk_backend_is_eula_valid (PkBackend *backend, const gchar *eula_id)
{
	gpointer present;
	g_autoptr(GMutexLocker) locker = NULL;

	locker = g_mutex_locker_new (&backend->priv->eulas_mutex);

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);

	present = g_hash_table_lookup (backend->priv->eulas, eula_id);
	if (present != NULL)
		return TRUE;
	return FALSE;
}

gchar *
pk_backend_get_accepted_eula_string (PkBackend *backend)
{
	GString *string;
	GList *l;
	g_autoptr(GList) keys = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	locker = g_mutex_locker_new (&backend->priv->eulas_mutex);

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);

	/* optimise for the common case */
	if (g_hash_table_size (backend->priv->eulas) == 0)
		return NULL;

	/* create a string of the accepted EULAs */
	string = g_string_new ("");
	keys = g_hash_table_get_keys (backend->priv->eulas);
	for (l=keys; l != NULL; l=l->next)
		g_string_append_printf (string, "%s;", (const gchar *) l->data);

	/* remove the trailing ';' */
	g_string_set_size (string, string->len -1);
	return g_string_free (string, FALSE);
}

gpointer
pk_backend_get_user_data (PkBackend *backend)
{
	g_return_val_if_fail (PK_IS_BACKEND (backend), NULL);
	return backend->priv->user_data;
}

void
pk_backend_set_user_data (PkBackend *backend, gpointer user_data)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	backend->priv->user_data = user_data;
}

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

gboolean
pk_backend_watch_file (PkBackend *backend,
		       const gchar *filename,
		       PkBackendFileChanged func,
		       gpointer data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (PK_IS_BACKEND (backend), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (pk_is_thread_default (), FALSE);

	if (backend->priv->file_changed_func != NULL) {
		g_warning ("already set");
		return FALSE;
	}

	/* monitor config files for changes */
	file = g_file_new_for_path (filename);
	backend->priv->monitor = g_file_monitor_file (file,
						      G_FILE_MONITOR_NONE,
						      NULL,
						      &error);
	if (backend->priv->monitor == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   filename, error->message);
		return FALSE;
	}

	/* success */
	g_signal_connect (backend->priv->monitor, "changed",
			  G_CALLBACK (pk_backend_file_monitor_changed_cb), backend);
	backend->priv->file_changed_func = func;
	backend->priv->file_changed_data = data;
	return TRUE;
}

static void
pk_backend_finalize (GObject *object)
{
	PkBackend *backend;
	g_return_if_fail (PK_IS_BACKEND (object));
	backend = PK_BACKEND (object);

	g_free (backend->priv->name);

	g_key_file_unref (backend->priv->conf);
	g_hash_table_destroy (backend->priv->eulas);

	g_mutex_clear (&backend->priv->eulas_mutex);
	g_mutex_clear (&backend->priv->thread_hash_mutex);
	g_hash_table_unref (backend->priv->thread_hash);
	g_free (backend->priv->desc);

	if (backend->priv->monitor != NULL)
		g_object_unref (backend->priv->monitor);
	if (backend->priv->transaction_inhibit_end_idle_id > 0)
		g_source_remove (backend->priv->transaction_inhibit_end_idle_id);
	if (backend->priv->updates_changed_id != 0)
		g_source_remove (backend->priv->updates_changed_id);
	if (backend->priv->handle != NULL)
		g_module_close (backend->priv->handle);

	G_OBJECT_CLASS (pk_backend_parent_class)->finalize (object);
}

static void
pk_backend_class_init (PkBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_finalize;

	signals [SIGNAL_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkBackendPrivate));
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	GCancellable *cancellable;
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (pk_is_thread_default ());

	/* cancel */
	cancellable = pk_backend_job_get_cancellable (job);
	if (g_cancellable_is_cancelled (cancellable))
		return;
	g_cancellable_cancel (cancellable);

	/* call into the backend */
	backend->priv->desc->cancel (backend, job);
}

void
pk_backend_download_packages (PkBackend *backend,
			      PkBackendJob *job,
			      gchar **package_ids,
			      const gchar *directory)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->download_packages != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_DOWNLOAD_PACKAGES);
	pk_backend_job_set_parameters (job, g_variant_new ("(^ass)",
							   package_ids,
							   directory));
	backend->priv->desc->download_packages (backend, job, package_ids, directory);
}

void
pk_backend_get_categories (PkBackend *backend, PkBackendJob *job)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_categories != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_CATEGORIES);
	backend->priv->desc->get_categories (backend, job);
}

void
pk_backend_depends_on (PkBackend *backend,
			PkBackendJob *job,
			PkBitfield filters,
			gchar **package_ids,
			gboolean recursive)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->depends_on != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_DEPENDS_ON);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^asb)",
							   filters,
							   package_ids,
							   recursive));
	backend->priv->desc->depends_on (backend, job, filters, package_ids, recursive);
}

void
pk_backend_get_details (PkBackend *backend,
			PkBackendJob *job,
			gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_details != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_DETAILS);
	pk_backend_job_set_parameters (job, g_variant_new ("(^as)",
							   package_ids));
	backend->priv->desc->get_details (backend, job, package_ids);
}

void
pk_backend_get_details_local (PkBackend *backend,
			      PkBackendJob *job,
			      gchar **files)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_details != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_DETAILS_LOCAL);
	pk_backend_job_set_parameters (job, g_variant_new ("(^as)",
							   files));
	backend->priv->desc->get_details_local (backend, job, files);
}

void
pk_backend_get_files_local (PkBackend *backend,
			    PkBackendJob *job,
			    gchar **files)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_details != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_FILES_LOCAL);
	pk_backend_job_set_parameters (job, g_variant_new ("(^as)",
							   files));
	backend->priv->desc->get_files_local (backend, job, files);
}

void
pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_distro_upgrades != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_DISTRO_UPGRADES);
	backend->priv->desc->get_distro_upgrades (backend, job);
}

void
pk_backend_get_files (PkBackend *backend,
		      PkBackendJob *job,
		      gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_files != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_FILES);
	pk_backend_job_set_parameters (job, g_variant_new ("(^as)",
							   package_ids));
	backend->priv->desc->get_files (backend, job, package_ids);
}

void
pk_backend_required_by (PkBackend *backend,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar **package_ids,
			 gboolean recursive)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->required_by != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REQUIRED_BY);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^asb)",
							   filters,
							   package_ids,
							   recursive));
	backend->priv->desc->required_by (backend, job, filters, package_ids, recursive);
}

void
pk_backend_get_update_detail (PkBackend *backend,
			      PkBackendJob *job,
			      gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_update_detail != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	pk_backend_job_set_parameters (job, g_variant_new ("(^as)",
							   package_ids));
	backend->priv->desc->get_update_detail (backend, job, package_ids);
}

void
pk_backend_get_updates (PkBackend *backend,
			PkBackendJob *job,
			PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_updates != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_UPDATES);
	pk_backend_job_set_parameters (job, g_variant_new ("(t)",
							   filters));
	backend->priv->desc->get_updates (backend, job, filters);
}

void
pk_backend_install_packages (PkBackend *backend,
			     PkBackendJob *job,
			     PkBitfield transaction_flags,
			     gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->install_packages != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_INSTALL_PACKAGES);
	pk_backend_job_set_transaction_flags (job, transaction_flags);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   transaction_flags,
							   package_ids));
	backend->priv->desc->install_packages (backend, job, transaction_flags, package_ids);
}

void
pk_backend_install_signature (PkBackend *backend,
			      PkBackendJob *job,
			      PkSigTypeEnum type,
			      const gchar *key_id,
			      const gchar *package_id)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->install_signature != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_INSTALL_SIGNATURE);
	pk_backend_job_set_parameters (job, g_variant_new ("(ss)",
							   key_id,
							   package_id));
	backend->priv->desc->install_signature (backend, job, type, key_id, package_id);
}

void
pk_backend_install_files (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield transaction_flags,
			  gchar **full_paths)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->install_files != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_INSTALL_FILES);
	pk_backend_job_set_transaction_flags (job, transaction_flags);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   transaction_flags,
							   full_paths));
	backend->priv->desc->install_files (backend, job, transaction_flags, full_paths);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->refresh_cache != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REFRESH_CACHE);
	pk_backend_job_set_parameters (job, g_variant_new ("(b)",
							   force));
	backend->priv->desc->refresh_cache (backend, job, force);
}

void
pk_backend_remove_packages (PkBackend *backend,
			    PkBackendJob *job,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->remove_packages != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REMOVE_PACKAGES);
	pk_backend_job_set_transaction_flags (job, transaction_flags);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^asbb)",
							   transaction_flags,
							   package_ids,
							   allow_deps,
							   autoremove));
	backend->priv->desc->remove_packages (backend, job,
					      transaction_flags,
					      package_ids,
					      allow_deps,
					      autoremove);
}

void
pk_backend_resolve (PkBackend *backend,
		    PkBackendJob *job,
		    PkBitfield filters,
		    gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->resolve != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_RESOLVE);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   filters,
							   package_ids));
	backend->priv->desc->resolve (backend, job, filters, package_ids);
}

void
pk_backend_search_details (PkBackend *backend,
			   PkBackendJob *job,
			   PkBitfield filters,
			   gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_details != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_SEARCH_DETAILS);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   filters,
							   values));
	backend->priv->desc->search_details (backend, job, filters, values);
}

void
pk_backend_search_files (PkBackend *backend,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_files != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_SEARCH_FILE);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   filters,
							   values));
	backend->priv->desc->search_files (backend, job, filters, values);
}

void
pk_backend_search_groups (PkBackend *backend,
			  PkBackendJob *job,
			  PkBitfield filters,
			  gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_groups != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_SEARCH_GROUP);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   filters,
							   values));
	backend->priv->desc->search_groups (backend, job, filters, values);
}

void
pk_backend_search_names (PkBackend *backend,
			 PkBackendJob *job,
			 PkBitfield filters,
			 gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->search_names != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_SEARCH_NAME);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   filters,
							   values));
	backend->priv->desc->search_names (backend, job, filters, values);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->update_packages != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_UPDATE_PACKAGES);
	pk_backend_job_set_transaction_flags (job, transaction_flags);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   transaction_flags,
							   package_ids));
	backend->priv->desc->update_packages (backend, job, transaction_flags, package_ids);
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_repo_list != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_REPO_LIST);
	pk_backend_job_set_parameters (job, g_variant_new ("(t)",
							   filters));
	backend->priv->desc->get_repo_list (backend, job, filters);
}

void
pk_backend_repo_enable (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, gboolean enabled)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repo_enable != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REPO_ENABLE);
	pk_backend_job_set_parameters (job, g_variant_new ("(sb)",
							   repo_id,
							   enabled));
	backend->priv->desc->repo_enable (backend, job, repo_id, enabled);
}

void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repo_set_data != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REPO_SET_DATA);
	pk_backend_job_set_parameters (job, g_variant_new ("(sss)",
							   repo_id,
							   parameter,
							   value));
	backend->priv->desc->repo_set_data (backend, job, repo_id, parameter, value);
}

void
pk_backend_repo_remove (PkBackend *backend,
			PkBackendJob *job,
			PkBitfield transaction_flags,
			const gchar *repo_id,
			gboolean autoremove)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repo_remove != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REPO_REMOVE);
	pk_backend_job_set_parameters (job, g_variant_new ("(tsb)",
							   transaction_flags,
							   repo_id,
							   autoremove));
	backend->priv->desc->repo_remove (backend,
					  job,
					  transaction_flags,
					  repo_id,
					  autoremove);
}

void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job,
			  PkBitfield filters, gchar **values)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->what_provides != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_WHAT_PROVIDES);
	pk_backend_job_set_parameters (job, g_variant_new ("(t^as)",
							   filters,
							   values));
	backend->priv->desc->what_provides (backend, job, filters, values);
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->get_packages != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_GET_PACKAGES);
	pk_backend_job_set_parameters (job, g_variant_new ("(t)",
							   filters));
	backend->priv->desc->get_packages (backend, job, filters);
}

void
pk_backend_upgrade_system (PkBackend *backend,
			   PkBackendJob *job,
			   PkBitfield transaction_flags,
			   const gchar *distro_id,
			   PkUpgradeKindEnum upgrade_kind)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->upgrade_system != NULL);

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_UPGRADE_SYSTEM);
	pk_backend_job_set_transaction_flags (job, transaction_flags);
	pk_backend_job_set_parameters (job, g_variant_new ("(tsu)",
							   transaction_flags,
							   distro_id,
							   upgrade_kind));
	backend->priv->desc->upgrade_system (backend,
					     job,
					     transaction_flags,
					     distro_id,
					     upgrade_kind);
}

void
pk_backend_repair_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->repair_system != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_REPAIR_SYSTEM);
	pk_backend_job_set_transaction_flags (job, transaction_flags);
	pk_backend_job_set_parameters (job, g_variant_new ("(t)",
							   transaction_flags));
	backend->priv->desc->repair_system (backend, job, transaction_flags);
}

void
pk_backend_import_pubkey (PkBackend	*backend,
							 PkBackendJob	*job,
							 const gchar	*key_path)
{
	g_return_if_fail (PK_IS_BACKEND (backend));
	g_return_if_fail (backend->priv->desc->import_pubkey != NULL);
	g_return_if_fail (pk_is_thread_default ());

	/* final pre-flight checks */
	g_assert (pk_backend_job_get_vfunc_enabled (job, PK_BACKEND_SIGNAL_FINISHED));

	pk_backend_job_set_role (job, PK_ROLE_ENUM_IMPORT_PUBKEY);
	pk_backend_job_set_parameters (job, g_variant_new ("(s)",
							   key_path));
	backend->priv->desc->import_pubkey (backend, job, key_path);
}

static void
pk_backend_init (PkBackend *backend)
{
	backend->priv = PK_BACKEND_GET_PRIVATE (backend);
	backend->priv->eulas = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	backend->priv->thread_hash = g_hash_table_new_full (g_direct_hash,
							    g_direct_equal,
							    NULL,
							    g_free);
	g_mutex_init (&backend->priv->eulas_mutex);
	g_mutex_init (&backend->priv->thread_hash_mutex);
}

PkBackend *
pk_backend_new (GKeyFile *conf)
{
	PkBackend *backend;
	backend = g_object_new (PK_TYPE_BACKEND, NULL);
	backend->priv->conf = g_key_file_ref (conf);
	return PK_BACKEND (backend);
}

