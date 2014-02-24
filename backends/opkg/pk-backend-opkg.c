/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi: set noexpandtab sts=8 sw=8:
 *
 * Copyright (C) 2007 OpenMoko, Inc
 * Copyright (C) 2009 Sebastian Krzyszkowiak <seba.dos1@gmail.com>
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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>

#include <stdio.h>
#include <libopkg/opkg.h>

enum {
	SEARCH_NAME,
	SEARCH_DESCRIPTION,
	SEARCH_TAG
};

/* parameters passed to the search thread */
typedef struct {
	gint search_type;
	gchar *needle;
	PkBitfield filters;
	PkBackend *backend;
} SearchParams;


/**
 * pk_pk_backend_initialize:
 */
void
pk_pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	/* BACKEND MAINTAINER: feel free to remove this when you've
	 * added support for ONLY_DOWNLOAD and merged the simulate
	 * methods as specified in backends/PORTING.txt */
	g_error ("Backend needs to be ported to 0.8.x -- "
		 "see backends/PORTING.txt for details");
}

static void
opkg_unknown_error (PkBackend *backend, gint error_code, const gchar *failed_cmd)
{
	gchar *msg;

	msg = g_strdup_printf ("%s failed with error code %d", failed_cmd, error_code);
	pk_backend_job_error_code (job, PK_ERROR_ENUM_UNKNOWN, msg);

	g_free (msg);
}

/**
 * opkg_is_gui_pkg:
 *
 * check an opkg package for known GUI dependancies
 */
static gboolean
opkg_is_gui_pkg (pkg_t *pkg)
{

  /* TODO: check appropriate tag */

  /*
  gint i;
  for (i = 0; i < pkg->depends_count; i++)
  {
    if (g_strrstr (pkg->depends_str[i], "gtk"))
      return TRUE;
  }
  */
  return FALSE;
}

/**
 * opkg_is_devel_pkg:
 *
 * check an opkg package to determine if it is a development package
 */
static gboolean
opkg_is_devel_pkg (pkg_t *pkg)
{
  if (g_strrstr (pkg->name, "-dev"))
      return TRUE;

  if (g_strrstr (pkg->name, "-dbg"))
      return TRUE;
/*
  if (g_strrstr (pkg->section, "devel"))
      return TRUE;
*/
  return FALSE;
}

/**
 * opkg_check_tag:
 * check a tag name and value on a package
 *
 * returns true if the tag is present
 */
static gboolean 
opkg_check_tag (pkg_t *pkg, const gchar *tag)
{
	if (pkg->tags && tag)
		return (g_strrstr (pkg->tags, tag) != NULL);
	else
		return FALSE;
}

static void
handle_install_error (PkBackend *backend, int err)
{
	switch (err)
	{
/*	case OPKG_NO_ERROR:
		break;
	case OPKG_PACKAGE_NOT_INSTALLED:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, NULL);
		break;
	case OPKG_PACKAGE_ALREADY_INSTALLED:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED, NULL);
		break;
	case OPKG_GPG_ERROR:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_GPG_FAILURE, NULL);
		break;
	case OPKG_DOWNLOAD_FAILED:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED, NULL);
		break;
	case OPKG_DEPENDENCIES_FAILED:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, NULL);
		break;
	case OPKG_MD5_ERROR:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_CORRUPT, NULL);
		break;
	case OPKG_PACKAGE_NOT_AVAILABLE:
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, NULL);
		break;*/
	default:
		opkg_unknown_error (backend, err, "Update package");
	}
}

/**
 * pk_backend_initialize:
 */
static void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	int opkg = opkg_new ();

	if (!opkg) {
		pk_backend_job_error_code (job,
				PK_ERROR_ENUM_FAILED_INITIALIZATION,
				"Could not start Opkg");
		return;
	}

#ifdef OPKG_OFFLINE_ROOT
	opkg_set_option ((char *) "offline_root", OPKG_OFFLINE_ROOT);
	opkg_re_read_config_files ();
#endif

}

/**
 * pk_backend_destroy:
 */
static void
pk_backend_destroy (PkBackend *backend)
{
	opkg_free ();
}

