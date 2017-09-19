/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_COMMON_H
#define __PK_COMMON_H

#include <glib.h>

#include "pk-enum.h"

G_BEGIN_DECLS

/**
 * PK_DBUS_SERVICE:
 *
 * The SYSTEM service DBUS name
 */
#define	PK_DBUS_SERVICE			"org.freedesktop.PackageKit"

/**
 * PK_DBUS_PATH:
 *
 * The DBUS path
 */
#define	PK_DBUS_PATH			"/org/freedesktop/PackageKit"

/**
 * PK_DBUS_INTERFACE:
 *
 * The DBUS interface
 */
#define	PK_DBUS_INTERFACE		"org.freedesktop.PackageKit"

/**
 * PK_DBUS_INTERFACE_TRANSACTION:
 *
 * The DBUS interface for the transactions
 */
#define	PK_DBUS_INTERFACE_TRANSACTION	"org.freedesktop.PackageKit.Transaction"

/**
 * PK_DBUS_INTERFACE_OFFLINE:
 *
 * The DBUS interface for the offline update functionality
 */
#define	PK_DBUS_INTERFACE_OFFLINE	"org.freedesktop.PackageKit.Offline"

/**
 * PK_PACKAGE_LIST_FILENAME:
 *
 * The default location of the package list
 *
 * NOTE: This constant is unused and will be removed next time the library
 * soname changes!
 */
#define	PK_SYSTEM_PACKAGE_LIST_FILENAME	"/var/lib/PackageKit/system.package-list"

/**
 * PK_PACKAGE_CACHE_FILENAME:
 *
 * The default location of the package cache database
 *
 * NOTE: This constant is unused and will be removed next time the library
 * soname changes!
 */
#define	PK_SYSTEM_PACKAGE_CACHE_FILENAME	"/var/lib/PackageKit/package-cache.db"

gchar		**pk_ptr_array_to_strv			(GPtrArray	*array)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_iso8601_present			(void)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_iso8601_from_date			(const GDate	*date);
GDate		*pk_iso8601_to_date			(const gchar	*iso_date);
GDateTime	*pk_iso8601_to_datetime			(const gchar	*iso_date);
gchar		*pk_get_distro_id			(void);

G_END_DECLS

#endif /* __PK_COMMON_H */
