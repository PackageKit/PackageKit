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
 * SECTION:zif-md-primary-xml
 * @short_description: PrimaryXml metadata functionality
 *
 * Provide access to the primary_xml repo metadata.
 * This object is a subclass of #ZifMd
 */

typedef enum {
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE,
	ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN
} ZifMdPrimaryXmlSection;

typedef enum {
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_NAME,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_ARCH,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_VERSION,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CHECKSUM,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SUMMARY,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_DESCRIPTION,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_URL,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SIZE,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LICENCE,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LOCATION,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_GROUP,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_PROVIDES,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_REQUIRES,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_OBSOLETES,
	ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN
} ZifMdPrimaryXmlSectionPackage;

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gio/gio.h>

#include "zif-md.h"
#include "zif-utils.h"
#include "zif-md-primary-xml.h"
#include "zif-package-remote.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_MD_PRIMARY_XML_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_PRIMARY_XML, ZifMdPrimaryXmlPrivate))

/**
 * ZifMdPrimaryXmlPrivate:
 *
 * Private #ZifMdPrimaryXml data
 **/
struct _ZifMdPrimaryXmlPrivate
{
	gboolean			 loaded;
	ZifMdPrimaryXmlSection		 section;
	ZifMdPrimaryXmlSectionPackage	 section_package;
	ZifPackage			*package_temp;
	GPtrArray			*array;
	gchar				*package_name_temp;
	gchar				*package_arch_temp;
	gchar				*package_version_temp;
	gchar				*package_release_temp;
	guint				 package_epoch_temp;
};

G_DEFINE_TYPE (ZifMdPrimaryXml, zif_md_primary_xml, ZIF_TYPE_MD)

/**
 * zif_md_primary_xml_unload:
 **/
static gboolean
zif_md_primary_xml_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}


/**
 * zif_md_primary_xml_parser_start_element:
 **/
static void
zif_md_primary_xml_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					const gchar **attribute_names, const gchar **attribute_values,
					gpointer user_data, GError **error)
{
	guint i;
	ZifMdPrimaryXml *primary_xml = user_data;

	g_return_if_fail (ZIF_IS_MD_PRIMARY_XML (primary_xml));

	/* group element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN) {

		/* start of list */
		if (g_strcmp0 (element_name, "metadata") == 0)
			goto out;

		/* start of update */
		if (g_strcmp0 (element_name, "package") == 0) {
			primary_xml->priv->section = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE;
			primary_xml->priv->package_temp = zif_package_new ();
			goto out;
		}

		egg_warning ("unhandled element: %s", element_name);

		goto out;
	}

	/* update element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE) {

		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN) {
			if (g_strcmp0 (element_name, "packager") == 0 ||
			    g_strcmp0 (element_name, "format") == 0 ||
			    g_strcmp0 (element_name, "file") == 0 ||
			    g_strcmp0 (element_name, "rpm:vendor") == 0 ||
			    g_strcmp0 (element_name, "rpm:buildhost") == 0 ||
			    g_strcmp0 (element_name, "rpm:header-range") == 0 ||
			    g_strcmp0 (element_name, "rpm:sourcerpm") == 0 ||
			    g_strcmp0 (element_name, "time") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN;
				goto out;
			}
			if (g_strcmp0 (element_name, "name") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_NAME;
				goto out;
			}
			if (g_strcmp0 (element_name, "checksum") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CHECKSUM;
				goto out;
			}
			if (g_strcmp0 (element_name, "arch") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_ARCH;
				goto out;
			}
			if (g_strcmp0 (element_name, "summary") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SUMMARY;
				goto out;
			}
			if (g_strcmp0 (element_name, "description") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_DESCRIPTION;
				goto out;
			}
			if (g_strcmp0 (element_name, "url") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_URL;
				goto out;
			}
			if (g_strcmp0 (element_name, "version") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_VERSION;
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "rel") == 0) {
						primary_xml->priv->package_release_temp = g_strdup (attribute_values[i]);
					} else if (g_strcmp0 (attribute_names[i], "epoch") == 0) {
						primary_xml->priv->package_epoch_temp = atoi (attribute_values[i]);
					} else if (g_strcmp0 (attribute_names[i], "ver") == 0) {
						primary_xml->priv->package_version_temp = g_strdup (attribute_values[i]);
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "size") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SIZE;
				for (i=0; attribute_names[i] != NULL; i++) {
					if (g_strcmp0 (attribute_names[i], "package") == 0) {
						zif_package_set_size (primary_xml->priv->package_temp, atoi (attribute_values[i]));
					}
				}
				goto out;
			}
			if (g_strcmp0 (element_name, "location") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LOCATION;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:license") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LICENCE;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:group") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_GROUP;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:provides") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_PROVIDES;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:requires") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_REQUIRES;
				goto out;
			}
			if (g_strcmp0 (element_name, "rpm:obsoletes") == 0) {
				primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_OBSOLETES;
				goto out;
			}
			egg_warning ("unhandled update base tag: %s", element_name);
			goto out;

		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_REQUIRES) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				goto out;
			}
		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_OBSOLETES) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				goto out;
			}
		} else if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_PROVIDES) {
			if (g_strcmp0 (element_name, "rpm:entry") == 0) {
				goto out;
			}
			goto out;
		}
		egg_warning ("unhandled package tag: %s", element_name);
	}

	egg_warning ("unhandled base tag: %s", element_name);

out:
	return;
}

/**
 * zif_md_primary_xml_parser_end_element:
 **/
