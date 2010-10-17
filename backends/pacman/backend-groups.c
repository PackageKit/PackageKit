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

#include <string.h>
#include <gio/gio.h>
#include "backend-error.h"
#include "backend-groups.h"

static GHashTable *group_map = NULL;
static PkBitfield groups = 0;

static GHashTable *
group_map_new (GError **error)
{
	GHashTable *map;
	GFile *file;

	GFileInputStream *file_stream;
	GDataInputStream *data_stream;

	gchar *key, *value;
	GError *e = NULL;

	g_debug ("pacman: reading groups from %s", PACMAN_GROUP_LIST);
	file = g_file_new_for_path (PACMAN_GROUP_LIST);
	file_stream = g_file_read (file, NULL, &e);

	if (file_stream == NULL) {
		g_object_unref (file);
		g_propagate_error (error, e);
		return NULL;
	}

	map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	data_stream = g_data_input_stream_new (G_INPUT_STREAM (file_stream));

	/* read groups line by line, ignoring comments */
	while ((value = g_data_input_stream_read_line (data_stream, NULL, NULL, &e)) != NULL) {
		PkGroupEnum group;

		g_strstrip (value);
		if (*value == '\0' || *value == '#') {
			g_free (value);
			continue;
		}

		/* line format: alpm-group (space|tab)+ packagekit-group */
		key = strsep (&value, " 	");
		g_strchomp (key);

		if (value == NULL) {
			/* safe to cast as it is never freed or modified */
			value = (gchar *) "other";
			group = PK_GROUP_ENUM_OTHER;
		} else {
			g_strchug (value);
			group = pk_group_enum_from_string (value);
		}

		if (group != PK_GROUP_ENUM_UNKNOWN) {
			/* use replace because key and value are allocated together */
			g_hash_table_replace (map, key, value);
			pk_bitfield_add (groups, group);
		}
	}

	g_object_unref (data_stream);
	g_object_unref (file_stream);
	g_object_unref (file);

	if (e != NULL) {
		g_hash_table_unref (map);
		g_propagate_error (error, e);
		return NULL;
	} else {
		return map;
	}
}

gboolean
backend_initialize_groups (PkBackend *backend, GError **error)
{
	g_return_val_if_fail (backend != NULL, FALSE);

	group_map = group_map_new (error);
	if (group_map == NULL) {
		return FALSE;
	}

	return TRUE;
}

void
backend_destroy_groups (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	if (group_map != NULL) {
		g_hash_table_unref (group_map);
	}
}

const gchar *
pacman_package_get_group (PacmanPackage *package)
{
	const PacmanList *list;

	g_return_val_if_fail (group_map != NULL, NULL);
	g_return_val_if_fail (package != NULL, NULL);

	/* use the first group that we recognise */
	for (list = pacman_package_get_groups (package); list != NULL; list = pacman_list_next (list)) {
		gpointer value = g_hash_table_lookup (group_map, pacman_list_get (list));
		if (value != NULL) {
			return (const gchar *) value;
		}
	}

	return "other";
}

/**
 * backend_get_groups:
 **/
PkBitfield backend_get_groups (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, 0);

	return groups;
}
