/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __PK_BACKEND_INTERNAL_H
#define __PK_BACKEND_INTERNAL_H

#include <glib-object.h>
#include <pk-enum-list.h>
#include "pk-backend.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND		(pk_backend_get_type ())
#define PK_BACKEND(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND, PkBackend))
#define PK_BACKEND_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND, PkBackendClass))
#define PK_IS_BACKEND(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND))
#define PK_IS_BACKEND_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND))
#define PK_BACKEND_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND, PkBackendClass))

typedef struct _PkBackendPrivate PkBackendPrivate;
typedef struct _PkBackendClass PkBackendClass;

struct _PkBackend
{
	GObject		 parent;
	const PkBackendDesc *desc;
	PkBackendPrivate *priv;
};

struct _PkBackendClass
{
	GObjectClass	parent_class;
};

/* general */
GType		 pk_backend_get_type			(void);
PkBackend	*pk_backend_new				(void);
PkEnumList	*pk_backend_get_actions			(PkBackend	*backend);
PkEnumList	*pk_backend_get_groups			(PkBackend	*backend);
PkEnumList	*pk_backend_get_filters			(PkBackend	*backend);
gdouble		 pk_backend_get_runtime			(PkBackend	*backend);
gboolean	 pk_backend_load			(PkBackend      *backend,
							 const gchar	*name);
gboolean	 pk_backend_run				(PkBackend      *backend);
gboolean	 pk_backend_unload			(PkBackend      *backend);
const gchar	*pk_backend_get_name			(PkBackend	*backend);
gboolean	 pk_backend_cancel			(PkBackend	*backend);
gboolean	 pk_backend_get_depends			(PkBackend	*backend,
							 const gchar	*package_id);
gboolean	 pk_backend_get_update_detail		(PkBackend	*backend,
							 const gchar	*package_id);
gboolean	 pk_backend_get_description		(PkBackend	*backend,
							 const gchar	*package_id);
gboolean	 pk_backend_get_requires		(PkBackend	*backend,
							 const gchar	*package_id);
gboolean	 pk_backend_get_updates			(PkBackend	*backend);
gboolean	 pk_backend_install_package		(PkBackend	*backend,
							 const gchar	*package_id);
gboolean	 pk_backend_install_file		(PkBackend	*backend,
							 const gchar	*full_path);
gboolean	 pk_backend_refresh_cache		(PkBackend	*backend,
							 gboolean	 force);
gboolean	 pk_backend_remove_package		(PkBackend	*backend,
							 const gchar	*package_id,
							 gboolean	 allow_deps);
gboolean	 pk_backend_search_details		(PkBackend	*backend,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_resolve			(PkBackend	*backend,
							 const gchar	*package);
gboolean	 pk_backend_search_file			(PkBackend	*backend,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_search_group		(PkBackend	*backend,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_search_name			(PkBackend	*backend,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_update_package		(PkBackend	*backend,
							 const gchar	*package_id);
gboolean	 pk_backend_update_system		(PkBackend	*backend);
gboolean	 pk_backend_get_status			(PkBackend	*backend,
							 PkStatusEnum	*status);
gboolean	 pk_backend_get_role			(PkBackend	*backend,
							 PkRoleEnum	*role,
							 const gchar	**package_id);
gboolean	 pk_backend_get_percentage		(PkBackend	*backend,
							 guint		*percentage);
gboolean	 pk_backend_get_sub_percentage		(PkBackend	*backend,
							 guint		*percentage);
gboolean	 pk_backend_get_package			(PkBackend	*backend,
							 gchar		**package_id);

/* these are external in nature, but we shouldn't be using them in helpers */
gboolean	 pk_backend_set_role			(PkBackend	*backend,
							 PkRoleEnum	 role);
gboolean	 pk_backend_not_implemented_yet		(PkBackend	*backend,
							 const gchar	*method);

G_END_DECLS

#endif /* __PK_BACKEND_INTERNAL_H */
