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

/**
 * backend_search_group_thread:
 */
static gboolean
backend_search_group_thread (PkBackend *backend, gpointer data)
{
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	return TRUE;
}

/**
 * backend_search_group:
 */
static void
backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_thread_helper (backend, backend_search_group_thread, NULL);
}

/**
 * backend_search_name_thread:
 */
static gboolean
backend_search_name_thread (PkBackend *backend, gpointer data)
{
	GTimer *timer;
	gdouble elapsed;

	pk_debug ("started task (%p,%p)", backend, data);
	timer = g_timer_new ();
	do {
		g_usleep (1000*100);
		elapsed = g_timer_elapsed (timer, NULL);
		pk_debug ("elapsed task (%p,%p) = %f", backend, data, elapsed);
	} while (elapsed < 5.0);
	g_timer_destroy (timer);
	pk_debug ("exited task (%p,%p)", backend, data);

	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "glib2;2.14.0;i386;fedora", "The GLib library");
	pk_backend_package (backend, PK_INFO_ENUM_INSTALLED,
			    "gtk2;gtk2-2.11.6-6.fc8;i386;fedora", "GTK+ Libraries for GIMP");
	return TRUE;
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search)
{
	g_return_if_fail (backend != NULL);
	pk_backend_thread_helper (backend, backend_search_name_thread, NULL);
}

PK_BACKEND_OPTIONS (
	"Test Thread",				/* description */
	"0.0.1",				/* version */
	"Richard Hughes <richard@hughsie.com>",	/* author */
	NULL,					/* initalize */
	NULL,					/* destroy */
	NULL,					/* get_groups */
	NULL,					/* get_filters */
	NULL,					/* cancel */
	NULL,					/* get_depends */
	NULL,					/* get_description */
	NULL,					/* get_requires */
	NULL,					/* get_update_detail */
	NULL,					/* get_updates */
	NULL,					/* install_package */
	NULL,					/* install_file */
	NULL,					/* refresh_cache */
	NULL,					/* remove_package */
	NULL,					/* resolve */
	NULL,					/* rollback */
	NULL,					/* search_details */
	NULL,					/* search_file */
	backend_search_group,			/* search_group */
	backend_search_name,			/* search_name */
	NULL,					/* update_package */
	NULL					/* update_system */
);

