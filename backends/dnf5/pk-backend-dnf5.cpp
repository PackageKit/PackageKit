/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Neal Gompa <neal@gompa.dev>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
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
#include <packagekit-glib2/pk-common-private.h>
#include <packagekit-glib2/pk-debug.h>

// Backend API Implementation

extern "C" {

const char *
pk_backend_get_description (PkBackend *backend)
{
	return "DNF5 package manager backend";
}

const char *
pk_backend_get_author (PkBackend *backend)
{
	return "Neal Gompa <neal@gompa.dev>";
}

gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
	return TRUE;
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	const gchar *mime_types[] = { "application/x-rpm", NULL };
	return g_strdupv ((gchar **) mime_types);
}

PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	return pk_bitfield_from_enums (
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
pk_backend_dnf5_inhibit_notify (PkBackend *backend)
{
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	gint64 current_time = g_get_monotonic_time ();
	gint64 time_since_last_notification = current_time - priv->last_notification_timestamp;

	/* Inhibit notifications for 5 seconds to avoid processing our own RPM transactions */
	if (time_since_last_notification < 5 * G_USEC_PER_SEC) {
		g_debug ("Ignoring signal: too soon after last notification (%" G_GINT64_FORMAT " µs)",
			 time_since_last_notification);
		return 1;
	}
	return 0;
}

static void
pk_backend_context_invalidate_cb (PkBackend *backend, PkBackend *backend_data)
{
	g_return_if_fail (PK_IS_BACKEND (backend));

	g_debug ("invalidating dnf5 base");

	if (pk_backend_dnf5_inhibit_notify (backend)) return;

	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	dnf5_setup_base (priv);
	priv->last_notification_timestamp = g_get_monotonic_time ();
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	g_autofree gchar *release_ver = NULL;
	g_autoptr(GError) error = NULL;

	// use logging
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	pk_debug_add_log_domain ("DNF5");

	PkBackendDnf5Private *priv = g_new0 (PkBackendDnf5Private, 1);

	g_debug ("Using libdnf5 %i.%i.%i",
		 LIBDNF5_VERSION_MAJOR,
		 LIBDNF5_VERSION_MINOR,
		 LIBDNF5_VERSION_PATCH);

	g_mutex_init (&priv->mutex);
	priv->conf = g_key_file_ref (conf);
	priv->last_notification_timestamp = 0;

	pk_backend_set_user_data (backend, priv);

	release_ver = pk_get_distro_version_id (&error);
	if (release_ver == NULL) {
		g_warning ("Failed to parse os-release: %s", error->message);
	} else {
		/* clean up any cache directories left over from a distro upgrade */
		dnf5_remove_old_cache_directories (backend, release_ver);
	}

	try {
		dnf5_setup_base (priv);
		g_signal_connect (backend, "updates-changed",
				  G_CALLBACK (pk_backend_context_invalidate_cb), backend);
	} catch (const std::exception &e) {
		g_warning ("Init failed: %s", e.what());
	}
}

void
pk_backend_destroy (PkBackend *backend)
{
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	priv->base.reset();
	if (priv->conf != NULL)
		g_key_file_unref (priv->conf);
	g_mutex_clear (&priv->mutex);
	g_free (priv);
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
}

void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", filters, values);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", filters, values);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", filters, values);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	g_autoptr(GVariant) params = g_variant_new ("(t)", filters);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", filters, package_ids);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	g_autoptr(GVariant) params = g_variant_new ("(^as)", package_ids);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	g_autoptr(GVariant) params = g_variant_new ("(^as)", package_ids);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	g_autoptr(GVariant) params = g_variant_new ("(t)", filters);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	g_autoptr(GVariant) params = g_variant_new ("(t)", filters);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **search)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", filters, search);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^asb)", filters, package_ids, recursive);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^asb)", filters, package_ids, recursive);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	g_autoptr(GVariant) params = g_variant_new ("(^as)", package_ids);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
	g_autoptr(GVariant) params = g_variant_new ("(^as&s)", package_ids, directory);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_details_local (PkBackend *backend, PkBackendJob *job, gchar **files)
{
	g_autoptr(GVariant) params = g_variant_new ("(^as)", files);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_get_files_local (PkBackend *backend, PkBackendJob *job, gchar **files)
{
	g_autoptr(GVariant) params = g_variant_new ("(^as)", files);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_query_thread, NULL, NULL);
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", transaction_flags, package_ids);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_transaction_thread, NULL, NULL);
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^asbb)", transaction_flags, package_ids, allow_deps, autoremove);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_transaction_thread, NULL, NULL);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", transaction_flags, package_ids);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_transaction_thread, NULL, NULL);
}

void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
	g_autoptr(GVariant) params = g_variant_new ("(t^as)", transaction_flags, full_paths);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_transaction_thread, NULL, NULL);
}

void
pk_backend_upgrade_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, const gchar *distro_id, PkUpgradeKindEnum upgrade_kind)
{
	g_autoptr(GVariant) params = g_variant_new ("(t&su)", transaction_flags, distro_id, upgrade_kind);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_transaction_thread, NULL, NULL);
}

void
pk_backend_repair_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
	g_autoptr(GVariant) params = g_variant_new ("(t)", transaction_flags);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_transaction_thread, NULL, NULL);
}

void
pk_backend_repo_enable (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, gboolean enabled)
{
	g_autoptr(GVariant) params = g_variant_new ("(sb)", repo_id, enabled);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_repo_thread, NULL, NULL);
}

void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *repo_id, const gchar *parameter, const gchar *value)
{
	g_autoptr(GVariant) params = g_variant_new ("(sss)", repo_id, parameter, value);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_repo_thread, NULL, NULL);
}

void
pk_backend_repo_remove (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, const gchar *repo_id, gboolean autoremove)
{
	g_autoptr(GVariant) params = g_variant_new ("(t&sb)", transaction_flags, repo_id, autoremove);
	pk_backend_job_set_parameters (job, g_steal_pointer (&params));
	pk_backend_job_thread_create (job, dnf5_repo_thread, NULL, NULL);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	PkBackendDnf5Private *priv = (PkBackendDnf5Private *) pk_backend_get_user_data (backend);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	try {
		dnf5_refresh_cache (priv, force);
	} catch (const std::exception &e) {
		pk_backend_job_error_code (job, PK_ERROR_ENUM_INTERNAL_ERROR, "%s", e.what());
	}
	pk_backend_job_finished (job);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
}

}
