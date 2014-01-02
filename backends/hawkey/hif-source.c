/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Most of this code was taken from Zif, libzif/zif-repos.c
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <pk-backend.h>

#include <librepo/librepo.h>
#include <hawkey/util.h>

#include "hif-package.h"
#include "hif-source.h"
#include "hif-utils.h"

struct HifSource {
	gboolean	 enabled;
	gboolean	 gpgcheck;
	gchar		*filename;
	gchar		*id;
	gchar		*location;	/* /var/cache/PackageKit/metadata/fedora */
	gchar		*location_tmp;	/* /var/cache/PackageKit/metadata/fedora.tmp */
	gint64		 timestamp;
	GKeyFile	*keyfile;
	HyRepo		 repo;
	LrHandle	*repo_handle;
	LrResult	*repo_result;
	LrUrlVars	*urlvars;
};

/**
 * hif_source_free:
 */
void
hif_source_free (HifSource *src)
{
	g_free (src->filename);
	g_free (src->id);
	g_free (src->location_tmp);
	g_free (src->location);
	if (src->repo_result != NULL)
		lr_result_free (src->repo_result);
	if (src->repo_handle != NULL)
		lr_handle_free (src->repo_handle);
	if (src->repo != NULL)
		hy_repo_free (src->repo);
	if (src->keyfile != NULL)
		g_key_file_unref (src->keyfile);
	g_slice_free (HifSource, src);
}

/**
 * hif_load_multiline_key_file:
 **/
static GKeyFile *
hif_load_multiline_key_file (const gchar *filename, GError **error)
{
	gboolean ret;
	gchar *data = NULL;
	gchar **lines = NULL;
	GKeyFile *file = NULL;
	gsize len;
	GString *string = NULL;
	guint i;

	/* load file */
	ret = g_file_get_contents (filename, &data, &len, error);
	if (!ret)
		goto out;

	/* split into lines */
	string = g_string_new ("");
	lines = g_strsplit (data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		/* if a line starts with whitespace, then append it on
		 * the previous line */
		g_strdelimit (lines[i], "\t", ' ');
		if (lines[i][0] == ' ' && string->len > 0) {
			g_string_set_size (string, string->len - 1);
			g_string_append_printf (string,
						";%s\n",
						g_strchug (lines[i]));
		} else {
			g_string_append_printf (string,
						"%s\n",
						lines[i]);
		}
	}

	/* remove final newline */
	if (string->len > 0)
		g_string_set_size (string, string->len - 1);

	/* load modified lines */
	file = g_key_file_new ();
	ret = g_key_file_load_from_data (file,
					 string->str,
					 -1,
					 G_KEY_FILE_KEEP_COMMENTS,
					 error);
	if (!ret) {
		g_key_file_free (file);
		file = NULL;
		goto out;
	}
out:
	if (string != NULL)
		g_string_free (string, TRUE);
	g_free (data);
	g_strfreev (lines);
	return file;
}

/**
 * hif_source_add_media:
 */
gboolean
hif_source_add_media (GPtrArray *sources,
		      const gchar *mount_point,
		      guint idx,
		      GError **error)
{
	GKeyFile *treeinfo;
	HifSource *src;
	gboolean ret = TRUE;
	gchar *basearch = NULL;
	gchar *release = NULL;
	gchar *treeinfo_fn;

	/* get common things */
	treeinfo_fn = g_build_filename (mount_point, ".treeinfo", NULL);
	treeinfo = g_key_file_new ();
	ret = g_key_file_load_from_file (treeinfo, treeinfo_fn, 0, error);
	if (!ret)
		goto out;
	basearch = g_key_file_get_string (treeinfo, "general", "arch", error);
	if (basearch == NULL) {
		ret = FALSE;
		goto out;
	}
	release = g_key_file_get_string (treeinfo, "general", "version", error);
	if (release == NULL) {
		ret = FALSE;
		goto out;
	}

	/* create read-only location */
	src = g_slice_new0 (HifSource);
	src->enabled = TRUE;
	if (idx == 0)
		src->id = g_strdup ("media");
	else
		src->id = g_strdup_printf ("media-%i", idx);
	src->location = g_strdup (mount_point);
	src->repo_handle = lr_handle_init ();
	ret = lr_handle_setopt (src->repo_handle, error, LRO_REPOTYPE, LR_YUMREPO);
	if (!ret)
		goto out;
	src->repo_result = lr_result_init ();
	src->gpgcheck = TRUE;
	src->urlvars = lr_urlvars_set (src->urlvars, "releasever", release);
	src->urlvars = lr_urlvars_set (src->urlvars, "basearch", basearch);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_VARSUB, src->urlvars);
	if (!ret)
		goto out;

	g_debug ("added source %s", src->id);
	g_ptr_array_add (sources, src);
