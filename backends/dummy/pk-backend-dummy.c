/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include <pk-backend.h>

typedef struct {
	gboolean	 has_signature;
	gboolean	 repo_enabled_devel;
	gboolean	 repo_enabled_fedora;
	gboolean	 repo_enabled_livna;
	gboolean	 repo_enabled_local;
	gboolean	 updated_gtkhtml;
	gboolean	 updated_kernel;
	gboolean	 updated_powertop;
	gboolean	 use_blocked;
	gboolean	 use_distro_upgrade;
	gboolean	 use_eula;
	gboolean	 use_gpg;
	gboolean	 use_media;
	gboolean	 use_trusted;
	gchar		**package_ids;
	gchar		**values;
	GSocket		*socket;
	guint		 progress_percentage;
	guint		 socket_listen_id;
	gulong		 signal_timeout;
	PkBitfield	 filters;
} PkBackendDummyPrivate;

static PkBackendDummyPrivate *priv;

/**
 * pk_backend_initialize:
 */
void
pk_backend_initialize (PkBackend *backend)
{
	/* create private area */
	priv = g_new0 (PkBackendDummyPrivate, 1);
	priv->repo_enabled_fedora = TRUE;
	priv->repo_enabled_devel = TRUE;
	priv->repo_enabled_livna = TRUE;
	priv->use_trusted = TRUE;
}

/**
 * pk_backend_destroy:
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_free (priv);
}

/**
 * pk_backend_get_groups:
 */
PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY,
		PK_GROUP_ENUM_GAMES,
		PK_GROUP_ENUM_SYSTEM,
		-1);
}

/**
 * pk_backend_get_filters:
 */
PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		-1);
}

/**
 * pk_backend_get_mime_types:
 */
gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = {
				"application/x-rpm",
				"application/x-deb",
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

/**
 * pk_backend_cancel_timeout:
 */
static gboolean
pk_backend_cancel_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	/* we can now cancel again */
	priv->signal_timeout = 0;

	/* now mark as finished */
	pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED,
			       "The task was stopped successfully");
	pk_backend_finished (backend);
	return FALSE;
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
	/* cancel the timeout */
	if (priv->signal_timeout != 0) {
		g_source_remove (priv->signal_timeout);

		/* emulate that it takes us a few ms to cancel */
		g_timeout_add (1500, pk_backend_cancel_timeout, backend);
	}
}

/**
 * pk_backend_get_depends:
 */
void
pk_backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
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
 * pk_backend_get_details:
 */
void
pk_backend_get_details (PkBackend *backend, gchar **package_ids)
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
 * pk_backend_get_distro_upgrades:
 */
void
pk_backend_get_distro_upgrades (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	if (!priv->use_distro_upgrade)
		goto out;
	pk_backend_distro_upgrade (backend, PK_DISTRO_UPGRADE_ENUM_STABLE,
				   "fedora-9", "Fedora 9");
	pk_backend_distro_upgrade (backend, PK_DISTRO_UPGRADE_ENUM_UNSTABLE,
				   "fedora-10-rc1", "Fedora 10 RC1");
out:
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_files:
 */
void
pk_backend_get_files (PkBackend *backend, gchar **package_ids)
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
 * pk_backend_get_requires:
 */
void
pk_backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
}

/**
 * pk_backend_get_update_detail_timeout:
 **/
