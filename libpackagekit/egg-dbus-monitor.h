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

#ifndef __EGG_DBUS_MONITOR_H
#define __EGG_DBUS_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define EGG_TYPE_DBUS_MONITOR		(egg_dbus_monitor_get_type ())
#define EGG_DBUS_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), EGG_TYPE_DBUS_MONITOR, EggDbusMonitor))
#define EGG_DBUS_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), EGG_TYPE_DBUS_MONITOR, EggDbusMonitorClass))
#define EGG_IS_DBUS_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), EGG_TYPE_DBUS_MONITOR))
#define EGG_IS_DBUS_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), EGG_TYPE_DBUS_MONITOR))
#define EGG_DBUS_MONITOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), EGG_TYPE_DBUS_MONITOR, EggDbusMonitorClass))
#define EGG_DBUS_MONITOR_ERROR		(egg_dbus_monitor_error_quark ())
#define EGG_DBUS_MONITOR_TYPE_ERROR	(egg_dbus_monitor_error_get_type ())

typedef struct EggDbusMonitorPrivate EggDbusMonitorPrivate;

typedef struct
{
	 GObject		 parent;
	 EggDbusMonitorPrivate	*priv;
} EggDbusMonitor;

typedef struct
{
	GObjectClass	parent_class;
	void		(* connection_changed)		(EggDbusMonitor	*watch,
							 gboolean	 connected);
	void		(* connection_replaced)		(EggDbusMonitor	*watch);
} EggDbusMonitorClass;

typedef enum {
        EGG_DBUS_MONITOR_SESSION,
        EGG_DBUS_MONITOR_SYSTEM
} EggDbusMonitorType;

GType		 egg_dbus_monitor_get_type	  	(void) G_GNUC_CONST;
EggDbusMonitor	*egg_dbus_monitor_new			(void);
gboolean	 egg_dbus_monitor_assign			(EggDbusMonitor	*monitor,
							 EggDbusMonitorType bus_type,
							 const gchar	*service);
gboolean	 egg_dbus_monitor_is_connected		(EggDbusMonitor	*monitor);

G_END_DECLS

#endif /* __EGG_DBUS_MONITOR_H */

