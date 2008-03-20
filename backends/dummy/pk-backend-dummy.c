/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
#include <pk-common.h>
#include <pk-backend.h>
#include <pk-package-ids.h>

static guint progress_percentage;
static gulong signal_timeout = 0;

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
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
 * backend_get_groups:
 */
static void
backend_get_groups (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_GROUP_ENUM_ACCESSIBILITY,
				      PK_GROUP_ENUM_GAMES,
				      PK_GROUP_ENUM_SYSTEM,
				      -1);
}

/**
 * backend_get_filters:
 */
static void
backend_get_filters (PkBackend *backend, PkEnumList *elist)
{
	g_return_if_fail (backend != NULL);
	pk_enum_list_append_multiple (elist,
				      PK_FILTER_ENUM_GUI,
				      PK_FILTER_ENUM_INSTALLED,
				      PK_FILTER_ENUM_DEVELOPMENT,
				      -1);
}

/**
 * backend_cancel_timeout:
 */
static gboolean
backend_cancel_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	/* we can now cancel again */
	signal_timeout = 0;

	/* now mark as finished */
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED,
			       "The task was stopped successfully");
	pk_backend_finished (backend);
	return FALSE;
}

/**
 * backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	/* cancel the timeout */
	if (signal_timeout != 0) {
		g_source_remove (signal_timeout);

		/* emulate that it takes us a few ms to cancel */
		pk_backend_set_status (backend, PK_STATUS_ENUM_CANCEL);
		g_timeout_add (1500, backend_cancel_timeout, backend);
	}
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
}

/**
 * backend_get_description:
 */
static void
backend_get_description (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_description (backend, "gnome-power-manager;2.6.19;i386;fedora", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
"Scribus is an desktop open source page layout program with "
"the aim of producing commercial grade output in PDF and "
"Postscript, primarily, though not exclusively for Linux.\n"
"\n"
"While the goals of the program are for ease of use and simple easy to "
"understand tools, Scribus offers support for professional publishing "
"features, such as CMYK color, easy PDF creation, Encapsulated Postscript "
"import/export and creation of color separations.", "http://live.gnome.org/GnomePowerManager", 11214665);
	pk_backend_finished (backend);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_files (backend, "gnome-power-manager;2.6.19;i386;fedora",
			  "/usr/share/man/man1;/usr/share/man/man1/gnome-power-manager.1.gz");
	pk_backend_finished (backend);
}
/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, const gchar *filter, const gchar *package_id, gboolean recursive)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* each one has a different detail for testing */
	if (pk_strequal (package_id, "powertop;1.8-1.fc8;i386;fedora")) {
		pk_backend_update_detail (backend, "powertop;1.8-1.fc8;i386;available",
					  "powertop;1.7-1.fc8;i386;installed", "",
					  "http://www.distro-update.org/page?moo;Bugfix release for powertop",
					  "http://bgzilla.fd.org/result.php?#12344;Freedesktop Bugzilla #12344",
					  "", PK_RESTART_ENUM_NONE, "Update to newest upstream source");
	} else if (pk_strequal (package_id, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed")) {
		pk_backend_update_detail (backend, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;available",
					  "kernel;2.6.22-0.105.rc3.git7.fc8;i386;installed", "",
					  "http://www.distro-update.org/page?moo;Bugfix release for kernel",
					  "http://bgzilla.fd.org/result.php?#12344;Freedesktop Bugzilla #12344;"
					  "http://bgzilla.gnome.org/result.php?#9876;GNOME Bugzilla #9876",
					  "http://nvd.nist.gov/nvd.cfm?cvename=CVE-2007-3381;CVE-2007-3381",
					  PK_RESTART_ENUM_SYSTEM, "Update to newest version");
	} else if (pk_strequal (package_id, "gtkhtml2;2.19.1-4.fc8;i386;fedora")) {
		pk_backend_update_detail (backend, "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					  "gtkhtml2;2.18.1-22.fc8;i386;installed", "",
					  "http://www.distro-update.org/page?moo;Bugfix release for gtkhtml",
					  "http://bgzilla.gnome.org/result.php?#9876;GNOME Bugzilla #9876",
					  NULL,
					  PK_RESTART_ENUM_SESSION, "Update to latest whizz bang version");
	} else {
		pk_backend_message (backend, PK_MESSAGE_ENUM_DAEMON, "Got unexpected package_id '%s'", package_id);
	}
	pk_backend_finished (backend);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, const gchar *filter)
{
	guint number;
	GRand *rand;

	g_return_if_fail (backend != NULL);

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	rand = g_rand_new ();
	number = g_rand_int_range (rand, 1, 5);
	g_rand_free (rand);

	/* only find updates one in 5 times */
//	if (number != 1) {
//		pk_backend_finished (backend);
//		return;
//	}

	pk_backend_no_percentage_updates (backend);
	pk_backend_package (backend, PK_INFO_ENUM_NORMAL,
			    "powertop;1.8-1.fc8;i386;fedora",
			    "Power consumption monitor");
	pk_backend_package (backend, PK_INFO_ENUM_SECURITY,
			    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
			    "The Linux kernel (the core of the Linux operating system)");
	pk_backend_package (backend, PK_INFO_ENUM_SECURITY,
			    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
			    "An HTML widget for GTK+ 2.0");
	pk_backend_finished (backend);
}