static gboolean
pk_backend_get_update_detail_timeout (gpointer data)
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
	len = g_strv_length (priv->package_ids);
	for (i=0; i<len; i++) {
		const gchar *to_array1[] = { NULL, NULL, NULL };
		const gchar *to_array2[] = { NULL, NULL, NULL };
		const gchar *to_array3[] = { NULL, NULL, NULL };
		const gchar *to_array4[] = { NULL, NULL, NULL };
		package_id = priv->package_ids[i];
		if (g_strcmp0 (package_id, "powertop;1.8-1.fc8;i386;fedora") == 0) {
			to_array1[0] = "powertop;1.7-1.fc8;i386;installed";
			to_array2[0] = "http://www.distro-update.org/page?moo";
			to_array3[0] = "http://bgzilla.fd.org/result.php?#12344";
			pk_backend_update_detail (backend, package_id,
						  (gchar**) to_array1,
						  NULL,
						  (gchar**) to_array2,
						  (gchar**) to_array3,
						  NULL,
						  PK_RESTART_ENUM_NONE,
						  "Update to newest upstream source",
						  changelog, PK_UPDATE_STATE_ENUM_STABLE,
						  "2009-11-17T09:19:00", "2009-11-19T09:19:00");
		} else if (g_strcmp0 (package_id, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed") == 0) {
			to_array1[0] = "kernel;2.6.22-0.104.rc3.git6.fc8;i386;installed";
			to_array1[1] = "kernel;2.6.22-0.105.rc3.git7.fc8;i386;installed";
			to_array2[0] = "http://www.distro-update.org/page?moo";
			to_array3[0] = "http://bgzilla.fd.org/result.php?#12344";
			to_array3[1] = "http://bgzilla.gnome.org/result.php?#9876";
			to_array4[0] = "http://nvd.nist.gov/nvd.cfm?cvename=CVE-2007-3381;CVE-2007-3381";
			pk_backend_update_detail (backend, package_id,
						  (gchar**) to_array1,
						  NULL,
						  (gchar**) to_array2,
						  (gchar**) to_array3,
						  (gchar**) to_array4,
						  PK_RESTART_ENUM_SYSTEM,
						  "Update to newest upstream version.\n"
						  "* This should fix many driver bugs when using nouveau\n"
						  " * This also introduces the new `frobnicator` driver for *vibrating* rabbit hardware.",
						  changelog,
						  PK_UPDATE_STATE_ENUM_UNSTABLE,
						  "2008-06-28T09:19:00",
						  NULL);
		} else if (g_strcmp0 (package_id, "gtkhtml2;2.19.1-4.fc8;i386;fedora") == 0) {
			to_array1[0] = "gtkhtml2;2.18.1-22.fc8;i386;installed";
			to_array2[0] = "http://www.distro-update.org/page?moo";
			to_array3[0] = "http://bgzilla.gnome.org/result.php?#9876";
			pk_backend_update_detail (backend, package_id,
						  (gchar**) to_array1,
						  NULL,
						  (gchar**) to_array2,
						  (gchar**) to_array3,
						  NULL,
						  PK_RESTART_ENUM_SESSION,
						  "Update to latest *whizz* **bang** version\n"
						  "* support this new thing\n"
						  "* something else\n"
						  "- and that new thing",
						  changelog,
						  PK_UPDATE_STATE_ENUM_UNKNOWN,
						  "2008-07-25T09:19:00",
						  NULL);

		} else if (g_strcmp0 (package_id, "vino;2.24.2.fc9;i386;fedora") == 0) {
			to_array1[0] = "vino;2.24.1.fc9;i386;fedora";
			pk_backend_update_detail (backend, package_id,
						  (gchar**) to_array1,
						  NULL,
						  NULL,
						  NULL,
						  NULL,
						  PK_RESTART_ENUM_NONE,
						  "Cannot get update as update conflics with vncviewer",
						  changelog,
						  PK_UPDATE_STATE_ENUM_UNKNOWN,
						  "2008-07-25",
						  NULL);
		} else {
			/* signal to UI */
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR, "the package update detail was not found for %s", package_id);
		}
	}
	pk_backend_set_percentage (backend, 100);
	pk_backend_finished (backend);
	priv->signal_timeout = 0;
	return FALSE;
}

/**
 * pk_backend_get_update_detail:
 */
void
pk_backend_get_update_detail (PkBackend *backend, gchar **package_ids)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	priv->package_ids = package_ids;
	priv->signal_timeout = g_timeout_add (500, pk_backend_get_update_detail_timeout, backend);
}

/**
 * pk_backend_get_updates_timeout:
 **/
