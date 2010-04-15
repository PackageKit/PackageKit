/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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
 * SECTION:zif-md-updateinfo
 * @short_description: Updateinfo metadata functionality
 *
 * Provide access to the updateinfo repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-md.h"
#include "zif-md-updateinfo.h"
#include "zif-update.h"
#include "zif-update-info.h"
#include "zif-utils.h"

#include "egg-debug.h"

#define ZIF_MD_UPDATEINFO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_UPDATEINFO, ZifMdUpdateinfoPrivate))

typedef enum {
	ZIF_MD_UPDATEINFO_SECTION_UPDATE,
	ZIF_MD_UPDATEINFO_SECTION_UNKNOWN
} ZifMdUpdateinfoSection;

typedef enum {
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_ID,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_TITLE,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_DESCRIPTION,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_ISSUED,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_REBOOT,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN
} ZifMdUpdateinfoSectionGroup;

typedef enum {
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_PACKAGE,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_FILENAME,
	ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_UNKNOWN
} ZifMdUpdateinfoSectionUpdatePkglistType;

/**
 * ZifMdUpdateinfoPrivate:
 *
 * Private #ZifMdUpdateinfo data
 **/
struct _ZifMdUpdateinfoPrivate
{
	gboolean			 loaded;
	GPtrArray			*array_updates;		/* stored as ZifUpdate */
	/* for parser */
	ZifMdUpdateinfoSection		 section;
	ZifMdUpdateinfoSectionGroup	 section_group;
	ZifMdUpdateinfoSectionUpdatePkglistType section_group_type;
	ZifUpdate			*update_temp;
	ZifUpdateInfo			*update_info_temp;
	ZifPackage			*package_temp;
};

G_DEFINE_TYPE (ZifMdUpdateinfo, zif_md_updateinfo, ZIF_TYPE_MD)

/**
 * zif_md_updateinfo_parser_start_element:
 **/
