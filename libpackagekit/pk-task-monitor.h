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

#ifndef __PK_TASK_MONITOR_H
#define __PK_TASK_MONITOR_H

#include <glib-object.h>
#include "pk-enum.h"

G_BEGIN_DECLS

#define PK_TYPE_TASK_MONITOR		(pk_task_monitor_get_type ())
#define PK_TASK_MONITOR(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK_MONITOR, PkTaskMonitor))
#define PK_TASK_MONITOR_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK_MONITOR, PkTaskMonitorClass))
#define PK_IS_TASK_MONITOR(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK_MONITOR))
#define PK_IS_TASK_MONITOR_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK_MONITOR))
#define PK_TASK_MONITOR_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK_MONITOR, PkTaskMonitorClass))

typedef struct PkTaskMonitorPrivate PkTaskMonitorPrivate;

typedef struct
{
	GObject			 parent;
	PkTaskMonitorPrivate	*priv;
} PkTaskMonitor;

typedef struct
{
	GObjectClass	parent_class;
} PkTaskMonitorClass;

GType		 pk_task_monitor_get_type		(void);
PkTaskMonitor	*pk_task_monitor_new			(void);

gboolean	 pk_task_monitor_set_job		(PkTaskMonitor	*tmonitor,
							 guint		 job);
guint		 pk_task_monitor_get_job		(PkTaskMonitor	*tmonitor);
gboolean	 pk_task_monitor_get_status		(PkTaskMonitor	*tmonitor,
							 PkTaskStatus	*status);
gboolean	 pk_task_monitor_get_role		(PkTaskMonitor	*tmonitor,
							 PkTaskRole	*role,
							 gchar		**package_id);

G_END_DECLS

#endif /* __PK_TASK_MONITOR_H */
