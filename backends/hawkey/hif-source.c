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

#include <glib.h>
#include <glib/gstdio.h>
#include <pk-backend.h>

#include <librepo/librepo.h>
#include <hawkey/util.h>

#include "hif-source.h"
#include "hif-utils.h"

struct HifSource {
	gboolean	 enabled;
	gboolean	 gpgcheck;
	gchar		*filename;
	gchar		*id;
	gchar		*location;	/* /var/cache/PackageKit/metadata/fedora */
	gchar		*packages;	/* /var/cache/PackageKit/metadata/fedora/packages */
	GKeyFile	*keyfile;
	HyRepo		 repo;
	lr_Handle	 repo_handle;
	lr_Result	 repo_result;
	lr_UrlVars	*urlvars;
};

/**
 * hif_source_free:
 */
static void
hif_source_free (gpointer data)
{
	HifSource *src = (HifSource *) data;
	g_free (src->filename);
	g_free (src->id);
	g_free (src->packages);
	g_free (src->location);
	if (src->repo_result != NULL)
		lr_result_free (src->repo_result);
	if (src->repo_handle != NULL)
		lr_handle_free (src->repo_handle);
	if (src->repo != NULL)
		hy_repo_free (src->repo);
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
 * hif_source_parse:
 */
static gboolean
hif_source_parse (GPtrArray *sources,
		  const gchar *filename,
		  HifSourceScanFlags flags,
		  GError **error)
{
	gboolean has_enabled;
	gboolean is_enabled;
	gboolean ret = TRUE;
	gchar **repos = NULL;
	gint rc;
	GKeyFile *keyfile;
	guint64 val;
	guint i;

	/* load non-standard keyfile */
	keyfile = hif_load_multiline_key_file (filename, error);
	if (keyfile == NULL) {
		ret = FALSE;
		goto out;
	}

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

		/* do not create object if we're not interested */
		if (is_enabled == FALSE && (flags & HIF_SOURCE_SCAN_FLAG_ONLY_ENABLED) > 0)
			continue;

		src = g_slice_new0 (HifSource);
		src->enabled = is_enabled;
		src->keyfile = g_key_file_ref (keyfile);
		src->filename = g_strdup (filename);
		src->id = g_strdup (repos[i]);
		src->location = g_build_filename ("/var/cache/PackageKit/metadata", repos[i], NULL);
		src->packages = g_build_filename (src->location, "packages", NULL);
		src->repo_handle = lr_handle_init ();
		lr_handle_setopt (src->repo_handle, LRO_REPOTYPE, LR_YUMREPO);
		lr_handle_setopt (src->repo_handle, LRO_USERAGENT, "PackageKit-hawkey");
		src->repo_result = lr_result_init ();

		//FIXME: only set if a gpgkry is also set?
		val = g_key_file_get_uint64 (src->keyfile, src->id, "gpgcheck", NULL);
		src->gpgcheck = (val == 1) ? 1 : 0;

		// FIXME: don't hardcode
		src->urlvars = lr_urlvars_set (src->urlvars, "releasever", "19");
		src->urlvars = lr_urlvars_set (src->urlvars, "basearch", "x86_64");
		lr_handle_setopt (src->repo_handle, LRO_VARSUB, src->urlvars);

		/* ensure exists */
		if (!g_file_test (src->location, G_FILE_TEST_EXISTS)) {
			rc = g_mkdir (src->location, 0755);
			if (rc != 0) {
				ret = FALSE;
				g_set_error (error,
					     HIF_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "Failed to create %s", src->location);
				goto out;
			}
		}

		/* ensure exists */
		if (!g_file_test (src->packages, G_FILE_TEST_EXISTS)) {
			rc = g_mkdir (src->packages, 0755);
			if (rc != 0) {
				ret = FALSE;
				g_set_error (error,
					     HIF_ERROR,
					     PK_ERROR_ENUM_INTERNAL_ERROR,
					     "Failed to create %s", src->packages);
				goto out;
			}
		}

		g_debug ("added source %s\t%s", filename, repos[i]);
		g_ptr_array_add (sources, src);
	}
out:
	g_strfreev (repos);
	if (keyfile != NULL)
		g_key_file_unref (keyfile);
	return ret;
}

/**
 * hif_source_find_all:
 */
