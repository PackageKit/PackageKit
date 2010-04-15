/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:zif-md-comps
 * @short_description: Comps metadata functionality
 *
 * Provide access to the comps repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-md.h"
#include "zif-md-comps.h"

#include "egg-debug.h"

#define ZIF_MD_COMPS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_COMPS, ZifMdCompsPrivate))

typedef enum {
	ZIF_MD_COMPS_SECTION_GROUP,
	ZIF_MD_COMPS_SECTION_CATEGORY,
	ZIF_MD_COMPS_SECTION_UNKNOWN
} ZifMdCompsSection;

typedef enum {
	ZIF_MD_COMPS_SECTION_GROUP_ID,
	ZIF_MD_COMPS_SECTION_GROUP_NAME,
	ZIF_MD_COMPS_SECTION_GROUP_DESCRIPTION,
	ZIF_MD_COMPS_SECTION_GROUP_VISIBLE,
	ZIF_MD_COMPS_SECTION_GROUP_PACKAGELIST,
	ZIF_MD_COMPS_SECTION_GROUP_PACKAGE,
	ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN
} ZifMdCompsSectionGroup;

typedef enum {
	ZIF_MD_COMPS_SECTION_GROUP_TYPE_DEFAULT,
	ZIF_MD_COMPS_SECTION_GROUP_TYPE_OPTIONAL,
	ZIF_MD_COMPS_SECTION_GROUP_TYPE_CONDITIONAL,
	ZIF_MD_COMPS_SECTION_GROUP_TYPE_UNKNOWN
} ZifMdCompsSectionGroupType;

typedef enum {
	ZIF_MD_COMPS_SECTION_CATEGORY_ID,
	ZIF_MD_COMPS_SECTION_CATEGORY_NAME,
	ZIF_MD_COMPS_SECTION_CATEGORY_DESCRIPTION,
	ZIF_MD_COMPS_SECTION_CATEGORY_GROUPLIST,
	ZIF_MD_COMPS_SECTION_CATEGORY_GROUP,
	ZIF_MD_COMPS_SECTION_CATEGORY_UNKNOWN
} ZifMdCompsSectionCategory;

typedef struct {
	gchar				*id;
	gchar				*name;
	gchar				*description;
	gboolean			 visible;
	GPtrArray			*packagelist;		/* stored as gchar */
} ZifMdCompsGroupData;

typedef struct {
	gchar				*id;
	gchar				*name;
	gchar				*description;
	GPtrArray			*grouplist;		/* stored as gchar */
} ZifMdCompsCategoryData;

/**
 * ZifMdCompsPrivate:
 *
 * Private #ZifMdComps data
 **/
struct _ZifMdCompsPrivate
{
	gboolean			 loaded;
	GPtrArray			*array_groups;		/* stored as ZifMdCompsGroupData */
	GPtrArray			*array_categories;	/* stored as ZifMdCompsCategoryData */
	/* for parser */
	ZifMdCompsSection		 section;
	ZifMdCompsSectionGroup		 section_group;
	ZifMdCompsSectionGroupType	 section_group_type;
	ZifMdCompsSectionCategory	 section_category;
	ZifMdCompsGroupData		*group_data_temp;
	ZifMdCompsCategoryData		*category_data_temp;
};

G_DEFINE_TYPE (ZifMdComps, zif_md_comps, ZIF_TYPE_MD)

/**
 * zif_md_comps_group_data_new:
 **/
static ZifMdCompsGroupData *
zif_md_comps_group_data_new (void)
{
	ZifMdCompsGroupData *data;
	data = g_new0 (ZifMdCompsGroupData, 1);
	data->packagelist = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	return data;
}

/**
 * zif_md_comps_category_data_new:
 **/
static ZifMdCompsCategoryData *
zif_md_comps_category_data_new (void)
{
	ZifMdCompsCategoryData *data;
	data = g_new0 (ZifMdCompsCategoryData, 1);
	data->grouplist = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	return data;
}

/**
 * zif_md_comps_group_data_free:
 **/
static void
zif_md_comps_group_data_free (ZifMdCompsGroupData *data)
{
	g_free (data->id);
	g_free (data->name);
	g_free (data->description);
	g_ptr_array_unref (data->packagelist);
	g_free (data);
}

/**
 * zif_md_comps_category_data_free:
 **/
