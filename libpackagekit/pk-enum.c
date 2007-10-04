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
	{PK_STATUS_ENUM_WAIT,			"wait"},
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
	{PK_ROLE_ENUM_CANCEL,			"cancel"},
	{PK_ROLE_ENUM_RESOLVE,			"resolve"},
	{PK_ROLE_ENUM_ROLLBACK,			"rollback"},
	{PK_ROLE_ENUM_GET_DEPENDS,		"get-depends"},
	{PK_ROLE_ENUM_GET_UPDATE_DETAIL,	"get-update-detail"},
	{PK_ROLE_ENUM_GET_DESCRIPTION,		"get-description"},
	{PK_ROLE_ENUM_GET_REQUIRES,		"get-requires"},
	{PK_ROLE_ENUM_GET_UPDATES,		"get-updates"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,		"search-details"},
	{PK_ROLE_ENUM_SEARCH_FILE,		"search-file"},
	{PK_ROLE_ENUM_SEARCH_GROUP,		"search-group"},
	{PK_ROLE_ENUM_SEARCH_NAME,		"search-name"},
	{PK_ROLE_ENUM_REFRESH_CACHE,		"refresh-cache"},
	{PK_ROLE_ENUM_REMOVE_PACKAGE,		"remove-package"},
	{PK_ROLE_ENUM_INSTALL_PACKAGE,		"install-package"},
	{PK_ROLE_ENUM_INSTALL_FILE,		"install-file"},
	{PK_ROLE_ENUM_UPDATE_PACKAGE,		"update-package"},
	{PK_ROLE_ENUM_UPDATE_SYSTEM,		"update-system"},
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

