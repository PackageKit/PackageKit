/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __HIF_LOCK_H
#define __HIF_LOCK_H

#include <glib-object.h>

G_BEGIN_DECLS

#define HIF_TYPE_LOCK		(hif_lock_get_type ())
#define HIF_LOCK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HIF_TYPE_LOCK, HifLock))
#define HIF_LOCK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), HIF_TYPE_LOCK, HifLockClass))
#define HIF_IS_LOCK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), HIF_TYPE_LOCK))
#define HIF_IS_LOCK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), HIF_TYPE_LOCK))
#define HIF_LOCK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HIF_TYPE_LOCK, HifLockClass))

typedef struct _HifLock		HifLock;
typedef struct _HifLockPrivate	HifLockPrivate;
typedef struct _HifLockClass	HifLockClass;

struct _HifLock
{
	GObject			 parent;
	HifLockPrivate		*priv;
};

struct _HifLockClass
{
	GObjectClass		 parent_class;
	/* Signals */
	void			(* state_changed)	(HifLock	*lock,
							 guint		 state_bitfield);
};

typedef enum {
	HIF_LOCK_TYPE_RPMDB,
	HIF_LOCK_TYPE_REPO,
	HIF_LOCK_TYPE_METADATA,
	HIF_LOCK_TYPE_CONFIG,
	HIF_LOCK_TYPE_LAST
} HifLockType;

typedef enum {
	HIF_LOCK_MODE_THREAD,
	HIF_LOCK_MODE_PROCESS,
	HIF_LOCK_MODE_LAST
} HifLockMode;

GType		 hif_lock_get_type		(void);
HifLock		*hif_lock_new			(void);

guint		 hif_lock_take			(HifLock	*lock,
						 HifLockType	 type,
						 HifLockMode	 mode,
						 GError		**error);
gboolean	 hif_lock_release		(HifLock	*lock,
						 guint		 id,
						 GError		**error);
void		 hif_lock_release_noerror	(HifLock	*lock,
						 guint		 id);
const gchar	*hif_lock_type_to_string	(HifLockType	 lock_type);
guint		 hif_lock_get_state		(HifLock	*lock);

G_END_DECLS

#endif /* __HIF_LOCK_H */

