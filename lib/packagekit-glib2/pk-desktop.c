/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-desktop
 * @short_description: Find desktop metadata about a package
 *
 * Desktop metadata such as icon name and localised summary may be stored in
 * a local sqlite cache, and this module allows applications to query this.
 */

#include "config.h"

#include <glib.h>
#include <packagekit-glib2/pk-desktop.h>

G_DEFINE_TYPE (PkDesktop, pk_desktop, G_TYPE_OBJECT)
static gpointer pk_desktop_object = NULL;

/**
 * pk_desktop_get_files_for_package:
 * @desktop: a valid #PkDesktop instance
 * @package: the package name, e.g. "gnome-power-manager"
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Return all desktop files owned by a package, regardless if they are shown
 * in the main menu or not.
 *
 * Return value: (transfer container) (element-type utf8): string array of results, free with g_ptr_array_unref()
 *
 * NOTE: This method is unused and will be removed next time the library
 * soname changes!
 *
 * Since: 0.5.3
 **/
GPtrArray *
pk_desktop_get_files_for_package (PkDesktop *desktop, const gchar *package, GError **error)
{
	g_set_error_literal (error, 1, 0, "no longer supported");
	return NULL;
}

/**
 * pk_desktop_get_shown_for_package:
 * @desktop: a valid #PkDesktop instance
 * @package: the package name, e.g. "gnome-power-manager"
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Return all desktop files owned by a package that would be shown in a menu,
 * i.e are an application
 *
 * Return value: (transfer container) (element-type utf8): string array of results, free with g_ptr_array_unref()
 *
 * NOTE: This method is unused and will be removed next time the library
 * soname changes!
 *
 * Since: 0.5.3
 **/
GPtrArray *
pk_desktop_get_shown_for_package (PkDesktop *desktop, const gchar *package, GError **error)
{
	g_set_error_literal (error, 1, 0, "no longer supported");
	return NULL;
}

/**
 * pk_desktop_get_package_for_file:
 * @desktop: a valid #PkDesktop instance
 * @filename: a fully qualified filename
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * Returns the package name that owns the desktop file. Fast.
 *
 * Return value: package name, or %NULL
 *
 * NOTE: This method is unused and will be removed next time the library
 * soname changes!
 *
 * Since: 0.5.3
 **/
gchar *
pk_desktop_get_package_for_file (PkDesktop *desktop, const gchar *filename, GError **error)
{
	g_set_error_literal (error, 1, 0, "no longer supported");
	return NULL;
}

/**
 * pk_desktop_open_database:
 * @desktop: a valid #PkDesktop instance
 * @error: a #GError to put the error code and message in, or %NULL
 *
 * This method is unused and will be removed next time the library
 * soname changes!
 *
 * Return value: %TRUE if opened correctly
 *
 * Since: 0.5.3
 **/
gboolean
pk_desktop_open_database (PkDesktop *desktop, GError **error)
{
	return TRUE;
}

/*
 * pk_desktop_class_init:
 **/
static void
pk_desktop_class_init (PkDesktopClass *klass)
{
}

/*
 * pk_desktop_init:
 **/
static void
pk_desktop_init (PkDesktop *desktop)
{
	desktop->priv = NULL;
}

/**
 * pk_desktop_new:
 *
 * NOTE: This method is unused and will be removed next time the library
 * soname changes!
 *
 * Since: 0.5.3
 **/
PkDesktop *
pk_desktop_new (void)
{
	if (pk_desktop_object != NULL) {
		g_object_ref (pk_desktop_object);
	} else {
		pk_desktop_object = g_object_new (PK_TYPE_DESKTOP, NULL);
		g_object_add_weak_pointer (pk_desktop_object, &pk_desktop_object);
	}
	return PK_DESKTOP (pk_desktop_object);
}
