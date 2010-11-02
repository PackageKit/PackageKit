/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008-2009 Sebastian Heinlein <glatzor@ubuntu.com>
 * Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
 * Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
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

#include "config.h"

#include <pk-backend.h>
#include <pk-backend-spawn.h>
#include <string.h>

static PkBackendSpawn *spawn;

/**
  * backend_stderr_cb:
  */
static gboolean
backend_stderr_cb (PkBackend *backend, const gchar *output)
{
	// APT is a little bit chatty on stderr
	if (strstr (output, "W:") != NULL)
		return FALSE;
	if (strstr (output, "E:") != NULL)
		return FALSE;
	// There have been a lot of API changes in python-apt recently
	if (strstr (output, "DeprecationWarning") != NULL)
		return FALSE;
	return TRUE;
}

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	g_debug ("backend: initialize");
	spawn = pk_backend_spawn_new ();
	pk_backend_spawn_set_filter_stderr (spawn, backend_stderr_cb);
	pk_backend_spawn_set_name (spawn, "apt");
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
	g_object_unref (spawn);
}

/**
 * backend_get_mime_types:
 */
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-deb");
}

/**
 * pk_backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	/* this feels bad... */
	pk_backend_spawn_kill (spawn);
}

/**
 * backend_download_packages:
 */
static void
backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "download-packages", directory, package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-depends", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-details", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

#ifdef HAVE_PYTHON_META_RELEASE
/**
 * backend_get_distro_upgrades:
 */
static void
backend_get_distro_upgrades (PkBackend *backend)
{
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-distro-upgrades", NULL);
}
#endif /* HAVE_PYTHON_META_RELEASE */

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn,  "aptBackend.py", "get-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	gchar *package_ids_temp;
	gchar *filters_text;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-requires", filters_text, package_ids_temp, pk_backend_bool_to_string (recursive), NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn,  "aptBackend.py", "get-updates", filters_text, NULL);
	g_free (filters_text);
}

/**
 * backend_get_packages:
 */
static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-packages", filters_text, NULL);
	g_free (filters_text);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-update-detail", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

 /**
 * backend_simulate_install_files:
 */
static void
backend_simulate_install_files (PkBackend *backend, gchar **full_paths)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "simulate-install-files", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_install_packages:
 */
static void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "simulate-install-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_remove_packages:
 */
static void
backend_simulate_remove_packages (PkBackend *backend, gchar **package_ids, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "simulate-remove-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_simulate_update_packages:
 */
static void
backend_simulate_update_packages (PkBackend *backend, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "simulate-update-packages", package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_packages:
 */
static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "install-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_files:
 */
static void
backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = g_strjoinv (PK_BACKEND_SPAWN_FILENAME_DELIM, full_paths);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "install-files", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * backend_install_signature:
 *
FIXME: Not implemented
 
static void
backend_install_signature (PkBackend *backend, PkSigTypeEnum type,
			   const gchar *key_id, const gchar *package_id)
{
	const gchar *type_text;

	type_text = pk_sig_type_enum_to_string (type);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "install-signature", type_text, key_id, package_id, NULL);
} */

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	pk_backend_spawn_helper (spawn, "aptBackend.py", "refresh-cache", pk_backend_bool_to_string (force), NULL);
}

/**
 * pk_backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "remove-packages", pk_backend_bool_to_string (allow_deps), pk_backend_bool_to_string (autoremove), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "search-details", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "search-file", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "search-group", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, gchar **values)
{
	gchar *filters_text;
	gchar *search;
	filters_text = pk_filter_bitfield_to_string (filters);
	search = g_strjoinv ("&", values);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "search-name", filters_text, search, NULL);
	g_free (filters_text);
	g_free (search);
}

/**
 * pk_backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	gchar *package_ids_temp;

	/* send the complete list as stdin */
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "update-packages", pk_backend_bool_to_string (only_trusted), package_ids_temp, NULL);
	g_free (package_ids_temp);
}

/**
 * pk_backend_update_system:
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_spawn_helper (spawn, "aptBackend.py", "update-system", pk_backend_bool_to_string (only_trusted), NULL);
}

/**
 * pk_backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **package_ids)
{
	gchar *filters_text;
	gchar *package_ids_temp;
	filters_text = pk_filter_bitfield_to_string (filters);
	package_ids_temp = pk_package_ids_to_string (package_ids);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "resolve", filters_text, package_ids_temp, NULL);
	g_free (filters_text);
	g_free (package_ids_temp);
}

#ifdef HAVE_PYTHON_SOFTWARE_PROPERTIES
/**
 * pk_backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-repo-list", filters_text, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	if (enabled == TRUE) {
		pk_backend_spawn_helper (spawn, "aptBackend.py", "repo-enable", rid, "true", NULL);
	} else {
		pk_backend_spawn_helper (spawn, "aptBackend.py", "repo-enable", rid, "false", NULL);
	}
}

/**
 * pk_backend_repo_set_data:
 *
FIXME: Not implemented
 
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_spawn_helper (spawn, "aptBackend.py", "repo-set-data", rid, parameter, value, NULL);
}
*/
#endif /* HAVE_PYTHON_SOFTWARE_PROPERTIES */

