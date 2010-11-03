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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <pk-backend-internal.h>

#include <razor/razor.h>

static struct razor_set *set = NULL;
static const char *repo_filename = "/home/hughsie/Code/razor/src/system.repo";
static const char *system_details = "/home/hughsie/Code/razor/src/system-details.repo";

typedef enum {
	PK_RAZOR_SEARCH_TYPE_NAME,
	PK_RAZOR_SEARCH_TYPE_SUMMARY
} PkRazorSearchType;

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	set = razor_set_open (repo_filename);
	razor_set_open_details (set, system_details);
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	razor_set_destroy (set);
}

/**
 * pk_razor_filter_devel:
 */
static gboolean
pk_razor_filter_devel (const gchar *name)
{
	if (g_str_has_suffix (name, "-debuginfo"))
		return TRUE;
	if (g_str_has_suffix (name, "-devel"))
		return TRUE;
	if (g_str_has_suffix (name, "-libs"))
		return TRUE;
	return FALSE;
}

/**
 * pk_razor_emit_package:
 */
static gboolean
pk_razor_emit_package (PkBackend *backend, const gchar *name, const gchar *version, const gchar *arch, const gchar *summary)
{
	PkBitfield filters;
	gchar *package_id;
	gboolean ret;

	filters = pk_backend_get_uint (backend, "filters");

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
		ret = pk_razor_filter_devel (name);
		if (!ret)
			return FALSE;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		ret = pk_razor_filter_devel (name);
		if (ret)
			return FALSE;
	}

	package_id = pk_package_id_build (name, version, arch, "installed");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED, package_id, summary);
	g_free (package_id);
	return TRUE;
}

static gboolean
backend_resolve_thread (PkBackend *backend)
{
	guint i;
	guint length;
	struct razor_package_iterator *pi;
	struct razor_package *package;
	const gchar *name, *version, *arch, *summary;
	gchar **package_ids;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	length = g_strv_length (package_ids);

	pi = razor_package_iterator_create (set);
	while (razor_package_iterator_next (pi, &package,
					    RAZOR_DETAIL_NAME, &name,
					    RAZOR_DETAIL_VERSION, &version,
					    RAZOR_DETAIL_ARCH, &arch,
					    RAZOR_DETAIL_SUMMARY, &summary,
					    RAZOR_DETAIL_LAST)) {
		for (i=0; i<length; i++) {
			if (g_strcmp0 (name, package_ids[i]) == 0) {
				pk_razor_emit_package (backend, name, version, arch, summary);
			}
		}
	}

	razor_package_iterator_destroy (pi);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_thread_create (backend, backend_resolve_thread);
}

/**
 * backend_get_details:
 */
static gboolean
backend_get_details_thread (PkBackend *backend)
{
	guint i;
	guint length;
	struct razor_package_iterator *pi;
	struct razor_package *package;
	const gchar *name, *version, *arch, *summary, *description, *url, *license;
	gchar *package_id;
	gchar **package_ids;
	PkPackageId *id;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	length = g_strv_length (package_ids);

	pi = razor_package_iterator_create (set);
	while (razor_package_iterator_next (pi, &package,
					    RAZOR_DETAIL_NAME, &name,
					    RAZOR_DETAIL_VERSION, &version,
					    RAZOR_DETAIL_ARCH, &arch,
					    RAZOR_DETAIL_LAST)) {
		for (i=0; i<length; i++) {
			/* TODO: we should cache this */
			id = pk_package_id_new_from_string (package_ids[i]);
			if (g_strcmp0 (name, id->name) == 0) {
				package_id = pk_package_id_build (name, version, arch, "installed");
				razor_package_get_details (set, package,
							   RAZOR_DETAIL_SUMMARY, &summary,
							   RAZOR_DETAIL_DESCRIPTION, &description,
							   RAZOR_DETAIL_URL, &url,
							   RAZOR_DETAIL_LICENSE, &license,
							   RAZOR_DETAIL_LAST);
				pk_backend_details (backend, package_ids[i], license, PK_GROUP_ENUM_UNKNOWN, description, url, 0);
				g_free (package_id);
			}
			pk_package_id_free (id);
		}
	}

	razor_package_iterator_destroy (pi);
	pk_backend_finished (backend);
	return TRUE;
}

static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_thread_create (backend, backend_get_details_thread);
}

/**
 * backend_resolve_package_id:
 */
