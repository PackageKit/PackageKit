/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Richard Hughes <richard@hughsie.com>
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

#include <config.h>
#include <gio/gio.h>
#include <pk-transaction.h>

typedef struct {
	guint		 dummy;
} PluginPrivate;

static PluginPrivate *priv;

/**
 * pk_transaction_plugin_get_description:
 */
const gchar *
pk_transaction_plugin_get_description (void)
{
	return "A dummy plugin that doesn't do anything";
}

/**
 * pk_transaction_plugin_initialize:
 */
void
pk_transaction_plugin_initialize (PkTransaction *transaction)
{
	/* create private area */
	priv = g_new0 (PluginPrivate, 1);
	priv->dummy = 999;

	g_debug ("plugin: initialize");
}

/**
 * pk_transaction_plugin_destroy:
 */
void
pk_transaction_plugin_destroy (PkTransaction *transaction)
{
	g_debug ("plugin: destroy");
	g_free (priv);
}

/**
 * pk_transaction_plugin_run:
 */
void
pk_transaction_plugin_run (PkTransaction *transaction)
{
	g_debug ("plugin: run");
}

/**
 * pk_transaction_plugin_finished_start:
 */
void
pk_transaction_plugin_finished_start (PkTransaction *transaction)
{
	g_debug ("plugin: finished-start");
}

/**
 * pk_transaction_plugin_finished_results:
 */
void
pk_transaction_plugin_finished_results (PkTransaction *transaction)
{
	g_debug ("plugin: finished-results");
}

/**
 * pk_transaction_plugin_finished_end:
 */
void
pk_transaction_plugin_finished_end (PkTransaction *transaction)
{
	g_debug ("plugin: finished-end");
}
