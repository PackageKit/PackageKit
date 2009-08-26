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
 * SECTION:pk-control
 * @short_description: An abstract control GObject
 */

#ifndef __PK_CONTROL_H
#define __PK_CONTROL_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_CONTROL		(pk_control_get_type ())
#define PK_CONTROL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CONTROL, PkControl))
#define PK_CONTROL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CONTROL, PkControlClass))
#define PK_IS_CONTROL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CONTROL))
#define PK_IS_CONTROL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CONTROL))
#define PK_CONTROL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CONTROL, PkControlClass))
#define PK_CONTROL_ERROR	(pk_control_error_quark ())
#define PK_CONTROL_TYPE_ERROR	(pk_control_error_get_type ())

typedef struct _PkControlPrivate	PkControlPrivate;
typedef struct _PkControl		PkControl;
typedef struct _PkControlClass		PkControlClass;

struct _PkControl
{
	 GObject		 parent;
	 PkControlPrivate	*priv;
};

struct _PkControlClass
{
	GObjectClass	parent_class;

	/* signals */
	void		(* changed)			(PkControl	*control);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_control_error_quark			(void);
GType		 pk_control_get_type		  	(void);
PkControl	*pk_control_new				(void);

void		 pk_control_get_tid_async		(PkControl		*control,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
gchar		*pk_control_get_tid_finish		(PkControl		*control,
							 GAsyncResult		*result,
							 GError			**error);

G_END_DECLS

#endif /* __PK_CONTROL_H */

