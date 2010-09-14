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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include <pk-backend.h>

/* static bodges */
static guint _progress_percentage = 0;
static gulong _signal_timeout = 0;
static gchar **_package_ids;
static gchar **_values;
static guint _package_current = 0;
static gboolean _repo_enabled_local = FALSE;
static gboolean _repo_enabled_fedora = TRUE;
static gboolean _repo_enabled_devel = TRUE;
static gboolean _repo_enabled_livna = TRUE;
static gboolean _updated_gtkhtml = FALSE;
static gboolean _updated_kernel = FALSE;
static gboolean _updated_powertop = FALSE;
static gboolean _has_signature = FALSE;
static gboolean _use_blocked = FALSE;
static gboolean _use_eula = FALSE;
static gboolean _use_media = FALSE;
static gboolean _use_gpg = FALSE;
static gboolean _use_trusted = TRUE;
static gboolean _use_distro_upgrade = FALSE;
static PkBitfield _filters = 0;

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
	_progress_percentage = 0;
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
}

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		-1);
}

/**
 * backend_get_mime_types:
 */
static gchar *
backend_get_mime_types (PkBackend *backend)
{
	return g_strdup ("application/x-rpm;application/x-deb");
}

/**
 * backend_cancel_timeout:
 */
static gboolean
backend_cancel_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	/* we can now cancel again */
	_signal_timeout = 0;

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
	/* cancel the timeout */
	if (_signal_timeout != 0) {
		g_source_remove (_signal_timeout);

		/* emulate that it takes us a few ms to cancel */
		g_timeout_add (1500, backend_cancel_timeout, backend);
	}
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (g_strcmp0 (package_ids[0], "scribus;1.3.4-1.fc8;i386;fedora") == 0) {
		pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
				    "scribus-clipart;1.3.4-1.fc8;i386;fedora", "Clipart for scribus");
	} else {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
				    "glib2;2.14.0;i386;fedora", "The GLib library");
		pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
				    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	}
	pk_backend_finished (backend);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	guint i;
	guint len;
	const gchar *package_id;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* each one has a different detail for testing */
	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
		package_id = package_ids[i];
		if (g_strcmp0 (package_id, "powertop;1.8-1.fc8;i386;fedora") == 0) {
			pk_backend_details (backend, "powertop;1.8-1.fc8;i386;fedora", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
					    "PowerTOP is a tool that finds the software component(s) that make your "
					    "computer use more power than necessary while it is idle.", "http://live.gnome.org/powertop", 101*1024);
		} else if (g_strcmp0 (package_id, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed") == 0) {
			pk_backend_details (backend, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
					    "The kernel package contains the Linux kernel (vmlinuz), the core of any "
					    "Linux operating system.  The kernel handles the basic functions of the "
					    "operating system: memory allocation, process allocation, device input "
					    "and output, etc.", "http://www.kernel.org", 33*1024*1024);
		} else if (g_strcmp0 (package_id, "gtkhtml2;2.19.1-4.fc8;i386;fedora") == 0) {
			pk_backend_details (backend, "gtkhtml2;2.19.1-4.fc8;i386;fedora", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
					    "GtkHTML2 (sometimes called libgtkhtml) is a widget for displaying html "
					    "pages.", "http://live.gnome.org/gtkhtml", 133*1024);
		} else if (g_strcmp0 (package_id, "vino;2.24.2.fc9;i386;fedora") == 0) {
			pk_backend_details (backend, "vino;2.24.2.fc9;i386;fedora", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
					    "Vino is a VNC server for GNOME. It allows remote users to "
					    "connect to a running GNOME session using VNC.", "http://live.gnome.org/powertop", 3*1024*1024);
		} else if (g_strcmp0 (package_id, "gnome-power-manager;2.6.19;i386;fedora") == 0) {
			pk_backend_details (backend, "gnome-power-manager;2.6.19;i386;fedora", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
					    "GNOME Power Manager uses the information and facilities provided by HAL "
					    "displaying icons and handling user callbacks in an interactive GNOME session.\n"
					    "GNOME Power Preferences allows authorised users to set policy and "
					    "change preferences.", "http://projects.gnome.org/gnome-power-manager/", 13*1024*1024);
		//TODO: add other packages
		} else {
			pk_backend_details (backend, "scribus;1.3.4-1.fc8;i386;fedora", "GPL2", PK_GROUP_ENUM_PROGRAMMING,
					    "Scribus is an desktop *open source* page layöut program with "
					    "the aim of producing commercial grade output in **PDF** and "
					    "**Postscript**, primarily, though not exclusively for Linux.\n"
					    "\n"
					    "While the goals of the program are for ease of use and simple easy to "
					    "understand tools, Scribus offers support for professional publishing "
					    "features, such as CMYK color, easy PDF creation, Encapsulated Postscript "
					    "import/export and creation of color separations.", "http://live.gnome.org/scribus", 44*1024*1024);
		}
	}
	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
}

