/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_EXTRA_OBJ_H
#define __PK_EXTRA_OBJ_H

#include <glib-object.h>
#include "pk-package-id.h"

G_BEGIN_DECLS

/**
 * PkExtraObj:
 *
 * The cached structure for the extra fields from the metadata store
 */
typedef struct {
	PkPackageId	*id;
	gchar		*icon;
	gchar		*exec;
	gchar		*summary;	/* one line quick description */
} PkExtraObj;

PkExtraObj	*pk_extra_obj_new			(void);
PkExtraObj	*pk_extra_obj_new_from_package_id	(const gchar	*package_id);
PkExtraObj	*pk_extra_obj_new_from_package_id_summary (const gchar	*package_id,
							 const gchar	*summary);
gboolean	 pk_extra_obj_free			(PkExtraObj	*extra_obj);

G_END_DECLS

#endif /* __PK_EXTRA_OBJ_H */
