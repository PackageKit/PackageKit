/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Matthias Klumpp <matthias@tenstral.net>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __PK_PACKAGE_CACHE_H
#define __PK_PACKAGE_CACHE_H

#include <glib-object.h>
#include <packagekit-glib2/pk-package.h>

G_BEGIN_DECLS

#define PK_TYPE_PACKAGE_CACHE		(pk_package_cache_get_type ())
#define PK_PACKAGE_CACHE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PACKAGE_CACHE, PkPackageCache))
#define PK_PACKAGE_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PACKAGE_CACHE, PkPackageCacheClass))
#define PK_IS_PACKAGE_CACHE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PACKAGE_CACHE))
#define PK_IS_PACKAGE_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PACKAGE_CACHE))
#define PK_PACKAGE_CACHE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PACKAGE_CACHE, PkPackageCacheClass))

typedef struct _PkPackageCachePrivate	PkPackageCachePrivate;
typedef struct _PkPackageCache		PkPackageCache;
typedef struct _PkPackageCacheClass	PkPackageCacheClass;

struct _PkPackageCache
{
	 GObject		 parent;
	 PkPackageCachePrivate	*priv;
};

struct _PkPackageCacheClass
{
	GObjectClass		 parent_class;
};

GType		 pk_package_cache_get_type		(void);
PkPackageCache	*pk_package_cache_new			(void);
gboolean	 pk_package_cache_set_filename		(PkPackageCache	*pkcache,
							 const gchar	*filename,
							 GError		**error);
gboolean	 pk_package_cache_open			(PkPackageCache	*pkcache,
							 gboolean	 synchronous,
							 GError		**error);
gboolean	 pk_package_cache_close			(PkPackageCache	*pkcache,
							 gboolean	 vaccuum,
							 GError		**error);
guint		 pk_package_cache_get_version		(PkPackageCache *pkcache);
gboolean	 pk_package_cache_clear			(PkPackageCache *pkcache,
							 GError **error);
gboolean	 pk_package_cache_add_package		(PkPackageCache *pkcache,
							 PkPackage *package,
							 GError **error);

G_END_DECLS

#endif /* __PK_PACKAGE_CACHE_H */
