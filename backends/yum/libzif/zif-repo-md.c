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
 * SECTION:zif-repo-md
 * @short_description: Metadata file common functionality
 *
 * This provides an abstract metadata class.
 * It is implemented by #ZifRepoMdFilelists, #ZifRepoMdMaster and #ZifRepoMdPrimary.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdlib.h>

#include "zif-utils.h"
#include "zif-repo-md.h"
#include "zif-config.h"

#include "egg-debug.h"
#include "egg-string.h"

#define ZIF_REPO_MD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_REPO_MD, ZifRepoMdPrivate))

/**
 * ZifRepoMdPrivate:
 *
 * Private #ZifRepoMd data
 **/
struct _ZifRepoMdPrivate
{
	gboolean		 loaded;
	gchar			*id;			/* fedora */
	gchar			*filename;		/* /var/cache/yum/fedora/repo.sqlite.bz2 */
	gchar			*filename_uncompressed;	/* /var/cache/yum/fedora/repo.sqlite */
	guint			 timestamp;
	gchar			*location;		/* repodata/35d817e-primary.sqlite.bz2 */
	gchar			*checksum;		/* of compressed file */
	gchar			*checksum_uncompressed;	/* of uncompressed file */
	GChecksumType		 checksum_type;
	ZifRepoMdType		 type;
	ZifStoreRemote		*remote;
	ZifConfig		*config;
};

G_DEFINE_TYPE (ZifRepoMd, zif_repo_md, G_TYPE_OBJECT)

/**
 * zif_repo_md_get_id:
 * @md: the #ZifRepoMd object
 *
 * Gets the md identifier, usually the repo name.
 *
 * Return value: the repo id.
 **/
const gchar *
zif_repo_md_get_id (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->id;
}

/**
 * zif_repo_md_get_filename:
 * @md: the #ZifRepoMd object
 *
 * Gets the compressed filename of the repo.
 *
 * Return value: the filename
 **/
const gchar *
zif_repo_md_get_filename (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->filename;
}

/**
 * zif_repo_md_get_location:
 * @md: the #ZifRepoMd object
 *
 * Gets the location of the repo.
 *
 * Return value: the location
 **/
const gchar *
zif_repo_md_get_location (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->location;
}

/**
 * zif_repo_md_get_mdtype:
 * @md: the #ZifRepoMd object
 *
 * Gets the type of the repo.
 *
 * Return value: the type
 **/
ZifRepoMdType
zif_repo_md_get_mdtype (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), ZIF_REPO_MD_TYPE_UNKNOWN);
	return md->priv->type;
}

/**
 * zif_repo_md_get_filename_uncompressed:
 * @md: the #ZifRepoMd object
 *
 * Gets the uncompressed filename of the repo.
 *
 * Return value: the filename
 **/
const gchar *
zif_repo_md_get_filename_uncompressed (ZifRepoMd *md)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), NULL);
	return md->priv->filename_uncompressed;
}

/**
 * zif_repo_md_set_filename:
 * @md: the #ZifRepoMd object
 * @filename: the base filename, e.g. "master.xml.bz2"
 *
 * Sets the filename of the compressed file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_filename (ZifRepoMd *md, const gchar *filename)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->filename == NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* this is the compressed name */
	md->priv->filename = g_strdup (filename);

	/* this is the uncompressed name */
	md->priv->filename_uncompressed = zif_file_get_uncompressed_name (filename);

	return TRUE;
}

/**
 * zif_repo_md_set_timestamp:
 * @md: the #ZifRepoMd object
 * @timestamp: the timestamp value
 *
 * Sets the timestamp of the compressed file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_timestamp (ZifRepoMd *md, guint timestamp)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->timestamp == 0, FALSE);
	g_return_val_if_fail (timestamp != 0, FALSE);

	/* save new value */
	md->priv->timestamp = timestamp;
	return TRUE;
}

