/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi: set noexpandtab sts=8 sw=8:
 *
 * Copyright (C) 2007 OpenMoko, Inc
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <pk-backend-thread.h>
#include <pk-debug.h>
#include <pk-package-ids.h>
#include <pk-enum.h>

#include <libopkg/opkg.h>

static PkBackendThread *thread;
static opkg_t *opkg;

enum {
	SEARCH_NAME,
	SEARCH_DESCRIPTION,
	SEARCH_TAG
};

/* parameters passed to the search thread */
typedef struct {
	gint search_type;
	gchar *needle;
	PkFilterEnum filters;
	PkBackend *backend;
} SearchParams;

static void
opkg_unknown_error (PkBackend *backend, gint error_code, gchar *failed_cmd)
{
	gchar *msg;

	msg = g_strdup_printf ("%s failed with error code %d", failed_cmd, error_code);
	pk_backend_error_code (backend, PK_ERROR_ENUM_UNKNOWN, msg);

	g_free (msg);
}

/**
 * opkg_is_gui_pkg:
 *
 * check an opkg package for known GUI dependancies
 */
static gboolean
opkg_is_gui_pkg (opkg_package_t *pkg)
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
opkg_is_devel_pkg (opkg_package_t *pkg)
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
gboolean
opkg_check_tag (opkg_package_t *pkg, gchar *tag)
{
	if (pkg->tags && tag)
		return (g_strrstr (pkg->tags, tag) != NULL);
	else
		return FALSE;
}

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	/* we use the thread helper */
	thread = pk_backend_thread_new ();


	opkg = opkg_new ();

	if (!opkg) {
		pk_backend_error_code (backend,
				PK_ERROR_ENUM_FAILED_INITIALIZATION,
				"Could not start Opkg");
		return;
	}

#ifdef OPKG_OFFLINE_ROOT
	opkg_set_option (opkg, "offline_root", OPKG_OFFLINE_ROOT);
	opkg_re_read_config_files (opkg);
#endif

}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	g_object_unref (thread);

	opkg_free (opkg);
}


static void
pk_opkg_progress_cb (opkg_t *opkg, int percent, void *data)
{
	PkBackend *backend = PK_BACKEND (data);
	if (!backend)
		return;

	pk_backend_set_percentage (backend, percent);
}

static gboolean
backend_refresh_cache_thread (PkBackendThread *thread, gpointer data)
{
	int ret;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	ret = opkg_update_package_lists (opkg, pk_opkg_progress_cb, backend);
	if (ret) {
		opkg_unknown_error (backend, ret, "Refreshing cache");
	}
	pk_backend_finished (backend);

	return (ret == 0);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_no_percentage_updates (backend);


	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_refresh_cache_thread,
		NULL);
}

/**
 * backend search:
 */

static void
pk_opkg_package_list_cb (opkg_t *opkg, opkg_package_t *pkg, void *data)
{
	SearchParams *params = (SearchParams*) data;
	gchar *uid;
	gchar *haystack;
	gint status, match;
	PkFilterEnum filters = params->filters;

	if (!pkg->name)
		return;

	switch (params->search_type)
	{
		case SEARCH_NAME:
			haystack = g_utf8_strdown (pkg->name, -1);
			match = (g_strrstr (haystack, params->needle) != NULL);
			g_free (haystack);
			if (!match)
				return;
			break;
		case SEARCH_DESCRIPTION:
			haystack = g_utf8_strdown (pkg->description, -1);
			match = (g_strrstr (haystack, params->needle) != NULL);
			g_free (haystack);
			if (!match)
				return;
			break;
		case SEARCH_TAG:
			if (!g_strrstr (pkg->tags, params->needle))
				return;
			break;
	}

	uid = g_strdup_printf ("%s;%s;%s;",
		pkg->name, pkg->version, pkg->architecture);

	if (pkg->installed)
		status = PK_INFO_ENUM_INSTALLED;
	else
		status = PK_INFO_ENUM_AVAILABLE;

	/* check filters */

	if ((filters & PK_FILTER_ENUM_DEVELOPMENT) && !opkg_is_devel_pkg (pkg))
		return;
	if ((filters & PK_FILTER_ENUM_NOT_DEVELOPMENT) && opkg_is_devel_pkg (pkg))
		return;
	if ((filters & PK_FILTER_ENUM_GUI) && !opkg_is_gui_pkg (pkg))
		return;
	if ((filters & PK_FILTER_ENUM_NOT_GUI) && opkg_is_gui_pkg (pkg))
		return;
	if ((filters & PK_FILTER_ENUM_INSTALLED) && (!pkg->installed))
		return;
	if ((filters & PK_FILTER_ENUM_NOT_INSTALLED) && (pkg->installed))
		return;

	pk_backend_package (params->backend, status, uid, pkg->description);

}

