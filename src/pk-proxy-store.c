/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#include "pk-proxy-store.h"
#include "pk-shared.h"

#define PK_PROXY_STORE_GROUP "Proxy"

static const gchar *pk_proxy_store_keys[] = { "http", "https", "ftp", "socks", "no_proxy", "pac" };

static gchar **
pk_proxy_settings_field (PkProxySettings *settings, guint i)
{
	/* the fields of PkProxySettings in the order of pk_proxy_store_keys */
	switch (i) {
	case 0:
		return &settings->http;
	case 1:
		return &settings->https;
	case 2:
		return &settings->ftp;
	case 3:
		return &settings->socks;
	case 4:
		return &settings->no_proxy;
	default:
		return &settings->pac;
	}
}

static gchar *
pk_proxy_strdup_nonempty (const gchar *str)
{
	if (str == NULL || str[0] == '\0')
		return NULL;
	return g_strdup (str);
}

PkProxySettings *
pk_proxy_settings_new (const gchar *http,
		       const gchar *https,
		       const gchar *ftp,
		       const gchar *socks,
		       const gchar *no_proxy,
		       const gchar *pac)
{
	PkProxySettings *settings = g_new0 (PkProxySettings, 1);
	settings->http = pk_proxy_strdup_nonempty (http);
	settings->https = pk_proxy_strdup_nonempty (https);
	settings->ftp = pk_proxy_strdup_nonempty (ftp);
	settings->socks = pk_proxy_strdup_nonempty (socks);
	settings->no_proxy = pk_proxy_strdup_nonempty (no_proxy);
	settings->pac = pk_proxy_strdup_nonempty (pac);
	return settings;
}

void
pk_proxy_settings_free (PkProxySettings *settings)
{
	if (settings == NULL)
		return;
	g_free (settings->http);
	g_free (settings->https);
	g_free (settings->ftp);
	g_free (settings->socks);
	g_free (settings->no_proxy);
	g_free (settings->pac);
	g_free (settings);
}

/**
 * pk_proxy_settings_is_empty:
 *
 * Returns: %TRUE if @settings is %NULL or sets no proxy at all
 */
gboolean
pk_proxy_settings_is_empty (const PkProxySettings *settings)
{
	if (settings == NULL)
		return TRUE;
	for (guint i = 0; i < G_N_ELEMENTS (pk_proxy_store_keys); i++) {
		const gchar *value = *pk_proxy_settings_field ((PkProxySettings *) settings, i);
		if (value != NULL && value[0] != '\0')
			return FALSE;
	}
	return TRUE;
}

/**
 * pk_proxy_settings_equal:
 *
 * Compares two settings, treating %NULL and an empty string as the same,
 * and %NULL settings as empty.
 */
gboolean
pk_proxy_settings_equal (const PkProxySettings *a, const PkProxySettings *b)
{
	if (pk_proxy_settings_is_empty (a) || pk_proxy_settings_is_empty (b))
		return pk_proxy_settings_is_empty (a) && pk_proxy_settings_is_empty (b);
	for (guint i = 0; i < G_N_ELEMENTS (pk_proxy_store_keys); i++) {
		const gchar *va = *pk_proxy_settings_field ((PkProxySettings *) a, i);
		const gchar *vb = *pk_proxy_settings_field ((PkProxySettings *) b, i);
		if (g_strcmp0 (va != NULL ? va : "", vb != NULL ? vb : "") != 0)
			return FALSE;
	}
	return TRUE;
}

/**
 * pk_proxy_store_get_path:
 *
 * Returns the path of the proxy file for @session, or for @uid if the caller
 * has no session.
 */
static gchar *
pk_proxy_store_get_path (GKeyFile *conf, guint uid, const gchar *session, GError **error)
{
	g_autofree gchar *dir = pk_util_get_runtime_dir (conf, error);
	g_autofree gchar *name = NULL;

	if (dir == NULL)
		return NULL;
	if (session != NULL) {
		/* the session is a D-Bus-style path, last element is the ID */
		g_autofree gchar *id = g_path_get_basename (session);
		name = g_strdup_printf ("proxy-%s", id);
	} else {
		name = g_strdup_printf ("proxy-uid-%u", uid);
	}
	return g_build_filename (dir, name, NULL);
}

/**
 * pk_proxy_store_load:
 * @conf: the daemon configuration
 * @uid: the user ID of the caller
 * @session: the session of the caller, or %NULL
 * @settings: (out): the stored settings, or %NULL if none are stored
 *
 * Returns: %FALSE on error; no stored settings is not an error
 */
gboolean
pk_proxy_store_load (GKeyFile *conf,
		     guint uid,
		     const gchar *session,
		     PkProxySettings **settings,
		     GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GKeyFile) key_file = g_key_file_new ();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PkProxySettings) tmp = NULL;

	g_return_val_if_fail (settings != NULL, FALSE);
	*settings = NULL;

	path = pk_proxy_store_get_path (conf, uid, session, error);
	if (path == NULL)
		return FALSE;
	if (!g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &error_local)) {
		if (g_error_matches (error_local, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			return TRUE;
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local), "%s: ", path);
		return FALSE;
	}

	tmp = g_new0 (PkProxySettings, 1);
	for (guint i = 0; i < G_N_ELEMENTS (pk_proxy_store_keys); i++) {
		*pk_proxy_settings_field (tmp, i) = g_key_file_get_string (key_file,
									   PK_PROXY_STORE_GROUP,
									   pk_proxy_store_keys[i],
									   NULL);
	}
	*settings = g_steal_pointer (&tmp);

	return TRUE;
}

/**
 * pk_proxy_store_save:
 * @conf: the daemon configuration
 * @uid: the user ID of the caller
 * @session: the session of the caller, or %NULL
 * @settings: the settings to store; %NULL or empty settings remove the file
 */
gboolean
pk_proxy_store_save (GKeyFile *conf,
		     guint uid,
		     const gchar *session,
		     const PkProxySettings *settings,
		     GError **error)
{
	g_autofree gchar *path = NULL;
	g_autoptr(GKeyFile) kf = NULL;

	path = pk_proxy_store_get_path (conf, uid, session, error);
	if (path == NULL)
		return FALSE;

	/* nothing set: do not keep a file around */
	if (pk_proxy_settings_is_empty (settings)) {
		if (g_unlink (path) < 0 && errno != ENOENT) {
			g_set_error (error,
				     G_IO_ERROR,
				     g_io_error_from_errno (errno),
				     "failed to remove %s: %s",
				     path,
				     g_strerror (errno));
			return FALSE;
		}
		return TRUE;
	}

	kf = g_key_file_new ();
	for (guint i = 0; i < G_N_ELEMENTS (pk_proxy_store_keys); i++) {
		const gchar *value = *pk_proxy_settings_field ((PkProxySettings *) settings, i);
		if (value != NULL && value[0] != '\0')
			g_key_file_set_string (kf,
					       PK_PROXY_STORE_GROUP,
					       pk_proxy_store_keys[i],
					       value);
	}
	return g_key_file_save_to_file (kf, path, error);
}
