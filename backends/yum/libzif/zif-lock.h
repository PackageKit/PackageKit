/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __ZIF_LOCK_H
#define __ZIF_LOCK_H

#include <glib-object.h>
#include "zif-completion.h"

G_BEGIN_DECLS

#define ZIF_TYPE_LOCK		(zif_lock_get_type ())
#define ZIF_LOCK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), ZIF_TYPE_LOCK, ZifLock))
#define ZIF_LOCK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), ZIF_TYPE_LOCK, ZifLockClass))
#define ZIF_IS_LOCK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), ZIF_TYPE_LOCK))
#define ZIF_IS_LOCK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), ZIF_TYPE_LOCK))
#define ZIF_LOCK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), ZIF_TYPE_LOCK, ZifLockClass))

typedef struct _ZifLock		ZifLock;
typedef struct _ZifLockPrivate	ZifLockPrivate;
typedef struct _ZifLockClass	ZifLockClass;

struct _ZifLock
{
	GObject				 parent;
	ZifLockPrivate	*priv;
};

struct _ZifLockClass
{
	GObjectClass	parent_class;
};

GType		 zif_lock_get_type			(void);
ZifLock		*zif_lock_new				(void);
gboolean	 zif_lock_is_locked			(ZifLock		*lock,
							 guint			*pid);
gboolean	 zif_lock_set_locked			(ZifLock		*lock,
							 guint			*pid,
							 GError			**error);
gboolean	 zif_lock_set_unlocked			(ZifLock		*lock,
							 GError			**error);

G_END_DECLS

#endif /* __ZIF_LOCK_H */