static gboolean
backend_search_thread (PkBackendThread *thread, SearchParams *params)
{

	opkg_list_packages (opkg, pk_opkg_package_list_cb, params);

	pk_backend_finished (params->backend);
	g_free (params->needle);
	g_free (params);

	return FALSE;
}

static void
backend_search_name (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
	SearchParams *params;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	params = g_new0 (SearchParams, 1);
	params->filters = filters;
	params->search_type = SEARCH_NAME;
	params->needle = g_utf8_strdown (search, -1);
	params->backend = backend;

	pk_backend_thread_create (thread, (PkBackendThreadFunc) backend_search_thread, params);
}

/**
 * backend_search_description:
 */
static void
backend_search_description (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
	SearchParams *params;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	params = g_new0 (SearchParams, 1);
	params->filters = filters;
	params->search_type = SEARCH_DESCRIPTION;
	params->needle = g_utf8_strdown (search, -1);

	pk_backend_thread_create (thread, (PkBackendThreadFunc) backend_search_thread, params);
}

static void
backend_search_group (PkBackend *backend, PkFilterEnum filters, const gchar *search)
{
	SearchParams *params;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	params = g_new0 (SearchParams, 1);
	params->filters = filters;
	params->search_type = SEARCH_TAG;
	params->needle = g_strdup_printf ("group::%s", search);

	pk_backend_thread_create (thread, (PkBackendThreadFunc) backend_search_thread, params);
}




static gboolean
backend_install_package_thread (PkBackendThread *thread, gchar *package_id)
{
	PkPackageId *pi;
	gint err;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	err = opkg_install_package (opkg, pi->name, pk_opkg_progress_cb, backend);
	if (err != 0)
		opkg_unknown_error (backend, err, "Install");

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err == 0);
}

static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	pk_backend_no_percentage_updates (backend);
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_install_package_thread,
		g_strdup (package_id));
}

static gboolean
backend_remove_package_thread (PkBackendThread *thread, gpointer data[3])
{
	PkPackageId *pi;
	gint err;
	PkBackend *backend;
	gchar *package_id;
	gboolean allow_deps;
	gboolean autoremove;


	package_id = (gchar*) data[0];
	allow_deps = GPOINTER_TO_INT (data[1]);
	autoremove = GPOINTER_TO_INT (data[2]);
	g_free (data);


	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	opkg_set_option (opkg, "autoremove", &autoremove);
	opkg_set_option (opkg, "force_removal_of_dependent_packages", &allow_deps);

	err = opkg_remove_package (opkg, pi->name, pk_opkg_progress_cb, backend);

	/* TODO: improve error reporting */
	if (err != 0)
		opkg_unknown_error (backend, err, "Remove");

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err == 0);
}

static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	gpointer *params;

	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_no_percentage_updates (backend);

	/* params is a small array we can pack our thread parameters into */
	params = g_new0 (gpointer, 2);

	params[0] = g_strdup (package_id);
	params[1] = GINT_TO_POINTER (allow_deps);
	params[2] = GINT_TO_POINTER (autoremove);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_remove_package_thread,
		params);

}

