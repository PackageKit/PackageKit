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

#ifndef __ZIF_REPO_MD_MIRRORLIST_H
#define __ZIF_REPO_MD_MIRRORLIST_H

#include <glib-object.h>

#include "zif-repo-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPO_MD_MIRRORLIST		(zif_repo_md_mirrorlist_get_type ())
#define ZIF_REPO_MD_MIRRORLIST(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPO_MD_MIRRORLIST, ZifRepoMdMirrorlist))
#define ZIF_REPO_MD_MIRRORLIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPO_MD_MIRRORLIST, ZifRepoMdMirrorlistClass))
#define ZIF_IS_REPO_MD_MIRRORLIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPO_MD_MIRRORLIST))
#define ZIF_IS_REPO_MD_MIRRORLIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPO_MD_MIRRORLIST))
#define ZIF_REPO_MD_MIRRORLIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPO_MD_MIRRORLIST, ZifRepoMdMirrorlistClass))

typedef struct _ZifRepoMdMirrorlist		ZifRepoMdMirrorlist;
typedef struct _ZifRepoMdMirrorlistPrivate	ZifRepoMdMirrorlistPrivate;
typedef struct _ZifRepoMdMirrorlistClass	ZifRepoMdMirrorlistClass;

struct _ZifRepoMdMirrorlist
{
	ZifRepoMd			 parent;
	ZifRepoMdMirrorlistPrivate	*priv;
};

struct _ZifRepoMdMirrorlistClass
{
	ZifRepoMdClass			 parent_class;
};

GType		 zif_repo_md_mirrorlist_get_type		(void);
ZifRepoMdMirrorlist *zif_repo_md_mirrorlist_new		(void);
GPtrArray	*zif_repo_md_mirrorlist_get_uris	(ZifRepoMdMirrorlist	*md,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_REPO_MD_MIRRORLIST_H */

