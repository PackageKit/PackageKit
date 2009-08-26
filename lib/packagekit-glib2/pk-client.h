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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

/**
 * SECTION:pk-client
 * @short_description: An abstract client GObject
 */

#ifndef __PK_CLIENT_H
#define __PK_CLIENT_H

#include <glib-object.h>
#include <packagekit-glib2/pk-results.h>

G_BEGIN_DECLS

#define PK_TYPE_CLIENT		(pk_client_get_type ())
#define PK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT, PkClient))
#define PK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT, PkClientClass))
#define PK_IS_CLIENT(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT))
#define PK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT))
#define PK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT, PkClientClass))
#define PK_CLIENT_ERROR	(pk_client_error_quark ())
#define PK_CLIENT_TYPE_ERROR	(pk_client_error_get_type ())

typedef struct _PkClientPrivate	PkClientPrivate;
typedef struct _PkClient		PkClient;
typedef struct _PkClientClass		PkClientClass;

struct _PkClient
{
	 GObject		 parent;
	 PkClientPrivate	*priv;
};

struct _PkClientClass
{
	GObjectClass	parent_class;

	/* signals */
	void		(* changed)			(PkClient	*client);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_client_error_quark			(void);
GType		 pk_client_get_type		  	(void);
PkClient	*pk_client_new				(void);

void		 pk_client_resolve_async		(PkClient		*client,
							 GCancellable		*cancellable,
							 /* TODO: progress cb */
							 /* TODO: status cb */
							 /* TODO: package cb */
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
PkResults	*pk_client_resolve_finish		(PkClient		*client,
							 GAsyncResult		*result,
							 GError			**error);

G_END_DECLS

#endif /* __PK_CLIENT_H */

