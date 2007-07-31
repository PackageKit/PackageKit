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

#ifndef __PK_JOB_H
#define __PK_JOB_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_JOB		(pk_job_get_type ())
#define PK_JOB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_JOB, PkJob))
#define PK_JOB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_JOB, PkJobClass))
#define PK_IS_JOB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_JOB))
#define PK_IS_JOB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_JOB))
#define PK_JOB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_JOB, PkJobClass))

typedef struct PkJobPrivate PkJobPrivate;

typedef struct
{
	GObject		 parent;
	PkJobPrivate	*priv;
} PkJob;

typedef struct
{
	GObjectClass	parent_class;
} PkJobClass;

GType			 pk_job_get_type			(void);
PkJob			*pk_job_new				(void);
guint			 pk_job_get_unique			(PkJob	*job);

G_END_DECLS

#endif /* __PK_JOB_H */
