/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Most of this code was taken from Zif, libzif/zif-transaction.c
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

#include <glib.h>
#include <hawkey/packagelist.h>
#include <hawkey/util.h>

#include "hif-goal.h"
#include "hif-utils.h"

/**
 * hif_goal_is_upgrade_package:
 */
gboolean
hif_goal_is_upgrade_package (HyGoal goal, HyPackage package)
{
	guint i;
	HyPackageList pkglist;
	HyPackage pkg;

	pkglist = hy_goal_list_upgrades (goal);
	FOR_PACKAGELIST(pkg, pkglist, i) {
		if (hy_package_cmp (pkg, package) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * hif_goal_get_packages:
 */
GPtrArray *
hif_goal_get_packages (HyGoal goal, PkBitfield types)
{
	GPtrArray *array;
	guint i;
	HyPackageList pkglist;
	HyPackage pkg;

	array = g_ptr_array_new ();
	if (pk_bitfield_contain (types, PK_INFO_ENUM_REMOVING)) {
		pkglist = hy_goal_list_erasures (goal);
		FOR_PACKAGELIST(pkg, pkglist, i)
			g_ptr_array_add (array, pkg);
	}
	if (pk_bitfield_contain (types, PK_INFO_ENUM_INSTALLING)) {
		pkglist = hy_goal_list_installs (goal);
		FOR_PACKAGELIST(pkg, pkglist, i)
			g_ptr_array_add (array, pkg);
	}
	if (pk_bitfield_contain (types, PK_INFO_ENUM_OBSOLETING)) {
		pkglist = hy_goal_list_obsoleted (goal);
		FOR_PACKAGELIST(pkg, pkglist, i)
			g_ptr_array_add (array, pkg);
	}
	if (pk_bitfield_contain (types, PK_INFO_ENUM_REINSTALLING)) {
		pkglist = hy_goal_list_reinstalls (goal);
		FOR_PACKAGELIST(pkg, pkglist, i)
			g_ptr_array_add (array, pkg);
	}
	if (pk_bitfield_contain (types, PK_INFO_ENUM_UPDATING)) {
		pkglist = hy_goal_list_upgrades (goal);
		FOR_PACKAGELIST(pkg, pkglist, i)
			g_ptr_array_add (array, pkg);
	}
	if (pk_bitfield_contain (types, PK_INFO_ENUM_DOWNGRADING)) {
		pkglist = hy_goal_list_downgrades (goal);
		FOR_PACKAGELIST(pkg, pkglist, i)
			g_ptr_array_add (array, pkg);
	}
	return array;
}

/**
 * hif_goal_depsolve:
 */
gboolean
hif_goal_depsolve (HyGoal goal, GError **error)
{
	gboolean ret = TRUE;
	gchar *tmp;
	gint cnt;
	gint j;
	gint rc;
	GString *string = NULL;
	HyPackageList pkglist;

	rc = hy_goal_run_flags (goal, HY_ALLOW_UNINSTALL);
	if (rc) {
		ret = FALSE;
		string = g_string_new ("Could not depsolve transaction; ");
		cnt = hy_goal_count_problems (goal);
		if (cnt == 1)
			g_string_append_printf (string, "%i problem detected:\n", cnt);
		else
			g_string_append_printf (string, "%i problems detected:\n", cnt);
		for (j = 0; j < cnt; j++) {
			tmp = hy_goal_describe_problem (goal, j);
			g_string_append_printf (string, "%i. %s\n", j, tmp);
			hy_free (tmp);
		}
		g_string_truncate (string, string->len - 1);
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_PACKAGE_CONFLICTS,
				     string->str);
		goto out;
	}

	/* anything to do? */
	if (hy_goal_req_length (goal) == 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE,
				     "The transaction was empty");
		goto out;
	}

	/* prevent downgrades */
	pkglist = hy_goal_list_downgrades (goal);
	if (hy_packagelist_count (pkglist) > 0) {
		ret = FALSE;
		g_set_error_literal (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED,
				     "Downgrading packages is prevented by policy");
		goto out;
	}
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	return ret;
}