static gboolean
backend_install_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (progress_percentage == 30) {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
				    "An HTML widget for GTK+ 2.0");
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	if (progress_percentage == 50) {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2-devel;2.19.1-0.fc8;i386;fedora",
				    "Devel files for gtkhtml");
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	progress_percentage += 10;
	pk_backend_set_percentage (backend, progress_percentage);
	return TRUE;
}

/**
 * backend_install_package:
 */
static void
backend_install_package (PkBackend *backend, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);

	if(strcmp(package_id,"signedpackage;1.0-1.fc8;i386;fedora") == 0) {
		pk_backend_repo_signature_required(backend, "updates", "http://example.com/gpgkey",
						   "Test Key (Fedora) fedora@example.com", "BB7576AC",
						   "D8CC 06C2 77EC 9C53 372F  C199 B1EE 1799 F24F 1B08",
						   "2007-10-04", PK_SIGTYPE_ENUM_GPG);
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE,
				       "GPG signed package could not be verified");
		pk_backend_finished (backend);
	}

	progress_percentage = 0;
	pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
			    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
			    "An HTML widget for GTK+ 2.0");
	signal_timeout = g_timeout_add (1000, backend_install_timeout, backend);
}

/**
 * backend_install_file:
 */
static void
backend_install_file (PkBackend *backend, const gchar *full_path)
{
	g_return_if_fail (backend != NULL);
	pk_backend_finished (backend);
}

/**
 * backend_refresh_cache_timeout:
 */
static gboolean
backend_refresh_cache_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	progress_percentage += 10;
	pk_backend_set_percentage (backend, progress_percentage);
	return TRUE;
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	g_return_if_fail (backend != NULL);
	progress_percentage = 0;
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	signal_timeout = g_timeout_add (500, backend_refresh_cache_timeout, backend);
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, const gchar *filter, const gchar *package_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_finished (backend);
}

/**
 * backend_rollback:
 */
static void
backend_rollback (PkBackend *backend, const gchar *transaction_id)
{
	g_return_if_fail (backend != NULL);
	pk_backend_finished (backend);
}

/**
 * backend_remove_package:
 */
static void
backend_remove_package (PkBackend *backend, const gchar *package_id, gboolean allow_deps, gboolean autoremove)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "No network connection available");
	pk_backend_finished (backend);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva",
			    "The vips \"documentation\" package.");
	pk_backend_finished (backend);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva",
			    "The vips documentation package.");
	pk_backend_finished (backend);
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva",
			    "The vips documentation package.");
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "bǣwulf-utf8;0.1;noarch;hughsie",
			    "The bǣwulf server test name.");
	pk_backend_finished (backend);
}

/**
 * backend_search_name_timeout:
 **/
static gboolean
backend_search_name_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "evince;0.9.3-5.fc8;i386;installed",
			    "PDF Document viewer");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "tetex;3.0-41.fc8;i386;fedora",
			    "TeTeX is an implementation of TeX for Linux or UNIX systems.");
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "scribus;1.3.4-1.fc8;i386;fedora",
			    "Scribus is an desktop open source page layout program");
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva",
			    "The vips documentation package.");
	pk_backend_finished (backend);
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
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	signal_timeout = g_timeout_add (2000, backend_search_name_timeout, backend);
}

