/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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
#include <glib.h>
#include <glib/gstdio.h>

#include <hawkey/errno.h>
#include <hawkey/packagelist.h>

#include "hif-utils.h"
#include "hif-package.h"

/**
 * hif_error_quark:
 **/
GQuark
hif_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("HifError");
	return quark;
}

/**
 * hif_rc_to_gerror:
 */
gboolean
hif_rc_to_gerror (gint rc, GError **error)
{
	if (rc == 0)
		return TRUE;
	switch (rc) {
	case HY_E_FAILED:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "general runtime error");
		break;
	case HY_E_OP:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "client programming error");
		break;
	case HY_E_LIBSOLV:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "error propagated from libsolv");
		break;
	case HY_E_IO:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "I/O error");
		break;
	case HY_E_CACHE_WRITE:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "cache write error");
		break;
	case HY_E_QUERY:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "ill-formed query");
		break;
	case HY_E_ARCH:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "unknown arch");
		break;
	case HY_E_VALIDATION:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "validation check failed");
		break;
	case HY_E_SELECTOR:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "ill-specified selector");
		break;
	case HY_E_NO_SOLUTION:
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "goal found no solutions");
		break;
	default:
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "no matching error enum %i", rc);
		break;
	}
	return FALSE;
}

/**
 * hif_rc_to_error_enum:
 */
PkErrorEnum
hif_rc_to_error_enum (gint rc)
{
	PkErrorEnum error_enum;

	g_assert (rc != 0);
	switch (rc) {
	case HY_E_OP:		/* client programming error */
	case HY_E_LIBSOLV:	/* error propagated from libsolv */
	case HY_E_IO:		/* I/O error */
	case HY_E_CACHE_WRITE:	/* cache write error */
	case HY_E_QUERY:	/* ill-formed query */
	case HY_E_ARCH:		/* unknown arch */
	case HY_E_VALIDATION:	/* validation check failed */
	case HY_E_SELECTOR:	/* ill-specified selector */
	case HY_E_NO_SOLUTION:	/* goal found no solutions */
	case HY_E_FAILED:	/* general runtime error */
	default:
		error_enum = PK_ERROR_ENUM_INTERNAL_ERROR;
		break;
	}
	return error_enum;
}

/**
 * hif_rc_to_error_str:
 */
const gchar *
hif_rc_to_error_str (gint rc)
{
	const gchar *str;

	g_assert (rc != 0);
	switch (rc) {
	case HY_E_FAILED:
		str = "general runtime error";
		break;
	case HY_E_OP:
		str = "client programming error";
		break;
	case HY_E_LIBSOLV:
		str = "error propagated from libsolv";
		break;
	case HY_E_IO:
		str = "I/O error";
		break;
	case HY_E_CACHE_WRITE:
		str = "cache write error";
		break;
	case HY_E_QUERY:
		str = "ill-formed query";
		break;
	case HY_E_ARCH:
		str = "unknown arch";
		break;
	case HY_E_VALIDATION:
		str = "validation check failed";
		break;
	case HY_E_SELECTOR:
		str = "ill-specified selector";
		break;
	case HY_E_NO_SOLUTION:
		str = "goal found no solutions";
		break;
	default:
		str = "no matching error enum";
		break;
	}
	return str;
}

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
	GHashTable *hash_cost;
	GHashTable *hash_installed;
	HyPackage found;
	HyPackage pkg;
	guint i;

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

	FOR_PACKAGELIST(pkg, pkglist, i) {

		/* blocked */
		if (hif_package_get_info (pkg) == PK_INFO_ENUM_BLOCKED)
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
	g_hash_table_unref (hash_cost);
	g_hash_table_unref (hash_installed);
}

/**
 * hif_get_filter_for_ids:
 */
PkBitfield
hif_get_filter_for_ids (gchar **package_ids)
{
	gboolean available = FALSE;
	gboolean installed = FALSE;
	gchar **split;
	guint i;
	PkBitfield filters;

	for (i = 0; package_ids[i] != NULL && (!installed || !available); i++) {
		split = pk_package_id_split (package_ids[i]);
		if (g_strcmp0 (split[PK_PACKAGE_ID_DATA], "installed") == 0)
			installed = TRUE;
		else
			available = TRUE;
		g_strfreev (split);
	}

	/* a mixture */
	if (installed && available) {
		filters = pk_bitfield_value (PK_FILTER_ENUM_NONE);
		goto out;
	}

	/* we can restrict what's loaded into the sack */
	if (!installed)
		filters = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);
	if (!available)
		filters = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
out:
	return filters;
}

/**
 * hif_update_severity_to_info_enum:
 */
PkInfoEnum
hif_update_severity_to_info_enum (HyUpdateSeverity severity)
{
	PkInfoEnum info_enum = HY_UPDATE_SEVERITY_UNKNOWN;
	switch (severity) {
	case HY_UPDATE_SEVERITY_SECURITY:
		info_enum = PK_INFO_ENUM_SECURITY;
		break;
	case HY_UPDATE_SEVERITY_IMPORTANT:
		info_enum = PK_INFO_ENUM_IMPORTANT;
		break;
	case HY_UPDATE_SEVERITY_BUGFIX:
		info_enum = PK_INFO_ENUM_BUGFIX;
		break;
	case HY_UPDATE_SEVERITY_NORMAL:
	case HY_UPDATE_SEVERITY_UNKNOWN:
		info_enum = PK_INFO_ENUM_NORMAL;
		break;
	case HY_UPDATE_SEVERITY_ENHANCEMENT:
		info_enum = PK_INFO_ENUM_ENHANCEMENT;
		break;
	case HY_UPDATE_SEVERITY_LOW:
		info_enum = PK_INFO_ENUM_LOW;
		break;
	default:
		g_warning ("Failed to find HyUpdateSeverity enum %i", severity);
		break;
	}
	return info_enum;
}
