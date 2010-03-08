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

#ifndef __ZIF_REPO_MD_FILELISTS_H
#define __ZIF_REPO_MD_FILELISTS_H

#include <glib-object.h>

#include "zif-repo-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPO_MD_FILELISTS		(zif_repo_md_filelists_get_type ())
#define ZIF_REPO_MD_FILELISTS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPO_MD_FILELISTS, ZifRepoMdFilelists))
#define ZIF_REPO_MD_FILELISTS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPO_MD_FILELISTS, ZifRepoMdFilelistsClass))
#define ZIF_IS_REPO_MD_FILELISTS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPO_MD_FILELISTS))
#define ZIF_IS_REPO_MD_FILELISTS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPO_MD_FILELISTS))
#define ZIF_REPO_MD_FILELISTS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPO_MD_FILELISTS, ZifRepoMdFilelistsClass))

typedef struct _ZifRepoMdFilelists		ZifRepoMdFilelists;
typedef struct _ZifRepoMdFilelistsPrivate	ZifRepoMdFilelistsPrivate;
typedef struct _ZifRepoMdFilelistsClass		ZifRepoMdFilelistsClass;

struct _ZifRepoMdFilelists
{
	ZifRepoMd			 parent;
	ZifRepoMdFilelistsPrivate	*priv;
};

struct _ZifRepoMdFilelistsClass
{
	ZifRepoMdClass			 parent_class;
};

GType			 zif_repo_md_filelists_get_type		(void);
ZifRepoMdFilelists	*zif_repo_md_filelists_new		(void);
GPtrArray		*zif_repo_md_filelists_search_file	(ZifRepoMdFilelists	*md,
								 const gchar		*search,
								 GCancellable		*cancellable,
								 ZifCompletion		*completion,
								 GError			**error);

G_END_DECLS

#endif /* __ZIF_REPO_MD_FILELISTS_H */