out:
	g_free (basearch);
	g_free (release);
	g_free (treeinfo_fn);
	g_key_file_free (treeinfo);
	return ret;
}

/**
 * hif_source_parse:
 */
gboolean
hif_source_parse (GKeyFile *config,
		  GPtrArray *sources,
		  const gchar *filename,
		  GError **error)
{
	gboolean has_enabled;
	gboolean is_enabled;
	gboolean ret = TRUE;
	gchar *basearch = NULL;
	gchar *fedora_release = NULL;
	gchar *cache_dir = NULL;
	gchar **repos = NULL;
	GKeyFile *keyfile;
	guint64 val;
	guint i;

	/* load non-standard keyfile */
	keyfile = hif_load_multiline_key_file (filename, error);
	if (keyfile == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get common things */
	basearch = g_key_file_get_string (config,
					  HIF_CONFIG_GROUP_NAME,
					  "BaseArch", NULL);
	fedora_release = g_key_file_get_string (config,
						HIF_CONFIG_GROUP_NAME,
						"ReleaseVersion", NULL);
	cache_dir = g_key_file_get_string (config,
					   HIF_CONFIG_GROUP_NAME,
					   "CacheDir", NULL);

	/* save all the repos listed in the file */
	repos = g_key_file_get_groups (keyfile, NULL);
	for (i = 0; repos[i] != NULL; i++) {
		HifSource *src;

		/* enabled isn't a required key */
		has_enabled = g_key_file_has_key (keyfile,
						  repos[i],
						  "enabled",
						  NULL);
		if (has_enabled) {
			is_enabled = g_key_file_get_boolean (keyfile,
							     repos[i],
							     "enabled",
							     NULL);
		} else {
			is_enabled = TRUE;
		}

		src = g_slice_new0 (HifSource);
		src->enabled = is_enabled;
		src->keyfile = g_key_file_ref (keyfile);
		src->filename = g_strdup (filename);
		src->id = g_strdup (repos[i]);
		src->location = g_build_filename (cache_dir, repos[i], NULL);
		src->location_tmp = g_strdup_printf ("%s.tmp", src->location);
		src->repo_handle = lr_handle_init ();
		ret = lr_handle_setopt (src->repo_handle, error, LRO_REPOTYPE, LR_YUMREPO);
		if (!ret)
			goto out;
		ret = lr_handle_setopt (src->repo_handle, error, LRO_USERAGENT, "PackageKit-hawkey");
		if (!ret)
			goto out;
		src->repo_result = lr_result_init ();

		//FIXME: only set if a gpgkry is also set?
		val = g_key_file_get_uint64 (src->keyfile, src->id, "gpgcheck", NULL);
		src->gpgcheck = (val == 1) ? 1 : 0;

		// FIXME: don't hardcode
		src->urlvars = lr_urlvars_set (src->urlvars, "releasever", fedora_release);
		src->urlvars = lr_urlvars_set (src->urlvars, "basearch", basearch);
		ret = lr_handle_setopt (src->repo_handle, error, LRO_VARSUB, src->urlvars);
		if (!ret)
			goto out;

		g_debug ("added source %s\t%s", filename, repos[i]);
		g_ptr_array_add (sources, src);
	}
out:
	g_free (basearch);
	g_free (cache_dir);
	g_free (fedora_release);
	g_strfreev (repos);
	if (keyfile != NULL)
		g_key_file_unref (keyfile);
	return ret;
}

/**
 * hif_source_update_state_cb:
 */
static int
hif_source_update_state_cb (void *user_data,
			    gdouble total_to_download,
			    gdouble now_downloaded)
{
	gboolean ret;
	gdouble percentage;
	HifState *state = (HifState *) user_data;

	/* abort */
	if (!hif_state_check (state, NULL))
		return -1;

	/* the number of files has changed */
	if (total_to_download <= 0.01 && now_downloaded <= 0.01) {
		hif_state_reset (state);
		return 0;
	}

	/* nothing sensible */
	if (total_to_download < 0)
		return 0;

	/* set percentage */
	percentage = 100.0f * now_downloaded / total_to_download;
	ret = hif_state_set_percentage (state, percentage);
	if (ret) {
		g_debug ("update state %.0f/%.0f",
			 now_downloaded,
			 total_to_download);
	}

	return 0;
}

/**
 * hif_source_check:
 */
gboolean
hif_source_check (HifSource *src, HifState *state, GError **error)
{
	const gchar *download_list[] = { "primary",
					 "filelists",
					 "group",
					 "updateinfo",
					 NULL};
	const gchar *tmp;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	LrYumRepo *yum_repo;
	const gchar *urls[] = { "", NULL };

	/* Yum metadata */
	hif_state_action_start (state, PK_STATUS_ENUM_LOADING_CACHE, NULL);
	urls[0] = src->location;
	ret = lr_handle_setopt (src->repo_handle, error, LRO_URLS, urls);
	if (!ret)
		goto out;
	ret = lr_handle_setopt (src->repo_handle, error, LRO_LOCAL, TRUE);
	if (!ret)
		goto out;
	ret = lr_handle_setopt (src->repo_handle, error, LRO_CHECKSUM, TRUE);
	if (!ret)
		goto out;
	ret = lr_handle_setopt (src->repo_handle, error, LRO_YUMDLIST, download_list);
	if (!ret)
		goto out;
	lr_result_clear (src->repo_result);
	ret = lr_handle_perform (src->repo_handle, src->repo_result, &error_local);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "repodata %s was not complete: %s",
			     src->id, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the metadata file locations */
	ret = lr_result_getinfo (src->repo_result, &error_local, LRR_YUM_REPO, &yum_repo);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to get yum-repo: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get timestamp */
#if LR_VERSION_CHECK(1,4,1)
	ret = lr_result_getinfo (src->repo_result, &error_local,
				 LRR_YUM_TIMESTAMP, &src->timestamp);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to get timestamp: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
#endif

	/* create a HyRepo */
	src->repo = hy_repo_create (src->id);
	hy_repo_set_string (src->repo, HY_REPO_MD_FN, yum_repo->repomd);
	tmp = lr_yum_repo_path (yum_repo, "primary");
	if (tmp != NULL)
		hy_repo_set_string (src->repo, HY_REPO_PRIMARY_FN, tmp);
	tmp = lr_yum_repo_path (yum_repo, "filelists");
	if (tmp != NULL)
		hy_repo_set_string (src->repo, HY_REPO_FILELISTS_FN, tmp);
	tmp = lr_yum_repo_path (yum_repo, "updateinfo");
	if (tmp != NULL)
		hy_repo_set_string (src->repo, HY_REPO_UPDATEINFO_FN, tmp);
out:
	return ret;
}

/**
 * hif_source_clean:
 */
gboolean
hif_source_clean (HifSource *src, GError **error)
{
	gboolean ret;

	if (!g_file_test (src->location, G_FILE_TEST_EXISTS))
		return TRUE;

	ret = pk_directory_remove_contents (src->location);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "Failed to remove %s",
			     src->location);
	}
	return ret;
}