static void
pk_opkg_progress_cb (const opkg_progress_data_t *pdata, void *data)
{
	PkBackend *backend = (PkBackend*) data;
	if (!backend)
		return;

	pk_backend_job_set_percentage (job, pdata->percentage);
	if (pdata->pkg)
	{
		gchar *uid;
		pkg_t *pkg = pdata->pkg;
		gint status = PK_INFO_ENUM_UNKNOWN;

		uid = g_strdup_printf ("%s;%s;%s;",
			pkg->name, pkg->version, pkg->architecture);

		if (pdata->action == OPKG_DOWNLOAD)
			status = PK_INFO_ENUM_DOWNLOADING;
		else if (pdata->action == OPKG_INSTALL)
			status = PK_INFO_ENUM_INSTALLING;
		else if (pdata->action == OPKG_REMOVE)
			status = PK_INFO_ENUM_REMOVING;

		pk_backend_job_package (job, status, uid, pkg->description);
		g_free (uid);
	}

	switch (pdata->action)
	{
	case OPKG_DOWNLOAD:
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
		break;
	case OPKG_INSTALL:
		pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
		break;
	case OPKG_REMOVE:
		pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
		break;
	}
}

static void
backend_refresh_cache_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	int ret;

	ret = opkg_update_package_lists (pk_opkg_progress_cb, backend);

	if (ret) {
//		if (ret == OPKG_DOWNLOAD_FAILED)
//			pk_backend_job_error_code (job, PK_ERROR_ENUM_REPO_NOT_AVAILABLE, NULL);
//		else
			opkg_unknown_error (backend, ret, "Refreshing cache");
	}
	pk_backend_job_finished (job);
}

/**
 * pk_backend_refresh_cache:
 */
static void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_job_thread_create (job, backend_refresh_cache_thread, NULL, NULL);
}

/**
 * backend search:
 */

static void
pk_opkg_package_list_cb (pkg_t *pkg, void *data)
{
	SearchParams *params = (SearchParams*) data;
	gchar *uid;
	gchar *haystack;
	gint status, match;
	PkBitfield filters = params->filters;

	if (!pkg->name)
		return;

	switch (params->search_type)
	{
		case SEARCH_NAME:
			if (!pkg->name)
				return;
			haystack = g_utf8_strdown (pkg->name, -1);
			match = (g_strrstr (haystack, params->needle) != NULL);
			g_free (haystack);
			if (!match)
				return;
			break;
		case SEARCH_DESCRIPTION:
			if (!pkg->description)
				return;
			haystack = g_utf8_strdown (pkg->description, -1);
			match = (g_strrstr (haystack, params->needle) != NULL);
			g_free (haystack);
			if (!match)
				return;
			break;
		case SEARCH_TAG:
			if (!pkg->tags)
				return;
			if (!g_strrstr (pkg->tags, params->needle))
				return;
			break;
	}

	uid = g_strdup_printf ("%s;%s;%s;",
		pkg->name, pkg->version, pkg->architecture);

	if (pkg->state_status == SS_INSTALLED)
		status = PK_INFO_ENUM_INSTALLED;
	else
		status = PK_INFO_ENUM_AVAILABLE;

	/* check filters */

	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_DEVELOPMENT) && 
		!opkg_is_devel_pkg (pkg))
		goto end_handle;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_DEVELOPMENT) && 
		opkg_is_devel_pkg (pkg))
		goto end_handle;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_GUI) && 
		!opkg_is_gui_pkg (pkg))
		goto end_handle;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_GUI) && 
		opkg_is_gui_pkg (pkg))
		goto end_handle;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_INSTALLED) && 
		(pkg->state_status != SS_INSTALLED))
		goto end_handle;
	if (pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED) && 
		(pkg->state_status == SS_INSTALLED))
		goto end_handle;

	pk_backend_job_package (params->backend, status, uid, pkg->description);

end_handle:
	g_free(uid);

}

static void
backend_search_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	SearchParams *params;

	params = pk_backend_get_pointer (backend, "search-params");

	opkg_list_packages (pk_opkg_package_list_cb, params);

	pk_backend_job_finished (params->backend);

	g_free (params->needle);
	g_free (params);
}