static void
zif_md_updateinfo_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					const gchar **attribute_names, const gchar **attribute_values,
					gpointer user_data, GError **error)
{
	guint i;
	ZifMdUpdateinfo *updateinfo = user_data;

	g_return_if_fail (ZIF_IS_MD_UPDATEINFO (updateinfo));

	/* group element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UNKNOWN) {

		/* start of list */
		if (g_strcmp0 (element_name, "updates") == 0)
			goto out;

		/* start of update */
		if (g_strcmp0 (element_name, "update") == 0) {
			updateinfo->priv->section = ZIF_MD_UPDATEINFO_SECTION_UPDATE;
			updateinfo->priv->update_temp = zif_update_new ();

			/* find the update type as a bonus */
			for (i=0; attribute_names[i] != NULL; i++) {
				if (g_strcmp0 (attribute_names[i], "status") == 0) {
					zif_update_set_state (updateinfo->priv->update_temp,
							      pk_update_state_enum_from_string (attribute_values[i]));
				}
				if (g_strcmp0 (element_name, "type") == 0) {
					zif_update_set_kind (updateinfo->priv->update_temp,
							     pk_info_enum_from_string (attribute_values[i]));
				}
			}
			goto out;
		}

		egg_warning ("unhandled element: %s", element_name);

		goto out;
	}

	/* update element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UPDATE) {

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN) {
			if (g_strcmp0 (element_name, "release") == 0)
				goto out;
			if (g_strcmp0 (element_name, "id") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_ID;
				goto out;
			}
			if (g_strcmp0 (element_name, "title") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_TITLE;
				goto out;
			}
			if (g_strcmp0 (element_name, "description") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_DESCRIPTION;
				goto out;
			}
			if (g_strcmp0 (element_name, "reboot_suggested") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_REBOOT;
				goto out;
			}
			if (g_strcmp0 (element_name, "issued") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_ISSUED;

				/* find the issued date */
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "date") == 0) {
						zif_update_set_issued (updateinfo->priv->update_temp, attribute_values[i]);
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "references") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES;
				goto out;
			}
			if (g_strcmp0 (element_name, "pkglist") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST;
				goto out;
			}
			egg_warning ("unhandled update base tag: %s", element_name);
			goto out;

		} else if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES) {
			if (g_strcmp0 (element_name, "reference") == 0) {
				updateinfo->priv->update_info_temp = zif_update_info_new ();

				/* find the details about the info */
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "href") == 0) {
						zif_update_info_set_url (updateinfo->priv->update_info_temp,
									 attribute_values[i]);
					}
					if (g_strcmp0 (attribute_names[i], "title") == 0) {
						zif_update_info_set_title (updateinfo->priv->update_info_temp,
									   attribute_values[i]);
					}
					if (g_strcmp0 (attribute_names[i], "type") == 0) {
						zif_update_info_set_kind (updateinfo->priv->update_info_temp,
									  zif_update_info_kind_from_string (attribute_values[i]));
					}
				}

				goto out;
			}

			egg_warning ("unhandled references tag: %s", element_name);
			goto out;

		} else if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST) {
			if (g_strcmp0 (element_name, "collection") == 0)
				goto out;
			if (g_strcmp0 (element_name, "name") == 0)
				goto out;
			if (g_strcmp0 (element_name, "reboot_suggested") == 0)
				goto out;
			//TODO: is this better than src?
			if (g_strcmp0 (element_name, "filename") == 0)
				goto out;

			if (g_strcmp0 (element_name, "package") == 0) {
				const gchar *name = NULL;
				guint epoch = 0;
				const gchar *version = NULL;
				const gchar *release = NULL;
				const gchar *arch = NULL;
				const gchar *src = NULL;
				const gchar *data;
				gchar *package_id;
				ZifString *string;

				updateinfo->priv->package_temp = zif_package_new ();

				/* find the details about the package */
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "name") == 0)
						name = attribute_values[i];
					else if (g_strcmp0 (attribute_names[i], "epoch") == 0)
						epoch = atoi (attribute_values[i]);
					else if (g_strcmp0 (attribute_names[i], "version") == 0)
						version = attribute_values[i];
					else if (g_strcmp0 (attribute_names[i], "release") == 0)
						release = attribute_values[i];
					else if (g_strcmp0 (attribute_names[i], "arch") == 0)
						arch = attribute_values[i];
					else if (g_strcmp0 (attribute_names[i], "src") == 0)
						src = attribute_values[i];
				}

				/* create a package from what we know */
				data = zif_md_get_id (ZIF_MD (updateinfo));
				package_id = zif_package_id_from_nevra (name, epoch, version, release, arch, data);
				zif_package_set_id (updateinfo->priv->package_temp, package_id);
				string = zif_string_new (src);
				zif_package_set_location_href (updateinfo->priv->package_temp, string);
				g_free (package_id);
				zif_string_unref (string);
				goto out;
			}

			egg_warning ("unexpected pklist tag: %s", element_name);
		}

		egg_warning ("unexpected update tag: %s", element_name);
	}

	egg_warning ("unhandled base tag: %s", element_name);

out:
	return;
}

/**
 * zif_md_updateinfo_parser_end_element:
 **/
static void
zif_md_updateinfo_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				      gpointer user_data, GError **error)
{
	ZifMdUpdateinfo *updateinfo = user_data;

	/* no element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UNKNOWN) {

		/* end of list */
		if (g_strcmp0 (element_name, "updates") == 0)
			goto out;

		egg_warning ("unhandled base end tag: %s", element_name);
	}

	/* update element */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UPDATE) {

		/* end of update */
		if (g_strcmp0 (element_name, "update") == 0) {
			updateinfo->priv->section = ZIF_MD_UPDATEINFO_SECTION_UNKNOWN;

			/* add to array */
			g_ptr_array_add (updateinfo->priv->array_updates, updateinfo->priv->update_temp);
			updateinfo->priv->update_temp = NULL;
			goto out;
		}

		if (g_strcmp0 (element_name, "id") == 0 ||
		    g_strcmp0 (element_name, "title") == 0 ||
		    g_strcmp0 (element_name, "release") == 0 ||
		    g_strcmp0 (element_name, "description") == 0 ||
		    g_strcmp0 (element_name, "issued") == 0) {
			updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
			goto out;
		}

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_REBOOT) {

			/* add property */
			if (g_strcmp0 (element_name, "reboot_suggested") == 0) {
				zif_update_set_reboot (updateinfo->priv->update_temp, TRUE);
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
				goto out;
			}
			egg_warning ("unhandled reboot_suggested end tag: %s", element_name);
			goto out;
		}

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_REFERENCES) {

			if (g_strcmp0 (element_name, "references") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
				goto out;
			}

			if (g_strcmp0 (element_name, "reference") == 0) {
				zif_update_add_update_info (updateinfo->priv->update_temp,
							    updateinfo->priv->update_info_temp);
				g_object_unref (updateinfo->priv->update_info_temp);
				updateinfo->priv->update_info_temp = NULL;
				goto out;
			}
			egg_warning ("unhandled references end tag: %s", element_name);
			goto out;
		}

		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST) {

			if (g_strcmp0 (element_name, "pkglist") == 0) {
				updateinfo->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
				goto out;
			}

			if (g_strcmp0 (element_name, "name") == 0)
				goto out;
			if (g_strcmp0 (element_name, "filename") == 0)
				goto out;
			if (g_strcmp0 (element_name, "collection") == 0)
				goto out;
			if (g_strcmp0 (element_name, "reboot_suggested") == 0)
				goto out;

			/* add to the update */
			if (g_strcmp0 (element_name, "package") == 0) {
				zif_update_add_package (updateinfo->priv->update_temp,
							updateinfo->priv->package_temp);
				g_object_unref (updateinfo->priv->package_temp);
				updateinfo->priv->package_temp = NULL;
				goto out;
			}

			egg_warning ("unhandled pkglist end tag: %s", element_name);
		}

		egg_warning ("unhandled update end tag: %s", element_name);
		goto out;
	}

	egg_warning ("unhandled end tag: %s", element_name);