GPtrArray *
hif_source_find_all (const gchar *repos_dir,
		     HifSourceScanFlags flags,
		     GError **error)
{
	const gchar *file;
	const gchar *repo_path = "/etc/yum.repos.d";
	gboolean ret;
	gchar *path_tmp;
	GDir *dir;
	GPtrArray *array = NULL;
	GPtrArray *sources = NULL;

	/* open dir */
	dir = g_dir_open (repo_path, 0, error);
	if (dir == NULL)
		goto out;

	/* find all the .repo files */
	array = g_ptr_array_new_with_free_func (hif_source_free);
	while ((file = g_dir_read_name (dir)) != NULL) {
		if (!g_str_has_suffix (file, ".repo"))
			continue;
		path_tmp = g_build_filename (repo_path, file, NULL);
		ret = hif_source_parse (array, path_tmp, flags, error);
		g_free (path_tmp);
		if (!ret)
			goto out;
	}

	/* all okay */
	sources = g_ptr_array_ref (array);
out:
	if (array != NULL)
		g_ptr_array_unref (array);
	if (dir != NULL)
		g_dir_close (dir);
	return sources;
}

/**
 * hif_source_filter_by_id:
 */
HifSource *
hif_source_filter_by_id (GPtrArray *sources, const gchar *id, GError **error)
{
	guint i;
	HifSource *tmp;
	HifSource *src = NULL;

	for (i = 0; i < sources->len; i++) {
		tmp = g_ptr_array_index (sources, i);
		if (g_strcmp0 (tmp->id, id) == 0) {
			src = tmp;
			goto out;
		}
	}

	/* we didn't find anything */
	g_set_error (error,
		     HIF_ERROR,
		     PK_ERROR_ENUM_REPO_NOT_FOUND,
		     "failed to find %s", id);
out:
	return src;
}

/**
 * hif_source_update_state_cb:
 */
static int
hif_source_update_state_cb (void *user_data,
			    gdouble total_to_download,
			    gdouble now_downloaded)
{
	gdouble percentage;
	HifState *state = (HifState *) user_data;

	/* nothing sensible */
	if (total_to_download <= 0 || now_downloaded <= 0)
		return 0;

	g_debug ("update state %.0f/%.0f", now_downloaded, total_to_download);

	/* abort */
	if (!hif_state_check (state, NULL))
		return -1;

	/* set percentage */
	percentage = 100.0f * now_downloaded / total_to_download;
	hif_state_set_percentage (state, percentage);

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
	lr_Rc rc;
	lr_YumRepo yum_repo;

	/* Yum metadata */
	hif_state_action_start (state, PK_STATUS_ENUM_LOADING_CACHE, NULL);
	lr_handle_setopt (src->repo_handle, LRO_URL, src->location);
	lr_handle_setopt (src->repo_handle, LRO_LOCAL, TRUE);
	lr_handle_setopt (src->repo_handle, LRO_CHECKSUM, TRUE);
	lr_handle_setopt (src->repo_handle, LRO_YUMDLIST, download_list);
	lr_result_clear (src->repo_result);
	rc = lr_handle_perform (src->repo_handle, src->repo_result);
	if (rc) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "repodata %s was not complete: %s",
			     src->id, lr_strerror (rc));
		goto out;
	}

	/* get the metadata file locations */
	rc = lr_result_getinfo (src->repo_result, LRR_YUM_REPO, &yum_repo);
	if (rc) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "failed to get yum-repo: %s",
			     lr_strerror (rc));
		goto out;
	}

	/* create a HyRepo */
	src->repo = hy_repo_create (src->id);
	hy_repo_set_string (src->repo, HY_REPO_MD_FN, yum_repo->repomd);
	tmp = lr_yum_repo_path(yum_repo, "primary");
	if (tmp != NULL)
		hy_repo_set_string (src->repo, HY_REPO_PRIMARY_FN, tmp);
	tmp = lr_yum_repo_path(yum_repo, "filelists");
	if (tmp != NULL)
		hy_repo_set_string (src->repo, HY_REPO_FILELISTS_FN, tmp);
	tmp = lr_yum_repo_path(yum_repo, "updateinfo");
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
static void
hif_source_set_keyfile_data (HifSource *src)
{
	gchar *pwd;
	gchar *str;
	gchar *usr;

	/* baseurl is optional */
	str = g_key_file_get_string (src->keyfile, src->id, "baseurl", NULL);
	lr_handle_setopt (src->repo_handle, LRO_URL, str);
	g_free (str);

	/* mirrorlist is optional */
	str = g_key_file_get_string (src->keyfile, src->id, "mirrorlist", NULL);
	lr_handle_setopt (src->repo_handle, LRO_MIRRORLIST, str);
	g_free (str);

	/* gpgcheck is optional */
	// FIXME: https://github.com/Tojaj/librepo/issues/16
	//lr_handle_setopt (src->repo_handle, LRO_GPGCHECK, src->gpgcheck == 1 ? 1 : 0);

	/* proxy is optional */
	str = g_key_file_get_string (src->keyfile, src->id, "proxy", NULL);
	lr_handle_setopt (src->repo_handle, LRO_PROXY, str);
	g_free (str);

	/* both parts of the proxy auth are optional */
	usr = g_key_file_get_string (src->keyfile, src->id, "proxy_username", NULL);
	pwd = g_key_file_get_string (src->keyfile, src->id, "proxy_password", NULL);
	str = hif_source_get_username_password_string (usr, pwd);
	lr_handle_setopt (src->repo_handle, LRO_PROXYUSERPWD, str);
	g_free (usr);
	g_free (pwd);
	g_free (str);

	/* both parts of the HTTP auth are optional */
	usr = g_key_file_get_string (src->keyfile, src->id, "username", NULL);
	pwd = g_key_file_get_string (src->keyfile, src->id, "password", NULL);
	str = hif_source_get_username_password_string (usr, pwd);
	lr_handle_setopt (src->repo_handle, LRO_USERPWD, str);
	g_free (usr);
	g_free (pwd);
	g_free (str);

//gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-$basearch
}

