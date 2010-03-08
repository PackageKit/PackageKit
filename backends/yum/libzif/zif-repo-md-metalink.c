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
 * SECTION:zif-repo-md-metalink
 * @short_description: Metalink metadata functionality
 *
 * Provide access to the metalink repo metadata.
 * This object is a subclass of #ZifRepoMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-repo-md.h"
#include "zif-repo-md-metalink.h"
#include "zif-config.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_METALINK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD_METALINK, ZifRepoMdMetalinkPrivate))

typedef enum {
	ZIF_REPO_MD_METALINK_PARSER_SECTION_URL,
	ZIF_REPO_MD_METALINK_PARSER_SECTION_UNKNOWN
} ZifRepoMdMetalinkParserSection;

typedef enum {
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_FTP,
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_HTTP,
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_RSYNC,
	ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_UNKNOWN
} ZifRepoMdMetalinkProtocolType;

typedef struct {
	ZifRepoMdMetalinkProtocolType	 protocol;
	gchar				*uri;
	guint				 preference;
} ZifRepoMdMetalinkData;

/**
 * ZifRepoMdMetalinkPrivate:
 *
 * Private #ZifRepoMdMetalink data
 **/
struct _ZifRepoMdMetalinkPrivate
{
	gboolean			 loaded;
	GPtrArray			*array;
	ZifConfig			*config;
	/* for parser */
	ZifRepoMdMetalinkParserSection	 section;
	ZifRepoMdMetalinkData		*temp;
};

G_DEFINE_TYPE (ZifRepoMdMetalink, zif_repo_md_metalink, ZIF_TYPE_REPO_MD)

/**
 * zif_repo_md_metalink_protocol_type_from_text:
 **/
static ZifRepoMdMetalinkProtocolType
zif_repo_md_metalink_protocol_type_from_text (const gchar *type_text)
{
	if (g_strcmp0 (type_text, "ftp") == 0)
		return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_FTP;
	if (g_strcmp0 (type_text, "http") == 0)
		return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_HTTP;
	if (g_strcmp0 (type_text, "rsync") == 0)
		return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_RSYNC;
	return ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_UNKNOWN;
}

/**
 * zif_repo_md_metalink_parser_start_element:
 **/
static void
zif_repo_md_metalink_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
					   const gchar **attribute_names, const gchar **attribute_values,
					   gpointer user_data, GError **error)
{
	guint i;
	ZifRepoMdMetalink *metalink = user_data;

	g_return_if_fail (ZIF_IS_REPO_MD_METALINK (metalink));
	g_return_if_fail (metalink->priv->temp == NULL);

	/* just ignore non url entries */
	if (g_strcmp0 (element_name, "url") != 0) {
		metalink->priv->temp = NULL;
		metalink->priv->section = ZIF_REPO_MD_METALINK_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* create new element */
	metalink->priv->section = ZIF_REPO_MD_METALINK_PARSER_SECTION_URL;
	metalink->priv->temp = g_new0 (ZifRepoMdMetalinkData, 1);

	/* read keys */
	for (i=0; attribute_names[i] != NULL; i++) {
		if (g_strcmp0 (attribute_names[i], "protocol") == 0)
			metalink->priv->temp->protocol = zif_repo_md_metalink_protocol_type_from_text (attribute_values[i]);
		if (g_strcmp0 (attribute_names[i], "preference") == 0)
			metalink->priv->temp->preference = atoi (attribute_values[i]);
	}

	/* add to array */
	g_ptr_array_add (metalink->priv->array, metalink->priv->temp);
out:
	return;
}

/**
 * zif_repo_md_metalink_parser_end_element:
 **/
static void
zif_repo_md_metalink_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
					 gpointer user_data, GError **error)
{
	ZifRepoMdMetalink *metalink = user_data;
	metalink->priv->temp = NULL;
	metalink->priv->section = ZIF_REPO_MD_METALINK_PARSER_SECTION_UNKNOWN;
}

/**
 * zif_repo_md_metalink_parser_text:
 **/
static void
zif_repo_md_metalink_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
				  gpointer user_data, GError **error)

{
	ZifRepoMdMetalink *metalink = user_data;

	if (metalink->priv->section != ZIF_REPO_MD_METALINK_PARSER_SECTION_URL)
		goto out;

	/* shouldn't happen */
	if (metalink->priv->temp == NULL) {
		egg_warning ("no data, so cannot save %s!", text);
		goto out;
	}

	/* save uri */
	if (metalink->priv->temp->uri != NULL) {
		egg_warning ("previously set uri to '%s', cannot overwrite with '%s'", metalink->priv->temp->uri, text);
		goto out;
	}
	metalink->priv->temp->uri = g_strdup (text);
out:
	return;
}

/**
 * zif_repo_md_metalink_unload:
 **/
