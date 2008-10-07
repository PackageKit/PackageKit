/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
 * GNU General Public License for more category.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __PK_CATEGORY_H
#define __PK_CATEGORY_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PkCategoryObj:
 *
 * Cached object to represent category about the package.
 **/
typedef struct
{
	struct PkCategoryObj		*parent;
	gchar				*parent_id;
	gchar				*cat_id;
	gchar				*name;
	gchar				*summary;
	gchar				*icon;
} PkCategoryObj;

PkCategoryObj	*pk_category_obj_new			(void);
PkCategoryObj	*pk_category_obj_copy			(const PkCategoryObj	*obj);
PkCategoryObj	*pk_category_obj_new_from_data		(const gchar		*parent_id,
							 const gchar		*cat_id,
							 const gchar		*name,
							 const gchar		*summary,
							 const gchar		*icon);
gboolean	 pk_category_obj_free			(PkCategoryObj		*obj);

G_END_DECLS

#endif /* __PK_CATEGORY_H */
