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

#ifndef __PK_ENUM_H
#define __PK_ENUM_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

/* what we asked to do */
typedef enum {
	PK_ROLE_ENUM_QUERY,
	PK_ROLE_ENUM_REFRESH_CACHE,
	PK_ROLE_ENUM_SYSTEM_UPDATE,
	PK_ROLE_ENUM_PACKAGE_REMOVE,
	PK_ROLE_ENUM_PACKAGE_INSTALL,
	PK_ROLE_ENUM_PACKAGE_UPDATE,
	PK_ROLE_ENUM_UNKNOWN
} PkTaskRole;

/* what we are actually doing */
typedef enum {
	PK_STATUS_ENUM_SETUP,
	PK_STATUS_ENUM_QUERY,
	PK_STATUS_ENUM_REMOVE,
	PK_STATUS_ENUM_REFRESH_CACHE,
	PK_STATUS_ENUM_DOWNLOAD,
	PK_STATUS_ENUM_INSTALL,
	PK_STATUS_ENUM_UPDATE,
	PK_STATUS_ENUM_UNKNOWN
} PkTaskStatus;

typedef enum {
	PK_EXIT_ENUM_SUCCESS,
	PK_EXIT_ENUM_FAILED,
	PK_EXIT_ENUM_CANCELED,
	PK_EXIT_ENUM_UNKNOWN
} PkTaskExit;

typedef enum {
	PK_RESTART_ENUM_NONE,
	PK_RESTART_ENUM_APPLICATION,
	PK_RESTART_ENUM_SESSION,
	PK_RESTART_ENUM_SYSTEM
} PkTaskRestart;

typedef enum {
	PK_ERROR_ENUM_OOM,
	PK_ERROR_ENUM_NO_NETWORK,
	PK_ERROR_ENUM_NOT_SUPPORTED,
	PK_ERROR_ENUM_INTERNAL_ERROR,
	PK_ERROR_ENUM_GPG_FAILURE,
	PK_ERROR_ENUM_PACKAGE_ID_INVALID,
	PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
	PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
	PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
	PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
	PK_ERROR_ENUM_FILTER_INVALID,
	PK_ERROR_ENUM_CREATE_THREAD_FAILED,
	PK_ERROR_ENUM_TRANSACTION_ERROR,
	PK_ERROR_ENUM_UNKNOWN
} PkTaskErrorCode;

typedef enum {
	PK_GROUP_ENUM_ACCESSIBILITY,
	PK_GROUP_ENUM_ACCESSORIES,
	PK_GROUP_ENUM_EDUCATION,
	PK_GROUP_ENUM_GAMES,
	PK_GROUP_ENUM_GRAPHICS,
	PK_GROUP_ENUM_INTERNET,
	PK_GROUP_ENUM_OFFICE,
	PK_GROUP_ENUM_OTHER,
	PK_GROUP_ENUM_PROGRAMMING,
	PK_GROUP_ENUM_MULTIMEDIA,
	PK_GROUP_ENUM_SYSTEM,
	PK_GROUP_ENUM_UNKNOWN
} PkTaskGroup;

typedef enum {
	PK_ACTION_ENUM_INSTALL = 1,
	PK_ACTION_ENUM_REMOVE,
	PK_ACTION_ENUM_UPDATE,
	PK_ACTION_ENUM_GET_UPDATES,
	PK_ACTION_ENUM_CANCEL_JOB,
	PK_ACTION_ENUM_REFRESH_CACHE,
	PK_ACTION_ENUM_UPDATE_SYSTEM,
	PK_ACTION_ENUM_SEARCH_NAME,
	PK_ACTION_ENUM_SEARCH_DETAILS,
	PK_ACTION_ENUM_SEARCH_GROUP,
	PK_ACTION_ENUM_SEARCH_FILE,
	PK_ACTION_ENUM_GET_DEPENDS,
	PK_ACTION_ENUM_GET_REQUIRES,
	PK_ACTION_ENUM_GET_DESCRIPTION,
	PK_ACTION_ENUM_INSTALL_PACKAGE,
	PK_ACTION_ENUM_REMOVE_PACKAGE,
	PK_ACTION_ENUM_UPDATE_PACKAGE,
	PK_ACTION_ENUM_UNKNOWN
} PkTaskAction;

PkTaskExit	 pk_exit_enum_from_text			(const gchar	*exit);
const gchar	*pk_exit_enum_to_text			(PkTaskExit	 exit);

PkTaskStatus	 pk_status_enum_from_text		(const gchar	*status);
const gchar	*pk_status_enum_to_text			(PkTaskStatus	 status);

PkTaskRole	 pk_role_enum_from_text			(const gchar	*role);
const gchar	*pk_role_enum_to_text			(PkTaskRole	 role);

PkTaskErrorCode	 pk_error_enum_from_text		(const gchar	*code);
const gchar	*pk_error_enum_to_text			(PkTaskErrorCode code);

PkTaskRestart	 pk_restart_enum_from_text		(const gchar	*restart);
const gchar	*pk_restart_enum_to_text		(PkTaskRestart	 restart);

PkTaskGroup	 pk_group_enum_from_text		(const gchar	*group);
const gchar	*pk_group_enum_to_text			(PkTaskGroup	 group);

PkTaskAction	 pk_action_enum_from_text		(const gchar	*action);
const gchar	*pk_action_enum_to_text			(PkTaskAction	 action);

G_END_DECLS

#endif /* __PK_ENUM_H */
