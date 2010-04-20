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
 * SECTION:zif-md-filelists-xml
 * @short_description: FilelistsXml metadata functionality
 *
 * Provide access to the filelists_xml repo metadata.
 * This object is a subclass of #ZifMd
 */

typedef enum {
	ZIF_MD_FILELISTS_XML_SECTION_LIST,
	ZIF_MD_FILELISTS_XML_SECTION_UNKNOWN
} ZifMdFilelistsXmlSection;

typedef enum {
	ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE,
	ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN
} ZifMdFilelistsXmlSectionList;

typedef enum {
	ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_FILE,
	ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_UNKNOWN
} ZifMdFilelistsXmlSectionListPackage;

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "zif-md.h"
#include "zif-md-filelists-xml.h"
#include "zif-package-remote.h"

#include "egg-debug.h"

#define ZIF_MD_FILELISTS_XML_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_FILELISTS_XML, ZifMdFilelistsXmlPrivate))

/**
 * ZifMdFilelistsXmlPrivate:
 *
 * Private #ZifMdFilelistsXml data
 **/
struct _ZifMdFilelistsXmlPrivate
{
	gboolean			 loaded;
	ZifMdFilelistsXmlSection	 section;
	ZifMdFilelistsXmlSectionList	 section_list;
	ZifMdFilelistsXmlSectionListPackage	section_list_package;
	ZifPackage			*package_temp;
	GPtrArray			*array;
	GPtrArray			*array_temp;
};

G_DEFINE_TYPE (ZifMdFilelistsXml, zif_md_filelists_xml, ZIF_TYPE_MD)

/**
 * zif_md_filelists_xml_unload:
 **/
static gboolean
zif_md_filelists_xml_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_filelists_xml_parser_start_element:
 **/
static void
zif_md_filelists_xml_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					const gchar **attribute_names, const gchar **attribute_values,
					gpointer user_data, GError **error)
{
	guint i;
	ZifMdFilelistsXml *filelists_xml = user_data;

	g_return_if_fail (ZIF_IS_MD_FILELISTS_XML (filelists_xml));

	/* group element */
	if (filelists_xml->priv->section == ZIF_MD_FILELISTS_XML_SECTION_UNKNOWN) {

		/* start of update */
		if (g_strcmp0 (element_name, "filelists") == 0) {
			filelists_xml->priv->section = ZIF_MD_FILELISTS_XML_SECTION_LIST;
			goto out;
		}

		egg_warning ("unhandled element: %s", element_name);
		goto out;
	}

	/* update element */
	if (filelists_xml->priv->section == ZIF_MD_FILELISTS_XML_SECTION_LIST) {

		if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN) {

			if (g_strcmp0 (element_name, "package") == 0) {
				filelists_xml->priv->section_list = ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE;
				filelists_xml->priv->package_temp = ZIF_PACKAGE (zif_package_remote_new ());
				filelists_xml->priv->array_temp = g_ptr_array_new_with_free_func (g_free);
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "pkgid") == 0) {
						zif_package_remote_set_pkgid (ZIF_PACKAGE_REMOTE (filelists_xml->priv->package_temp),
									      attribute_values[i]);
					}
				}
				goto out;
			}

			egg_warning ("unhandled update list tag: %s", element_name);
			goto out;

		}
		if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE) {

			if (g_strcmp0 (element_name, "version") == 0) {
				filelists_xml->priv->section_list_package = ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN;
				goto out;
			}

			if (g_strcmp0 (element_name, "file") == 0) {
				filelists_xml->priv->section_list_package = ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_FILE;
				goto out;
			}
			egg_warning ("unhandled update package tag: %s", element_name);
			goto out;
		}
		egg_warning ("unhandled package tag: %s", element_name);
	}

	egg_warning ("unhandled base tag: %s", element_name);

out:
	return;
}

/**
 * zif_md_filelists_xml_parser_end_element:
 **/
