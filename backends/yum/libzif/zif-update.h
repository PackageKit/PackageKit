/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_UPDATE_H
#define __ZIF_UPDATE_H

#include <glib-object.h>
#include <gio/gio.h>
#include <packagekit-glib2/packagekit.h>

#include "zif-update-info.h"

G_BEGIN_DECLS

#define ZIF_TYPE_UPDATE		(zif_update_get_type ())
#define ZIF_UPDATE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_UPDATE, ZifUpdate))
#define ZIF_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_UPDATE, ZifUpdateClass))
#define ZIF_IS_UPDATE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_UPDATE))
#define ZIF_IS_UPDATE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_UPDATE))
#define ZIF_UPDATE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_UPDATE, ZifUpdateClass))
#define ZIF_UPDATE_ERROR	(zif_update_error_quark ())

typedef struct _ZifUpdate	 ZifUpdate;
typedef struct _ZifUpdatePrivate ZifUpdatePrivate;
typedef struct _ZifUpdateClass	 ZifUpdateClass;

#include "zif-package.h"

struct _ZifUpdate
{
	GObject			 parent;
	ZifUpdatePrivate	*priv;
};

struct _ZifUpdateClass
{
	GObjectClass		 parent_class;
};

GType			 zif_update_get_type		(void);
ZifUpdate		*zif_update_new			(void);

/* public getters */
PkUpdateStateEnum	 zif_update_get_state		(ZifUpdate		*update);
PkInfoEnum		 zif_update_get_kind		(ZifUpdate		*update);
const gchar		*zif_update_get_id		(ZifUpdate		*update);
const gchar		*zif_update_get_title		(ZifUpdate		*update);
const gchar		*zif_update_get_description	(ZifUpdate		*update);
const gchar		*zif_update_get_issued		(ZifUpdate		*update);
GPtrArray		*zif_update_get_update_infos	(ZifUpdate		*update);
GPtrArray		*zif_update_get_packages	(ZifUpdate		*update);

/* internal setters: TODO, in seporate -internal header file */
void			 zif_update_set_state		(ZifUpdate		*update,
							 PkUpdateStateEnum	 state);
void			 zif_update_set_kind		(ZifUpdate		*update,
							 PkInfoEnum		 type);
void			 zif_update_set_id		(ZifUpdate		*update,
							 const gchar		*id);
void			 zif_update_set_title		(ZifUpdate		*update,
							 const gchar		*title);
void			 zif_update_set_description	(ZifUpdate		*update,
							 const gchar		*description);
void			 zif_update_set_issued		(ZifUpdate		*update,
							 const gchar		*issued);
void			 zif_update_add_update_info	(ZifUpdate		*update,
							 ZifUpdateInfo		*update_info);
void			 zif_update_add_package		(ZifUpdate		*update,
							 ZifPackage		*package);

G_END_DECLS

#endif /* __ZIF_UPDATE_H */

