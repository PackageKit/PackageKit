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

/**
 * pk_task_exit_from_text:
 */
PkTaskExit
pk_task_exit_from_text (const gchar *exit)
{
	if (strcmp (exit, "success") == 0) {
		return PK_TASK_EXIT_SUCCESS;
	}
	if (strcmp (exit, "failed") == 0) {
		return PK_TASK_EXIT_FAILED;
	}
	if (strcmp (exit, "canceled") == 0) {
		return PK_TASK_EXIT_CANCELED;
	}
	return PK_TASK_EXIT_UNKNOWN;
}

/**
 * pk_task_exit_to_text:
 **/
const gchar *
pk_task_exit_to_text (PkTaskExit exit)
{
	const gchar *text = NULL;
	switch (exit) {
	case PK_TASK_EXIT_SUCCESS:
		text = "success";
		break;
	case PK_TASK_EXIT_FAILED:
		text = "failed";
		break;
	case PK_TASK_EXIT_CANCELED:
		text = "canceled";
		break;
	default:
		text = "unknown";
	}
	return text;
}

/**
 * pk_task_status_from_text:
 **/
PkTaskStatus
pk_task_status_from_text (const gchar *status)
{
	g_return_val_if_fail (status != NULL, PK_TASK_STATUS_UNKNOWN);

	if (strcmp (status, "setup") == 0) {
		return PK_TASK_STATUS_SETUP;
	}
	if (strcmp (status, "query") == 0) {
		return PK_TASK_STATUS_QUERY;
	}
	if (strcmp (status, "remove") == 0) {
		return PK_TASK_STATUS_REMOVE;
	}
	if (strcmp (status, "refresh-cache") == 0) {
		return PK_TASK_STATUS_REFRESH_CACHE;
	}
	if (strcmp (status, "download") == 0) {
		return PK_TASK_STATUS_DOWNLOAD;
	}
	if (strcmp (status, "install") == 0) {
		return PK_TASK_STATUS_INSTALL;
	}
	if (strcmp (status, "update") == 0) {
		return PK_TASK_STATUS_UPDATE;
	}
	if (strcmp (status, "unknown") == 0) {
		return PK_TASK_STATUS_UNKNOWN;
	}
	pk_error ("fall through: '%s'", status);
	return PK_TASK_STATUS_UNKNOWN;
}

/**
 * pk_task_status_to_text:
 **/
const gchar *
pk_task_status_to_text (PkTaskStatus status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_TASK_STATUS_SETUP:
		text = "setup";
		break;
	case PK_TASK_STATUS_QUERY:
		text = "query";
		break;
	case PK_TASK_STATUS_REFRESH_CACHE:
		text = "refresh-cache";
		break;
	case PK_TASK_STATUS_REMOVE:
		text = "remove";
		break;
	case PK_TASK_STATUS_DOWNLOAD:
		text = "download";
		break;
	case PK_TASK_STATUS_INSTALL:
		text = "install";
		break;
	case PK_TASK_STATUS_UPDATE:
		text = "update";
		break;
	case PK_TASK_STATUS_UNKNOWN:
		text = "unknown";
		break;
	default:
		pk_error ("status unrecognised: %i", status);
	}
	return text;
}

/**
 * pk_task_status_to_localised_text:
 **/
const gchar *
pk_task_status_to_localised_text (PkTaskStatus status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_TASK_STATUS_SETUP:
		text = "Setting up";
		break;
	case PK_TASK_STATUS_QUERY:
		text = "Querying";
		break;
	case PK_TASK_STATUS_REMOVE:
		text = "Removing";
		break;
	case PK_TASK_STATUS_DOWNLOAD:
		text = "Downloading";
		break;
	case PK_TASK_STATUS_INSTALL:
		text = "Installing";
		break;
	case PK_TASK_STATUS_REFRESH_CACHE:
		text = "Refreshing package cache";
		break;
	case PK_TASK_STATUS_UPDATE:
		text = "Updating";
		break;
	default:
		pk_error ("status unrecognised: %i", status);
	}
	return text;
}

/**
 * pk_task_error_code_from_text:
 **/
PkTaskErrorCode
pk_task_error_code_from_text (const gchar *code)
{
	g_return_val_if_fail (code != NULL, PK_TASK_ERROR_CODE_UNKNOWN);

	if (strcmp (code, "no-network") == 0) {
		return PK_TASK_ERROR_CODE_NO_NETWORK;
	}
	if (strcmp (code, "not-supported") == 0) {
		return PK_TASK_ERROR_CODE_NOT_SUPPORTED;
	}
	if (strcmp (code, "internal-error") == 0) {
		return PK_TASK_ERROR_CODE_INTERNAL_ERROR;
	}
	return PK_TASK_ERROR_CODE_UNKNOWN;
}

/**
 * pk_task_error_code_to_text:
 **/
const gchar *
pk_task_error_code_to_text (PkTaskErrorCode code)
{
	const gchar *text = NULL;
	switch (code) {
	case PK_TASK_ERROR_CODE_NO_NETWORK:
		text = "no-network";
		break;
	case PK_TASK_ERROR_CODE_NOT_SUPPORTED:
		text = "not-supported";
		break;
	case PK_TASK_ERROR_CODE_INTERNAL_ERROR:
		text = "internal-error";
		break;
	default:
		text = "unknown";
	}
	return text;
}

/**
 * pk_task_error_code_to_localised_text:
 **/
const gchar *
pk_task_error_code_to_localised_text (PkTaskErrorCode code)
{
	const gchar *text = NULL;
	switch (code) {
	case PK_TASK_ERROR_CODE_NO_NETWORK:
		text = "No network connection available";
		break;
	case PK_TASK_ERROR_CODE_NOT_SUPPORTED:
		text = "Not supported by this backend";
		break;
	case PK_TASK_ERROR_CODE_INTERNAL_ERROR:
		text = "An internal system error has occurred";
		break;
	default:
		text = "Unknown error";
	}
	return text;
}

