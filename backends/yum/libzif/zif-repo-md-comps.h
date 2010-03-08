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

#ifndef __ZIF_REPO_MD_COMPS_H
#define __ZIF_REPO_MD_COMPS_H

#include <glib-object.h>

#include "zif-repo-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPO_MD_COMPS		(zif_repo_md_comps_get_type ())
#define ZIF_REPO_MD_COMPS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPO_MD_COMPS, ZifRepoMdComps))
#define ZIF_REPO_MD_COMPS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPO_MD_COMPS, ZifRepoMdCompsClass))
#define ZIF_IS_REPO_MD_COMPS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPO_MD_COMPS))
#define ZIF_IS_REPO_MD_COMPS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPO_MD_COMPS))
#define ZIF_REPO_MD_COMPS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPO_MD_COMPS, ZifRepoMdCompsClass))

typedef struct _ZifRepoMdComps		ZifRepoMdComps;
typedef struct _ZifRepoMdCompsPrivate	ZifRepoMdCompsPrivate;
typedef struct _ZifRepoMdCompsClass	ZifRepoMdCompsClass;

struct _ZifRepoMdComps
{
	ZifRepoMd			 parent;
	ZifRepoMdCompsPrivate		*priv;
};

struct _ZifRepoMdCompsClass
{
	ZifRepoMdClass			 parent_class;
};

GType		 zif_repo_md_comps_get_type		(void);
ZifRepoMdComps	*zif_repo_md_comps_new			(void);

GPtrArray	*zif_repo_md_comps_get_categories		(ZifRepoMdComps		*md,
								 GCancellable		*cancellable,
								 ZifCompletion		*completion,
								 GError			**error);
GPtrArray	*zif_repo_md_comps_get_groups_for_category	(ZifRepoMdComps		*md,
								 const gchar		*category_id,
								 GCancellable		*cancellable,
								 ZifCompletion		*completion,
								 GError			**error);
GPtrArray	*zif_repo_md_comps_get_packages_for_group	(ZifRepoMdComps		*md,
								 const gchar		*group_id,
								 GCancellable		*cancellable,
								 ZifCompletion		*completion,
								 GError			**error);

G_END_DECLS

#endif /* __ZIF_REPO_MD_COMPS_H */