/**
 * backend_get_distro_upgrades:
 */
static void
backend_get_distro_upgrades (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	if (!_use_distro_upgrade)
		goto out;
	pk_backend_distro_upgrade (backend, PK_DISTRO_UPGRADE_ENUM_STABLE,
				   "fedora-9", "Fedora 9");
	pk_backend_distro_upgrade (backend, PK_DISTRO_UPGRADE_ENUM_UNSTABLE,
				   "fedora-10-rc1", "Fedora 10 RC1");
out:
	pk_backend_finished (backend);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	guint i;
	guint len;
	const gchar *package_id;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	len = g_strv_length (package_ids);
	for (i=0; i<len; i++) {
		package_id = package_ids[i];
		if (g_strcmp0 (package_id, "powertop;1.8-1.fc8;i386;fedora") == 0)
			pk_backend_files (backend, package_id, "/usr/share/man/man1/boo;/usr/bin/xchat-gnome");
		else if (g_strcmp0 (package_id, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed") == 0)
			pk_backend_files (backend, package_id, "/usr/share/man/man1;/usr/share/man/man1/gnome-power-manager.1.gz;/usr/lib/firefox-3.5.7/firefox");
		else if (g_strcmp0 (package_id, "gtkhtml2;2.19.1-4.fc8;i386;fedora") == 0)
			pk_backend_files (backend, package_id, "/usr/share/man/man1;/usr/bin/ck-xinit-session;/lib/libselinux.so.1");
		else
			pk_backend_files (backend, package_id, "/usr/share/gnome-power-manager;/usr/bin/ck-xinit-session");
	}
	pk_backend_finished (backend);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
}

/**
 * backend_get_update_detail_timeout:
 **/
static gboolean
backend_get_update_detail_timeout (gpointer data)
{
	guint i;
	guint len;
	const gchar *package_id;
	PkBackend *backend = (PkBackend *) data;
	const gchar *changelog;

	/* dummy */
	changelog = "**Thu Mar 12 2009** Adam Jackson <ajax@redhat.com> 1.6.0-13\n"
		    "- xselinux-1.6.0-selinux-nlfd.patch: Acquire the netlink socket from selinux,\n"
		    "  check it ourselves rather than having libselinux bang on it all the time.\n"
		    "\n"
		    "**Wed Mar 11 2009** Adam Jackson <ajax@redhat.com> 1.6.0-10\n"
		    "- xserver-1.6.0-selinux-less.patch: Don't init selinux unless the policy\n"
		    "  says to be an object manager.\n"
		    "\n"
		    "**Wed Mar 11 2009** Adam Jackson <ajax@redhat.com> 1.6.0-11\n"
		    "- xserver-1.6.0-less-acpi-brokenness.patch: Don't build the (broken)\n"
		    "  ACPI code.\n"
		    "\n"
		    "**Wed Mar 11 2009** Adam Jackson <ajax@redhat.com> 1.6.0-12\n"
		    "- Requires: pixman >= 0.14.0\n"
		    "\n"
		    "**Fri Mar  6 2009** Adam Jackson <ajax@redhat.com> 1.6.0-8\n"
		    "- xserver-1.6.0-primary.patch: Really, only look at VGA devices. (#488869)\n";

	/* each one has a different detail for testing */
	pk_backend_set_percentage (backend, 0);
	len = g_strv_length (_package_ids);
	for (i=0; i<len; i++) {
		package_id = _package_ids[i];
		if (g_strcmp0 (package_id, "powertop;1.8-1.fc8;i386;fedora") == 0) {
			pk_backend_update_detail (backend, package_id,
						  "powertop;1.7-1.fc8;i386;installed", "",
						  "http://www.distro-update.org/page?moo;Bugfix release for powertop",
						  "http://bgzilla.fd.org/result.php?#12344;Freedesktop Bugzilla #12344",
						  "", PK_RESTART_ENUM_NONE, "Update to newest upstream source",
						  changelog, PK_UPDATE_STATE_ENUM_STABLE, "2009-11-17T09:19:00", "2009-11-19T09:19:00");
		} else if (g_strcmp0 (package_id, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed") == 0) {
			pk_backend_update_detail (backend, package_id,
						  "kernel;2.6.22-0.104.rc3.git6.fc8;i386;installed"
						  PK_PACKAGE_IDS_DELIM
						  "kernel;2.6.22-0.105.rc3.git7.fc8;i386;installed", "",
						  "http://www.distro-update.org/page?moo;Bugfix release for kernel",
						  "http://bgzilla.fd.org/result.php?#12344;Freedesktop Bugzilla #12344;"
						  "http://bgzilla.gnome.org/result.php?#9876;GNOME Bugzilla #9876",
						  "http://nvd.nist.gov/nvd.cfm?cvename=CVE-2007-3381;CVE-2007-3381",
						  PK_RESTART_ENUM_SYSTEM,
						  "Update to newest upstream version.\n"
						  "* This should fix many driver bugs when using nouveau\n"
						  " * This also introduces the new `frobnicator` driver for *vibrating* rabbit hardware.",
						  changelog, PK_UPDATE_STATE_ENUM_UNSTABLE, "2008-06-28T09:19:00", NULL);
		} else if (g_strcmp0 (package_id, "gtkhtml2;2.19.1-4.fc8;i386;fedora") == 0) {
			pk_backend_update_detail (backend, package_id,
						  "gtkhtml2;2.18.1-22.fc8;i386;installed", "",
						  "http://www.distro-update.org/page?moo;Bugfix release for gtkhtml",
						  "http://bgzilla.gnome.org/result.php?#9876;GNOME Bugzilla #9876",
						  NULL, PK_RESTART_ENUM_SESSION,
						  "Update to latest *whizz* **bang** version\n"
						  "* support this new thing\n"
						  "* something else\n"
						  "- and that new thing",
						  changelog, PK_UPDATE_STATE_ENUM_UNKNOWN, "2008-07-25T09:19:00", NULL);

		} else if (g_strcmp0 (package_id, "vino;2.24.2.fc9;i386;fedora") == 0) {
			pk_backend_update_detail (backend, package_id,
						  "vino;2.24.1.fc9;i386;fedora", "",
						  "", "", NULL, PK_RESTART_ENUM_NONE,
						  "Cannot get update as update conflics with vncviewer",
						  changelog, PK_UPDATE_STATE_ENUM_UNKNOWN, "2008-07-25", NULL);
		} else {
			/* signal to UI */
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "the package update detail was not found for %s", package_id);
		}
	}
	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	_signal_timeout = 0;
	return FALSE;
}

/**
 * backend_get_update_detail:
 */
static void
backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	_package_ids = package_ids;
	_signal_timeout = g_timeout_add (500, backend_get_update_detail_timeout, backend);
}

/**
 * backend_get_updates_timeout:
 **/
static gboolean
backend_get_updates_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	if (_use_blocked) {
		if (!_updated_powertop && !_updated_kernel && !_updated_gtkhtml) {
			pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
					    "vino;2.24.2.fc9;i386;fedora",
					    "Remote desktop server for the desktop");
		}
	}
	if (!_updated_powertop) {
		pk_backend_package (backend, PK_INFO_ENUM_NORMAL,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
	}
	if (!_updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_BUGFIX,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	if (!_updated_gtkhtml) {
		pk_backend_package (backend, PK_INFO_ENUM_SECURITY,
				    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
				    "An HTML widget for GTK+ 2.0");
	}
	pk_backend_finished (backend);
	_signal_timeout = 0;
	return FALSE;
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot check when offline");
		pk_backend_finished (backend);
		return;
	}
	_signal_timeout = g_timeout_add (1000, backend_get_updates_timeout, backend);
}

