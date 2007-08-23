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

#ifndef __PK_TASK_UTILS_H
#define __PK_TASK_UTILS_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	PK_TASK_STATUS_SETUP,
	PK_TASK_STATUS_QUERY,
	PK_TASK_STATUS_REMOVE,
	PK_TASK_STATUS_REFRESH_CACHE,
	PK_TASK_STATUS_DOWNLOAD,
	PK_TASK_STATUS_INSTALL,
	PK_TASK_STATUS_UPDATE,
	PK_TASK_STATUS_UNKNOWN
} PkTaskStatus;

typedef enum {
	PK_TASK_EXIT_SUCCESS,
	PK_TASK_EXIT_FAILED,
	PK_TASK_EXIT_CANCELED,
	PK_TASK_EXIT_UNKNOWN
} PkTaskExit;

typedef enum {
	PK_TASK_RESTART_APPLICATION,
	PK_TASK_RESTART_SESSION,
	PK_TASK_RESTART_SYSTEM,
	PK_TASK_RESTART_UNKNOWN
} PkTaskRestart;

typedef enum {
	PK_TASK_ERROR_CODE_NO_NETWORK,
	PK_TASK_ERROR_CODE_NOT_SUPPORTED,
	PK_TASK_ERROR_CODE_INTERNAL_ERROR,
	PK_TASK_ERROR_CODE_GPG_FAILURE,
	PK_TASK_ERROR_CODE_UNKNOWN
} PkTaskErrorCode;

PkTaskExit	 pk_task_exit_from_text			(const gchar	*exit);
const gchar	*pk_task_exit_to_text			(PkTaskExit	 exit);

PkTaskStatus	 pk_task_status_from_text		(const gchar	*status);
const gchar	*pk_task_status_to_text			(PkTaskStatus	 status);
const gchar	*pk_task_status_to_localised_text	(PkTaskStatus	 status);

PkTaskErrorCode	 pk_task_error_code_from_text		(const gchar	*code);
const gchar	*pk_task_error_code_to_text		(PkTaskErrorCode code);
const gchar	*pk_task_error_code_to_localised_text	(PkTaskErrorCode code);

PkTaskRestart	 pk_task_restart_from_text		(const gchar	*restart);
const gchar	*pk_task_restart_to_text			(PkTaskRestart	 restart);
const gchar	*pk_task_restart_to_localised_text	(PkTaskRestart	 restart);

G_END_DECLS

#endif /* __PK_TASK_UTILS_H */
