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

#include "pk-alpm-error.h"

void
pk_alpm_error_emit (PkBackendJob *job, GError *error)
{
	PkErrorEnum code = PK_ERROR_ENUM_UNKNOWN;

	g_return_if_fail (error != NULL);

	if (error->domain != PK_ALPM_ERROR) {
		pk_backend_job_error_code (job, code, "%s", error->message);
		return;
	}

	switch (error->code) {
	case ALPM_ERR_MEMORY:
	case ALPM_ERR_SYSTEM:
		code = PK_ERROR_ENUM_OOM;
		break;
	case ALPM_ERR_BADPERMS:
		code = PK_ERROR_ENUM_NOT_AUTHORIZED;
		break;
	case ALPM_ERR_NOT_A_FILE:
	case ALPM_ERR_NOT_A_DIR:
		code = PK_ERROR_ENUM_FILE_NOT_FOUND;
		break;
	case ALPM_ERR_WRONG_ARGS:
	case ALPM_ERR_HANDLE_NULL:
	case ALPM_ERR_DB_NULL:
	case ALPM_ERR_TRANS_NULL:
	case ALPM_ERR_TRANS_NOT_INITIALIZED:
	case ALPM_ERR_TRANS_NOT_PREPARED:
	case ALPM_ERR_TRANS_NOT_LOCKED:
	case ALPM_ERR_INVALID_REGEX:
		code = PK_ERROR_ENUM_INTERNAL_ERROR;
		break;
	case ALPM_ERR_DISK_SPACE:
		code = PK_ERROR_ENUM_NO_SPACE_ON_DEVICE;
		break;
	case ALPM_ERR_HANDLE_NOT_NULL:
	case ALPM_ERR_DB_NOT_NULL:
	case ALPM_ERR_TRANS_NOT_NULL:
		code = PK_ERROR_ENUM_FAILED_INITIALIZATION;
		break;
	case ALPM_ERR_HANDLE_LOCK:
		code = PK_ERROR_ENUM_CANNOT_GET_LOCK;
		break;
	case ALPM_ERR_DB_OPEN:
	case ALPM_ERR_DB_NOT_FOUND:
		code = PK_ERROR_ENUM_REPO_NOT_FOUND;
		break;
	case ALPM_ERR_DB_CREATE:
		code = PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG;
		break;
	case ALPM_ERR_DB_INVALID:
	case ALPM_ERR_DB_VERSION:
	case ALPM_ERR_DB_REMOVE:
	case ALPM_ERR_SERVER_BAD_URL:
		code = PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR;
		break;
	case ALPM_ERR_DB_INVALID_SIG:
	case ALPM_ERR_PKG_INVALID_SIG:
	case ALPM_ERR_SIG_INVALID:
		code = PK_ERROR_ENUM_BAD_GPG_SIGNATURE;
		break;
	case ALPM_ERR_DB_WRITE:
		code = PK_ERROR_ENUM_REPO_NOT_AVAILABLE;
		break;
	case ALPM_ERR_SERVER_NONE:
		code = PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY;
		break;
	case ALPM_ERR_TRANS_DUP_TARGET:
	case ALPM_ERR_TRANS_ABORT:
		code = PK_ERROR_ENUM_TRANSACTION_ERROR;
		break;
	case ALPM_ERR_TRANS_TYPE:
		code = PK_ERROR_ENUM_CANNOT_CANCEL;
		break;
	case ALPM_ERR_PKG_NOT_FOUND:
		code = PK_ERROR_ENUM_PACKAGE_NOT_FOUND;
		break;
	case ALPM_ERR_PKG_IGNORED:
		code = PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED;
		break;
	case ALPM_ERR_PKG_INVALID:
	case ALPM_ERR_PKG_OPEN:
	case ALPM_ERR_PKG_INVALID_NAME:
		code = PK_ERROR_ENUM_INVALID_PACKAGE_FILE;
		break;
	case ALPM_ERR_PKG_INVALID_CHECKSUM:
		code = PK_ERROR_ENUM_PACKAGE_CORRUPT;
		break;
	case ALPM_ERR_PKG_CANT_REMOVE:
		code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE;
		break;
	case ALPM_ERR_PKG_INVALID_ARCH:
		code = PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE;
		break;
	case ALPM_ERR_SIG_MISSING:
		code = PK_ERROR_ENUM_MISSING_GPG_SIGNATURE;
		break;
	case ALPM_ERR_UNSATISFIED_DEPS:
		code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
		break;
	case ALPM_ERR_CONFLICTING_DEPS:
		code = PK_ERROR_ENUM_PACKAGE_CONFLICTS;
		break;
	case ALPM_ERR_FILE_CONFLICTS:
		code = PK_ERROR_ENUM_FILE_CONFLICTS;
		break;
	case ALPM_ERR_RETRIEVE:
	case ALPM_ERR_LIBCURL:
	case ALPM_ERR_EXTERNAL_DOWNLOAD:
		code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
		break;
	case ALPM_ERR_LIBARCHIVE:
		code = PK_ERROR_ENUM_LOCAL_INSTALL_FAILED;
		break;
	case ALPM_ERR_GPGME:
		code = PK_ERROR_ENUM_GPG_FAILURE;
		break;
	case PK_ALPM_ERR_CONFIG_INVALID:
		code = PK_ERROR_ENUM_FAILED_CONFIG_PARSING;
		break;
	case PK_ALPM_ERR_PKG_HELD:
		code = PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE;
		break;
	}

	pk_backend_job_error_code (job, code, "%s", error->message);
}

G_DEFINE_QUARK (pk-alpm-error-quark, pk_alpm_error)
