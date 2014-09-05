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

PkBackend *backend = NULL;
GCancellable *cancellable = NULL;
static GMutex mutex;
static gboolean env_initialized = FALSE;

alpm_handle_t *alpm = NULL;
alpm_db_t *localdb = NULL;

gchar *xfercmd = NULL;
alpm_list_t *holdpkgs = NULL;
alpm_list_t *syncfirsts = NULL;

const gchar *
pk_backend_get_description (PkBackend *self)
{
	return "alpm";
}

const gchar *
pk_backend_get_author (PkBackend *self)
{
	return "Jonathan Conder <jonno.conder@gmail.com>";
}

static gboolean
pk_alpm_spawn (PkBackend *self, const gchar *command)
{
	int status;
	_cleanup_error_free_ GError *error = NULL;

	g_return_val_if_fail (command != NULL, FALSE);

	if (!g_spawn_command_line_sync (command, NULL, NULL, &status, &error)) {
		g_warning ("could not spawn command: %s", error->message);
		return FALSE;
	}

	if (WIFEXITED (status) == 0) {
		g_warning ("command did not execute correctly");
		return FALSE;
	}

	if (WEXITSTATUS (status) != EXIT_SUCCESS) {
		gint code = WEXITSTATUS (status);
		g_warning ("command returned error code %d", code);
		return FALSE;
	}

	return TRUE;
}

gint
pk_alpm_fetchcb (const gchar *url, const gchar *path, gint force)
{
	GRegex *xo, *xi;
	gint result = 0;
	_cleanup_free_ gchar *basename = NULL;
	_cleanup_free_ gchar *file = NULL;
	_cleanup_free_ gchar *finalcmd = NULL;
	_cleanup_free_ gchar *oldpwd = NULL;
	_cleanup_free_ gchar *part = NULL;
	_cleanup_free_ gchar *tempcmd = NULL;

	g_return_val_if_fail (url != NULL, -1);
	g_return_val_if_fail (path != NULL, -1);
	g_return_val_if_fail (xfercmd != NULL, -1);
	g_return_val_if_fail (backend != NULL, -1);

	oldpwd = g_get_current_dir ();
	if (g_chdir (path) < 0) {
		g_warning ("could not find or read directory '%s'", path);
		g_free (oldpwd);
		return -1;
	}

	xo = g_regex_new ("%o", 0, 0, NULL);
	xi = g_regex_new ("%u", 0, 0, NULL);

	basename = g_path_get_basename (url);
	file = g_strconcat (path, basename, NULL);
	part = g_strconcat (file, ".part", NULL);

	if (force != 0 && g_file_test (part, G_FILE_TEST_EXISTS))
		g_unlink (part);
	if (force != 0 && g_file_test (file, G_FILE_TEST_EXISTS))
		g_unlink (file);

	tempcmd = g_regex_replace_literal (xo, xfercmd, -1, 0, part, 0, NULL);
	if (tempcmd == NULL) {
		result = -1;
		goto out;
	}

	finalcmd = g_regex_replace_literal (xi, tempcmd, -1, 0, url, 0, NULL);
	if (finalcmd == NULL) {
		result = -1;
		goto out;
	}

	if (!pk_alpm_spawn (backend, finalcmd)) {
		result = -1;
		goto out;
	}
	if (g_strrstr (xfercmd, "%o") != NULL) {
		/* using .part filename */
		if (g_rename (part, file) < 0) {
			g_warning ("could not rename %s", part);
			result = -1;
			goto out;
		}
	}
out:
	g_regex_unref (xi);
	g_regex_unref (xo);

	g_chdir (oldpwd);

	return result;
}

static void
pk_alpm_logcb (alpm_loglevel_t level, const gchar *format, va_list args)
{
	_cleanup_free_ gchar *output = NULL;

	g_return_if_fail (backend != NULL);

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
pk_alpm_initialize (PkBackend *self, GError **error)
{
	alpm = pk_alpm_configure (PK_BACKEND_CONFIG_FILE, error);
	if (alpm == NULL) {
		g_prefix_error (error, "using %s: ", PK_BACKEND_CONFIG_FILE);
		return FALSE;
	}

	backend = self;
	alpm_option_set_logcb (alpm, pk_alpm_logcb);

	localdb = alpm_get_localdb (alpm);
	if (localdb == NULL) {
		alpm_errno_t errno = alpm_errno (alpm);
		g_set_error (error, PK_ALPM_ERROR, errno, "[%s]: %s", "local",
			     alpm_strerror (errno));
	}

	return TRUE;
}

static void
pk_alpm_destroy (void)
{
	if (alpm != NULL) {
		if (alpm_trans_get_flags (alpm) < 0)
			alpm_trans_release (alpm);
		alpm_release (alpm);
		alpm = NULL;
		backend = NULL;
	}

	FREELIST (syncfirsts);
	FREELIST (holdpkgs);
	g_free (xfercmd);
	xfercmd = NULL;
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *self)
{
	_cleanup_error_free_ GError *error = NULL;
	if (!pk_alpm_initialize (self, &error))
		g_error ("Failed to initialize alpm: %s", error->message);
	if (!pk_alpm_initialize_databases (&error))
		g_error ("Failed to initialize databases: %s", error->message);
	if (!pk_alpm_groups_initialize (self, &error))
		g_error ("Failed to initialize groups: %s", error->message);
}

void
pk_backend_destroy (PkBackend *self)
{
	pk_alpm_groups_destroy (self);
	pk_alpm_destroy_databases (self);
	pk_alpm_destroy ();
}

PkBitfield
pk_backend_get_filters (PkBackend *self)
{
	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
}

gchar **
pk_backend_get_mime_types (PkBackend *self)
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

	g_mutex_lock (&mutex);

	if (cancellable != NULL) {
		g_warning ("cancellable was not NULL");
		g_object_unref (cancellable);
	}
	cancellable = g_cancellable_new ();

	g_mutex_unlock (&mutex);

	pk_backend_job_set_allow_cancel (job, TRUE);

	pk_backend_job_set_status (job, status);
	pk_backend_job_thread_create (job, func, data, NULL);
}

gboolean
pk_alpm_is_backend_cancelled (PkBackendJob *job)
{
	gboolean cancelled;

	g_return_val_if_fail (cancellable != NULL, FALSE);

	g_mutex_lock (&mutex);

	cancelled = g_cancellable_is_cancelled (cancellable);

	g_mutex_unlock (&mutex);

	return cancelled;
}

gboolean
pk_alpm_finish (PkBackendJob *job, GError *error)
{
	gboolean cancelled = FALSE;

	pk_backend_job_set_allow_cancel (job, FALSE);

	g_mutex_lock (&mutex);

	if (cancellable != NULL) {
		cancelled = g_cancellable_is_cancelled (cancellable);
		g_object_unref (cancellable);
		cancellable = NULL;
	}

	g_mutex_unlock (&mutex);

	if (error != NULL)
		pk_alpm_error_emit (job, error);

	if (cancelled)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_CANCEL);

	pk_backend_job_finished (job);
	return (error == NULL);
}

void
pk_backend_start_job (PkBackend* self, PkBackendJob* job)
{
	if (!env_initialized) {
		pk_alpm_configure_environment (job);
		env_initialized = TRUE; //we only need to do it once
	}
}