static gboolean
pk_backend_get_updates_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	if (priv->use_blocked) {
		if (!priv->updated_powertop && !priv->updated_kernel && !priv->updated_gtkhtml) {
			pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
					    "vino;2.24.2.fc9;i386;fedora",
					    "Remote desktop server for the desktop");
		}
	}
	if (!priv->updated_powertop) {
		pk_backend_package (backend, PK_INFO_ENUM_NORMAL,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
	}
	if (!priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_BUGFIX,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	if (!priv->updated_gtkhtml) {
		pk_backend_package (backend, PK_INFO_ENUM_SECURITY,
				    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
				    "An HTML widget for GTK+ 2.0");
	}
	pk_backend_finished (backend);
	priv->signal_timeout = 0;
	return FALSE;
}

/**
 * pk_backend_get_updates:
 */
void
pk_backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot check when offline");
		pk_backend_finished (backend);
		return;
	}
	priv->signal_timeout = g_timeout_add (1000, pk_backend_get_updates_timeout, backend);
}

static gboolean
pk_backend_install_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	if (priv->progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (priv->progress_percentage == 30) {
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
				    "An HTML widget for GTK+ 2.0");
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	if (priv->progress_percentage == 50) {
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2-devel;2.19.1-0.fc8;i386;fedora",
				    "Devel files for gtkhtml");
		/* this duplicate package should be ignored */
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "gtkhtml2-devel;2.19.1-0.fc8;i386;fedora", NULL);
		pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	}
	priv->progress_percentage += 1;
	pk_backend_set_percentage (backend, priv->progress_percentage);
	return TRUE;
}

/**
 * pk_backend_install_packages:
 */
void
pk_backend_install_packages (PkBackend *backend, PkBitfield transaction_flags, gchar **package_ids)
{
	const gchar *license_agreement;
	const gchar *eula_id;
	gboolean has_eula;

	/* simulate */
	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
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
		return;
	}

	if (g_strcmp0 (package_ids[0], "vips-doc;7.12.4-2.fc8;noarch;linva") == 0) {
		if (priv->use_gpg && !priv->has_signature) {
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
		if (priv->use_eula && !has_eula) {
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
		if (priv->use_media) {
			priv->use_media = FALSE;
			pk_backend_media_change_required (backend, PK_MEDIA_TYPE_ENUM_DVD, "linux-disk-1of7", "Linux Disc 1 of 7");
			pk_backend_error_code (backend, PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED,
					       "additional media linux-disk-1of7 required");
			pk_backend_finished (backend);
			return;
		}
	}

	if (priv->use_trusted && pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
				       "Can't install as untrusted");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_set_allow_cancel (backend, TRUE);
	priv->progress_percentage = 0;
	pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
			    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
			    "An HTML widget for GTK+ 2.0");
	priv->signal_timeout = g_timeout_add (100, pk_backend_install_timeout, backend);
}

/**
 * pk_backend_install_signature:
 */
void
pk_backend_install_signature (PkBackend *backend, PkSigTypeEnum type,
			   const gchar *key_id, const gchar *package_id)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	if (type == PK_SIGTYPE_ENUM_GPG &&
	    /* egg_strequal (package_id, "vips-doc;7.12.4-2.fc8;noarch;linva") && */
	    g_strcmp0 (key_id, "BB7576AC") == 0) {
		g_debug ("installed signature %s for %s", key_id, package_id);
		priv->has_signature = TRUE;
	} else {
		pk_backend_error_code (backend, PK_ERROR_ENUM_GPG_FAILURE,
				       "GPG key %s not recognised for package_id %s",
				       key_id, package_id);
	}
	pk_backend_finished (backend);
}

/**
 * pk_backend_refresh_cache_timeout:
 */
static gboolean
pk_backend_install_files_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	pk_backend_finished (backend);
	return FALSE;
}

/**
 * pk_backend_install_files:
 */
