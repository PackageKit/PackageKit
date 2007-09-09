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

#ifndef __PK_JOB_LIST_H
#define __PK_JOB_LIST_H

#include <glib-object.h>
#include "pk-backend-internal.h"

G_BEGIN_DECLS

#define PK_TYPE_JOB_LIST		(pk_job_list_get_type ())
#define PK_JOB_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_JOB_LIST, PkJobList))
#define PK_JOB_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_JOB_LIST, PkJobListClass))
#define PK_IS_JOB_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_JOB_LIST))
#define PK_IS_JOB_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_JOB_LIST))
#define PK_JOB_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_JOB_LIST, PkJobListClass))
#define PK_JOB_LIST_ERROR		(pk_job_list_error_quark ())
#define PK_JOB_LIST_TYPE_ERROR		(pk_job_list_error_get_type ()) 

typedef struct PkJobListPrivate PkJobListPrivate;

typedef struct
{
	 GObject		 parent;
	 PkJobListPrivate	*priv;
} PkJobList;

typedef struct
{
	GObjectClass	parent_class;
} PkJobListClass;

typedef struct {
	guint		 job;
	gboolean	 valid;
	PkTask		*task;
} PkJobListItem;

GType		 pk_job_list_get_type		  	(void);
PkJobList	*pk_job_list_new			(void);

PkJobListItem	*pk_job_list_add			(PkJobList	*job_list,
							 PkTask		*task);
gboolean	 pk_job_list_remove			(PkJobList	*job_list,
							 PkTask		*task);
gboolean	 pk_job_list_commit			(PkJobList	*job_list,
							 PkTask		*task);
GArray		*pk_job_list_get_array			(PkJobList	*job_list);
guint		 pk_job_list_get_size			(PkJobList	*job_list);
PkJobListItem	*pk_job_list_get_item_from_job		(PkJobList	*job_list,
							 guint		 job);
PkJobListItem	*pk_job_list_get_item_from_task		(PkJobList	*job_list,
							 PkTask		*task);

G_END_DECLS

#endif /* __PK_JOB_LIST_H */
