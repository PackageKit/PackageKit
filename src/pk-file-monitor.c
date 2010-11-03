/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <signal.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "pk-conf.h"
#include "pk-file-monitor.h"

static void     pk_file_monitor_finalize	(GObject		*object);

#define PK_FILE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_FILE_MONITOR, PkFileMonitorPrivate))
#define PK_FILE_MONITOR_RATE_LIMIT	1000

struct PkFileMonitorPrivate
{
	GString			*stdout_buf;
	GFileMonitor		*monitor;
	GFile			*file;
};

enum {
	PK_FILE_MONITOR_CHANGED,
	PK_FILE_MONITOR_LAST_SIGNAL
};

static guint signals [PK_FILE_MONITOR_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkFileMonitor, pk_file_monitor, G_TYPE_OBJECT)

/**
 * pk_file_monitor_class_init:
 * @klass: The PkFileMonitorClass
 **/
static void
pk_file_monitor_class_init (PkFileMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_file_monitor_finalize;

	signals [PK_FILE_MONITOR_CHANGED] =
		g_signal_new ("file-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkFileMonitorPrivate));
}

/**
 * pk_file_monitor_monitor_changed:
 * @file_monitor: This class instance
 **/
static void
pk_file_monitor_monitor_changed (GFileMonitor *monitor, GFile *file, GFile *other_file,
			    GFileMonitorEvent event_type, PkFileMonitor *file_monitor)
{
	g_debug ("emit: file-changed");
	g_signal_emit (file_monitor, signals [PK_FILE_MONITOR_CHANGED], 0);
}

/**
 * pk_file_monitor_set_file:
 **/
gboolean
pk_file_monitor_set_file (PkFileMonitor	*file_monitor, const gchar *filename)
{
	GError *error = NULL;

	if (file_monitor->priv->file != NULL) {
		g_warning ("already set file monitor, so can't set %s", filename);
		return FALSE;
	}

	/* use a GFile */
	file_monitor->priv->file = g_file_new_for_path (filename);

	/* watch this */
	file_monitor->priv->monitor = g_file_monitor_file (file_monitor->priv->file, G_FILE_MONITOR_NONE, NULL, &error);
	if (file_monitor->priv->monitor == NULL) {
		g_warning ("failed to setup watch: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* we should get notified of changes */
	g_debug ("watching for changes: %s", filename);
	g_file_monitor_set_rate_limit (file_monitor->priv->monitor, PK_FILE_MONITOR_RATE_LIMIT);
	g_signal_connect (file_monitor->priv->monitor, "changed",
			  G_CALLBACK (pk_file_monitor_monitor_changed), file_monitor);
	return TRUE;
}

/**
 * pk_file_monitor_init:
 * @file_monitor: This class instance
 **/
static void
pk_file_monitor_init (PkFileMonitor *file_monitor)
{
	file_monitor->priv = PK_FILE_MONITOR_GET_PRIVATE (file_monitor);
	file_monitor->priv->file = NULL;
	file_monitor->priv->monitor = NULL;
}

/**
 * pk_file_monitor_finalize:
 * @object: The object to finalize
 **/
static void
pk_file_monitor_finalize (GObject *object)
{
	PkFileMonitor *file_monitor;

	g_return_if_fail (PK_IS_FILE_MONITOR (object));

	file_monitor = PK_FILE_MONITOR (object);
	g_return_if_fail (file_monitor->priv != NULL);

	/* we might not have called pk_file_monitor_set_file */
	if (file_monitor->priv->monitor != NULL) {
		g_file_monitor_cancel (file_monitor->priv->monitor);
		g_object_unref (file_monitor->priv->monitor);
	}
	if (file_monitor->priv->file != NULL) {
		g_object_unref (file_monitor->priv->file);
	}

	G_OBJECT_CLASS (pk_file_monitor_parent_class)->finalize (object);
}

/**
 * pk_file_monitor_new:
 *
 * Return value: a new PkFileMonitor object.
 **/
PkFileMonitor *
pk_file_monitor_new (void)
{
	PkFileMonitor *file_monitor;
	file_monitor = g_object_new (PK_TYPE_FILE_MONITOR, NULL);
	return PK_FILE_MONITOR (file_monitor);
}

