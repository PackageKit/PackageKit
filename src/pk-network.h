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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:pk-network
 * @short_description: An abstract network access GObject
 *
 * This allows a switchable network backend.
 */

#ifndef __PK_NETWORK_H
#define __PK_NETWORK_H

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_NETWORK		(pk_network_get_type ())
#define PK_NETWORK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_NETWORK, PkNetwork))
#define PK_NETWORK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_NETWORK, PkNetworkClass))
#define PK_IS_NETWORK(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_NETWORK))
#define PK_IS_NETWORK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_NETWORK))
#define PK_NETWORK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_NETWORK, PkNetworkClass))
#define PK_NETWORK_ERROR	(pk_network_error_quark ())
#define PK_NETWORK_TYPE_ERROR	(pk_network_error_get_type ())

typedef struct _PkNetworkPrivate	PkNetworkPrivate;
typedef struct _PkNetwork		PkNetwork;
typedef struct _PkNetworkClass		PkNetworkClass;

struct _PkNetwork
{
	 GObject		 parent;
	 PkNetworkPrivate	*priv;
};

struct _PkNetworkClass
{
	GObjectClass	parent_class;
};

GType		 pk_network_get_type		  	(void);
PkNetwork	*pk_network_new				(void);
PkNetworkEnum	 pk_network_get_network_state		(PkNetwork	*network);

G_END_DECLS

#endif /* __PK_NETWORK_H */
