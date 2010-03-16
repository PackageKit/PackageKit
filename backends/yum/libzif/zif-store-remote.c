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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:zif-store-remote
 * @short_description: A remote store is a store that can operate on remote packages
 *
 * A #ZifStoreRemote is a subclassed #ZifStore and operates on remote objects.
 * A repository is another name for a #ZifStoreRemote.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <stdlib.h>
#include <gio/gio.h>

#include "zif-config.h"
#include "zif-download.h"
#include "zif-lock.h"
#include "zif-monitor.h"
#include "zif-package.h"
#include "zif-package-remote.h"
#include "zif-repo-md-comps.h"
#include "zif-repo-md-filelists.h"
#include "zif-repo-md-metalink.h"
#include "zif-repo-md-mirrorlist.h"
#include "zif-repo-md-primary.h"
#include "zif-store.h"
#include "zif-store-local.h"
#include "zif-store-remote.h"
#include "zif-utils.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_STORE_REMOTE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_STORE_REMOTE, ZifStoreRemotePrivate))

typedef enum {
	ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM,
	ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_UNCOMPRESSED,
	ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP,
	ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN
} ZifStoreRemoteParserSection;

struct _ZifStoreRemotePrivate
{
	gchar			*id;			/* fedora */
	gchar			*name;			/* Fedora $arch */
	gchar			*name_expanded;		/* Fedora 1386 */
	gchar			*directory;		/* /var/cache/yum/fedora */
	gchar			*repomd_filename;	/* /var/cache/yum/fedora/repomd.xml */
	GPtrArray		*baseurls;		/* http://download.fedora.org/ */
	gchar			*mirrorlist;
	gchar			*metalink;
	gchar			*cache_dir;		/* /var/cache/yum */
	gchar			*repo_filename;		/* /etc/yum.repos.d/fedora.repo */
	gboolean		 enabled;
	gboolean		 loaded;
	gboolean		 loaded_metadata;
	ZifRepoMd		*md_primary;
	ZifRepoMd		*md_filelists;
	ZifRepoMd		*md_metalink;
	ZifRepoMd		*md_mirrorlist;
	ZifRepoMd		*md_comps;
	ZifConfig		*config;
	ZifMonitor		*monitor;
	ZifLock			*lock;
	GPtrArray		*packages;
	/* temp data for the xml parser */
	ZifRepoMdType		 parser_type;
	ZifRepoMdType		 parser_section;
};

G_DEFINE_TYPE (ZifStoreRemote, zif_store_remote, ZIF_TYPE_STORE)

static gboolean zif_store_remote_load_metadata (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error);

/**
 * zif_store_remote_checksum_type_from_text:
 **/
static GChecksumType
zif_store_remote_checksum_type_from_text (const gchar *type)
{
	if (g_strcmp0 (type, "sha") == 0)
		return G_CHECKSUM_SHA1;
	if (g_strcmp0 (type, "sha1") == 0)
		return G_CHECKSUM_SHA1;
	if (g_strcmp0 (type, "sha256") == 0)
		return G_CHECKSUM_SHA256;
	return G_CHECKSUM_MD5;
}

/**
 * zif_store_remote_get_md_from_type:
 **/
static ZifRepoMd *
zif_store_remote_get_md_from_type (ZifStoreRemote *store, ZifRepoMdType type)
{
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (type != ZIF_REPO_MD_TYPE_UNKNOWN, NULL);

	if (type == ZIF_REPO_MD_TYPE_FILELISTS_DB)
		return store->priv->md_filelists;
	if (type == ZIF_REPO_MD_TYPE_PRIMARY_DB)
		return store->priv->md_primary;
	if (type == ZIF_REPO_MD_TYPE_OTHER_DB)
		return NULL;
	if (type == ZIF_REPO_MD_TYPE_COMPS_XML)
		return store->priv->md_comps;
	if (type == ZIF_REPO_MD_TYPE_METALINK)
		return store->priv->md_metalink;
	if (type == ZIF_REPO_MD_TYPE_MIRRORLIST)
		return store->priv->md_mirrorlist;
	return NULL;
}

/**
 * zif_store_remote_parser_start_element:
 **/
