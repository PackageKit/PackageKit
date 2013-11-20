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

#ifndef __HIF_PACKAGE_H
#define __HIF_PACKAGE_H

#include <glib.h>
#include <pk-backend.h>

#include <hawkey/package.h>

#include "hif-source.h"

HifSource	*hif_package_get_source			(HyPackage	 pkg);
void		 hif_package_set_source			(HyPackage	 pkg,
							 HifSource	*src);
const gchar	*hif_package_get_filename		(HyPackage	 pkg);
void		 hif_package_set_filename		(HyPackage	 pkg,
							 const gchar	*filename);
PkInfoEnum	 hif_package_get_info			(HyPackage	 pkg);
void		 hif_package_set_info			(HyPackage	 pkg,
							 PkInfoEnum	 info);
gboolean	 hif_package_get_user_action		(HyPackage	 pkg);
void		 hif_package_set_user_action		(HyPackage	 pkg,
							 gboolean	 user_action);
gboolean	 hif_package_is_gui			(HyPackage	 pkg);
gboolean	 hif_package_is_devel			(HyPackage	 pkg);
gboolean	 hif_package_is_downloaded		(HyPackage	 pkg);
const gchar	*hif_package_get_pkgid			(HyPackage	 pkg);
gchar		*hif_package_download			(HyPackage	 pkg,
							 const gchar	*directory,
							 HifState	*state,
							 GError		**error);
gboolean	 hif_package_check_filename		(HyPackage	 pkg,
							 gboolean	*valid,
							 GError		**error);

#endif /* __HIF_PACKAGE_H */
