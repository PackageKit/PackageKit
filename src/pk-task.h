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

#ifndef __PK_TASK_H
#define __PK_TASK_H

#include <glib-object.h>
#include "pk-task-utils.h"
#include "pk-spawn.h"

G_BEGIN_DECLS

#define PK_TYPE_TASK		(pk_task_get_type ())
#define PK_TASK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK, PkTask))
#define PK_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK, PkTaskClass))
#define PK_IS_TASK(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK))
#define PK_IS_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK))
#define PK_TASK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK, PkTaskClass))

typedef struct PkTaskPrivate PkTaskPrivate;

typedef enum {
	PK_TASK_JOB_STATUS_CHANGED,
	PK_TASK_PERCENTAGE_CHANGED,
	PK_TASK_SUB_PERCENTAGE_CHANGED,
	PK_TASK_NO_PERCENTAGE_UPDATES,
	PK_TASK_DESCRIPTION,
	PK_TASK_PACKAGE,
	PK_TASK_ERROR_CODE,
	PK_TASK_REQUIRE_RESTART,
	PK_TASK_FINISHED,
	PK_TASK_ALLOW_INTERRUPT,
	PK_TASK_LAST_SIGNAL
} PkSignals;

typedef struct
{
	GObject			 parent;
	PkTaskPrivate		*priv;
	gboolean		 assigned;
	guint			 job;
	PkTaskStatus		 status;
	PkTaskExit		 exit;
	GTimer			*timer;
	gchar			*package;
	guint			*signals;
	PkSpawn			*spawn;
	gboolean		 is_killable;
} PkTask;

typedef struct
{
	GObjectClass	parent_class;
} PkTaskClass;

GType		 pk_task_get_type		  	(void);
PkTask		*pk_task_new				(void);
gchar		*pk_task_get_actions			(void);

gboolean	 pk_task_get_updates			(PkTask		*task);
gboolean	 pk_task_update_system			(PkTask		*task);
gboolean	 pk_task_search_name			(PkTask		*task,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_task_search_details			(PkTask		*task,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_task_search_group			(PkTask		*task,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_task_search_file			(PkTask		*task,
							 const gchar	*filter,
							 const gchar	*search);
gboolean	 pk_task_get_deps			(PkTask		*task,
							 const gchar	*package);
gboolean	 pk_task_remove_package			(PkTask		*task,
							 const gchar	*package,
							 gboolean	 allow_deps);
gboolean	 pk_task_refresh_cache			(PkTask		*task,
							 gboolean	 force);
gboolean	 pk_task_install_package		(PkTask		*task,
							 const gchar	*package);
gboolean	 pk_task_update_package			(PkTask		*task,
							 const gchar	*package);
gboolean	 pk_task_get_description		(PkTask		*task,
							 const gchar	*package);
gboolean	 pk_task_cancel_job_try			(PkTask		*task);

G_END_DECLS

#endif /* __PK_TASK_H */
