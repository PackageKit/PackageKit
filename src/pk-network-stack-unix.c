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

#include "pk-network-stack-unix.h"
#include "pk-marshal.h"
#include "pk-conf.h"

struct PkNetworkStackUnixPrivate
{
	PkConf			*conf;
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
	gchar **sections = NULL;
	gboolean online = FALSE;
	guint number_sections;

	/* empty line */
	if (line == NULL || line[0] == '\0')
		goto out;

	/* tab delimited */
	sections = g_strsplit (line, "\t", 0);
	if (sections == NULL) {
		g_warning ("unable to split %s", PK_NETWORK_PROC_ROUTE);
		goto out;
	}

	/* is header? */
	if (g_strcmp0 (sections[0], "Iface") == 0)
		goto out;

	/* is loopback? */
	if (g_strcmp0 (sections[0], "lo") == 0)
		goto out;

	/* is correct parameters? */
	number_sections = g_strv_length (sections);
	if (number_sections != 11) {
		g_warning ("invalid line '%s' (%i)", line, number_sections);
		goto out;
	}

	/* is destination zero (default route)? */
	if (g_strcmp0 (sections[1], "00000000") == 0) {
		g_debug ("destination %s is valid", sections[0]);
		online = TRUE;
		goto out;
	}

	/* is gateway nonzero? */
	if (g_strcmp0 (sections[2], "00000000") != 0) {
		g_debug ("interface %s is valid", sections[0]);
		online = TRUE;
		goto out;
	}
out:
	g_strfreev (sections);
	return online;
}

/**
 * pk_network_stack_unix_get_state:
 **/
static PkNetworkEnum
pk_network_stack_unix_get_state (PkNetworkStack *nstack)
{
	gchar *contents = NULL;
	gboolean ret;
	GError *error = NULL;
	gchar **lines = NULL;
	guint number_lines;
	guint i;
	gboolean online = FALSE;
	PkNetworkEnum state = PK_NETWORK_ENUM_ONLINE;

	/* no warning if the file is missing, like if no /proc */
	if (!g_file_test (PK_NETWORK_PROC_ROUTE, G_FILE_TEST_EXISTS)) {
		goto out;
	}

	/* hack, because netlink is teh suck */
	ret = g_file_get_contents (PK_NETWORK_PROC_ROUTE, &contents, NULL, &error);
	if (!ret) {
		g_warning ("could not open %s: %s", PK_NETWORK_PROC_ROUTE, error->message);
		g_error_free (error);
		/* no idea whatsoever! */
		goto out;
	}

	/* something insane */
	if (contents == NULL) {
		g_warning ("insane contents of %s", PK_NETWORK_PROC_ROUTE);
		goto out;
	}

	/* one line per interface */
	lines = g_strsplit (contents, "\n", 0);
	if (lines == NULL) {
		g_warning ("unable to split %s", PK_NETWORK_PROC_ROUTE);
		goto out;
	}

	number_lines = g_strv_length (lines);
	for (i=0; i<number_lines; i++) {

		/* is valid interface */
		ret = pk_network_stack_unix_is_valid (lines[i]);
		if (ret)
			online = TRUE;
	}

	if (!online)
		state = PK_NETWORK_ENUM_OFFLINE;
out:
	g_free (contents);
	g_strfreev (lines);
	return state;
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
	GError *error = NULL;
	GFile *file;

	nstack_unix->priv = PK_NETWORK_STACK_UNIX_GET_PRIVATE (nstack_unix);
	nstack_unix->priv->state_old = PK_NETWORK_ENUM_UNKNOWN;
	nstack_unix->priv->conf = pk_conf_new ();

	/* do we use this code? */
	nstack_unix->priv->is_enabled = pk_conf_get_bool (nstack_unix->priv->conf,
							  "UseNetworkHeuristic");

	/* monitor the route file for changes */
	file = g_file_new_for_path (PK_NETWORK_PROC_ROUTE);
	nstack_unix->priv->monitor = g_file_monitor_file (file,
							  G_FILE_MONITOR_NONE,
							  NULL,
							  &error);
	if (nstack_unix->priv->monitor == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   PK_NETWORK_PROC_ROUTE,
			   error->message);
		g_error_free (error);
	} else {
		g_signal_connect (nstack_unix->priv->monitor, "changed",
				  G_CALLBACK (pk_network_stack_unix_file_monitor_changed_cb), nstack_unix);
	}
	g_object_unref (file);
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

	g_object_unref (nstack_unix->priv->conf);
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

