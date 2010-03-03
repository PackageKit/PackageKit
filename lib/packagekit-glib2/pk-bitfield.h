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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_BITFIELD_H
#define __PK_BITFIELD_H

#include <glib.h>

G_BEGIN_DECLS

typedef guint64 PkBitfield;
#define PK_BITFIELD_FORMAT G_GUINT64_FORMAT

/* convenience functions as it's easy to forget the bitwise operators */
#define pk_bitfield_add(bitfield,enum)		do { ((bitfield) |= (pk_bitfield_value(enum))); } while (0)
#define pk_bitfield_remove(bitfield,enum)	do { ((bitfield) &= ~(pk_bitfield_value(enum))); } while (0)
#define pk_bitfield_invert(bitfield,enum)	do { ((bitfield) ^= (pk_bitfield_value(enum))); } while (0)
#define pk_bitfield_contain(bitfield,enum)	(((bitfield) & (pk_bitfield_value(enum))) > 0)
#define pk_bitfield_value(enum)			((PkBitfield) 1 << (enum))

gint		 pk_bitfield_contain_priority		(PkBitfield	 values,
							 gint		 value, ...);
PkBitfield	 pk_bitfield_from_enums			(gint		 value, ...);
PkBitfield	 pk_role_bitfield_from_text 		(const gchar	*roles);
gchar		*pk_role_bitfield_to_text		(PkBitfield	 roles);
PkBitfield	 pk_group_bitfield_from_text 		(const gchar	*groups);
gchar		*pk_group_bitfield_to_text		(PkBitfield	 groups);
PkBitfield	 pk_filter_bitfield_from_text 		(const gchar	*filters);
gchar		*pk_filter_bitfield_to_text		(PkBitfield	 filters);
void		 pk_bitfield_test			(gpointer	 user_data);

#define pk_role_bitfield_from_string	pk_role_bitfield_from_text
#define pk_role_bitfield_to_string	pk_role_bitfield_to_text
#define pk_group_bitfield_from_string	pk_group_bitfield_from_text
#define pk_group_bitfield_to_string	pk_group_bitfield_to_text
#define pk_filter_bitfield_from_string	pk_filter_bitfield_from_text
#define pk_filter_bitfield_to_string	pk_filter_bitfield_to_text

G_END_DECLS

#endif /* __PK_BITFIELD_H */

