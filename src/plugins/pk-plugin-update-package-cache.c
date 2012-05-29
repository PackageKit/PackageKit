/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Matthias Klumpp <matthias@tenstral.net>
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
#include <packagekit-glib2/pk-package.h>
#include <packagekit-glib2/pk-package-sack-sync.h>
#include <packagekit-glib2/pk-debug.h>

#include "pk-package-cache.h"

struct PkPluginPrivate {
	PkPackageSack		*sack;
	GMainLoop		*loop;
};

/**
 * pk_plugin_get_description:
 */
const gchar *
pk_plugin_get_description (void)
{
	return "Maintains a database of all packages for fast read-only access to package information";
}

/**
 * pk_plugin_initialize:
 */
void
pk_plugin_initialize (PkPlugin *plugin)
{
	/* create private area */
	plugin->priv = PK_TRANSACTION_PLUGIN_GET_PRIVATE (PkPluginPrivate);
	plugin->priv->loop = g_main_loop_new (NULL, FALSE);
	plugin->priv->sack = pk_package_sack_new ();

	/* use logging */
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	pk_debug_add_log_domain ("PkPkgCache");
}

/**
 * pk_plugin_destroy:
 */
void
pk_plugin_destroy (PkPlugin *plugin)
{
	g_main_loop_unref (plugin->priv->loop);
	g_object_unref (plugin->priv->sack);
}

/**
 * pk_plugin_package_cb:
 **/
static void
pk_plugin_package_cb (PkBackend *backend,
		      PkPackage *package,
		      PkPlugin *plugin)
{
	pk_package_sack_add_package (plugin->priv->sack, package);
}

/**
 * pk_plugin_details_cb:
 **/
static void
pk_plugin_details_cb (PkBackend *backend,
			PkDetails *item,
			PkPlugin *plugin)
{
	gchar *package_id;
	gchar *description;
	gchar *license;
	gchar *url;
	guint64 size;
	PkGroupEnum group;
	PkPackage *package;

	/* get data */
	g_object_get (item,
		      "package-id", &package_id,
		      "group", &group,
		      "description", &description,
		      "license", &license,
		      "url", &url,
		      "size", &size,
		      NULL);

	/* get package, and set data */
	package = pk_package_sack_find_by_id (plugin->priv->sack, package_id);
	if (package == NULL) {
		g_warning ("failed to find %s", package_id);
		goto out;
	}

	/* set data */
	g_object_set (package,
		      "license", license,
		      "group", group,
		      "description", description,
		      "url", url,
		      "size", size,
		      NULL);
	g_object_unref (package);

out:
	g_free (package_id);
	g_free (description);
	g_free (license);
	g_free (url);
}

/**
 * pk_plugin_finished_cb:
 **/
static void
pk_plugin_finished_cb (PkBackend *backend,
		       PkExitEnum exit_enum,
		       PkPlugin *plugin)
{
	if (!g_main_loop_is_running (plugin->priv->loop))
		return;
	g_main_loop_quit (plugin->priv->loop);
}

/**
 * pk_plugin_package_array_to_string:
 **/
static gchar *
pk_plugin_package_array_to_string (GPtrArray *array)
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
 * pk_plugin_package_array_to_string:
 **/
static void
pk_plugin_save_package_list (PkPlugin *plugin, GPtrArray *pkg_array)
{
	GError *error = NULL;
	gboolean ret;
	gchar *data = NULL;

	/* convert to a file and save the package list - we require this for backward-compatibility */
	data = pk_plugin_package_array_to_string (pkg_array);

	ret = g_file_set_contents (PK_SYSTEM_PACKAGE_LIST_FILENAME,
				data, -1, &error);
	if (!ret) {
		g_warning ("failed to save to file: %s",
			error->message);
		g_error_free (error);
	}
}

/**
 * pk_plugin_transaction_finished_end:
 */