static PkTaskEnumMatch task_filter[] = {
	{PK_FILTER_ENUM_DEVELOPMENT,		"devel"},
	{PK_FILTER_ENUM_INSTALLED,		"installed"},
	{PK_FILTER_ENUM_GUI,			"gui"},
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

static PkTaskEnumMatch task_freq[] = {
	{PK_FREQ_ENUM_HOURLY,			"hourly"},
	{PK_FREQ_ENUM_DAILY,			"daily"},
	{PK_FREQ_ENUM_WEEKLY,			"weekly"},
	{PK_FREQ_ENUM_NEVER,			"never"},
	{0, NULL},
};

static PkTaskEnumMatch task_update[] = {
	{PK_UPDATE_ENUM_ALL,			"all"},
	{PK_UPDATE_ENUM_SECURITY,		"security"},
	{PK_UPDATE_ENUM_NONE,			"none"},
	{0, NULL},
};

static PkTaskEnumMatch task_info[] = {
	{PK_INFO_ENUM_INSTALLED,		"installed"},
	{PK_INFO_ENUM_AVAILABLE,		"available"},
	{PK_INFO_ENUM_LOW,			"low"},
	{PK_INFO_ENUM_NORMAL,			"normal"},
	{PK_INFO_ENUM_IMPORTANT,		"important"},
	{PK_INFO_ENUM_SECURITY,			"security"},
	{PK_INFO_ENUM_DOWNLOADING,		"downloading"},
	{PK_INFO_ENUM_UPDATING,			"updating"},
	{PK_INFO_ENUM_INSTALLING,		"installing"},
	{PK_INFO_ENUM_REMOVING,			"removing"},
	{0, NULL},
};

static PkTaskEnumMatch task_sig_type[] = {
	{PK_SIGTYPE_ENUM_GPG,                   "gpg"},
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
 * pk_sig_type_enum_from_text:
 */
PkSigTypeEnum
pk_sig_type_enum_from_text (const gchar *sig_type)
{
	return pk_task_enum_find_value (task_sig_type, sig_type);
}

/**
 * pk_sig_type_enum_to_text:
 **/
const gchar *
pk_sig_type_enum_to_text (PkSigTypeEnum sig_type)
{
	return pk_task_enum_find_string (task_sig_type, sig_type);
}

/**
 * pk_info_enum_from_text:
 */
PkInfoEnum
pk_info_enum_from_text (const gchar *info)
{
	return pk_task_enum_find_value (task_info, info);
}

/**
 * pk_info_enum_to_text:
 **/
const gchar *
pk_info_enum_to_text (PkInfoEnum info)
{
	return pk_task_enum_find_string (task_info, info);
}

/**
 * pk_exit_enum_from_text:
 */
PkExitEnum
pk_exit_enum_from_text (const gchar *exit)
{
	return pk_task_enum_find_value (task_exit, exit);
}

/**
 * pk_exit_enum_to_text:
 **/
const gchar *
pk_exit_enum_to_text (PkExitEnum exit)
{
	return pk_task_enum_find_string (task_exit, exit);
}

/**
 * pk_status_enum_from_text:
 **/
PkStatusEnum
pk_status_enum_from_text (const gchar *status)
{
	return pk_task_enum_find_value (task_status, status);
}

/**
 * pk_status_enum_to_text:
 **/
const gchar *
pk_status_enum_to_text (PkStatusEnum status)
{
	return pk_task_enum_find_string (task_status, status);
}

/**
 * pk_role_enum_from_text:
 **/
PkRoleEnum
pk_role_enum_from_text (const gchar *role)
{
	return pk_task_enum_find_value (task_role, role);
}

/**
 * pk_role_enum_to_text:
 **/
const gchar *
pk_role_enum_to_text (PkRoleEnum role)
{
	return pk_task_enum_find_string (task_role, role);
}

/**
 * pk_error_enum_from_text:
 **/
PkErrorCodeEnum
pk_error_enum_from_text (const gchar *code)
{
	return pk_task_enum_find_value (task_error, code);
}

/**
 * pk_error_enum_to_text:
 **/
const gchar *
pk_error_enum_to_text (PkErrorCodeEnum code)
{
	return pk_task_enum_find_string (task_error, code);
}

/**
 * pk_restart_enum_from_text:
 **/
PkRestartEnum
pk_restart_enum_from_text (const gchar *restart)
{
	return pk_task_enum_find_value (task_restart, restart);
}

/**
 * pk_restart_enum_to_text:
 **/
const gchar *
pk_restart_enum_to_text (PkRestartEnum restart)
{
	return pk_task_enum_find_string (task_restart, restart);
}

/**
 * pk_group_enum_from_text:
 **/
PkGroupEnum
pk_group_enum_from_text (const gchar *group)
{
	return pk_task_enum_find_value (task_group, group);
}

/**
 * pk_group_enum_to_text:
 **/
const gchar *
pk_group_enum_to_text (PkGroupEnum group)
{
	return pk_task_enum_find_string (task_group, group);
}

/**
 * pk_freq_enum_from_text:
 **/
PkFreqEnum
pk_freq_enum_from_text (const gchar *freq)
{
	return pk_task_enum_find_value (task_freq, freq);
}

/**
 * pk_freq_enum_to_text:
 **/
const gchar *
pk_freq_enum_to_text (PkFreqEnum freq)
{
	return pk_task_enum_find_string (task_freq, freq);
}

/**
 * pk_update_enum_from_text:
 **/
PkUpdateEnum
pk_update_enum_from_text (const gchar *update)
{
	return pk_task_enum_find_value (task_update, update);
}

/**
 * pk_update_enum_to_text:
 **/
const gchar *
pk_update_enum_to_text (PkUpdateEnum update)
{
	return pk_task_enum_find_string (task_update, update);
}

/**
 * pk_filter_enum_from_text:
 **/
PkFilterEnum
pk_filter_enum_from_text (const gchar *filter)
{
	return pk_task_enum_find_value (task_filter, filter);
}

/**
 * pk_filter_enum_to_text:
 **/
const gchar *
pk_filter_enum_to_text (PkFilterEnum filter)
{
	return pk_task_enum_find_string (task_filter, filter);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_enum (LibSelfTest *test)
{
	const gchar *string;
	PkRoleEnum value;

	if (libst_start (test, "PkEnum", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "find value");
	value = pk_task_enum_find_value (task_role, "search-file");
	if (PK_ROLE_ENUM_SEARCH_FILE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "find string");
	string = pk_task_enum_find_string (task_role, PK_ROLE_ENUM_SEARCH_FILE);
	if (strcmp (string, "search-file") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "find value");
	value = pk_role_enum_from_text ("search-file");
	if (PK_ROLE_ENUM_SEARCH_FILE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "find string");
	string = pk_role_enum_to_text (PK_ROLE_ENUM_SEARCH_FILE);
	if (strcmp (string, "search-file") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_end (test);
}
#endif

