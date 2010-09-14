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
#include <pk-backend.h>

static gboolean is_cancelled = FALSE;

/**
 * backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_initialize (PkBackend *backend)
{
	egg_debug ("backend: initialize");
}

/**
 * backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
static void
backend_destroy (PkBackend *backend)
{
	egg_debug ("backend: destroy");
}

/**
 * backend_search_group_thread:
 */
static gboolean
backend_search_group_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* emit */
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, backend_search_group_thread);
}

/**
 * backend_search_name_thread:
 */
static gboolean
backend_search_name_thread (PkBackend *backend)
{
	GTimer *timer;
	guint percentage;
	PkBitfield filters;
	gchar *filters_text;
	const gchar *search;

	filters = pk_backend_get_uint (backend, "filters");
	search = pk_backend_get_string (backend, "search");

	filters_text = pk_filter_bitfield_to_string (filters);
	egg_debug ("started task (%p) search=%s filters=%s", backend, search, filters_text);
	g_free (filters_text);
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	timer = g_timer_new ();
	percentage = 0;
	do {
		/* now is a good time to see if we should cancel the thread */
		if (is_cancelled) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "The thread was stopped successfully");
			pk_backend_finished (backend);
			return TRUE;
		}
		pk_backend_set_percentage (backend, percentage);
		percentage += 10;
		g_usleep (1000*100);
	} while (percentage < 100);
	g_timer_destroy (timer);
	pk_backend_set_percentage (backend, 100);
	egg_debug ("exited task (%p)", backend);

	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, backend_search_name_thread);
}

/**
 * backend_cancel:
 */
static void
backend_cancel (PkBackend *backend)
{
	egg_debug ("cancelling %p", backend);
	is_cancelled = TRUE;
}

PK_BACKEND_OPTIONS (
	"Test Thread",				/* description */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	NULL,					/* get_filters */
	NULL,					/* get_roles */
	NULL,					/* get_mime_types */
	backend_cancel,				/* cancel */
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
	backend_search_group,			/* search_groups */
	backend_search_name,			/* search_names */
	NULL,					/* update_package */
	NULL,					/* update_system */
	NULL,					/* what_provides */
	NULL,					/* simulate_install_files */
	NULL,					/* simulate_install_packages */
	NULL,					/* simulate_remove_packages */
	NULL,					/* simulate_update_packages */
	NULL,					/* transaction_start */
	NULL					/* transaction_stop */
);

