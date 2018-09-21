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

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return g_strdup ("Test-Thread");
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	g_debug ("backend: initialize");
}

void
pk_backend_destroy (PkBackend *backend)
{
	g_debug ("backend: destroy");
}

static void
pk_backend_search_groups_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	/* emit */
	pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
}

void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job, pk_backend_search_groups_thread, NULL, NULL);
}

static void
pk_backend_search_names_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	GTimer *timer;
	guint percentage;
	PkBitfield filters;
	gchar *filters_text;
	g_autofree gchar **search = NULL;

	g_variant_get (params, "(t^a&s)",
		       &filters,
		       &search);

	filters_text = pk_filter_bitfield_to_string (filters);
	g_debug ("started task (%p) search=%s filters=%s", job, search[0], filters_text);
	g_free (filters_text);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	timer = g_timer_new ();
	percentage = 0;
	do {
		/* now is a good time to see if we should cancel the thread */
		if (is_cancelled) {
			pk_backend_job_error_code (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED,
					       "The thread was stopped successfully");
			return;
		}
		pk_backend_job_set_percentage (job, percentage);
		percentage += 10;
		g_usleep (1000*100);
	} while (percentage < 100);
	g_timer_destroy (timer);
	pk_backend_job_set_percentage (job, 100);
	g_debug ("exited task (%p)", job);

	pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_job_package (job, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	pk_backend_job_thread_create (job,
				      pk_backend_search_names_thread,
				      NULL, NULL);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	g_debug ("cancelling %p", backend);
	is_cancelled = TRUE;
}