/**
 * hif_source_update:
 */
gboolean
hif_source_update (HifSource *src, HifState *state, GError **error)
{
	gboolean ret;
	HifState *state_local;
	lr_Rc rc;

	/* set state */
	ret = hif_state_set_steps (state, error,
				   50, /* download */
				   50, /* check */
				   -1);
	if (!ret)
		goto out;

	/* clean and start again */
	ret = hif_source_clean (src, error);
	if (!ret)
		goto out;

	g_debug ("Attempting to update %s", src->id);
	lr_handle_setopt (src->repo_handle, LRO_LOCAL, FALSE);
//	lr_handle_setopt (src->repo_handle, LRO_UPDATE, TRUE);
	lr_handle_setopt (src->repo_handle, LRO_DESTDIR, src->location);
	hif_source_set_keyfile_data (src);

	// Callback to display progress of downloading
	state_local = hif_state_get_child (state);
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSDATA, state_local);
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSCB, hif_source_update_state_cb);
	lr_result_clear (src->repo_result);
	hif_state_action_start (state_local, PK_STATUS_ENUM_DOWNLOAD_REPOSITORY, NULL);
	rc = lr_handle_perform (src->repo_handle, src->repo_result);
	if (rc) {
		ret = FALSE;
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "cannot update repo: %s [%s:%s]",
			     lr_strerror (rc),
			     lr_handle_last_curl_strerror (src->repo_handle),
			     lr_handle_last_curlm_strerror (src->repo_handle));
		goto out;
	}

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
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSCB, NULL);
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSDATA, 0xdeadbeef);
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
static lr_ChecksumType
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
	gchar *basename = NULL;
	gchar *directory_slash;
	gchar *loc = NULL;
	gchar *package_id = NULL;
	int checksum_type;
	int rc;

	/* if nothing specified then use cachedir */
	if (directory == NULL) {
		directory_slash = g_build_filename (src->location, "packages", "/", NULL);
	} else {
		/* librepo uses the GNU basename() function to find out if the
		 * output directory is fully specified as a filename, but
		 * basename needs a trailing '/' to detect it's not a filename */
		directory_slash = g_build_filename (directory, "/", NULL);
	}

	/* setup the repo remote */
	hif_source_set_keyfile_data (src);
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSDATA, state);
	//TODO: this doesn't actually report sane things
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSCB, hif_source_update_state_cb);
	g_debug ("downloading %s to %s", hy_package_get_location (pkg), directory_slash);

	checksum = hy_package_get_chksum (pkg, &checksum_type);
	checksum_str = hy_chksum_str (checksum, checksum_type);
	package_id = hif_package_get_id (pkg);
	hif_state_action_start (state, PK_STATUS_ENUM_DOWNLOAD, package_id);
	rc = lr_download_package (src->repo_handle,
				  hy_package_get_location (pkg),
				  directory_slash,
				  hif_source_checksum_hy_to_lr (checksum_type),
				  checksum_str,
				  NULL, /* baseurl not required */
				  TRUE);
	if (rc && rc != LRE_ALREADYDOWNLOADED) {
		g_set_error (error,
			     HIF_ERROR,
			     PK_ERROR_ENUM_INTERNAL_ERROR,
			     "cannot download %s to %s: %s [%s:%s]",
			     hy_package_get_location (pkg),
			     directory_slash,
			     lr_strerror (rc),
			     lr_handle_last_curl_strerror (src->repo_handle),
			     lr_handle_last_curlm_strerror (src->repo_handle));
		goto out;
	}

	/* build return value */
	basename = g_path_get_basename (hy_package_get_location (pkg));
	loc = g_build_filename (directory_slash,
				basename,
				NULL);
out:
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSCB, NULL);
	lr_handle_setopt (src->repo_handle, LRO_PROGRESSDATA, 0xdeadbeef);
	hy_free (checksum_str);
	g_free (basename);
	g_free (package_id);
	g_free (directory_slash);
	return loc;
}
