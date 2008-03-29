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

#ifndef __PK_FILTER_H
#define __PK_FILTER_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PkFilter:
 *
 * Convenience object that is unwrapped.
 **/
typedef struct {
	gboolean installed;
	gboolean not_installed;
	gboolean devel;
	gboolean not_devel;
	gboolean gui;
	gboolean not_gui;
	gboolean supported;
	gboolean not_supported;
	gboolean visible;
	gboolean not_visible;
	gboolean basename;
	gboolean not_basename;
	gboolean newest;
	gboolean not_newest;
} PkFilter;

gboolean	 pk_filter_check			(const gchar	*filter)
							 G_GNUC_WARN_UNUSED_RESULT;
PkFilter	*pk_filter_new				(void)
							 G_GNUC_WARN_UNUSED_RESULT;
PkFilter	*pk_filter_new_from_string		(const gchar	*filter)
							 G_GNUC_WARN_UNUSED_RESULT;
gchar		*pk_filter_to_string			(PkFilter	*filter)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_filter_free				(PkFilter	*filter);
gboolean	 pk_filter_set_all			(PkFilter	*filter,
							 gboolean 	 value);

G_END_DECLS

#endif /* __PK_FILTER_H */
