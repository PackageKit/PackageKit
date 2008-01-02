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


#define IPKG_LIB
#include <libipkg.h>

/* global config structures */
static ipkg_conf_t global_conf;
static args_t args;

int
ipkg_debug (ipkg_conf_t *conf, message_level_t level, char *msg)
{
	if (level == 0)
		printf ("IPKG <%d>: %s", level, msg);
	return 0;
}

/**
 * backend_initalize:
 */
static void
backend_initalize (PkBackend *backend)
{
	int err;
	g_return_if_fail (backend != NULL);

	memset(&global_conf, 0 ,sizeof(global_conf));
	memset(&args, 0 ,sizeof(args));

	args_init (&args);

	/* testing only */
	args.offline_root = "/home/thomas/chroots/openmoko/";
	args.noaction = 1;


	err = ipkg_conf_init (&global_conf, &args);
	if (err) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "init failed");
	}
	args_deinit (&args);
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	ipkg_conf_deinit (&global_conf);
	g_return_if_fail (backend != NULL);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	pkg_t *pkg;
	PkPackageId *pi;
	g_return_if_fail (backend != NULL);

	pi = pk_package_id_new_from_string (package_id);
	pkg = pkg_hash_fetch_by_name_version (&global_conf.pkg_hash, pi->name, pi->version);

	pk_backend_description (backend, pi->name,
	    "unknown", PK_GROUP_ENUM_OTHER, pkg->description, pkg->url, 0, NULL);

	pk_backend_finished (backend);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	int ret;
	g_return_if_fail (backend != NULL);
	pk_backend_no_percentage_updates (backend);

	ipkg_cb_message = ipkg_debug;

	ret = ipkg_lists_update (&args);
	if (ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "update failed");
	}
	pk_backend_finished (backend);
}

/**
 * backend_search_name:
 */
static gboolean
backend_search_name_thread (PkBackend *backend, gchar *search)
{
	int i;
	pkg_vec_t *available;
	pkg_t *pkg;

	g_return_val_if_fail ((search), FALSE);

	ipkg_cb_message = ipkg_debug;

	pk_backend_change_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_no_percentage_updates (backend);

	available = pkg_vec_alloc();
	pkg_hash_fetch_available (&global_conf.pkg_hash, available);
	for (i=0; i < available->len; i++) {
		char *uid;
		pkg = available->pkgs[i];
		if (g_strrstr (pkg->name, search)) {
			uid = g_strdup_printf ("%s;%s;%s;",
				pkg->name, pkg->version, pkg->architecture);

			pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE, uid,pkg->description);
		}
	}

	pkg_vec_free(available);
	pk_backend_finished (backend);

	g_free (search);
	return TRUE;
}

static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	char *foo = g_strdup (search);

	pk_backend_thread_create (backend,(PkBackendThreadFunc) backend_search_name_thread, foo);
}


PK_BACKEND_OPTIONS (
	"ipkg",					/* description */
	"Thomas Wood <thomas@openedhand.com>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	NULL,					/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	backend_get_description,		/* get_description */
	NULL,					/* get_files */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	NULL,					/* install_package */
	NULL,					/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	NULL,					/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	NULL,					/* search_details */
	NULL,					/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL,					/* update_system */
	NULL,					/* get_repo_list */
	NULL,					/* repo_enable */
	NULL					/* repo_set_data */
);

