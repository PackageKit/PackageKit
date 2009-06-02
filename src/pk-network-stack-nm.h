/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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

#ifndef __PK_NETWORK_STACK_NM_H__
#define __PK_NETWORK_STACK_NM_H__

#include <glib-object.h>

#include "pk-network-stack.h"

G_BEGIN_DECLS

#define PK_TYPE_NETWORK_STACK_NM  		(pk_network_stack_nm_get_type ())
#define PK_NETWORK_STACK_NM(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_NETWORK_STACK_NM, PkNetworkStackNm))
#define PK_NETWORK_STACK_NM_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_NETWORK_STACK_NM, PkNetworkStackNmClass))
#define PK_IS_NETWORK_STACK_NM(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_NETWORK_STACK_NM))
#define PK_IS_NETWORK_STACK_NM_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_NETWORK_STACK_NM))
#define PK_NETWORK_STACK_NM_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_NETWORK_STACK_NM, PkNetworkStackNmClass))

typedef struct PkNetworkStackNmPrivate PkNetworkStackNmPrivate;

typedef struct
{
	PkNetworkStack			 parent;
	PkNetworkStackNmPrivate		*priv;
} PkNetworkStackNm;

typedef struct
{
	PkNetworkStackClass		 parent_class;
} PkNetworkStackNmClass;

GType			 pk_network_stack_nm_get_type		(void);
PkNetworkStackNm	*pk_network_stack_nm_new		(void);

G_END_DECLS

#endif /* __PK_NETWORK_STACK_NM_H__ */

