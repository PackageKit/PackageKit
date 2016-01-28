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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkSpawn, g_object_unref)
#endif

/**
 * PkSpawnExitType:
 *
 * How the spawned file exited
 **/
typedef enum {
	PK_SPAWN_EXIT_TYPE_SUCCESS,		/* script run, without any problems */
	PK_SPAWN_EXIT_TYPE_FAILED,		/* script failed to run */
	PK_SPAWN_EXIT_TYPE_DISPATCHER_CHANGED,	/* changed dispatcher, another started */
	PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT,	/* we timed out, and exited the dispatcher instance */
	PK_SPAWN_EXIT_TYPE_SIGQUIT,		/* we killed the instance (SIGQUIT) */
	PK_SPAWN_EXIT_TYPE_SIGKILL,		/* we killed the instance (SIGKILL) */
	PK_SPAWN_EXIT_TYPE_UNKNOWN
} PkSpawnExitType;

typedef enum {
	PK_SPAWN_ARGV_FLAGS_NONE,
	PK_SPAWN_ARGV_FLAGS_NEVER_REUSE,
	PK_SPAWN_ARGV_FLAGS_LAST
} PkSpawnArgvFlags;

GType		 pk_spawn_get_type			(void);
PkSpawn		*pk_spawn_new				(GKeyFile		*conf);

gboolean	 pk_spawn_argv				(PkSpawn	*spawn,
							 gchar		**argv,
							 gchar		**envp,
							 PkSpawnArgvFlags flags,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_spawn_is_running			(PkSpawn	*spawn);
gboolean	 pk_spawn_kill				(PkSpawn	*spawn);
gboolean	 pk_spawn_exit				(PkSpawn	*spawn);

G_END_DECLS

#endif /* __PK_SPAWN_H */
