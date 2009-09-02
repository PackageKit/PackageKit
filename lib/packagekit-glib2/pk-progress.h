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
 * SECTION:pk-progress
 * @short_description: An abstract progress GObject
 */

#ifndef __PK_PROGRESS_H
#define __PK_PROGRESS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_PROGRESS		(pk_progress_get_type ())
#define PK_PROGRESS(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PROGRESS, PkProgress))
#define PK_PROGRESS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PROGRESS, PkProgressClass))
#define PK_IS_PROGRESS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PROGRESS))
#define PK_IS_PROGRESS_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PROGRESS))
#define PK_PROGRESS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PROGRESS, PkProgressClass))
#define PK_PROGRESS_ERROR		(pk_progress_error_quark ())
#define PK_PROGRESS_TYPE_ERROR		(pk_progress_error_get_type ())

typedef struct _PkProgressPrivate	PkProgressPrivate;
typedef struct _PkProgress		PkProgress;
typedef struct _PkProgressClass		PkProgressClass;

struct _PkProgress
{
	 GObject		 parent;
	 PkProgressPrivate	*priv;
};

struct _PkProgressClass
{
	GObjectClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_progress_error_quark		(void);
GType		 pk_progress_get_type		  	(void);
PkProgress	*pk_progress_new			(void);

typedef enum {
	PK_PROGRESS_TYPE_PACKAGE_ID,
	PK_PROGRESS_TYPE_PERCENTAGE,
	PK_PROGRESS_TYPE_SUBPERCENTAGE,
	PK_PROGRESS_TYPE_ALLOW_CANCEL,
	PK_PROGRESS_TYPE_STATUS,
	PK_PROGRESS_TYPE_ROLE,
	PK_PROGRESS_TYPE_CALLER_ACTIVE,
	PK_PROGRESS_TYPE_INVALID
} PkProgressType;

typedef void	(*PkProgressCallback)			(PkProgress		*progress,
							 PkProgressType		 type,
                                                         gpointer		 user_data);
void		 pk_progress_test			(gpointer		 user_data);

G_END_DECLS

#endif /* __PK_PROGRESS_H */

