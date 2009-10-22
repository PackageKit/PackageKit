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

#ifndef __PK_ERROR_CODE_H
#define __PK_ERROR_CODE_H

#include <glib-object.h>
#include <packagekit-glib2/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_ERROR_CODE		(pk_error_code_get_type ())
#define PK_ERROR_CODE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_ERROR_CODE, PkErrorCode))
#define PK_ERROR_CODE_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_ERROR_CODE, PkErrorCodeClass))
#define PK_IS_ERROR_CODE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_ERROR_CODE))
#define PK_IS_ERROR_CODE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_ERROR_CODE))
#define PK_ERROR_CODE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_ERROR_CODE, PkErrorCodeClass))

typedef struct _PkErrorCodePrivate	PkErrorCodePrivate;
typedef struct _PkErrorCode		PkErrorCode;
typedef struct _PkErrorCodeClass	PkErrorCodeClass;

struct _PkErrorCode
{
	 GObject		 parent;
	 PkErrorCodePrivate	*priv;
};

struct _PkErrorCodeClass
{
	GObjectClass	parent_class;
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GType		 pk_error_code_get_type			(void);
PkErrorCode	*pk_error_code_new			(void);

PkErrorCodeEnum	 pk_error_code_get_code			(PkErrorCode *error_code);
const gchar	*pk_error_code_get_details		(PkErrorCode *error_code);

G_END_DECLS

#endif /* __PK_ERROR_CODE_H */

