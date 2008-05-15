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

/**
 * SECTION:pk-network_unix-dummy
 * @short_description: Dummy network_unix detection code
 *
 * This file contains a unix network implimentation.
 * It is designed for people that don't have NetworkUnixManager installed.
 * It polls the network to wait to see if it's up
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

#include "pk-common.h"
#include "pk-debug.h"
#include "pk-network-unix.h"
#include "pk-marshal.h"

static void     pk_network_unix_class_init	(PkNetworkUnixClass *klass);
static void     pk_network_unix_init		(PkNetworkUnix      *network_unix);
static void     pk_network_unix_finalize	(GObject        *object);

#define PK_NETWORK_UNIX_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_UNIX, PkNetworkUnixPrivate))

/**
 * _PkNetworkUnixPrivate:
 *
 * Private #PkNetworkUnix data
 **/
struct _PkNetworkUnixPrivate
{
	gpointer		 data;
};

enum {
	PK_NETWORK_UNIX_STATE_CHANGED,
	PK_NETWORK_UNIX_LAST_SIGNAL
};

static guint signals [PK_NETWORK_UNIX_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkNetworkUnix, pk_network_unix, G_TYPE_OBJECT)
#define PK_NETWORK_PROC_ROUTE	"/proc/net/route"


/**
 * pk_network_unix_get_network_state:
 * @network_unix: a valid #PkNetworkUnix instance
 *
 * Return value: %TRUE if the network is online
 **/
PkNetworkEnum
pk_network_unix_get_network_state (PkNetworkUnix *network_unix)
{
	gchar *contents = NULL;
	gboolean ret;
	GError *error = NULL;
	gchar **lines;
	gchar **sections;
	guint number_lines;
	guint number_sections;
	guint i;
	gboolean online = FALSE;

	g_return_val_if_fail (PK_IS_NETWORK_UNIX (network_unix), PK_NETWORK_ENUM_UNKNOWN);

	/* hack, because netlink is teh suck */
	ret = g_file_get_contents (PK_NETWORK_PROC_ROUTE, &contents, NULL, &error);
	if (!ret) {
		pk_warning ("could not open %s: %s", PK_NETWORK_PROC_ROUTE, error->message);
		g_error_free (error);
		/* no idea whatsoever! */
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* something insane */
	if (contents == NULL) {
		pk_warning ("insane contents of %s", PK_NETWORK_PROC_ROUTE);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* one line per interface */
	lines = g_strsplit (contents, "\n", 0);
	if (lines == NULL) {
		pk_warning ("unable to split %s", PK_NETWORK_PROC_ROUTE);
		return PK_NETWORK_ENUM_ONLINE;
	}

	number_lines = g_strv_length (lines);
	for (i=0; i<number_lines; i++) {
		/* empty line */
		if (pk_strzero (lines[i])) {
			continue;
		}

		/* tab delimited */
		sections = g_strsplit (lines[i], "\t", 0);
		if (sections == NULL) {
			pk_warning ("unable to split %s", PK_NETWORK_PROC_ROUTE);
			continue;
		}

		/* is header? */
		if (pk_strequal (sections[0], "Iface")) {
			continue;
		}

		/* is loopback? */
		if (pk_strequal (sections[0], "lo")) {
			continue;
		}

		/* is correct parameters? */
		number_sections = g_strv_length (sections);
		if (number_sections != 11) {
			pk_warning ("invalid line '%s' (%i)", lines[i], number_sections);
			continue;
		}

		/* is gateway nonzero? */
		if (!pk_strequal (sections[2], "00000000")) {
			pk_debug ("interface %s is valid", sections[0]);
			online = TRUE;
		}
		g_strfreev (sections);
	}
	g_strfreev (lines);

	if (online) {
		return PK_NETWORK_ENUM_ONLINE;
	}

	return PK_NETWORK_ENUM_OFFLINE;
}

/**
 * pk_network_unix_class_init:
 * @klass: The PkNetworkUnixClass
 **/
static void
pk_network_unix_class_init (PkNetworkUnixClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_network_unix_finalize;
	signals [PK_NETWORK_UNIX_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	g_type_class_add_private (klass, sizeof (PkNetworkUnixPrivate));
}

/**
 * pk_network_unix_init:
 * @network_unix: This class instance
 **/
static void
pk_network_unix_init (PkNetworkUnix *network_unix)
{
	network_unix->priv = PK_NETWORK_UNIX_GET_PRIVATE (network_unix);
}

/**
 * pk_network_unix_finalize:
 * @object: The object to finalize
 **/
static void
pk_network_unix_finalize (GObject *object)
{
	PkNetworkUnix *network_unix;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_UNIX (object));
	network_unix = PK_NETWORK_UNIX (object);

	g_return_if_fail (network_unix->priv != NULL);
	G_OBJECT_CLASS (pk_network_unix_parent_class)->finalize (object);
}

/**
 * pk_network_unix_new:
 *
 * Return value: a new PkNetworkUnix object.
 **/
PkNetworkUnix *
pk_network_unix_new (void)
{
	PkNetworkUnix *network_unix;
	network_unix = g_object_new (PK_TYPE_NETWORK_UNIX, NULL);
	return PK_NETWORK_UNIX (network_unix);
}

