/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib/gstdio.h>
#include <librepo/librepo.h>
#include <pk-backend.h>

#include "hif-package-md.h"
#include "hif-utils.h"

struct GHashTable {
	GHashTable	*data;
};

typedef struct {
	gpointer	 value;
	GDestroyNotify	 destroy_func;
} HifPackageMdObj;

/**
 * hif_package_md_obj_free:
 */
static void
hif_package_md_obj_free (HifPackageMdObj *obj)
{
	if (obj->destroy_func)
		obj->destroy_func (obj->value);
	g_slice_free (HifPackageMdObj, obj);
}

/**
 * hif_package_md_free:
 */
GHashTable *
hif_package_md_new (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) hif_package_md_obj_free);
}

/**
 * hif_package_md_free:
 */
void
hif_package_md_free (GHashTable *hash)
{
	g_hash_table_unref (hash);
}

/**
 * hif_package_md_set_data:
 */
static gchar *
hif_package_format_key (HyPackage pkg, const gchar *key)
{
	return g_strdup_printf ("%s;%s;%s;%s{%s}",
				hy_package_get_name (pkg),
				hy_package_get_evr (pkg),
				hy_package_get_arch (pkg),
				hy_package_get_reponame (pkg),
				key);
}

/**
 * hif_package_md_set_data:
 */
void
hif_package_md_set_data (GHashTable *hash,
			 HyPackage pkg,
			 const gchar *key,
			 gpointer value,
			 GDestroyNotify destroy_func)
{
	HifPackageMdObj *obj;
	obj = g_slice_new0 (HifPackageMdObj);
	obj->value = value;
	obj->destroy_func = destroy_func;
	g_hash_table_insert (hash,
			     hif_package_format_key (pkg, key),
			     obj);
}

/**
 * hif_package_md_get_data:
 */
gpointer
hif_package_md_get_data (GHashTable *hash, HyPackage pkg, const gchar *key)
{
	HifPackageMdObj *obj;
	gchar *actual_key;

	actual_key = hif_package_format_key (pkg, key);
	obj = g_hash_table_lookup (hash, actual_key);
	g_free (actual_key);
	if (obj == NULL)
		return NULL;
	return obj->value;
}