static gboolean
backend_install_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	guint sub_percent;

	if (_progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (_progress_percentage == 30) {
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
				    "An HTML widget for GTK+ 2.0");
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	if (_progress_percentage == 50) {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2-devel;2.19.1-0.fc8;i386;fedora",
				    "Devel files for gtkhtml");
		/* this duplicate package should be ignored */
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2-devel;2.19.1-0.fc8;i386;fedora", NULL);
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	if (_progress_percentage > 30 && _progress_percentage < 50) {
		sub_percent = ((gfloat) (_progress_percentage - 30.0f) / 20.0f) * 100.0f;
		pk_backend_set_sub_percentage (backend, sub_percent);
	} else {
		pk_backend_set_sub_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	}
	_progress_percentage += 1;
	pk_backend_set_percentage (backend, _progress_percentage);
	return TRUE;
}

/**
 * backend_install_packages:
 */
static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	const gchar *license_agreement;
	const gchar *eula_id;
	gboolean has_eula;

	/* FIXME: support only_trusted */

	if (g_strcmp0 (package_ids[0], "vips-doc;7.12.4-2.fc8;noarch;linva") == 0) {
		if (_use_gpg && !_has_signature) {
			pk_backend_repo_signature_required (backend, package_ids[0], "updates",
							    "http://example.com/gpgkey",
							    "Test Key (Fedora) fedora@example.com",
							    "BB7576AC",
							    "D8CC 06C2 77EC 9C53 372F C199 B1EE 1799 F24F 1B08",
							    "2007-10-04", PK_SIGTYPE_ENUM_GPG);
			pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE,
					       "GPG signed package could not be verified");
			pk_backend_finished (backend);
			return;
		}
		eula_id = "eula_hughsie_dot_com";
		has_eula = pk_backend_is_eula_valid (backend, eula_id);
		if (_use_eula && !has_eula) {
			license_agreement = "Narrator: In A.D. 2101, war was beginning.\n"
					    "Captain: What happen ?\n"
					    "Mechanic: Somebody set up us the bomb.\n\n"
					    "Operator: We get signal.\n"
					    "Captain: What !\n"
					    "Operator: Main screen turn on.\n"
					    "Captain: It's you !!\n"
					    "CATS: How are you gentlemen !!\n"
					    "CATS: All your base are belong to us.\n"
					    "CATS: You are on the way to destruction.\n\n"
					    "Captain: What you say !!\n"
					    "CATS: You have no chance to survive make your time.\n"
					    "CATS: Ha Ha Ha Ha ....\n\n"
					    "Operator: Captain!! *\n"
					    "Captain: Take off every 'ZIG' !!\n"
					    "Captain: You know what you doing.\n"
					    "Captain: Move 'ZIG'.\n"
					    "Captain: For great justice.\n";
			pk_backend_eula_required (backend, eula_id, package_ids[0],
						  "CATS Inc.", license_agreement);
			pk_backend_error_code (backend, PK_ERROR_ENUM_NO_LICENSE_AGREEMENT,
					       "licence not installed so cannot install");
			pk_backend_finished (backend);
			return;
		}
		if (_use_media) {
			_use_media = FALSE;
			pk_backend_media_change_required (backend, PK_MEDIA_TYPE_ENUM_DVD, "linux-disk-1of7", "Linux Disc 1 of 7");
			pk_backend_error_code (backend, PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED,
					       "additional media linux-disk-1of7 required");
			pk_backend_finished (backend);
			return;
		}
	}

	if (_use_trusted && only_trusted) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
				       "Can't install as untrusted");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_set_allow_cancel (backend, TRUE);
	_progress_percentage = 0;
	pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
			    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
			    "An HTML widget for GTK+ 2.0");
	_signal_timeout = g_timeout_add (100, backend_install_timeout, backend);
}

