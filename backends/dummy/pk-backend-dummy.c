/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

static guint progress_percentage;

/**
 * backend_initalize:
 */
static void
backend_initalize (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	progress_percentage = 0;
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
}

/**
 * backend_cancel_job_try:
 */
static void
backend_cancel_job_try (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 1, "glib2;2.14.0;i386;fedora",
			 "The GLib library");
	pk_backend_package (backend, 1, "gtk2;gtk2-2.11.6-6.fc8;i386;fedora",
			 "GTK+ Libraries for GIMP");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_description (backend, "gnome-power-manager;2.6.19;i386;fedora", PK_GROUP_ENUM_PROGRAMMING,
"Scribus is an desktop open source page layout program with "
"the aim of producing commercial grade output in PDF and "
"Postscript, primarily, though not exclusively for Linux.\n"
"\n"
"While the goals of the program are for ease of use and simple easy to "
"understand tools, Scribus offers support for professional publishing "
"features, such as CMYK color, easy PDF creation, Encapsulated Postscript "
"import/export and creation of color separations.", "http://live.gnome.org/GnomePowerManager");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 1, "glib2;2.14.0;i386;fedora",
			 "The GLib library");
	pk_backend_package (backend, 1, "gtk2;gtk2-2.11.6-6.fc8;i386;fedora",
			 "GTK+ Libraries for GIMP");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "powertop;1.8-1.fc8;i386;fedora",
			 "Power consumption monitor");
	pk_backend_package (backend, 1, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
			 "The Linux kernel (the core of the Linux operating system)");
	pk_backend_package (backend, 1, "gtkhtml2;2.19.1-4.fc8;i386;fedora", "An HTML widget for GTK+ 2.0");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

static gboolean
backend_install_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (progress_percentage == 100) {
		pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
		return FALSE;
	}
	if (progress_percentage == 50) {
		pk_backend_change_job_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	progress_percentage += 10;
	pk_backend_change_percentage (backend, progress_percentage);
	return TRUE;
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	progress_percentage = 0;
	g_timeout_add (1000, backend_install_timeout, backend);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps)
{
	g_return_if_fail (backend != NULL);
	pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "No network connection available");
	pk_backend_finished (backend, PK_EXIT_ENUM_FAILED);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

/**
 * backend_search_name_timeout:
 **/
gboolean
backend_search_name_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	pk_backend_package (backend, 1, "evince;0.9.3-5.fc8;i386;installed",
			 "PDF Document viewer");
	pk_backend_package (backend, 1, "tetex;3.0-41.fc8;i386;fedora",
			 "TeTeX is an implementation of TeX for Linux or UNIX systems.");
	pk_backend_package (backend, 0, "scribus;1.3.4-1.fc8;i386;fedora",
			 "Scribus is an desktop open source page layout program");
	pk_backend_package (backend, 0, "vips-doc;7.12.4-2.fc8;noarch;linva",
			 "The vips documentation package.");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
	return FALSE;
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_no_percentage_updates (backend);
	g_timeout_add (2000, backend_search_name_timeout, backend);
}

/**
 * backend_update_package:
 */
static void
backend_update_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, 1, package_id, "The same thing");
	pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
}

static gboolean
backend_update_system_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (progress_percentage == 100) {
		pk_backend_finished (backend, PK_EXIT_ENUM_SUCCESS);
		return FALSE;
	}
	pk_backend_change_job_status (backend, PK_STATUS_ENUM_UPDATE);
	progress_percentage += 10;
	pk_backend_change_percentage (backend, progress_percentage);
	return TRUE;
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_change_job_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	progress_percentage = 0;
	pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, NULL);
	g_timeout_add (1000, backend_update_system_timeout, backend);
}

PK_BACKEND_OPTIONS (
	"Dummy Backend",			/* description */
	"0.0.1",				/* version */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	backend_initalize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_cancel_job_try,			/* cancel_job_try */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_requires,			/* get_requires */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_package,			/* update_package */
	backend_update_system			/* update_system */
);

