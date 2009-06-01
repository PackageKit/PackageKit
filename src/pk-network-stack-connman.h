/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009  Intel Corporation. All rights reserved.
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __PK_NETWORK_STACK_CONNMAN_H__
#define __PK_NETWORK_STACK_CONNMAN_H__

#include <glib-object.h>

#include "pk-network-stack.h"

G_BEGIN_DECLS

#define PK_TYPE_NETWORK_STACK_CONNMAN  		(pk_network_stack_connman_get_type ())
#define PK_NETWORK_STACK_CONNMAN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_NETWORK_STACK_CONNMAN, PkNetworkStackConnman))
#define PK_NETWORK_STACK_CONNMAN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_NETWORK_STACK_CONNMAN, PkNetworkStackConnmanClass))
#define PK_IS_NETWORK_STACK_CONNMAN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_NETWORK_STACK_CONNMAN))
#define PK_IS_NETWORK_STACK_CONNMAN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_NETWORK_STACK_CONNMAN))
#define PK_NETWORK_STACK_CONNMAN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_NETWORK_STACK_CONNMAN, PkNetworkStackConnmanClass))

typedef struct PkNetworkStackConnmanPrivate PkNetworkStackConnmanPrivate;

typedef struct
{
	PkNetworkStack			 parent;
	PkNetworkStackConnmanPrivate	*priv;
} PkNetworkStackConnman;

typedef struct
{
	PkNetworkStackClass		 parent_class;
} PkNetworkStackConnmanClass;

GType			 pk_network_stack_connman_get_type	(void);
PkNetworkStackConnman	*pk_network_stack_connman_new		(void);

G_END_DECLS

#endif /* __PK_NETWORK_STACK_CONNMAN_H__ */

