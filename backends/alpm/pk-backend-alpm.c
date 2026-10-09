/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
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

#include <config.h>

#include <glib/gstdio.h>
#include <glib/gthread.h>
#include <syslog.h>
#include <pk-backend.h>

#include "pk-backend-alpm.h"
#include "pk-alpm-config.h"
#include "pk-alpm-databases.h"
#include "pk-alpm-error.h"
#include "pk-alpm-groups.h"
#include "pk-alpm-transaction.h"
#include "pk-alpm-environment.h"

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
pk_alpm_logcb (void *ctx, alpm_loglevel_t level, const gchar *format, va_list args)
{
	g_autofree gchar *output = NULL;

	if (format == NULL || format[0] == '\0')
		return;
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
	output = g_strdup_vprintf (format, args);
#pragma GCC diagnostic pop

	/* report important output to PackageKit */
	switch (level) {
	case ALPM_LOG_DEBUG:
	case ALPM_LOG_FUNCTION:
		g_debug ("%s", output);
		break;
	case ALPM_LOG_WARNING:
		syslog (LOG_DAEMON | LOG_WARNING, "%s", output);
		pk_alpm_transaction_output (output);
		break;
	default:
		syslog (LOG_DAEMON | LOG_WARNING, "%s", output);
		break;
	}
}

static gboolean
pk_alpm_initialize (PkBackend *backend, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);

	priv->alpm = pk_alpm_configure (backend, PK_BACKEND_CONFIG_FILE, FALSE, error);
	if (priv->alpm == NULL) {
		g_prefix_error (error, "using %s: ", PK_BACKEND_CONFIG_FILE);
		return FALSE;
	}

	priv->alpm_check = NULL;
	alpm_option_set_logcb (priv->alpm, pk_alpm_logcb, NULL);

	priv->localdb = alpm_get_localdb (priv->alpm);
	if (priv->localdb == NULL) {
		alpm_errno_t alpm_err = alpm_errno (priv->alpm);
		g_set_error (error, PK_ALPM_ERROR, alpm_err, "[%s]: %s", "local",
			     alpm_strerror (alpm_err));
	}

	return TRUE;
}

static void
pk_backend_context_invalidate_cb (GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, PkBackend *backend)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	if (!pk_backend_is_transaction_inhibited (backend)) {
		priv->localdb_changed = TRUE;
	}
}

static void
pk_alpm_destroy_monitor (PkBackend *backend)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	g_object_unref (priv->monitor);
}

static gboolean
pk_alpm_initialize_monitor (PkBackend *backend, GError **error)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);

	g_autofree gchar * path = NULL;
	g_autoptr(GFile) directory = NULL;

	path = g_strconcat (alpm_option_get_dbpath (priv->alpm) ,"/local", NULL);
	directory = g_file_new_for_path (path);

	priv->monitor = g_file_monitor_directory (directory, 0, NULL, error);
	if (priv->monitor == NULL)
		return FALSE;

	g_signal_connect (priv->monitor, "changed",
		G_CALLBACK (pk_backend_context_invalidate_cb), backend);
	return TRUE;
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	PkBackendAlpmPrivate *priv;

	g_autoptr(GError) error = NULL;

	priv = g_new0 (PkBackendAlpmPrivate, 1);
	pk_backend_set_user_data (backend, priv);

	if (!pk_alpm_initialize (backend, &error))
		g_error ("Failed to initialize alpm: %s", error->message);
	if (!pk_alpm_initialize_databases (backend, &error))
		g_error ("Failed to initialize databases: %s", error->message);
	if (!pk_alpm_groups_initialize (backend, &error))
		g_error ("Failed to initialize groups: %s", error->message);

	if (!pk_alpm_initialize_monitor (backend, &error))
		g_error ("Failed to initialize monitor: %s", error->message);

	priv->localdb_changed = FALSE;
}

void
pk_backend_destroy (PkBackend *backend)
{
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	pk_alpm_groups_destroy (backend);
	pk_alpm_destroy_databases (backend);
	pk_alpm_destroy_monitor (backend);

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
	PkBackend *backend = pk_backend_job_get_backend (job);
	PkBackendAlpmPrivate *priv = pk_backend_get_user_data (backend);
	g_return_if_fail (func != NULL);

	if (priv->localdb_changed) {
		pk_backend_destroy (backend);
		pk_backend_initialize (NULL, backend);
		pk_backend_installed_db_changed (backend);
	}

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
	pk_alpm_environment_initialize (job);
}

PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	return pk_bitfield_from_enums (PK_ROLE_ENUM_GET_REPO_LIST,
				       PK_ROLE_ENUM_DEPENDS_ON,
				       PK_ROLE_ENUM_REQUIRED_BY,
				       PK_ROLE_ENUM_GET_UPDATE_DETAIL,
				       PK_ROLE_ENUM_GET_UPDATES,
				       PK_ROLE_ENUM_REFRESH_CACHE,
				       PK_ROLE_ENUM_REMOVE_PACKAGES,
				       PK_ROLE_ENUM_RESOLVE,
				       PK_ROLE_ENUM_GET_DETAILS,
				       PK_ROLE_ENUM_GET_FILES,
				       PK_ROLE_ENUM_INSTALL_FILES,
				       PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
				       PK_ROLE_ENUM_UPDATE_PACKAGES,
				       PK_ROLE_ENUM_INSTALL_PACKAGES,
				       PK_ROLE_ENUM_GET_PACKAGES,
				       PK_ROLE_ENUM_SEARCH_DETAILS,
				       PK_ROLE_ENUM_SEARCH_FILE,
				       PK_ROLE_ENUM_SEARCH_GROUP,
				       PK_ROLE_ENUM_SEARCH_NAME,
				       PK_ROLE_ENUM_WHAT_PROVIDES,
				       -1);
}

void
pk_backend_run_job (PkBackend *backend, PkBackendJob *job)
{
	PkRoleEnum role = pk_backend_job_get_role (job);

	switch (role) {
	case PK_ROLE_ENUM_GET_REPO_LIST:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_get_repo_list_thread, NULL);
		break;
	case PK_ROLE_ENUM_DEPENDS_ON:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_depends_on_thread, NULL);
		break;
	case PK_ROLE_ENUM_REQUIRED_BY:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_required_by_thread, NULL);
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_get_update_detail_thread, NULL);
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_get_updates_thread, NULL);
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_alpm_refresh_cache_thread, NULL);
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_alpm_remove_packages_thread, NULL);
		break;
	case PK_ROLE_ENUM_RESOLVE:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_resolve_thread, NULL);
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_get_details_thread, NULL);
		break;
	case PK_ROLE_ENUM_GET_FILES:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_get_files_thread, NULL);
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_alpm_install_files_thread, NULL);
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_alpm_download_packages_thread, NULL);
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_alpm_sync_thread, (void *) TRUE);
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		pk_alpm_run (job, PK_STATUS_ENUM_SETUP, pk_alpm_sync_thread, FALSE);
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
	case PK_ROLE_ENUM_SEARCH_DETAILS:
	case PK_ROLE_ENUM_SEARCH_FILE:
	case PK_ROLE_ENUM_SEARCH_GROUP:
	case PK_ROLE_ENUM_SEARCH_NAME:
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		pk_alpm_run (job, PK_STATUS_ENUM_QUERY, pk_alpm_search_thread, NULL);
		break;
	default:
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_NOT_SUPPORTED,
					   "role %s is not supported",
					   pk_role_enum_to_string (role));
		pk_backend_job_finished (job);
		break;
	}
}
