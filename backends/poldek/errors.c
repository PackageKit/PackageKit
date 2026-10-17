/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
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

/*
 * The error a failed poldek or rpm run is reported as, from what they
 * logged. The texts are matched with LC_ALL=C, which the helper runs with.
 */

#include <string.h>

#include "errors.h"

typedef struct
{
	const gchar *text;
	PkErrorEnum code;
} PkPoldekErrorMap;

/* the first one found in the messages wins, so the more specific ones come first */
static const PkPoldekErrorMap pk_poldek_error_map[] = {
	{ "can't create transaction lock", PK_ERROR_ENUM_CANNOT_GET_LOCK },
	{ "conflicts between attempted installs", PK_ERROR_ENUM_FILE_CONFLICTS },
	{ "conflicts with file from package", PK_ERROR_ENUM_FILE_CONFLICTS },
	{ "conflicts with", PK_ERROR_ENUM_PACKAGE_CONFLICTS },
	{ "does not verify", PK_ERROR_ENUM_BAD_GPG_SIGNATURE },
	{ "is needed by", PK_ERROR_ENUM_DEP_RESOLUTION_FAILED },
	{ "unresolved depend", PK_ERROR_ENUM_DEP_RESOLUTION_FAILED },
	{ " not found", PK_ERROR_ENUM_DEP_RESOLUTION_FAILED },
	{ " more space on the ", PK_ERROR_ENUM_NO_SPACE_ON_DEVICE },
	{ " more inodes on the ", PK_ERROR_ENUM_NO_SPACE_ON_DEVICE },
	{ "No space left on device", PK_ERROR_ENUM_NO_SPACE_ON_DEVICE },
	{ "vfff:", PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED },
	{ "vfjuggle:", PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED },
	{ "Could not resolve", PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED },
};

PkErrorEnum
pk_poldek_error_from_messages (const gchar *messages, PkErrorEnum fallback)
{
	if (messages == NULL)
		return fallback;
	for (guint i = 0; i < G_N_ELEMENTS (pk_poldek_error_map); i++) {
		if (strstr (messages, pk_poldek_error_map[i].text) != NULL)
			return pk_poldek_error_map[i].code;
	}
	return fallback;
}