/**
 * zif_repo_md_set_location:
 * @md: the #ZifRepoMd object
 * @location: the location
 *
 * Sets the location of the compressed file, e.g. "repodata/35d817e-primary.sqlite.bz2"
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_location (ZifRepoMd *md, const gchar *location)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->location == NULL, FALSE);
	g_return_val_if_fail (location != NULL, FALSE);

	/* save new value */
	md->priv->location = g_strdup (location);
	return TRUE;
}

/**
 * zif_repo_md_set_checksum:
 * @md: the #ZifRepoMd object
 * @checksum: the checksum value
 *
 * Sets the checksum of the compressed file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_checksum (ZifRepoMd *md, const gchar *checksum)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->checksum == NULL, FALSE);
	g_return_val_if_fail (checksum != NULL, FALSE);

	/* save new value */
	md->priv->checksum = g_strdup (checksum);
	return TRUE;
}

/**
 * zif_repo_md_set_checksum_uncompressed:
 * @md: the #ZifRepoMd object
 * @checksum_uncompressed: the uncompressed checksum value
 *
 * Sets the checksum of the uncompressed file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_checksum_uncompressed (ZifRepoMd *md, const gchar *checksum_uncompressed)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->checksum_uncompressed == NULL, FALSE);
	g_return_val_if_fail (checksum_uncompressed != NULL, FALSE);

	/* save new value */
	md->priv->checksum_uncompressed = g_strdup (checksum_uncompressed);
	return TRUE;
}

/**
 * zif_repo_md_set_checksum_type:
 * @md: the #ZifRepoMd object
 * @checksum_type: the checksum type
 *
 * Sets the checksum_type of the files.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_checksum_type (ZifRepoMd *md, GChecksumType checksum_type)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->checksum_type == 0, FALSE);

	/* save new value */
	md->priv->checksum_type = checksum_type;
	return TRUE;
}

/**
 * zif_repo_md_set_mdtype:
 * @md: the #ZifRepoMd object
 * @type: the metadata type
 *
 * Sets the type of the metadata, e.g. ZIF_REPO_MD_TYPE_FILELISTS_DB.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_mdtype (ZifRepoMd *md, ZifRepoMdType type)
{
	gboolean ret = TRUE;

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->type == ZIF_REPO_MD_TYPE_UNKNOWN, FALSE);
	g_return_val_if_fail (type != ZIF_REPO_MD_TYPE_UNKNOWN, FALSE);

	/* save new value */
	md->priv->type = type;

	/* metalink is not specified in the repomd.xml file */
	if (type == ZIF_REPO_MD_TYPE_METALINK) {
		zif_repo_md_set_location (md, "metalink.xml");
		goto out;
	}

	/* mirrorlist is not specified in the repomd.xml file */
	if (type == ZIF_REPO_MD_TYPE_MIRRORLIST) {
		zif_repo_md_set_location (md, "mirrorlist.txt");
		goto out;
	}

	/* check we've got the needed data */
	if (md->priv->location != NULL && (md->priv->checksum == NULL || md->priv->timestamp == 0)) {
		egg_warning ("cannot load md for %s (loc=%s, checksum=%s, checksum_open=%s, timestamp=%i)",
			     zif_repo_md_type_to_text (type), md->priv->location,
			     md->priv->checksum, md->priv->checksum_uncompressed, md->priv->timestamp);
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * zif_repo_md_set_id:
 * @md: the #ZifRepoMd object
 * @id: the repository id, e.g. "fedora"
 *
 * Sets the repository ID for this metadata.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_id (ZifRepoMd *md, const gchar *id)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->id == NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	md->priv->id = g_strdup (id);
	return TRUE;
}

/**
 * zif_repo_md_set_id:
 * @md: the #ZifRepoMd object
 * @id: the repository id, e.g. "fedora"
 *
 * Sets the repository ID for this metadata.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_set_store_remote (ZifRepoMd *md, ZifStoreRemote *remote)
{
	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->remote == NULL, FALSE);
	g_return_val_if_fail (remote != NULL, FALSE);

	/* do not take a reference, else the parent device never goes away */
	md->priv->remote = remote;
	return TRUE;
}

/**
 * zif_repo_md_delete_file:
 **/
