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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_MONITOR_H
#define __ZIF_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define ZIF_TYPE_MONITOR		(zif_monitor_get_type ())
#define ZIF_MONITOR(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_MONITOR, ZifMonitor))
#define ZIF_MONITOR_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_MONITOR, ZifMonitorClass))
#define ZIF_IS_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_MONITOR))
#define ZIF_IS_MONITOR_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_MONITOR))
#define ZIF_MONITOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_MONITOR, ZifMonitorClass))
#define ZIF_MONITOR_ERROR		(zif_monitor_error_quark ())

typedef struct _ZifMonitor		ZifMonitor;
typedef struct _ZifMonitorPrivate	ZifMonitorPrivate;
typedef struct _ZifMonitorClass		ZifMonitorClass;

struct _ZifMonitor
{
	GObject			 parent;
	ZifMonitorPrivate	*priv;
};

struct _ZifMonitorClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	ZIF_MONITOR_ERROR_FAILED,
	ZIF_MONITOR_ERROR_LAST
} ZifMonitorError;

GType		 zif_monitor_get_type		(void);
GQuark		 zif_monitor_error_quark	(void);
ZifMonitor	*zif_monitor_new		(void);
gboolean	 zif_monitor_add_watch		(ZifMonitor	*monitor,
						 const gchar	*filename,
						 GError		**error);

G_END_DECLS

#endif /* __ZIF_MONITOR_H */

