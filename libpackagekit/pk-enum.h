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
	PK_ROLE_ENUM_CANCEL,
	PK_ROLE_ENUM_RESOLVE,
	PK_ROLE_ENUM_ROLLBACK,
	PK_ROLE_ENUM_GET_DEPENDS,
	PK_ROLE_ENUM_GET_UPDATE_DETAIL,
	PK_ROLE_ENUM_GET_DESCRIPTION,
	PK_ROLE_ENUM_GET_REQUIRES,
	PK_ROLE_ENUM_GET_UPDATES,
	PK_ROLE_ENUM_SEARCH_DETAILS,
	PK_ROLE_ENUM_SEARCH_FILE,
	PK_ROLE_ENUM_SEARCH_GROUP,
	PK_ROLE_ENUM_SEARCH_NAME,
	PK_ROLE_ENUM_REFRESH_CACHE,
	PK_ROLE_ENUM_UPDATE_SYSTEM,
	PK_ROLE_ENUM_REMOVE_PACKAGE,
	PK_ROLE_ENUM_INSTALL_PACKAGE,
	PK_ROLE_ENUM_INSTALL_FILE,
	PK_ROLE_ENUM_UPDATE_PACKAGE,
	PK_ROLE_ENUM_GET_REPO_LIST,
	PK_ROLE_ENUM_REPO_ENABLE,
	PK_ROLE_ENUM_REPO_SET_DATA,
	PK_ROLE_ENUM_UNKNOWN
} PkRoleEnum;

/* what we are actually doing */
typedef enum {
	PK_STATUS_ENUM_SETUP,
	PK_STATUS_ENUM_WAIT,
	PK_STATUS_ENUM_QUERY,
	PK_STATUS_ENUM_REMOVE,
	PK_STATUS_ENUM_REFRESH_CACHE,
	PK_STATUS_ENUM_DOWNLOAD,
	PK_STATUS_ENUM_INSTALL,
	PK_STATUS_ENUM_UPDATE,
	PK_STATUS_ENUM_UNKNOWN
} PkStatusEnum;

typedef enum {
	PK_EXIT_ENUM_SUCCESS,
	PK_EXIT_ENUM_FAILED,
	PK_EXIT_ENUM_CANCELED,
	PK_EXIT_ENUM_UNKNOWN
} PkExitEnum;

typedef enum {
	PK_FILTER_ENUM_DEVELOPMENT,
	PK_FILTER_ENUM_INSTALLED,
	PK_FILTER_ENUM_GUI,
	PK_FILTER_ENUM_NORMAL,		/* not DEVELOPMENT */
	PK_FILTER_ENUM_AVAILABLE,	/* not INSTALLED */
	PK_FILTER_ENUM_TEXT,		/* not GUI */
	PK_FILTER_ENUM_UNKNOWN
} PkFilterEnum;

typedef enum {
	PK_RESTART_ENUM_NONE,
	PK_RESTART_ENUM_APPLICATION,
	PK_RESTART_ENUM_SESSION,
	PK_RESTART_ENUM_SYSTEM,
	PK_RESTART_ENUM_UNKNOWN
} PkRestartEnum;

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
	PK_ERROR_ENUM_NO_CACHE,
	PK_ERROR_ENUM_REPO_NOT_FOUND,
	PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
	PK_ERROR_ENUM_UNKNOWN
} PkErrorCodeEnum;

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
} PkGroupEnum;

typedef enum {
	PK_FREQ_ENUM_HOURLY,
	PK_FREQ_ENUM_DAILY,
	PK_FREQ_ENUM_WEEKLY,
	PK_FREQ_ENUM_NEVER,
	PK_FREQ_ENUM_UNKNOWN
} PkFreqEnum;

typedef enum {
	PK_UPDATE_ENUM_ALL,
	PK_UPDATE_ENUM_SECURITY,
	PK_UPDATE_ENUM_NONE,
	PK_UPDATE_ENUM_UNKNOWN
} PkUpdateEnum;

typedef enum {
	PK_INFO_ENUM_INSTALLED,
	PK_INFO_ENUM_AVAILABLE,
	PK_INFO_ENUM_LOW,
	PK_INFO_ENUM_NORMAL,
	PK_INFO_ENUM_IMPORTANT,
	PK_INFO_ENUM_SECURITY,
	PK_INFO_ENUM_DOWNLOADING,
	PK_INFO_ENUM_UPDATING,
	PK_INFO_ENUM_INSTALLING,
	PK_INFO_ENUM_REMOVING,
	PK_INFO_ENUM_UNKNOWN
} PkInfoEnum;

typedef enum {
	PK_SIGTYPE_ENUM_GPG,
	PK_SIGTYPE_ENUM_UNKNOWN
} PkSigTypeEnum;

PkSigTypeEnum    pk_sig_type_enum_from_text             (const gchar    *sig_type);
const gchar     *pk_sig_type_enum_to_text               (PkSigTypeEnum   sig_type);

PkInfoEnum	 pk_info_enum_from_text			(const gchar	*info);
const gchar	*pk_info_enum_to_text			(PkInfoEnum	 info);

PkUpdateEnum	 pk_update_enum_from_text		(const gchar	*update);
const gchar	*pk_update_enum_to_text			(PkUpdateEnum	 update);

PkFreqEnum	 pk_freq_enum_from_text			(const gchar	*freq);
const gchar	*pk_freq_enum_to_text			(PkFreqEnum	 freq);

PkExitEnum	 pk_exit_enum_from_text			(const gchar	*exit);
const gchar	*pk_exit_enum_to_text			(PkExitEnum	 exit);

PkStatusEnum	 pk_status_enum_from_text		(const gchar	*status);
const gchar	*pk_status_enum_to_text			(PkStatusEnum	 status);

PkRoleEnum	 pk_role_enum_from_text			(const gchar	*role);
const gchar	*pk_role_enum_to_text			(PkRoleEnum	 role);

PkErrorCodeEnum	 pk_error_enum_from_text		(const gchar	*code);
const gchar	*pk_error_enum_to_text			(PkErrorCodeEnum code);

PkRestartEnum	 pk_restart_enum_from_text		(const gchar	*restart);
const gchar	*pk_restart_enum_to_text		(PkRestartEnum	 restart);

PkGroupEnum	 pk_group_enum_from_text		(const gchar	*group);
const gchar	*pk_group_enum_to_text			(PkGroupEnum	 group);

PkFilterEnum	 pk_filter_enum_from_text		(const gchar	*filter);
const gchar	*pk_filter_enum_to_text			(PkFilterEnum	 filter);

G_END_DECLS

#endif /* __PK_ENUM_H */
