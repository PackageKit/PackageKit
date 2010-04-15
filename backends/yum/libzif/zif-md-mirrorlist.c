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
 * SECTION:zif-md-mirrorlist
 * @short_description: Mirrorlist metadata functionality
 *
 * Provide access to the mirrorlist repo metadata.
 * This object is a subclass of #ZifMd
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <glib.h>

#include "zif-md.h"
#include "zif-md-mirrorlist.h"
#include "zif-config.h"

#include "egg-debug.h"

#define ZIF_MD_MIRRORLIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_MD_MIRRORLIST, ZifMdMirrorlistPrivate))

/**
 * ZifMdMirrorlistPrivate:
 *
 * Private #ZifMdMirrorlist data
 **/
struct _ZifMdMirrorlistPrivate
{
	gboolean			 loaded;
	GPtrArray			*array;
	ZifConfig			*config;
};

G_DEFINE_TYPE (ZifMdMirrorlist, zif_md_mirrorlist, ZIF_TYPE_MD)

/**
 * zif_md_mirrorlist_unload:
 **/
static gboolean
zif_md_mirrorlist_unload (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	return ret;
}

/**
 * zif_md_mirrorlist_load:
 **/
static gboolean
zif_md_mirrorlist_load (ZifMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = TRUE;
	gchar *contents = NULL;
	const gchar *filename;
	gchar **lines = NULL;
	guint i;
	ZifMdMirrorlist *mirrorlist = ZIF_MD_MIRRORLIST (md);

	g_return_val_if_fail (ZIF_IS_MD_MIRRORLIST (md), FALSE);

	/* already loaded */
	if (mirrorlist->priv->loaded)
		goto out;

	/* get filename */
	filename = zif_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error_literal (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to get filename for mirrorlist");
		goto out;
	}

	/* open database */
	egg_debug ("filename = %s", filename);

	/* get repo contents */
	ret = g_file_get_contents (filename, &contents, NULL, error);
	if (!ret)
		goto out;

	/* split, and add uris */
	lines = g_strsplit (contents, "\n", -1);
	for (i=0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0' ||
		    lines[i][0] == '#')
			continue;
		if (g_str_has_prefix (lines[i], "http://"))
			g_ptr_array_add (mirrorlist->priv->array, g_strdup (lines[i]));
	}

	mirrorlist->priv->loaded = TRUE;
out:
	g_strfreev (lines);
	g_free (contents);
	return ret;
}

/**
 * zif_md_mirrorlist_get_uris:
 * @md: the #ZifMdMirrorlist object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds all mirrors we should use.
 *
 * Return value: the uris to use as an array of strings
 *
 * Since: 0.0.1
 **/
GPtrArray *
zif_md_mirrorlist_get_uris (ZifMdMirrorlist *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	guint len;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	const gchar *data;
	gchar *uri;
	guint i;
	ZifMdMirrorlist *mirrorlist = ZIF_MD_MIRRORLIST (md);

	g_return_val_if_fail (ZIF_IS_MD_MIRRORLIST (md), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* if not already loaded, load */
	if (!mirrorlist->priv->loaded) {
		ret = zif_md_load (ZIF_MD (md), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED_TO_LOAD,
				     "failed to get uris from mirrorlist: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get list */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	len = mirrorlist->priv->array->len;
	for (i=0; i<len; i++) {
		data = g_ptr_array_index (mirrorlist->priv->array, i);
		uri = zif_config_expand_substitutions (md->priv->config, data, &error_local);
		if (uri == NULL) {
			g_set_error (error, ZIF_MD_ERROR, ZIF_MD_ERROR_FAILED,
				     "failed to expand substitutions: %s", error_local->message);
			g_error_free (error_local);
			/* rip apart what we've done already */
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}
		g_ptr_array_add (array, uri);
	}
out:
	return array;
}

/**
 * zif_md_mirrorlist_finalize:
 **/
static void
zif_md_mirrorlist_finalize (GObject *object)
{
	ZifMdMirrorlist *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_MD_MIRRORLIST (object));
	md = ZIF_MD_MIRRORLIST (object);

	g_ptr_array_unref (md->priv->array);
	g_object_unref (md->priv->config);

	G_OBJECT_CLASS (zif_md_mirrorlist_parent_class)->finalize (object);
}

/**
 * zif_md_mirrorlist_class_init:
 **/
static void
zif_md_mirrorlist_class_init (ZifMdMirrorlistClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifMdClass *md_class = ZIF_MD_CLASS (klass);
	object_class->finalize = zif_md_mirrorlist_finalize;

	/* map */
	md_class->load = zif_md_mirrorlist_load;
	md_class->unload = zif_md_mirrorlist_unload;
	g_type_class_add_private (klass, sizeof (ZifMdMirrorlistPrivate));
}

/**
 * zif_md_mirrorlist_init:
 **/
static void
zif_md_mirrorlist_init (ZifMdMirrorlist *md)
{
	md->priv = ZIF_MD_MIRRORLIST_GET_PRIVATE (md);
	md->priv->loaded = FALSE;
	md->priv->config = zif_config_new ();
	md->priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
}

/**
 * zif_md_mirrorlist_new:
 *
 * Return value: A new #ZifMdMirrorlist class instance.
 *
 * Since: 0.0.1
 **/
ZifMdMirrorlist *
zif_md_mirrorlist_new (void)
{
	ZifMdMirrorlist *md;
	md = g_object_new (ZIF_TYPE_MD_MIRRORLIST, NULL);
	return ZIF_MD_MIRRORLIST (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_md_mirrorlist_test (EggTest *test)
{
	ZifMdMirrorlist *md;
	gboolean ret;
	GError *error = NULL;
	GPtrArray *array;
	const gchar *uri;
	GCancellable *cancellable;
	ZifCompletion *completion;
	ZifConfig *config;

	if (!egg_test_start (test, "ZifMdMirrorlist"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();
	config = zif_config_new ();
	zif_config_set_filename (config, "../test/etc/yum.conf", NULL);

	/************************************************************/
	egg_test_title (test, "get md_mirrorlist md");
	md = zif_md_mirrorlist_new ();
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
	ret = zif_md_set_mdtype (ZIF_MD (md), ZIF_MD_TYPE_MIRRORLIST);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "set filename");
	ret = zif_md_set_filename (ZIF_MD (md), "../test/cache/mirrorlist.txt");
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
	egg_test_title (test, "get uris");
	array = zif_md_mirrorlist_get_uris (md, cancellable, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect value %i", array->len);

	/************************************************************/
	egg_test_title (test, "correct value");
	uri = g_ptr_array_index (array, 0);
	if (g_strcmp0 (uri, "http://rpm.livna.org/repo/11/i386/") == 0)
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

