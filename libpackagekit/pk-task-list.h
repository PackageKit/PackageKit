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

#ifndef __PK_TASK_LIST_H
#define __PK_TASK_LIST_H

#include <glib-object.h>
#include "pk-task-monitor.h"
#include "pk-enum.h"

G_BEGIN_DECLS

#define PK_TYPE_TASK_LIST		(pk_task_list_get_type ())
#define PK_TASK_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK_LIST, PkTaskList))
#define PK_TASK_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK_LIST, PkTaskListClass))
#define PK_IS_TASK_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK_LIST))
#define PK_IS_TASK_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK_LIST))
#define PK_TASK_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK_LIST, PkTaskListClass))

typedef struct PkTaskListPrivate PkTaskListPrivate;

typedef struct
{
	guint			 job;
	PkStatusEnum		 status;
	PkRoleEnum		 role;
	gchar			*package_id;
	PkTaskMonitor		*monitor;
	gboolean		 valid;
} PkTaskListItem;

typedef struct
{
	GObject			 parent;
	PkTaskListPrivate	*priv;
} PkTaskList;

typedef struct
{
	GObjectClass	parent_class;
} PkTaskListClass;

GType		 pk_task_list_get_type			(void);
PkTaskList	*pk_task_list_new			(void);

gboolean	 pk_task_list_refresh			(PkTaskList	*tlist);
gboolean	 pk_task_list_print			(PkTaskList	*tlist);
gboolean	 pk_task_list_free			(PkTaskList	*tlist);
GPtrArray	*pk_task_list_get_latest		(PkTaskList	*tlist);

G_END_DECLS

#endif /* __PK_TASK_LIST_H */
