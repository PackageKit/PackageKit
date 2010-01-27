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

#ifndef __PK_NOTIFY_H
#define __PK_NOTIFY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_NOTIFY		(pk_notify_get_type ())
#define PK_NOTIFY(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_NOTIFY, PkNotify))
#define PK_NOTIFY_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_NOTIFY, PkNotifyClass))
#define PK_IS_NOTIFY(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_NOTIFY))
#define PK_IS_NOTIFY_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_NOTIFY))
#define PK_NOTIFY_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_NOTIFY, PkNotifyClass))

typedef struct PkNotifyPrivate PkNotifyPrivate;

typedef struct
{
	GObject		      parent;
	PkNotifyPrivate     *priv;
} PkNotify;

typedef struct
{
	GObjectClass	parent_class;
} PkNotifyClass;

GType		 pk_notify_get_type		(void);
PkNotify	*pk_notify_new			(void);

gboolean	 pk_notify_repo_list_changed	(PkNotify	*notify);
gboolean	 pk_notify_updates_changed	(PkNotify	*notify);
gboolean	 pk_notify_wait_updates_changed	(PkNotify	*notify,
						 guint		 timeout);

G_END_DECLS

#endif /* __PK_NOTIFY_H */

