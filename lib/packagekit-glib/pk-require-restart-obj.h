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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_REQUIRE_RESTART_OBJ_H
#define __PK_REQUIRE_RESTART_OBJ_H

#include <glib-object.h>
#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-package-id.h>

G_BEGIN_DECLS

/**
 * PkRequireRestartObj:
 *
 * Cached object to represent details about the require_restart.
 **/
typedef struct
{
	PkRestartEnum			 restart;
	PkPackageId			*id;
} PkRequireRestartObj;

PkRequireRestartObj	*pk_require_restart_obj_new		(void);
PkRequireRestartObj	*pk_require_restart_obj_copy		(const PkRequireRestartObj *obj);
PkRequireRestartObj	*pk_require_restart_obj_new_from_data	(PkRestartEnum		 restart,
								 const PkPackageId	*id);
gboolean		 pk_require_restart_obj_free		(PkRequireRestartObj	*obj);

G_END_DECLS

#endif /* __PK_REQUIRE_RESTART_OBJ_H */
