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

#define	PK_DBUS_SERVICE			"org.freedesktop.PackageKit"
#define	PK_DBUS_PATH			"/org/freedesktop/PackageKit"
#define	PK_DBUS_INTERFACE		"org.freedesktop.PackageKit"

G_BEGIN_DECLS

typedef enum {
	PK_TASK_COMMON_STATUS_INVALID,
	PK_TASK_COMMON_STATUS_SETUP,
	PK_TASK_COMMON_STATUS_QUERY,
	PK_TASK_COMMON_STATUS_REMOVE,
	PK_TASK_COMMON_STATUS_DOWNLOAD,
	PK_TASK_COMMON_STATUS_INSTALL,
	PK_TASK_COMMON_STATUS_UPDATE,
	PK_TASK_COMMON_STATUS_EXIT,
	PK_TASK_COMMON_STATUS_UNKNOWN
} PkTaskStatus;

typedef enum {
	PK_TASK_COMMON_EXIT_SUCCESS,
	PK_TASK_COMMON_EXIT_FAILED,
	PK_TASK_COMMON_EXIT_CANCELED,
	PK_TASK_COMMON_EXIT_UNKNOWN
} PkTaskExit;

PkTaskExit	 pk_task_common_exit_from_text		(const gchar	*exit);
PkTaskStatus	 pk_task_common_status_from_text	(const gchar	*status);

G_END_DECLS

#endif /* __PK_TASK_COMMON_H */
