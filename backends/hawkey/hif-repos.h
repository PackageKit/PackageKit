/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __HIF_REPOS_H
#define __HIF_REPOS_H

#include <glib.h>
#include <glib-object.h>

#include <hawkey/repo.h>
#include <hawkey/package.h>

#include "hif-state.h"
#include "hif-source.h"

typedef struct _HifRepos	HifRepos;
typedef struct _HifReposClass	HifReposClass;

struct _HifRepos {
	GObject		 parent_instance;
};

struct _HifReposClass {
	GObjectClass	 parent_class;
	/* signals */
	void		(* changed)		(HifRepos		*self);
};

#define HIF_TYPE_REPOS		(hif_repos_get_type ())
#define HIF_REPOS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HIF_TYPE_REPOS, HifRepos))
#define HIF_IS_REPOS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), HIF_TYPE_REPOS))

GType		 hif_repos_get_type		(void);
HifRepos	*hif_repos_new			(GKeyFile		*config);

gboolean	 hif_repos_has_removable	(HifRepos		*self);
GPtrArray	*hif_repos_get_sources		(HifRepos		*self,
						 GError			**error);
HifSource	*hif_repos_get_source_by_id	(HifRepos		*self,
						 const gchar		*id,
						 GError			**error);

#endif /* __HIF_REPOS_H */