static void
zif_md_comps_category_data_free (ZifMdCompsCategoryData *data)
{
	g_free (data->id);
	g_free (data->name);
	g_free (data->description);
	g_ptr_array_unref (data->grouplist);
	g_free (data);
}

/**
 * zif_md_comps_parser_start_element:
 **/
static void
zif_md_comps_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				   const gchar **attribute_names, const gchar **attribute_values,
				   gpointer user_data, GError **error)
{
	guint i;
	ZifMdComps *comps = user_data;

	g_return_if_fail (ZIF_IS_MD_COMPS (comps));

	/* group element */
	if (comps->priv->section == ZIF_MD_COMPS_SECTION_UNKNOWN) {

		/* start of group */
		if (g_strcmp0 (element_name, "group") == 0) {
			comps->priv->section = ZIF_MD_COMPS_SECTION_GROUP;
			comps->priv->group_data_temp = zif_md_comps_group_data_new ();
			goto out;
		}

		/* start of category */
		if (g_strcmp0 (element_name, "category") == 0) {
			comps->priv->section = ZIF_MD_COMPS_SECTION_CATEGORY;
			comps->priv->category_data_temp = zif_md_comps_category_data_new ();
			goto out;
		}

		goto out;
	}

	/* group element */
	if (comps->priv->section == ZIF_MD_COMPS_SECTION_GROUP) {
		/* id */
		if (g_strcmp0 (element_name, "id") == 0) {
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_ID;
			goto out;
		}
		if (g_strcmp0 (element_name, "name") == 0) {
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_NAME;
			goto out;
		}
		if (g_strcmp0 (element_name, "description") == 0) {
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_DESCRIPTION;
			goto out;
		}
		if (g_strcmp0 (element_name, "uservisible") == 0) {
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_VISIBLE;
			goto out;
		}
		if (g_strcmp0 (element_name, "packagelist") == 0) {
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_PACKAGELIST;
			goto out;
		}
		if (g_strcmp0 (element_name, "packagereq") == 0) {
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_PACKAGE;

			/* find the package type as a bonus */
			comps->priv->section_group_type = ZIF_MD_COMPS_SECTION_GROUP_TYPE_UNKNOWN;
			for (i=0; attribute_names[i] != NULL; i++) {
				if (g_strcmp0 (element_name, "type") == 0) {
					if (g_strcmp0 (attribute_values[i], "default"))
						comps->priv->section_group_type = ZIF_MD_COMPS_SECTION_GROUP_TYPE_DEFAULT;
					break;
				}
			}
			goto out;
		}
	}

	/* category element */
	if (comps->priv->section == ZIF_MD_COMPS_SECTION_CATEGORY) {
		/* id */
		if (g_strcmp0 (element_name, "id") == 0) {
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_ID;
			goto out;
		}
		if (g_strcmp0 (element_name, "name") == 0) {
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_NAME;
			goto out;
		}
		if (g_strcmp0 (element_name, "description") == 0) {
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_DESCRIPTION;
			goto out;
		}
		if (g_strcmp0 (element_name, "grouplist") == 0) {
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_GROUPLIST;
			goto out;
		}
		if (g_strcmp0 (element_name, "groupid") == 0) {
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_GROUP;
			goto out;
		}
	}
out:
	return;
}

/**
 * zif_md_comps_parser_end_element:
 **/
static void
zif_md_comps_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				 gpointer user_data, GError **error)
{
	ZifMdComps *comps = user_data;

	/* end of group */
	if (g_strcmp0 (element_name, "group") == 0) {
		comps->priv->section = ZIF_MD_COMPS_SECTION_UNKNOWN;

		/* add to array */
		g_ptr_array_add (comps->priv->array_groups, comps->priv->group_data_temp);

		if (FALSE)
		egg_debug ("added GROUP '%s' name:%s, desc:%s, visible:%i, list=%p",
			   comps->priv->group_data_temp->id,
			   comps->priv->group_data_temp->name,
			   comps->priv->group_data_temp->description,
			   comps->priv->group_data_temp->visible,
			   comps->priv->group_data_temp->packagelist);

		comps->priv->group_data_temp = NULL;
		goto out;
	}

	/* start of group */
	if (g_strcmp0 (element_name, "category") == 0) {
		comps->priv->section = ZIF_MD_COMPS_SECTION_UNKNOWN;

		/* add to array */
		g_ptr_array_add (comps->priv->array_categories, comps->priv->category_data_temp);

		if (FALSE)
		egg_debug ("added CATEGORY '%s' name:%s, desc:%s, list=%p",
			   comps->priv->category_data_temp->id,
			   comps->priv->category_data_temp->name,
			   comps->priv->category_data_temp->description,
			   comps->priv->category_data_temp->grouplist);

		comps->priv->category_data_temp = NULL;
		goto out;
	}
out:
	return;
}