static gboolean
zif_repo_md_delete_file (const gchar *filename)
{
	gint retval;
	gboolean ret;

	/* file exists? */
	ret = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (!ret)
		goto out;

	egg_warning ("deleting %s", filename);

	/* remove */
	retval = g_unlink (filename);
	if (retval != 0) {
		egg_warning ("failed to delete %s", filename);
		ret = FALSE;
	}
out:
	return ret;
}

/**
 * zif_repo_md_load:
 * @md: the #ZifRepoMd object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Load the metadata store.
 *
 * - Check compressed file
 *   if invalid:
 *       delete_it()
 *       if online:
 *           download_it()
 *           if failure:
 *               abort
 *           check_it()
 *           if failure:
 *               abort
 *       else
 *           abort
 *
 * - Check uncompressed file
 *   if invalid:
 *       delete_it()
 *       decompress_it()
 *           if failure:
 *               abort()
 *       check_it()
 *       if failure:
 *           abort
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_load (ZifRepoMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret;
	gboolean uncompressed_check;
	GError *error_local = NULL;
	ZifRepoMdClass *klass = ZIF_REPO_MD_GET_CLASS (md);
	ZifCompletion *completion_local;

	/* no support */
	if (klass->load == NULL) {
		g_set_error_literal (error, 1, 0, "operation cannot be performed on this md");
		return FALSE;
	}

	/* setup completion */
	zif_completion_set_number_steps (completion, 3);

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);

	/* optimise: if uncompressed file is okay, then don't even check the compressed file */
	uncompressed_check = zif_repo_md_file_check (md, TRUE, &error_local);
	if (uncompressed_check) {
		zif_completion_done (completion);
		goto skip_compressed_check;
	}

	/* display any warning */
	egg_warning ("failed checksum for uncompressed: %s", error_local->message);
	g_clear_error (&error_local);

	/* check compressed file */
	ret = zif_repo_md_file_check (md, FALSE, &error_local);
	if (!ret) {
		egg_warning ("failed checksum for compressed: %s", error_local->message);
		g_clear_error (&error_local);

		/* delete file if it exists */
		zif_repo_md_delete_file (md->priv->filename);

		/* if not online, then this is fatal */
		ret = zif_config_get_boolean (md->priv->config, "network", NULL);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to check %s checksum for %s and offline",
						      zif_repo_md_type_to_text (md->priv->type), md->priv->id);
			goto out;
		}

		/* TODO: download file */
		//ret = download (&error_local)
		if (!ret) {
			g_set_error (error, 1, 0, "failed to download missing compressed file: %s", error_local->message);
			goto out;
		}

		/* check newly downloaded compressed file */
		ret = zif_repo_md_file_check (md, FALSE, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed checksum on downloaded file: %s", error_local->message);
			goto out;
		}
	}

	/* this section done */
	zif_completion_done (completion);

	/* check uncompressed file */
	if (!uncompressed_check) {

		/* delete file if it exists */
		zif_repo_md_delete_file (md->priv->filename_uncompressed);

		/* decompress file */
		egg_debug ("decompressing file");
		completion_local = zif_completion_get_child (completion);
		ret = zif_file_decompress (md->priv->filename, md->priv->filename_uncompressed,
					   cancellable, completion_local, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to decompress: %s", error_local->message);
			goto out;
		}

		/* check newly uncompressed file */
		ret = zif_repo_md_file_check (md, TRUE, &error_local);
		if (!ret) {
			g_set_error (error, 1, 0, "failed checksum on decompressed file: %s", error_local->message);
			goto out;
		}
	}

skip_compressed_check:

	/* this section done */
	zif_completion_done (completion);

	/* do subclassed load */
	completion_local = zif_completion_get_child (completion);
	ret = klass->load (md, cancellable, completion_local, error);

	/* this section done */
	zif_completion_done (completion);
out:
	return ret;
}