/**
 * hif_source_get_username_password_string:
 */
static gchar *
hif_source_get_username_password_string (const gchar *user, const gchar *pass)
{
	if (user == NULL && pass == NULL)
		return NULL;
	if (user != NULL && pass == NULL)
		return g_strdup (user);
	if (user == NULL && pass != NULL)
		return g_strdup_printf (":%s", pass);
	return g_strdup_printf ("%s:%s", user, pass);
}

/**
 * hif_source_set_keyfile_data:
 */
static gboolean
hif_source_set_keyfile_data (HifSource *src, GError **error)
{
	gchar *pwd = NULL;
	gchar *str = NULL;
	gchar *usr = NULL;
	gchar **baseurls;
	gboolean ret;

	/* baseurl is optional */
	baseurls = g_key_file_get_string_list (src->keyfile, src->id, "baseurl", NULL, NULL);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_URLS, baseurls);
	if (!ret)
		goto out;
	g_strfreev (baseurls);

	/* mirrorlist is optional */
	str = g_key_file_get_string (src->keyfile, src->id, "mirrorlist", NULL);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_MIRRORLIST, str);
	if (!ret)
		goto out;
	g_free (str);

	/* metalink is optional */
	str = g_key_file_get_string (src->keyfile, src->id, "metalink", NULL);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_METALINKURL, str);
	if (!ret)
		goto out;
	g_free (str);

	/* gpgcheck is optional */
	// FIXME: https://github.com/Tojaj/librepo/issues/16
	//ret = lr_handle_setopt (src->repo_handle, error, LRO_GPGCHECK, src->gpgcheck == 1 ? 1 : 0);
	//if (!ret)
	//	goto out;

	/* proxy is optional */
	str = g_key_file_get_string (src->keyfile, src->id, "proxy", NULL);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_PROXY, str);
	if (!ret)
		goto out;
	g_free (str);

	/* both parts of the proxy auth are optional */
	usr = g_key_file_get_string (src->keyfile, src->id, "proxy_username", NULL);
	pwd = g_key_file_get_string (src->keyfile, src->id, "proxy_password", NULL);
	str = hif_source_get_username_password_string (usr, pwd);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_PROXYUSERPWD, str);
	if (!ret)
		goto out;
	g_free (usr);
	g_free (pwd);
	g_free (str);

	/* both parts of the HTTP auth are optional */
	usr = g_key_file_get_string (src->keyfile, src->id, "username", NULL);
	pwd = g_key_file_get_string (src->keyfile, src->id, "password", NULL);
	str = hif_source_get_username_password_string (usr, pwd);
	ret = lr_handle_setopt (src->repo_handle, error, LRO_USERPWD, str);
	if (!ret)
		goto out;
