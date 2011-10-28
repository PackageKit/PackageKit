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

#include <gio/gio.h>
#include <string.h>

#include "pk-backend-groups.h"

static GHashTable *grps = NULL;
static PkBitfield groups = 0;

static GHashTable *
group_map_new (GError **error)
{
	GHashTable *map;
	GFile *file;

	GFileInputStream *is;
	GDataInputStream *input;

	GError *e = NULL;

	g_debug ("reading group map from %s", PK_BACKEND_GROUP_FILE);
	file = g_file_new_for_path (PK_BACKEND_GROUP_FILE);
	is = g_file_read (file, NULL, &e);

	if (is == NULL) {
		g_object_unref (file);
		g_propagate_error (error, e);
		return NULL;
	}

	map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	input = g_data_input_stream_new (G_INPUT_STREAM (is));

	/* read groups line by line, ignoring comments */
	while (TRUE) {
		PkGroupEnum group;
		gchar *key, *value;

		value = g_data_input_stream_read_line (input, NULL, NULL, &e);

		if (value != NULL) {
			g_strstrip (value);
		} else {
			break;
		}

		if (*value == '\0' || *value == '#') {
			g_free (value);
			continue;
		}

		/* line format: grp (space|tab)+ group */
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
			/* key and value are allocated together */
			g_hash_table_replace (map, key, value);
			pk_bitfield_add (groups, group);
		}
	}

	g_object_unref (input);
	g_object_unref (is);
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
pk_backend_initialize_groups (PkBackend *self, GError **error)
{
	g_return_val_if_fail (self != NULL, FALSE);

	grps = group_map_new (error);

	return (grps != NULL);
}

void
pk_backend_destroy_groups (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	if (grps != NULL) {
		g_hash_table_unref (grps);
	}
}

const gchar *
alpm_pkg_get_group (alpm_pkg_t *pkg)
{
	const alpm_list_t *i;

	g_return_val_if_fail (pkg != NULL, NULL);
	g_return_val_if_fail (grps != NULL, NULL);

	/* use the first group that we recognise */
	for (i = alpm_pkg_get_groups (pkg); i != NULL; i = i->next) {
		gpointer value = g_hash_table_lookup (grps, i->data);

		if (value != NULL) {
			return (const gchar *) value;
		}
	}

	return "other";
}

PkBitfield
pk_backend_get_groups (PkBackend *self)
{
	g_return_val_if_fail (self != NULL, 0);

	return groups;
}
