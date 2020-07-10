/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2020 Neal Gompa <ngompa13@gmail.com>
 * based on dnf-backend-vendor-mageia.c
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "dnf-backend-vendor.h"

gboolean
dnf_validate_supported_repo (const gchar *id)
{
	guint i, j, k, l;

	const gchar *valid_sourcesect[] = { "-oss",
					  "-non-oss",
					  NULL };

	const gchar *valid_sourcetype[] = { "",
					  "-debuginfo",
					  "-source",
					  NULL };

	const gchar *valid_sourcechan[] = { "",
				      "-update",
				      NULL };

	const gchar *valid[] = { "opensuse-tumbleweed",
				 "opensuse-leap",
				 NULL };

	/* Iterate over the ID arrays to find a matching identifier */
	for (i = 0; valid[i] != NULL; i++) {
		for (j = 0; valid_sourcesect[j] != NULL; j++) {
			for (k = 0; valid_sourcechan[k] != NULL; k++) {
				for (l = 0; valid_sourcetype[l] != NULL; l++) {
					g_autofree gchar *source_entry = g_strconcat(valid[i], valid_sourcesect[j], valid_sourcechan[k], valid_sourcetype[l], NULL);
					if (g_strcmp0 (id, source_entry) == 0) {
						return TRUE;
					}
				}
			}
		}
	}
	return FALSE;
}
