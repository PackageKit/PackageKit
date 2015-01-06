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

gboolean	 pk_directory_remove_contents		(const gchar	*directory);
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

gchar		*pk_util_get_config_filename		(void);
gboolean	 pk_util_set_auto_backend		(GKeyFile	*conf,
							 GError		**error);

#define pk_is_thread_default() pk_is_thread_default_real(G_STRLOC, G_STRFUNC)
gboolean	 pk_is_thread_default_real		(const gchar *strloc,
							 const gchar *strfunc);

gboolean	 pk_ioprio_set_idle			(GPid		 pid);
guint		 pk_string_replace			(GString	*string,
							 const gchar	*search,
							 const gchar	*replace);

G_END_DECLS

#endif /* __PK_SHARED_H */