/**
 * backend_get_filters:
 */
static PkFilterEnum
backend_get_filters (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, PK_FILTER_ENUM_UNKNOWN);
	return (PK_FILTER_ENUM_INSTALLED |
		PK_FILTER_ENUM_DEVELOPMENT |
		PK_FILTER_ENUM_GUI);
}


static gboolean
backend_update_system_thread (PkBackendThread *thread, gpointer data)
{
	gint err;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	opkg_upgrade_all (opkg, pk_opkg_progress_cb, backend);

	if (err)
		opkg_unknown_error (backend, err, "Upgrading system");

	pk_backend_finished (backend);
	return (err != 0);
}

static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_update_system_thread,
		NULL);
}

/**
 * backend_update_package:
 */
static gboolean
backend_update_package_thread (PkBackendThread *thread, gchar *package_id)
{
	PkPackageId *pi;
	gint err = 0;
	PkBackend *backend;

	/* get current backend */
	backend = pk_backend_thread_get_backend (thread);

	pi = pk_package_id_new_from_string (package_id);

	if (!pi->name || !pi->version)
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
				"Package not found");
		pk_package_id_free (pi);
		pk_backend_finished (backend);
		return FALSE;
	}

	err = opkg_upgrade_package (opkg, pi->name, pk_opkg_progress_cb, backend);

	if (err != 0) {
		opkg_unknown_error (backend, err, "Update package");
	}

	g_free (package_id);
	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return (err != 0);
}

static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_update_package_thread,
		/* TODO: process the entire list */
		g_strdup (package_ids[0]));
}

/**
 * backend_get_updates:
 */

static void
pk_opkg_list_upgradable_cb (opkg_t *opkg, opkg_package_t *pkg, void *data)
{
	PkBackend *backend = PK_BACKEND (data);
	gchar *uid;
	gint status;

	if (pkg->installed)
		status = PK_INFO_ENUM_INSTALLED;
	else
		status = PK_INFO_ENUM_AVAILABLE;

	uid = g_strdup_printf ("%s;%s;%s;",
		pkg->name, pkg->version, pkg->architecture);

	pk_backend_package (backend, status, uid, pkg->description);
}

static gboolean
backend_get_updates_thread (PkBackendThread *thread, gpointer data)
{
	PkBackend *backend = PK_BACKEND (data);

	opkg_list_upgradable_packages (opkg, pk_opkg_list_upgradable_cb, backend);

	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_updates (PkBackend *backend, PkFilterEnum filters)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
	pk_backend_no_percentage_updates (backend);

	pk_backend_thread_create (thread,
		(PkBackendThreadFunc) backend_get_updates_thread,
		backend);
}

/**
 * backend_get_groups:
 */
static PkGroupEnum
backend_get_groups (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, PK_GROUP_ENUM_UNKNOWN);
	return (PK_GROUP_ENUM_COMMUNICATION |
		PK_GROUP_ENUM_PROGRAMMING |
		PK_GROUP_ENUM_GAMES |
		PK_GROUP_ENUM_OTHER |
		PK_GROUP_ENUM_INTERNET |
		PK_GROUP_ENUM_REPOS |
		PK_GROUP_ENUM_MAPS);
}


PK_BACKEND_OPTIONS (
	"opkg",					/* description */
	"Thomas Wood <thomas@openedhand.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	NULL,					/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_packages */
	NULL,					/* get_repo_list */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	backend_get_updates,			/* get_updates */
	NULL,					/* install_file */
	backend_install_package,		/* install_package */
	NULL,					/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	NULL,					/* repo_enable */
	NULL,					/* repo_set_data */
	NULL,					/* resolve */
	NULL,					/* rollback */
	backend_search_description,		/* search_details */
	NULL,					/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* service_pack */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	NULL					/* what_provides */
);