/**
 * backend_install_signature:
 */
static void
backend_install_signature (PkBackend *backend, PkSigTypeEnum type,
			   const gchar *key_id, const gchar *package_id)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	if (type == PK_SIGTYPE_ENUM_GPG &&
	    /* egg_strequal (package_id, "vips-doc;7.12.4-2.fc8;noarch;linva") && */
	    g_strcmp0 (key_id, "BB7576AC") == 0) {
		egg_debug ("installed signature %s for %s", key_id, package_id);
		_has_signature = TRUE;
	} else {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE,
				       "GPG key %s not recognised for package_id %s",
				       key_id, package_id);
	}
	pk_backend_finished (backend);
}

/**
 * backend_refresh_cache_timeout:
 */
static gboolean
backend_install_files_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	pk_backend_finished (backend);
	return FALSE;
}

/**
 * backend_install_files:
 */
static void
backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **full_paths)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	pk_backend_set_percentage (backend, 101);
	_signal_timeout = g_timeout_add (2000, backend_install_files_timeout, backend);
}

/**
 * backend_refresh_cache_timeout:
 */
static gboolean
backend_refresh_cache_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (_progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (_progress_percentage == 80)
		pk_backend_set_allow_cancel (backend, FALSE);
	_progress_percentage += 10;
	pk_backend_set_percentage (backend, _progress_percentage);
	return TRUE;
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	_progress_percentage = 0;

	/* reset */
	_updated_gtkhtml = FALSE;
	_updated_kernel = FALSE;
	_updated_powertop = FALSE;

	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	_signal_timeout = g_timeout_add (500, backend_refresh_cache_timeout, backend);
}

