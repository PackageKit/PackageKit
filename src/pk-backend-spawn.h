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

#ifndef __PK_BACKEND_SPAWN_H
#define __PK_BACKEND_SPAWN_H

#include <glib-object.h>
#include "pk-backend-job.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_SPAWN		(pk_backend_spawn_get_type ())
G_DECLARE_FINAL_TYPE (PkBackendSpawn, pk_backend_spawn, PK, BACKEND_SPAWN, GObject)

#define PK_BACKEND_SPAWN_FILENAME_DELIM	"|"

/* general */
PkBackendSpawn	*pk_backend_spawn_new			(GKeyFile		*conf);
gboolean	 pk_backend_spawn_helper		(PkBackendSpawn	*backend_spawn,
							 PkBackendJob	*job,
							 const gchar	*first_element, ...)
							 G_GNUC_NULL_TERMINATED;
gboolean	 pk_backend_spawn_is_busy		(PkBackendSpawn	*backend_spawn);
gboolean	 pk_backend_spawn_kill			(PkBackendSpawn	*backend_spawn);
gboolean	 pk_backend_spawn_exit			(PkBackendSpawn	*backend_spawn);
const gchar	*pk_backend_spawn_get_name		(PkBackendSpawn	*backend_spawn);
gboolean	 pk_backend_spawn_set_name		(PkBackendSpawn	*backend_spawn,
							 const gchar	*name);
void		 pk_backend_spawn_set_allow_sigkill	(PkBackendSpawn	*backend_spawn,
							 gboolean	 allow_sigkill);
gboolean	 pk_backend_spawn_inject_data		(PkBackendSpawn *backend_spawn,
							 PkBackendJob	*job,
							 const gchar	*line,
							 GError		**error);

/* filtering */
typedef gboolean (*PkBackendSpawnFilterFunc)		(PkBackendJob	*job,
							 const gchar	*data);
gboolean	 pk_backend_spawn_set_filter_stderr	(PkBackendSpawn	*backend_spawn,
							 PkBackendSpawnFilterFunc func);
gboolean	 pk_backend_spawn_set_filter_stdout	(PkBackendSpawn	*backend_spawn,
							 PkBackendSpawnFilterFunc func);


G_END_DECLS

#endif /* __PK_BACKEND_SPAWN_H */
