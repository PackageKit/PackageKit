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

#ifndef __EGG_STRING_H
#define __EGG_STRING_H

#include <glib.h>

G_BEGIN_DECLS

guint		 egg_strlen				(const gchar	*text,
							 guint		 len)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 egg_strzero				(const gchar	*text)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 egg_strvequal				(gchar		**id1,
							 gchar		**id2)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 egg_strtoint				(const gchar	*text,
							 gint		*value);
gboolean	 egg_strtouint				(const gchar	*text,
							 guint		*value);
gboolean	 egg_strtouint64			(const gchar	*text,
							 guint64	*value);
gchar		*egg_strreplace				(const gchar	*text,
							 const gchar	*find,
							 const gchar	*replace);
void		 egg_string_test			(gpointer	 user_data);

G_END_DECLS

#endif /* __EGG_STRING_H */
