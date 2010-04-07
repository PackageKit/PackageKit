/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_MD_MIRRORLIST_H
#define __ZIF_MD_MIRRORLIST_H

#include <glib-object.h>

#include "zif-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD_MIRRORLIST		(zif_md_mirrorlist_get_type ())
#define ZIF_MD_MIRRORLIST(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD_MIRRORLIST, ZifMdMirrorlist))
#define ZIF_MD_MIRRORLIST_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD_MIRRORLIST, ZifMdMirrorlistClass))
#define ZIF_IS_MD_MIRRORLIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD_MIRRORLIST))
#define ZIF_IS_MD_MIRRORLIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD_MIRRORLIST))
#define ZIF_MD_MIRRORLIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD_MIRRORLIST, ZifMdMirrorlistClass))

typedef struct _ZifMdMirrorlist		ZifMdMirrorlist;
typedef struct _ZifMdMirrorlistPrivate	ZifMdMirrorlistPrivate;
typedef struct _ZifMdMirrorlistClass	ZifMdMirrorlistClass;

struct _ZifMdMirrorlist
{
	ZifMd				 parent;
	ZifMdMirrorlistPrivate		*priv;
};

struct _ZifMdMirrorlistClass
{
	ZifMdClass			 parent_class;
};

GType		 zif_md_mirrorlist_get_type		(void);
ZifMdMirrorlist *zif_md_mirrorlist_new			(void);
GPtrArray	*zif_md_mirrorlist_get_uris		(ZifMdMirrorlist	*md,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_MD_MIRRORLIST_H */

