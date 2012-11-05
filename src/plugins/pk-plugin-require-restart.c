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
 * pkg_filter_func_cb:
 */
static gboolean
pkg_filter_func_cb (PkPackage *package, gpointer user_data)
{
	const gchar *name = pk_package_get_name (package);
	if (g_strcmp0 (name, "kernel") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-smp") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-xen-hypervisor") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-PAE") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-xen0") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-xenU") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-xen") == 0)
		return TRUE;
	if (g_strcmp0 (name, "kernel-xen-guest") == 0)
		return TRUE;
	if (g_strcmp0 (name, "glibc") == 0)
		return TRUE;
	if (g_strcmp0 (name, "dbus") == 0)
		return TRUE;
	return FALSE;
}

/**
 * pk_plugin_transaction_started:
 */
void
pk_plugin_transaction_started (PkPlugin *plugin,
			       PkTransaction *transaction)
{
	gchar **values;
	GPtrArray *array = NULL;
	guint i;
	PkPackage *pkg;
	PkPackageSack *sack = NULL;
	PkRoleEnum role;

	/* skip only-download */
	if (pk_bitfield_contain (pk_transaction_get_transaction_flags (transaction),
				 PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD)) {
		goto out;
	}

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_UPDATE_PACKAGES)
		goto out;

	/* get a sack of PkPackages */
	values = pk_transaction_get_package_ids (transaction);
	if (values == NULL)
		goto out;
	sack = pk_package_sack_new ();
	for (i = 0; values[i] != NULL; i++)
		pk_package_sack_add_package_by_id (sack, values[i], NULL);

	/* filter to interesting packages */
	pk_package_sack_remove_by_filter (sack, pkg_filter_func_cb, NULL);
	array = pk_package_sack_get_array (sack);
	for (i = 0; i < array->len; i++) {
		pkg = g_ptr_array_index (array, i);
		pk_backend_job_require_restart (plugin->job,
						PK_RESTART_ENUM_SYSTEM,
						pk_package_get_id (pkg));
	}
out:
	if (sack != NULL)
		g_object_unref (sack);
	if (array != NULL)
		g_ptr_array_unref (array);
}
