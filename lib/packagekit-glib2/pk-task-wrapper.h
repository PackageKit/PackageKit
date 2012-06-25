/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __PK_TASK_WRAPPER_H
#define __PK_TASK_WRAPPER_H

#include <glib-object.h>
#include <packagekit-glib2/pk-task.h>

G_BEGIN_DECLS

#define PK_TYPE_TASK_WRAPPER		(pk_task_wrapper_get_type ())
#define PK_TASK_WRAPPER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK_WRAPPER, PkTaskWrapper))
#define PK_TASK_WRAPPER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK_WRAPPER, PkTaskWrapperClass))
#define PK_IS_TASK_WRAPPER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK_WRAPPER))
#define PK_IS_TASK_WRAPPER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK_WRAPPER))
#define PK_TASK_WRAPPER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK_WRAPPER, PkTaskWrapperClass))

typedef struct _PkTaskWrapperPrivate	PkTaskWrapperPrivate;
typedef struct _PkTaskWrapper		PkTaskWrapper;
typedef struct _PkTaskWrapperClass	PkTaskWrapperClass;

struct _PkTaskWrapper
{
	 PkTask				 parent;
	 PkTaskWrapperPrivate		*priv;
};

struct _PkTaskWrapperClass
{
	PkTaskClass			 parent_class;
};

GQuark		 pk_task_wrapper_error_quark			(void);
GType		 pk_task_wrapper_get_type			(void);
PkTaskWrapper	*pk_task_wrapper_new				(void);

G_END_DECLS

#endif /* __PK_TASK_WRAPPER_H */

