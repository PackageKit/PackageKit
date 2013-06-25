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

#include "hif-package.h"

typedef struct {
	gchar		*filename;
} HifPackagePrivate;

/**
 * hif_package_destroy_func:
 **/
static void
hif_package_destroy_func (void *userdata)
{
	HifPackagePrivate *priv = (HifPackagePrivate *) userdata;
	g_free (priv->filename);
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
 * hif_package_set_filename:
 **/
void
hif_package_set_filename (HyPackage pkg, const gchar *filename)
{
	HifPackagePrivate *priv;

	/* create private area */
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL) {
		priv = g_slice_new0 (HifPackagePrivate);
		hy_package_set_userdata (pkg, priv, hif_package_destroy_func);
	}

	/* replace contents */
	g_free (priv->filename);
	priv->filename = g_strdup (filename);
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