static gboolean
zif_repo_md_metalink_unload (ZifRepoMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_repo_md_metalink_load:
 **/
static gboolean
zif_repo_md_metalink_load (ZifRepoMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_repo_md_metalink_markup_parser = {
		zif_repo_md_metalink_parser_start_element,
		zif_repo_md_metalink_parser_end_element,
		zif_repo_md_metalink_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};
	ZifRepoMdMetalink *metalink = ZIF_REPO_MD_METALINK (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_METALINK (md), FALSE);

	/* already loaded */
	if (metalink->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_repo_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get filename for metalink");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);

	/* get repo contents */
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_repo_md_metalink_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, metalink, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	metalink->priv->loaded = TRUE;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_repo_md_metalink_get_uris:
 * @md: the #ZifRepoMdMetalink object
 * @threshold: the threshold in percent
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all mirrors we should use.
 *
 * Return value: the uris to use as an array of strings
 **/
GPtrArray *
zif_repo_md_metalink_get_uris (ZifRepoMdMetalink *md, guint threshold, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	guint len;
	gchar *uri;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	ZifRepoMdMetalinkData *data;
	guint i;
	ZifRepoMdMetalink *metalink = ZIF_REPO_MD_METALINK (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD_METALINK (md), FALSE);

	/* if not already loaded, load */
	if (!metalink->priv->loaded) {
		ret = zif_repo_md_load (ZIF_REPO_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			if (error != NULL)
				*error = g_error_new (1, 0, "failed to get mirrors from metalink: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	len = metalink->priv->array->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (metalink->priv->array, i);

		/* ignore not http mirrors */
		if (data->protocol != ZIF_REPO_MD_METALINK_PROTOCOL_TYPE_HTTP)
			continue;

		/* ignore low priority */
		if (data->preference >= threshold) {
			uri = zif_config_expand_substitutions (md->priv->config, data->uri, &error_local);
			if (uri == NULL) {
				if (error != NULL)
					*error = g_error_new (1, 0, "failed to expand substitutions: %s", error_local->message);
				g_error_free (error_local);
				/* rip apart what we've done already */
				g_ptr_array_unref (array);
				array = NULL;
				goto out;
			}
			g_ptr_array_add (array, uri);
		}
	}
out:
	return array;
}

/**
 * zif_repo_md_metalink_free_data:
 **/
static void
zif_repo_md_metalink_free_data (ZifRepoMdMetalinkData *data)
{
	g_free (data->uri);
	g_free (data);
}

/**
 * zif_repo_md_metalink_finalize:
 **/
static void
zif_repo_md_metalink_finalize (GObject *object)
{
	ZifRepoMdMetalink *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD_METALINK (object));
	md = ZIF_REPO_MD_METALINK (object);

	g_ptr_array_unref (md->priv->array);
	g_object_unref (md->priv->config);

	G_OBJECT_CLASS (zif_repo_md_metalink_parent_class)->finalize (object);
}

/**
 * zif_repo_md_metalink_class_init:
 **/
static void
zif_repo_md_metalink_class_init (ZifRepoMdMetalinkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifRepoMdClass *repo_md_class = ZIF_REPO_MD_CLASS (klass);
	object_class->finalize = zif_repo_md_metalink_finalize;

	/* map */
	repo_md_class->load = zif_repo_md_metalink_load;
	repo_md_class->unload = zif_repo_md_metalink_unload;
	g_type_class_add_private (klass, sizeof (ZifRepoMdMetalinkPrivate));
}

/**
 * zif_repo_md_metalink_init:
 **/
static void
zif_repo_md_metalink_init (ZifRepoMdMetalink *md)
{
	md->priv = ZIF_REPO_MD_METALINK_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->config = zif_config_new ();
	md->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) zif_repo_md_metalink_free_data);
}

/**
 * zif_repo_md_metalink_new:
 *
 * Return value: A new #ZifRepoMdMetalink class instance.
 **/
ZifRepoMdMetalink *
zif_repo_md_metalink_new (void)
{
	ZifRepoMdMetalink *md;
	md = g_object_new (ZIF_TYPE_REPO_MD_METALINK, NULL);
	return ZIF_REPO_MD_METALINK (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_metalink_test (EggTest *test)
{
	ZifRepoMdMetalink *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;
	GCancellable *cancellable;
	ZifCompletion *completion;
	ZifConfig *config;

	if (!egg_test_start (test, "ZifRepoMdMetalink"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();
	config = zif_config_new ();
	zif_config_set_filename (config, "../test/etc/yum.conf", NULL);

	/************************************************************/
	egg_test_title (test, "get repo_md_metalink md");
	md = zif_repo_md_metalink_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_repo_md_set_id (ZIF_REPO_MD (md), "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set type");
	ret = zif_repo_md_set_mdtype (ZIF_REPO_MD (md), ZIF_REPO_MD_TYPE_METALINK);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_repo_md_set_filename (ZIF_REPO_MD (md), "../test/cache/fedora/metalink.xml");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_repo_md_load (ZIF_REPO_MD (md), cancellable, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "get uris");
	array = zif_repo_md_metalink_get_uris (md, 50, cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 47)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %i", array->len);

	/************************************************************/
	egg_test_title (test, "correct value");
	uri = g_ptr_array_index (array, 0);
	if (g_strcmp0 (uri, "http://www.mirrorservice.org/sites/download.fedora.redhat.com/pub/fedora/linux/releases/12/Everything/i386/os/repodata/repomd.xml") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get correct url '%s'", uri);
	g_ptr_array_unref (array);

	g_object_unref (md);
	g_object_unref (cancellable);
	g_object_unref (completion);
	g_object_unref (config);

	egg_test_end (test);
}
#endif

