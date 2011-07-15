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
 */

#ifndef __PK_NETWORK_STACK_H__
#define __PK_NETWORK_STACK_H__

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_NETWORK_STACK		(pk_network_stack_get_type ())
#define PK_NETWORK_STACK(o)	   	(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_NETWORK_STACK, PkNetworkStack))
#define PK_NETWORK_STACK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_NETWORK_STACK, PkNetworkStackClass))
#define PK_IS_NETWORK_STACK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_NETWORK_STACK))
#define PK_IS_NETWORK_STACK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_NETWORK_STACK))
#define PK_NETWORK_STACK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_NETWORK_STACK, PkNetworkStackClass))

typedef struct PkNetworkStackPrivate PkNetworkStackPrivate;

typedef struct
{
	GObject			 parent;
} PkNetworkStack;

typedef struct
{
	GObjectClass	 parent_class;
	/* vtable */
	PkNetworkEnum	 (*get_state)			(PkNetworkStack	*nstack);
	gboolean	 (*is_enabled)			(PkNetworkStack	*nstack);
} PkNetworkStackClass;

GType		 pk_network_stack_get_type		(void);
PkNetworkEnum	 pk_network_stack_get_state		(PkNetworkStack	*nstack);
gboolean	 pk_network_stack_is_enabled		(PkNetworkStack	*nstack);

G_END_DECLS

#endif /* __PK_NETWORK_STACK_H__ */
