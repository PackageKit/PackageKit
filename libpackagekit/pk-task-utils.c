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
#include "pk-task-utils.h"

typedef struct {
	guint		 value;
	const gchar	*string;
} PkTaskEnumMatch;

static PkTaskEnumMatch task_exit[] = {
	{PK_TASK_EXIT_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_TASK_EXIT_SUCCESS,			"success"},
	{PK_TASK_EXIT_FAILED,			"failed"},
	{PK_TASK_EXIT_CANCELED,			"canceled"},
	{0, NULL},
};

static PkTaskEnumMatch task_status[] = {
	{PK_TASK_STATUS_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_TASK_STATUS_SETUP,			"setup"},
	{PK_TASK_STATUS_QUERY,			"query"},
	{PK_TASK_STATUS_REFRESH_CACHE,		"refresh-cache"},
	{PK_TASK_STATUS_REMOVE,			"remove"},
	{PK_TASK_STATUS_DOWNLOAD,		"download"},
	{PK_TASK_STATUS_INSTALL,		"install"},
	{PK_TASK_STATUS_UPDATE,			"update"},
	{0, NULL},
};

static PkTaskEnumMatch task_error[] = {
	{PK_TASK_ERROR_CODE_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_TASK_ERROR_CODE_NO_NETWORK,		"no-network"},
	{PK_TASK_ERROR_CODE_NOT_SUPPORTED,	"not-supported"},
	{PK_TASK_ERROR_CODE_INTERNAL_ERROR,	"internal-error"},
	{PK_TASK_ERROR_CODE_GPG_FAILURE,	"gpg-failure"},
	{PK_TASK_ERROR_CODE_FILTER_INVALID,	"filter-invalid"},
	{PK_TASK_ERROR_CODE_PACKAGE_ID_INVALID,	"package-id-invalid"},
	{PK_TASK_ERROR_CODE_PACKAGE_NOT_INSTALLED,	"package-not-installed"},
	{PK_TASK_ERROR_CODE_PACKAGE_ALREADY_INSTALLED,	"package-already-installed"},
	{PK_TASK_ERROR_CODE_PACKAGE_DOWNLOAD_FAILED,	"package-download-failed"},
	{PK_TASK_ERROR_CODE_DEP_RESOLUTION_FAILED,	"dep-resolution-failed"},
	{0, NULL},
};

static PkTaskEnumMatch task_restart[] = {
	{PK_TASK_RESTART_NONE,			"none"},
	{PK_TASK_RESTART_SYSTEM,		"system"},
	{PK_TASK_RESTART_SESSION,		"session"},
	{PK_TASK_RESTART_APPLICATION,		"application"},
	{0, NULL},
};

