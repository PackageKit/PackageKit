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
	PK_TASK_RESTART_NONE,
	PK_TASK_RESTART_APPLICATION,
	PK_TASK_RESTART_SESSION,
	PK_TASK_RESTART_SYSTEM
} PkTaskRestart;

typedef enum {
	PK_TASK_ERROR_CODE_NO_NETWORK,
	PK_TASK_ERROR_CODE_NOT_SUPPORTED,
	PK_TASK_ERROR_CODE_INTERNAL_ERROR,
	PK_TASK_ERROR_CODE_GPG_FAILURE,
	PK_TASK_ERROR_CODE_PACKAGE_ID_INVALID,
	PK_TASK_ERROR_CODE_PACKAGE_NOT_INSTALLED,
	PK_TASK_ERROR_CODE_PACKAGE_ALREADY_INSTALLED,
	PK_TASK_ERROR_CODE_PACKAGE_DOWNLOAD_FAILED,
	PK_TASK_ERROR_CODE_DEP_RESOLUTION_FAILED,
	PK_TASK_ERROR_CODE_FILTER_INVALID,
	PK_TASK_ERROR_CODE_UNKNOWN
} PkTaskErrorCode;

typedef enum {
	PK_TASK_GROUP_ACCESSIBILITY,
	PK_TASK_GROUP_ACCESSORIES,
	PK_TASK_GROUP_EDUCATION,
	PK_TASK_GROUP_GAMES,
	PK_TASK_GROUP_GRAPHICS,
	PK_TASK_GROUP_INTERNET,
	PK_TASK_GROUP_OFFICE,
	PK_TASK_GROUP_OTHER,
	PK_TASK_GROUP_PROGRAMMING,
	PK_TASK_GROUP_MULTIMEDIA,
	PK_TASK_GROUP_SYSTEM,
	PK_TASK_GROUP_UNKNOWN
} PkTaskGroup;

typedef enum {
	PK_TASK_ACTION_INSTALL = 1,
	PK_TASK_ACTION_REMOVE,
	PK_TASK_ACTION_UPDATE,
	PK_TASK_ACTION_GET_UPDATES,
	PK_TASK_ACTION_REFRESH_CACHE,
	PK_TASK_ACTION_UPDATE_SYSTEM,
	PK_TASK_ACTION_SEARCH_NAME,
	PK_TASK_ACTION_SEARCH_DETAILS,
	PK_TASK_ACTION_SEARCH_GROUP,
	PK_TASK_ACTION_SEARCH_FILE,
	PK_TASK_ACTION_GET_DEPS,
	PK_TASK_ACTION_GET_DESCRIPTION,
	PK_TASK_ACTION_UNKNOWN
} PkTaskAction;

PkTaskExit	 pk_task_exit_from_text			(const gchar	*exit);
const gchar	*pk_task_exit_to_text			(PkTaskExit	 exit);

PkTaskStatus	 pk_task_status_from_text		(const gchar	*status);
const gchar	*pk_task_status_to_text			(PkTaskStatus	 status);

PkTaskErrorCode	 pk_task_error_code_from_text		(const gchar	*code);
const gchar	*pk_task_error_code_to_text		(PkTaskErrorCode code);

PkTaskRestart	 pk_task_restart_from_text		(const gchar	*restart);
const gchar	*pk_task_restart_to_text		(PkTaskRestart	 restart);

PkTaskGroup	 pk_task_group_from_text		(const gchar	*group);
const gchar	*pk_task_group_to_text			(PkTaskGroup	 group);

PkTaskAction	 pk_task_action_from_text		(const gchar	*action);
const gchar	*pk_task_action_to_text			(PkTaskAction	 action);

gboolean	 pk_task_filter_check			(const gchar	*filter);

/* actions */
gchar		*pk_task_action_build			(PkTaskAction	  action, ...);
gboolean	 pk_task_action_contains		(const gchar	 *actions,
							 PkTaskAction	  action);

G_END_DECLS

#endif /* __PK_TASK_UTILS_H */