void
pk_plugin_transaction_finished_end (PkPlugin *plugin,
				    PkTransaction *transaction)
{
	gboolean ret;
	GError *error = NULL;
	PkConf *conf;
	PkRoleEnum role;
	guint finished_sig_id = 0;
	guint package_sig_id = 0;
	PkBitfield backend_signals;

	PkPackageCache *cache = NULL;
	GPtrArray *pkg_array = NULL;
	gchar **package_ids;
	PkPackage *package;
	uint i;
	PkPluginPrivate *priv = plugin->priv;

	/* check the config file */
	conf = pk_transaction_get_conf (transaction);
	ret = pk_conf_get_bool (conf, "UpdatePackageCache");
	if (!ret)
		goto out;

	/* check the role */
	role = pk_transaction_get_role (transaction);
	if (role != PK_ROLE_ENUM_REFRESH_CACHE)
		goto out;

	/* check we can do the action */
	if (!pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_PACKAGES)) {
		g_debug ("cannot get packages");
		goto out;
	}

	/* don't forward unnecessary info the the transaction */
	backend_signals = PK_TRANSACTION_ALL_BACKEND_SIGNALS;
	pk_bitfield_remove (backend_signals, PK_BACKEND_SIGNAL_DETAILS);
	pk_bitfield_remove (backend_signals, PK_BACKEND_SIGNAL_PACKAGE);
	pk_bitfield_remove (backend_signals, PK_BACKEND_SIGNAL_FINISHED);
	pk_transaction_set_signals (transaction, backend_signals);

	/* connect to backend */
	finished_sig_id = g_signal_connect (plugin->backend, "finished",
					G_CALLBACK (pk_plugin_finished_cb), plugin);
	package_sig_id = g_signal_connect (plugin->backend, "package",
				       G_CALLBACK (pk_plugin_package_cb), plugin);
	pk_backend_set_vfunc (plugin->backend,
			      PK_BACKEND_SIGNAL_DETAILS,
			      (PkBackendVFunc) pk_plugin_details_cb,
			      plugin);

	g_debug ("plugin: rebuilding package cache");

	/* clear old package list */
	pk_package_sack_clear (priv->sack);

	/* update UI */
	pk_backend_set_status (plugin->backend,
			       PK_STATUS_ENUM_GENERATE_PACKAGE_LIST);
	pk_backend_set_percentage (plugin->backend, 101);

	/* get the new package list */
	pk_backend_reset (plugin->backend);
	pk_backend_get_packages (plugin->backend, PK_FILTER_ENUM_NONE);

	/* wait for finished */
	g_main_loop_run (priv->loop);

	/* update UI */
	pk_backend_set_percentage (plugin->backend, 90);

	/* fetch package details too, if possible */
	if (pk_backend_is_implemented (plugin->backend,
	    PK_ROLE_ENUM_GET_DETAILS)) {
		pk_backend_reset (plugin->backend);
		package_ids = pk_package_sack_get_ids (priv->sack);
		pk_backend_get_details (plugin->backend, package_ids);

		/* wait for finished */
		g_main_loop_run (priv->loop);

		g_strfreev (package_ids);
	} else {
		g_warning ("cannot get details");
	}

	/* open the package-cache */
	cache = pk_package_cache_new ();
	pk_package_cache_set_filename (cache, PK_SYSTEM_PACKAGE_CACHE_FILENAME, NULL);
	ret = pk_package_cache_open (cache, FALSE, &error);
	if (!ret) {
		g_warning ("%s: %s\n", "Failed to open cache", error->message);
		g_error_free (error);
		goto out;
	}

	pkg_array = pk_package_sack_get_array (priv->sack);

	/* clear the cache, so we can recreate it */
	g_clear_error (&error);
	pk_package_cache_clear (cache, &error);
	if (!ret) {
		g_warning ("%s: %s\n", "Failed to clear cache", error->message);
		g_error_free (error);
		goto out;
	}

	/* add packages to cache */
	g_clear_error (&error);
	for (i=0; i<pkg_array->len; i++) {
		package = g_ptr_array_index (pkg_array, i);
		ret = pk_package_cache_add_package (cache, package, &error);
		if (!ret) {
			g_warning ("%s: %s\n", "Couldn't update cache", error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* create & save legacy package-list */
	pk_plugin_save_package_list (plugin, pkg_array);

	/* update UI (finished) */
	pk_backend_set_percentage (plugin->backend, 100);
	pk_backend_set_status (plugin->backend, PK_STATUS_ENUM_FINISHED);

out:
	if (finished_sig_id != 0) {
		g_signal_handler_disconnect (plugin->backend, finished_sig_id);
		g_signal_handler_disconnect (plugin->backend, package_sig_id);
		pk_backend_set_vfunc (plugin->backend,
				      PK_BACKEND_SIGNAL_DETAILS,
				      NULL, NULL);
	}

	/* restore transaction signal connections */
	pk_transaction_set_signals (transaction, PK_TRANSACTION_ALL_BACKEND_SIGNALS);

	if (cache != NULL) {
		g_clear_error (&error);
		ret = pk_package_cache_close (cache, FALSE, &error);
		if (!ret) {
			g_warning ("%s: %s\n", "Failed to close cache", error->message);
			g_error_free (error);
		}
		g_object_unref (cache);
	}

	if (pkg_array != NULL)
		g_ptr_array_unref (pkg_array);
}
