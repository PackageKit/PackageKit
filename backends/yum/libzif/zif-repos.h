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

#ifndef __ZIF_REPOS_H
#define __ZIF_REPOS_H

#include <glib-object.h>
#include "zif-store-remote.h"

G_BEGIN_DECLS

#define ZIF_TYPE_REPOS		(zif_repos_get_type ())
#define ZIF_REPOS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_REPOS, ZifRepos))
#define ZIF_REPOS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_REPOS, ZifReposClass))
#define ZIF_IS_REPOS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_REPOS))
#define ZIF_IS_REPOS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_REPOS))
#define ZIF_REPOS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_REPOS, ZifReposClass))
#define ZIF_REPOS_ERROR		(zif_repos_error_quark ())

typedef struct _ZifRepos	ZifRepos;
typedef struct _ZifReposPrivate	ZifReposPrivate;
typedef struct _ZifReposClass	ZifReposClass;

struct _ZifRepos
{
	GObject			 parent;
	ZifReposPrivate		*priv;
};

struct _ZifReposClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	ZIF_REPOS_ERROR_FAILED,
	ZIF_REPOS_ERROR_LAST
} ZifReposError;

GType		 zif_repos_get_type		(void);
GQuark		 zif_repos_error_quark			(void);
ZifRepos	*zif_repos_new			(void);
gboolean	 zif_repos_set_repos_dir	(ZifRepos		*repos,
						 const gchar		*repos_dir,
						 GError			**error);
gboolean	 zif_repos_load			(ZifRepos		*repos,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_repos_get_stores		(ZifRepos		*repos,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
GPtrArray	*zif_repos_get_stores_enabled	(ZifRepos		*repos,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
ZifStoreRemote	*zif_repos_get_store		(ZifRepos		*repos,
						 const gchar		*id,
						 GCancellable		*cancellable,
						 ZifCompletion		*completion,
						 GError			**error);
G_END_DECLS

#endif /* __ZIF_REPOS_H */

