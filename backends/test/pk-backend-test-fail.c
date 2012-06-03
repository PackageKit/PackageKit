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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>

/**
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Test Fail");
}

/**
 * pk_backend_initialize:
 */
void
pk_backend_initialize (PkBackend *backend)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
			       "Failed to initialize package manager");
}

/**
 * pk_backend_destroy:
 */
void
pk_backend_destroy (PkBackend *backend)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
			       "Failed to release control");
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ACCESSIBILITY,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_SYSTEM,
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
		-1);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBitfield transaction_flags, gchar **package_ids)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, PkBitfield transaction_flags, gchar **full_paths)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
			       "Error number 1");
	pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
			       "Duplicate error");
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
			       "Error number 1");
	pk_backend_finished (backend);
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, PkBitfield transaction_flags, gchar **package_ids)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, PkBitfield transaction_flags)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
	pk_backend_error_code (backend, PK_ERROR_ENUM_INSTALL_ROOT_INVALID,
			       "Cannot find boot partition");
	pk_backend_finished (backend);
}
