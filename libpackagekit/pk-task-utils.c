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

static PkTaskEnumMatch task_role[] = {
	{PK_TASK_ROLE_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_TASK_ROLE_QUERY,			"query"},
	{PK_TASK_ROLE_REFRESH_CACHE,		"refresh-cache"},
	{PK_TASK_ROLE_PACKAGE_REMOVE,		"package-remove"},
	{PK_TASK_ROLE_PACKAGE_INSTALL,		"package-install"},
	{PK_TASK_ROLE_PACKAGE_UPDATE,		"package-update"},
	{PK_TASK_ROLE_SYSTEM_UPDATE,		"system-update"},
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
	{PK_TASK_GROUP_MULTIMEDIA,		"multimedia"},
	{PK_TASK_GROUP_SYSTEM,			"system"},
	{0, NULL},
};

static PkTaskEnumMatch task_action[] = {
	{PK_TASK_ACTION_INSTALL,		"install"},
	{PK_TASK_ACTION_REMOVE,			"remove"},
	{PK_TASK_ACTION_UPDATE,			"update"},
	{PK_TASK_ACTION_GET_UPDATES,		"get-updates"},
	{PK_TASK_ACTION_REFRESH_CACHE,		"refresh-cache"},
	{PK_TASK_ACTION_UPDATE_SYSTEM,		"update-system"},
	{PK_TASK_ACTION_SEARCH_NAME,		"search-name"},
	{PK_TASK_ACTION_SEARCH_DETAILS,		"search-details"},
	{PK_TASK_ACTION_SEARCH_GROUP,		"search-group"},
	{PK_TASK_ACTION_SEARCH_FILE,		"search-file"},
	{PK_TASK_ACTION_GET_DEPENDS,		"get-depends"},
	{PK_TASK_ACTION_GET_REQUIRES,		"get-requires"},
	{PK_TASK_ACTION_GET_DESCRIPTION,	"get-description"},
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
 * pk_task_role_from_text:
 **/
PkTaskRole
pk_task_role_from_text (const gchar *role)
{
	return pk_task_enum_find_value (task_role, role);
}

/**
 * pk_task_role_to_text:
 **/
const gchar *
pk_task_role_to_text (PkTaskRole role)
{
	return pk_task_enum_find_string (task_role, role);
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
 * pk_task_action_from_text:
 **/
PkTaskAction
pk_task_action_from_text (const gchar *action)
{
	return pk_task_enum_find_value (task_action, action);
}

/**
 * pk_task_action_to_text:
 **/
const gchar *
pk_task_action_to_text (PkTaskAction action)
{
	return pk_task_enum_find_string (task_action, action);
}

/**
 * pk_task_filter_check_part:
 **/
gboolean
pk_task_filter_check_part (const gchar *filter)
{
	if (filter == NULL) {
		return FALSE;
	}
	if (strlen (filter) == 0) {
		return FALSE;
	}
	if (strcmp (filter, "none") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "installed") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "~installed") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "devel") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "~devel") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "gui") == 0) {
		return TRUE;
	}
	if (strcmp (filter, "~gui") == 0) {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_task_filter_check:
 **/
gboolean
pk_task_filter_check (const gchar *filter)
{
	gchar **sections;
	guint i;
	guint length;
	gboolean ret;

	if (filter == NULL) {
		pk_warning ("filter null");
		return FALSE;
	}
	if (strlen (filter) == 0) {
		pk_warning ("filter zero length");
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (filter, ";", 0);
	length = g_strv_length (sections);
	ret = FALSE;
	for (i=0; i<length; i++) {
		/* only one wrong part is enough to fail the filter */
		if (strlen (sections[i]) == 0) {
			goto out;
		}
		if (pk_task_filter_check_part (sections[i]) == FALSE) {
			goto out;
		}
	}
	ret = TRUE;
out:
	g_strfreev (sections);
	return ret;
}

/**
 * pk_task_action_build:
 **/
gchar *
pk_task_action_build (PkTaskAction action, ...)
{
	va_list args;
	guint i;
	GString *string;
	PkTaskAction action_temp;

	string = g_string_new (pk_task_action_to_text (action));
	g_string_append (string, ";");

	/* process the valist */
	va_start (args, action);
	for (i=0;; i++) {
		action_temp = va_arg (args, PkTaskAction);
		if (action_temp == 0) break;
		g_string_append (string, pk_task_action_to_text (action_temp));
		g_string_append (string, ";");
	}
	va_end (args);

	/* remove last ';' */
	g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * pk_task_action_contains:
 **/
gboolean
pk_task_action_contains (const gchar *actions, PkTaskAction action)
{
	gchar **sections;
	guint i;
	guint ret = FALSE;

	if (actions == NULL) {
		pk_warning ("actions null");
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (actions, ";", 0);

	for (i=0; sections[i]; i++) {
		if (pk_task_action_from_text (sections[i]) == action) {
			ret = TRUE;
			break;
		}
	}
	g_strfreev (sections);
	return ret;
}

/**
 * pk_util_action_new:
 **/
PkActionList *
pk_util_action_new (PkTaskAction action, ...)
{
	va_list args;
	guint i;
	PkActionList *alist;
	PkTaskAction action_temp;

	/* create a new list. A list must have at least one entry */
	alist = g_ptr_array_new ();
	g_ptr_array_add (alist, GUINT_TO_POINTER(action));

	/* process the valist */
	va_start (args, action);
	for (i=0;; i++) {
		action_temp = va_arg (args, PkTaskAction);
		if (action_temp == 0) break;
		g_ptr_array_add (alist, GUINT_TO_POINTER(action_temp));
	}
	va_end (args);

	return alist;
}


/**
 * pk_util_action_new_from_string:
 **/
PkActionList *
pk_util_action_new_from_string (const gchar *actions)
{
	PkActionList *alist;
	gchar **sections;
	guint i;
	PkTaskAction action_temp;

	if (actions == NULL) {
		pk_warning ("actions null");
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (actions, ";", 0);

	/* create a new list. A list must have at least one entry */
	alist = g_ptr_array_new ();

	for (i=0; sections[i]; i++) {
		action_temp = pk_task_action_from_text (sections[i]);
		g_ptr_array_add (alist, GUINT_TO_POINTER(action_temp));
	}
	g_strfreev (sections);
	return alist;
}

/**
 * pk_util_action_free:
 **/
gboolean
pk_util_action_free (PkActionList *alist)
{
	g_ptr_array_free (alist, TRUE);
	return TRUE;
}

/**
 * pk_util_action_to_string:
 **/
gchar *
pk_util_action_to_string (PkActionList *alist)
{
	guint i;
	GString *string;

	string = g_string_new ("");
	for (i=0; i<alist->len; i++) {
		g_string_append (string, g_ptr_array_index (alist, i));
		g_string_append (string, ";");
	}

	/* remove last ';' */
	g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * pk_util_action_contains:
 **/
gboolean
pk_util_action_contains (PkActionList *alist, PkTaskAction action)
{
	guint i;
	for (i=0; i<alist->len; i++) {
		if (GPOINTER_TO_UINT (g_ptr_array_index (alist, i)) == action) {
			return TRUE;
		}
	}
	return FALSE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_task_utils (LibSelfTest *test)
{
	gboolean ret;
	gchar *text;
	const gchar *temp;

	if (libst_start (test, "PkTaskUtils", CLASS_AUTO) == FALSE) {
		return;
	}


	/************************************************************
	 ****************          FILTERS         ******************
	 ************************************************************/
	temp = NULL;
	libst_title (test, "test a fail filter (null)");
	ret = pk_task_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "";
	libst_title (test, "test a fail filter ()");
	ret = pk_task_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = ";";
	libst_title (test, "test a fail filter (;)");
	ret = pk_task_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "moo";
	libst_title (test, "test a fail filter (invalid)");
	ret = pk_task_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "moo;foo";
	libst_title (test, "test a fail filter (invalid, multiple)");
	ret = pk_task_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "gui;;";
	libst_title (test, "test a fail filter (valid then zero length)");
	ret = pk_task_filter_check (temp);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "passed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "none";
	libst_title (test, "test a pass filter (none)");
	ret = pk_task_filter_check (temp);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "gui";
	libst_title (test, "test a pass filter (single)");
	ret = pk_task_filter_check (temp);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "devel;~gui";
	libst_title (test, "test a pass filter (multiple)");
	ret = pk_task_filter_check (temp);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************/
	temp = "~gui;~installed";
	libst_title (test, "test a pass filter (multiple2)");
	ret = pk_task_filter_check (temp);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "failed the filter '%s'", temp);
	}

	/************************************************************
	 ****************          ACTIONS         ******************
	 ************************************************************/
	libst_title (test, "test the action building (single)");
	text = pk_task_action_build (PK_TASK_ACTION_INSTALL, 0);
	if (strcmp (text, "install") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect single argument '%s'", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "test the action building (multiple)");
	text = pk_task_action_build (PK_TASK_ACTION_INSTALL, PK_TASK_ACTION_SEARCH_NAME, PK_TASK_ACTION_GET_DEPENDS, 0);
	if (strcmp (text, "install;search-name;get-depends") == 0) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "incorrect multiple argument '%s'", text);
	}

	/************************************************************/
	libst_title (test, "test the action checking (present)");
	ret = pk_task_action_contains (text, PK_TASK_ACTION_INSTALL);
	if (ret == TRUE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "not found present");
	}

	/************************************************************/
	libst_title (test, "test the action checking (not-present)");
	ret = pk_task_action_contains (text, PK_TASK_ACTION_REMOVE);
	if (ret == FALSE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "found present");
	}
	g_free (text);

	libst_end (test);
}
#endif

