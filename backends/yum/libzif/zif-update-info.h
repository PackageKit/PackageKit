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

#ifndef __ZIF_UPDATE_INFO_H
#define __ZIF_UPDATE_INFO_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ZIF_TYPE_UPDATE_INFO		(zif_update_info_get_type ())
#define ZIF_UPDATE_INFO(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_UPDATE_INFO, ZifUpdateInfo))
#define ZIF_UPDATE_INFO_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_UPDATE_INFO, ZifUpdateInfoClass))
#define ZIF_IS_UPDATE_INFO(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_UPDATE_INFO))
#define ZIF_IS_UPDATE_INFO_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_UPDATE_INFO))
#define ZIF_UPDATE_INFO_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_UPDATE_INFO, ZifUpdateInfoClass))
#define ZIF_UPDATE_INFO_ERROR		(zif_update_info_error_quark ())

typedef struct _ZifUpdateInfo		ZifUpdateInfo;
typedef struct _ZifUpdateInfoPrivate	ZifUpdateInfoPrivate;
typedef struct _ZifUpdateInfoClass	ZifUpdateInfoClass;

typedef enum {
	ZIF_UPDATE_INFO_KIND_CVE,
	ZIF_UPDATE_INFO_KIND_BUGZILLA,
	ZIF_UPDATE_INFO_KIND_LAST
} ZifUpdateInfoKind;

struct _ZifUpdateInfo
{
	GObject			 parent;
	ZifUpdateInfoPrivate	*priv;
};

struct _ZifUpdateInfoClass
{
	GObjectClass	parent_class;
};

GType			 zif_update_info_get_type		(void);
ZifUpdateInfo		*zif_update_info_new		(void);

/* public getters */
ZifUpdateInfoKind	 zif_update_info_get_kind	(ZifUpdateInfo		*update_info);
const gchar		*zif_update_info_get_url	(ZifUpdateInfo		*update_info);
const gchar		*zif_update_info_get_title	(ZifUpdateInfo		*update_info);

/* internal setters: TODO, in seporate -internal header file */
void			 zif_update_info_set_kind	(ZifUpdateInfo		*update_info,
							 ZifUpdateInfoKind	 kind);
void			 zif_update_info_set_url	(ZifUpdateInfo		*update_info,
							 const gchar		*url);
void			 zif_update_info_set_title	(ZifUpdateInfo		*update_info,
							 const gchar		*title);

/* utility functions */
const gchar		*zif_update_info_kind_to_string	(ZifUpdateInfoKind	 type);
ZifUpdateInfoKind	 zif_update_info_kind_from_string	(const gchar	*type);

G_END_DECLS

#endif /* __ZIF_UPDATE_INFO_H */

