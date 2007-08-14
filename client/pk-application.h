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

#ifndef __PK_APPLICATION_H
#define __PK_APPLICATION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_APPLICATION		(pk_application_get_type ())
#define PK_APPLICATION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_APPLICATION, PkApplication))
#define PK_APPLICATION_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_APPLICATION, PkApplicationClass))
#define PK_IS_APPLICATION(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_APPLICATION))
#define PK_IS_APPLICATION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_APPLICATION))
#define PK_APPLICATION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_APPLICATION, PkApplicationClass))

typedef struct PkApplicationPrivate PkApplicationPrivate;

typedef struct
{
	GObject		 parent;
	PkApplicationPrivate *priv;
} PkApplication;

typedef struct
{
	GObjectClass	parent_class;
	void		(* action_help)			(PkApplication	*application);
	void		(* action_close)		(PkApplication	*application);
} PkApplicationClass;

GType		 pk_application_get_type		(void);
PkApplication	*pk_application_new			(void);

G_END_DECLS

#endif	/* __PK_APPLICATION_H */
