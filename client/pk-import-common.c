/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <glib.h>

#include <pk-debug.h>
#include "pk-import-common.h"

GPtrArray *
pk_import_get_locale_list (void)
{
	GDir *dir;
	const gchar *name;
	GPtrArray *locale_array;

	locale_array = g_ptr_array_new ();

	dir = g_dir_open (PK_IMPORT_LOCALEDIR, 0, NULL);
	if (dir == NULL) {
		pk_error ("not a valid locale dir!");
	}

	name = g_dir_read_name (dir);
	while (name != NULL) {
		pk_debug ("locale=%s", name);
		name = g_dir_read_name (dir);
		g_ptr_array_add (locale_array, g_strdup (name));
	}
	g_dir_close (dir);
	return locale_array;
}

