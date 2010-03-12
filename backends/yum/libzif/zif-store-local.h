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

#ifndef __ZIF_STORE_LOCAL_H
#define __ZIF_STORE_LOCAL_H

#include <glib-object.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-store.h"
#include "zif-package.h"

G_BEGIN_DECLS

#define ZIF_TYPE_STORE_LOCAL		(zif_store_local_get_type ())
#define ZIF_STORE_LOCAL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_STORE_LOCAL, ZifStoreLocal))
#define ZIF_STORE_LOCAL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_STORE_LOCAL, ZifStoreLocalClass))
#define ZIF_IS_STORE_LOCAL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_STORE_LOCAL))
#define ZIF_IS_STORE_LOCAL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_STORE_LOCAL))
#define ZIF_STORE_LOCAL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_STORE_LOCAL, ZifStoreLocalClass))

typedef struct _ZifStoreLocal		ZifStoreLocal;
typedef struct _ZifStoreLocalPrivate	ZifStoreLocalPrivate;
typedef struct _ZifStoreLocalClass	ZifStoreLocalClass;

struct _ZifStoreLocal
{
	ZifStore		 parent;
	ZifStoreLocalPrivate	*priv;
};

struct _ZifStoreLocalClass
{
	ZifStoreClass		 parent_class;
};

GType		 zif_store_local_get_type	(void);
ZifStoreLocal	*zif_store_local_new		(void);
gboolean	 zif_store_local_set_prefix	(ZifStoreLocal		*store,
						 const gchar		*prefix,
						 GError			**error);

G_END_DECLS

#endif /* __ZIF_STORE_LOCAL_H */

