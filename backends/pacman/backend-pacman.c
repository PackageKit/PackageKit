/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include "backend-depends.h"
#include "backend-error.h"
#include "backend-groups.h"
#include "backend-install.h"
#include "backend-packages.h"
#include "backend-remove.h"
#include "backend-repos.h"
#include "backend-search.h"
#include "backend-transaction.h"
#include "backend-pacman.h"

PacmanManager *pacman = NULL;
GCancellable *cancellable = NULL;

static void
pacman_message_cb (const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer user_data)
{
	g_return_if_fail (message != NULL);
	g_return_if_fail (user_data != NULL);

	/* report important output to PackageKit */
	switch (level) {
		case G_LOG_LEVEL_WARNING:
		case G_LOG_LEVEL_MESSAGE:
			egg_warning ("pacman: %s", message);
			backend_message ((PkBackend *) user_data, message);
			break;

		case G_LOG_LEVEL_INFO:
		case G_LOG_LEVEL_DEBUG:
			egg_debug ("pacman: %s", message);
			break;

		default:
			break;
	}
}

/**
 * backend_initialize:
 **/
static void
backend_initialize (PkBackend *backend)
{
	GError *error = NULL;
	GLogLevelFlags flags = G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG;

	g_return_if_fail (backend != NULL);

	/* handle output from pacman */
	g_log_set_handler ("Pacman", flags, pacman_message_cb, backend);

	/* PATH needs to be set for install scriptlets */
	g_setenv ("PATH", PACMAN_DEFAULT_PATH, FALSE);

	egg_debug ("pacman: initializing");

	/* initialize pacman-glib */
	pacman = pacman_manager_get (&error);
	if (pacman == NULL) {
		egg_error ("pacman: %s", error->message);
		g_error_free (error);
		return;
	}

	/* configure and disable the relevant databases */
	if (!backend_initialize_databases (backend, &error)) {
		egg_error ("pacman: %s", error->message);
		g_error_free (error);
		return;
	}

	/* read the group mapping from a config file */
	if (!backend_initialize_groups (backend, &error)) {
		egg_error ("pacman: %s", error->message);
		g_error_free (error);
		return;
	}

	/* setup better download progress reporting */
	if (!backend_initialize_downloads (backend, &error)) {
		egg_error ("pacman: %s", error->message);
		g_error_free (error);
		return;
	}
}

/**
 * backend_destroy:
 **/
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	egg_debug ("pacman: cleaning up");

	backend_destroy_downloads (backend);
	backend_destroy_groups (backend);
	backend_destroy_databases (backend);

	if (pacman != NULL) {
		g_object_unref (pacman);
	}
}

/**
 * backend_get_filters:
 **/
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, 0);

	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
}

/**
 * backend_get_mime_types:
 **/
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	g_return_val_if_fail (backend != NULL, NULL);

	/* packages currently use .pkg.tar.gz and .pkg.tar.xz */
	return g_strdup ("application/x-compressed-tar;application/x-xz-compressed-tar");
}

void
backend_run (PkBackend *backend, PkStatusEnum status, PkBackendThreadFunc func)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (func != NULL);

	if (cancellable != NULL) {
		egg_warning ("pacman: cancellable was not NULL");
		g_object_unref (cancellable);
	}
	cancellable = g_cancellable_new ();
	pk_backend_set_allow_cancel (backend, TRUE);

	pk_backend_set_status (backend, status);
	pk_backend_thread_create (backend, func);
}

/**
 * backend_cancel:
 **/
static void
backend_cancel (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	if (cancellable != NULL) {
		g_cancellable_cancel (cancellable);
	}
}

gboolean
backend_cancelled (PkBackend *backend)
{
	g_return_val_if_fail (cancellable != NULL, FALSE);
	g_return_val_if_fail (backend != NULL, FALSE);

	if (g_cancellable_is_cancelled (cancellable)) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_CANCEL);
		return TRUE;
	} else {
		return FALSE;
	}
}

void
backend_finished (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);

	pk_backend_set_allow_cancel (backend, FALSE);
	if (cancellable != NULL) {
		g_object_unref (cancellable);
		cancellable = NULL;
	}

	pk_backend_thread_finished (backend);
}

PK_BACKEND_OPTIONS (
	"pacman",				/* description */
	"Jonathan Conder <j@skurvy.no-ip.org>",	/* author */
	backend_initialize,			/* initialize */
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
	NULL,					/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	backend_get_packages,			/* get_packages */
	backend_get_repo_list,			/* get_repo_list */
	backend_get_requires,			/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	backend_install_files,			/* install_files */
	backend_install_packages,		/* install_packages */
	NULL,					/* install_signature */
	NULL,					/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
	backend_repo_enable,			/* repo_enable */
	NULL,					/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_files,			/* search_files */
	backend_search_groups,			/* search_groups */
	backend_search_names,			/* search_names */
	NULL,					/* update_packages */
	NULL,					/* update_system */
	backend_what_provides,			/* what_provides */
	NULL,					/* simulate_install_files */
	backend_simulate_install_packages,	/* simulate_install_packages */
	backend_simulate_remove_packages,	/* simulate_remove_packages */
	NULL					/* simulate_update_packages */
);
