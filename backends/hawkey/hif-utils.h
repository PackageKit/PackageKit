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

#ifndef __HIF_UTILS_H
#define __HIF_UTILS_H

#include <glib.h>
#include <pk-backend.h>

#include <hawkey/package.h>
#include <hawkey/packagelist.h>

#define HIF_ERROR				(hif_error_quark ())

#define HIF_CONFIG_GROUP_NAME			"PluginHawkey"

gboolean	 hif_rc_to_gerror		(gint			 rc,
						 GError			**error);
PkErrorEnum	 hif_rc_to_error_enum		(gint			 rc);
const gchar	*hif_rc_to_error_str		(gint			 rc);
PkInfoEnum	 hif_update_severity_to_info_enum (HyUpdateSeverity	 severity);
GQuark		 hif_error_quark		(void);

void		 hif_emit_package		(PkBackendJob		*job,
						 PkInfoEnum		 info,
						 HyPackage		 pkg);
void		 hif_emit_package_list		(PkBackendJob		*job,
						 PkInfoEnum		 info,
						 HyPackageList		 pkglist);
void		 hif_emit_package_array		(PkBackendJob		*job,
						 PkInfoEnum		 info,
						 GPtrArray		*array);
void		 hif_emit_package_list_filter	(PkBackendJob		*job,
						 PkBitfield		 filters,
						 HyPackageList		 pkglist);
PkBitfield	 hif_get_filter_for_ids		(gchar			**package_ids);

#endif /* __HIF_UTILS_H */
