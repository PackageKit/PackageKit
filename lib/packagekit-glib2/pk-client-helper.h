/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_CLIENT_HELPER_H
#define __PK_CLIENT_HELPER_H

#include <glib-object.h>

#include <packagekit-glib2/pk-source.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_CLIENT_HELPER			(pk_client_helper_get_type ())
#define PK_CLIENT_HELPER(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT_HELPER, PkClientHelper))
#define PK_CLIENT_HELPER_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT_HELPER, PkClientHelperClass))
#define PK_IS_CLIENT_HELPER(o)			(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT_HELPER))
#define PK_IS_CLIENT_HELPER_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT_HELPER))
#define PK_CLIENT_HELPER_GET_CLASS(o)		(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT_HELPER, PkClientHelperClass))

typedef struct _PkClientHelperPrivate	PkClientHelperPrivate;
typedef struct _PkClientHelper		PkClientHelper;
typedef struct _PkClientHelperClass	PkClientHelperClass;

struct _PkClientHelper
{
	 GObject		 parent;
	 PkClientHelperPrivate	*priv;
};

struct _PkClientHelperClass
{
	GObjectClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_client_helper_get_type		(void);
PkClientHelper	*pk_client_helper_new			(void);

gboolean	 pk_client_helper_stop			(PkClientHelper	*client_helper,
							 GError		**error);
gboolean	 pk_client_helper_start			(PkClientHelper	*client_helper,
							 const gchar	*socket_filename,
							 gchar		**argv,
							 gchar		**envp,
							 GError		**error);
gboolean	 pk_client_helper_start_with_socket	(PkClientHelper	*client_helper,
							 GSocket	*socket,
							 gchar		**argv,
							 gchar		**envp,
							 GError		**error);


gboolean 	pk_client_helper_is_active		(PkClientHelper	*client_helper);

G_END_DECLS

#endif /* __PK_CLIENT_HELPER_H */