/**
 * zif_repo_md_unload:
 * @md: the #ZifRepoMd object
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Unload the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_unload (ZifRepoMd *md, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	ZifRepoMdClass *klass = ZIF_REPO_MD_GET_CLASS (md);

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);

	/* no support */
	if (klass->unload == NULL) {
		g_set_error_literal (error, 1, 0, "operation cannot be performed on this md");
		return FALSE;
	}

	return klass->unload (md, cancellable, completion, error);
}

/**
 * zif_repo_md_clean:
 * @md: the #ZifRepoMd object
 * @error: a #GError which is used on failure, or %NULL
 *
 * Clean the metadata store.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_clean (ZifRepoMd *md, GError **error)
{
	gboolean ret = FALSE;
	gboolean exists;
	const gchar *filename;
	GFile *file;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);

	/* get filename */
	filename = zif_repo_md_get_filename (md);
	if (filename == NULL) {
		g_set_error (error, 1, 0, "failed to get filename for %s", zif_repo_md_type_to_text (md->priv->type));
		ret = FALSE;
		goto out;
	}

	/* file does not exist */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to delete metadata file %s: %s", filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* get filename */
	filename = zif_repo_md_get_filename_uncompressed (md);
	if (filename == NULL) {
		g_set_error (error, 1, 0, "failed to get uncompressed filename for %s", zif_repo_md_type_to_text (md->priv->type));
		ret = FALSE;
		goto out;
	}

	/* file does not exist */
	exists = g_file_test (filename, G_FILE_TEST_EXISTS);
	if (exists) {
		file = g_file_new_for_path (filename);
		ret = g_file_delete (file, NULL, &error_local);
		g_object_unref (file);
		if (!ret) {
			g_set_error (error, 1, 0, "failed to delete metadata file %s: %s", filename, error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* okay */
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_repo_md_type_to_text:
 **/
const gchar *
zif_repo_md_type_to_text (ZifRepoMdType type)
{
	if (type == ZIF_REPO_MD_TYPE_FILELISTS)
		return "filelists";
	if (type == ZIF_REPO_MD_TYPE_FILELISTS_DB)
		return "filelists_db";
	if (type == ZIF_REPO_MD_TYPE_PRIMARY)
		return "primary";
	if (type == ZIF_REPO_MD_TYPE_PRIMARY_DB)
		return "primary_db";
	if (type == ZIF_REPO_MD_TYPE_OTHER)
		return "other";
	if (type == ZIF_REPO_MD_TYPE_OTHER_DB)
		return "other_db";
	if (type == ZIF_REPO_MD_TYPE_COMPS)
		return "group";
	if (type == ZIF_REPO_MD_TYPE_COMPS_XML)
		return "group_gz";
	if (type == ZIF_REPO_MD_TYPE_METALINK)
		return "metalink";
	if (type == ZIF_REPO_MD_TYPE_MIRRORLIST)
		return "mirrorlist";
	if (type == ZIF_REPO_MD_TYPE_PRESTODELTA)
		return "prestodelta";
	if (type == ZIF_REPO_MD_TYPE_UPDATEINFO)
		return "updateinfo";
	return "unknown";
}

/**
 * zif_repo_md_file_check:
 * @md: the #ZifRepoMd object
 * @use_uncompressed: If we should check only the uncompresed version
 * @error: a #GError which is used on failure, or %NULL
 *
 * Check the metadata files to make sure they are valid.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_repo_md_file_check (ZifRepoMd *md, gboolean use_uncompressed, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *data = NULL;
	gchar *checksum = NULL;
	const gchar *filename;
	const gchar *checksum_wanted;
	gsize length;

	g_return_val_if_fail (ZIF_IS_REPO_MD (md), FALSE);
	g_return_val_if_fail (md->priv->id != NULL, FALSE);

	/* metalink has no checksum... */
	if (md->priv->type == ZIF_REPO_MD_TYPE_METALINK ||
	    md->priv->type == ZIF_REPO_MD_TYPE_MIRRORLIST) {
		egg_debug ("skipping checksum check on %s", zif_repo_md_type_to_text (md->priv->type));
		ret = TRUE;
		goto out;
	}

	/* get correct filename */
	if (use_uncompressed)
		filename = md->priv->filename_uncompressed;
	else
		filename = md->priv->filename;

	/* get contents */
	ret = g_file_get_contents (filename, &data, &length, &error_local);
	if (!ret) {
		g_set_error (error, 1, 0, "failed to get contents of %s: %s", filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the one we want */
	if (use_uncompressed)
		checksum_wanted = md->priv->checksum_uncompressed;
	else
		checksum_wanted = md->priv->checksum;

	/* no checksum set */
	if (checksum_wanted == NULL) {
		g_set_error (error, 1, 0, "checksum not set for %s", filename);
		ret = FALSE;
		goto out;
	}

	/* compute checksum */
	checksum = g_compute_checksum_for_data (md->priv->checksum_type, (guchar*) data, length);

	/* matches? */
	ret = (g_strcmp0 (checksum, checksum_wanted) == 0);
	if (!ret) {
		g_set_error (error, 1, 0, "checksum incorrect, wanted %s, got %s for %s", checksum_wanted, checksum, filename);
		goto out;
	}
	egg_debug ("%s checksum correct (%s)", filename, checksum_wanted);
out:
	g_free (data);
	g_free (checksum);
	return ret;
}

/**
 * zif_repo_md_finalize:
 **/
static void
zif_repo_md_finalize (GObject *object)
{
	ZifRepoMd *md;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_REPO_MD (object));
	md = ZIF_REPO_MD (object);

	g_free (md->priv->id);
	g_free (md->priv->filename);
	g_free (md->priv->location);
	g_free (md->priv->checksum);
	g_free (md->priv->checksum_uncompressed);

	g_object_unref (md->priv->config);

	G_OBJECT_CLASS (zif_repo_md_parent_class)->finalize (object);
}

/**
 * zif_repo_md_class_init:
 **/
static void
zif_repo_md_class_init (ZifRepoMdClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_repo_md_finalize;
	g_type_class_add_private (klass, sizeof (ZifRepoMdPrivate));
}

/**
 * zif_repo_md_init:
 **/
static void
zif_repo_md_init (ZifRepoMd *md)
{
	md->priv = ZIF_REPO_MD_GET_PRIVATE (md);
	md->priv->type = ZIF_REPO_MD_TYPE_UNKNOWN;
	md->priv->loaded = FALSE;
	md->priv->id = NULL;
	md->priv->filename = NULL;
	md->priv->timestamp = 0;
	md->priv->location = NULL;
	md->priv->checksum = NULL;
	md->priv->checksum_uncompressed = NULL;
	md->priv->checksum_type = 0;
	md->priv->remote = NULL;
	md->priv->config = zif_config_new ();
}

/**
 * zif_repo_md_new:
 *
 * Return value: A new #ZifRepoMd class instance.
 **/
ZifRepoMd *
zif_repo_md_new (void)
{
	ZifRepoMd *md;
	md = g_object_new (ZIF_TYPE_REPO_MD, NULL);
	return ZIF_REPO_MD (md);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
zif_repo_md_test (EggTest *test)
{
	ZifRepoMd *md;
	gboolean ret;
	GError *error = NULL;
	GCancellable *cancellable;
	ZifCompletion *completion;

	if (!egg_test_start (test, "ZifRepoMd"))
		return;

	/* use */
	cancellable = g_cancellable_new ();
	completion = zif_completion_new ();

	/************************************************************/
	egg_test_title (test, "get store_remote md");
	md = zif_repo_md_new ();
	egg_test_assert (test, md != NULL);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, !md->priv->loaded);

	/************************************************************/
	egg_test_title (test, "set id");
	ret = zif_repo_md_set_id (md, "fedora");
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set");

	/************************************************************/
	egg_test_title (test, "load");
	ret = zif_repo_md_load (md, cancellable, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "loaded");
	egg_test_assert (test, md->priv->loaded);

	g_object_unref (md);
	g_object_unref (cancellable);
	g_object_unref (completion);

	egg_test_end (test);
}
#endif