static struct razor_package *
backend_resolve_package_id (const PkPackageId *id)
{
	struct razor_package_iterator *pi;
	struct razor_package *package;
	struct razor_package *package_retval = NULL;
	const gchar *name, *version, *arch;

	pi = razor_package_iterator_create (set);
	while (razor_package_iterator_next (pi, &package,
					    RAZOR_DETAIL_NAME, &name,
					    RAZOR_DETAIL_VERSION, &version,
					    RAZOR_DETAIL_ARCH, &arch,
					    RAZOR_DETAIL_LAST)) {
		if (g_strcmp0 (name, id->name) == 0) {
			package_retval = package;
			break;
		}
	}
	razor_package_iterator_destroy (pi);
	return package_retval;
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	guint i;
	guint length;
	const gchar *package_id;
	struct razor_package *package;
	PkPackageId *id;

	length = g_strv_length (package_ids);
	for (i=0; i<length; i++) {
		package_id = package_ids[i];
		id = pk_package_id_new_from_string (package_id);
		/* TODO: we need to get this list! */
		package = backend_resolve_package_id (id);
		razor_set_list_package_files (set, package);
		pk_backend_files (backend, package_id, "/usr/bin/dave;/usr/share/brian");
		pk_package_id_free (id);
	}
	pk_backend_finished (backend);
}

static gboolean
backend_get_packages_thread (PkBackend *backend)
{
	struct razor_package_iterator *pi;
	struct razor_package *package;
	const gchar *name, *version, *arch, *summary;

	pi = razor_package_iterator_create (set);
	while (razor_package_iterator_next (pi, &package,
					    RAZOR_DETAIL_NAME, &name,
					    RAZOR_DETAIL_VERSION, &version,
					    RAZOR_DETAIL_ARCH, &arch,
					    RAZOR_DETAIL_SUMMARY, &summary,
					    RAZOR_DETAIL_LAST)) {
		pk_razor_emit_package (backend, name, version, arch, summary);
	}

	razor_package_iterator_destroy (pi);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_get_packages:
 */
static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_thread_create (backend, backend_get_packages_thread);
}

/**
 * pk_str_case_contains:
 */
static gboolean
pk_str_case_contains (const gchar *haystack, const gchar *needle)
{
	gint ret;
	guint i;
	guint haystack_length;
	guint needle_length;

	haystack_length = egg_strlen (haystack, 1024);
	needle_length = egg_strlen (needle, 1024);

	/* needle longer than haystack */
	if (needle_length > haystack_length) {
		return FALSE;
	}

	/* search case insensitive */
	for (i=0; i<haystack_length - needle_length; i++) {
		ret = g_ascii_strncasecmp (haystack+i, needle, needle_length);
		if (ret == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * backend_search_thread:
 */
static gboolean
backend_search_thread (PkBackend *backend)
{
	struct razor_package_iterator *pi;
	struct razor_package *package;
	const gchar *name, *version, *arch, *summary, *description;
	PkRazorSearchType type;
	gboolean found;
	const gchar *search;

	type = pk_backend_get_uint (backend, "search-type");
	search = pk_backend_get_string (backend, "search");

	pi = razor_package_iterator_create (set);
	while (razor_package_iterator_next (pi, &package,
					    RAZOR_DETAIL_NAME, &name,
					    RAZOR_DETAIL_VERSION, &version,
					    RAZOR_DETAIL_ARCH, &arch,
					    RAZOR_DETAIL_SUMMARY, &summary,
					    RAZOR_DETAIL_DESCRIPTION, &description,
					    RAZOR_DETAIL_LAST)) {

		/* find in the name */
		found = pk_str_case_contains (name, search);
		if (found) {
			pk_razor_emit_package (backend, name, version, arch, summary);

		/* look in summary and description if we are searching by description */
		} else if (type == PK_RAZOR_SEARCH_TYPE_SUMMARY) {
			found = pk_str_case_contains (summary, search);
			if (!found) {
				found = pk_str_case_contains (description, search);
			}
			if (found) {
				pk_razor_emit_package (backend, name, version, arch, summary);
			}
		}
	}

	razor_package_iterator_destroy (pi);
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_uint (backend, "search-type", PK_RAZOR_SEARCH_TYPE_NAME);
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_search_description:
 */
static void
backend_search_description (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_uint (backend, "search-type", PK_RAZOR_SEARCH_TYPE_SUMMARY);
	pk_backend_thread_create (backend, backend_search_thread);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_DEVELOPMENT, -1);
}

/* FIXME: port this away from PK_BACKEND_OPTIONS */
PK_BACKEND_OPTIONS (
	"razor",				/* description */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* get_roles */
	NULL,					/* get_mime_types */
	NULL,					/* cancel */
	NULL,					/* download_packages */
	NULL,					/* get_categories */
	NULL,					/* get_depends */
	backend_get_details,			/* get_details */
	NULL,					/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	backend_get_packages,			/* get_packages */
	NULL,					/* get_repo_list */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	NULL,					/* install_files */
	NULL,					/* install_packages */
	NULL,					/* install_signature */
	NULL,					/* refresh_cache */
	NULL,					/* remove_packages */
	NULL,					/* repo_enable */
	NULL,					/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_description,		/* search_details */
	NULL,					/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_packages */
	NULL,					/* update_system */
	NULL,					/* what_provides */
	NULL,					/* simulate_install_files */
	NULL,					/* simulate_install_packages */
	NULL,					/* simulate_remove_packages */
	NULL,					/* simulate_update_packages */
	NULL,					/* upgrade_system */
	NULL,					/* transaction_start */
	NULL					/* transaction_stop */
);

