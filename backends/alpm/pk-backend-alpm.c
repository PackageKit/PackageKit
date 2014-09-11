/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
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
#include <locale.h>
#include <glib/gstdio.h>
#include <glib/gthread.h>
#include <sys/utsname.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>

#include "pk-backend-alpm.h"
#include "pk-alpm-config.h"
#include "pk-alpm-databases.h"
#include "pk-alpm-error.h"
#include "pk-alpm-groups.h"
#include "pk-alpm-transaction.h"

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	return "alpm";
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
	return "Aleix Pol i Gonzàlez <aleixpol@kde.org>, "
	       "Fabien Bourigault <bourigaultfabien@gmail.com>, "
	       "Jonathan Conder <jonno.conder@gmail.com>";
}

static void
pk_alpm_logcb (alpm_loglevel_t level, const gchar *format, va_list args)
{
	_cleanup_free_ gchar *output = NULL;

	if (format == NULL || format[0] == '\0')
		return;
	output = g_strdup_vprintf (format, args);

	/* report important output to PackageKit */
	switch (level) {
	case ALPM_LOG_DEBUG:
	case ALPM_LOG_FUNCTION:
		g_debug ("%s", output);
		break;
	case ALPM_LOG_WARNING:
		g_warning ("%s", output);
		pk_alpm_transaction_output (output);
		break;
	default:
		g_warning ("%s", output);
		break;
	}
}

static void
pk_alpm_configure_environment (PkBackendJob *job)
{
	struct utsname un;
	gchar *value;

	/* PATH might have been nuked by D-Bus */
	g_setenv ("PATH", PK_BACKEND_DEFAULT_PATH, FALSE);

	uname (&un);
	value = g_strdup_printf ("%s/%s (%s %s) libalpm/%s", PACKAGE_TARNAME,
				 PACKAGE_VERSION, un.sysname, un.machine,
				 alpm_version ());
	g_setenv ("HTTP_USER_AGENT", value, FALSE);
	g_free (value);

	value = pk_backend_job_get_locale (job);
	if (value != NULL) {
		setlocale (LC_ALL, value);
		g_free (value);
	}

	value = pk_backend_job_get_proxy_http (job);
	if (value != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_spawn_convert_uri (value);
		g_setenv ("http_proxy", uri, TRUE);
		g_free (value);
	}

	value = pk_backend_job_get_proxy_https (job);
	if (value != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_spawn_convert_uri (value);
		g_setenv ("https_proxy", uri, TRUE);
		g_free (value);
	}

	value = pk_backend_job_get_proxy_ftp (job);
	if (value != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_spawn_convert_uri (value);
		g_setenv ("ftp_proxy", uri, TRUE);
		g_free (value);
	}

	value = pk_backend_job_get_proxy_socks (job);
	if (value != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_spawn_convert_uri (value);
		g_setenv ("socks_proxy", uri, TRUE);
		g_free (value);
	}

	value = pk_backend_job_get_no_proxy (job);
	if (value != NULL) {
		g_setenv ("no_proxy", value, TRUE);
		g_free (value);
	}

	value = pk_backend_job_get_pac (job);
	if (value != NULL) {
		_cleanup_free_ gchar *uri = pk_backend_spawn_convert_uri (value);
		g_setenv ("pac", uri, TRUE);
		g_free (value);
	}
}

static gboolean
pk_alpm_initialize (PkBackend *backend, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);

	priv->alpm = pk_alpm_configure (backend, PK_BACKEND_CONFIG_FILE, error);
	if (priv->alpm == NULL) {
		g_prefix_error (error, "using %s: ", PK_BACKEND_CONFIG_FILE);
		return FALSE;
	}

	alpm_option_set_logcb (priv->alpm, pk_alpm_logcb);

	priv->localdb = alpm_get_localdb (priv->alpm);
	if (priv->localdb == NULL) {
		alpm_errno_t errno = alpm_errno (priv->alpm);
		g_set_error (error, PK_ALPM_ERROR, errno, "[%s]: %s", "local",
			     alpm_strerror (errno));
	}

	return TRUE;
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	PkBackendAlpmPrivate *priv;

	_cleanup_error_free_ GError *error = NULL;

	priv = g_new0 (PkBackendAlpmPrivate, 1);
	pk_backend_set_user_data (backend, priv);

	if (!pk_alpm_initialize (backend, &error))
		g_error ("Failed to initialize alpm: %s", error->message);
	if (!pk_alpm_initialize_databases (backend, &error))
		g_error ("Failed to initialize databases: %s", error->message);
	if (!pk_alpm_groups_initialize (backend, &error))
		g_error ("Failed to initialize groups: %s", error->message);
}

void
pk_backend_destroy (PkBackend *backend)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	pk_alpm_groups_destroy (backend);
	pk_alpm_destroy_databases (backend);

	if (priv->alpm != NULL) {
		if (alpm_trans_get_flags (priv->alpm) < 0)
			alpm_trans_release (priv->alpm);
		alpm_release (priv->alpm);
	}

	FREELIST (priv->syncfirsts);
	FREELIST (priv->holdpkgs);
	g_free (priv);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	/* packages currently use .pkg.tar.gz and .pkg.tar.xz */
	const gchar *mime_types[] = {
				"application/x-compressed-tar",
				"application/x-xz-compressed-tar",
				NULL };
	return g_strdupv ((gchar **) mime_types);
}

void
pk_alpm_run (PkBackendJob *job, PkStatusEnum status, PkBackendJobThreadFunc func, gpointer data)
{
	g_return_if_fail (func != NULL);

	pk_backend_job_set_allow_cancel (job, TRUE);
	pk_backend_job_set_status (job, status);
	pk_backend_job_thread_create (job, func, data, NULL);
}

gboolean
pk_alpm_finish (PkBackendJob *job, GError *error)
{
	if (error != NULL)
		pk_alpm_error_emit (job, error);
	return (error == NULL);
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	if (!priv->env_initialized) {
		pk_alpm_configure_environment (job);
		priv->env_initialized = TRUE; //we only need to do it once
	}
}