static void
zif_store_remote_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
				       const gchar **attribute_names, const gchar **attribute_values,
				       gpointer user_data, GError **error)
{
	guint i, j;
	ZifRepoMd *md;
	ZifStoreRemote *store = user_data;
	GString *string;

	/* data */
	if (g_strcmp0 (element_name, "data") == 0) {

		/* reset */
		store->priv->parser_type = ZIF_REPO_MD_TYPE_UNKNOWN;

		/* find type */
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				if (g_strcmp0 (attribute_values[i], "primary") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_PRIMARY;
				else if (g_strcmp0 (attribute_values[i], "primary_db") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_PRIMARY_DB;
				else if (g_strcmp0 (attribute_values[i], "filelists") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_FILELISTS;
				else if (g_strcmp0 (attribute_values[i], "filelists_db") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_FILELISTS_DB;
				else if (g_strcmp0 (attribute_values[i], "other") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_OTHER;
				else if (g_strcmp0 (attribute_values[i], "other_db") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_OTHER_DB;
				else if (g_strcmp0 (attribute_values[i], "group") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_COMPS;
				else if (g_strcmp0 (attribute_values[i], "group_gz") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_COMPS_XML;
				else if (g_strcmp0 (attribute_values[i], "prestodelta") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_PRESTODELTA;
				else if (g_strcmp0 (attribute_values[i], "updateinfo") == 0)
					store->priv->parser_type = ZIF_REPO_MD_TYPE_UPDATEINFO;
				else {
					if (error != NULL) {
						/* we didn't recognise the file type */
						string = g_string_new ("");
						g_string_append_printf (string, "unhandled data type '%s', expecting ", attribute_values[i]);

						/* list all the types we support */
						for (j=0; j<ZIF_REPO_MD_TYPE_UNKNOWN; j++)
							g_string_append_printf (string, "%s, ", zif_repo_md_type_to_text (j));

						/* remove triling comma and space */
						g_string_set_size (string, string->len - 2);

						/* return error */
						g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
							     "%s", string->str);
						g_string_free (string, TRUE);
					}
				}
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* not a section we recognise */
	if (store->priv->parser_type == ZIF_REPO_MD_TYPE_UNKNOWN)
		goto out;

	/* get MetaData object */
	md = zif_store_remote_get_md_from_type (store, store->priv->parser_type);
	if (md == NULL)
		goto out;

	/* location */
	if (g_strcmp0 (element_name, "location") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "href") == 0) {
				zif_repo_md_set_location (md, attribute_values[i]);
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
		goto out;
	}

	/* checksum */
	if (g_strcmp0 (element_name, "checksum") == 0) {
		for (i=0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				zif_repo_md_set_checksum_type (md, zif_store_remote_checksum_type_from_text (attribute_values[i]));
				break;
			}
		}
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM;
		goto out;
	}

	/* checksum */
	if (g_strcmp0 (element_name, "open-checksum") == 0) {
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_UNCOMPRESSED;
		goto out;
	}

	/* timestamp */
	if (g_strcmp0 (element_name, "timestamp") == 0) {
		store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP;
		goto out;
	}
out:
	return;
}

/**
 * zif_store_remote_parser_end_element:
 **/
static void
zif_store_remote_parser_end_element (GMarkupParseContext *context, const gchar *element_name,
				     gpointer user_data, GError **error)
{
	ZifStoreRemote *store = user_data;

	/* reset */
	store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
	if (g_strcmp0 (element_name, "data") == 0)
		store->priv->parser_type = ZIF_REPO_MD_TYPE_UNKNOWN;
}

/**
 * zif_store_remote_parser_text:
 **/
static void
zif_store_remote_parser_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
			      gpointer user_data, GError **error)

{
	ZifRepoMd *md;
	ZifStoreRemote *store = user_data;

	if (store->priv->parser_type == ZIF_REPO_MD_TYPE_UNKNOWN)
		return;

	/* get MetaData object */
	md = zif_store_remote_get_md_from_type (store, store->priv->parser_type);
	if (md == NULL)
		return;

	if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM)
		zif_repo_md_set_checksum (md, text);
	else if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_CHECKSUM_UNCOMPRESSED)
		zif_repo_md_set_checksum_uncompressed (md, text);
	else if (store->priv->parser_section == ZIF_STORE_REMOTE_PARSER_SECTION_TIMESTAMP)
		zif_repo_md_set_timestamp (md, atol (text));
}

/**
 * zif_store_remote_download_try:
 **/
static gboolean
zif_store_remote_download_try (ZifStoreRemote *store, const gchar *uri, const gchar *filename,
			       GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	ZifDownload *download = NULL;
	gchar *contents = NULL;
	gsize length;

	/* download object */
	download = zif_download_new ();
	egg_debug ("trying to download %s and save to %s", uri, filename);
	ret = zif_download_file (download, uri, filename, cancellable, completion, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to download %s from %s: %s", filename, uri, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* try to read it */
	ret = g_file_get_contents (filename, &contents, &length, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to download %s from %s: %s (unable to read file)", filename, uri, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check we have some data */
	if (length == 0) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to download %s from %s: no data", filename, uri);
		ret = FALSE;
		goto out;
	}

	/* check this really isn't a fancy 404 page */
	if (g_str_has_prefix (contents, "<html>")) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to download %s from %s: invalid file", filename, uri);
		ret = FALSE;
		goto out;
	}

out:
	g_free (contents);
	g_object_unref (download);
	return ret;
}

/**
 * zif_store_remote_ensure_parent_dir_exists:
 **/
static gboolean
zif_store_remote_ensure_parent_dir_exists (const gchar *filename, GError **error)
{
	gchar *dirname = NULL;
	dirname = g_path_get_dirname (filename);
	if (!g_file_test (dirname, G_FILE_TEST_EXISTS)) {
		egg_debug ("creating directory %s", dirname);
		g_mkdir_with_parents (dirname, 0777);
	}
	g_free (dirname);
	return TRUE;
}

/**
 * zif_store_remote_download:
 * @store: the #ZifStoreRemote object
 * @filename: the completion filename to download, e.g. "Packages/hal-0.0.1.rpm"
 * @directory: the directory to put the downloaded file, e.g. "/var/cache/zif"
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a remote package to a local directory.
 * NOTE: if @filename is "Packages/hal-0.0.1.rpm" and @directory is "/var/cache/zif"
 * then the downloaded file will "/var/cache/zif/hal-0.0.1.rpm"
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_remote_download (ZifStoreRemote *store, const gchar *filename, const gchar *directory,
			   GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	guint len;
	gboolean ret = FALSE;
	gchar *uri = NULL;
	GError *error_local = NULL;
	gchar *filename_local = NULL;
	gchar *basename = NULL;
	const gchar *baseurl;
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (directory != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* if not online, then this is fatal */
	ret = zif_config_get_boolean (store->priv->config, "network", NULL);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
			     "failed to download %s as offline", filename);
		goto out;
	}

	/* check this isn't an absolute path */
	if (g_str_has_prefix (filename, "/")) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
			     "filename %s' should not be an absolute path", filename);
		ret = FALSE;
		goto out;
	}

	/* setup completion */
	if (store->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* if not already loaded, load */
	if (!store->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load metadata: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* we need at least one baseurl */
	if (store->priv->baseurls->len == 0) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "no baseurls for %s", store->priv->id);
		ret = FALSE;
		goto out;
	}

	/* get the location to download to */
	basename = g_path_get_basename (filename);
	filename_local = g_build_filename (directory, basename, NULL);

	/* ensure path is valid */
	ret = zif_store_remote_ensure_parent_dir_exists (filename_local, error);
	if (!ret)
		goto out;

	/* try to use all uris */
	len = store->priv->baseurls->len;
	completion_local = zif_completion_get_child (completion);
	for (i=0; i<len; i++) {

		/* build url */
		baseurl = g_ptr_array_index (store->priv->baseurls, i);
		uri = g_build_filename (baseurl, filename, NULL);

		/* try download */
		zif_completion_reset (completion_local);
		ret = zif_store_remote_download_try (store, uri, filename_local, cancellable, completion_local, &error_local);
		if (!ret) {
			egg_debug ("failed to download (non-fatal): %s", error_local->message);
			g_clear_error (&error_local);
		}

		/* free */
		g_free (uri);

		/* succeeded, otherwise retry with new mirrors */
		if (ret)
			break;
	}

	/* this section done */
	zif_completion_done (completion);

	/* nothing */
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to download %s from any sources", filename);
		goto out;
	}
out:
	g_free (basename);
	g_free (filename_local);
	return ret;
}

/**
 * zif_store_remote_add_metalink:
 **/
