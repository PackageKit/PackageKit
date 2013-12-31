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

#ifndef __HIF_REPOS_H
#define __HIF_REPOS_H

#include <glib.h>

#include <hawkey/repo.h>
#include <hawkey/package.h>

#include "hif-state.h"
#include "hif-source.h"

GPtrArray	*hif_repos_get_sources		(GKeyFile		*config,
						 HifSourceScanFlags	 flags,
						 GError			**error);
HifSource	*hif_repos_get_source_by_id	(GPtrArray		*sources,
						 const gchar		*id,
						 GError			**error);

#endif /* __HIF_REPOS_H */
