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

#ifndef __ZIF_CHANGESET_H
#define __ZIF_CHANGESET_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ZIF_TYPE_CHANGESET		(zif_changeset_get_type ())
#define ZIF_CHANGESET(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_CHANGESET, ZifChangeset))
#define ZIF_CHANGESET_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_CHANGESET, ZifChangesetClass))
#define ZIF_IS_CHANGESET(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_CHANGESET))
#define ZIF_IS_CHANGESET_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_CHANGESET))
#define ZIF_CHANGESET_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_CHANGESET, ZifChangesetClass))
#define ZIF_CHANGESET_ERROR		(zif_changeset_error_quark ())

typedef struct _ZifChangeset		 ZifChangeset;
typedef struct _ZifChangesetPrivate	 ZifChangesetPrivate;
typedef struct _ZifChangesetClass	 ZifChangesetClass;

#include "zif-package.h"

struct _ZifChangeset
{
	GObject			 parent;
	ZifChangesetPrivate	*priv;
};

struct _ZifChangesetClass
{
	GObjectClass		 parent_class;
};

GType			 zif_changeset_get_type		(void);
ZifChangeset		*zif_changeset_new		(void);

/* public getters */
guint64			 zif_changeset_get_date		(ZifChangeset		*changeset);
const gchar		*zif_changeset_get_author	(ZifChangeset		*changeset);
const gchar		*zif_changeset_get_description	(ZifChangeset		*changeset);
const gchar		*zif_changeset_get_version	(ZifChangeset		*changeset);

/* internal setters: TODO, in seporate -internal header file */
void			 zif_changeset_set_date		(ZifChangeset		*changeset,
							 guint64		 date);
void			 zif_changeset_set_author	(ZifChangeset		*changeset,
							 const gchar		*author);
void			 zif_changeset_set_description	(ZifChangeset		*changeset,
							 const gchar		*description);
void			 zif_changeset_set_version	(ZifChangeset		*changeset,
							 const gchar		*version);
gboolean		 zif_changeset_parse_header	(ZifChangeset		*changeset,
							 const gchar		*header,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_CHANGESET_H */

