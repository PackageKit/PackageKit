/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_MD_UPDATEINFO_H
#define __ZIF_MD_UPDATEINFO_H

#include <glib-object.h>

#include "zif-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD_UPDATEINFO		(zif_md_updateinfo_get_type ())
#define ZIF_MD_UPDATEINFO(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD_UPDATEINFO, ZifMdUpdateinfo))
#define ZIF_MD_UPDATEINFO_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD_UPDATEINFO, ZifMdUpdateinfoClass))
#define ZIF_IS_MD_UPDATEINFO(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD_UPDATEINFO))
#define ZIF_IS_MD_UPDATEINFO_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD_UPDATEINFO))
#define ZIF_MD_UPDATEINFO_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD_UPDATEINFO, ZifMdUpdateinfoClass))

typedef struct _ZifMdUpdateinfo		ZifMdUpdateinfo;
typedef struct _ZifMdUpdateinfoPrivate	ZifMdUpdateinfoPrivate;
typedef struct _ZifMdUpdateinfoClass	ZifMdUpdateinfoClass;

struct _ZifMdUpdateinfo
{
	ZifMd				 parent;
	ZifMdUpdateinfoPrivate		*priv;
};

struct _ZifMdUpdateinfoClass
{
	ZifMdClass			 parent_class;
};

GType		 zif_md_updateinfo_get_type		(void);
ZifMdUpdateinfo	*zif_md_updateinfo_new			(void);

GPtrArray	*zif_md_updateinfo_get_detail		(ZifMdUpdateinfo	*md,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_md_updateinfo_get_detail_for_package (ZifMdUpdateinfo	*md,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_MD_UPDATEINFO_H */

