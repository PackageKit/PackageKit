/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2010 Richard Hughes <richard@hughsie.com>
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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_STORE_ARRAY_H
#define __ZIF_STORE_ARRAY_H

#include <glib.h>

#include "zif-store.h"
#include "zif-package.h"
#include "zif-completion.h"

G_BEGIN_DECLS

GPtrArray	*zif_store_array_new			(void);

/* stores */
gboolean	 zif_store_array_add_store		(GPtrArray		*store_array,
							 ZifStore		*store);
gboolean	 zif_store_array_add_stores		(GPtrArray		*store_array,
							 GPtrArray		*stores);
gboolean	 zif_store_array_add_local		(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
gboolean	 zif_store_array_add_remote		(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
gboolean	 zif_store_array_add_remote_enabled	(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

/* methods */
gboolean	 zif_store_array_clean			(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
gboolean	 zif_store_array_refresh		(GPtrArray		*store_array,
							 gboolean		 force,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_resolve		(GPtrArray		*store_array,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_search_name		(GPtrArray		*store_array,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_search_details		(GPtrArray		*store_array,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_search_group		(GPtrArray		*store_array,
							 const gchar		*group_enum,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_search_category	(GPtrArray		*store_array,
							 const gchar		*group_id,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_search_file		(GPtrArray		*store_array,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_what_provides		(GPtrArray		*store_array,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_get_packages		(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_get_updates		(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
ZifPackage	*zif_store_array_find_package		(GPtrArray		*store_array,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_store_array_get_categories		(GPtrArray		*store_array,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_STORE_ARRAY_H */

