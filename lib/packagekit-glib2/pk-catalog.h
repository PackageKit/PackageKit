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

#ifndef __PK_CATALOG_H
#define __PK_CATALOG_H

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-progress.h>
#include <packagekit-glib2/pk-results.h>

G_BEGIN_DECLS

#define PK_TYPE_CATALOG		(pk_catalog_get_type ())
#define PK_CATALOG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CATALOG, PkCatalog))
#define PK_CATALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CATALOG, PkCatalogClass))
#define PK_IS_CATALOG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CATALOG))
#define PK_IS_CATALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CATALOG))
#define PK_CATALOG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CATALOG, PkCatalogClass))
#define PK_CATALOG_ERROR	(pk_catalog_error_quark ())
#define PK_CATALOG_TYPE_ERROR	(pk_catalog_error_get_type ())

/* the file extension to a catalog */
#define PK_CATALOG_FILE_EXTENSION	"catalog"
#define PK_CATALOG_FILE_HEADER		"PackageKit Catalog"

typedef enum
{
	PK_CATALOG_ERROR_FAILED
} PkCatalogError;

typedef struct _PkCatalogPrivate	PkCatalogPrivate;
typedef struct _PkCatalog		PkCatalog;
typedef struct _PkCatalogClass	PkCatalogClass;

struct _PkCatalog
{
	GObject			 parent;
	PkCatalogPrivate	*priv;
};

struct _PkCatalogClass
{
	GObjectClass	parent_class;
	/* Padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
};

GQuark		 pk_catalog_error_quark			(void);
GType		 pk_catalog_get_type			(void);
G_DEPRECATED
PkCatalog	*pk_catalog_new				(void);

G_DEPRECATED
GPtrArray	*pk_catalog_lookup_finish		(PkCatalog		*catalog,
							 GAsyncResult		*res,
							 GError			**error);

G_DEPRECATED
void		 pk_catalog_lookup_async 		(PkCatalog		*catalog,
							 const gchar		*filename,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);

G_END_DECLS

#endif /* __PK_CATALOG_H */

