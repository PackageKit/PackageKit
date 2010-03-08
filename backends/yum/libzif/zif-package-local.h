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

#ifndef __ZIF_PACKAGE_LOCAL_H
#define __ZIF_PACKAGE_LOCAL_H

#include <glib-object.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmdb.h>

#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_PACKAGE_LOCAL		(zif_package_local_get_type ())
#define ZIF_PACKAGE_LOCAL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_PACKAGE_LOCAL, ZifPackageLocal))
#define ZIF_PACKAGE_LOCAL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_PACKAGE_LOCAL, ZifPackageLocalClass))
#define ZIF_IS_PACKAGE_LOCAL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_PACKAGE_LOCAL))
#define ZIF_IS_PACKAGE_LOCAL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_PACKAGE_LOCAL))
#define ZIF_PACKAGE_LOCAL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_PACKAGE_LOCAL, ZifPackageLocalClass))

typedef struct _ZifPackageLocal		ZifPackageLocal;
typedef struct _ZifPackageLocalPrivate	ZifPackageLocalPrivate;
typedef struct _ZifPackageLocalClass	ZifPackageLocalClass;

struct _ZifPackageLocal
{
	ZifPackage		 parent;
	ZifPackageLocalPrivate	*priv;
};

struct _ZifPackageLocalClass
{
	ZifPackageClass		 parent_class;
};

GType			 zif_package_local_get_type		(void);
ZifPackageLocal		*zif_package_local_new			(void);
gboolean		 zif_package_local_set_from_header	(ZifPackageLocal *pkg,
								 Header		 header,
								 GError		**error);
gboolean		 zif_package_local_set_from_filename	(ZifPackageLocal *pkg,
								 const gchar	*filename,
								 GError		**error);

G_END_DECLS

#endif /* __ZIF_PACKAGE_LOCAL_H */