void
pk_backend_install_files (PkBackend *backend, PkBitfield transaction_flags, gchar **full_paths)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_INSTALL);
	pk_backend_set_percentage (backend, 101);
	priv->signal_timeout = g_timeout_add (2000, pk_backend_install_files_timeout, backend);
}

/**
 * pk_backend_refresh_cache_timeout:
 */
static gboolean
pk_backend_refresh_cache_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (priv->progress_percentage == 100) {
		pk_backend_finished (backend);
		return FALSE;
	}
	if (priv->progress_percentage == 80)
		pk_backend_set_allow_cancel (backend, FALSE);
	priv->progress_percentage += 10;
	pk_backend_set_percentage (backend, priv->progress_percentage);
	return TRUE;
}

/**
 * pk_backend_refresh_cache:
 */
void
pk_backend_refresh_cache (PkBackend *backend, gboolean force)
{
	priv->progress_percentage = 0;

	/* reset */
	priv->updated_gtkhtml = FALSE;
	priv->updated_kernel = FALSE;
	priv->updated_powertop = FALSE;

	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);
	priv->signal_timeout = g_timeout_add (500, pk_backend_refresh_cache_timeout, backend);
}

/**
 * pk_backend_resolve_timeout:
 */
static gboolean
pk_backend_resolve_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	guint i;
	guint len;
	gchar **packages = priv->package_ids;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_percentage (backend, 0);

	/* each one has a different detail for testing */
	len = g_strv_length (packages);
	for (i=0; i<len; i++) {
		if (g_strcmp0 (packages[i], "vips-doc") == 0 || g_strcmp0 (packages[i], "vips-doc;7.12.4-2.fc8;noarch;linva") == 0) {
			if (!pk_bitfield_contain (priv->filters, PK_FILTER_ENUM_INSTALLED)) {
				pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
						    "vips-doc;7.12.4-2.fc8;noarch;linva", "The vips documentation package.");
			}
		} else if (g_strcmp0 (packages[i], "glib2") == 0 || g_strcmp0 (packages[i], "glib2;2.14.0;i386;fedora") == 0) {
			if (!pk_bitfield_contain (priv->filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
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
 * pk_backend_resolve:
 */
void
pk_backend_resolve (PkBackend *backend, PkBitfield filters, gchar **packages)
{
	priv->filters = filters;
	priv->package_ids = packages;
	priv->signal_timeout = g_timeout_add (20, pk_backend_resolve_timeout, backend);
}

/**
 * pk_backend_remove_packages:
 */
void
pk_backend_remove_packages (PkBackend *backend,
			    PkBitfield transaction_flags,
			    gchar **package_ids,
			    gboolean allow_deps,
			    gboolean autoremove)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);
	pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "No network connection available");
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_details:
 */
void
pk_backend_search_details (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
			    "vips-doc;7.12.4-2.fc8;noarch;linva",
			    "The vips \"documentation\" package.");
	pk_backend_finished (backend);
}

/**
 * pk_backend_search_files:
 */
void
pk_backend_search_files (PkBackend *backend, PkBitfield filters, gchar **values)
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
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
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
 * pk_backend_search_name_timeout:
 **/
static gboolean
pk_backend_search_name_timeout (gpointer data)
{
	gchar *locale;
	PkBackend *backend = (PkBackend *) data;
	locale = pk_backend_get_locale (backend);

	g_debug ("locale is %s", locale);
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
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	priv->signal_timeout = g_timeout_add (2000, pk_backend_search_name_timeout, backend);
}

/**
 * pk_backend_update_packages_download_timeout:
 **/