out:
	g_free (usr);
	g_free (pwd);
	g_free (str);
	return ret;
//gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-$basearch
}

/**
 * hif_source_update:
 */
gboolean
hif_source_update (HifSource *src,
		   HifSourceUpdateFlags flags,
		   HifState *state,
		   GError **error)
{
	GError *error_local = NULL;
	HifState *state_local;
	gboolean ret;
	gint rc;
#if LR_VERSION_CHECK(1,4,1)
	gint64 timestamp_new = 0;
#endif

	/* take lock */
	ret = hif_state_take_lock (state,
				   HIF_LOCK_TYPE_METADATA,
				   HIF_LOCK_MODE_PROCESS,
				   error);
	if (!ret)
		goto out;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   50, /* download */
				   50, /* check */
				   -1);
	if (!ret)
		goto out;

	/* remove the temporary space if it already exists */
	if (g_file_test (src->location_tmp, G_FILE_TEST_EXISTS)) {
		ret = pk_directory_remove_contents (src->location_tmp);
		if (!ret) {
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "Failed to remove %s",
				     src->location_tmp);
			goto out;
		}
	}

	/* ensure exists */
	if (!g_file_test (src->location_tmp, G_FILE_TEST_EXISTS)) {
		rc = g_mkdir (src->location_tmp, 0755);
		if (rc != 0) {
			ret = FALSE;
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "Failed to create %s", src->location_tmp);
			goto out;
		}
	}

	g_debug ("Attempting to update %s", src->id);
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_LOCAL, FALSE);
	if (!ret)
		goto out;
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_DESTDIR, src->location_tmp);
	if (!ret)
		goto out;
	ret = hif_source_set_keyfile_data (src, error);
	if (!ret)
		goto out;

	/* Callback to display progress of downloading */
	state_local = hif_state_get_child (state);
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_PROGRESSDATA, state_local);
	if (!ret)
		goto out;
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_PROGRESSCB, hif_source_update_state_cb);
	if (!ret)
		goto out;
	lr_result_clear (src->repo_result);
	hif_state_action_start (state_local,
				PK_STATUS_ENUM_DOWNLOAD_REPOSITORY, NULL);
	ret = lr_handle_perform (src->repo_handle,
				 src->repo_result,
				 &error_local);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_CANNOT_FETCH_SOURCES,
			     "cannot update repo: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check the newer metadata is newer */