out:
	return;
}

/**
 * zif_md_updateinfo_parser_text:
 **/
static void
zif_md_updateinfo_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			       gpointer user_data, GError **error)

{
	ZifMdUpdateinfo *updateinfo = user_data;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (updateinfo->priv->section == ZIF_MD_UPDATEINFO_SECTION_UPDATE) {
		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_ID) {
			zif_update_set_id (updateinfo->priv->update_temp, text);
			goto out;
		}
		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_TITLE) {
			zif_update_set_title (updateinfo->priv->update_temp, text);
			goto out;
		}
		if (updateinfo->priv->section_group == ZIF_MD_UPDATEINFO_SECTION_UPDATE_DESCRIPTION) {
			zif_update_set_description (updateinfo->priv->update_temp, text);
			goto out;
		}
		goto out;
	}
out:
	return;
}

/**
 * zif_md_updateinfo_unload:
 **/
static gboolean
zif_md_updateinfo_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_updateinfo_load:
 **/
static gboolean
zif_md_updateinfo_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_updateinfo_markup_parser = {
		zif_md_updateinfo_parser_start_element,
		zif_md_updateinfo_parser_end_element,
		zif_md_updateinfo_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifMdUpdateinfo *updateinfo = ZIF_MD_UPDATEINFO (md);

	g_return_val_if_fail (ZIF_IS_MD_UPDATEINFO (md), FALSE);

	/* already loaded */
	if (updateinfo->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for updateinfo");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);

	/* get repo contents */
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_md_updateinfo_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, updateinfo, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	updateinfo->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_md_updateinfo_get_detail:
 * @md: the #ZifMdUpdateinfo object
 * @cancellable: the %GCancellable, or %NULL
 * @completion: the %ZifCompletion object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets all the available update data.
 *
 * Return value: #GPtrArray of #ZifUpdate's, free with g_ptr_array_unref()
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_updateinfo_get_detail (ZifMdUpdateinfo *md,
			      GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_MD_UPDATEINFO (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get load updateinfo: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	array = g_ptr_array_ref (md->priv->array_updates);
out:
	return array;
}

/**
 * zif_md_updateinfo_get_detail_for_package:
 * @md: the #ZifMdUpdateinfo object
 * @package_id: the group to search for
 * @cancellable: the %GCancellable, or %NULL
 * @completion: the %ZifCompletion object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Gets the list of update details for the package_id.
 *
 * Return value: #GPtrArray of #ZifUpdate's, free with g_ptr_array_unref()
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_updateinfo_get_detail_for_package (ZifMdUpdateinfo *md, const gchar *package_id,
					  GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *array_tmp;
	guint i;
	guint j;
	guint len;
	gboolean ret;
	GError *error_local = NULL;
	ZifUpdate *update;
	ZifPackage *package;

	g_return_val_if_fail (ZIF_IS_MD_UPDATEINFO (md), NULL);
	g_return_val_if_fail (package_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!md->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get load updateinfo: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get packages in this group */
	len = md->priv->array_updates->len;
	for (i=0; i<len; i++) {
		update = g_ptr_array_index (md->priv->array_updates, i);

		/* have we matched on any entries */
		ret = FALSE;

		array_tmp = zif_update_get_packages (update);
		for (j=0; j<array_tmp->len; j++) {
			package = g_ptr_array_index (array_tmp, j);
			if (g_strcmp0 (zif_package_get_id (package), package_id) == 0) {
				ret = TRUE;
				break;
			}
		}

		/* we found a package match */
		if (ret) {
			if (array == NULL)
				array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			g_ptr_array_add (array, g_object_ref (update));
		}
	}

	/* nothing found */
	if (array == NULL) {
		g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
			     "could not find package (%i in sack): %s", len, package_id);
	}
out:
	return array;
}

/**
 * zif_md_updateinfo_finalize:
 **/
static void
zif_md_updateinfo_finalize (GObject *object)
{
	ZifMdUpdateinfo *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_UPDATEINFO (object));
	md = ZIF_MD_UPDATEINFO (object);

	g_ptr_array_unref (md->priv->array_updates);

	G_OBJECT_CLASS (zif_md_updateinfo_parent_class)->finalize (object);
}

