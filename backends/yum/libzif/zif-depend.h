/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#if !defined (__ZIF_H_INSIDE__) && !defined (ZIF_COMPILATION)
#error "Only <zif.h> can be included directly."
#endif

#ifndef __ZIF_DEPEND_H
#define __ZIF_DEPEND_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
	ZIF_DEPEND_FLAG_ANY,
	ZIF_DEPEND_FLAG_LESS,
	ZIF_DEPEND_FLAG_GREATER,
	ZIF_DEPEND_FLAG_EQUAL,
	ZIF_DEPEND_FLAG_UNKNOWN
} ZifDependFlag;

typedef struct {
	gchar		*name;
	ZifDependFlag	 flag;
	gchar		*version;
	guint		 count;
} ZifDepend;

ZifDepend	*zif_depend_new			(const gchar		*name,
						 ZifDependFlag		 flag,
						 const gchar		*version);
ZifDepend	*zif_depend_new_value		(gchar			*name,
						 ZifDependFlag		 flag,
						 gchar			*version);
ZifDepend	*zif_depend_ref			(ZifDepend		*depend);
ZifDepend	*zif_depend_unref		(ZifDepend		*depend);
gchar		*zif_depend_to_string		(const ZifDepend	*depend);
const gchar	*zif_depend_flag_to_string	(ZifDependFlag		 flag);

G_END_DECLS

#endif /* __ZIF_DEPEND_H */

