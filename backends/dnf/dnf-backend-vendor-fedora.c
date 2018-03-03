/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Neal Gompa <ngompa13@gmail.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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
	const gchar *default_repos[] = { "fedora",
	                                 "rawhide",
	                                 "updates",
	                                 NULL };

	/* core repos that users shouldn't play with */
	for (guint i = 0; default_repos[i] != NULL; i++) {
		if (g_strcmp0 (id, default_repos[i]) == 0)
			return TRUE;
	}

	return FALSE;
}
