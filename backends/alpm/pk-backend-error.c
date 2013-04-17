/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
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

#include <alpm.h>

#include "pk-backend-error.h"

void
pk_backend_error (PkBackend *self, GError *error)
{
	PkErrorEnum code = PK_ERROR_ENUM_UNKNOWN;

	g_return_if_fail (self != NULL);
	g_return_if_fail (error != NULL);

	if (error->domain == ALPM_ERROR) {
		switch (error->code) {
			case PM_ERR_MEMORY:
			case PM_ERR_SYSTEM:
				code = PK_ERROR_ENUM_OOM;
				break;

			case PM_ERR_BADPERMS:
				code = PK_ERROR_ENUM_NOT_AUTHORIZED;
				break;

			case PM_ERR_NOT_A_FILE:
			case PM_ERR_NOT_A_DIR:
				code = PK_ERROR_ENUM_FILE_NOT_FOUND;
				break;

			case PM_ERR_WRONG_ARGS:
			case PM_ERR_HANDLE_NULL:
			case PM_ERR_DB_NULL:
			case PM_ERR_TRANS_NULL:
			case PM_ERR_TRANS_NOT_INITIALIZED:
			case PM_ERR_TRANS_NOT_PREPARED:
			case PM_ERR_TRANS_NOT_LOCKED:
			case PM_ERR_INVALID_REGEX:
				code = PK_ERROR_ENUM_INTERNAL_ERROR;
				break;

			case PM_ERR_DISK_SPACE:
				code = PK_ERROR_ENUM_NO_SPACE_ON_DEVICE;
				break;

			case PM_ERR_HANDLE_NOT_NULL:
			case PM_ERR_DB_NOT_NULL:
			case PM_ERR_TRANS_NOT_NULL:
				code = PK_ERROR_ENUM_FAILED_INITIALIZATION;
				break;

			case PM_ERR_HANDLE_LOCK:
				code = PK_ERROR_ENUM_CANNOT_GET_LOCK;
				break;

			case PM_ERR_DB_OPEN:
			case PM_ERR_DB_NOT_FOUND:
			case PM_ERR_PKG_REPO_NOT_FOUND:
				code = PK_ERROR_ENUM_REPO_NOT_FOUND;
				break;

			case PM_ERR_DB_CREATE:
				code = PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG;
				break;

			case PM_ERR_DB_VERSION:
			case PM_ERR_DB_REMOVE:
				code = PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR;
				break;

			case PM_ERR_DB_WRITE:
				code = PK_ERROR_ENUM_REPO_NOT_AVAILABLE;
				break;

			case PM_ERR_SERVER_BAD_URL:
				code = PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR;
				break;

			case PM_ERR_SERVER_NONE:
				code = PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY;
				break;

			case PM_ERR_TRANS_DUP_TARGET:
			case PM_ERR_TRANS_ABORT:
				code = PK_ERROR_ENUM_TRANSACTION_ERROR;
				break;

			case PM_ERR_TRANS_TYPE:
				code = PK_ERROR_ENUM_CANNOT_CANCEL;
				break;

			case PM_ERR_PKG_NOT_FOUND:
				code = PK_ERROR_ENUM_PACKAGE_NOT_FOUND;
				break;

			case PM_ERR_PKG_IGNORED:
				code = PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED;
				break;

			case PM_ERR_PKG_INVALID:
			case PM_ERR_PKG_OPEN:
			case PM_ERR_PKG_INVALID_NAME:
			case PM_ERR_DLT_INVALID:
				code = PK_ERROR_ENUM_INVALID_PACKAGE_FILE;
				break;

			case PM_ERR_PKG_CANT_REMOVE:
				code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE;
				break;

			case PM_ERR_PKG_INVALID_ARCH:
				code = PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE;
				break;

			case PM_ERR_DLT_PATCHFAILED:
				code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD;
				break;

			case PM_ERR_UNSATISFIED_DEPS:
				code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
				break;

			case PM_ERR_CONFLICTING_DEPS:
				code = PK_ERROR_ENUM_PACKAGE_CONFLICTS;
				break;

			case PM_ERR_FILE_CONFLICTS:
				code = PK_ERROR_ENUM_FILE_CONFLICTS;
				break;

			case PM_ERR_RETRIEVE:
			case PM_ERR_LIBFETCH:
			case PM_ERR_EXTERNAL_DOWNLOAD:
				code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
				break;

			case PM_ERR_LIBARCHIVE:
				code = PK_ERROR_ENUM_LOCAL_INSTALL_FAILED;
				break;

			case PM_ERR_CONFIG_INVALID:
				code = PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE;
				break;

			case PM_ERR_PKG_HELD:
				code = PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE;
				break;
		}
	}

	pk_backend_error_code (self, code, "%s", error->message);
}

GQuark
alpm_error_quark (void)
{
	return g_quark_from_static_string ("alpm-error-quark");
}