#if LR_VERSION_CHECK(1,4,1)
	ret = lr_result_getinfo (src->repo_result, &error_local,
				 LRR_YUM_TIMESTAMP, &timestamp_new);
	if (!ret) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to get timestamp: %s",
			     error_local->message);
		g_error_free (error_local);
		goto out;
	}
	if ((flags & HIF_SOURCE_UPDATE_FLAG_FORCE) == 0 ||
	    timestamp_new < src->timestamp) {
		g_debug ("fresh metadata was older than what we have, ignoring");
		goto out;
	}
#endif

	/* delete old /var/cache/PackageKit/metadata/$REPO/ */
	ret = hif_source_clean (src, error);
	if (!ret)
		goto out;

	/* rename .tmp actual name */
	rc = g_rename (src->location_tmp, src->location);
	if (rc != 0) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_CANNOT_FETCH_SOURCES,
			     "cannot move %s to %s",
			     src->location_tmp, src->location);
		goto out;
	}
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_DESTDIR, src->location_tmp);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;

	/* now setup internal hawkey stuff */
	state_local = hif_state_get_child (state);
	ret = hif_source_check (src, state_local, error);
	if (!ret)
		goto out;

	/* done */
	ret = hif_state_done (state, error);
	if (!ret)
		goto out;
out:
	lr_handle_setopt (src->repo_handle, NULL, LRO_PROGRESSCB, NULL);
	lr_handle_setopt (src->repo_handle, NULL, LRO_PROGRESSDATA, 0xdeadbeef);
	return ret;
}

/**
 * hif_source_get_id:
 */
const gchar *
hif_source_get_id (HifSource *src)
{
	return src->id;
}

/**
 * hif_source_get_location:
 */
const gchar *
hif_source_get_location (HifSource *src)
{
	return src->location;
}

/**
 * hif_source_get_description:
 */
static gchar *
hif_source_substitute (HifSource *src, const gchar *url)
{
	char *tmp;
	gchar *substituted;

	/* do a little dance so we can use g_free() rather than lr_free() */
	tmp = lr_url_substitute (url, src->urlvars);
	substituted = g_strdup (tmp);
	lr_free (tmp);

	return substituted;
}

/**
 * hif_source_get_description:
 */
gchar *
hif_source_get_description (HifSource *src)
{
	gchar *substituted = NULL;
	gchar *tmp;

	/* have to substitute things like $releasever and $basearch */
	tmp = g_key_file_get_string (src->keyfile,
				     hif_source_get_id (src),
				     "name",
				     NULL);
	if (tmp == NULL)
		goto out;
	substituted = hif_source_substitute (src, tmp);
out:
	g_free (tmp);
	return substituted;
}

/**
 * hif_source_get_enabled:
 */
gboolean
hif_source_get_enabled (HifSource *src)
{
	return src->enabled;
}

/**
 * hif_source_get_gpgcheck:
 */
gboolean
hif_source_get_gpgcheck (HifSource *src)
{
	return src->gpgcheck;
}

/**
 * hif_source_get_repo:
 */
HyRepo
hif_source_get_repo (HifSource *src)
{
	return src->repo;
}

/**
 * hif_source_set_data:
 */
gboolean
hif_source_set_data (HifSource *src,
		     const gchar *parameter,
		     const gchar *value,
		     GError **error)
{
	gboolean ret;
	gchar *data = NULL;

	/* save change to keyfile and dump updated file to disk */
	g_key_file_set_string (src->keyfile,
			       src->id,
			       parameter,
			       value);
	data = g_key_file_to_data (src->keyfile, NULL, error);
	if (data == NULL) {
		ret = FALSE;
		goto out;
	}
	ret = g_file_set_contents (src->filename, data, -1, error);
	if (!ret)
		goto out;
out:
	g_free (data);
	return ret;
}