static gboolean
pk_backend_update_packages_download_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;

	if (priv->progress_percentage == 100) {
		if (priv->use_blocked) {
			pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			priv->updated_gtkhtml = FALSE;
		}
		pk_backend_finished (backend);
		return FALSE;
	}
	if (priv->progress_percentage == 0 && !priv->updated_powertop) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
		pk_backend_set_item_progress (backend,
					      "powertop;1.8-1.fc8;i386;fedora",
					      0);
	}
	if (priv->progress_percentage == 20 && !priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		pk_backend_set_item_progress (backend,
					      "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
					      0);
		pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed");
	}
	if (priv->progress_percentage == 30 && !priv->updated_gtkhtml) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS, "A newer package preupgrade is available in fedora-updates-testing");
		pk_backend_message (backend, PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED, "/etc/X11/xorg.conf has been auto-merged, please check before rebooting");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-debuginfo metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-source metadata is invalid");
		if (!priv->use_blocked) {
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			priv->updated_gtkhtml = TRUE;
		}
		pk_backend_set_item_progress (backend,
					      "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					      0);
	}
	if (priv->progress_percentage == 40 && !priv->updated_powertop) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
		pk_backend_set_item_progress (backend,
					      "powertop;1.8-1.fc8;i386;fedora",
					      0);
		priv->updated_powertop = TRUE;
	}
	if (priv->progress_percentage == 60 && !priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		pk_backend_set_item_progress (backend,
					      "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
					      0);
		priv->updated_kernel = TRUE;
		pk_backend_set_item_progress (backend,
					      "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
					      0);
	}
	if (priv->progress_percentage == 80 && !priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_CLEANUP,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		pk_backend_set_item_progress (backend,
					      "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
					      0);
	}
	priv->progress_percentage += 1;
	pk_backend_set_percentage (backend, priv->progress_percentage);
	return TRUE;
}

/**
 * pk_backend_update_packages:
 */
void
pk_backend_update_packages (PkBackend *backend, PkBitfield transaction_flags, gchar **package_ids)
{
	const gchar *eula_id;
	const gchar *license_agreement;
	gboolean has_eula;

	/* FIXME: support only_trusted */
        PkRoleEnum role = pk_backend_get_role (backend);
        if (role == PK_ROLE_ENUM_UPDATE_PACKAGES && priv->use_gpg && !priv->has_signature) {
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
        if (role == PK_ROLE_ENUM_UPDATE_PACKAGES && priv->use_eula && !has_eula) {
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

	priv->package_ids = package_ids;
	priv->progress_percentage = 0;
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_percentage (backend, 0);
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	priv->signal_timeout = g_timeout_add (200, pk_backend_update_packages_download_timeout, backend);
}

static gboolean
pk_backend_update_system_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (priv->progress_percentage == 100) {

		/* cleanup socket stuff */
		if (priv->socket != NULL)
			g_object_unref (priv->socket);
		if (priv->socket_listen_id != 0)
			g_source_remove (priv->socket_listen_id);

		pk_backend_finished (backend);
		return FALSE;
	}
	if (priv->progress_percentage == 0 && !priv->updated_powertop) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
	}
	if (priv->progress_percentage == 20 && !priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	if (priv->progress_percentage == 30 && !priv->updated_gtkhtml) {
		pk_backend_message (backend, PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS, "A newer package preupgrade is available in fedora-updates-testing");
		pk_backend_message (backend, PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED, "/etc/X11/xorg.conf has been auto-merged, please check before rebooting");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-debuginfo metadata is invalid");
		pk_backend_message (backend, PK_MESSAGE_ENUM_BROKEN_MIRROR, "fedora-updates-testing-source metadata is invalid");
		if (priv->use_blocked) {
			pk_backend_package (backend, PK_INFO_ENUM_BLOCKED,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			priv->updated_gtkhtml = FALSE;
		} else {
			pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
					    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
					    "An HTML widget for GTK+ 2.0");
			priv->updated_gtkhtml = TRUE;
		}
	}
	if (priv->progress_percentage == 40 && !priv->updated_powertop) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_UPDATE);
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_INSTALLING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
		priv->updated_powertop = TRUE;
	}
	if (priv->progress_percentage == 60 && !priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_UPDATING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
		priv->updated_kernel = TRUE;
	}
	if (priv->progress_percentage == 80 && !priv->updated_kernel) {
		pk_backend_package (backend, PK_INFO_ENUM_CLEANUP,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	priv->progress_percentage += 1;
	pk_backend_set_percentage (backend, priv->progress_percentage);
	return TRUE;
}


/**
 * pk_backend_socket_has_data_cb:
 **/
static gboolean
pk_backend_socket_has_data_cb (GSocket *socket, GIOCondition condition, PkBackend *backend)
{
	GError *error = NULL;
	gsize len;
	gchar buffer[1024];
	gboolean ret = TRUE;
	gint wrote = 0;

	/* the helper process exited */
	if ((condition & G_IO_HUP) > 0) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "socket was disconnected");
		pk_backend_finished (backend);
		ret = FALSE;
		goto out;
	}

	/* there is data */
	if ((condition & G_IO_IN) > 0) {
		len = g_socket_receive (socket, buffer, 1024, NULL, &error);
		if (error != NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "failed to read: %s", error->message);
			pk_backend_finished (backend);
			g_error_free (error);
			ret = FALSE;
			goto out;
		}
		if (len == 0)
			goto out;
		buffer[len] = '\0';
		if (g_strcmp0 (buffer, "pong\n") == 0) {
			/* send a message so we can verify in the self checks */
			pk_backend_message (backend, PK_MESSAGE_ENUM_PARAMETER_INVALID, buffer);

			/* verify we can write into the socket */
			wrote = g_socket_send (priv->socket, "invalid\n", 8, NULL, &error);
			if (error != NULL) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
						       "failed to write to socket: %s", error->message);
				pk_backend_finished (backend);
				g_error_free (error);
				goto out;
			}
			if (wrote != 8) {
				pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
						       "failed to write, only %i bytes", wrote);
				pk_backend_finished (backend);
				goto out;
			}
		} else if (g_strcmp0 (buffer, "you said to me: invalid\n") == 0) {
			g_debug ("ignoring invalid data (one is good)");
		} else {
			pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
					       "unexpected data: %s", buffer);
			g_source_remove (priv->signal_timeout);
			pk_backend_finished (backend);
			goto out;
		}
	}
