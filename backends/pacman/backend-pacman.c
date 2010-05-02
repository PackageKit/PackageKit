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

#include "backend-pacman.h"

PacmanManager *pacman = NULL;

/**
 * backend_initialize:
 **/
static void
backend_initialize (PkBackend *backend)
{
	GError *error = NULL;

	g_return_if_fail (backend != NULL);

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

	/* read configuration from PackageKit pacman config file */
	if (!pacman_manager_configure (pacman, PACMAN_CONFIG_FILE, &error)) {
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

	if (pacman != NULL) {
		g_object_unref (pacman);
	}
}

PK_BACKEND_OPTIONS (
	"pacman",				/* description */
	"Jonathan Conder <j@skurvy.no-ip.org>",	/* author */
	backend_initialize,			/* initialize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	NULL,					/* get_filters */
	NULL,					/* get_roles */
	NULL,					/* get_mime_types */
	NULL,					/* cancel */
	NULL,					/* download_packages */
	NULL,					/* get_categories */
	NULL,					/* get_depends */
	NULL,					/* get_details */
	NULL,					/* get_distro_upgrades */
	NULL,					/* get_files */
	NULL,					/* get_packages */
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
	NULL,					/* resolve */
	NULL,					/* rollback */
	NULL,					/* search_details */
	NULL,					/* search_files */
	NULL,					/* search_groups */
	NULL,					/* search_names */
	NULL,					/* update_packages */
	NULL,					/* update_system */
	NULL,					/* what_provides */
	NULL,					/* simulate_install_files */
	NULL,					/* simulate_install_packages */
	NULL,					/* simulate_remove_packages */
	NULL					/* simulate_update_packages */
);
