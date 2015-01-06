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
#include <pk-cleanup.h>

#include <libhif.h>

/* allow compiling with older libhif versions */
#if !HIF_CHECK_VERSION(0,2,0)
#include <libhif-private.h>
#endif

#include <hawkey/errno.h>

#include "hif-backend.h"

/**
 * hif_emit_package:
 */
void
hif_emit_package (PkBackendJob *job, PkInfoEnum info, HyPackage pkg)
{
	/* detect */
	if (info == PK_INFO_ENUM_UNKNOWN)
		info = hif_package_get_info (pkg);
	if (info == PK_INFO_ENUM_UNKNOWN)
		info = hy_package_installed (pkg) ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
	pk_backend_job_package (job,
				info,
				hif_package_get_id (pkg),
				hy_package_get_summary (pkg));
}

/**
 * hif_emit_package_list:
 */
void
hif_emit_package_list (PkBackendJob *job,
		       PkInfoEnum info,
		       HyPackageList pkglist)
{
	guint i;
	HyPackage pkg;

	FOR_PACKAGELIST(pkg, pkglist, i)
		hif_emit_package (job, info, pkg);
}

/**
 * hif_emit_package_array:
 */
void
hif_emit_package_array (PkBackendJob *job,
			 PkInfoEnum info,
			 GPtrArray *array)
{
	guint i;
	HyPackage pkg;

	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		hif_emit_package (job, info, pkg);
	}
}

/**
 * hif_emit_package_list_filter:
 */
void
hif_emit_package_list_filter (PkBackendJob *job,
			      PkBitfield filters,
			      HyPackageList pkglist)
{
	HyPackage found;
	HyPackage pkg;
	guint i;
	_cleanup_hashtable_unref_ GHashTable *hash_cost = NULL;
	_cleanup_hashtable_unref_ GHashTable *hash_installed = NULL;

	/* if a package exists in multiple repos, show the one with the lowest
	 * cost of downloading */
	hash_cost = g_hash_table_new (g_str_hash, g_str_equal);
	FOR_PACKAGELIST(pkg, pkglist, i) {
		if (hy_package_installed (pkg))
			continue;

		/* if the NEVRA does not already exist in the array, just add */
		found = g_hash_table_lookup (hash_cost,
					     hif_package_get_nevra (pkg));
		if (found == NULL) {
			g_hash_table_insert (hash_cost,
					     (gpointer) hif_package_get_nevra (pkg),
					     (gpointer) pkg);
			continue;
		}

		/* a lower cost package */
		if (hif_package_get_cost (pkg) < hif_package_get_cost (found)) {
			hif_package_set_info (found, PK_INFO_ENUM_BLOCKED);
			g_hash_table_replace (hash_cost,
					      (gpointer) hif_package_get_nevra (pkg),
					      (gpointer) pkg);
		} else {
			hif_package_set_info (pkg, PK_INFO_ENUM_BLOCKED);
		}
	}

	/* add all the installed packages to a hash */
	hash_installed = g_hash_table_new (g_str_hash, g_str_equal);
	FOR_PACKAGELIST(pkg, pkglist, i) {
		if (!hy_package_installed (pkg))
			continue;
		g_hash_table_insert (hash_installed,
				     (gpointer) hif_package_get_nevra (pkg),
				     (gpointer) pkg);
	}

	/* anything remote in metadata-only mode needs to be unavailable */
	FOR_PACKAGELIST(pkg, pkglist, i) {
		HifSource *src;
		if (hy_package_installed (pkg))
			continue;
		src = hif_package_get_source (pkg);
		if (src == NULL)
			continue;
		if (hif_source_get_enabled (src) != HIF_SOURCE_ENABLED_METADATA)
			continue;
		hif_package_set_info (pkg, PK_INFO_ENUM_UNAVAILABLE);
	}

	FOR_PACKAGELIST(pkg, pkglist, i) {

		/* blocked */
		if ((PkInfoEnum) hif_package_get_info (pkg) == PK_INFO_ENUM_BLOCKED)
			continue;

		/* GUI */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI) && !hif_package_is_gui (pkg))
			continue;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI) && hif_package_is_gui (pkg))
			continue;

		/* DEVELOPMENT */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT) && !hif_package_is_devel (pkg))
			continue;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && hif_package_is_devel (pkg))
			continue;

		/* DOWNLOADED */
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DOWNLOADED) && !hif_package_is_downloaded (pkg))
			continue;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DOWNLOADED) && hif_package_is_downloaded (pkg))
			continue;

		/* if this package is available and the very same NEVRA is
		 * installed, skip this package */
		if (!hy_package_installed (pkg)) {
			found = g_hash_table_lookup (hash_installed,
						     hif_package_get_nevra (pkg));
			if (found != NULL)
				continue;
		}

		hif_emit_package (job, PK_INFO_ENUM_UNKNOWN, pkg);
	}
}

/**
 * hif_advisory_type_to_info_enum:
 */
PkInfoEnum
hif_advisory_type_to_info_enum (HyAdvisoryType type)
{
	PkInfoEnum info_enum = PK_INFO_ENUM_UNKNOWN;
	switch (type) {
	case HY_ADVISORY_SECURITY:
		info_enum = PK_INFO_ENUM_SECURITY;
		break;
	case HY_ADVISORY_BUGFIX:
		info_enum = PK_INFO_ENUM_BUGFIX;
		break;
	case HY_ADVISORY_UNKNOWN:
		info_enum = PK_INFO_ENUM_NORMAL;
		break;
	case HY_ADVISORY_ENHANCEMENT:
		info_enum = PK_INFO_ENUM_ENHANCEMENT;
		break;
	default:
		g_warning ("Failed to find HyAdvisoryType enum %i", type);
		break;
	}
	return info_enum;
}

/**
 * hif_get_filter_for_ids:
 */
PkBitfield
hif_get_filter_for_ids (gchar **package_ids)
{
	gboolean available = FALSE;
	gboolean installed = FALSE;
	guint i;
	PkBitfield filters = 0;

	for (i = 0; package_ids[i] != NULL && (!installed || !available); i++) {
		_cleanup_strv_free_ gchar **split = pk_package_id_split (package_ids[i]);
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