out:
	return ret;
}

/**
 * pk_backend_update_system:
 */
void
pk_backend_update_system (PkBackend *backend, PkBitfield transaction_flags)
{
	gchar *frontend_socket = NULL;
	GError *error = NULL;
	gboolean ret;
	GSocketAddress *address = NULL;
	gsize wrote;
	GSource *source;

	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_allow_cancel (backend, TRUE);
	priv->progress_percentage = 0;

	priv->socket = NULL;
	priv->socket_listen_id = 0;

	/* make sure we can contact the frontend */
	frontend_socket = pk_backend_get_frontend_socket (backend);
	if (frontend_socket == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "failed to get frontend socket");
		pk_backend_finished (backend);
		goto out;
	}

	/* create socket */
	priv->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
	if (priv->socket == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "failed to create socket: %s", error->message);
		pk_backend_finished (backend);
		g_error_free (error);
		goto out;
	}
	g_socket_set_blocking (priv->socket, FALSE);
	g_socket_set_keepalive (priv->socket, TRUE);

	/* connect to it */
	address = g_unix_socket_address_new (frontend_socket);
	ret = g_socket_connect (priv->socket, address, NULL, &error);
	if (!ret) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "failed to open socket: %s", error->message);
		pk_backend_finished (backend);
		g_error_free (error);
		goto out;
	}

	/* socket has data */
	source = g_socket_create_source (priv->socket, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, NULL);
	g_source_set_callback (source, (GSourceFunc) pk_backend_socket_has_data_cb, backend, NULL);
	priv->socket_listen_id = g_source_attach (source, NULL);

	/* send some data */
	wrote = g_socket_send (priv->socket, "ping\n", 5, NULL, &error);
	if (wrote != 5) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_INTERNAL_ERROR,
				       "failed to write, only %i bytes", wrote);
		pk_backend_finished (backend);
		goto out;
	}

	/* FIXME: support only_trusted */
	pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed");
	priv->signal_timeout = g_timeout_add (100, pk_backend_update_system_timeout, backend);