static PkTaskEnumMatch task_group[] = {
	{PK_TASK_GROUP_ACCESSIBILITY,		"accessibility"},
	{PK_TASK_GROUP_ACCESSORIES,		"accessories"},
	{PK_TASK_GROUP_EDUCATION,		"education"},
	{PK_TASK_GROUP_GAMES,			"games"},
	{PK_TASK_GROUP_GRAPHICS,		"graphics"},
	{PK_TASK_GROUP_INTERNET,		"internet"},
	{PK_TASK_GROUP_OFFICE,			"office"},
	{PK_TASK_GROUP_OTHER,			"other"},
	{PK_TASK_GROUP_PROGRAMMING,		"programming"},
	{PK_TASK_GROUP_SOUND_VIDEO,		"sound-video"},
	{PK_TASK_GROUP_SYSTEM,			"system"},
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
 * pk_task_exit_from_text:
 */
PkTaskExit
pk_task_exit_from_text (const gchar *exit)
{
	return pk_task_enum_find_value (task_exit, exit);
}

/**
 * pk_task_exit_to_text:
 **/
const gchar *
pk_task_exit_to_text (PkTaskExit exit)
{
	return pk_task_enum_find_string (task_exit, exit);
}

/**
 * pk_task_status_from_text:
 **/
PkTaskStatus
pk_task_status_from_text (const gchar *status)
{
	return pk_task_enum_find_value (task_status, status);
}

/**
 * pk_task_status_to_text:
 **/
const gchar *
pk_task_status_to_text (PkTaskStatus status)
{
	return pk_task_enum_find_string (task_status, status);
}

/**
 * pk_task_error_code_from_text:
 **/
PkTaskErrorCode
pk_task_error_code_from_text (const gchar *code)
{
	return pk_task_enum_find_value (task_error, code);
}

/**
 * pk_task_error_code_to_text:
 **/
const gchar *
pk_task_error_code_to_text (PkTaskErrorCode code)
{
	return pk_task_enum_find_string (task_error, code);
}

/**
 * pk_task_restart_from_text:
 **/
PkTaskRestart
pk_task_restart_from_text (const gchar *restart)
{
	return pk_task_enum_find_value (task_restart, restart);
}

/**
 * pk_task_restart_to_text:
 **/
const gchar *
pk_task_restart_to_text (PkTaskRestart restart)
{
	return pk_task_enum_find_string (task_restart, restart);
}

/**
 * pk_task_group_from_text:
 **/
PkTaskGroup
pk_task_group_from_text (const gchar *group)
{
	return pk_task_enum_find_value (task_group, group);
}

/**
 * pk_task_group_to_text:
 **/
const gchar *
pk_task_group_to_text (PkTaskGroup group)
{
	return pk_task_enum_find_string (task_group, group);
}

/**
 * pk_task_check_package_id:
 **/
gboolean
pk_task_check_package_id (const gchar *package_id)
{
	guint i;
	guint length;
	guint sections;

	if (package_id == NULL) {
		return FALSE;
	}

	length = strlen (package_id);
	sections = 1;
	for (i=0; i<length; i++) {
		if (package_id[i] == ';') {
			sections += 1;
		}
	}
	if (sections != 4) {
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_task_check_filter_part:
 **/
gboolean
pk_task_check_filter_part (const gchar *filter)
{
	if (filter == NULL) {
		return FALSE;
	}
	if (strlen (filter) == 0) {
		return FALSE;
	}
	if (strcmp (filter, "installed") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "!installed") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "devel") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "!devel") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "gui") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "!gui") == 0) {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_task_check_filter:
 **/
gboolean
pk_task_check_filter (const gchar *filter)
{
	gchar **sections;
	guint i;

	/* split by delimeter ';' */
	sections = g_strsplit (filter, ";", 4);
	for (i=0; i>3; i++) {
		if (pk_task_check_filter_part (sections[3]) == FALSE) {
			return FALSE;
		}
	}
	g_strfreev (sections);
	return TRUE;	
}

/**
 * pk_task_package_ident_build:
 **/
gchar *
pk_task_package_ident_build (const gchar *name, const gchar *version,
			     const gchar *arch, const gchar *data)
{
	return g_strdup_printf ("%s;%s;%s;%s", name, version, arch, data);
}

/**
 * pk_task_package_ident_new:
 **/
PkPackageIdent *
pk_task_package_ident_new (void)
{
	PkPackageIdent *ident;
	ident = g_new0 (PkPackageIdent, 1);
	ident->name = NULL;
	ident->version = NULL;
	ident->arch = NULL;
	ident->data = NULL;
	return ident;
}

/**
 * pk_task_package_ident_from_string:
 **/
PkPackageIdent*
pk_task_package_ident_from_string (const gchar *package_id)
{
	gchar **sections;
	PkPackageIdent *ident;

	/* create new object */
	ident = pk_task_package_ident_new ();

	/* split by delimeter ';' */
	sections = g_strsplit (package_id, ";", 4);
	ident->name = g_strdup (sections[0]);
	ident->version = g_strdup (sections[1]);
	ident->arch = g_strdup (sections[2]);
	ident->data = g_strdup (sections[3]);
	g_strfreev (sections);
	return ident;	
}

/**
 * pk_task_package_ident_to_string:
 **/
gchar *
pk_task_package_ident_to_string (PkPackageIdent *ident)
{
	return g_strdup_printf ("%s;%s;%s;%s", ident->name, ident->version,
				ident->arch, ident->data);
}

/**
 * pk_task_package_ident_free:
 **/
gboolean
pk_task_package_ident_free (PkPackageIdent *ident)
{
	g_free (ident->name);
	g_free (ident->arch);
	g_free (ident->version);
	g_free (ident->data);
	g_free (ident);
	return TRUE;
}

