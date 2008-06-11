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

#ifndef __PK_COMMON_H
#define __PK_COMMON_H

#include <glib-object.h>

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

guint		 pk_strlen				(gchar		*text,
							 guint		 max_length)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strzero				(const gchar	*text)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strvalidate				(const gchar	*text)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strequal				(const gchar	*id1,
							 const gchar	*id2)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strnumber				(const gchar	*text)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strtoint				(const gchar	*text,
							 gint		*value);
gboolean	 pk_strtouint				(const gchar	*text,
							 guint		*value);
gchar		*pk_strpad				(const gchar	*data,
							 guint		 length)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_strpad_extra			(const gchar	*data,
							 guint		 length,
							 guint		*extra)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_strsafe				(const gchar	*text)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		**pk_strsplit				(const gchar	*id,
							 guint		 parts);
gchar		*pk_strbuild_va				(const gchar	*first_element,
							 va_list	*args)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_strreplace				(const gchar	*text,
							 const gchar	*find,
							 const gchar	*replace);
gint		 pk_ptr_array_find_string		(GPtrArray	*array,
							 const gchar	*string);
gboolean	 pk_ptr_array_remove_string		(GPtrArray	*array,
							 const gchar	*string);
gchar		**pk_ptr_array_to_argv			(GPtrArray	*array)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		**pk_va_list_to_argv			(const gchar	*string_first,
							 va_list	*args)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strcmp_sections			(const gchar	*id1,
							 const gchar	*id2,
							 guint		 parts,
							 guint		 compare)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_iso8601_present			(void)
							 G_GNUC_WARN_UNUSED_RESULT;
guint		 pk_iso8601_difference			(const gchar	*isodate);
gboolean	 pk_delay_yield				(gfloat		 delay);
gchar		*pk_get_distro_id			(void)
							 G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* __PK_COMMON_H */