static gboolean
zif_store_remote_add_metalink (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	const gchar *uri_tmp;
	const gchar *filename;
	gboolean ret = FALSE;
	ZifCompletion *completion_local;
	ZifDownload *download = NULL;

	/* if we're loading the metadata with an empty cache, the file won't yet exist. So download it */
	filename = zif_repo_md_get_filename_uncompressed (store->priv->md_metalink);
	if (filename == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "metalink filename not set for %s", store->priv->id);
		goto out;
	}

	zif_completion_set_number_steps (completion, 2);

	/* find if the file already exists */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		completion_local = zif_completion_get_child (completion);

		/* ensure path is valid */
		ret = zif_store_remote_ensure_parent_dir_exists (filename, error);
		if (!ret)
			goto out;

		/* download object directly, as we don't have the repo setup yet */
		download = zif_download_new ();
		ret = zif_download_file (download, store->priv->metalink, filename, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to download %s from %s: %s", filename, store->priv->metalink, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	zif_completion_done (completion);

	/* get mirrors */
	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_metalink_get_uris (ZIF_REPO_MD_METALINK (store->priv->md_metalink), 50, cancellable, completion_local, &error_local);
	if (array == NULL) {
		ret = FALSE;
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to add mirrors from metalink: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* nothing here? */
	if (array->len == 0) {
		ret = FALSE;
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get any mirrors from metalink: %s", filename);
		goto out;
	}

	zif_completion_done (completion);

	/* add array */
	for (i=0; i<array->len; i++) {
		uri_tmp = g_ptr_array_index (array, i);
		g_ptr_array_add (store->priv->baseurls, g_strdup (uri_tmp));
	}
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;
}

/**
 * zif_store_remote_add_mirrorlist:
 **/
static gboolean
zif_store_remote_add_mirrorlist (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	GPtrArray *array = NULL;
	GError *error_local = NULL;
	const gchar *uri_tmp;
	const gchar *filename;
	gboolean ret = FALSE;
	ZifCompletion *completion_local;
	ZifDownload *download = NULL;

	/* if we're loading the metadata with an empty cache, the file won't yet exist. So download it */
	filename = zif_repo_md_get_filename_uncompressed (store->priv->md_mirrorlist);
	if (filename == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "mirrorlist filename not set for %s", store->priv->id);
		goto out;
	}

	zif_completion_set_number_steps (completion, 2);

	/* find if the file already exists */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		completion_local = zif_completion_get_child (completion);

		/* ensure path is valid */
		ret = zif_store_remote_ensure_parent_dir_exists (filename, error);
		if (!ret)
			goto out;

		/* download object directly, as we don't have the repo setup yet */
		download = zif_download_new ();
		ret = zif_download_file (download, store->priv->mirrorlist, filename, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to download %s from %s: %s", filename, store->priv->mirrorlist, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	zif_completion_done (completion);

	/* get mirrors */
	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_mirrorlist_get_uris (ZIF_REPO_MD_MIRRORLIST (store->priv->md_mirrorlist), cancellable, completion_local, &error_local);
	if (array == NULL) {
		ret = FALSE;
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to add mirrors from mirrorlist: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* nothing here? */
	if (array->len == 0) {
		ret = FALSE;
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get any mirrors from mirrorlist: %s", filename);
		goto out;
	}

	zif_completion_done (completion);

	/* add array */
	for (i=0; i<array->len; i++) {
		uri_tmp = g_ptr_array_index (array, i);
		g_ptr_array_add (store->priv->baseurls, g_strdup (uri_tmp));
	}
out:
	if (download != NULL)
		g_object_unref (download);
	if (array != NULL)
		g_ptr_array_unref (array);
	return ret;

}

/**
 * zif_store_remote_load_metadata:
 *
 * This function does the following things:
 *
 * - opens repomd.xml (downloading it if it doesn't exist)
 * - parses the contents, and populates the ZifMd types
 * - parses metalink and mirrorlink into lists of plain urls
 * - checks all the compressed metadata checksums are valid, else they are deleted
 * - checks all the uncompressed metadata checksums are valid, else they are deleted
 **/
static gboolean
zif_store_remote_load_metadata (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	guint i;
	ZifCompletion *completion_local;
	const gchar *location;
	gboolean ret = TRUE;
	gchar *contents = NULL;
	gchar *basename;
	gchar *filename;
	gsize size;
	GError *error_local = NULL;
	ZifRepoMd *md;
	GMarkupParseContext *context = NULL;
	const GMarkupParser gpk_store_remote_markup_parser = {
		zif_store_remote_parser_start_element,
		zif_store_remote_parser_end_element,
		zif_store_remote_parser_text,
		NULL, /* passthrough */
		NULL /* error */
	};

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);

	/* not locked */
	ret = zif_lock_is_locked (store->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* already loaded */
	if (store->priv->loaded_metadata)
		goto out;

	/* setup completion */
	zif_completion_set_number_steps (completion, 4);

	/* extract details from mirrorlist */
	if (store->priv->mirrorlist != NULL) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_add_mirrorlist (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to add mirrorlist: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* this section done */
	zif_completion_done (completion);

	/* extract details from metalink */
	if (store->priv->metalink != NULL) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_add_metalink (store, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to add metalink: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* check we got something */
	if (store->priv->baseurls->len == 0) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_ARRAY_IS_EMPTY,
			     "no baseurls for %s, so can't download anything! [meta:%s, mirror:%s]",
			     store->priv->id, store->priv->metalink, store->priv->mirrorlist);
		ret = FALSE;
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* repomd file does not exist */
	ret = g_file_test (store->priv->repomd_filename, G_FILE_TEST_EXISTS);
	if (!ret) {
		/* if not online, then this is fatal */
		ret = zif_config_get_boolean (store->priv->config, "network", NULL);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
				     "failed to download repomd as offline");
			goto out;
		}

		/* download new file */
		completion_local = zif_completion_get_child (completion);
		store->priv->loaded_metadata = TRUE;
		ret = zif_store_remote_download (store, "repodata/repomd.xml", store->priv->directory, cancellable, completion_local, &error_local);
		store->priv->loaded_metadata = FALSE;
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to download missing repomd: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* this section done */
	zif_completion_done (completion);

	/* get repo contents */
	ret = g_file_get_contents (store->priv->repomd_filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gpk_store_remote_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, store, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;

	/* set MD id and filename for each repo type */
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md == NULL) {
			/* TODO: until we've created ZifRepoMdComps and ZifRepoMdOther we'll get warnings here */
			egg_debug ("failed to get local store for %s with %s", zif_repo_md_type_to_text (i), store->priv->id);
			continue;
		}

		/* no metalink? */
		if (i == ZIF_REPO_MD_TYPE_METALINK)
			continue;

		/* no mirrorlist? */
		if (i == ZIF_REPO_MD_TYPE_MIRRORLIST)
			continue;

		/* location not set */
		location = zif_repo_md_get_location (md);
		if (location == NULL) {
			/* messed up repo file, this is fatal */
			if (i == ZIF_REPO_MD_TYPE_PRIMARY_DB) {
				g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
					     "failed to get primary metadata location for %s", store->priv->id);
				ret = FALSE;
				goto out;
			}
			egg_debug ("no location set for %s with %s", zif_repo_md_type_to_text (i), store->priv->id);
			continue;
		}

		/* set MD id and filename */
		basename = g_path_get_basename (location);
		filename = g_build_filename (store->priv->directory, basename, NULL);
		zif_repo_md_set_filename (md, filename);
		g_free (basename);
		g_free (filename);
	}

	/* all okay */
	store->priv->loaded_metadata = TRUE;

	/* this section done */
	zif_completion_done (completion);
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (contents);
	return ret;
}

/**
 * zif_store_file_decompress:
 **/
static gboolean
zif_store_file_decompress (const gchar *filename, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = TRUE;
	gboolean compressed;
	gchar *filename_uncompressed = NULL;

	/* only do for compressed filenames */
	compressed = zif_file_is_compressed_name (filename);
	if (!compressed) {
		egg_debug ("%s not compressed", filename);
		goto out;
	}

	/* get new name */
	filename_uncompressed = zif_file_get_uncompressed_name (filename);

	/* decompress */
	ret = zif_file_decompress (filename, filename_uncompressed, cancellable, completion, error);
out:
	g_free (filename_uncompressed);
	return ret;
}

/**
 * zif_store_remote_refresh:
 **/