static void
zif_md_primary_xml_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				      gpointer user_data, GError **error)
{
	ZifMdPrimaryXml *primary_xml = user_data;
	gchar *package_id = NULL;

	/* no element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN) {
		/* end of list */
		if (g_strcmp0 (element_name, "metadata") == 0)
			goto out;
		egg_warning ("unhandled base end tag: %s", element_name);
	}

	/* update element */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE) {

		/* end of update */
		if (g_strcmp0 (element_name, "package") == 0) {
			primary_xml->priv->section = ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN;

			/* add to array */
			package_id = zif_package_id_from_nevra (primary_xml->priv->package_name_temp,
								primary_xml->priv->package_epoch_temp,
								primary_xml->priv->package_version_temp,
								primary_xml->priv->package_release_temp,
								primary_xml->priv->package_arch_temp,
								zif_md_get_id (ZIF_MD (primary_xml)));
			zif_package_set_id (primary_xml->priv->package_temp, package_id);
			g_ptr_array_add (primary_xml->priv->array, primary_xml->priv->package_temp);
			primary_xml->priv->package_temp = NULL;
			goto out;
		}

		/* do not change section */
		if (g_strcmp0 (element_name, "rpm:entry") == 0) {
			goto out;
		}

		if (g_strcmp0 (element_name, "name") == 0 ||
		    g_strcmp0 (element_name, "summary") == 0 ||
		    g_strcmp0 (element_name, "arch") == 0 ||
		    g_strcmp0 (element_name, "version") == 0 ||
		    g_strcmp0 (element_name, "checksum") == 0 ||
		    g_strcmp0 (element_name, "file") == 0 ||
		    g_strcmp0 (element_name, "time") == 0 ||
		    g_strcmp0 (element_name, "size") == 0 ||
		    g_strcmp0 (element_name, "rpm:license") == 0 ||
		    g_strcmp0 (element_name, "rpm:vendor") == 0 ||
		    g_strcmp0 (element_name, "rpm:group") == 0 ||
		    g_strcmp0 (element_name, "rpm:buildhost") == 0 ||
		    g_strcmp0 (element_name, "rpm:provides") == 0 ||
		    g_strcmp0 (element_name, "rpm:requires") == 0 ||
		    g_strcmp0 (element_name, "rpm:obsoletes") == 0 ||
		    g_strcmp0 (element_name, "rpm:sourcerpm") == 0 ||
		    g_strcmp0 (element_name, "rpm:header-range") == 0 ||
		    g_strcmp0 (element_name, "location") == 0 ||
		    g_strcmp0 (element_name, "format") == 0 ||
		    g_strcmp0 (element_name, "packager") == 0 ||
		    g_strcmp0 (element_name, "description") == 0 ||
		    g_strcmp0 (element_name, "url") == 0) {
			primary_xml->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN;
			goto out;
		}

		egg_warning ("unhandled update end tag: %s", element_name);
		goto out;
	}

	egg_warning ("unhandled end tag: %s", element_name);