/**
 * zif_md_updateinfo_class_init:
 **/
static void
zif_md_updateinfo_class_init (ZifMdUpdateinfoClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_updateinfo_finalize;

	/* map */
	md_class->load = zif_md_updateinfo_load;
	md_class->unload = zif_md_updateinfo_unload;
	g_type_class_add_private (klass, sizeof (ZifMdUpdateinfoPrivate));
}

/**
 * zif_md_updateinfo_init:
 **/
static void
zif_md_updateinfo_init (ZifMdUpdateinfo *md)
{
	md->priv = ZIF_MD_UPDATEINFO_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_UPDATEINFO_SECTION_UNKNOWN;
	md->priv->section_group = ZIF_MD_UPDATEINFO_SECTION_UPDATE_UNKNOWN;
	md->priv->section_group_type = ZIF_MD_UPDATEINFO_SECTION_UPDATE_PKGLIST_UNKNOWN;
	md->priv->update_temp = NULL;
	md->priv->update_info_temp = NULL;
	md->priv->package_temp = NULL;
	md->priv->array_updates = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_md_updateinfo_new:
 *
 * Return value: A new #ZifMdUpdateinfo class instance.
 *
 * Since: 0.0.1
 **/
ZifMdUpdateinfo *
zif_md_updateinfo_new (void)
{
	ZifMdUpdateinfo *md;
	md = g_object_new (ZIF_TYPE_MD_UPDATEINFO, NULL);
	return ZIF_MD_UPDATEINFO (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_updateinfo_test (EggTest *test)
{
	ZifMdUpdateinfo *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *id;
	GCancellable *cancellable;
	ZifCompletion *completion;
	ZifUpdate *update;

	if (!egg_test_start (test, "ZifMdUpdateinfo"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_updateinfo md");
	md = zif_md_updateinfo_new ();
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
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_UPDATEINFO);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/updateinfo.xml");
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
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "4fa3657a79af078c588e2ab181ab0a3a156c6008a084d85edccaf6c57d67d47d");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "get categories");
	array = zif_md_updateinfo_get_detail_for_package (md, "device-mapper-libs;1.02.27-7.fc10;ppc64;fedora", cancellable, completion, &error);
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
	update = g_ptr_array_index (array, 0);
	if (g_strcmp0 (zif_update_get_id (update), "FEDORA-2008-9969") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct id '%s'", zif_update_get_id (update));

	/************************************************************/
	egg_test_title (test, "correct title value");
	update = g_ptr_array_index (array, 0);
	if (g_strcmp0 (zif_update_get_title (update), "lvm2-2.02.39-7.fc10") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct title '%s'", zif_update_get_title (update));

	/************************************************************/
	egg_test_title (test, "correct description value");
	update = g_ptr_array_index (array, 0);
	if (g_strcmp0 (zif_update_get_description (update), "Fix an incorrect path that prevents the clvmd init script from working and include licence files with the sub-packages.") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct id '%s'", zif_update_get_description (update));

	g_ptr_array_unref (array);
	g_object_unref (md);
	g_object_unref (cancellable);
	g_object_unref (completion);

	egg_test_end (test);
}
#endif

