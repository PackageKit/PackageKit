/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "pk-network.h"
#include "pk-network-stack.h"
#include "pk-network-stack-unix.h"
#include "pk-network-stack-connman.h"
#include "pk-network-stack-nm.h"

static void     pk_network_finalize	(GObject        *object);

#define PK_NETWORK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK, PkNetworkPrivate))

/**
 * _PkNetworkPrivate:
 *
 * Private #PkNetwork data
 **/
struct _PkNetworkPrivate
{
	GPtrArray		*nstacks;
};

enum {
	PK_NETWORK_STATE_CHANGED,
	PK_NETWORK_LAST_SIGNAL
};

static guint signals [PK_NETWORK_LAST_SIGNAL] = { 0 };
static gpointer pk_network_object = NULL;

G_DEFINE_TYPE (PkNetwork, pk_network, G_TYPE_OBJECT)

/**
 * pk_network_get_network_state:
 * @network: a valid #PkNetwork instance
 *
 * Return value: %TRUE if the network is online
 * Note: This is a dummy file and no checks are done
 **/
PkNetworkEnum
pk_network_get_network_state (PkNetwork *network)
{
	PkNetworkEnum state;
	GPtrArray *nstacks;
	PkNetworkStack *nstack;
	guint i;

	g_return_val_if_fail (PK_IS_NETWORK (network), PK_NETWORK_ENUM_UNKNOWN);

	/* try each networking stack in order of preference */
	nstacks = network->priv->nstacks;
	for (i=0; i<nstacks->len; i++) {
		nstack = g_ptr_array_index (nstacks, i);
		if (pk_network_stack_is_enabled (nstack)) {
			state = pk_network_stack_get_state (nstack);
			if (state != PK_NETWORK_ENUM_UNKNOWN)
				goto out;
		}
	}

	/* no valid data providers */
	state = PK_NETWORK_ENUM_ONLINE;
out:
	return state;
}

/**
 * pk_network_stack_state_changed_cb:
 **/
static void
pk_network_stack_state_changed_cb (PkNetworkStack *nstack, PkNetworkEnum state, PkNetwork *network)
{
	g_return_if_fail (PK_IS_NETWORK (network));

	g_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (state));
	g_signal_emit (network, signals [PK_NETWORK_STATE_CHANGED], 0, state);
}

/**
 * pk_network_class_init:
 * @klass: The PkNetworkClass
 **/
static void
pk_network_class_init (PkNetworkClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_network_finalize;
	signals [PK_NETWORK_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkNetworkPrivate));
}

/**
 * pk_network_init:
 * @network: This class instance
 **/
static void
pk_network_init (PkNetwork *network)
{
	PkNetworkStack *nstack;
	network->priv = PK_NETWORK_GET_PRIVATE (network);

	/* array of PkNetworkStacks, in order of preference */
	network->priv->nstacks = g_ptr_array_new ();

#if PK_BUILD_NETWORKMANAGER
	nstack = PK_NETWORK_STACK (pk_network_stack_nm_new ());
	g_signal_connect (nstack, "state-changed",
			  G_CALLBACK (pk_network_stack_state_changed_cb), network);
	g_ptr_array_add (network->priv->nstacks, nstack);
#endif

#if PK_BUILD_CONNMAN
	nstack = PK_NETWORK_STACK (pk_network_stack_connman_new ());
	g_signal_connect (nstack, "state-changed",
			  G_CALLBACK (pk_network_stack_state_changed_cb), network);
	g_ptr_array_add (network->priv->nstacks, nstack);
#endif

	/* always build UNIX fallback */
	nstack = PK_NETWORK_STACK (pk_network_stack_unix_new ());
	g_signal_connect (nstack, "state-changed",
			  G_CALLBACK (pk_network_stack_state_changed_cb), network);
	g_ptr_array_add (network->priv->nstacks, nstack);
}

/**
 * pk_network_finalize:
 * @object: The object to finalize
 **/
static void
pk_network_finalize (GObject *object)
{
	PkNetwork *network;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK (object));
	network = PK_NETWORK (object);

	g_return_if_fail (network->priv != NULL);

	/* free all network stacks in use */
	g_ptr_array_foreach (network->priv->nstacks, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (network->priv->nstacks, TRUE);

	G_OBJECT_CLASS (pk_network_parent_class)->finalize (object);
}

/**
 * pk_network_new:
 *
 * Return value: a new PkNetwork object.
 **/
PkNetwork *
pk_network_new (void)
{
	if (pk_network_object != NULL) {
		g_object_ref (pk_network_object);
	} else {
		pk_network_object = g_object_new (PK_TYPE_NETWORK, NULL);
		g_object_add_weak_pointer (pk_network_object, &pk_network_object);
	}
	return PK_NETWORK (pk_network_object);
}

