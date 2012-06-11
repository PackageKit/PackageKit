/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#include <config.h>
#include <pk-plugin.h>

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "Remove the prepared update notifier if a transaction is "
		"done that could modify the validity";
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
	PkRoleEnum role;

	/* just delete the file, no questions asked :) */
	role = pk_transaction_get_role (transaction);
	if (role == PK_ROLE_ENUM_REFRESH_CACHE ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM ||
	    role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		unlink ("/system-update");
	}
}
