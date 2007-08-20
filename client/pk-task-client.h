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

#ifndef __PK_TASK_CLIENT_H
#define __PK_TASK_CLIENT_H

#include <glib-object.h>
#include "pk-task-utils.h"

G_BEGIN_DECLS

#define PK_TYPE_TASK_CLIENT		(pk_task_client_get_type ())
#define PK_TASK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK_CLIENT, PkTaskClient))
#define PK_TASK_CLIENT_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK_CLIENT, PkTaskClientClass))
#define PK_IS_TASK_CLIENT(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK_CLIENT))
#define PK_IS_TASK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK_CLIENT))
#define PK_TASK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK_CLIENT, PkTaskClientClass))

typedef struct PkTaskClientPrivate PkTaskClientPrivate;

typedef struct
{
	GObject			 parent;
	PkTaskClientPrivate	*priv;
} PkTaskClient;

typedef struct
{
	GObjectClass	parent_class;
} PkTaskClientClass;

GType		 pk_task_client_get_type		(void);
PkTaskClient	*pk_task_client_new			(void);

gboolean	 pk_task_client_set_sync		(PkTaskClient	*tclient,
							 gboolean	 is_sync);
gboolean	 pk_task_client_get_updates		(PkTaskClient	*tclient);
gboolean	 pk_task_client_update_system		(PkTaskClient	*tclient);
gboolean	 pk_task_client_find_packages		(PkTaskClient	*tclient,
							 const gchar	*search,
							 guint		 depth,
							 gboolean	 installed,
							 gboolean	 available);
gboolean	 pk_task_client_get_deps		(PkTaskClient	*tclient,
							 const gchar	*package);
gboolean	 pk_task_client_remove_package		(PkTaskClient	*tclient,
							 const gchar	*package);
gboolean	 pk_task_client_refresh_cache		(PkTaskClient	*tclient);
gboolean	 pk_task_client_remove_package_with_deps(PkTaskClient	*tclient,
							 const gchar	*package);
gboolean	 pk_task_client_install_package		(PkTaskClient	*tclient,
							 const gchar	*package);
gboolean	 pk_task_client_cancel_job_try		(PkTaskClient	*tclient);
gboolean	 pk_task_client_reset			(PkTaskClient	*tclient);

G_END_DECLS

#endif /* __PK_TASK_CLIENT_H */
