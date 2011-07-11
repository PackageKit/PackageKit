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
#include <pk-plugin.h>

struct PkPluginPrivate {
	guint			 dummy;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "A dummy plugin that doesn't do anything";
}

/**
 * pk_plugin_initialize:
 */
void
pk_plugin_initialize (PkPlugin *plugin)
{
	/* create private area */
	plugin->priv = PK_TRANSACTION_PLUGIN_GET_PRIVATE (PkPluginPrivate);
	plugin->priv->dummy = 999;
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	plugin->priv->dummy = 0;
}

/**
 * pk_plugin_transaction_run:
 */
void
pk_plugin_transaction_run (PkPlugin *plugin,
			   PkTransaction *transaction)
{
	gboolean ret;
	PkBackend *backend = NULL;
	PkConf *conf;
	PkRoleEnum role;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "UseDummy");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_REFRESH_CACHE)
		goto out;

	/* check we can do the action */
	backend = pk_transaction_get_backend (transaction);
	if (!pk_backend_is_implemented (backend,
	    PK_ROLE_ENUM_GET_PACKAGES)) {
		g_debug ("cannot get packages");
		goto out;
	}
out:
	return;
}

/**
 * pk_plugin_transaction_started:
 */
void
pk_plugin_transaction_started (PkPlugin *plugin,
			       PkTransaction *transaction)
{
}

/**
 * pk_plugin_transaction_finished_start:
 */
void
pk_plugin_transaction_finished_start (PkPlugin *plugin,
				      PkTransaction *transaction)
{
}

/**
 * pk_plugin_transaction_finished_results:
 */
void
pk_plugin_transaction_finished_results (PkPlugin *plugin,
					PkTransaction *transaction)
{
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
}
