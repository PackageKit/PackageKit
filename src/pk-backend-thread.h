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

#ifndef __PK_BACKEND_THREAD_H
#define __PK_BACKEND_THREAD_H

#include <glib-object.h>

#include "pk-backend-internal.h"
#include "pk-backend-thread.h"

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_THREAD		(pk_backend_thread_get_type ())
#define PK_BACKEND_THREAD(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND_THREAD, PkBackendThread))
#define PK_BACKEND_THREAD_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND_THREAD, PkBackendThreadClass))
#define PK_IS_BACKEND_THREAD(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND_THREAD))
#define PK_IS_BACKEND_THREAD_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND_THREAD))
#define PK_BACKEND_THREAD_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND_THREAD, PkBackendThreadClass))

typedef struct PkBackendThreadPrivate PkBackendThreadPrivate;

typedef struct
{
	 GObject		 parent;
	 PkBackendThreadPrivate	*priv;
} PkBackendThread;

typedef struct
{
	GObjectClass	parent_class;
} PkBackendThreadClass;

/* general */
GType		 pk_backend_thread_get_type		(void) G_GNUC_CONST;
PkBackendThread	*pk_backend_thread_new			(void);
typedef gboolean (*PkBackendThreadFunc)			(PkBackendThread	*backend_thread,
							 gpointer		 data);
gboolean	 pk_backend_thread_create		(PkBackendThread	*backend_thread,
							 PkBackendThreadFunc	 func,
							 gpointer		 data);
PkBackend	*pk_backend_thread_get_backend		(PkBackendThread	*backend_thread);


G_END_DECLS

#endif /* __PK_BACKEND_THREAD_H */