/**
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gchar **package_ids)
{
	guint i;
	guint len;

	g_return_if_fail (backend != NULL);

	len = pk_package_ids_size (package_ids);
	for (i=0; i<len; i++) {
		pk_debug ("package_ids[%i]=%s", i, package_ids[i]);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING, package_ids[i], "The same thing");
	}
	pk_backend_finished (backend);
}

static gboolean
backend_update_system_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (progress_percentage == 0) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "update1;2.19.1-4.fc8;i386;fedora",
				    "The first update");
	}
	if (progress_percentage == 20) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "update2;2.19.1-4.fc8;i386;fedora",
				    "The second update");
	}
	if (progress_percentage == 30) {
		pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
				    "update3;2.19.1-4.fc8;i386;fedora",
				    "The third update");
	}
	if (progress_percentage == 40) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "update1;2.19.1-4.fc8;i386;fedora",
				    "The first update");
	}
	if (progress_percentage == 60) {
		pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
				    "update2;2.19.1-4.fc8;i386;fedora",
				    "The second update");
	}
	if (progress_percentage == 80) {
		pk_backend_package (backend, PK_INFO_ENUM_CLEANUP,
				    "update1;2.19.1-4.fc8;i386;fedora",
				    "The first update (old version)");
	}
	progress_percentage += 10;
	pk_backend_set_percentage (backend, progress_percentage);
	return TRUE;
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_allow_cancel (backend, TRUE);
	progress_percentage = 0;
	pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, NULL);
	signal_timeout = g_timeout_add (1000, backend_update_system_timeout, backend);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_repo_detail (backend, "development",
				"Fedora - Development", TRUE);
	pk_backend_repo_detail (backend, "development-debuginfo",
				"Fedora - Development - Debug", TRUE);
	pk_backend_repo_detail (backend, "development-source",
				"Fedora - Development - Source", FALSE);
	pk_backend_repo_detail (backend, "livna-development",
				"Livna for Fedora Core 8 - i386 - Development Tree", TRUE);
	pk_backend_repo_detail (backend, "livna-development-debuginfo",
				"Livna for Fedora Core 8 - i386 - Development Tree - Debug", TRUE);
	pk_backend_repo_detail (backend, "livna-development-source",
				"Livna for Fedora Core 8 - i386 - Development Tree - Source", FALSE);
	pk_backend_finished (backend);
}

/**
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	if (enabled == TRUE) {
		pk_warning ("REPO ENABLE '%s'", rid);
	} else {
		pk_warning ("REPO DISABLE '%s'", rid);
	}
	pk_backend_finished (backend);
}

/**
 * backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	g_return_if_fail (backend != NULL);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	pk_warning ("REPO '%s' PARAMETER '%s' TO '%s'", rid, parameter, value);
	pk_backend_finished (backend);
}

/**
 * backend_service_pack:
 */
static void
backend_service_pack (PkBackend *backend, const gchar *location, gboolean enabled)
{
	g_return_if_fail (backend != NULL);
	pk_warning ("service pack %i on %s device", enabled, location);
	pk_backend_finished (backend);
}

/**
 * backend_what_provides:
 */
static void
backend_what_provides (PkBackend *backend, const gchar *filter, PkProvidesEnum provides, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "update1;2.19.1-4.fc8;i386;fedora",
			    "The first update");
	pk_backend_finished (backend);
}

PK_BACKEND_OPTIONS (
	"Dummy",				/* description */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	backend_get_groups,			/* get_groups */
	backend_get_filters,			/* get_filters */
	backend_cancel,				/* cancel */
	backend_get_depends,			/* get_depends */
	backend_get_description,		/* get_description */
	backend_get_files,			/* get_files */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_package,		/* install_package */
	backend_install_file,			/* install_file */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_package,			/* remove_package */
	backend_resolve,			/* resolve */
	backend_rollback,			/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	backend_get_repo_list,			/* get_repo_list */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	backend_service_pack,			/* service_pack */
	backend_what_provides			/* what_provides */
);

