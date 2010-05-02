/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <pacman.h>
#include "backend-error.h"

void
backend_error (PkBackend *backend, GError *error)
{
	PkErrorEnum code = PK_ERROR_ENUM_INTERNAL_ERROR;

	g_return_if_fail (backend != NULL);
	g_return_if_fail (error != NULL);

	/* convert error codes */
	if (error->domain == PACMAN_ERROR) {
		switch (error->code) {
			case PACMAN_ERROR_MEMORY:
				code = PK_ERROR_ENUM_OOM;
				break;

			case PACMAN_ERROR_SYSTEM:
			case PACMAN_ERROR_INVALID_ARGS:
			case PACMAN_ERROR_NOT_INITIALIZED:
			case PACMAN_ERROR_DATABASE_NOT_INITIALIZED:
			case PACMAN_ERROR_SERVER_INVALID_URL:
			case PACMAN_ERROR_REGEX_INVALID:
			case PACMAN_ERROR_LIBARCHIVE:
			case PACMAN_ERROR_LIBFETCH:
			case PACMAN_ERROR_DOWNLOAD_HANDLER:
				code = PK_ERROR_ENUM_INTERNAL_ERROR;
				break;

			case PACMAN_ERROR_NOT_PERMITTED:
				code = PK_ERROR_ENUM_NOT_AUTHORIZED;
				break;

			case PACMAN_ERROR_FILE_NOT_FOUND:
			case PACMAN_ERROR_DIRECTORY_NOT_FOUND:
				code = PK_ERROR_ENUM_FILE_NOT_FOUND;
				break;

			case PACMAN_ERROR_ALREADY_INITIALIZED:
			case PACMAN_ERROR_DATABASE_ALREADY_INITIALIZED:
				code = PK_ERROR_ENUM_FAILED_INITIALIZATION;
				break;

			case PACMAN_ERROR_ALREADY_RUNNING:
				code = PK_ERROR_ENUM_CANNOT_GET_LOCK;
				break;

			case PACMAN_ERROR_DATABASE_OPEN_FAILED:
				code = PK_ERROR_ENUM_REPO_NOT_FOUND;
				break;

			case PACMAN_ERROR_DATABASE_CREATE_FAILED:
				code = PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG;
				break;

			case PACMAN_ERROR_DATABASE_NOT_FOUND:
				code = PK_ERROR_ENUM_REPO_NOT_FOUND;
				break;

			case PACMAN_ERROR_DATABASE_UPDATE_FAILED:
				code = PK_ERROR_ENUM_REPO_NOT_AVAILABLE;
				break;

			case PACMAN_ERROR_DATABASE_REMOVE_FAILED:
				code = PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR;
				break;

			case PACMAN_ERROR_SERVER_NONE_AVAILABLE:
				code = PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY;
				break;

			case PACMAN_ERROR_TRANSACTION_ALREADY_INITIALIZED:
			case PACMAN_ERROR_TRANSACTION_NOT_INITIALIZED:
			case PACMAN_ERROR_TRANSACTION_DUPLICATE_TARGET:
			case PACMAN_ERROR_TRANSACTION_NOT_READY:
			case PACMAN_ERROR_TRANSACTION_NOT_PREPARED:
			case PACMAN_ERROR_TRANSACTION_INVALID_OPERATION:
			case PACMAN_ERROR_TRANSACTION_NOT_LOCKED:
				code = PK_ERROR_ENUM_TRANSACTION_ERROR;
				break;

			case PACMAN_ERROR_TRANSACTION_ABORTED:
				code = PK_ERROR_ENUM_TRANSACTION_CANCELLED;
				break;

			case PACMAN_ERROR_PACKAGE_NOT_FOUND:
				code = PK_ERROR_ENUM_PACKAGE_NOT_FOUND;
				break;

			case PACMAN_ERROR_PACKAGE_IGNORED:
				code = PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED;
				break;

			case PACMAN_ERROR_DELTA_INVALID:
			case PACMAN_ERROR_PACKAGE_INVALID:
				code = PK_ERROR_ENUM_INVALID_PACKAGE_FILE;
				break;

			case PACMAN_ERROR_PACKAGE_OPEN_FAILED:
				code = PK_ERROR_ENUM_PACKAGE_NOT_FOUND;
				break;

			case PACMAN_ERROR_PACKAGE_REMOVE_FAILED:
				code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE;
				break;

			case PACMAN_ERROR_PACKAGE_UNKNOWN_FILENAME:
			case PACMAN_ERROR_PACKAGE_DATABASE_NOT_FOUND:
				code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_CONFIGURE;
				break;

			case PACMAN_ERROR_DELTA_PATCH_FAILED:
				code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD;
				break;

			case PACMAN_ERROR_DEPENDENCY_UNSATISFIED:
				code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
				break;

			case PACMAN_ERROR_CONFLICT:
				code = PK_ERROR_ENUM_PACKAGE_CONFLICTS;
				break;

			case PACMAN_ERROR_FILE_CONFLICT:
				code = PK_ERROR_ENUM_FILE_CONFLICTS;
				break;

			case PACMAN_ERROR_DOWNLOAD_FAILED:
				code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
				break;

			case PACMAN_ERROR_CONFIG_INVALID:
				code = PK_ERROR_ENUM_FAILED_CONFIG_PARSING;
				break;

			case PACMAN_ERROR_PACKAGE_HELD:
				code = PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE;
				break;
		}
	}

	pk_backend_error_code (backend, code, "%s", error->message);
	g_error_free (error);
}

void
backend_message (PkBackend *backend, const gchar *message)
{
	guint iterator;
	gchar **messages = g_strsplit_set (message, "\r\n", 0);

	/* display multi-line messages in a nice format */
	for (iterator = 0; messages[iterator] != NULL; ++iterator) {
		g_strstrip (messages[iterator]);
		if (*messages[iterator] != '\0') {
			pk_backend_message (backend, PK_MESSAGE_ENUM_UNKNOWN, "%s", messages[iterator]);
		}
	}

	g_strfreev (messages);
}
