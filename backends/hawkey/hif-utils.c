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

#include <stdlib.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <hawkey/errno.h>
#include <hawkey/packagelist.h>
#include <hawkey/reldep.h>

#include "hif-utils.h"

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
 * hif_package_get_id:
 */
gchar *
hif_package_get_id (HyPackage pkg)
{
	const gchar *reponame;

	reponame = hy_package_get_reponame (pkg);
	if (g_strcmp0 (reponame, HY_SYSTEM_REPO_NAME) == 0)
		reponame = "installed";
	return pk_package_id_build (hy_package_get_name (pkg),
				    hy_package_get_evr (pkg),
				    hy_package_get_arch (pkg),
				    reponame);
}

/**
 * hif_emit_package:
 */
void
hif_emit_package (PkBackendJob *job, PkInfoEnum info, HyPackage pkg)
{
	gchar *package_id;

	/* detect */
	if (info == PK_INFO_ENUM_UNKNOWN)
		info = hy_package_installed (pkg) ? PK_INFO_ENUM_INSTALLED : PK_INFO_ENUM_AVAILABLE;
	package_id = hif_package_get_id (pkg);
	pk_backend_job_package (job,
				info,
				package_id,
				hy_package_get_summary (pkg));
	g_free (package_id);
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
 * hif_package_is_gui:
 */
static gboolean
hif_package_is_gui (HyPackage pkg)
{
	gboolean ret = FALSE;
	gchar *tmp;
	gint idx;
	HyReldepList reldeplist;
	HyReldep reldep;
	int size;

	/* find if the package depends on GTK or KDE */
	reldeplist = hy_package_get_requires (pkg);
	size = hy_reldeplist_count (reldeplist);
	for (idx = 0; idx < size && !ret; idx++) {
		reldep = hy_reldeplist_get_clone (reldeplist, idx);
		tmp = hy_reldep_str (reldep);
		if (g_strstr_len (tmp, -1, "libgtk") != NULL ||
		    g_strstr_len (tmp, -1, "libkde") != NULL) {
			ret = TRUE;
		}
		free (tmp);
		hy_reldep_free (reldep);
	}

	hy_reldeplist_free (reldeplist);
	return ret;
}

/**
 * hif_package_is_devel:
 */
static gboolean
hif_package_is_devel (HyPackage pkg)
{
	const gchar *name;
	name = hy_package_get_name (pkg);
	if (g_str_has_suffix (name, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (name, "-devel"))
		return TRUE;
	if (g_str_has_suffix (name, "-static"))
		return TRUE;
	if (g_str_has_suffix (name, "-libs"))
		return TRUE;
	return FALSE;
}

#if 0
/**
 * hif_package_is_application:
 **/
static gboolean
hif_package_is_application (HifPackage *package)
{
	const gchar *filename;
	gboolean ret = FALSE;
	GPtrArray *files;
	guint i;

	/* get file lists and see if it installs a desktop file */
	files = hif_package_get_files (package);
	if (files == NULL)
		goto out;
	for (i = 0; i < files->len; i++) {
		filename = g_ptr_array_index (files, i);
		if (g_str_has_prefix (filename, "/usr/share/applications/") &&
		    g_str_has_suffix (filename, ".desktop")) {
			ret = TRUE;
			goto out;
		}
	}
out:
	if (files != NULL)
		g_ptr_array_unref (files);
	return ret;
}
#endif

/**
 * hif_emit_package_list_filter:
 */
void
hif_emit_package_list_filter (PkBackendJob *job,
				PkBitfield filters,
				HyPackageList pkglist)
{
	guint i;
	HyPackage pkg;

	FOR_PACKAGELIST(pkg, pkglist, i) {
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
		hif_emit_package (job, PK_INFO_ENUM_UNKNOWN, pkg);
	}
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
