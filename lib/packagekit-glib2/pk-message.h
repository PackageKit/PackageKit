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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_MESSAGE_H
#define __PK_MESSAGE_H

#include <glib-object.h>

#include <packagekit-glib2/pk-source.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_MESSAGE			(pk_message_get_type ())
#define PK_MESSAGE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_MESSAGE, PkMessage))
#define PK_MESSAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_MESSAGE, PkMessageClass))
#define PK_IS_MESSAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_MESSAGE))
#define PK_IS_MESSAGE_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_MESSAGE))
#define PK_MESSAGE_GET_CLASS(o)		(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_MESSAGE, PkMessageClass))

typedef struct _PkMessagePrivate	PkMessagePrivate;
typedef struct _PkMessage		PkMessage;
typedef struct _PkMessageClass		PkMessageClass;

struct _PkMessage
{
	 PkSource		 parent;
	 PkMessagePrivate	*priv;
};

struct _PkMessageClass
{
	PkSourceClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_message_get_type		(void);
PkMessage	*pk_message_new			(void);

/* accessors */
const gchar	*pk_message_get_details		(PkMessage	*message);
PkMessageEnum	 pk_message_get_kind		(PkMessage	*message);

G_END_DECLS

#endif /* __PK_MESSAGE_H */

