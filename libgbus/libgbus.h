/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __LIBGBUS_H
#define __LIBGBUS_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define LIBGBUS_TYPE		(libgbus_get_type ())
#define LIBGBUS_OBJECT(o)	(G_TYPE_CHECK_INSTANCE_CAST ((o), LIBGBUS_TYPE, LibGBus))
#define LIBGBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), LIBGBUS_TYPE, LibGBusClass))
#define IS_LIBGBUS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), LIBGBUS_TYPE))
#define IS_LIBGBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), LIBGBUS_TYPE))
#define LIBGBUS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), LIBGBUS_TYPE, LibGBusClass))

typedef struct LibGBusPrivate LibGBusPrivate;

typedef struct
{
	GObject		parent;
	LibGBusPrivate *priv;
} LibGBus;

typedef struct
{
	GObjectClass	parent_class;
	void		(* connection_changed)	(LibGBus	*watch,
						 gboolean	 connected);
	void		(* connection_replaced)	(LibGBus	*watch);
} LibGBusClass;

typedef enum {
        LIBGBUS_SESSION,
        LIBGBUS_SYSTEM
} LibGBusType;

GType		 libgbus_get_type		(void);
LibGBus		*libgbus_new			(void);

gboolean	 libgbus_assign			(LibGBus	*libgbus,
						 LibGBusType	 bus_type,
						 const gchar	*service);
gboolean	 libgbus_is_connected		(LibGBus	*libgbus);

G_END_DECLS

#endif	/* __LIBGBUS_H */

