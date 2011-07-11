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

/* You need to compile this with the folowing commands:
 * {
libtool --mode=compile gcc -Wall -Werror -g -O `pkg-config --cflags --libs packagekit-plugin` -c pk-plugin-external.c
libtool --mode=link gcc -g -O -module -avoid-version -o libpk-plugin-external.la -rpath /home/hughsie/.root/lib/packagekit-plugins pk-plugin-external.lo
libtool --mode=install cp libpk-plugin-external.la /home/hughsie/.root/lib/packagekit-plugins/libpk-plugin-external.la
 *
 * or, if using automake:
 * {
plugindir = $(LIBDIR)/packagekit-plugins
plugin_LTLIBRARIES = libpk_plugin_external.la
libpk_plugin_external_la_SOURCES = pk-plugin-external.c
libpk_plugin_external_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_external_la_LDFLAGS = -module -avoid-version
libpk_plugin_external_la_CFLAGS = $(PK_PLUGIN_CFLAGS)
 * } 
 */

#define I_KNOW_THE_PACKAGEKIT_PLUGIN_API_IS_SUBJECT_TO_CHANGE
#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE

#include <stdlib.h>
#include <PackageKit/plugin/packagekit-plugin.h>

typedef struct {
	guint			 dummy;
} PluginPrivate;

static PluginPrivate *priv;

/**
 * pk_transaction_plugin_get_description:
 */
const gchar *
pk_transaction_plugin_get_description (void)
{
	return "An external plugin that compiles outside of PK";
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
}

/**
 * pk_transaction_plugin_destroy:
 */
void
pk_transaction_plugin_destroy (PkTransaction *transaction)
{
	g_free (priv);
}

/**
 * pk_transaction_plugin_run:
 */
void
pk_transaction_plugin_run (PkTransaction *transaction)
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
 * pk_transaction_plugin_started:
 */
void
pk_transaction_plugin_started (PkTransaction *transaction)
{
}

/**
 * pk_transaction_plugin_finished_start:
 */
void
pk_transaction_plugin_finished_start (PkTransaction *transaction)
{
}

/**
 * pk_transaction_plugin_finished_results:
 */
void
pk_transaction_plugin_finished_results (PkTransaction *transaction)
{
}

/**
 * pk_transaction_plugin_finished_end:
 */
void
pk_transaction_plugin_finished_end (PkTransaction *transaction)
{
}
