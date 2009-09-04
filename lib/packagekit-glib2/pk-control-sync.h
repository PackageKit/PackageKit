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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

/**
 * SECTION:pk-control_sync
 * @short_description: An abstract synchronous control access GObject
 */

#ifndef __PK_CONTROL_SYNC_H
#define __PK_CONTROL_SYNC_H

#include <glib-object.h>

#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-bitfield.h>

G_BEGIN_DECLS

#define PK_TYPE_CONTROL_SYNC		(pk_control_sync_get_type ())
#define PK_CONTROL_SYNC(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CONTROL_SYNC, PkControlSync))
#define PK_CONTROL_SYNC_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CONTROL_SYNC, PkControlSyncClass))
#define PK_IS_CONTROL_SYNC(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CONTROL_SYNC))
#define PK_IS_CONTROL_SYNC_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CONTROL_SYNC))
#define PK_CONTROL_SYNC_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CONTROL_SYNC, PkControlSyncClass))

typedef struct _PkControlSyncPrivate	PkControlSyncPrivate;
typedef struct _PkControlSync		PkControlSync;
typedef struct _PkControlSyncClass	PkControlSyncClass;

struct _PkControlSync
{
	 PkControl		 parent;
	 PkControlSyncPrivate	*priv;
};

struct _PkControlSyncClass
{
	PkControlClass		parent_class;
};

GQuark		 pk_control_sync_error_quark		(void);
GType		 pk_control_sync_get_type		(void);
PkControlSync	*pk_control_sync_new			(void);
void		 pk_control_sync_test			(gpointer		 user_data);

gchar		*pk_control_sync_get_daemon_state	(PkControlSync		*control,
							 GError			**error);
gboolean	 pk_control_sync_get_properties		(PkControlSync		*control,
							 GError			**error);

G_END_DECLS

#endif /* __PK_CONTROL_SYNC_H */

