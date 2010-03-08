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

#ifndef __ZIF_REPO_MD_PRIMARY_H
#define __ZIF_REPO_MD_PRIMARY_H

#include <glib-object.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-repo-md.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPO_MD_PRIMARY		(zif_repo_md_primary_get_type ())
#define ZIF_REPO_MD_PRIMARY(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPO_MD_PRIMARY, ZifRepoMdPrimary))
#define ZIF_REPO_MD_PRIMARY_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPO_MD_PRIMARY, ZifRepoMdPrimaryClass))
#define ZIF_IS_REPO_MD_PRIMARY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPO_MD_PRIMARY))
#define ZIF_IS_REPO_MD_PRIMARY_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPO_MD_PRIMARY))
#define ZIF_REPO_MD_PRIMARY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPO_MD_PRIMARY, ZifRepoMdPrimaryClass))

typedef struct _ZifRepoMdPrimary		ZifRepoMdPrimary;
typedef struct _ZifRepoMdPrimaryPrivate		ZifRepoMdPrimaryPrivate;
typedef struct _ZifRepoMdPrimaryClass		ZifRepoMdPrimaryClass;

struct _ZifRepoMdPrimary
{
	ZifRepoMd			 parent;
	ZifRepoMdPrimaryPrivate		*priv;
};

struct _ZifRepoMdPrimaryClass
{
	ZifRepoMdClass			 parent_class;
};

GType		 zif_repo_md_primary_get_type		(void);
ZifRepoMdPrimary *zif_repo_md_primary_new		(void);
GPtrArray	*zif_repo_md_primary_search_file	(ZifRepoMdPrimary	*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_search_name	(ZifRepoMdPrimary	*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_search_details	(ZifRepoMdPrimary	*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_search_group	(ZifRepoMdPrimary	*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_search_pkgid	(ZifRepoMdPrimary	*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_resolve		(ZifRepoMdPrimary	*md,
							 const gchar		*search,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_get_packages	(ZifRepoMdPrimary	*md,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);
GPtrArray	*zif_repo_md_primary_find_package	(ZifRepoMdPrimary	*md,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 ZifCompletion		*completion,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_REPO_MD_PRIMARY_H */

