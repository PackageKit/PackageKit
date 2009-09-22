/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <glib-object.h>

#include "pk-network-stack.h"

enum
{
	STATE_CHANGED,
	LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkNetworkStack, pk_network_stack, G_TYPE_OBJECT)

/**
 * pk_network_stack_is_enabled:
 **/
gboolean
pk_network_stack_is_enabled (PkNetworkStack *nstack)
{
	PkNetworkStackClass *klass = PK_NETWORK_STACK_GET_CLASS (nstack);

	g_return_val_if_fail (PK_IS_NETWORK_STACK (nstack), FALSE);

	/* no support */
	if (klass->is_enabled == NULL)
		return FALSE;

	return klass->is_enabled (nstack);
}

/**
 * pk_network_stack_get_state:
 **/
PkNetworkEnum
pk_network_stack_get_state (PkNetworkStack *nstack)
{
	PkNetworkStackClass *klass = PK_NETWORK_STACK_GET_CLASS (nstack);

	g_return_val_if_fail (PK_IS_NETWORK_STACK (nstack), PK_NETWORK_ENUM_UNKNOWN);

	/* no support */
	if (klass->get_state == NULL)
		return PK_NETWORK_ENUM_UNKNOWN;

	return klass->get_state (nstack);
}

/**
 * pk_network_stack_init:
 **/
static void
pk_network_stack_init (PkNetworkStack *nstack)
{
}

/**
 * pk_network_stack_finalize:
 **/
static void
pk_network_stack_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_STACK (object));
	G_OBJECT_CLASS (pk_network_stack_parent_class)->finalize (object);
}

/**
 * pk_network_stack_class_init:
 **/
static void
pk_network_stack_class_init (PkNetworkStackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_network_stack_finalize;

	signals[STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

