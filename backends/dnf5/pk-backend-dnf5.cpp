/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Neal Gompa <neal@gompa.dev>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "dnf5-backend-utils.hpp"
#include "dnf5-backend-thread.hpp"
#include <pk-common-private.h>
#include <pk-debug.h>

// Backend API Implementation

extern "C" {

const char *
pk_backend_get_description(PkBackend *backend)
{
	return "DNF5 package manager backend";
}

const char *
pk_backend_get_author(PkBackend *backend)
{
	return "Neal Gompa <neal@gompa.dev>";
}

gboolean
pk_backend_supports_parallelization(PkBackend *backend)
{
	return TRUE;
}

gchar **
pk_backend_get_mime_types(PkBackend *backend)
{
	const gchar *mime_types[] = {"application/x-rpm", NULL};
	return g_strdupv((gchar **) mime_types);
}

PkBitfield
pk_backend_get_roles(PkBackend *backend)
{
	return pk_bitfield_from_enums(
		PK_ROLE_ENUM_DEPENDS_ON,
		PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
		PK_ROLE_ENUM_GET_DETAILS,
		PK_ROLE_ENUM_GET_DETAILS_LOCAL,
		PK_ROLE_ENUM_GET_FILES,
		PK_ROLE_ENUM_GET_FILES_LOCAL,
		PK_ROLE_ENUM_GET_PACKAGES,
		PK_ROLE_ENUM_GET_REPO_LIST,
		PK_ROLE_ENUM_INSTALL_FILES,
		PK_ROLE_ENUM_INSTALL_PACKAGES,
		PK_ROLE_ENUM_REMOVE_PACKAGES,
		PK_ROLE_ENUM_UPDATE_PACKAGES,
		PK_ROLE_ENUM_REPAIR_SYSTEM,
		PK_ROLE_ENUM_UPGRADE_SYSTEM,
		PK_ROLE_ENUM_REPO_ENABLE,
		PK_ROLE_ENUM_REPO_REMOVE,
		PK_ROLE_ENUM_REPO_SET_DATA,
		PK_ROLE_ENUM_REQUIRED_BY,
		PK_ROLE_ENUM_RESOLVE,
		PK_ROLE_ENUM_REFRESH_CACHE,
		PK_ROLE_ENUM_GET_UPDATES,
		PK_ROLE_ENUM_GET_UPDATE_DETAIL,
		PK_ROLE_ENUM_WHAT_PROVIDES,
		PK_ROLE_ENUM_SEARCH_NAME,
		PK_ROLE_ENUM_SEARCH_DETAILS,
		PK_ROLE_ENUM_SEARCH_FILE,
		PK_ROLE_ENUM_CANCEL,
		-1);
}

static int
pk_backend_dnf5_inhibit_notify(PkBackend *backend)
{
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data(backend);
	gint64 current_time = g_get_monotonic_time();
	gint64 time_since_last_notification = current_time - priv->last_notification_timestamp;

	/* Inhibit notifications for 5 seconds to avoid processing our own RPM transactions */
	if (time_since_last_notification < 5 * G_USEC_PER_SEC) {
		g_debug("Ignoring signal: too soon after last notification (%" G_GINT64_FORMAT " µs)",
			time_since_last_notification);
		return 1;
	}
	return 0;
}

static void
pk_backend_context_invalidate_cb(PkBackend *backend, PkBackend *backend_data)
{
	g_return_if_fail(PK_IS_BACKEND(backend));

	g_debug("invalidating dnf5 base");

	if (pk_backend_dnf5_inhibit_notify(backend))
		return;

	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data(backend);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&priv->mutex);

	try {
		dnf5_setup_base(priv);
		priv->last_notification_timestamp = g_get_monotonic_time();
	} catch (const std::exception &e) {
		g_warning("Failed to invalidate dnf5 base: %s", e.what());
	}
}

