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

/**
 * SECTION:pk-error
 * @short_description: ErrorCode object
 *
 * This GObject represents a error_code from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-error.h>
#include <packagekit-glib2/pk-enum.h>

static void     pk_error_finalize	(GObject     *object);

#define PK_ERROR_CODE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ERROR_CODE, PkErrorPrivate))

/**
 * PkErrorPrivate:
 *
 * Private #PkError data
 **/
struct _PkErrorPrivate
{
	PkErrorEnum			 code;
	gchar				*details;
};

enum {
	PROP_0,
	PROP_CODE,
	PROP_DETAILS,
	PROP_LAST
};

G_DEFINE_TYPE (PkError, pk_error, PK_TYPE_SOURCE)

/**
 * pk_error_get_property:
 **/
static void
pk_error_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkError *error_code = PK_ERROR_CODE (object);
	PkErrorPrivate *priv = error_code->priv;

	switch (prop_id) {
	case PROP_CODE:
		g_value_set_uint (value, priv->code);
		break;
	case PROP_DETAILS:
		g_value_set_string (value, priv->details);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_error_set_property:
 **/
static void
pk_error_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkError *error_code = PK_ERROR_CODE (object);
	PkErrorPrivate *priv = error_code->priv;

	switch (prop_id) {
	case PROP_CODE:
		priv->code = g_value_get_uint (value);
		break;
	case PROP_DETAILS:
		g_free (priv->details);
		priv->details = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_error_get_code:
 *
 * Since: 0.5.5
 **/
PkErrorEnum
pk_error_get_code (PkError *error_code)
{
	g_return_val_if_fail (PK_IS_ERROR_CODE (error_code), 0);
	return error_code->priv->code;
}

/**
 * pk_error_get_details:
 *
 * Since: 0.5.5
 **/
const gchar *
pk_error_get_details (PkError *error_code)
{
	g_return_val_if_fail (PK_IS_ERROR_CODE (error_code), NULL);
	return error_code->priv->details;
}

/**
 * pk_error_class_init:
 **/
static void
pk_error_class_init (PkErrorClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_error_finalize;
	object_class->get_property = pk_error_get_property;
	object_class->set_property = pk_error_set_property;

	/**
	 * PkError:code:
	 *
	 * Since: 0.5.5
	 */
	pspec = g_param_spec_uint ("code", NULL, NULL,
				   0, G_MAXUINT, PK_ERROR_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CODE, pspec);

	/**
	 * PkError:details:
	 *
	 * Since: 0.5.5
	 */
	pspec = g_param_spec_string ("details", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DETAILS, pspec);

	g_type_class_add_private (klass, sizeof (PkErrorPrivate));
}

/**
 * pk_error_init:
 **/
static void
pk_error_init (PkError *error_code)
{
	error_code->priv = PK_ERROR_CODE_GET_PRIVATE (error_code);
}

/**
 * pk_error_finalize:
 **/
static void
pk_error_finalize (GObject *object)
{
	PkError *error_code = PK_ERROR_CODE (object);
	PkErrorPrivate *priv = error_code->priv;

	g_free (priv->details);

	G_OBJECT_CLASS (pk_error_parent_class)->finalize (object);
}

/**
 * pk_error_new:
 *
 * Return value: a new PkError object.
 *
 * Since: 0.5.5
 **/
PkError *
pk_error_new (void)
{
	PkError *error_code;
	error_code = g_object_new (PK_TYPE_ERROR_CODE, NULL);
	return PK_ERROR_CODE (error_code);
}

