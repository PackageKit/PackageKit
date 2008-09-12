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

#ifndef __PK_BITFIELD_H
#define __PK_BITFIELD_H

#include <glib-object.h>
#include <glib.h>
#include "pk-enum.h"

G_BEGIN_DECLS

typedef guint64 PkBitfield;

/* convenience functions as it's easy to forget the bitwise operators */
#define pk_bitfield_add(bitfield,enum)		do { ((bitfield) |= (pk_bitfield_value(enum))); } while (0)
#define pk_bitfield_remove(bitfield,enum)	do { ((bitfield) &= ~(pk_bitfield_value(enum))); } while (0)
#define pk_bitfield_contain(bitfield,enum)	(((bitfield) & (pk_bitfield_value(enum))) > 0)
#define pk_bitfield_value(enum)			((PkBitfield) 1 << (enum))

gint		 pk_bitfield_contain_priority		(PkBitfield	 values,
							 gint		 value, ...);
PkBitfield	 pk_bitfield_from_enums			(gint		 value, ...);
PkBitfield	 pk_role_bitfield_from_text 		(const gchar	*roles);
gchar		*pk_role_bitfield_to_text		(PkBitfield	 roles);
PkBitfield	 pk_group_bitfield_from_text 		(const gchar	*groups);
gchar		*pk_group_bitfield_to_text		(PkBitfield groups);
PkBitfield	 pk_filter_bitfield_from_text 		(const gchar	*filters);
gchar		*pk_filter_bitfield_to_text		(PkBitfield filters);

G_END_DECLS

#endif /* __PK_BITFIELD_H */