/**
 * hif_source_is_devel:
 **/
gboolean
hif_source_is_devel (HifSource *src)
{
	if (g_str_has_suffix (src->id, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (src->id, "-debug"))
		return TRUE;
	if (g_str_has_suffix (src->id, "-development"))
		return TRUE;
	if (g_str_has_suffix (src->id, "-source"))
		return TRUE;
	return FALSE;
}

/**
 * hif_source_checksum_hy_to_lr:
 **/
static LrChecksumType
hif_source_checksum_hy_to_lr (int checksum_hy)
{
	if (checksum_hy == HY_CHKSUM_MD5)
		return LR_CHECKSUM_MD5;
	if (checksum_hy == HY_CHKSUM_SHA1)
		return LR_CHECKSUM_SHA1;
	if (checksum_hy == HY_CHKSUM_SHA256)
		return LR_CHECKSUM_SHA256;
	return LR_CHECKSUM_UNKNOWN;
}

/**
 * hif_source_download_package:
 **/
gchar *
hif_source_download_package (HifSource *src,
			     HyPackage pkg,
			     const gchar *directory,
			     HifState *state,
			     GError **error)
{
	char *checksum_str = NULL;
	const unsigned char *checksum;
	gboolean ret;
	gchar *basename = NULL;
	gchar *directory_slash;
	gchar *loc = NULL;
	GError *error_local = NULL;
	gint rc;
	int checksum_type;

	/* if nothing specified then use cachedir */
	if (directory == NULL) {
		directory_slash = g_build_filename (src->location, "packages", "/", NULL);
		if (!g_file_test (directory_slash, G_FILE_TEST_EXISTS)) {
			rc = g_mkdir (directory_slash, 0755);
			if (rc != 0) {
				g_set_error (error,
					     HIF_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "Failed to create %s",
					     directory_slash);
				goto out;
			}
		}
	} else {
		/* librepo uses the GNU basename() function to find out if the
		 * output directory is fully specified as a filename, but
		 * basename needs a trailing '/' to detect it's not a filename */
		directory_slash = g_build_filename (directory, "/", NULL);
	}

	/* setup the repo remote */
	ret = hif_source_set_keyfile_data (src, error);
	if (!ret)
		goto out;
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_PROGRESSDATA, state);
	if (!ret)
		goto out;
	//TODO: this doesn't actually report sane things
	ret = lr_handle_setopt (src->repo_handle, error,
				LRO_PROGRESSCB, hif_source_update_state_cb);
	if (!ret)
		goto out;
	g_debug ("downloading %s to %s",
		 hy_package_get_location (pkg),
		 directory_slash);

	checksum = hy_package_get_chksum (pkg, &checksum_type);
	checksum_str = hy_chksum_str (checksum, checksum_type);
	hif_state_action_start (state,
				PK_STATUS_ENUM_DOWNLOAD,
				hif_package_get_id (pkg));
	ret = lr_download_package (src->repo_handle,
				  hy_package_get_location (pkg),
				  directory_slash,
				  hif_source_checksum_hy_to_lr (checksum_type),
				  checksum_str,
				  0, /* size unknown */
				  NULL, /* baseurl not required */
				  TRUE,
				  &error_local);
	if (!ret) {
		if (g_error_matches (error_local,
				     LR_PACKAGE_DOWNLOADER_ERROR,
				     LRE_ALREADYDOWNLOADED)) {
			/* ignore */
			g_clear_error (&error_local);
		} else {
			g_set_error (error,
				     HIF_ERROR,
				     PK_ERROR_ENUM_INTERNAL_ERROR,
				     "cannot download %s to %s: %s",
				     hy_package_get_location (pkg),
				     directory_slash,
				     error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* build return value */
	basename = g_path_get_basename (hy_package_get_location (pkg));
	loc = g_build_filename (directory_slash,
				basename,
				NULL);
out:
	lr_handle_setopt (src->repo_handle, NULL, LRO_PROGRESSCB, NULL);
	lr_handle_setopt (src->repo_handle, NULL, LRO_PROGRESSDATA, 0xdeadbeef);
	hy_free (checksum_str);
	g_free (basename);
	g_free (directory_slash);
	return loc;
}
