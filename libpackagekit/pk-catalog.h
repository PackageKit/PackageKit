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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __PK_CATALOG_H
#define __PK_CATALOG_H

#include <glib-object.h>
#include <pk-package-list.h>

G_BEGIN_DECLS

#define PK_TYPE_CATALOG		(pk_catalog_get_type ())
#define PK_CATALOG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CATALOG, PkCatalog))
#define PK_CATALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CATALOG, PkCatalogClass))
#define PK_IS_CATALOG(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CATALOG))
#define PK_IS_CATALOG_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CATALOG))
#define PK_CATALOG_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CATALOG, PkCatalogClass))
#define PK_CATALOG_ERROR	(pk_catalog_error_quark ())
#define PK_CATALOG_TYPE_ERROR	(pk_catalog_error_get_type ())

typedef struct PkCatalogPrivate PkCatalogPrivate;

typedef struct
{
	 GObject		 parent;
	 PkCatalogPrivate	*priv;
} PkCatalog;

typedef struct
{
	GObjectClass	parent_class;
} PkCatalogClass;

typedef enum {
	PK_CATALOG_PROGRESS_PACKAGES,
	PK_CATALOG_PROGRESS_FILES,
	PK_CATALOG_PROGRESS_PROVIDES,
	PK_CATALOG_PROGRESS_LAST
} PkCatalogProgress;

GType		 pk_catalog_get_type		  	(void) G_GNUC_CONST;
PkCatalog	*pk_catalog_new				(void);
PkPackageList	*pk_catalog_process_files		(PkCatalog		*catalog,
							 gchar			**filenames);
gboolean	 pk_catalog_cancel			(PkCatalog		*catalog);

G_END_DECLS

#endif /* __PK_CATALOG_H */

