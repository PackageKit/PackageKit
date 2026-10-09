/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "Test Fail";
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	//	pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR,
	//			       "Failed to initialize package manager");
}

void
pk_backend_destroy (PkBackend *backend)
{
	//	pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR,
	//			       "Failed to release control");
}

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY,
				       PK_GROUP_ENUM_GAMES,
				       PK_GROUP_ENUM_SYSTEM,
				       -1);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_GUI,
				       PK_FILTER_ENUM_INSTALLED,
				       PK_FILTER_ENUM_DEVELOPMENT,
				       -1);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	pk_backend_job_finished (job);
}

void
pk_backend_run_job (PkBackend *backend, PkBackendJob *job)
{
	PkRoleEnum role = pk_backend_job_get_role (job);

	switch (role) {
	case PK_ROLE_ENUM_DEPENDS_ON:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_GET_DETAILS_LOCAL:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_GET_FILES_LOCAL:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_GET_FILES:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_REQUIRED_BY:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_RESOLVE:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Error number 1");
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Duplicate error");
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Error number 1");
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		pk_backend_job_finished (job);
		break;
	case PK_ROLE_ENUM_UPGRADE_SYSTEM:
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_INTERNAL_ERROR,
					   "Cannot find boot partition");
		pk_backend_job_finished (job);
		break;
	default:
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_NOT_SUPPORTED,
					   "role %s is not supported",
					   pk_role_enum_to_string (role));
		pk_backend_job_finished (job);
		break;
	}
}

PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_ROLE_ENUM_CANCEL,
				       PK_ROLE_ENUM_DEPENDS_ON,
				       PK_ROLE_ENUM_GET_DETAILS,
				       PK_ROLE_ENUM_GET_DETAILS_LOCAL,
				       PK_ROLE_ENUM_GET_FILES_LOCAL,
				       PK_ROLE_ENUM_GET_FILES,
				       PK_ROLE_ENUM_REQUIRED_BY,
				       PK_ROLE_ENUM_GET_UPDATE_DETAIL,
				       PK_ROLE_ENUM_GET_UPDATES,
				       PK_ROLE_ENUM_INSTALL_PACKAGES,
				       PK_ROLE_ENUM_INSTALL_FILES,
				       PK_ROLE_ENUM_REFRESH_CACHE,
				       PK_ROLE_ENUM_REMOVE_PACKAGES,
				       PK_ROLE_ENUM_RESOLVE,
				       PK_ROLE_ENUM_SEARCH_DETAILS,
				       PK_ROLE_ENUM_SEARCH_FILE,
				       PK_ROLE_ENUM_SEARCH_GROUP,
				       PK_ROLE_ENUM_SEARCH_NAME,
				       PK_ROLE_ENUM_UPDATE_PACKAGES,
				       PK_ROLE_ENUM_UPGRADE_SYSTEM,
				       -1);
}
