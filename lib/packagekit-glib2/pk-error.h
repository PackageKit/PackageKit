/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_ERROR_CODE_H
#define __PK_ERROR_CODE_H

#include <glib-object.h>

#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-source.h>

G_BEGIN_DECLS

#define PK_TYPE_ERROR_CODE		(pk_error_get_type ())
#define PK_ERROR_CODE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_ERROR_CODE, PkError))
#define PK_ERROR_CODE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_ERROR_CODE, PkErrorClass))
#define PK_IS_ERROR_CODE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_ERROR_CODE))
#define PK_IS_ERROR_CODE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_ERROR_CODE))
#define PK_ERROR_CODE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_ERROR_CODE, PkErrorClass))

typedef struct _PkErrorPrivate	PkErrorPrivate;
typedef struct _PkError		PkError;
typedef struct _PkErrorClass	PkErrorClass;

struct _PkError
{
	 PkSource		 parent;
	 PkErrorPrivate	*priv;
};

struct _PkErrorClass
{
	PkSourceClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_error_get_type			(void);
PkError	*pk_error_new			(void);

PkErrorEnum	 pk_error_get_code			(PkError *error_code);
const gchar	*pk_error_get_details		(PkError *error_code);

G_END_DECLS

#endif /* __PK_ERROR_CODE_H */