static void
zif_md_filelists_xml_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				      gpointer user_data, GError **error)
{
	ZifMdFilelistsXml *filelists_xml = user_data;

	/* no element */
	if (filelists_xml->priv->section == ZIF_MD_FILELISTS_XML_SECTION_UNKNOWN) {
		egg_warning ("unhandled base end tag: %s", element_name);
		goto out;
	}

	/* update element */
	if (filelists_xml->priv->section == ZIF_MD_FILELISTS_XML_SECTION_LIST) {

		/* update element */
		if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN) {

			/* end of list */
			if (g_strcmp0 (element_name, "filelists") == 0) {
				filelists_xml->priv->section = ZIF_MD_FILELISTS_XML_SECTION_UNKNOWN;
				goto out;
			}
			egg_warning ("unhandled outside tag: %s", element_name);
			goto out;
		}

		/* update element */
		if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE) {

			if (filelists_xml->priv->section_list_package == ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_UNKNOWN) {

				if (g_strcmp0 (element_name, "version") == 0)
					goto out;

				/* end of list */
				if (g_strcmp0 (element_name, "package") == 0) {
					zif_package_set_files (filelists_xml->priv->package_temp, filelists_xml->priv->array_temp);
					g_ptr_array_add (filelists_xml->priv->array, filelists_xml->priv->package_temp);
					filelists_xml->priv->package_temp = NULL;
					filelists_xml->priv->array_temp = NULL;
					filelists_xml->priv->section_list = ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN;
					goto out;
				}
				egg_warning ("unhandled package tag: %s", element_name);
				goto out;
			}

			if (filelists_xml->priv->section_list_package == ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_FILE) {
				if (g_strcmp0 (element_name, "file") == 0) {
					filelists_xml->priv->section_list_package = ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_UNKNOWN;
					goto out;
				}
				egg_warning ("unhandled end of file tag: %s", element_name);
				goto out;
			}
			egg_warning ("unhandled end of package tag: %s", element_name);
			goto out;
		}

		egg_warning ("unhandled update end tag: %s", element_name);
		goto out;
	}

	egg_warning ("unhandled end tag: %s", element_name);
out:
	return;
}

/**
 * zif_md_filelists_xml_parser_text:
 **/
static void
zif_md_filelists_xml_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			       gpointer user_data, GError **error)

{
	ZifMdFilelistsXml *filelists_xml = user_data;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (filelists_xml->priv->section == ZIF_MD_FILELISTS_XML_SECTION_LIST) {
		if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN) {
			egg_warning ("not saving: %s", text);
			goto out;
		}
		if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE) {
			if (filelists_xml->priv->section_list == ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_FILE) {
				g_ptr_array_add (filelists_xml->priv->array_temp, g_strdup (text));
				goto out;
			};
			egg_warning ("not saving: %s", text);
			goto out;
		}
		egg_warning ("not saving: %s", text);
		goto out;
	}
out:
	return;
}

/**
 * zif_md_filelists_xml_load:
 **/
static gboolean
zif_md_filelists_xml_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	const gchar *filename;
	gboolean ret;
	gchar *contents = NULL;
	gsize size;
	ZifMdFilelistsXml *filelists_xml = ZIF_MD_FILELISTS_XML (md);
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_filelists_xml_markup_parser = {
		zif_md_filelists_xml_parser_start_element,
		zif_md_filelists_xml_parser_end_element,
		zif_md_filelists_xml_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};

	g_return_val_if_fail (ZIF_IS_MD_FILELISTS_XML (md), FALSE);

	/* already loaded */
	if (filelists_xml->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for filelists_xml");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_md_filelists_xml_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, filelists_xml, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	/* we don't need to keep syncing */
	filelists_xml->priv->loaded = TRUE;
out:
	g_free (contents);
	return filelists_xml->priv->loaded;
}

/**
 * zif_md_filelists_xml_get_files:
 **/
