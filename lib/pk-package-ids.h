/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_PACKAGE_IDS_H
#define __PK_PACKAGE_IDS_H

#include <glib.h>

G_BEGIN_DECLS

/* rationale:
 *
 * '%': breaks printf
 * '|': used as the filename separator
 * '~': conary
 * '@': conary
 *
 * If this has to be changed, also change:
 * - backends/urpmi/helpers/urpmi-dispatched-backend.pl
 * - python/packagekit/backend.py
 */
#define PK_PACKAGE_IDS_DELIM	"&"

gchar		**pk_package_ids_from_id		(const gchar	*package_id);
gchar		**pk_package_ids_from_string		(const gchar	*package_id);
gboolean	 pk_package_ids_check			(gchar		**package_ids);
gchar		*pk_package_ids_to_string			(gchar		**package_ids);
gboolean	 pk_package_ids_present_id		(gchar		**package_ids,
							 const gchar	*package_id);
gchar		**pk_package_ids_add_id			(gchar		**package_ids,
							 const gchar	*package_id);
gchar		**pk_package_ids_add_ids		(gchar		**package_ids,
							 gchar		**package_ids_new);
gchar		**pk_package_ids_remove_id		(gchar		**package_ids,
							 const gchar	*package_id);

/* compat defines for old versions */

/**
 * pk_package_ids_from_text:
 * @package_id: A single package_id
 *
 * Form a composite string array of package_id's from
 * a delimited string
 *
 * Return value: (transfer full): the string array, or %NULL if invalid, free with g_strfreev()
 *
 * Deprecated: Use pk_package_ids_from_string()
 */
#define pk_package_ids_from_text	pk_package_ids_from_string

/**
 * pk_package_ids_to_text:
 * @package_ids: a string array of package_id's
 *
 * Cats the string array of package_id's into one delimited string
 * Return value: a string representation of all the package_id's.
 *
 * Deprecated: pk_package_ids_to_string()
 */
#define pk_package_ids_to_text		pk_package_ids_to_string

G_END_DECLS

#endif /* __PK_PACKAGE_IDS_H */