/**
 * backend_resolve_timeout:
 */
static gboolean
backend_resolve_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	guint i;
	guint len;
	gchar **packages = _package_ids;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* each one has a different detail for testing */
	len = g_strv_length (packages);
	for (i=0; i<len; i++) {
		if (g_strcmp0 (packages[i], "vips-doc") == 0 || g_strcmp0 (packages[i], "vips-doc;7.12.4-2.fc8;noarch;linva") == 0) {
			if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_INSTALLED)) {
				pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
						    "vips-doc;7.12.4-2.fc8;noarch;linva", "The vips documentation package.");
			}
		} else if (g_strcmp0 (packages[i], "glib2") == 0 || g_strcmp0 (packages[i], "glib2;2.14.0;i386;fedora") == 0) {
			if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
				pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
						    "glib2;2.14.0;i386;fedora", "The GLib library");
			}
		} else if (g_strcmp0 (packages[i], "powertop") == 0 || g_strcmp0 (packages[i], "powertop;1.8-1.fc8;i386;fedora") == 0)
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
					    "powertop;1.8-1.fc8;i386;fedora", "Power consumption monitor");
		else if (g_strcmp0 (packages[i], "kernel") == 0 || g_strcmp0 (packages[i], "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed") == 0)
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
					    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed", "The Linux kernel (the core of the Linux operating system)");
		else if (g_strcmp0 (packages[i], "gtkhtml2") == 0 || g_strcmp0 (packages[i], "gtkhtml2;2.19.1-4.fc8;i386;fedora") == 0)
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora", "An HTML widget for GTK+ 2.0");
	}
	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);

	/* never repeat */
	return FALSE;
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	_filters = filters;
	_package_ids = packages;
	_signal_timeout = g_timeout_add (20, backend_resolve_timeout, backend);
}

/**
 * backend_rollback_timeout:
 */
static gboolean
backend_rollback_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (_progress_percentage == 0) {
		_updated_gtkhtml = FALSE;
		_updated_kernel = FALSE;
		_updated_powertop = FALSE;
		pk_backend_set_status (backend, PK_STATUS_ENUM_ROLLBACK);
	}
	if (_progress_percentage == 20)
		pk_backend_set_allow_cancel (backend, FALSE);
	if (_progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	_progress_percentage += 10;
	pk_backend_set_percentage (backend, _progress_percentage);
	return TRUE;
}


/**
 * backend_rollback:
 */
static void
backend_rollback (PkBackend *backend, const gchar *transaction_id)
{
	/* allow testing error condition */
	if (g_strcmp0 (transaction_id, "/397_eeecadad_data") == 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_ERROR, "invalid transaction_id");
		pk_backend_finished (backend);
		return;
	}
	_progress_percentage = 0;
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	_signal_timeout = g_timeout_add (2000, backend_rollback_timeout, backend);
}

/**
 * backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "No network connection available");
	pk_backend_finished (backend);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva",
			    "The vips \"documentation\" package.");
	pk_backend_finished (backend);
}

/**
 * backend_search_files:
 */
static void
backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, TRUE);
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
				    "vips-doc;7.12.4-2.fc8;noarch;linva",
				    "The vips documentation package");
	else
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
				    "vips-doc;7.12.4-2.fc8;noarch;linva",
				    "The vips documentation package");
	pk_backend_finished (backend);
}

/**
 * backend_search_groups:
 */
static void
backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, TRUE);
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
	gchar *locale;
	PkBackend *backend = (PkBackend *) data;
	locale = pk_backend_get_locale (backend);

	egg_debug ("locale is %s", locale);
	if (g_strcmp0 (locale, "en_GB.utf8") != 0) {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
				    "evince;0.9.3-5.fc8;i386;installed",
				    "PDF Dokument Ƥrŏgrȃɱ");
	} else {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
				    "evince;0.9.3-5.fc8;i386;installed",
				    "PDF Document viewer");
	}
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
 * backend_search_names:
 */
static void
backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	_signal_timeout = g_timeout_add (2000, backend_search_name_timeout, backend);
}

/**
 * backend_update_packages_download_timeout:
 **/
