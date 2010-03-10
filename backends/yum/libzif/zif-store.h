/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_STORE_H
#define __ZIF_STORE_H

#include <glib-object.h>
#include <gio/gio.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-package.h"
#include "zif-completion.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE		(zif_store_get_type ())
#define ZIF_STORE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE, ZifStore))
#define ZIF_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE, ZifStoreClass))
#define ZIF_IS_STORE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE))
#define ZIF_IS_STORE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE))
#define ZIF_STORE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE, ZifStoreClass))
#define ZIF_STORE_ERROR		(zif_store_error_quark ())

typedef struct _ZifStore	ZifStore;
typedef struct _ZifStorePrivate	ZifStorePrivate;
typedef struct _ZifStoreClass	ZifStoreClass;

struct _ZifStore
{
	GObject			 parent;
	ZifStorePrivate		*priv;
};

struct _ZifStoreClass
{
	GObjectClass	parent_class;
	/* vtable */
	gboolean	 (*load)		(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	gboolean	 (*clean)		(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	gboolean	 (*refresh)		(ZifStore		*store,
						 gboolean		 force,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*search_name)		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*search_category)	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*search_details)	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*search_group)	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*search_file)		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*resolve)		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*what_provides)	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*get_packages)	(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*get_updates)		(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	ZifPackage	*(*find_package)	(ZifStore		*store,
						 const gchar		*package_id,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	GPtrArray	*(*get_categories)	(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
	const gchar	*(*get_id)		(ZifStore		*store);
	void		 (*print)		(ZifStore		*store);
};


typedef enum {
	ZIF_STORE_ERROR_FAILED,
	ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
	ZIF_STORE_ERROR_FAILED_TO_FIND,
	ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
	ZIF_STORE_ERROR_NO_SUPPORT,
	ZIF_STORE_ERROR_NOT_LOCKED,
	ZIF_STORE_ERROR_MULTIPLE_MATCHES,
	ZIF_STORE_ERROR_LAST
} ZifStoreError;

GType		 zif_store_get_type		(void);
GQuark		 zif_store_error_quark		(void);
ZifStore	*zif_store_new			(void);
gboolean	 zif_store_load			(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
gboolean	 zif_store_clean		(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
gboolean	 zif_store_refresh		(ZifStore		*store,
						 gboolean		 force,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_search_name		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_search_category	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_search_details	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_search_group		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_search_file		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_resolve		(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_what_provides	(ZifStore		*store,
						 const gchar		*search,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_get_packages		(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_get_updates		(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
ZifPackage	*zif_store_find_package		(ZifStore		*store,
						 const gchar		*package_id,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_store_get_categories	(ZifStore		*store,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
const gchar	*zif_store_get_id		(ZifStore		*store);
void		 zif_store_print		(ZifStore		*store);

G_END_DECLS

#endif /* __ZIF_STORE_H */

