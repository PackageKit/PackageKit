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

#include <glib.h>

#include <hawkey/repo.h>
#include <hawkey/package.h>

#include "hif-state.h"

typedef struct HifSource HifSource;

typedef enum {
	HIF_SOURCE_SCAN_FLAG_NONE		= 0,
	HIF_SOURCE_SCAN_FLAG_ONLY_ENABLED	= 1,
	HIF_SOURCE_SCAN_FLAG_LAST
} HifSourceScanFlags;

GPtrArray	*hif_source_find_all		(const gchar		*repos_dir,
						 HifSourceScanFlags	 flags,
						 GError			**error);
HifSource	*hif_source_filter_by_id	(GPtrArray		*sources,
						 const gchar		*id,
						 GError			**error);
const gchar	*hif_source_get_id		(HifSource		*src);
const gchar	*hif_source_get_location	(HifSource		*src);
gboolean	 hif_source_get_enabled		(HifSource		*src);
gboolean	 hif_source_get_gpgcheck	(HifSource		*src);
gchar		*hif_source_get_description	(HifSource		*src);
HyRepo		 hif_source_get_repo		(HifSource		*src);
gboolean	 hif_source_is_devel		(HifSource		*src);
gboolean	 hif_source_check		(HifSource		*src,
						 HifState		*state,
						 GError			**error);
gboolean	 hif_source_update		(HifSource		*src,
						 HifState		*state,
						 GError			**error);
gboolean	 hif_source_clean		(HifSource		*src,
						 GError			**error);
gboolean	 hif_source_set_data		(HifSource		*src,
						 const gchar		*parameter,
						 const gchar		*value,
						 GError			**error);
gchar		*hif_source_download_package	(HifSource		*src,
						 HyPackage		 pkg,
						 const gchar		*directory,
						 HifState		*state,
						 GError			**error);