static void
pk_backend_search_name (PkBackend *backend, PkBitfield filters, gchar **search)
{
	SearchParams *params;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	params = g_new0 (SearchParams, 1);
	params->filters = filters;
	params->search_type = SEARCH_NAME;
	params->needle = g_utf8_strdown (search[0], -1);
	params->backend = backend;

	pk_backend_set_pointer (backend, "search-params", params);
	pk_backend_job_thread_create (job, backend_search_thread, NULL, NULL);
}

/**
 * pk_backend_search_description:
 */
static void
pk_backend_search_description (PkBackend *backend, PkBitfield filters, gchar **search)
{
	SearchParams *params;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	params = g_new0 (SearchParams, 1);
	params->filters = filters;
	params->search_type = SEARCH_DESCRIPTION;
	params->needle = g_utf8_strdown (search[0], -1);
	params->backend = backend;

	pk_backend_set_pointer (backend, "search-params", params);
	pk_backend_job_thread_create (job, backend_search_thread, NULL, NULL);
}

static void
pk_backend_search_group (PkBackend *backend, PkBitfield filters, gchar **search)
{
	SearchParams *params;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	params = g_new0 (SearchParams, 1);
	params->filters = filters;
	params->search_type = SEARCH_TAG;
	params->needle = g_strdup_printf ("group::%s", search[0]);
	params->backend = backend;

	pk_backend_set_pointer (backend, "search-params", params);
	pk_backend_job_thread_create (job, backend_search_thread, NULL, NULL);
}

static void
backend_install_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gint err, i;
	gchar **package_ids;
	gchar **parts;

	package_ids = pk_backend_get_strv (backend, "pkids");

	err = 0;

	for (i = 0; package_ids[i]; i++)
	{
		pk_backend_job_package (job, PK_INFO_ENUM_INSTALLING, package_ids[i], NULL);

		parts = pk_package_id_split (package_ids[i]);

		err = opkg_install_package (parts[PK_PACKAGE_ID_NAME], pk_opkg_progress_cb, backend);
		if (err)
			handle_install_error (backend, err);

		g_strfreev (parts);
		if (err != 0)
			break;
	}

	pk_backend_job_finished (job);
}

static void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);

	pk_backend_set_strv (backend, "pkids", package_ids);

	pk_backend_job_thread_create (job, backend_install_packages_thread, NULL, NULL);
}

static void
backend_remove_packages_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gint err, i;
	gchar **package_ids;
	gchar **parts;
	gboolean allow_deps;
	gboolean autoremove;
	gpointer *data;

	data = pk_backend_get_pointer (backend, "remove-params");

	package_ids = (gchar**) data[0];
	allow_deps = GPOINTER_TO_INT (data[1]);
	autoremove = GPOINTER_TO_INT (data[2]);
	g_free (data);

	opkg_set_option ((char *)"autoremove", &autoremove);
	opkg_set_option ((char *)"force_removal_of_dependent_packages", &allow_deps);

	err = 0;

	for (i = 0; package_ids[i]; i++)
	{
		pk_backend_job_package (job, PK_INFO_ENUM_REMOVING, package_ids[i], NULL);

		parts = pk_package_id_split (package_ids[i]);

		err = opkg_remove_package (parts[PK_PACKAGE_ID_NAME], pk_opkg_progress_cb, backend);

		switch (err)
		{
		//case OPKG_NO_ERROR:
		//	break;
		//case OPKG_PACKAGE_NOT_INSTALLED:
		//	pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED, NULL);
		//	break;
		default:
			opkg_unknown_error (backend, err, "Remove");
		}
		g_strfreev (parts);

		if (err != 0)
			break;
	}

	pk_backend_job_finished (job);
}

static void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gpointer *params;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	/* params is a small array we can pack our thread parameters into */
	params = g_new0 (gpointer, 2);

	params[0] = g_strdupv (package_ids);
	params[1] = GINT_TO_POINTER (allow_deps);
	params[2] = GINT_TO_POINTER (autoremove);

	pk_backend_set_pointer (backend, "remove-params", params);

	pk_backend_job_thread_create (job, backend_remove_packages_thread, NULL, NULL);
}

