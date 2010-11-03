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
 * pk_backend_get_description:
 */
gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Test-Thread");
}

/**
 * pk_backend_initialize:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_initialize (PkBackend *backend)
{
	g_debug ("backend: initialize");
}

/**
 * pk_backend_destroy:
 * This should only be run once per backend load, i.e. not every transaction
 */
void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
}

/**
 * pk_backend_search_groups_thread:
 */
static gboolean
pk_backend_search_groups_thread (PkBackend *backend)
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
 * pk_backend_search_groups:
 */
void
pk_backend_search_groups (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, pk_backend_search_groups_thread);
}

/**
 * pk_backend_search_names_thread:
 */
static gboolean
pk_backend_search_names_thread (PkBackend *backend)
{
	GTimer *timer;
	guint percentage;
	PkBitfield filters;
	gchar *filters_text;
	const gchar *search;

	filters = pk_backend_get_uint (backend, "filters");
	search = pk_backend_get_string (backend, "search");

	filters_text = pk_filter_bitfield_to_string (filters);
	g_debug ("started task (%p) search=%s filters=%s", backend, search, filters_text);
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
	g_debug ("exited task (%p)", backend);

	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	pk_backend_finished (backend);
	return TRUE;
}

/**
 * pk_backend_search_names:
 */
void
pk_backend_search_names (PkBackend *backend, PkBitfield filters, gchar **values)
{
	pk_backend_thread_create (backend, pk_backend_search_names_thread);
}

/**
 * pk_backend_cancel:
 */
void
pk_backend_cancel (PkBackend *backend)
{
	g_debug ("cancelling %p", backend);
	is_cancelled = TRUE;
}
