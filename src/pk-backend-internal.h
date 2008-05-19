/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
	GObject			 parent;
	PkBackendDesc		*desc;
	PkBackendPrivate	*priv;
};

struct _PkBackendClass
{
	GObjectClass	parent_class;
};

GType		 pk_backend_get_type			(void) G_GNUC_CONST;
PkBackend	*pk_backend_new				(void);
gboolean	 pk_backend_lock			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_unlock			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_reset			(PkBackend	*backend);
gboolean	 pk_backend_set_name			(PkBackend	*backend,
							 const gchar	*name)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_set_proxy			(PkBackend	*backend,
							 const gchar	*proxy_http,
							 const gchar	*proxy_ftp);
gchar		*pk_backend_get_name			(PkBackend	*backend)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_backend_get_backend_detail		(PkBackend	*backend,
							 gchar		**name,
							 gchar		**author);
PkGroupEnum	 pk_backend_get_groups			(PkBackend	*backend);
PkFilterEnum	 pk_backend_get_filters			(PkBackend	*backend);
PkRoleEnum	 pk_backend_get_actions			(PkBackend	*backend);

G_END_DECLS

#endif /* __PK_BACKEND_INTERNAL_H */

