/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_DBUS_MONITOR_H
#define __PK_DBUS_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_DBUS_MONITOR		(pk_dbus_monitor_get_type ())
#define PK_DBUS_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_DBUS_MONITOR, PkDbusMonitor))
#define PK_DBUS_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_DBUS_MONITOR, PkDbusMonitorClass))
#define PK_IS_DBUS_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_DBUS_MONITOR))
#define PK_IS_DBUS_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_DBUS_MONITOR))
#define PK_DBUS_MONITOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_DBUS_MONITOR, PkDbusMonitorClass))
#define PK_DBUS_MONITOR_ERROR		(pk_dbus_monitor_error_quark ())
#define PK_DBUS_MONITOR_TYPE_ERROR	(pk_dbus_monitor_error_get_type ())

typedef struct PkDbusMonitorPrivate PkDbusMonitorPrivate;

typedef struct
{
	 GObject		 parent;
	 PkDbusMonitorPrivate	*priv;
} PkDbusMonitor;

typedef struct
{
	GObjectClass	parent_class;
	void		(* connection_changed)		(PkDbusMonitor	*watch,
							 gboolean	 connected);
	void		(* connection_replaced)		(PkDbusMonitor	*watch);
} PkDbusMonitorClass;

typedef enum {
        PK_DBUS_MONITOR_SESSION,
        PK_DBUS_MONITOR_SYSTEM
} PkDbusMonitorType;

GType		 pk_dbus_monitor_get_type	  	(void) G_GNUC_CONST;
PkDbusMonitor	*pk_dbus_monitor_new			(void);
gboolean	 pk_dbus_monitor_assign			(PkDbusMonitor	*monitor,
							 PkDbusMonitorType bus_type,
							 const gchar	*service);
gboolean	 pk_dbus_monitor_is_connected		(PkDbusMonitor	*monitor);

G_END_DECLS

#endif /* __PK_DBUS_MONITOR_H */

