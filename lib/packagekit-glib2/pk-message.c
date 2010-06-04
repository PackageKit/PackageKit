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
 * SECTION:pk-message
 * @short_description: Message object
 *
 * This GObject represents a message from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-message.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"

static void     pk_message_finalize	(GObject     *object);

#define PK_MESSAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_MESSAGE, PkMessagePrivate))

/**
 * PkMessagePrivate:
 *
 * Private #PkMessage data
 **/
struct _PkMessagePrivate
{
	PkMessageEnum			 type;
	gchar				*details;
};

enum {
	PROP_0,
	PROP_TYPE,
	PROP_DETAILS,
	PROP_LAST
};

G_DEFINE_TYPE (PkMessage, pk_message, PK_TYPE_SOURCE)

/**
 * pk_message_get_kind:
 * @message: a valid #PkMessage instance
 *
 * Gets the message kind
 *
 * Return value: the %PkMessageEnum
 *
 * Since: 0.6.4
 **/
PkMessageEnum
pk_message_get_kind (PkMessage *message)
{
	g_return_val_if_fail (PK_IS_MESSAGE (message), PK_MESSAGE_ENUM_UNKNOWN);
	return message->priv->type;
}

/**
 * pk_message_get_details:
 * @message: a valid #PkMessage instance
 *
 * Gets the message details.
 *
 * Return value: the details, or %NULL if unset
 *
 * Since: 0.6.4
 **/
const gchar *
pk_message_get_details (PkMessage *message)
{
	g_return_val_if_fail (PK_IS_MESSAGE (message), NULL);
	return message->priv->details;
}

/**
 * pk_message_get_property:
 **/
static void
pk_message_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkMessage *message = PK_MESSAGE (object);
	PkMessagePrivate *priv = message->priv;

	switch (prop_id) {
	case PROP_TYPE:
		g_value_set_uint (value, priv->type);
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
 * pk_message_set_property:
 **/
static void
pk_message_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkMessage *message = PK_MESSAGE (object);
	PkMessagePrivate *priv = message->priv;

	switch (prop_id) {
	case PROP_TYPE:
		priv->type = g_value_get_uint (value);
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
 * pk_message_class_init:
 **/
static void
pk_message_class_init (PkMessageClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_message_finalize;
	object_class->get_property = pk_message_get_property;
	object_class->set_property = pk_message_set_property;

	/**
	 * PkMessage:type:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_uint ("type", NULL, NULL,
				   0, G_MAXUINT, PK_MESSAGE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TYPE, pspec);

	/**
	 * PkMessage:details:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("details", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DETAILS, pspec);

	g_type_class_add_private (klass, sizeof (PkMessagePrivate));
}

/**
 * pk_message_init:
 **/
static void
pk_message_init (PkMessage *message)
{
	message->priv = PK_MESSAGE_GET_PRIVATE (message);
}

/**
 * pk_message_finalize:
 **/
static void
pk_message_finalize (GObject *object)
{
	PkMessage *message = PK_MESSAGE (object);
	PkMessagePrivate *priv = message->priv;

	g_free (priv->details);

	G_OBJECT_CLASS (pk_message_parent_class)->finalize (object);
}

/**
 * pk_message_new:
 *
 * Return value: a new PkMessage object.
 *
 * Since: 0.5.4
 **/
PkMessage *
pk_message_new (void)
{
	PkMessage *message;
	message = g_object_new (PK_TYPE_MESSAGE, NULL);
	return PK_MESSAGE (message);
}

