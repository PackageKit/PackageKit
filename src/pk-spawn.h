/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_SPAWN_H
#define __PK_SPAWN_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_SPAWN		(pk_spawn_get_type ())
#define PK_SPAWN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_SPAWN, PkSpawn))
#define PK_SPAWN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_SPAWN, PkSpawnClass))
#define PK_IS_SPAWN(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_SPAWN))
#define PK_IS_SPAWN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_SPAWN))
#define PK_SPAWN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_SPAWN, PkSpawnClass))
#define PK_SPAWN_ERROR		(pk_spawn_error_quark ())
#define PK_SPAWN_TYPE_ERROR	(pk_spawn_error_get_type ()) 

typedef struct PkSpawnPrivate PkSpawnPrivate;

typedef struct
{
	 GObject		 parent;
	 PkSpawnPrivate		*priv;
} PkSpawn;

typedef struct
{
	GObjectClass	parent_class;
} PkSpawnClass;

GType		 pk_spawn_get_type		  	(void) G_GNUC_CONST;
PkSpawn		*pk_spawn_new				(void);

gboolean	 pk_spawn_argv				(PkSpawn	*spawn,
							 gchar		**argv)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_spawn_kill				(PkSpawn	*spawn);

G_END_DECLS

#endif /* __PK_SPAWN_H */
