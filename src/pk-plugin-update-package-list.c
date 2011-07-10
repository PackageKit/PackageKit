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
#include <packagekit-glib2/pk-package.h>

typedef struct {
	GPtrArray		*list;
	GMainLoop		*loop;
} PluginPrivate;

static PluginPrivate *priv;

/**
 * pk_transaction_plugin_get_description:
 */
const gchar *
pk_transaction_plugin_get_description (void)
{
	return "Updates the package lists after refresh";
}

/**
 * pk_plugin_package_cb:
 **/
static void
pk_plugin_package_cb (PkBackend *backend,
		      PkPackage *package,
		      gpointer user_data)
{
	g_ptr_array_add (priv->list, g_object_ref (package));
}

/**
 * pk_plugin_finished_cb:
 **/
static void
pk_plugin_finished_cb (PkBackend *backend,
		       PkExitEnum exit_enum,
		       gpointer user_data)
{
	if (!g_main_loop_is_running (priv->loop))
		return;
	if (exit_enum != PK_EXIT_ENUM_SUCCESS) {
		g_warning ("%s failed with exit code: %s",
			     pk_role_enum_to_string (pk_backend_get_role (backend)),
			     pk_exit_enum_to_string (exit_enum));
	}
	g_main_loop_quit (priv->loop);
}

/**
 * pk_transaction_plugin_initialize:
 */
void
pk_transaction_plugin_initialize (PkTransaction *transaction)
{
	/* create private area */
	priv = g_new0 (PluginPrivate, 1);
	priv->loop = g_main_loop_new (NULL, FALSE);
	priv->list = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * pk_transaction_plugin_destroy:
 */
void
pk_transaction_plugin_destroy (PkTransaction *transaction)
{
	g_ptr_array_unref (priv->list);
	g_main_loop_unref (priv->loop);
	g_free (priv);
}

/**
 * pk_plugin_package_list_to_string:
 **/
static gchar *
pk_plugin_package_list_to_string (GPtrArray *array)
{
	guint i;
	PkPackage *package;
	GString *string;
	PkInfoEnum info;
	gchar *package_id;
	gchar *summary;

	string = g_string_new ("");
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_object_get (package,
			      "info", &info,
			      "package-id", &package_id,
			      "summary", &summary,
			      NULL);
		g_string_append_printf (string, "%s\t%s\t%s\n",
					pk_info_enum_to_string (info),
					package_id,
					summary);
		g_free (package_id);
		g_free (summary);
	}

	/* remove trailing newline */
	if (string->len != 0)
		g_string_set_size (string, string->len-1);
	return g_string_free (string, FALSE);
}

/**
 * pk_transaction_plugin_finished_end:
 */
void
pk_transaction_plugin_finished_end (PkTransaction *transaction)
{
	gboolean ret;
	gchar *data = NULL;
	GError *error = NULL;
	guint finished_id = 0;
	guint package_id = 0;
	PkBackend *backend = NULL;
	PkConf *conf;
	PkRoleEnum role;

	/* check the config file */
	conf = pk_transaction_priv_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "UpdatePackageList");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_priv_get_role (transaction);
	if (role != PK_ROLE_ENUM_REFRESH_CACHE)
		goto out;

	/* check we can do the action */
	backend = pk_transaction_priv_get_backend (transaction);
	if (!pk_backend_is_implemented (backend,
	    PK_ROLE_ENUM_GET_PACKAGES)) {
		g_debug ("cannot get packages");
		goto out;
	}

	/* connect to backend */
	backend = pk_transaction_priv_get_backend (transaction);
	finished_id = g_signal_connect (backend, "finished",
					G_CALLBACK (pk_plugin_finished_cb), NULL);
	package_id = g_signal_connect (backend, "package",
				       G_CALLBACK (pk_plugin_package_cb), NULL);

	g_debug ("plugin: updating package lists");

	/* clear old list */
	if (priv->list->len > 0)
		g_ptr_array_set_size (priv->list, 0);

	/* update UI */
	pk_backend_set_status (backend,
			       PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_backend_set_percentage (backend, 101);

	/* get the new package list */
	pk_backend_reset (backend);
	pk_backend_get_packages (backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (priv->loop);

	/* update UI */
	pk_backend_set_percentage (backend, 90);

	/* convert to a file */
	data = pk_plugin_package_list_to_string (priv->list);
	ret = g_file_set_contents (PK_SYSTEM_PACKAGE_LIST_FILENAME,
				   data, -1, &error);
	if (!ret) {
		g_warning ("failed to save to file: %s",
			   error->message);
		g_error_free (error);
	}

	/* update UI */
	pk_backend_set_percentage (backend, 100);
	pk_backend_set_status (backend, PK_STATUS_ENUM_FINISHED);
out:
	if (backend != NULL) {
		g_signal_handler_disconnect (backend, finished_id);
		g_signal_handler_disconnect (backend, package_id);
	}
	g_free (data);
}
