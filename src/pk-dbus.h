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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __PK_DBUS_H
#define __PK_DBUS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_DBUS		(pk_dbus_get_type ())
#define PK_DBUS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_DBUS, PkDbus))
#define PK_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_DBUS, PkDbusClass))
#define PK_IS_DBUS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_DBUS))
#define PK_IS_DBUS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_DBUS))
#define PK_DBUS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_DBUS, PkDbusClass))

typedef struct PkDbusPrivate PkDbusPrivate;

typedef struct
{
	GObject			 parent;
	PkDbusPrivate		*priv;
} PkDbus;

typedef struct
{
	GObjectClass		 parent_class;
} PkDbusClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkDbus, g_object_unref)
#endif

GType		 pk_dbus_get_type		(void);
PkDbus		*pk_dbus_new			(void);
gboolean	 pk_dbus_connect		(PkDbus		*dbus,
						 GError		**error);
guint		 pk_dbus_get_uid		(PkDbus		*dbus,
						 const gchar	*sender);
gchar		*pk_dbus_get_cmdline		(PkDbus		*dbus,
						 const gchar	*sender);
gchar		*pk_dbus_get_session		(PkDbus		*dbus,
						 const gchar	*sender);

G_END_DECLS

#endif /* __PK_DBUS_H */

