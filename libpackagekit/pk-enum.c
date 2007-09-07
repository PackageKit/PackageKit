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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-enum.h"

typedef struct {
	guint		 value;
	const gchar	*string;
} PkTaskEnumMatch;

static PkTaskEnumMatch task_exit[] = {
	{PK_EXIT_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_EXIT_ENUM_SUCCESS,			"success"},
	{PK_EXIT_ENUM_FAILED,			"failed"},
	{PK_EXIT_ENUM_CANCELED,			"canceled"},
	{0, NULL},
};

static PkTaskEnumMatch task_status[] = {
	{PK_STATUS_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_STATUS_ENUM_SETUP,			"setup"},
	{PK_STATUS_ENUM_QUERY,			"query"},
	{PK_STATUS_ENUM_REFRESH_CACHE,		"refresh-cache"},
	{PK_STATUS_ENUM_REMOVE,			"remove"},
	{PK_STATUS_ENUM_DOWNLOAD,		"download"},
	{PK_STATUS_ENUM_INSTALL,		"install"},
	{PK_STATUS_ENUM_UPDATE,			"update"},
	{0, NULL},
};

static PkTaskEnumMatch task_role[] = {
	{PK_ROLE_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_ROLE_ENUM_QUERY,			"query"},
	{PK_ROLE_ENUM_REFRESH_CACHE,		"refresh-cache"},
	{PK_ROLE_ENUM_PACKAGE_REMOVE,		"package-remove"},
	{PK_ROLE_ENUM_PACKAGE_INSTALL,		"package-install"},
	{PK_ROLE_ENUM_PACKAGE_UPDATE,		"package-update"},
	{PK_ROLE_ENUM_SYSTEM_UPDATE,		"system-update"},
	{0, NULL},
};

static PkTaskEnumMatch task_error[] = {
	{PK_ERROR_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_ERROR_ENUM_OOM,			"out-of-memory"},
	{PK_ERROR_ENUM_NO_NETWORK,		"no-network"},
	{PK_ERROR_ENUM_NOT_SUPPORTED,		"not-supported"},
	{PK_ERROR_ENUM_INTERNAL_ERROR,		"internal-error"},
	{PK_ERROR_ENUM_GPG_FAILURE,		"gpg-failure"},
	{PK_ERROR_ENUM_FILTER_INVALID,		"filter-invalid"},
	{PK_ERROR_ENUM_PACKAGE_ID_INVALID,	"package-id-invalid"},
	{PK_ERROR_ENUM_TRANSACTION_ERROR,	"transaction-error"},
	{PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,	"package-not-installed"},
	{PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,	"package-already-installed"},
	{PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,	"package-download-failed"},
	{PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,	"dep-resolution-failed"},
	{PK_ERROR_ENUM_CREATE_THREAD_FAILED,	"create-thread-failed"},
	{0, NULL},
};

static PkTaskEnumMatch task_restart[] = {
	{PK_RESTART_ENUM_NONE,			"none"},
	{PK_RESTART_ENUM_SYSTEM,		"system"},
	{PK_RESTART_ENUM_SESSION,		"session"},
	{PK_RESTART_ENUM_APPLICATION,		"application"},
	{0, NULL},
};

static PkTaskEnumMatch task_group[] = {
	{PK_GROUP_ENUM_ACCESSIBILITY,		"accessibility"},
	{PK_GROUP_ENUM_ACCESSORIES,		"accessories"},
	{PK_GROUP_ENUM_EDUCATION,		"education"},
	{PK_GROUP_ENUM_GAMES,			"games"},
	{PK_GROUP_ENUM_GRAPHICS,		"graphics"},
	{PK_GROUP_ENUM_INTERNET,		"internet"},
	{PK_GROUP_ENUM_OFFICE,			"office"},
	{PK_GROUP_ENUM_OTHER,			"other"},
	{PK_GROUP_ENUM_PROGRAMMING,		"programming"},
	{PK_GROUP_ENUM_MULTIMEDIA,		"multimedia"},
	{PK_GROUP_ENUM_SYSTEM,			"system"},
	{0, NULL},
};

