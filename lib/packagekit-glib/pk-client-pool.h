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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_CLIENT_POOL_H
#define __PK_CLIENT_POOL_H

#include <glib-object.h>
#include <packagekit-glib/pk-client.h>

G_BEGIN_DECLS

#define PK_TYPE_CLIENT_POOL		(pk_client_pool_get_type ())
#define PK_CLIENT_POOL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT_POOL, PkClientPool))
#define PK_CLIENT_POOL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT_POOL, PkClientPoolClass))
#define PK_IS_CLIENT_POOL(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT_POOL))
#define PK_IS_CLIENT_POOL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT_POOL))
#define PK_CLIENT_POOL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT_POOL, PkClientPoolClass))

typedef struct _PkClientPoolPrivate	PkClientPoolPrivate;
typedef struct _PkClientPool		PkClientPool;
typedef struct _PkClientPoolClass	PkClientPoolClass;

struct _PkClientPool
{
	 PkObjList		 parent;
	 PkClientPoolPrivate	*priv;
};

struct _PkClientPoolClass
{
	PkObjListClass		parent_class;
	/* Padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType			 pk_client_pool_get_type	(void);
PkClientPool		*pk_client_pool_new		(void);
guint			 pk_client_pool_get_size	(PkClientPool	*pool);
PkClient		*pk_client_pool_create		(PkClientPool	*pool);
gboolean		 pk_client_pool_remove		(PkClientPool	*pool,
							 PkClient	*client);
gboolean		 pk_client_pool_connect		(PkClientPool	*pool,
							 const gchar	*signal_name,
							 GCallback	 c_handler,
							 GObject	*object);
gboolean		 pk_client_pool_disconnect	(PkClientPool	*pool,
							 const gchar	*signal_name);

G_END_DECLS

#endif /* __PK_CLIENT_POOL_H */

