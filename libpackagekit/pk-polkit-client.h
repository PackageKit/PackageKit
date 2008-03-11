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

#ifndef __PK_POLKIT_CLIENT_H
#define __PK_POLKIT_CLIENT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_POLKIT_CLIENT		(pk_polkit_client_get_type ())
#define PK_POLKIT_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_POLKIT_CLIENT, PkPolkitClient))
#define PK_POLKIT_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_POLKIT_CLIENT, PkPolkitClientClass))
#define PK_IS_POLKIT_CLIENT(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_POLKIT_CLIENT))
#define PK_IS_POLKIT_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_POLKIT_CLIENT))
#define PK_POLKIT_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_POLKIT_CLIENT, PkPolkitClientClass))

typedef struct _PkPolkitClientPrivate	PkPolkitClientPrivate;
typedef struct _PkPolkitClient		PkPolkitClient;
typedef struct _PkPolkitClientClass	PkPolkitClientClass;

struct _PkPolkitClient
{
	GObject			 parent;
	PkPolkitClientPrivate	*priv;
};

struct _PkPolkitClientClass
{
	GObjectClass	parent_class;
};

GType		 pk_polkit_client_get_type		(void);
PkPolkitClient	*pk_polkit_client_new			(void);

gboolean	 pk_polkit_client_gain_privilege	(PkPolkitClient	*pclient,
							 const gchar	*pk_action)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_polkit_client_gain_privilege_str	(PkPolkitClient	*pclient,
							 const gchar	*error_str)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_polkit_client_error_denied_by_policy(GError		*error);

G_END_DECLS

#endif /* __PK_POLKIT_CLIENT_H */
