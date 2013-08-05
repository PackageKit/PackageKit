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
#include <hawkey/reldep.h>
#include <hawkey/util.h>

#include "hif-package.h"

typedef struct {
	char		*checksum_str;
	gboolean	 user_action;
	gchar		*filename;
	PkInfoEnum	 info;
} HifPackagePrivate;

/**
 * hif_package_destroy_func:
 **/
static void
hif_package_destroy_func (void *userdata)
{
	HifPackagePrivate *priv = (HifPackagePrivate *) userdata;
	g_free (priv->filename);
	hy_free (priv->checksum_str);
	g_slice_free (HifPackagePrivate, priv);
}

/**
 * hif_package_get_filename:
 **/
const gchar *
hif_package_get_filename (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return NULL;
	return priv->filename;
}

/**
 * hif_package_get_pkgid:
 **/
const gchar *
hif_package_get_pkgid (HyPackage pkg)
{
	const unsigned char *checksum;
	HifPackagePrivate *priv;
	int checksum_type;

	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return NULL;
	if (priv->checksum_str != NULL)
		goto out;

	/* calculate and cache */
	checksum = hy_package_get_hdr_chksum (pkg, &checksum_type);
	if (checksum == NULL)
		goto out;
	priv->checksum_str = hy_chksum_str (checksum, checksum_type);
out:
	return priv->checksum_str;
}

/**
 * hif_package_get_priv:
 **/
static HifPackagePrivate *
hif_package_get_priv (HyPackage pkg)
{
	HifPackagePrivate *priv;

	/* create private area */
	priv = hy_package_get_userdata (pkg);
	if (priv != NULL)
		return priv;

	priv = g_slice_new0 (HifPackagePrivate);
	priv->info = PK_INFO_ENUM_UNKNOWN;
	hy_package_set_userdata (pkg, priv, hif_package_destroy_func);
	return priv;
}

/**
 * hif_package_set_filename:
 **/
void
hif_package_set_filename (HyPackage pkg, const gchar *filename)
{
	HifPackagePrivate *priv;

	/* replace contents */
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	g_free (priv->filename);
	priv->filename = g_strdup (filename);
}

/**
 * hif_package_get_info:
 */
PkInfoEnum
hif_package_get_info (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return PK_INFO_ENUM_UNKNOWN;
	return priv->info;
}

/**
 * hif_package_set_info:
 */
void
hif_package_set_info (HyPackage pkg, PkInfoEnum info)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->info = info;
}

/**
 * hif_package_get_user_action:
 */
gboolean
hif_package_get_user_action (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return FALSE;
	return priv->user_action;
}

/**
 * hif_package_set_user_action:
 */
void
hif_package_set_user_action (HyPackage pkg, gboolean user_action)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->user_action = user_action;
}

/**
 * hif_package_is_gui:
 */
gboolean
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
gboolean
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

/**
 * hif_package_is_downloaded:
 **/
gboolean
hif_package_is_downloaded (HyPackage pkg)
{
	const gchar *filename;

	if (hy_package_installed (pkg))
		return FALSE;
	filename = hif_package_get_filename (pkg);
	if (filename == NULL) {
		g_warning ("Failed to get cache filename for %s",
			   hy_package_get_name (pkg));
		return FALSE;
	}
	return g_file_test (filename, G_FILE_TEST_EXISTS);
}