static gboolean
backend_update_packages_download_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	guint sub;

	if (_progress_percentage == 100) {
		if (_use_blocked) {
			pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			_updated_gtkhtml = FALSE;
		}
		pk_backend_finished (backend);
		return FALSE;
	}
	if (_progress_percentage == 0 && !_updated_powertop) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
		pk_backend_set_sub_percentage (backend, 0);
	}
	if (_progress_percentage == 20 && !_updated_kernel) {
		pk_backend_set_sub_percentage (backend, 100);
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		pk_backend_set_sub_percentage (backend, 0);
		pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed");
	}
	if (_progress_percentage == 30 && !_updated_gtkhtml) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS, "A newer package preupgrade is available in fedora-updates-testing");
		pk_backend_message (backend, PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED, "/etc/X11/xorg.conf has been auto-merged, please check before rebooting");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-debuginfo metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-source metadata is invalid");
		pk_backend_set_sub_percentage (backend, 100);
		if (!_use_blocked) {
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			_updated_gtkhtml = TRUE;
		}
		pk_backend_set_sub_percentage (backend, 0);
	}
	if (_progress_percentage == 40 && !_updated_powertop) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_set_sub_percentage (backend, 100);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
		_updated_powertop = TRUE;
		pk_backend_set_sub_percentage (backend, 0);
	}
	if (_progress_percentage == 60 && !_updated_kernel) {
		pk_backend_set_sub_percentage (backend, 100);
		pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		_updated_kernel = TRUE;
		pk_backend_set_sub_percentage (backend, 0);
	}
	if (_progress_percentage == 80 && !_updated_kernel) {
		pk_backend_set_sub_percentage (backend, 100);
		pk_backend_package (backend, PK_INFO_ENUM_CLEANUP,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		pk_backend_set_sub_percentage (backend, 0);
	}
	_progress_percentage += 1;
	pk_backend_set_percentage (backend, _progress_percentage);
	sub = (_progress_percentage % 10) * 10;
	if (sub != 0)
		pk_backend_set_sub_percentage (backend, sub);
	return TRUE;
}

/**
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	const gchar *eula_id;
	const gchar *license_agreement;
	gboolean has_eula;

	/* FIXME: support only_trusted */

	if (_use_gpg && !_has_signature) {
		pk_backend_repo_signature_required (backend, package_ids[0], "updates",
						    "http://example.com/gpgkey",
						    "Test Key (Fedora) fedora@example.com",
						    "BB7576AC",
						    "D8CC 06C2 77EC 9C53 372F C199 B1EE 1799 F24F 1B08",
						    "2007-10-04", PK_SIGTYPE_ENUM_GPG);
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE,
				       "GPG signed package could not be verified");
		pk_backend_finished (backend);
		return;
	}
	eula_id = "eula_hughsie_dot_com";
	has_eula = pk_backend_is_eula_valid (backend, eula_id);
	if (_use_eula && !has_eula) {
		license_agreement = "Narrator: In A.D. 2101, war was beginning.\n"
				    "Captain: What happen ?\n"
				    "Mechanic: Somebody set up us the bomb.\n\n"
				    "Operator: We get signal.\n"
				    "Captain: What !\n"
				    "Operator: Main screen turn on.\n"
				    "Captain: It's you !!\n"
				    "CATS: How are you gentlemen !!\n"
				    "CATS: All your base are belong to us.\n"
				    "CATS: You are on the way to destruction.\n\n"
				    "Captain: What you say !!\n"
				    "CATS: You have no chance to survive make your time.\n"
				    "CATS: Ha Ha Ha Ha ....\n\n"
				    "Operator: Captain!! *\n"
				    "Captain: Take off every 'ZIG' !!\n"
				    "Captain: You know what you doing.\n"
				    "Captain: Move 'ZIG'.\n"
				    "Captain: For great justice.\n";
		pk_backend_eula_required (backend, eula_id, package_ids[0],
					  "CATS Inc.", license_agreement);
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_LICENSE_AGREEMENT,
				       "licence not installed so cannot install");
		pk_backend_finished (backend);
		return;
	}

	_package_ids = package_ids;
	_package_current = 0;
	_progress_percentage = 0;
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_percentage (backend, 0);
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	_signal_timeout = g_timeout_add (200, backend_update_packages_download_timeout, backend);
}

