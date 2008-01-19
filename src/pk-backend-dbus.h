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

#ifndef __PK_BACKEND_DBUS_H
#define __PK_BACKEND_DBUS_H

#include <glib-object.h>
#include <pk-enum-list.h>
#include "pk-backend.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_DBUS		(pk_backend_dbus_get_type ())
#define PK_BACKEND_DBUS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND_DBUS, PkBackendDbus))
#define PK_BACKEND_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND_DBUS, PkBackendDbusClass))
#define PK_IS_BACKEND_DBUS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND_DBUS))
#define PK_IS_BACKEND_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND_DBUS))
#define PK_BACKEND_DBUS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND_DBUS, PkBackendDbusClass))

typedef struct PkBackendDbusPrivate PkBackendDbusPrivate;

typedef struct
{
	 GObject		 parent;
	 PkBackendDbusPrivate	*priv;
} PkBackendDbus;

typedef struct
{
	GObjectClass	parent_class;
} PkBackendDbusClass;

/* general */
GType		 pk_backend_dbus_get_type		(void);
PkBackendDbus	*pk_backend_dbus_new			(void);
gboolean	 pk_backend_dbus_search_name		(PkBackendDbus	*backend_dbus,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_backend_dbus_kill			(PkBackendDbus	*backend_dbus);
gboolean	 pk_backend_dbus_set_name		(PkBackendDbus	*backend_dbus,
							 const gchar	*service,
							 const gchar	*interface,
							 const gchar	*path);


G_END_DECLS

#endif /* __PK_BACKEND_DBUS_H */