out:
	g_free (package_id);
	return;
}

/**
 * zif_md_primary_xml_parser_text:
 **/
static void
zif_md_primary_xml_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			       gpointer user_data, GError **error)

{
	ZifMdPrimaryXml *primary_xml = user_data;
	ZifString *string = NULL;

	/* skip whitespace */
	if (text_len < 1 || text[0] == ' ' || text[0] == '\t' || text[0] == '\n')
		goto out;

	/* group section */
	if (primary_xml->priv->section == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE) {
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN)
			goto out;
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_NAME) {
			primary_xml->priv->package_name_temp = g_strdup (text);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_ARCH) {
			primary_xml->priv->package_arch_temp = g_strdup (text);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_SUMMARY) {
			string = zif_string_new (text);
			zif_package_set_summary (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_DESCRIPTION) {
			string = zif_string_new (text);
			zif_package_set_description (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_URL) {
			string = zif_string_new (text);
			zif_package_set_url (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_GROUP) {
			string = zif_string_new (text);
			zif_package_set_category (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_LICENCE) {
			string = zif_string_new (text);
			zif_package_set_license (primary_xml->priv->package_temp, string);
			goto out;
		}
		if (primary_xml->priv->section_package == ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_CHECKSUM) {
			/* TODO: put in to the zif API? */
			g_object_set_data_full (G_OBJECT(primary_xml->priv->package_temp), "pkgid", g_strdup (text), g_free);
			goto out;
		}
		egg_error ("not saving: %s", text);
		goto out;
	}
out:
	if (string != NULL)
		zif_string_unref (string);
	return;
}

/**
 * zif_md_primary_xml_load:
 **/
static gboolean
zif_md_primary_xml_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	const gchar *filename;
	gboolean ret;
	gchar *contents = NULL;
	gsize size;
	ZifMdPrimaryXml *primary_xml = ZIF_MD_PRIMARY_XML (md);
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_md_primary_xml_markup_parser = {
		zif_md_primary_xml_parser_start_element,
		zif_md_primary_xml_parser_end_element,
		zif_md_primary_xml_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_XML (md), FALSE);

	/* already loaded */
	if (primary_xml->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for primary_xml");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_md_primary_xml_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, primary_xml, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	/* we don't need to keep syncing */
	primary_xml->priv->loaded = TRUE;
out:
	g_free (contents);
	return primary_xml->priv->loaded;
}

typedef gboolean (*ZifPackageFilterFunc)		(ZifPackage		*package,
							 gpointer		 user_data);

/**
 * zif_md_primary_xml_filter:
 **/
static GPtrArray *
zif_md_primary_xml_filter (ZifMd *md, ZifPackageFilterFunc filter_func, gpointer user_data,
			   GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GPtrArray *array = NULL;
	GPtrArray *packages;
	ZifPackage *package;
	guint i;
	gboolean ret;
	GError *error_local = NULL;
	ZifCompletion *completion_local;
	ZifMdPrimaryXml *md_primary = ZIF_MD_PRIMARY_XML (md);

	g_return_val_if_fail (ZIF_IS_MD_PRIMARY_XML (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* setup completion */
	if (md_primary->priv->loaded)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* if not already loaded, load */
	if (!md_primary->priv->loaded) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_md_load (ZIF_MD (md), cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to load md_primary_xml file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* search array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	packages = md_primary->priv->array;
	for (i=0; i<packages->len; i++) {
		package = g_ptr_array_index (packages, i);
		if (filter_func (package, user_data))
			g_ptr_array_add (array, g_object_ref (package));
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_md_primary_xml_resolve_cb:
 **/
static gboolean
zif_md_primary_xml_resolve_cb (ZifPackage *package, gpointer user_data)
{
	const gchar *value;
	const gchar *search = (const gchar *) user_data;
	value = zif_package_get_name (package);
	return (g_strcmp0 (value, search) == 0);
}

/**
 * zif_md_primary_xml_resolve:
 **/
static GPtrArray *
zif_md_primary_xml_resolve (ZifMd *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_resolve_cb, (gpointer) search,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_search_name_cb:
 **/
static gboolean
zif_md_primary_xml_search_name_cb (ZifPackage *package, gpointer user_data)
{
	const gchar *value;
	const gchar *search = (const gchar *) user_data;
	value = zif_package_get_name (package);
	return (g_strstr_len (value, -1, search) != NULL);
}

/**
 * zif_md_primary_xml_search_name:
 **/
static GPtrArray *
zif_md_primary_xml_search_name (ZifMd *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_search_name_cb, (gpointer) search,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_search_details_cb:
 **/
static gboolean
zif_md_primary_xml_search_details_cb (ZifPackage *package, gpointer user_data)
{
	gboolean ret;
	const gchar *value;
	ZifString *string;
	const gchar *search = (const gchar *) user_data;
	value = zif_package_get_name (package);
	ret = (g_strstr_len (value, -1, search) != NULL);
	if (ret)
		goto out;
	string = zif_package_get_summary (package, NULL);
	ret = (g_strstr_len (zif_string_get_value (string), -1, search) != NULL);
	zif_string_unref (string);
	if (ret)
		goto out;
	string = zif_package_get_description (package, NULL);
	ret = (g_strstr_len (zif_string_get_value (string), -1, search) != NULL);
	zif_string_unref (string);
	if (ret)
		goto out;
out:
	return ret;
}

/**
 * zif_md_primary_xml_search_details:
 **/
static GPtrArray *
zif_md_primary_xml_search_details (ZifMd *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_search_details_cb, (gpointer) search,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_search_group_cb:
 **/
static gboolean
zif_md_primary_xml_search_group_cb (ZifPackage *package, gpointer user_data)
{
	gboolean ret;
	ZifString *value;
	const gchar *search = (const gchar *) user_data;
	value = zif_package_get_category (package, NULL);
	ret = (g_strstr_len (zif_string_get_value (value), -1, search) != NULL);
	zif_string_unref (value);
	return ret;
}

/**
 * zif_md_primary_xml_search_group:
 **/
static GPtrArray *
zif_md_primary_xml_search_group (ZifMd *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_search_group_cb, (gpointer) search,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_search_pkgid_cb:
 **/
static gboolean
zif_md_primary_xml_search_pkgid_cb (ZifPackage *package, gpointer user_data)
{
	const gchar *value;
	const gchar *search = (const gchar *) user_data;
	value = (const gchar *) g_object_get_data (G_OBJECT (package), "pkgid");
	return (g_strcmp0 (value, search) == 0);
}

/**
 * zif_md_primary_xml_search_pkgid:
 **/
static GPtrArray *
zif_md_primary_xml_search_pkgid (ZifMd *md, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_search_pkgid_cb, (gpointer) search,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_what_provides_cb:
 **/
static gboolean
zif_md_primary_xml_what_provides_cb (ZifPackage *package, gpointer user_data)
{
	gboolean ret;
	GPtrArray *array;
//	const gchar *search = (const gchar *) user_data;
	array = zif_package_get_provides (package, NULL);
	/* TODO: do something with the ZifDepend objects */
	ret = FALSE;
	g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_md_primary_xml_what_provides:
 **/
static GPtrArray *
zif_md_primary_xml_what_provides (ZifMd *md, const gchar *search,
				  GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_what_provides_cb, (gpointer) search,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_find_package_cb:
 **/
static gboolean
zif_md_primary_xml_find_package_cb (ZifPackage *package, gpointer user_data)
{
	const gchar *value;
	const gchar *search = (const gchar *) user_data;
	value = zif_package_get_id (package);
	return (g_strcmp0 (value, search) == 0);
}

/**
 * zif_md_primary_xml_find_package:
 **/
static GPtrArray *
zif_md_primary_xml_find_package (ZifMd *md, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	return zif_md_primary_xml_filter (md, zif_md_primary_xml_find_package_cb, (gpointer) package_id,
					  cancellable, completion, error);
}

/**
 * zif_md_primary_xml_get_packages:
 **/
static GPtrArray *
zif_md_primary_xml_get_packages (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifMdPrimaryXml *primary_xml = ZIF_MD_PRIMARY_XML (md);
	return g_ptr_array_ref (primary_xml->priv->array);
}

/**
 * zif_md_primary_xml_finalize:
 **/
static void
zif_md_primary_xml_finalize (GObject *object)
{
	ZifMdPrimaryXml *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_PRIMARY_XML (object));
	md = ZIF_MD_PRIMARY_XML (object);

	g_ptr_array_unref (md->priv->array);

	G_OBJECT_CLASS (zif_md_primary_xml_parent_class)->finalize (object);
}

/**
 * zif_md_primary_xml_class_init:
 **/
static void
zif_md_primary_xml_class_init (ZifMdPrimaryXmlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_primary_xml_finalize;

	/* map */
	md_class->load = zif_md_primary_xml_load;
	md_class->unload = zif_md_primary_xml_unload;
	md_class->search_name = zif_md_primary_xml_search_name;
	md_class->search_details = zif_md_primary_xml_search_details;
	md_class->search_group = zif_md_primary_xml_search_group;
	md_class->search_pkgid = zif_md_primary_xml_search_pkgid;
	md_class->what_provides = zif_md_primary_xml_what_provides;
	md_class->resolve = zif_md_primary_xml_resolve;
	md_class->get_packages = zif_md_primary_xml_get_packages;
	md_class->find_package = zif_md_primary_xml_find_package;

	g_type_class_add_private (klass, sizeof (ZifMdPrimaryXmlPrivate));
}

/**
 * zif_md_primary_xml_init:
 **/
static void
zif_md_primary_xml_init (ZifMdPrimaryXml *md)
{
	md->priv = ZIF_MD_PRIMARY_XML_GET_PRIVATE (md);
	md->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	md->priv->loaded = FALSE;
	md->priv->section = ZIF_MD_PRIMARY_XML_SECTION_UNKNOWN;
	md->priv->section_package = ZIF_MD_PRIMARY_XML_SECTION_PACKAGE_UNKNOWN;
	md->priv->package_temp = NULL;
}

/**
 * zif_md_primary_xml_new:
 *
 * Return value: A new #ZifMdPrimaryXml class instance.
 *
 * Since: 0.0.1
 **/
ZifMdPrimaryXml *
zif_md_primary_xml_new (void)
{
	ZifMdPrimaryXml *md;
	md = g_object_new (ZIF_TYPE_MD_PRIMARY_XML, NULL);
	return ZIF_MD_PRIMARY_XML (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_primary_xml_test (EggTest *test)
{
	ZifMdPrimaryXml *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	ZifPackage *package;
	ZifString *summary;
	GCancellable *cancellable;
	ZifCompletion *completion;

	if (!egg_test_start (test, "ZifMdPrimaryXml"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get md_primary_xml md");
	md = zif_md_primary_xml_new ();
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
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_PRIMARY_XML);
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
	ret = zif_md_set_checksum (ZIF_MD (md), "33a0eed8e12f445618756b18aa49d05ee30069d280d37b03a7a15d1ec954f833");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set checksum uncompressed");
	ret = zif_md_set_checksum_uncompressed (ZIF_MD (md), "52e4c37b13b4b23ae96432962186e726550b19e93cf3cbf7bf55c2a673a20086");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/fedora/primary.xml.gz");
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
	array = zif_md_primary_xml_resolve (md, "gnome-power-manager", cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	egg_test_assert (test, array->len == 1);

	/************************************************************/
	egg_test_title (test, "correct value");
	package = g_ptr_array_index (array, 0);
	summary = zif_package_get_summary (package, NULL);
	if (g_strcmp0 (zif_string_get_value (summary), "GNOME power management service") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct summary '%s'", zif_string_get_value (summary));
	zif_string_unref (summary);
	g_ptr_array_unref (array);

	g_object_unref (cancellable);
	g_object_unref (completion);
	g_object_unref (md);

	egg_test_end (test);
}
#endif

