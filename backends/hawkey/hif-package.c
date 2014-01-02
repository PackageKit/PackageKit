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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <hawkey/reldep.h>
#include <hawkey/util.h>
#include <librepo/librepo.h>

#include "hif-package.h"
#include "hif-utils.h"

typedef struct {
	char		*checksum_str;
	char		*nevra;
	gboolean	 user_action;
	gchar		*filename;
	gchar		*package_id;
	PkInfoEnum	 info;
	HifSource	*src;
} HifPackagePrivate;

/**
 * hif_package_destroy_func:
 **/
static void
hif_package_destroy_func (void *userdata)
{
	HifPackagePrivate *priv = (HifPackagePrivate *) userdata;
	g_free (priv->filename);
	g_free (priv->package_id);
	hy_free (priv->checksum_str);
	hy_free (priv->nevra);
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
 * hif_package_get_pkgid:
 **/
const gchar *
hif_package_get_pkgid (HyPackage pkg)
{
	const unsigned char *checksum;
	HifPackagePrivate *priv;
	int checksum_type;

	priv = hif_package_get_priv (pkg);
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
 * hif_package_get_id:
 **/
const gchar *
hif_package_get_id (HyPackage pkg)
{
	HifPackagePrivate *priv;
	const gchar *reponame;

	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return NULL;
	if (priv->package_id != NULL)
		goto out;

	/* calculate and cache */
	reponame = hy_package_get_reponame (pkg);
	if (g_strcmp0 (reponame, HY_SYSTEM_REPO_NAME) == 0)
		reponame = "installed";
	else if (g_strcmp0 (reponame, HY_CMDLINE_REPO_NAME) == 0)
		reponame = "local";
	priv->package_id = pk_package_id_build (hy_package_get_name (pkg),
						hy_package_get_evr (pkg),
						hy_package_get_arch (pkg),
						reponame);
out:
	return priv->package_id;
}

/**
 * hif_package_get_nevra:
 **/
const gchar *
hif_package_get_nevra (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv->nevra == NULL)
		priv->nevra = hy_package_get_nevra (pkg);
	return priv->nevra;
}

/**
 * hif_package_get_cost:
 **/
guint
hif_package_get_cost (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hif_package_get_priv (pkg);
	if (priv->src == NULL) {
		g_warning ("no src for %s", hif_package_get_id (pkg));
		return G_MAXUINT;
	}
	return hif_source_get_cost (priv->src);
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
 * hif_package_set_source:
 **/
void
hif_package_set_source (HyPackage pkg, HifSource *src)
{
	HifPackagePrivate *priv;
	gchar *basename = NULL;

	/* replace contents */
	priv = hif_package_get_priv (pkg);
	if (priv == NULL)
		return;
	priv->src = src;

	/* default cache filename location */
	if (!hy_package_installed (pkg)) {
		basename = g_path_get_basename (hy_package_get_location (pkg));
		g_free (priv->filename);
		priv->filename = g_build_filename (hif_source_get_location (src),
						   "packages",
						   basename,
						   NULL);
		g_free (basename);
	}
}

/**
 * hif_package_get_source:
 **/
HifSource *
hif_package_get_source (HyPackage pkg)
{
	HifPackagePrivate *priv;
	priv = hy_package_get_userdata (pkg);
	if (priv == NULL)
		return NULL;
	return priv->src;
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

/**
 * hif_source_checksum_hy_to_lr:
 **/
static GChecksumType
hif_source_checksum_hy_to_lr (int checksum_hy)
{
	if (checksum_hy == HY_CHKSUM_MD5)
		return LR_CHECKSUM_MD5;
	if (checksum_hy == HY_CHKSUM_SHA1)
		return LR_CHECKSUM_SHA1;
	if (checksum_hy == HY_CHKSUM_SHA256)
		return LR_CHECKSUM_SHA256;
	return G_CHECKSUM_SHA512;
}

/**
 * hif_package_check_filename:
 **/
gboolean
hif_package_check_filename (HyPackage pkg, gboolean *valid, GError **error)
{
	LrChecksumType checksum_type_lr;
	char *checksum_valid = NULL;
	const gchar *path;
	const unsigned char *checksum;
	gboolean ret = TRUE;
	int checksum_type_hy;
	int fd;

	/* check if the file does not exist */
	path = hif_package_get_filename (pkg);
	g_debug ("checking if %s already exists...", path);
	if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
		*valid = FALSE;
		goto out;
	}

	/* check the checksum */
	checksum = hy_package_get_chksum (pkg, &checksum_type_hy);
	checksum_valid = hy_chksum_str (checksum, checksum_type_hy);
	checksum_type_lr = hif_source_checksum_hy_to_lr (checksum_type_hy);
	fd = g_open (path, O_RDONLY, 0);
	if (fd < 0) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Failed to open %s", path);
		goto out;
	}
	ret = lr_checksum_fd_cmp(checksum_type_lr,
				 fd,
				 checksum_valid,
				 TRUE, /* use xattr value */
				 valid,
				 error);
	if (!ret) {
		g_close (fd, NULL);
		goto out;
	}
	ret = g_close (fd, error);
	if (!ret)
		goto out;
out:
	hy_free (checksum_valid);
	return ret;
}

/**
 * hif_package_download:
 **/
gchar *
hif_package_download (HyPackage pkg,
		      const gchar *directory,
		      HifState *state,
		      GError **error)
{
	HifSource *src;
	src = hif_package_get_source (pkg);
	return hif_source_download_package (src, pkg, directory, state, error);
}
