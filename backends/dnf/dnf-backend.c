/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <libdnf/libdnf.h>

#include "dnf-backend.h"

/**
 * dnf_emit_package:
 */
void
dnf_emit_package (PkBackendJob *job, PkInfoEnum info, DnfPackage *pkg)
{
	/* detect */
	if (info == PK_INFO_ENUM_UNKNOWN)
		info = dnf_package_get_info (pkg);
	if (info == PK_INFO_ENUM_UNKNOWN)
		info = dnf_package_installed (pkg) ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
	pk_backend_job_package (job,
				info,
				dnf_package_get_package_id (pkg),
				dnf_package_get_summary (pkg));
}

/**
 * dnf_emit_package_list:
 */
void
dnf_emit_package_list (PkBackendJob *job,
		       PkInfoEnum info,
		       GPtrArray *pkglist)
{
	guint i;
	DnfPackage *pkg;

	for (i = 0; i < pkglist->len; i++) {
		pkg = g_ptr_array_index (pkglist, i);
		dnf_emit_package (job, info, pkg);
	}
}

/**
 * dnf_emit_package_array:
 */
void
dnf_emit_package_array (PkBackendJob *job,
			 PkInfoEnum info,
			 GPtrArray *array)
{
	guint i;
	DnfPackage *pkg;

	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		dnf_emit_package (job, info, pkg);
	}
}

/**
 * dnf_emit_package_list_filter:
 */
void
dnf_emit_package_list_filter (PkBackendJob *job,
			      PkBitfield filters,
			      GPtrArray *pkglist)
{
	DnfPackage *found;
	DnfPackage *pkg;
	guint i;
	g_autoptr(GHashTable) hash_cost = NULL;
	g_autoptr(GHashTable) hash_installed = NULL;

	/* if a package exists in multiple repos, show the one with the lowest
	 * cost of downloading */
	hash_cost = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < pkglist->len; i++) {
		pkg = g_ptr_array_index (pkglist, i);
		if (dnf_package_installed (pkg))
			continue;

		/* if the NEVRA does not already exist in the array, just add */
		found = g_hash_table_lookup (hash_cost,
					     dnf_package_get_nevra (pkg));
		if (found == NULL) {
			g_hash_table_insert (hash_cost,
					     (gpointer) dnf_package_get_nevra (pkg),
					     (gpointer) pkg);
			continue;
		}

		/* a lower cost package */
		if (dnf_package_get_cost (pkg) < dnf_package_get_cost (found)) {
			dnf_package_set_info (found, PK_INFO_ENUM_BLOCKED);
			g_hash_table_replace (hash_cost,
					      (gpointer) dnf_package_get_nevra (pkg),
					      (gpointer) pkg);
		} else {
			dnf_package_set_info (pkg, PK_INFO_ENUM_BLOCKED);
		}
	}

	/* add all the installed packages to a hash */
	hash_installed = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < pkglist->len; i++) {
		pkg = g_ptr_array_index (pkglist, i);
		if (!dnf_package_installed (pkg))
			continue;
		g_hash_table_insert (hash_installed,
				     (gpointer) dnf_package_get_nevra (pkg),
				     (gpointer) pkg);
	}

	/* anything remote in metadata-only mode needs to be unavailable */
	for (i = 0; i < pkglist->len; i++) {
		DnfRepo *src;
		pkg = g_ptr_array_index (pkglist, i);
		if (dnf_package_installed (pkg))
			continue;
		src = dnf_package_get_repo (pkg);
		if (src == NULL)
			continue;
		if (dnf_repo_get_enabled (src) != DNF_REPO_ENABLED_METADATA)
			continue;
		dnf_package_set_info (pkg, PK_INFO_ENUM_UNAVAILABLE);
	}

	for (i = 0; i < pkglist->len; i++) {
		pkg = g_ptr_array_index (pkglist, i);

		/* blocked */
		if ((PkInfoEnum) dnf_package_get_info (pkg) == PK_INFO_ENUM_BLOCKED)
			continue;

		/* GUI */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI) && !dnf_package_is_gui (pkg))
			continue;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI) && dnf_package_is_gui (pkg))
			continue;

		/* DEVELOPMENT */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) && !dnf_package_is_devel (pkg))
			continue;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && dnf_package_is_devel (pkg))
			continue;

		/* DOWNLOADED */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DOWNLOADED) && !dnf_package_is_downloaded (pkg))
			continue;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DOWNLOADED) && dnf_package_is_downloaded (pkg))
			continue;

		/* if this package is available and the very same NEVRA is
		 * installed, skip this package */
		if (!dnf_package_installed (pkg)) {
			found = g_hash_table_lookup (hash_installed,
						     dnf_package_get_nevra (pkg));
			if (found != NULL)
				continue;
		}

		dnf_emit_package (job, PK_INFO_ENUM_UNKNOWN, pkg);
	}
}

/**
 * dnf_advisory_kind_to_info_enum:
 */
PkInfoEnum
dnf_advisory_kind_to_info_enum (DnfAdvisoryKind kind)
{
	PkInfoEnum info_enum = PK_INFO_ENUM_UNKNOWN;
	switch (kind) {
	case DNF_ADVISORY_KIND_SECURITY:
		info_enum = PK_INFO_ENUM_SECURITY;
		break;
	case DNF_ADVISORY_KIND_BUGFIX:
		info_enum = PK_INFO_ENUM_BUGFIX;
		break;
	case DNF_ADVISORY_KIND_UNKNOWN:
		info_enum = PK_INFO_ENUM_NORMAL;
		break;
	case DNF_ADVISORY_KIND_ENHANCEMENT:
		info_enum = PK_INFO_ENUM_ENHANCEMENT;
		break;
	default:
		g_warning ("Failed to find DnfAdvisoryKind enum %i", kind);
		break;
	}
	return info_enum;
}

/**
 * dnf_get_filter_for_ids:
 */
PkBitfield
dnf_get_filter_for_ids (gchar **package_ids)
{
	gboolean available = FALSE;
	gboolean installed = FALSE;
	guint i;
	PkBitfield filters = 0;

	for (i = 0; package_ids[i] != NULL && (!installed || !available); i++) {
		g_auto(GStrv) split = pk_package_id_split (package_ids[i]);
		if (g_strcmp0 (split[PK_PACKAGE_ID_DATA], "installed") == 0)
			installed = TRUE;
		else
			available = TRUE;
	}

	/* a mixture */
	if (installed && available)
		return pk_bitfield_value (PK_FILTER_ENUM_NONE);

	/* we can restrict what's loaded into the sack */
	if (!installed)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	if (!available)
		filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
	return filters;
}