/**
 * zif_md_comps_parser_text:
 **/
static void
zif_md_comps_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			  gpointer user_data, GError **error)

{
	ZifMdComps *comps = user_data;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (comps->priv->section == ZIF_MD_COMPS_SECTION_GROUP) {
		if (comps->priv->section_group == ZIF_MD_COMPS_SECTION_GROUP_ID) {
			comps->priv->group_data_temp->id = g_strdup (text);
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_group == ZIF_MD_COMPS_SECTION_GROUP_NAME) {
			/* ignore translated versions for now */
			if (comps->priv->group_data_temp->name != NULL)
				goto out;
			comps->priv->group_data_temp->name = g_strdup (text);
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_group == ZIF_MD_COMPS_SECTION_GROUP_DESCRIPTION) {
			/* ignore translated versions for now */
			if (comps->priv->group_data_temp->description != NULL)
				goto out;
			comps->priv->group_data_temp->description = g_strdup (text);
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_group == ZIF_MD_COMPS_SECTION_GROUP_VISIBLE) {
			/* TODO: parse true and false */
			comps->priv->group_data_temp->visible = atoi (text);
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_group == ZIF_MD_COMPS_SECTION_GROUP_PACKAGE) {
			g_ptr_array_add (comps->priv->group_data_temp->packagelist, g_strdup (text));
			comps->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN;
			goto out;
		}
		goto out;
	}

	/* category section */
	if (comps->priv->section == ZIF_MD_COMPS_SECTION_CATEGORY) {
		if (comps->priv->section_category == ZIF_MD_COMPS_SECTION_CATEGORY_ID) {
			comps->priv->category_data_temp->id = g_strdup (text);
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_category == ZIF_MD_COMPS_SECTION_CATEGORY_NAME) {
			/* ignore translated versions for now */
			if (comps->priv->category_data_temp->name != NULL)
				goto out;
			comps->priv->category_data_temp->name = g_strdup (text);
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_category == ZIF_MD_COMPS_SECTION_CATEGORY_DESCRIPTION) {
			/* ignore translated versions for now */
			if (comps->priv->category_data_temp->description != NULL)
				goto out;
			comps->priv->category_data_temp->description = g_strdup (text);
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_UNKNOWN;
			goto out;
		}
		if (comps->priv->section_category == ZIF_MD_COMPS_SECTION_CATEGORY_GROUP) {
			g_ptr_array_add (comps->priv->category_data_temp->grouplist, g_strdup (text));
			comps->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_UNKNOWN;
			goto out;
		}
		goto out;
	}
out:
	return;
}

/**
 * zif_md_comps_unload:
 **/
static gboolean
zif_md_comps_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_comps_load:
 **/
