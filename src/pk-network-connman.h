/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009  Intel Corporation. All rights reserved.
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

/**
 * SECTION:pk-network-connman
 * @short_description: An ConnmMan access GObject
 *
 * This allows a switchable network backend.
 */

#ifndef __PK_NETWORK_CONNMAN_H
#define __PK_NETWORK_CONNMAN_H

#include <glib-object.h>
#include <packagekit-glib/packagekit.h>

G_BEGIN_DECLS

#define PK_TYPE_NETWORK_CONNMAN		(pk_network_connman_get_type ())
#define PK_NETWORK_CONNMAN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_NETWORK_CONNMAN, PkNetworkConnman))
#define PK_NETWORK_CONNMAN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_NETWORK_CONNMAN, PkNetworkConnmanClass))
#define PK_IS_NETWORK_CONNMAN(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_NETWORK_CONNMAN))
#define PK_IS_NETWORK_CONNMAN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_NETWORK_CONNMAN))
#define PK_NETWORK_CONNMAN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_NETWORK_CONNMAN, PkNetworkConnmanClass))
#define PK_NETWORK_CONNMAN_ERROR	(pk_network_connman_error_quark ())
#define PK_NETWORK_CONNMAN_TYPE_ERROR	(pk_network_connman_error_get_type ())

typedef struct _PkNetworkConnmanPrivate	PkNetworkConnmanPrivate;
typedef struct _PkNetworkConnman	PkNetworkConnman;
typedef struct _PkNetworkConnmanClass  PkNetworkConnmanClass;

struct _PkNetworkConnman
{
	GObject			 parent;
	PkNetworkConnmanPrivate	*priv;
};

struct _PkNetworkConnmanClass
{
	GObjectClass		 parent_class;
};

GType			 pk_network_connman_get_type		(void);
PkNetworkConnman	*pk_network_connman_new			(void);
PkNetworkEnum		 pk_network_connman_get_network_state	(PkNetworkConnman	*network_connman);

G_END_DECLS

#endif /* __PK_NETWORK_CONNMAN_H */

