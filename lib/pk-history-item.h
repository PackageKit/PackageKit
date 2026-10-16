/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(__PACKAGEKIT_H_INSIDE__) && !defined(PK_COMPILATION)
#error "Only <packagekit/packagekit.h> can be included directly."
#endif

#ifndef __PK_HISTORY_ITEM_H
#define __PK_HISTORY_ITEM_H

#include <glib-object.h>

#include "pk-enum.h"
#include "pk-source.h"

G_BEGIN_DECLS

#define PK_TYPE_HISTORY_ITEM (pk_history_item_get_type ())
#define PK_HISTORY_ITEM(o)   (G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_HISTORY_ITEM, PkHistoryItem))
#define PK_HISTORY_ITEM_CLASS(k) \
	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_HISTORY_ITEM, PkHistoryItemClass))
#define PK_IS_HISTORY_ITEM(o)	    (G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_HISTORY_ITEM))
#define PK_IS_HISTORY_ITEM_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_HISTORY_ITEM))
#define PK_HISTORY_ITEM_GET_CLASS(o) \
	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_HISTORY_ITEM, PkHistoryItemClass))

typedef struct _PkHistoryItemPrivate PkHistoryItemPrivate;
typedef struct _PkHistoryItem	     PkHistoryItem;
typedef struct _PkHistoryItemClass   PkHistoryItemClass;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PkHistoryItem, g_object_unref)
#endif

struct _PkHistoryItem
{
	PkSource	      parent;
	PkHistoryItemPrivate *priv;
};

struct _PkHistoryItemClass
{
	PkSourceClass parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType	       pk_history_item_get_type (void);
PkHistoryItem *pk_history_item_new (void);
const gchar   *pk_history_item_get_cmdline (PkHistoryItem *item);
const gchar   *pk_history_item_get_id (PkHistoryItem *item);
GDateTime     *pk_history_item_get_time_started (PkHistoryItem *item);
gboolean       pk_history_item_get_succeeded (PkHistoryItem *item);
guint	       pk_history_item_get_duration (PkHistoryItem *item);
guint	       pk_history_item_get_uid (PkHistoryItem *item);
PkRoleEnum     pk_history_item_get_role (PkHistoryItem *item);
GPtrArray     *pk_history_item_get_packages (PkHistoryItem *item);

G_END_DECLS

#endif /* __PK_HISTORY_ITEM_H */
