/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_LSOF_H
#define __PK_LSOF_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_LSOF		(pk_lsof_get_type ())
#define PK_LSOF(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_LSOF, PkLsof))
#define PK_LSOF_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_LSOF, PkLsofClass))
#define PK_IS_LSOF(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_LSOF))
#define PK_IS_LSOF_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_LSOF))
#define PK_LSOF_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_LSOF, PkLsofClass))

typedef struct PkLsofPrivate PkLsofPrivate;

typedef struct
{
	GObject		 parent;
	PkLsofPrivate	*priv;
} PkLsof;

typedef struct
{
	GObjectClass	parent_class;
} PkLsofClass;

GType		 pk_lsof_get_type		(void);
PkLsof		*pk_lsof_new			(void);

gboolean	 pk_lsof_refresh		(PkLsof		*lsof);
GPtrArray	*pk_lsof_get_pids_for_filenames	(PkLsof		*lsof,
						 gchar		**filenames);

G_END_DECLS

#endif /* __PK_LSOF_H */

