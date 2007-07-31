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

G_BEGIN_DECLS

#define PK_TYPE_TASK		(pk_task_get_type ())
#define PK_TASK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK, PkTask))
#define PK_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK, PkTaskClass))
#define PK_IS_TASK(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK))
#define PK_IS_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK))
#define PK_TASK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK, PkTaskClass))
#define PK_TASK_ERROR		(pk_task_error_quark ())
#define PK_TASK_TYPE_ERROR	(pk_task_error_get_type ()) 

typedef struct PkTaskPrivate PkTaskPrivate;

typedef enum {
	PK_TASK_STATUS_INVALID,
	PK_TASK_STATUS_SETUP,
	PK_TASK_STATUS_DOWNLOAD,
	PK_TASK_STATUS_INSTALL,
	PK_TASK_STATUS_UPDATE,
	PK_TASK_STATUS_EXIT,
	PK_TASK_STATUS_UNKNOWN
} PkTaskStatus;

typedef enum {
	PK_TASK_EXIT_SUCCESS,
	PK_TASK_EXIT_FAILED,
	PK_TASK_EXIT_CANCELED,
	PK_TASK_EXIT_UNKNOWN
} PkTaskExit;

typedef struct
{
	 GObject		 parent;
	 PkTaskPrivate		*priv;
} PkTask;

typedef struct
{
	GObjectClass	parent_class;
	void		(* job_status_changed)		(PkTask		*task,
							 PkTaskStatus	 status);
	void		(* percentage_complete_changed)	(PkTask		*task,
							 guint		 percentage);
	void		(* packages)			(PkTask		*task,
							 GPtrArray	*packages);
	void		(* finished)			(PkTask		*task,
							 PkTaskExit	 completion);
} PkTaskClass;

typedef enum
{
	PK_TASK_ERROR_DENIED,
	PK_TASK_ERROR_LAST
} PkTaskError;


GQuark		 pk_task_error_quark			(void);
GType		 pk_task_error_get_type			(void);
GType		 pk_task_get_type		  	(void);
PkTask		*pk_task_new				(void);

gboolean	 pk_task_get_updates			(PkTask		*task);
gboolean	 pk_task_update_system			(PkTask		*task);
gboolean	 pk_task_find_packages			(PkTask		*task,
							 const gchar	*search);
gboolean	 pk_task_get_dependencies		(PkTask		*task,
							 const gchar	*package);
gboolean	 pk_task_remove_packages		(PkTask		*task,
							 const gchar	**packages);
gboolean	 pk_task_remove_packages_with_dependencies(PkTask	*task,
							 const gchar	**packages);
gboolean	 pk_task_install_packages		(PkTask		*task,
							 const gchar	**packages);
gboolean	 pk_task_get_job_status			(PkTask		*task,
							 PkTaskStatus	*status);
gboolean	 pk_task_cancel_job_try			(PkTask		*task);

guint		 pk_task_get_job			(PkTask		*task);


G_END_DECLS

#endif /* __PK_TASK_H */