/**
 * pk_backend_get_filters:
 */
static PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_GUI,
		-1);
}

static void
backend_update_system_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gint err;

	/* FIXME: support only_trusted */
	err = opkg_upgrade_all (pk_opkg_progress_cb, backend);

	if (err)
		opkg_unknown_error (backend, err, "Upgrading system");

	pk_backend_job_finished (job);
}

static void
pk_backend_update_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_job_thread_create (job, backend_update_system_thread, NULL, NULL);
}

/**
 * backend_update_package:
 */
static void
backend_update_package_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **parts;
	gint err = 0;
	const gchar *package_id;

	/* FIXME: support only_trusted */
	package_id = pk_backend_get_string (backend, "pkgid");
	parts = pk_package_id_split (package_id);

	if (!parts)
	{
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		pk_backend_job_finished (job);
		return FALSE;
	}

	err = opkg_upgrade_package (parts[PK_PACKAGE_ID_NAME], pk_opkg_progress_cb, backend);
	if (err)
		handle_install_error (backend, err);

	g_strfreev (parts);
	pk_backend_job_finished (job);
}

static void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	gint i;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	for (i = 0; package_ids[i]; i++) {
		pk_backend_set_string (backend, "pkgid", package_ids[i]);
		pk_backend_job_thread_create (job, backend_update_package_thread, NULL, NULL);
	}
}

/**
 * pk_backend_get_updates:
 */

static void
pk_opkg_list_upgradable_cb (pkg_t *pkg, void *data)
{
	PkBackend *backend = (PkBackend*) data;
	gchar *uid;
	gint status;

	if (pkg->state_status == SS_INSTALLED)
		status = PK_INFO_ENUM_INSTALLED;
	else
		status = PK_INFO_ENUM_AVAILABLE;

	uid = g_strdup_printf ("%s;%s;%s;",
		pkg->name, pkg->version, pkg->architecture);

	pk_backend_job_package (job, status, uid, pkg->description);
	g_free(uid);
}

static void
backend_get_updates_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	opkg_list_upgradable_packages (pk_opkg_list_upgradable_cb, backend);
	pk_backend_job_finished (job);
}

static void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);

	pk_backend_job_thread_create (job, backend_get_updates_thread, NULL, NULL);
}

/**
 * pk_backend_get_groups:
 */
static PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_INTERNET,
		PK_GROUP_ENUM_REPOS,
		PK_GROUP_ENUM_MAPS,
		-1);
}

/**
 * pk_backend_get_details:
 */
static void
backend_get_details_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	gchar **package_ids;
	gchar **parts;
	int group_index;
	PkGroupEnum group = 0;
	pkg_t *pkg;
	gchar *newid;

	package_ids = pk_backend_get_strv(backend, "package_ids");
	parts = pk_package_id_split (package_ids[0]);

	if (!parts)
	{
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_backend_job_finished (job);
		return;
	}

	pkg = opkg_find_package (parts[PK_PACKAGE_ID_NAME], parts[PK_PACKAGE_ID_VERSION], parts[PK_PACKAGE_ID_ARCH], parts[PK_PACKAGE_ID_DATA]);
	g_strfreev (parts);

	if (!pkg)
	{
		pk_backend_job_error_code (job, PK_ERROR_ENUM_PACKAGE_NOT_FOUND, NULL);
		pk_backend_job_finished (job);
		return;
	}

	newid = g_strdup_printf ("%s;%s;%s;%s", pkg->name, pkg->version, pkg->architecture, pkg->src->name);

	if (pkg->tags) {
		for (group_index = 0; group < PK_GROUP_ENUM_LAST; group_index++) {
			group = 1 << group_index;
			if (!(group & pk_backend_get_groups(backend))) continue;
			if (opkg_check_tag(pkg, (const gchar *)pk_group_enum_to_string(group))) 
				break;
		}
	}

	pk_backend_job_details (job, newid, NULL, NULL, group, pkg->description, NULL, pkg->size);
	g_free (newid);
	pk_backend_job_finished (job);
}

static void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_job_thread_create (job, backend_get_details_thread, NULL, NULL);
}

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "opkg";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Thomas Wood <thomas@openedhand.com>";
}