static PkTaskEnumMatch task_action[] = {
	{PK_ACTION_ENUM_INSTALL,		"install"},
	{PK_ACTION_ENUM_REMOVE,			"remove"},
	{PK_ACTION_ENUM_UPDATE,			"update"},
	{PK_ACTION_ENUM_GET_UPDATES,		"get-updates"},
	{PK_ACTION_ENUM_REFRESH_CACHE,		"refresh-cache"},
	{PK_ACTION_ENUM_UPDATE_SYSTEM,		"update-system"},
	{PK_ACTION_ENUM_SEARCH_NAME,		"search-name"},
	{PK_ACTION_ENUM_SEARCH_DETAILS,		"search-details"},
	{PK_ACTION_ENUM_SEARCH_GROUP,		"search-group"},
	{PK_ACTION_ENUM_SEARCH_FILE,		"search-file"},
	{PK_ACTION_ENUM_GET_DEPENDS,		"get-depends"},
	{PK_ACTION_ENUM_GET_REQUIRES,		"get-requires"},
	{PK_ACTION_ENUM_GET_DESCRIPTION,	"get-description"},
	{0, NULL},
};

/**
 * pk_task_enum_find_value:
 */
static guint
pk_task_enum_find_value (PkTaskEnumMatch *table, const gchar *string)
{
	guint i;
	const gchar *string_tmp;

	/* return the first entry on non-found or error */
	if (string == NULL) {
		return table[0].value;
	}
	for (i=0;;i++) {
		string_tmp = table[i].string;
		if (string_tmp == NULL) {
			break;
		}
		if (strcmp (string, string_tmp) == 0) {
			return table[i].value;
		}
	}
	return table[0].value;
}

/**
 * pk_task_enum_find_string:
 */
static const gchar *
pk_task_enum_find_string (PkTaskEnumMatch *table, guint value)
{
	guint i;
	guint tmp;
	const gchar *string_tmp;

	for (i=0;;i++) {
		string_tmp = table[i].string;
		if (string_tmp == NULL) {
			break;
		}
		tmp = table[i].value;
		if (tmp == value) {
			return table[i].string;
		}
	}
	return table[0].string;
}

/**
 * pk_exit_enum_from_text:
 */
PkTaskExit
pk_exit_enum_from_text (const gchar *exit)
{
	return pk_task_enum_find_value (task_exit, exit);
}

/**
 * pk_exit_enum_to_text:
 **/
const gchar *
pk_exit_enum_to_text (PkTaskExit exit)
{
	return pk_task_enum_find_string (task_exit, exit);
}

/**
 * pk_status_enum_from_text:
 **/
PkTaskStatus
pk_status_enum_from_text (const gchar *status)
{
	return pk_task_enum_find_value (task_status, status);
}

/**
 * pk_status_enum_to_text:
 **/
const gchar *
pk_status_enum_to_text (PkTaskStatus status)
{
	return pk_task_enum_find_string (task_status, status);
}

/**
 * pk_role_enum_from_text:
 **/
PkTaskRole
pk_role_enum_from_text (const gchar *role)
{
	return pk_task_enum_find_value (task_role, role);
}

/**
 * pk_role_enum_to_text:
 **/
const gchar *
pk_role_enum_to_text (PkTaskRole role)
{
	return pk_task_enum_find_string (task_role, role);
}

/**
 * pk_error_enum_from_text:
 **/
PkTaskErrorCode
pk_error_enum_from_text (const gchar *code)
{
	return pk_task_enum_find_value (task_error, code);
}

/**
 * pk_error_enum_to_text:
 **/
const gchar *
pk_error_enum_to_text (PkTaskErrorCode code)
{
	return pk_task_enum_find_string (task_error, code);
}

/**
 * pk_restart_enum_from_text:
 **/
PkTaskRestart
pk_restart_enum_from_text (const gchar *restart)
{
	return pk_task_enum_find_value (task_restart, restart);
}

/**
 * pk_restart_enum_to_text:
 **/
const gchar *
pk_restart_enum_to_text (PkTaskRestart restart)
{
	return pk_task_enum_find_string (task_restart, restart);
}

/**
 * pk_group_enum_from_text:
 **/
PkTaskGroup
pk_group_enum_from_text (const gchar *group)
{
	return pk_task_enum_find_value (task_group, group);
}

/**
 * pk_group_enum_to_text:
 **/
const gchar *
pk_group_enum_to_text (PkTaskGroup group)
{
	return pk_task_enum_find_string (task_group, group);
}

/**
 * pk_action_enum_from_text:
 **/
PkTaskAction
pk_action_enum_from_text (const gchar *action)
{
	return pk_task_enum_find_value (task_action, action);
}

/**
 * pk_action_enum_to_text:
 **/
const gchar *
pk_action_enum_to_text (PkTaskAction action)
{
	return pk_task_enum_find_string (task_action, action);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_task_utils (LibSelfTest *test)
{
	if (libst_start (test, "PkTaskUtils", CLASS_AUTO) == FALSE) {
		return;
	}

	libst_end (test);
}
#endif