out:
	if (address != NULL)
		g_object_unref (address);
	g_free (frontend_socket);
}

/**
 * pk_backend_get_repo_list:
 */
void
pk_backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	pk_backend_repo_detail (backend, "fedora",
				"Fedora - 9", priv->repo_enabled_fedora);
	if (!pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		pk_backend_repo_detail (backend, "development",
					"Fedora - Development", priv->repo_enabled_devel);
	}
	pk_backend_repo_detail (backend, "livna-development",
				"Livna for Fedora Core 8 - i386 - Development Tree", priv->repo_enabled_livna);
	pk_backend_finished (backend);
}

/**
 * pk_backend_repo_enable:
 */
void
pk_backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);

	if (g_strcmp0 (rid, "local") == 0) {
		g_debug ("local repo: %i", enabled);
		priv->repo_enabled_local = enabled;
	} else if (g_strcmp0 (rid, "development") == 0) {
		g_debug ("devel repo: %i", enabled);
		priv->repo_enabled_devel = enabled;
	} else if (g_strcmp0 (rid, "fedora") == 0) {
		g_debug ("fedora repo: %i", enabled);
		priv->repo_enabled_fedora = enabled;
	} else if (g_strcmp0 (rid, "livna-development") == 0) {
		g_debug ("livna repo: %i", enabled);
		priv->repo_enabled_livna = enabled;
	} else {
		g_warning ("unknown repo: %s", rid);
	}
	pk_backend_finished (backend);
}

/**
 * pk_backend_repo_set_data:
 */
void
pk_backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	g_warning ("REPO '%s' PARAMETER '%s' TO '%s'", rid, parameter, value);

	if (g_strcmp0 (parameter, "use-blocked") == 0)
		priv->use_blocked = atoi (value);
	else if (g_strcmp0 (parameter, "use-eula") == 0)
		priv->use_eula = atoi (value);
	else if (g_strcmp0 (parameter, "use-media") == 0)
		priv->use_media = atoi (value);
	else if (g_strcmp0 (parameter, "use-gpg") == 0)
		priv->use_gpg = atoi (value);
	else if (g_strcmp0 (parameter, "use-trusted") == 0)
		priv->use_trusted = atoi (value);
	else if (g_strcmp0 (parameter, "use-distro-upgrade") == 0)
		priv->use_distro_upgrade = atoi (value);
	else
		pk_backend_message (backend, PK_MESSAGE_ENUM_PARAMETER_INVALID, "invalid parameter %s", parameter);
	pk_backend_finished (backend);
}

/**
 * pk_backend_what_provides_timeout:
 */
static gboolean
pk_backend_what_provides_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (priv->progress_percentage == 100) {
		if (g_strcmp0 (priv->values[0], "gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)") == 0) {
			pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
					    "gstreamer-plugins-bad;0.10.3-5.lvn;i386;available",
					    "GStreamer streaming media framework \"bad\" plug-ins");
		} else if (g_strcmp0 (priv->values[0], "gstreamer0.10(decoder-video/x-wma)(wmaversion=3)") == 0) {
			pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
					    "gstreamer-plugins-flumpegdemux;0.10.15-5.lvn;i386;available",
					    "MPEG demuxer for GStreamer");
		} else {
			/* pkcon install vips-doc says it's installed cause evince is INSTALLED */
			if (g_strcmp0 (priv->values[0], "vips-doc") != 0) {
				if (!pk_bitfield_contain (priv->filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
					pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
							    "evince;0.9.3-5.fc8;i386;installed",
							    "PDF Document viewer");
				}
				if (!pk_bitfield_contain (priv->filters, PK_FILTER_ENUM_INSTALLED)) {
					pk_backend_package (backend, PK_INFO_ENUM_AVAILABLE,
							    "scribus;1.3.4-1.fc8;i386;fedora",
							    "Scribus is an desktop open source page layout program");
				}
			}
		}
		pk_backend_finished (backend);
		return FALSE;
	}
	priv->progress_percentage += 10;
	pk_backend_set_percentage (backend, priv->progress_percentage);
	return TRUE;
}

