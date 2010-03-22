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

#ifndef __PK_TASK_SYNC_H
#define __PK_TASK_SYNC_H

#include <glib.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-task.h>
#include <packagekit-glib2/pk-progress.h>

G_BEGIN_DECLS

PkResults	*pk_task_remove_packages_sync		(PkTask			*task,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_task_install_packages_sync		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_task_update_packages_sync		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_task_install_files_sync		(PkTask			*task,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

PkResults	*pk_task_update_system_sync		(PkTask			*task,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GError			**error);

G_END_DECLS

#endif /* __PK_TASK_SYNC_H */

