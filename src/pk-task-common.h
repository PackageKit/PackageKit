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

#ifndef __PK_TASK_COMMON_H
#define __PK_TASK_COMMON_H

#include <glib-object.h>
#include "pk-task.h"

G_BEGIN_DECLS

guint		 pk_task_get_job			(PkTask		*task);
gboolean	 pk_task_set_job			(PkTask		*task,
							 guint		 job);
gboolean	 pk_task_change_percentage		(PkTask		*task,
							 guint		 percentage);
gboolean	 pk_task_change_job_status		(PkTask		*task,
							 PkTaskStatus	 status);
gboolean	 pk_task_finished			(PkTask		*task,
							 PkTaskExit	 exit);
gboolean	 pk_task_package			(PkTask		*task,
							 const gchar	*package);
gboolean	 pk_task_assign				(PkTask		*task);
gboolean	 pk_task_get_job_status			(PkTask		*task,
							 PkTaskStatus	*status);
gboolean	 pk_task_clear				(PkTask		*task);
gboolean	 pk_task_setup_signals			(GObjectClass	*object_class,
							 guint		*signals);
const gchar	*pk_task_status_to_text			(PkTaskStatus	 status);
const gchar	*pk_task_exit_to_text			(PkTaskExit	 exit);
gchar		*pk_task_get_description		(PkTask		*task);
gboolean	 pk_task_filter_package_name		(PkTask		*task,
							 gchar		*package);

G_END_DECLS

#endif /* __PK_TASK_COMMON_H */