static gboolean
zif_md_comps_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_comps_markup_parser = {
		zif_md_comps_parser_start_element,
		zif_md_comps_parser_end_element,
		zif_md_comps_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifMdComps *comps = ZIF_MD_COMPS (md);

	g_return_val_if_fail (ZIF_IS_MD_COMPS (md), FALSE);

	/* already loaded */
	if (comps->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for comps");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);

	/* get repo contents */
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_md_comps_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, comps, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	comps->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_md_comps_category_set_icon:
 *
 * Check the icon exists, otherwise fallback to the parent ID, and then
 * something sane.
 **/
static void
zif_md_comps_category_set_icon (PkCategory *category)
{
	const gchar *icon;
	GString *filename = g_string_new ("");

	/* try the proper group icon */
	icon = pk_category_get_id (category);
	g_string_printf (filename, "/usr/share/pixmaps/comps/%s.png", icon);
	if (g_file_test (filename->str, G_FILE_TEST_EXISTS))
		goto out;

	/* fall back to parent icon */
	icon = pk_category_get_parent_id (category);
	g_string_printf (filename, "/usr/share/pixmaps/comps/%s.png", icon);
	if (g_file_test (filename->str, G_FILE_TEST_EXISTS))
		goto out;

	/* fall back to the missing icon */
	icon = "image-missing";
out:
	pk_category_set_icon (category, icon);
	g_string_free (filename, TRUE);
}

/**
 * zif_md_comps_get_categories:
 * @md: the #ZifMdComps object
 * @cancellable: the %GCancellable, or %NULL
 * @completion: the %ZifCompletion object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the available list of categories.
 *
 * Return value: %PkCategory array of categories, with parent_id set to %NULL
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_comps_get_categories (ZifMdComps *md, GCancellable *cancellable,
			     ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	guint i;
	guint len;
	gboolean ret;
	GError *error_local = NULL;
	const ZifMdCompsCategoryData *data;
	PkCategory *category;

	g_return_val_if_fail (ZIF_IS_MD_COMPS (md), NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get load comps: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get categories */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	len = md->priv->array_categories->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (md->priv->array_categories, i);
		category = pk_category_new ();
		pk_category_set_id (category, data->id);
		pk_category_set_name (category, data->name);
		pk_category_set_summary (category, data->description);
		zif_md_comps_category_set_icon (category);
		g_ptr_array_add (array, category);
	}
out:
	return array;
}

/**
 * zif_md_comps_get_category_for_group:
 **/
static PkCategory *
zif_md_comps_get_category_for_group (ZifMdComps *md, const gchar *group_id)
{
	guint i;
	guint len;
	PkCategory *category = NULL;
	ZifMdCompsGroupData *data;

	/* find group matching group_id */
	len = md->priv->array_groups->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (md->priv->array_groups, i);
		if (g_strcmp0 (group_id, data->id) == 0) {
			category = pk_category_new ();
			pk_category_set_id (category, data->id);
			pk_category_set_name (category, data->name);
			pk_category_set_summary (category, data->description);
			break;
		}
	}
	return category;
}

/**
 * zif_md_comps_get_groups_for_category:
 * @md: the #ZifMdComps object
 * @category_id: the category to search for
 * @cancellable: the %GCancellable, or %NULL
 * @completion: the %ZifCompletion object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the list of groups for a specific category.
 *
 * Return value: %PkCategory array of groups
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_comps_get_groups_for_category (ZifMdComps *md, const gchar *category_id,
				      GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	guint i;
	guint j;
	guint len;
	gboolean ret;
	GError *error_local = NULL;
	const ZifMdCompsCategoryData *data;
	const gchar *id;
	PkCategory *category;

	g_return_val_if_fail (ZIF_IS_MD_COMPS (md), NULL);
	g_return_val_if_fail (category_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get load comps: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get categories */
	len = md->priv->array_categories->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (md->priv->array_categories, i);

		/* category matches */
		if (g_strcmp0 (category_id, data->id) == 0) {
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			for (j=0; j<data->grouplist->len; j++) {
				id = g_ptr_array_index (data->grouplist, j);
				/* find group matching group_id */
				category = zif_md_comps_get_category_for_group (md, id);
				if (category == NULL)
					continue;

				/* add */
				pk_category_set_parent_id (category, category_id);
				zif_md_comps_category_set_icon (category);
				g_ptr_array_add (array, category);
			}
			break;
		}
	}

	/* nothing found */
	if (array == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "could not find category: %s", category_id);
	}
out:
	return array;
}

/**
 * zif_md_comps_get_packages_for_group:
 * @md: the #ZifMdComps object
 * @group_id: the group to search for
 * @cancellable: the %GCancellable, or %NULL
 * @completion: the %ZifCompletion object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the package names for a group.
 *
 * Return value: gchar pointer array of package names (not %ZifPackage's)
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_comps_get_packages_for_group (ZifMdComps *md, const gchar *group_id,
				     GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	guint i;
	guint j;
	guint len;
	gboolean ret;
	GError *error_local = NULL;
	const ZifMdCompsGroupData *data;
	const gchar *packagename;

	g_return_val_if_fail (ZIF_IS_MD_COMPS (md), NULL);
	g_return_val_if_fail (group_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get load comps: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get packages in this group */
	len = md->priv->array_groups->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (md->priv->array_groups, i);

		/* category matches */
		if (g_strcmp0 (group_id, data->id) == 0) {
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
			for (j=0; j<data->packagelist->len; j++) {
				packagename = g_ptr_array_index (data->packagelist, j);
				g_ptr_array_add (array, g_strdup (packagename));
			}
			break;
		}
	}

	/* nothing found */
	if (array == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "could not find group: %s", group_id);
	}