static gboolean
zif_store_remote_refresh (ZifStore *store, gboolean force, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	const gchar *filename;
	ZifCompletion *completion_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifRepoMd *md;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* if not online, then this is fatal */
	ret = zif_config_get_boolean (remote->priv->config, "network", NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_AS_OFFLINE,
				     "failed to refresh as offline");
		goto out;
	}

	/* setup completion with the correct number of steps */
	zif_completion_set_number_steps (completion, (ZIF_REPO_MD_TYPE_UNKNOWN * 2) + 2);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* get local completion object */
	completion_local = zif_completion_get_child (completion);

	/* download new repomd file */
	ret = zif_store_remote_download (remote, "repodata/repomd.xml", remote->priv->directory, cancellable, completion_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to download repomd: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* reload */
	completion_local = zif_completion_get_child (completion);
	ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to load updated metadata: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* refresh each repo type */
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (remote, i);
		if (md == NULL) {
			egg_debug ("failed to get local store for %s", zif_repo_md_type_to_text (i));
			continue;
		}

		/* get filename */
		filename = zif_repo_md_get_location (md);
		if (filename == NULL) {
			egg_warning ("no filename set for %s", zif_repo_md_type_to_text (i));
			continue;
		}

		/* does current uncompressed file equal what repomd says it should be */
		ret = zif_repo_md_file_check (md, TRUE, &error_local);
		if (!ret) {
			egg_warning ("failed to verify md: %s", error_local->message);
			g_clear_error (&error_local);
		}
		if (ret && !force) {
			egg_debug ("%s is okay, and we're not forcing", zif_repo_md_type_to_text (i));
			continue;
		}

		/* download new file */
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_download (remote, filename, remote->priv->directory, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to refresh %s (%s): %s", zif_repo_md_type_to_text (i), filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);

		/* decompress */
		completion_local = zif_completion_get_child (completion);
		filename = zif_repo_md_get_filename (md);
		ret = zif_store_file_decompress (filename, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to decompress %s for %s: %s",
				     filename, zif_repo_md_type_to_text (i), error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

out:
	return ret;
}

/**
 * zif_store_remote_load:
 *
 * This function has to be fast, so don't download anything or load any
 * databases until zif_store_remote_load_metadata().
 **/
static gboolean
zif_store_remote_load (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GKeyFile *file = NULL;
	gboolean ret = TRUE;
	gchar *enabled = NULL;
	GError *error_local = NULL;
	gchar *temp;
	gchar *filename;
//	ZifCompletion *completion_local;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);
	g_return_val_if_fail (remote->priv->repo_filename != NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* already loaded */
	if (remote->priv->loaded)
		goto out;

	/* setup completion with the correct number of steps */
	zif_completion_set_number_steps (completion, 2);

	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, remote->priv->repo_filename, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to load %s: %s", remote->priv->repo_filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* name */
	remote->priv->name = g_key_file_get_string (file, remote->priv->id, "name", &error_local);
	if (error_local != NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get name: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* enabled */
	enabled = g_key_file_get_string (file, remote->priv->id, "enabled", &error_local);
	if (enabled == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get enabled: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* convert to bool */
	remote->priv->enabled = zif_boolean_from_text (enabled);

	/* expand out */
	remote->priv->name_expanded = zif_config_expand_substitutions (remote->priv->config, remote->priv->name, NULL);

	/* get base url (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "baseurl", NULL);
	if (temp != NULL && temp[0] != '\0')
		g_ptr_array_add (remote->priv->baseurls, zif_config_expand_substitutions (remote->priv->config, temp, NULL));
	g_free (temp);

	/* get mirror list (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "mirrorlist", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->mirrorlist = zif_config_expand_substitutions (remote->priv->config, temp, NULL);
	g_free (temp);

	/* get metalink (allowed to be blank) */
	temp = g_key_file_get_string (file, remote->priv->id, "metalink", NULL);
	if (temp != NULL && temp[0] != '\0')
		remote->priv->metalink = zif_config_expand_substitutions (remote->priv->config, temp, NULL);
	g_free (temp);

	/* urgh.. yum allows mirrorlist= to be used as well as metalink= for metalink URLs */
	if (remote->priv->metalink == NULL &&
	    remote->priv->mirrorlist != NULL &&
	    g_strstr_len (remote->priv->mirrorlist, -1, "metalink?") != NULL) {
		/* swap */
		remote->priv->metalink = remote->priv->mirrorlist;
		remote->priv->mirrorlist = NULL;
	}

	/* we have to set this here in case we are using the metalink to download repodata.xml */
	if (remote->priv->metalink != NULL) {
		filename = g_build_filename (remote->priv->directory, "metalink.xml", NULL);
		zif_repo_md_set_filename (remote->priv->md_metalink, filename);
		g_free (filename);
	}

	/* we have to set this here in case we are using the mirrorlist to download repodata.xml */
	if (remote->priv->mirrorlist != NULL) {
		filename = g_build_filename (remote->priv->directory, "mirrorlist.txt", NULL);
		zif_repo_md_set_filename (remote->priv->md_mirrorlist, filename);
		g_free (filename);
	}

	/* we need either a base url or mirror list for an enabled store */
	if (remote->priv->enabled && remote->priv->baseurls->len == 0 && remote->priv->metalink == NULL && remote->priv->mirrorlist == NULL) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "baseurl, metalink or mirrorlist required");
		ret = FALSE;
		goto out;
	}

	/* okay */
	remote->priv->loaded = TRUE;

	/* this section done */
	zif_completion_done (completion);
out:
	g_free (enabled);
	if (file != NULL)
		g_key_file_free (file);
	return ret;
}

/**
 * zif_store_remote_clean:
 **/
static gboolean
zif_store_remote_clean (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	GError *error_local = NULL;
	GFile *file;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;
	ZifRepoMd *md;
	guint i;
	const gchar *location;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (remote->priv->id != NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion with the correct number of steps */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1+ZIF_REPO_MD_TYPE_UNKNOWN);
	else
		zif_completion_set_number_steps (completion, 2+ZIF_REPO_MD_TYPE_UNKNOWN);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			/* ignore this error */
			g_print ("failed to load xml: %s\n", error_local->message);
			g_error_free (error_local);
			ret = TRUE;
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* set MD id and filename for each repo type */
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (remote, i);
		if (md == NULL) {
			/* TODO: until we've created ZifRepoMdComps and ZifRepoMdOther we'll get warnings here */
			egg_debug ("failed to get local store for %s with %s", zif_repo_md_type_to_text (i), remote->priv->id);
			goto skip;
		}

		/* location not set */
		location = zif_repo_md_get_location (md);
		if (location == NULL) {
			egg_debug ("no location set for %s with %s", zif_repo_md_type_to_text (i), remote->priv->id);
			goto skip;
		}

		/* clean md */
		ret = zif_repo_md_clean (md, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to clean %s: %s", zif_repo_md_type_to_text (i), error_local->message);
			g_error_free (error_local);
			goto out;
		}
skip:
		/* this section done */
		zif_completion_done (completion);
	}

	/* clean master (last) */
	exists = g_file_test (remote->priv->repomd_filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (remote->priv->repomd_filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to delete metadata file %s: %s",
				     remote->priv->repomd_filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return ret;
}

/**
 * zif_store_remote_set_from_file:
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_remote_set_from_file (ZifStoreRemote *store, const gchar *repo_filename, const gchar *id,
				GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = TRUE;
	guint i;
	GError *error_local = NULL;
	ZifRepoMd *md;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (repo_filename != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);
	g_return_val_if_fail (store->priv->id == NULL, FALSE);
	g_return_val_if_fail (!store->priv->loaded, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (store->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* save */
	egg_debug ("setting store %s", id);
	store->priv->id = g_strdup (id);
	store->priv->repo_filename = g_strdup (repo_filename);
	store->priv->directory = g_build_filename (store->priv->cache_dir, store->priv->id, NULL);

	/* repomd location */
	store->priv->repomd_filename = g_build_filename (store->priv->cache_dir, store->priv->id, "repomd.xml", NULL);

	/* set MD id for each repo type */
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md == NULL)
			continue;
		zif_repo_md_set_id (md, store->priv->id);
	}

	/* setup watch */
	ret = zif_monitor_add_watch (store->priv->monitor, repo_filename, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to setup watch: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get data */
	ret = zif_store_remote_load (ZIF_STORE (store), cancellable, completion, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to load %s: %s", id, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	/* save */
	return ret;
}

/**
 * zif_store_remote_set_enabled:
 * @store: the #ZifStoreRemote object
 * @enabled: If the object should be enabled
 * @error: a #GError which is used on failure, or %NULL
 *
 * Enable or disable a remote repository.
 *
 * Return value: %TRUE for success, %FALSE for failure
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_remote_set_enabled (ZifStoreRemote *store, gboolean enabled, GError **error)
{
	GKeyFile *file;
	gboolean ret;
	GError *error_local = NULL;
	gchar *data;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (store->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* load file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, store->priv->repo_filename, G_KEY_FILE_KEEP_COMMENTS, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to load store file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* toggle enabled */
	store->priv->enabled = enabled;
	g_key_file_set_boolean (file, store->priv->id, "enabled", store->priv->enabled);

	/* save new data to file */
	data = g_key_file_to_data (file, NULL, NULL);
	ret = g_file_set_contents (store->priv->repo_filename, data, -1, &error_local);
	if (!ret) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to save: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	g_free (data);
	g_key_file_free (file);
out:
	return ret;
}

/**
 * zif_store_remote_print:
 **/
static void
zif_store_remote_print (ZifStore *store)
{
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	g_return_if_fail (ZIF_IS_STORE_REMOTE (store));
	g_return_if_fail (remote->priv->id != NULL);

	g_print ("id: %s\n", remote->priv->id);
	g_print ("name: %s\n", remote->priv->name);
	g_print ("name-expanded: %s\n", remote->priv->name_expanded);
	g_print ("enabled: %i\n", remote->priv->enabled);
}

/**
 * zif_store_remote_resolve:
 **/
static GPtrArray *
zif_store_remote_resolve (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load metadata for %s: %s", remote->priv->id, error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_primary_resolve (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, cancellable, completion_local, error);

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_remote_search_name:
 **/
static GPtrArray *
zif_store_remote_search_name (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_primary_search_name (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, cancellable, completion_local, error);

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_remote_search_details:
 **/
static GPtrArray *
zif_store_remote_search_details (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_primary_search_details (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, cancellable, completion_local, error);

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_remote_search_category_resolve:
 **/
static ZifPackage *
zif_store_remote_search_category_resolve (ZifStore *store, const gchar *name, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifStoreLocal *store_local = NULL;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	ZifCompletion *completion_local;

	store_local = zif_store_local_new ();

	/* setup steps */
	zif_completion_set_number_steps (completion, 2);

	/* is already installed? */
	completion_local = zif_completion_get_child (completion);
	array = zif_store_resolve (ZIF_STORE (store_local), name, cancellable, completion_local, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to resolve installed package %s: %s", name, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* get newest, ignore error */
	package = zif_package_array_get_newest (array, NULL);
	if (package != NULL) {
		/* we don't need to do the second part */
		zif_completion_done (completion);
		goto out;
	}

	/* clear array */
	g_ptr_array_unref (array);

	/* is available in this repo? */
	completion_local = zif_completion_get_child (completion);
	array = zif_store_resolve (ZIF_STORE (store), name, cancellable, completion_local, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to resolve installed package %s: %s", name, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* get newest, ignore error */
	package = zif_package_array_get_newest (array, NULL);
	if (package != NULL)
		goto out;

	/* we suck */
	g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
		     "failed to resolve installed package %s installed or in this repo", name);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (store_local != NULL)
		g_object_unref (store_local);
	return package;
}

/**
 * zif_store_remote_search_category:
 **/
static GPtrArray *
zif_store_remote_search_category (ZifStore *store, const gchar *group_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_names = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
	ZifPackage *package;
	const gchar *name;
	const gchar *location;
	guint i;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* does this repo have comps data? */
	location = zif_repo_md_get_location (remote->priv->md_comps);
	if (location == NULL) {
		/* empty array, as we want success */
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		goto out;
	}

	/* get package names for group */
	completion_local = zif_completion_get_child (completion);
	array_names = zif_repo_md_comps_get_packages_for_group (ZIF_REPO_MD_COMPS (remote->priv->md_comps),
								group_id, cancellable, completion_local, &error_local);
	if (array_names == NULL) {
		/* ignore when group isn't present, TODO: use GError code */
		if (g_str_has_prefix (error_local->message, "could not find group")) {
			array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
			g_error_free (error_local);
			goto out;
		}
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get packages for group %s: %s", group_id, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* setup completion */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, array_names->len);

	/* results array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* resolve names */
	for (i=0; i<array_names->len; i++) {
		name = g_ptr_array_index (array_names, i);

		/* completion */
		completion_loop = zif_completion_get_child (completion_local);
		package = zif_store_remote_search_category_resolve (store, name, cancellable, completion_loop, &error_local);
		if (package == NULL) {
			/* ignore when package isn't present */
			if (error_local->code == ZIF_STORE_ERROR_FAILED_TO_FIND) {
				g_clear_error (&error_local);
				egg_debug ("Failed to find %s installed or in repo %s", name, remote->priv->id);
				goto ignore_error;
			}

			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to get resolve %s for %s: %s", name, group_id, error_local->message);
			g_error_free (error_local);

			/* undo all our hard work */
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* add to array */
		g_ptr_array_add (array, package);
ignore_error:
		/* this section done */
		zif_completion_done (completion_local);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	if (array_names != NULL)
		g_ptr_array_unref (array_names);
	return array;
}

/**
 * zif_store_remote_search_group:
 **/
static GPtrArray *
zif_store_remote_search_group (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_primary_search_group (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), search, cancellable, completion_local, error);

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_remote_find_package:
 **/
static ZifPackage *
zif_store_remote_find_package (ZifStore *store, const gchar *package_id, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GPtrArray *array = NULL;
	ZifPackage *package = NULL;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* search with predicate, TODO: search version (epoch+release) */
	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_primary_find_package (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), package_id, cancellable, completion_local, &error_local);
	if (array == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to search: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* nothing */
	if (array->len == 0) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to find package");
		goto out;
	}

	/* more than one match */
	if (array->len > 1) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_MULTIPLE_MATCHES,
				     "more than one match");
		goto out;
	}

	/* return ref to package */
	package = g_object_ref (g_ptr_array_index (array, 0));
out:
	g_ptr_array_unref (array);
	return package;
}

/**
 * zif_store_remote_get_packages:
 **/
static GPtrArray *
zif_store_remote_get_packages (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 1);
	else
		zif_completion_set_number_steps (completion, 2);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	completion_local = zif_completion_get_child (completion);
	array = zif_repo_md_primary_get_packages (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), cancellable, completion_local, error);

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_remote_get_categories:
 **/
static GPtrArray *
zif_store_remote_get_categories (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	guint i, j;
	const gchar *location;
	GError *error_local = NULL;
	GPtrArray *array = NULL;
	GPtrArray *array_cats = NULL;
	GPtrArray *array_groups;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;
	ZifCompletion *completion_loop;
//	PkCategory *comps_category;
	PkCategory *group;
	PkCategory *category;
	PkCategory *category_tmp;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (remote->priv->id != NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* does this repo have comps data? */
	location = zif_repo_md_get_location (remote->priv->md_comps);
	if (location == NULL) {
		/* empty array, as we want success */
		array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
		goto out;
	}

	/* get list of categories */
	completion_local = zif_completion_get_child (completion);
	array_cats = zif_repo_md_comps_get_categories (ZIF_REPO_MD_COMPS (remote->priv->md_comps), cancellable, completion_local, &error_local);
	if (array_cats == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get categories: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* results array */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* no results */
	if (array_cats->len == 0)
		goto skip;

	/* setup steps */
	completion_local = zif_completion_get_child (completion);
	zif_completion_set_number_steps (completion_local, array_cats->len);

	/* get groups for categories */
	for (i=0; i<array_cats->len; i++) {
		category = g_ptr_array_index (array_cats, i);

		/* get the groups for this category */
		completion_loop = zif_completion_get_child (completion_local);
		array_groups = zif_repo_md_comps_get_groups_for_category (ZIF_REPO_MD_COMPS (remote->priv->md_comps),
									  pk_category_get_id (category), cancellable, completion_loop, &error_local);
		if (array_groups == NULL) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to get groups for %s: %s", pk_category_get_id (category), error_local->message);
			g_error_free (error_local);

			/* undo the work we've already done */
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* only add categories which have groups */
		if (array_groups->len > 0) {

			/* first, add the parent */
			g_ptr_array_add (array, g_object_ref (category));

			/* second, add the groups belonging to this parent */
			for (j=0; j<array_groups->len; j++) {
				group = g_ptr_array_index (array_groups, j);
				category_tmp = g_object_ref (group);
				g_ptr_array_add (array, category_tmp);
			}
		}

		/* this section done */
		zif_completion_done (completion_local);
	}
skip:
	/* this section done */
	zif_completion_done (completion);
out:
	if (array_cats != NULL)
		g_ptr_array_unref (array_cats);
	return array;
}

/**
 * zif_store_remote_get_updates:
 **/
static GPtrArray *
zif_store_remote_get_updates (ZifStore *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	ZifStore *store_local;
	GPtrArray *packages;
	GPtrArray *updates;
	GPtrArray *array = NULL;
	ZifPackage *package;
	ZifPackage *update;
	GError *error_local = NULL;
	guint i, j;
	gint val;
	const gchar *package_id;
	const gchar *package_id_update;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	ZifCompletion *completion_local;
	gchar **split;
	gchar **split_update;

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* get list of local packages */
	store_local = ZIF_STORE (zif_store_local_new ());
	completion_local = zif_completion_get_child (completion);
	packages = zif_store_get_packages (store_local, cancellable, completion_local, &error_local);
	if (packages == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to get local store: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
	egg_debug ("searching with %i packages", packages->len);

	/* this section done */
	zif_completion_done (completion);

	/* create array for packages to update */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	/* find each one in a remote repo */
	for (i=0; i<packages->len; i++) {
		package = ZIF_PACKAGE (g_ptr_array_index (packages, i));
		package_id = zif_package_get_id (package);

		/* find package name in repo */
		completion_local = zif_completion_get_child (completion);
		split = pk_package_id_split (package_id);
		updates = zif_repo_md_primary_resolve (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), split[PK_PACKAGE_ID_NAME], cancellable, completion_local, NULL);
		g_strfreev (split);
		if (updates == NULL) {
			egg_debug ("not found %s", split[PK_PACKAGE_ID_NAME]);
			continue;
		}

		/* find updates */
		for (j=0; j<updates->len; j++) {
			update = ZIF_PACKAGE (g_ptr_array_index (updates, j));

			/* newer? */
			val = zif_package_compare (update, package);
			if (val > 0) {
				package_id_update = zif_package_get_id (update);
				split = pk_package_id_split (package_id);
				split_update = pk_package_id_split (package_id_update);
				egg_debug ("*** update %s from %s to %s", split[PK_PACKAGE_ID_NAME], split[PK_PACKAGE_ID_VERSION], split_update[PK_PACKAGE_ID_VERSION]);
				g_strfreev (split);
				g_strfreev (split_update);
				g_ptr_array_add (array, g_object_ref (update));
			}
		}
		g_ptr_array_unref (updates);
	}

	/* this section done */
	zif_completion_done (completion);

	g_ptr_array_unref (packages);
	g_object_unref (store_local);
out:
	return array;
}

/**
 * zif_store_remote_what_provides:
 **/
static GPtrArray *
zif_store_remote_what_provides (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		ret = zif_store_remote_load_metadata (remote, cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
	//FIXME: load other MD
out:
	return g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * zif_store_remote_search_file:
 **/
static GPtrArray *
zif_store_remote_search_file (ZifStore *store, const gchar *search, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	GPtrArray *pkgids;
	GPtrArray *array = NULL;
	GPtrArray *tmp;
	ZifPackage *package;
	ZifCompletion *completion_local;
	const gchar *pkgid;
	guint i, j;
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);

	/* not locked */
	ret = zif_lock_is_locked (remote->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* setup completion */
	if (remote->priv->loaded_metadata)
		zif_completion_set_number_steps (completion, 2);
	else
		zif_completion_set_number_steps (completion, 3);

	/* load metadata */
	if (!remote->priv->loaded_metadata) {
		completion_local = zif_completion_get_child (completion);
		ret = zif_store_remote_load_metadata (remote, cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load xml: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}

		/* this section done */
		zif_completion_done (completion);
	}

	/* gets a list of pkgId's that match this file */
	completion_local = zif_completion_get_child (completion);
	pkgids = zif_repo_md_filelists_search_file (ZIF_REPO_MD_FILELISTS (remote->priv->md_filelists), search, cancellable, completion_local, &error_local);
	if (pkgids == NULL) {
		g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
			     "failed to load get list of pkgids: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* this section done */
	zif_completion_done (completion);

	/* resolve the pkgId to a set of packages */
	array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i=0; i<pkgids->len; i++) {
		pkgid = g_ptr_array_index (pkgids, i);

		/* get the results (should just be one) */
		completion_local = zif_completion_get_child (completion);
		tmp = zif_repo_md_primary_search_pkgid (ZIF_REPO_MD_PRIMARY (remote->priv->md_primary), pkgid, cancellable, completion_local, &error_local);
		if (tmp == NULL) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED_TO_FIND,
				     "failed to resolve pkgId to package: %s", error_local->message);
			g_error_free (error_local);
			/* free what we've collected already */
			g_ptr_array_unref (array);
			array = NULL;
			goto out;
		}

		/* add to main array */
		for (j=0; j<tmp->len; j++) {
			package = g_ptr_array_index (tmp, j);
			g_ptr_array_add (array, g_object_ref (package));
		}

		/* free temp array */
		g_ptr_array_unref (tmp);
	}

	/* this section done */
	zif_completion_done (completion);
out:
	return array;
}

/**
 * zif_store_remote_is_devel:
 * @store: the #ZifStoreRemote object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Finds out if the repository is a development repository.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_remote_is_devel (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (store->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* do tests */
	if (g_str_has_suffix (store->priv->id, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-testing"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-debug"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-development"))
		return TRUE;
	if (g_str_has_suffix (store->priv->id, "-source"))
		return TRUE;
out:
	return FALSE;
}

/**
 * zif_store_remote_get_id:
 * @store: the #ZifStoreRemote object
 *
 * Get the id of this repository.
 *
 * Return value: The repository id, e.g. "fedora"
 **/
static const gchar *
zif_store_remote_get_id (ZifStore *store)
{
	ZifStoreRemote *remote = ZIF_STORE_REMOTE (store);
	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	return remote->priv->id;
}

/**
 * zif_store_remote_get_name:
 * @store: the #ZifStoreRemote object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Get the name of this repository.
 *
 * Return value: The repository name, e.g. "Fedora"
 *
 * Since: 0.0.1
 **/
const gchar *
zif_store_remote_get_name (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), NULL);
	g_return_val_if_fail (store->priv->id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* not locked */
	ret = zif_lock_is_locked (store->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return store->priv->name_expanded;
}

/**
 * zif_store_remote_get_enabled:
 * @store: the #ZifStoreRemote object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Find out if this repository is enabled or not.
 *
 * Return value: %TRUE or %FALSE
 *
 * Since: 0.0.1
 **/
gboolean
zif_store_remote_get_enabled (ZifStoreRemote *store, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	GError *error_local = NULL;
	gboolean ret;

	g_return_val_if_fail (ZIF_IS_STORE_REMOTE (store), FALSE);
	g_return_val_if_fail (store->priv->id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not locked */
	ret = zif_lock_is_locked (store->priv->lock, NULL);
	if (!ret) {
		g_set_error_literal (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_NOT_LOCKED,
				     "not locked");
		goto out;
	}

	/* if not already loaded, load */
	if (!store->priv->loaded) {
		ret = zif_store_remote_load (ZIF_STORE (store), cancellable, completion, &error_local);
		if (!ret) {
			g_set_error (error, ZIF_STORE_ERROR, ZIF_STORE_ERROR_FAILED,
				     "failed to load store file: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}
out:
	return store->priv->enabled;
}

/**
 * zif_store_remote_file_monitor_cb:
 **/
static void
zif_store_remote_file_monitor_cb (ZifMonitor *monitor, ZifStoreRemote *store)
{
	/* free invalid data */
	g_free (store->priv->id);
	g_free (store->priv->name);
	g_free (store->priv->name_expanded);
	g_free (store->priv->repo_filename);
	g_ptr_array_set_size (store->priv->baseurls, 0);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);

	store->priv->loaded = FALSE;
	store->priv->loaded_metadata = FALSE;
	store->priv->enabled = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->name_expanded = NULL;
	store->priv->repo_filename = NULL;
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;

	egg_debug ("store file changed");
}

/**
 * zif_store_remote_finalize:
 **/
static void
zif_store_remote_finalize (GObject *object)
{
	ZifStoreRemote *store;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_STORE_REMOTE (object));
	store = ZIF_STORE_REMOTE (object);

	g_free (store->priv->id);
	g_free (store->priv->name);
	g_free (store->priv->name_expanded);
	g_free (store->priv->repo_filename);
	g_free (store->priv->mirrorlist);
	g_free (store->priv->metalink);
	g_free (store->priv->cache_dir);
	g_free (store->priv->repomd_filename);
	g_free (store->priv->directory);

	g_object_unref (store->priv->md_primary);
	g_object_unref (store->priv->md_filelists);
	g_object_unref (store->priv->md_comps);
	g_object_unref (store->priv->md_metalink);
	g_object_unref (store->priv->md_mirrorlist);
	g_object_unref (store->priv->config);
	g_object_unref (store->priv->monitor);
	g_object_unref (store->priv->lock);

	g_ptr_array_unref (store->priv->baseurls);

	G_OBJECT_CLASS (zif_store_remote_parent_class)->finalize (object);
}

/**
 * zif_store_remote_class_init:
 **/
static void
zif_store_remote_class_init (ZifStoreRemoteClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ZifStoreClass *store_class = ZIF_STORE_CLASS (klass);
	object_class->finalize = zif_store_remote_finalize;

	/* map */
	store_class->load = zif_store_remote_load;
	store_class->clean = zif_store_remote_clean;
	store_class->refresh = zif_store_remote_refresh;
	store_class->search_name = zif_store_remote_search_name;
	store_class->search_category = zif_store_remote_search_category;
	store_class->search_details = zif_store_remote_search_details;
	store_class->search_group = zif_store_remote_search_group;
	store_class->search_file = zif_store_remote_search_file;
	store_class->resolve = zif_store_remote_resolve;
	store_class->what_provides = zif_store_remote_what_provides;
	store_class->get_packages = zif_store_remote_get_packages;
	store_class->get_updates = zif_store_remote_get_updates;
	store_class->find_package = zif_store_remote_find_package;
	store_class->get_categories = zif_store_remote_get_categories;
	store_class->get_id = zif_store_remote_get_id;
	store_class->print = zif_store_remote_print;

	g_type_class_add_private (klass, sizeof (ZifStoreRemotePrivate));
}

/**
 * zif_store_remote_init:
 **/
static void
zif_store_remote_init (ZifStoreRemote *store)
{
	gchar *cache_dir = NULL;
	guint i;
	GError *error = NULL;
	ZifRepoMd *md;

	store->priv = ZIF_STORE_REMOTE_GET_PRIVATE (store);
	store->priv->loaded = FALSE;
	store->priv->loaded_metadata = FALSE;
	store->priv->id = NULL;
	store->priv->name = NULL;
	store->priv->directory = NULL;
	store->priv->name_expanded = NULL;
	store->priv->enabled = FALSE;
	store->priv->repo_filename = NULL;
	store->priv->baseurls = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
	store->priv->mirrorlist = NULL;
	store->priv->metalink = NULL;
	store->priv->config = zif_config_new ();
	store->priv->monitor = zif_monitor_new ();
	store->priv->lock = zif_lock_new ();
	store->priv->md_filelists = ZIF_REPO_MD (zif_repo_md_filelists_new ());
	store->priv->md_primary = ZIF_REPO_MD (zif_repo_md_primary_new ());
	store->priv->md_metalink = ZIF_REPO_MD (zif_repo_md_metalink_new ());
	store->priv->md_mirrorlist = ZIF_REPO_MD (zif_repo_md_mirrorlist_new ());
	store->priv->md_comps = ZIF_REPO_MD (zif_repo_md_comps_new ());
	store->priv->parser_type = ZIF_REPO_MD_TYPE_UNKNOWN;
	store->priv->parser_section = ZIF_STORE_REMOTE_PARSER_SECTION_UNKNOWN;
	g_signal_connect (store->priv->monitor, "changed", G_CALLBACK (zif_store_remote_file_monitor_cb), store);

	/* get cache */
	cache_dir = zif_config_get_string (store->priv->config, "cachedir", &error);
	if (cache_dir == NULL) {
		egg_warning ("failed to get cachedir: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* expand */
	store->priv->cache_dir = zif_config_expand_substitutions (store->priv->config, cache_dir, &error);
	if (store->priv->cache_dir == NULL) {
		egg_warning ("failed to get expand substitutions: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* set MD type on each repo */
	for (i=0; i<ZIF_REPO_MD_TYPE_UNKNOWN; i++) {
		md = zif_store_remote_get_md_from_type (store, i);
		if (md == NULL)
			continue;

		/* set parent reference */
		zif_repo_md_set_store_remote (md, store);

		/* set MD type */
		zif_repo_md_set_mdtype (md, i);
	}
out:
	g_free (cache_dir);
}

/**
 * zif_store_remote_new:
 *
 * Return value: A new #ZifStoreRemote class instance.
 *
 * Since: 0.0.1
 **/
ZifStoreRemote *
zif_store_remote_new (void)
{
	ZifStoreRemote *store;
	store = g_object_new (ZIF_TYPE_STORE_REMOTE, NULL);
	return ZIF_STORE_REMOTE (store);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include "zif-groups.h"

void
zif_store_remote_test (EggTest *test)
{
	ZifGroups *groups;
	ZifStoreRemote *store;
	ZifStoreLocal *store_local;
	ZifConfig *config;
	ZifLock *lock;
	ZifCompletion *completion;
	GPtrArray *array;
	gboolean ret;
	GError *error = NULL;
	const gchar *id;
	PkCategory *category;
	guint i;

	if (!egg_test_start (test, "ZifStoreRemote"))
		return;

	/* set this up as dummy */
	config = zif_config_new ();
	zif_config_set_filename (config, "../test/etc/yum.conf", NULL);

	/* use completion object */
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get store");
	store = zif_store_remote_new ();
	egg_test_assert (test, store != NULL);

	/************************************************************/
	egg_test_title (test, "get lock");
	lock = zif_lock_new ();
	egg_test_assert (test, lock != NULL);

	/************************************************************/
	egg_test_title (test, "lock");
	ret = zif_lock_set_locked (lock, NULL, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "load from a file");
	zif_completion_reset (completion);
	ret = zif_store_remote_set_from_file (store, "../test/repos/fedora.repo", "fedora", NULL, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/* setup state */
	groups = zif_groups_new ();
	zif_groups_set_mapping_file (groups, "../test/share/yum-comps-groups.conf", NULL);
	store_local = zif_store_local_new ();
	zif_store_local_set_prefix (store_local, "/", NULL);

	/************************************************************/
	egg_test_title (test, "get updates");
	zif_completion_reset (completion);
	array = zif_store_remote_get_updates (ZIF_STORE (store), NULL, completion, &error);
	if (array == NULL)
		egg_test_failed (test, "no data: %s", error->message);
	else if (array->len > 0)
		egg_test_success (test, NULL);
	else
		egg_test_success (test, "no updates"); //TODO: failure
	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "is devel");
	ret = zif_store_remote_is_devel (store, NULL, completion, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = zif_store_remote_get_enabled (store, NULL, completion, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get id");
	id = zif_store_get_id (ZIF_STORE (store));
	if (egg_strequal (id, "fedora"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid id '%s'", id);

	/************************************************************/
	egg_test_title (test, "get name");
	id = zif_store_remote_get_name (store, NULL, completion, NULL);
	if (egg_strequal (id, "Fedora 11 - i386"))
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "invalid name '%s'", id);

	/************************************************************/
	egg_test_title (test, "load metadata");
	zif_completion_reset (completion);
	ret = zif_store_remote_load (ZIF_STORE (store), NULL, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load metadata '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "resolve");
	zif_completion_reset (completion);
	array = zif_store_remote_resolve (ZIF_STORE (store), "kernel", NULL, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to resolve '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len >= 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "search name");
	zif_completion_reset (completion);
	array = zif_store_remote_search_name (ZIF_STORE (store), "power-manager", NULL, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search name '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "search name correct number");
	if (array->len == 3)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "search details");
	zif_completion_reset (completion);
	array = zif_store_remote_search_details (ZIF_STORE (store), "browser plugin", NULL, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search details '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "search details correct number");
	if (array->len == 5)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "search file");
	zif_completion_reset (completion);
	array = zif_store_remote_search_file (ZIF_STORE (store), "/usr/bin/gnome-power-manager", NULL, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to search details '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "search file correct number");
	if (array->len == 1)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "set disabled");
	ret = zif_store_remote_set_enabled (store, FALSE, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to disable '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = zif_store_remote_get_enabled (store, NULL, completion, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set enabled");
	ret = zif_store_remote_set_enabled (store, TRUE, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to enable '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "is enabled");
	ret = zif_store_remote_get_enabled (store, NULL, completion, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get packages");
	zif_completion_reset (completion);
	array = zif_store_remote_get_packages (ZIF_STORE (store), NULL, completion, &error);
	if (array != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to get packages '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "correct number");
	if (array->len > 10000)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect length %i", array->len);

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "get categories");
	zif_completion_reset (completion);
	array = zif_store_remote_get_categories (ZIF_STORE (store), NULL, completion, &error);
	if (array == NULL)
		egg_test_failed (test, "no data: %s", error->message);
	else if (array->len > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "no categories"); //TODO: failure

	/* get first object */
	category = g_ptr_array_index (array, 0);

	/************************************************************/
	egg_test_title (test, "test parent_id");
	if (pk_category_get_parent_id (category) == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect data: %s", pk_category_get_parent_id (category));

	/************************************************************/
	egg_test_title (test, "test cat_id");
	if (g_strcmp0 (pk_category_get_id (category), "language-support") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect data: %s", pk_category_get_id (category));

	/************************************************************/
	egg_test_title (test, "test name");
	if (g_strcmp0 (pk_category_get_name (category), "Languages") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "incorrect data: %s", pk_category_get_name (category));

	g_ptr_array_unref (array);

	/************************************************************/
	egg_test_title (test, "search category");
	zif_completion_reset (completion);
	array = zif_store_remote_search_category (ZIF_STORE (store), "admin-tools", NULL, completion, &error);
	if (array == NULL)
		egg_test_failed (test, "no data: %s", error->message);
	else if (array->len > 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "no results");

	g_ptr_array_unref (array);

	g_object_unref (store);
	g_object_unref (config);
	g_object_unref (lock);
	g_object_unref (completion);
	g_object_unref (groups);
	g_object_unref (store_local);

	egg_test_end (test);
}
#endif

