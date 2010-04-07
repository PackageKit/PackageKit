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

#ifndef __ZIF_MD_METALINK_H
#define __ZIF_MD_METALINK_H

#include <glib-object.h>

#include "zif-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_MD_METALINK		(zif_md_metalink_get_type ())
#define ZIF_MD_METALINK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MD_METALINK, ZifMdMetalink))
#define ZIF_MD_METALINK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MD_METALINK, ZifMdMetalinkClass))
#define ZIF_IS_MD_METALINK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MD_METALINK))
#define ZIF_IS_MD_METALINK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MD_METALINK))
#define ZIF_MD_METALINK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MD_METALINK, ZifMdMetalinkClass))

typedef struct _ZifMdMetalink		ZifMdMetalink;
typedef struct _ZifMdMetalinkPrivate	ZifMdMetalinkPrivate;
typedef struct _ZifMdMetalinkClass	ZifMdMetalinkClass;

struct _ZifMdMetalink
{
	ZifMd				 parent;
	ZifMdMetalinkPrivate		*priv;
};

struct _ZifMdMetalinkClass
{
	ZifMdClass			 parent_class;
};

GType		 zif_md_metalink_get_type		(void);
ZifMdMetalink	*zif_md_metalink_new			(void);
GPtrArray	*zif_md_metalink_get_uris		(ZifMdMetalink		*md,
							 guint			 threshold,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_MD_METALINK_H */