/**
 * pk_backend_what_provides:
 */
void
pk_backend_what_provides (PkBackend *backend, PkBitfield filters, PkProvidesEnum provides, gchar **values)
{
	priv->progress_percentage = 0;
	priv->values = values;
	priv->signal_timeout = g_timeout_add (200, pk_backend_what_provides_timeout, backend);
	priv->filters = filters;
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	pk_backend_set_allow_cancel (backend, TRUE);
	pk_backend_set_percentage (backend, priv->progress_percentage);
}

/**
 * pk_backend_get_packages:
 */
void
pk_backend_get_packages (PkBackend *backend, PkBitfield filters)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REQUEST);
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "update1;2.19.1-4.fc8;i386;fedora",
			    "The first update");
	pk_backend_finished (backend);
}

/**
 * pk_backend_download_packages:
 */
void
pk_backend_download_packages (PkBackend *backend, gchar **package_ids, const gchar *directory)
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

static gboolean
pk_backend_upgrade_system_timeout (gpointer data)
{
	PkBackend *backend = (PkBackend *) data;
	if (priv->progress_percentage == 100) {
		pk_backend_require_restart (backend, PK_RESTART_ENUM_SYSTEM, "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed");
		pk_backend_finished (backend);
		return FALSE;
	}
	if (priv->progress_percentage == 0) {
		pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO);
	}
	if (priv->progress_percentage == 20) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	if (priv->progress_percentage == 30) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "gtkhtml2;2.19.1-4.fc8;i386;fedora",
				    "An HTML widget for GTK+ 2.0");
	}
	if (priv->progress_percentage == 40) {
		pk_backend_set_allow_cancel (backend, FALSE);
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
	}
	if (priv->progress_percentage == 60) {
		pk_backend_set_allow_cancel (backend, TRUE);
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "kernel;2.6.23-0.115.rc3.git1.fc8;i386;installed",
				    "The Linux kernel (the core of the Linux operating system)");
	}
	if (priv->progress_percentage == 80) {
		pk_backend_package (backend, PK_INFO_ENUM_DOWNLOADING,
				    "powertop;1.8-1.fc8;i386;fedora",
				    "Power consumption monitor");
	}
	priv->progress_percentage += 1;
	pk_backend_set_percentage (backend, priv->progress_percentage);
	return TRUE;
}

/**
 * pk_backend_upgrade_system:
 */
void
pk_backend_upgrade_system (PkBackend *backend, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_DOWNLOAD);
	pk_backend_set_allow_cancel (backend, TRUE);
	priv->progress_percentage = 0;
	priv->signal_timeout = g_timeout_add (100, pk_backend_upgrade_system_timeout, backend);
}

/**
 * pk_backend_repair_system:
 */
void
pk_backend_repair_system (PkBackend *backend, PkBitfield transaction_flags)
{
	pk_backend_finished (backend);
}

/**
 * pk_backend_job_start:
 */
void
pk_backend_job_start (PkBackend *backend)
{
	/* here you would lock the backend */
	pk_backend_message (backend, PK_MESSAGE_ENUM_AUTOREMOVE_IGNORED, "backend is crap");

	/* you can use pk_backend_error_code() here too */
}

/**
 * pk_backend_job_stop:
 */
void
pk_backend_job_stop (PkBackend *backend)
{
	/* here you would unlock the backend */
	pk_backend_message (backend, PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED, "backend is crap");

	/* you *cannot* use pk_backend_error_code() here,
	 * unless pk_backend_get_is_error_set() returns FALSE, and
	 * even then it's probably just best to clean up silently */

	/* you cannot do pk_backend_finished() here as well as this is
	 * needed to fire the job_stop() vfunc */
}

/**
 * pk_backend_get_description:
 */
const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "Dummy";
}

/**
 * pk_backend_get_author:
 */
const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Richard Hughes <richard@hughsie.com>";
}
