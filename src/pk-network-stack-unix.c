/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "pk-cleanup.h"
#include "pk-network-stack-unix.h"

struct PkNetworkStackUnixPrivate
{
	PkNetworkEnum		 state_old;
	GFileMonitor		*monitor;
	gboolean		 is_enabled;
};

G_DEFINE_TYPE (PkNetworkStackUnix, pk_network_stack_unix, PK_TYPE_NETWORK_STACK)
#define PK_NETWORK_STACK_UNIX_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_NETWORK_STACK_UNIX, PkNetworkStackUnixPrivate))

#define PK_NETWORK_PROC_ROUTE	"/proc/net/route"

/**
 * pk_network_stack_unix_is_valid:
 **/
static gboolean
pk_network_stack_unix_is_valid (const gchar *line)
{
	guint number_sections;
	_cleanup_strv_free_ gchar **sections = NULL;

	/* empty line */
	if (line == NULL || line[0] == '\0')
		return FALSE;

	/* tab delimited */
	sections = g_strsplit (line, "\t", 0);
	if (sections == NULL) {
		g_warning ("unable to split %s", PK_NETWORK_PROC_ROUTE);
		return FALSE;
	}

	/* is header? */
	if (g_strcmp0 (sections[0], "Iface") == 0)
		return FALSE;

	/* is loopback? */
	if (g_strcmp0 (sections[0], "lo") == 0)
		return FALSE;

	/* is correct parameters? */
	number_sections = g_strv_length (sections);
	if (number_sections != 11) {
		g_warning ("invalid line '%s' (%i)", line, number_sections);
		return FALSE;
	}

	/* is destination zero (default route)? */
	if (g_strcmp0 (sections[1], "00000000") == 0) {
		g_debug ("destination %s is valid", sections[0]);
		return TRUE;
	}

	/* is gateway nonzero? */
	if (g_strcmp0 (sections[2], "00000000") != 0) {
		g_debug ("interface %s is valid", sections[0]);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_network_stack_unix_get_state:
 **/
static PkNetworkEnum
pk_network_stack_unix_get_state (PkNetworkStack *nstack)
{
	gboolean ret;
	guint i;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *contents = NULL;
	_cleanup_strv_free_ gchar **lines = NULL;

	/* no warning if the file is missing, like if no /proc */
	if (!g_file_test (PK_NETWORK_PROC_ROUTE, G_FILE_TEST_EXISTS))
		return PK_NETWORK_ENUM_ONLINE;

	/* hack, because netlink is teh suck */
	ret = g_file_get_contents (PK_NETWORK_PROC_ROUTE, &contents, NULL, &error);
	if (!ret) {
		g_warning ("could not open %s: %s", PK_NETWORK_PROC_ROUTE, error->message);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* something insane */
	if (contents == NULL) {
		g_warning ("insane contents of %s", PK_NETWORK_PROC_ROUTE);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* one line per interface */
	lines = g_strsplit (contents, "\n", 0);
	if (lines == NULL) {
		g_warning ("unable to split %s", PK_NETWORK_PROC_ROUTE);
		return PK_NETWORK_ENUM_ONLINE;
	}

	/* is valid interface */
	for (i = 0; lines[i] != NULL; i++) {
		if (pk_network_stack_unix_is_valid (lines[i]))
			return PK_NETWORK_ENUM_ONLINE;
	}
	return PK_NETWORK_ENUM_OFFLINE;
}

/**
 * pk_network_stack_unix_file_monitor_changed_cb:
 **/
static void
pk_network_stack_unix_file_monitor_changed_cb (GFileMonitor *monitor,
					       GFile *file,
					       GFile *other_file,
					       GFileMonitorEvent event_type,
					       PkNetworkStackUnix *nstack_unix)
{
	PkNetworkEnum state;

	g_return_if_fail (PK_IS_NETWORK_STACK_UNIX (nstack_unix));

	/* do not use */
	if (!nstack_unix->priv->is_enabled) {
		g_debug ("not enabled, so ignoring");
		return;
	}

	/* same state? */
	state = pk_network_stack_unix_get_state (PK_NETWORK_STACK (nstack_unix));
	if (state == nstack_unix->priv->state_old) {
		g_debug ("same state");
		return;
	}

	/* new state */
	nstack_unix->priv->state_old = state;
	g_debug ("emitting network-state-changed: %s", pk_network_enum_to_string (state));
	g_signal_emit_by_name (PK_NETWORK_STACK (nstack_unix), "state-changed", state);
}

/**
 * pk_network_stack_unix_is_enabled:
 *
 * Return %TRUE on success, %FALSE if we failed to is_enabled or no data
 **/
static gboolean
pk_network_stack_unix_is_enabled (PkNetworkStack *nstack)
{
	PkNetworkStackUnix *nstack_unix = PK_NETWORK_STACK_UNIX (nstack);
	return nstack_unix->priv->is_enabled;
}

/**
 * pk_network_stack_unix_init:
 **/
static void
pk_network_stack_unix_init (PkNetworkStackUnix *nstack_unix)
{
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GFile *file = NULL;

	nstack_unix->priv = PK_NETWORK_STACK_UNIX_GET_PRIVATE (nstack_unix);
	nstack_unix->priv->state_old = PK_NETWORK_ENUM_UNKNOWN;
	nstack_unix->priv->is_enabled = TRUE;

	/* monitor the route file for changes */
	file = g_file_new_for_path (PK_NETWORK_PROC_ROUTE);
	nstack_unix->priv->monitor = g_file_monitor_file (file,
							  G_FILE_MONITOR_NONE,
							  NULL,
							  &error);
	if (nstack_unix->priv->monitor == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   PK_NETWORK_PROC_ROUTE, error->message);
	} else {
		g_signal_connect (nstack_unix->priv->monitor, "changed",
				  G_CALLBACK (pk_network_stack_unix_file_monitor_changed_cb), nstack_unix);
	}
}

/**
 * pk_network_stack_unix_finalize:
 **/
static void
pk_network_stack_unix_finalize (GObject *object)
{
	PkNetworkStackUnix *nstack_unix;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_NETWORK_STACK_UNIX (object));

	nstack_unix = PK_NETWORK_STACK_UNIX (object);
	g_return_if_fail (nstack_unix->priv != NULL);

	g_object_unref (nstack_unix->priv->monitor);

	G_OBJECT_CLASS (pk_network_stack_unix_parent_class)->finalize (object);
}

/**
 * pk_network_stack_unix_class_init:
 **/
static void
pk_network_stack_unix_class_init (PkNetworkStackUnixClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	PkNetworkStackClass *nstack_class = PK_NETWORK_STACK_CLASS (klass);

	object_class->finalize = pk_network_stack_unix_finalize;
	nstack_class->get_state = pk_network_stack_unix_get_state;
	nstack_class->is_enabled = pk_network_stack_unix_is_enabled;

	g_type_class_add_private (klass, sizeof (PkNetworkStackUnixPrivate));
}

/**
 * pk_network_stack_unix_new:
 **/
PkNetworkStackUnix *
pk_network_stack_unix_new (void)
{
	return g_object_new (PK_TYPE_NETWORK_STACK_UNIX, NULL);
}

