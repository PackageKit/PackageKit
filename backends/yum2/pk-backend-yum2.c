/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#include <pk-network.h>
#include <pk-backend.h>
#include <pk-backend-dbus.h>

static PkBackendDbus *dbus;
static PkNetwork *network;

#define PK_DBUS_YUM_INTERFACE		"org.freedesktop.PackageKitYumBackend"
#define PK_DBUS_YUM_SERVICE		"org.freedesktop.PackageKitYumBackend"
#define PK_DBUS_YUM_PATH		"/org/freedesktop/PackageKitYumBackend"

/**
 * backend_initalize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initalize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_debug ("FILTER: initalize");
	network = pk_network_new ();
	dbus = pk_backend_dbus_new ();
	pk_backend_dbus_set_name (dbus, PK_DBUS_YUM_SERVICE, PK_DBUS_YUM_INTERFACE, PK_DBUS_YUM_PATH);
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_debug ("FILTER: destroy");
	g_object_unref (network);
	pk_backend_dbus_kill (dbus);
	g_object_unref (dbus);
}

/**
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
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
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_GUI,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				      PK_FILTER_ENUM_FREE,
				      -1);
}

/**
 * pk_backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	/* this feels bad... */
	pk_backend_dbus_cancel (dbus);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_depends (dbus, package_id, recursive);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_description (dbus, package_id);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_files (dbus, package_id);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_requires (dbus, package_id, recursive);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_updates (dbus);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_update_detail (dbus, package_id);
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);

	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_dbus_install_package (dbus, package_id);
}

/**
 * backend_install_file:
 */
static void
backend_install_file (PkBackend *backend, const gchar *full_path)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_install_file (dbus, full_path);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);

	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_dbus_refresh_cache (dbus, force);
}

/**
 * pk_backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_remove_package (dbus, package_id, allow_deps);
}

/**
 * pk_backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_search_details (dbus, filter, search);
}

/**
 * pk_backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_search_file (dbus, filter, search);
}

/**
 * pk_backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_search_group (dbus, filter, search);
}

/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_search_name (dbus, filter, search);
}

/**
 * pk_backend_update_package:
 */
static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);

	/* check network state */
	if (pk_network_is_online (network) == FALSE) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_dbus_update_package (dbus, package_id);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_update_system (dbus);
}

/**
 * pk_backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_resolve (dbus, filter, package_id);
}

/**
 * pk_backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_get_repo_list (dbus);
}

/**
 * pk_backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_repo_enable (dbus, rid, enabled);
}

/**
 * pk_backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (dbus != NULL);
	pk_backend_dbus_repo_set_data (dbus, rid, parameter, value);
}

PK_BACKEND_OPTIONS (
	"YUM",					/* description */
	"Tim Lauridsen <timlau@fedoraproject.org>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	backend_cancel,				/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_files,			/* get_files */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	backend_install_file,			/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_package,			/* update_package */
	backend_update_system,			/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data			/* repo_set_data */
);