out:
	return array;
}

/**
 * zif_md_comps_finalize:
 **/
static void
zif_md_comps_finalize (GObject *object)
{
	ZifMdComps *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_COMPS (object));
	md = ZIF_MD_COMPS (object);

	g_ptr_array_unref (md->priv->array_groups);
	g_ptr_array_unref (md->priv->array_categories);

	G_OBJECT_CLASS (zif_md_comps_parent_class)->finalize (object);
}

/**
 * zif_md_comps_class_init:
 **/
static void
zif_md_comps_class_init (ZifMdCompsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_comps_finalize;

	/* map */
	md_class->load = zif_md_comps_load;
	md_class->unload = zif_md_comps_unload;
	g_type_class_add_private (klass, sizeof (ZifMdCompsPrivate));
}

/**
 * zif_md_comps_init:
 **/
static void
zif_md_comps_init (ZifMdComps *md)
{
	md->priv = ZIF_MD_COMPS_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_COMPS_SECTION_UNKNOWN;
	md->priv->section_group = ZIF_MD_COMPS_SECTION_GROUP_UNKNOWN;
	md->priv->section_group_type = ZIF_MD_COMPS_SECTION_GROUP_TYPE_UNKNOWN;
	md->priv->section_category = ZIF_MD_COMPS_SECTION_CATEGORY_UNKNOWN;
	md->priv->group_data_temp = NULL;
	md->priv->category_data_temp = NULL;
	md->priv->array_groups = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_md_comps_group_data_free);
	md->priv->array_categories = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_md_comps_category_data_free);
}

/**
 * zif_md_comps_new:
 *
 * Return value: A new #ZifMdComps class instance.
 *
 * Since: 0.0.1
 **/
ZifMdComps *
zif_md_comps_new (void)
{
	ZifMdComps *md;
	md = g_object_new (ZIF_TYPE_MD_COMPS, NULL);
	return ZIF_MD_COMPS (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_comps_test (EggTest *test)
{
	ZifMdComps *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *id;
	GCancellable *cancellable;
	ZifCompletion *completion;
	PkCategory *category;

	if (!egg_test_start (test, "ZifMdComps"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_comps md");
	md = zif_md_comps_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_md_set_id (ZIF_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set type");
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_COMPS);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/comps-rawhide.xml");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum type");
	ret = zif_md_set_checksum_type (ZIF_MD (md), G_CHECKSUM_SHA256);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "14f17b894303b4dc9683511104848f75d98cea8f76c107bf25e1b4db5741f6a8");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "get categories");
	array = zif_md_comps_get_categories (md, cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get categories '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %i", array->len);

	/************************************************************/
	egg_test_title (test, "correct id value");
	category = g_ptr_array_index (array, 0);
	if (g_strcmp0 (pk_category_get_id (category), "apps") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct id '%s'", pk_category_get_id (category));

	/************************************************************/
	egg_test_title (test, "correct name value");
	category = g_ptr_array_index (array, 0);
	if (g_strcmp0 (pk_category_get_name (category), "Applications") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct name '%s'", pk_category_get_name (category));

	/************************************************************/
	egg_test_title (test, "correct summary value");
	category = g_ptr_array_index (array, 0);
	if (g_strcmp0 (pk_category_get_summary (category), "Applications to perform a variety of tasks") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct summary '%s'", pk_category_get_summary (category));

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "get groups for category");
	array = zif_md_comps_get_groups_for_category (md, "apps", cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get categories '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %i", array->len);

	/************************************************************/
	egg_test_title (test, "correct id value");
	category = g_ptr_array_index (array, 0);
	if (g_strcmp0 (pk_category_get_id (category), "admin-tools") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct id '%s'", pk_category_get_id (category));
	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "get packages for group");
	array = zif_md_comps_get_packages_for_group (md, "admin-tools", cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get categories '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 2)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %i", array->len);

	/************************************************************/
	egg_test_title (test, "correct value");
	id = g_ptr_array_index (array, 0);
	if (g_strcmp0 (id, "gnome-packagekit") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct id '%s'", id);
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (cancellable);
	g_object_unref (completion);

	egg_test_end (test);
}
#endif

