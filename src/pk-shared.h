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

#ifndef __PK_SHARED_H
#define __PK_SHARED_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* a boolean with unset */
typedef enum {
	PK_HINT_ENUM_FALSE = FALSE,
	PK_HINT_ENUM_TRUE = TRUE,
	PK_HINT_ENUM_UNSET,
	PK_HINT_ENUM_INVALID,
	PK_HINT_ENUM_LAST
} PkHintEnum;

gboolean	 pk_directory_remove_contents		(const gchar	*directory);
const gchar	*pk_hint_enum_to_string			(PkHintEnum	 hint);
PkHintEnum	 pk_hint_enum_from_string		(const gchar	*hint);

guint		 pk_strlen				(const gchar	*text,
							 guint		 len)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strzero				(const gchar	*text)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_strtoint				(const gchar	*text,
							 gint		*value);
gboolean	 pk_strtouint				(const gchar	*text,
							 guint		*value);
gboolean	 pk_strtouint64				(const gchar	*text,
							 guint64	*value);
GDBusNodeInfo	*pk_load_introspection			(const gchar	*filename,
							 GError		**error);

G_END_DECLS

#endif /* __PK_SHARED_H */
