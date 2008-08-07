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

#ifndef __PK_UPDATE_DETAIL_H
#define __PK_UPDATE_DETAIL_H

#include <glib-object.h>
#include <pk-enum.h>
#include <pk-package-id.h>

G_BEGIN_DECLS

/**
 * PkUpdateDetailObj:
 *
 * Cached object to represent details about the update.
 **/
typedef struct
{
	PkPackageId			*id;
	gchar				*updates;
	gchar				*obsoletes;
	gchar				*vendor_url;
	gchar				*bugzilla_url;
	gchar				*cve_url;
	PkRestartEnum			 restart;
	gchar				*update_text;
	gchar				*changelog;
	PkUpdateStateEnum		 state;
	GDate				*issued;
	GDate				*updated;
} PkUpdateDetailObj;

PkUpdateDetailObj	*pk_update_detail_obj_new		(void);
PkUpdateDetailObj	*pk_update_detail_obj_copy		(const PkUpdateDetailObj *obj);
PkUpdateDetailObj	*pk_update_detail_obj_new_from_data	(const PkPackageId	*id,
								 const gchar		*updates,
								 const gchar		*obsoletes,
								 const gchar		*vendor_url,
								 const gchar		*bugzilla_url,
								 const gchar		*cve_url,
								 PkRestartEnum		 restart,
								 const gchar		*update_text,
								 const gchar		*changelog,
								 PkUpdateStateEnum	 state,
								 GDate			*issued,
								 GDate			*updated);
gboolean		 pk_update_detail_obj_free		(PkUpdateDetailObj	*obj);

G_END_DECLS

#endif /* __PK_UPDATE_DETAIL_H */