static gboolean
backend_update_system_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (_progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (_progress_percentage == 0 && !_updated_powertop) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
	}
	if (_progress_percentage == 20 && !_updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	if (_progress_percentage == 30 && !_updated_gtkhtml) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS, "A newer package preupgrade is available in fedora-updates-testing");
		pk_backend_message (backend, PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED, "/etc/X11/xorg.conf has been auto-merged, please check before rebooting");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-debuginfo metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-source metadata is invalid");
		if (_use_blocked) {
			pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			_updated_gtkhtml = FALSE;
		} else {
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			_updated_gtkhtml = TRUE;
		}
	}
	if (_progress_percentage == 40 && !_updated_powertop) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
		_updated_powertop = TRUE;
	}
	if (_progress_percentage == 60 && !_updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		_updated_kernel = TRUE;
	}
	if (_progress_percentage == 80 && !_updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_CLEANUP,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	_progress_percentage += 1;
	pk_backend_set_percentage (backend, _progress_percentage);
	pk_backend_set_sub_percentage (backend, (_progress_percentage % 10) * 10);
	return TRUE;
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_allow_cancel (backend, TRUE);
	_progress_percentage = 0;

	/* FIXME: support only_trusted */

	pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed");
	_signal_timeout = g_timeout_add (100, backend_update_system_timeout, backend);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_repo_detail (backend, "fedora",
				"Fedora - 9", _repo_enabled_fedora);
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		pk_backend_repo_detail (backend, "development",
					"Fedora - Development", _repo_enabled_devel);
	}
	pk_backend_repo_detail (backend, "livna-development",
				"Livna for Fedora Core 8 - i386 - Development Tree", _repo_enabled_livna);
	pk_backend_finished (backend);
}

/**
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);

	if (g_strcmp0 (rid, "local") == 0) {
		egg_debug ("local repo: %i", enabled);
		_repo_enabled_local = enabled;
	} else if (g_strcmp0 (rid, "development") == 0) {
		egg_debug ("devel repo: %i", enabled);
		_repo_enabled_devel = enabled;
	} else if (g_strcmp0 (rid, "fedora") == 0) {
		egg_debug ("fedora repo: %i", enabled);
		_repo_enabled_fedora = enabled;
	} else if (g_strcmp0 (rid, "livna-development") == 0) {
		egg_debug ("livna repo: %i", enabled);
		_repo_enabled_livna = enabled;
	} else {
		egg_warning ("unknown repo: %s", rid);
	}
	pk_backend_finished (backend);
}

/**
 * backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	egg_warning ("REPO '%s' PARAMETER '%s' TO '%s'", rid, parameter, value);

	if (g_strcmp0 (parameter, "use-blocked") == 0)
		_use_blocked = atoi (value);
	else if (g_strcmp0 (parameter, "use-eula") == 0)
		_use_eula = atoi (value);
	else if (g_strcmp0 (parameter, "use-media") == 0)
		_use_media = atoi (value);
	else if (g_strcmp0 (parameter, "use-gpg") == 0)
		_use_gpg = atoi (value);
	else if (g_strcmp0 (parameter, "use-trusted") == 0)
		_use_trusted = atoi (value);
	else if (g_strcmp0 (parameter, "use-distro-upgrade") == 0)
		_use_distro_upgrade = atoi (value);
	else
		pk_backend_message (backend, PK_MESSAGE_ENUM_PARAMETER_INVALID, "invalid parameter %s", parameter);
	pk_backend_finished (backend);
}

/**
 * backend_what_provides_timeout:
 */
static gboolean
backend_what_provides_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (_progress_percentage == 100) {
		if (g_strcmp0 (_values[0], "gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)") == 0) {
			pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
					    "gstreamer-plugins-bad;0.10.3-5.lvn;i386;available",
					    "GStreamer streaming media framework \"bad\" plug-ins");
		} else if (g_strcmp0 (_values[0], "gstreamer0.10(decoder-video/x-wma)(wmaversion=3)") == 0) {
			pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
					    "gstreamer-plugins-flumpegdemux;0.10.15-5.lvn;i386;available",
					    "MPEG demuxer for GStreamer");
		} else {
			/* pkcon install vips-doc says it's installed cause evince is INSTALLED */
			if (g_strcmp0 (_values[0], "vips-doc") != 0) {
				if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
					pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
							    "evince;0.9.3-5.fc8;i386;installed",
							    "PDF Document viewer");
				}
				if (!pk_bitfield_contain (_filters, PK_FILTER_ENUM_INSTALLED)) {
					pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
							    "scribus;1.3.4-1.fc8;i386;fedora",
							    "Scribus is an desktop open source page layout program");
				}
			}
		}
		pk_backend_finished (backend);
		return FALSE;
	}
	_progress_percentage += 10;
	pk_backend_set_percentage (backend, _progress_percentage);
	return TRUE;
}