/**
 * backend_what_provides:
 */
static void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, const gchar *search)
{
	gchar *filters_text;
	const gchar *provides_text;
	provides_text = pk_provides_enum_to_string (provides);
	filters_text = pk_filter_bitfield_to_string (filters);
	pk_backend_spawn_helper (spawn, "aptBackend.py", "what-provides", filters_text, provides_text, search, NULL);
	g_free (filters_text);
}

/**
 * pk_backend_get_categories:
 *
FIXME: Not implemented
static void
backend_get_categories (PkBackend *backend)
{
	pk_backend_spawn_helper (spawn, "aptBackend.py", "get-categories", NULL);
} */

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_GROUP_ENUM_ACCESSORIES,
		PK_GROUP_ENUM_ADMIN_TOOLS,
		PK_GROUP_ENUM_COMMUNICATION,
		PK_GROUP_ENUM_DOCUMENTATION,
		PK_GROUP_ENUM_DESKTOP_GNOME,
		PK_GROUP_ENUM_DESKTOP_KDE,
		PK_GROUP_ENUM_DESKTOP_OTHER,
		PK_GROUP_ENUM_ELECTRONICS,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_GRAPHICS,
		PK_GROUP_ENUM_INTERNET,
		PK_GROUP_ENUM_LEGACY,
		PK_GROUP_ENUM_LOCALIZATION,
		PK_GROUP_ENUM_MULTIMEDIA,
		PK_GROUP_ENUM_NETWORK,
		PK_GROUP_ENUM_OTHER,
		PK_GROUP_ENUM_PROGRAMMING,
		PK_GROUP_ENUM_PUBLISHING,
		PK_GROUP_ENUM_SCIENCE,
		PK_GROUP_ENUM_SYSTEM,
		PK_GROUP_ENUM_UNKNOWN,
		PK_GROUP_ENUM_COLLECTIONS,
		-1);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		PK_FILTER_ENUM_SUPPORTED,
		PK_FILTER_ENUM_FREE,
		PK_FILTER_ENUM_COLLECTIONS,
		-1);
}

/* FIXME: port this away from PK_BACKEND_OPTIONS */
PK_BACKEND_OPTIONS (
	"Apt",					/* description */
	"Sebastian Heinlein <glatzor@ubuntu.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* get_roles */
	backend_get_mime_types,			/* get_mime_types */
	backend_cancel,				/* cancel */
	backend_download_packages,		/* download_packages */
	NULL,					/* get_categories */
	backend_get_depends,			/* get_depends */
	backend_get_details,			/* get_details */
#ifdef HAVE_PYTHON_META_RELEASE
	backend_get_distro_upgrades,		/* get_distro_upgrades */
#else
	NULL,					/* get_distro_upgrades */
#endif /* HAVE_PYTHON_META_RELEASE */
	backend_get_files,			/* get_files */
	backend_get_packages,			/* get_packages */
#ifdef HAVE_PYTHON_SOFTWARE_PROPERTIES
	backend_get_repo_list,			/* get_repo_list */
#else
	NULL,					/* get_repo_list */
#endif /* HAVE_PYTHON_SOFTWARE_PROPERTIES */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_files,			/* install_files */
	backend_install_packages,		/* install_packages */
	NULL,					/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
#ifdef HAVE_PYTHON_SOFTWARE_PROPERTIES
	backend_repo_enable,			/* repo_enable */
#else
	NULL,					/* repo_enable */
#endif /* HAVE_PYTHON_SOFTWARE_PROPERTIES */
	NULL,					/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	backend_what_provides,			/* what_provides */
	backend_simulate_install_files,		/* simulate_install_files */
	backend_simulate_install_packages,	/* simulate_install_packages */
	backend_simulate_remove_packages,	/* simulate_remove_packages */
	backend_simulate_update_packages,	/* simulate_update_packages */
	NULL,					/* upgrade_system */
	NULL,					/* transaction_start */
	NULL					/* transaction_stop */
);