void
pk_backend_initialize(GKeyFile *conf, PkBackend *backend)
{
	g_autofree gchar *release_ver = NULL;
	g_autoptr(GError) error = NULL;

	// use logging
	pk_debug_add_log_domain(G_LOG_DOMAIN);
	pk_debug_add_log_domain("DNF5");

	PkBackendDnf5Private *priv = g_new0(PkBackendDnf5Private, 1);

	g_debug("Using libdnf5 %i.%i.%i", LIBDNF5_VERSION_MAJOR, LIBDNF5_VERSION_MINOR, LIBDNF5_VERSION_PATCH);

	g_mutex_init(&priv->mutex);
	priv->conf = g_key_file_ref(conf);
	priv->last_notification_timestamp = 0;

	pk_backend_set_user_data(backend, priv);

	release_ver = pk_get_distro_version_id(&error);
	if (release_ver == NULL) {
		g_warning("Failed to parse os-release: %s", error->message);
	} else {
		/* clean up any cache directories left over from a distro upgrade */
		dnf5_remove_old_cache_directories(backend, release_ver);
	}

	try {
		dnf5_setup_base(priv, FALSE, FALSE, nullptr, pk_backend_is_online(backend));
		g_signal_connect(backend, "updates-changed", G_CALLBACK(pk_backend_context_invalidate_cb), backend);
	} catch (const std::exception &e) {
		g_warning("Init failed: %s", e.what());
	}
}

void
pk_backend_destroy(PkBackend *backend)
{
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data(backend);
	priv->base.reset();
	if (priv->conf != NULL)
		g_key_file_unref(priv->conf);
	g_mutex_clear(&priv->mutex);
	g_free(priv);
}

void
pk_backend_start_job(PkBackend *backend, PkBackendJob *job)
{
}

void
pk_backend_stop_job(PkBackend *backend, PkBackendJob *job)
{
}

void
pk_backend_run_job(PkBackend *backend, PkBackendJob *job)
{
	PkRoleEnum role = pk_backend_job_get_role(job);

	switch (role) {
	case PK_ROLE_ENUM_SEARCH_NAME:
	case PK_ROLE_ENUM_SEARCH_DETAILS:
	case PK_ROLE_ENUM_SEARCH_FILE:
	case PK_ROLE_ENUM_GET_PACKAGES:
	case PK_ROLE_ENUM_RESOLVE:
	case PK_ROLE_ENUM_GET_DETAILS:
	case PK_ROLE_ENUM_GET_FILES:
	case PK_ROLE_ENUM_GET_REPO_LIST:
	case PK_ROLE_ENUM_GET_UPDATES:
	case PK_ROLE_ENUM_WHAT_PROVIDES:
	case PK_ROLE_ENUM_DEPENDS_ON:
	case PK_ROLE_ENUM_REQUIRED_BY:
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
	case PK_ROLE_ENUM_GET_DETAILS_LOCAL:
	case PK_ROLE_ENUM_GET_FILES_LOCAL:
		pk_backend_job_thread_create(job, dnf5_query_thread, NULL, NULL);
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
	case PK_ROLE_ENUM_INSTALL_FILES:
	case PK_ROLE_ENUM_UPGRADE_SYSTEM:
	case PK_ROLE_ENUM_REPAIR_SYSTEM:
		pk_backend_job_thread_create(job, dnf5_transaction_thread, NULL, NULL);
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
	case PK_ROLE_ENUM_REPO_SET_DATA:
	case PK_ROLE_ENUM_REPO_REMOVE:
		pk_backend_job_thread_create(job, dnf5_repo_thread, NULL, NULL);
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE: {
		gboolean force;
		g_variant_get(pk_backend_job_get_parameters(job), "(b)", &force);
		pk_backend_job_set_status(job, PK_STATUS_ENUM_REFRESH_CACHE);
		PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data(backend);
		g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&priv->mutex);
		try {
			dnf5_refresh_cache(priv, force);
		} catch (const std::exception &e) {
			pk_backend_job_error_code(job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", e.what());
		}
		pk_backend_job_finished(job);
		break;
	}
	default:
		pk_backend_job_error_code(
			job,
			PK_ERROR_ENUM_NOT_SUPPORTED,
			"role %s is not supported",
			pk_role_enum_to_string(role));
		pk_backend_job_finished(job);
		break;
	}
}

void
pk_backend_cancel(PkBackend *backend, PkBackendJob *job)
{
}
}