/**
 * backend_what_provides:
 */
static void
backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	_progress_percentage = 0;
	_values = values;
	_signal_timeout = g_timeout_add (200, backend_what_provides_timeout, backend);
	_filters = filters;
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_percentage (backend, _progress_percentage);
}

/**
 * backend_get_packages:
 */
static void
backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "update1;2.19.1-4.fc8;i386;fedora",
			    "The first update");
	pk_backend_finished (backend);
}

/**
 * backend_download_packages:
 */
static void
backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
{
	gchar *filename;

	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);

	/* first package */
	filename = g_build_filename (directory, "powertop-1.8-1.fc8.rpm", NULL);
	g_file_set_contents (filename, "powertop data", -1, NULL);
	pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
			    "powertop;1.8-1.fc8;i386;fedora", "Power consumption monitor");
	pk_backend_files (backend, "powertop;1.8-1.fc8;i386;fedora", filename);
	g_free (filename);

	/* second package */
	filename = g_build_filename (directory, "powertop-common-1.8-1.fc8.rpm", NULL);
	g_file_set_contents (filename, "powertop-common data", -1, NULL);
	pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
			    "powertop-common;1.8-1.fc8;i386;fedora", "Power consumption monitor");
	pk_backend_files (backend, "powertop-common;1.8-1.fc8;i386;fedora", filename);
	g_free (filename);

	pk_backend_finished (backend);
}

/**
 * backend_simulate_install_packages:
 */
static void
backend_simulate_install_packages (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_DEP_RESOLVE);

	pk_backend_package (backend, PK_INFO_ENUM_REMOVING,
			    "powertop;1.8-1.fc8;i386;fedora", "Power consumption monitor");

	pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
			    "gtk2;2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");

	pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
			    "lib7;7.0.1-6.fc13;i386;fedora", "C Libraries");

	pk_backend_package (backend, PK_INFO_ENUM_REINSTALLING,
			    "libssl;3.5.7-2.fc13;i386;fedora", "SSL Libraries");

	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva", "The vips documentation package.");

	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");

	pk_backend_package (backend, PK_INFO_ENUM_DOWNGRADING,
			    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed", "The Linux kernel (the core of the Linux operating system)");

	pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
			    "gtkhtml2;2.19.1-4.fc8;i386;fedora", "An HTML widget for GTK+ 2.0");

	pk_backend_finished (backend);
}

/**
 * backend_transaction_start:
 */
static void
backend_transaction_start (PkBackend *backend)
{
	/* here you would lock the backend */
	pk_backend_message (backend, PK_MESSAGE_ENUM_AUTOREMOVE_IGNORED, "backend is crap");

	/* you can use pk_backend_error_code() here too */
}

/**
 * backend_transaction_stop:
 */
static void
backend_transaction_stop (PkBackend *backend)
{
	/* here you would unlock the backend */
	pk_backend_message (backend, PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED, "backend is crap");

	/* you *cannot* use pk_backend_error_code() here,
	 * unless pk_backend_get_is_error_set() returns FALSE, and
	 * even then it's probably just best to clean up silently */

	/* you cannot do pk_backend_finished() here as well as this is
	 * needed to fire the transaction_stop() vfunc */
}

PK_BACKEND_OPTIONS (
	"Dummy",				/* description */
	"Richard Hughes <richard@hughsie.com>",	/* author */
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
	backend_get_distro_upgrades,		/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	backend_get_packages,			/* get_packages */
	backend_get_repo_list,			/* get_repo_list */
	backend_get_requires,			/* get_requires */
	backend_get_update_detail,		/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_files,			/* install_files */
	backend_install_packages,		/* install_packages */
	backend_install_signature,		/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	backend_resolve,			/* resolve */
	backend_rollback,			/* rollback */
	backend_search_details,			/* search_details */
	backend_search_files,			/* search_files */
	backend_search_groups,			/* search_groups */
	backend_search_names,			/* search_names */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	backend_what_provides,			/* what_provides */
	NULL,					/* simulate_install_files */
	backend_simulate_install_packages,	/* simulate_install_packages */
	NULL,					/* simulate_remove_packages */
	NULL,					/* simulate_update_packages */
	backend_transaction_start,		/* transaction_start */
	backend_transaction_stop		/* transaction_stop */
);