static GPtrArray *
zif_md_filelists_xml_get_files (ZifMd *md, ZifPackage *package,
				GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *packages;
	ZifPackage *package_tmp;
	guint i;
	gboolean ret;
	const gchar *pkgid;
	const gchar *pkgid_tmp;
	GError *error_local = NULL;
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	ZifMdFilelistsXml *md_filelists = ZIF_MD_FILELISTS_XML (md);

	g_return_val_if_fail (ZIF_IS_MD_FILELISTS_XML (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* setup completion */
	if (md_filelists->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!md_filelists->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_md_load (ZIF_MD (md), cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_filelists_xml file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* setup steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, md_filelists->priv->array->len);

	/* search array */
	pkgid = zif_package_remote_get_pkgid (ZIF_PACKAGE_REMOTE (package));
	packages = md_filelists->priv->array;
	for (i=0; i<packages->len; i++) {
		package_tmp = g_ptr_array_index (packages, i);
		pkgid_tmp = zif_package_remote_get_pkgid (ZIF_PACKAGE_REMOTE (package_tmp));
		if (g_strcmp0 (pkgid, pkgid_tmp) == 0) {
			completion_loop = zif_completion_get_child (completion_local);
			array = zif_package_get_files (package_tmp, cancellable, completion_loop, NULL);
			break;
		}

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_md_filelists_xml_search_file:
 **/
static GPtrArray *
zif_md_filelists_xml_search_file (ZifMd *md, gchar **search,
				  GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *packages;
	ZifPackage *package;
	GPtrArray *files = NULL;
	const gchar *filename;
	guint i, j, k;
	gboolean ret;
	const gchar *pkgid;
	GError *error_local = NULL;
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	ZifMdFilelistsXml *md_filelists = ZIF_MD_FILELISTS_XML (md);

	g_return_val_if_fail (ZIF_IS_MD_FILELISTS_XML (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* setup completion */
	if (md_filelists->priv->loaded)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!md_filelists->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_md_load (ZIF_MD (md), cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_filelists_xml file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* create results array */
	array = g_ptr_array_new_with_free_func (g_free);

	/* no entries, so shortcut */
	if (md_filelists->priv->array->len == 0) {
		zif_completion_done (completion);
		goto out;
	}

	/* setup steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, md_filelists->priv->array->len);

	/* search array */
	packages = md_filelists->priv->array;
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);
		pkgid = zif_package_remote_get_pkgid (ZIF_PACKAGE_REMOTE (package));
		completion_loop = zif_completion_get_child (completion_local);
		files = zif_package_get_files (package, cancellable, completion_loop, NULL);
		for (k=0; k<files->len; k++) {
			filename = g_ptr_array_index (files, k);
			for (j=0; search[j] != NULL; j++) {
				if (g_strcmp0 (filename, search[j]) == 0) {
					g_ptr_array_add (array, g_strdup (pkgid));
					break;
				}
			}
		}

		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	if (files != NULL)
		g_ptr_array_unref (files);
	return array;
}

/**
 * zif_md_filelists_xml_finalize:
 **/
static void
zif_md_filelists_xml_finalize (GObject *object)
{
	ZifMdFilelistsXml *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_FILELISTS_XML (object));
	md = ZIF_MD_FILELISTS_XML (object);

	g_ptr_array_unref (md->priv->array);

	G_OBJECT_CLASS (zif_md_filelists_xml_parent_class)->finalize (object);
}

/**
 * zif_md_filelists_xml_class_init:
 **/
static void
zif_md_filelists_xml_class_init (ZifMdFilelistsXmlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_filelists_xml_finalize;

	/* map */
	md_class->load = zif_md_filelists_xml_load;
	md_class->unload = zif_md_filelists_xml_unload;
	md_class->search_file = zif_md_filelists_xml_search_file;
	md_class->get_files = zif_md_filelists_xml_get_files;

	g_type_class_add_private (klass, sizeof (ZifMdFilelistsXmlPrivate));
}

/**
 * zif_md_filelists_xml_init:
 **/
static void
zif_md_filelists_xml_init (ZifMdFilelistsXml *md)
{
	md->priv = ZIF_MD_FILELISTS_XML_GET_PRIVATE (md);
	md->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_FILELISTS_XML_SECTION_UNKNOWN;
	md->priv->section_list = ZIF_MD_FILELISTS_XML_SECTION_LIST_UNKNOWN;
	md->priv->section_list_package = ZIF_MD_FILELISTS_XML_SECTION_LIST_PACKAGE_UNKNOWN;
	md->priv->package_temp = NULL;
	md->priv->array_temp = NULL;
}

/**
 * zif_md_filelists_xml_new:
 *
 * Return value: A new #ZifMdFilelistsXml class instance.
 *
 * Since: 0.0.1
 **/
ZifMdFilelistsXml *
zif_md_filelists_xml_new (void)
{
	ZifMdFilelistsXml *md;
	md = g_object_new (ZIF_TYPE_MD_FILELISTS_XML, NULL);
	return ZIF_MD_FILELISTS_XML (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_filelists_xml_test (EggTest *test)
{
	ZifMdFilelistsXml *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifString *summary;
	GCancellable *cancellable;
	ZifCompletion *completion;
	gchar *pkgid;
	gchar *data[] = { "/usr/lib/debug/usr/bin/gpk-prefs.debug", NULL };

	if (!egg_test_start (test, "ZifMdFilelistsXml"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_filelists_xml md");
	md = zif_md_filelists_xml_new ();
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
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_FILELISTS_XML);
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
	egg_test_title (test, "set checksum compressed");
	ret = zif_md_set_checksum (ZIF_MD (md), "cadb324b10d395058ed22c9d984038927a3ea4ff9e0e798116be44b0233eaa49");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "8018e177379ada1d380b4ebf800e7caa95ff8cf90fdd6899528266719bbfdeab");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/fedora/filelists.xml.gz");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "search for files");
	zif_completion_reset (completion);
	array = zif_md_filelists_xml_search_file (ZIF_MD (md), (gchar**)data, cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len != 1)
		egg_test_failed (test, "got %i", array->len);
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "correct value");
	pkgid = g_ptr_array_index (array, 0);
	if (pkgid == NULL)
		egg_test_failed (test, "failed to get a pkgId");
	else if (pkgid[0] != '\0' && strlen (pkgid) == 64)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get a correct pkgId '%s' (%i)", pkgid, strlen (pkgid));
	g_ptr_array_unref (array);

	g_object_unref (cancellable);
	g_object_unref (completion);
	g_object_unref (md);

	egg_test_end (test);
}
#endif

