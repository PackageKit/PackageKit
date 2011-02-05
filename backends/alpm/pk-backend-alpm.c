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
#include <glib/gstdio.h>
#include <sys/utsname.h>

#include "pk-backend-alpm.h"
#include "pk-backend-databases.h"
#include "pk-backend-error.h"
#include "pk-backend-groups.h"

PkBackend *backend = NULL;
GCancellable *cancellable = NULL;
static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

gchar *xfercmd = NULL;
alpm_list_t *holdpkgs = NULL;
alpm_list_t *syncfirsts = NULL;

gchar *
pk_backend_get_description (PkBackend *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return g_strdup ("alpm");
}

gchar *
pk_backend_get_author (PkBackend *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return g_strdup ("Jonathan Conder <jonno.conder@gmail.com>");
}

static gboolean
pk_backend_spawn (PkBackend *self, const gchar *command)
{
	int status;
	GError *error = NULL;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (command != NULL, FALSE);

	if (!g_spawn_command_line_sync (command, NULL, NULL, &status, &error)) {
		g_warning ("could not spawn command: %s", error->message);
		g_error_free (error);
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
pk_backend_fetchcb (const gchar *url, const gchar *path, gint force)
{
	GRegex *xo, *xi;
	gchar *oldpwd, *basename, *file, *part;
	gchar *tempcmd = NULL, *finalcmd = NULL;
	gint result = 0;

	g_return_val_if_fail (url != NULL, -1);
	g_return_val_if_fail (path != NULL, -1);
	g_return_val_if_fail (xfercmd != NULL, -1);
	g_return_val_if_fail (backend != NULL, -1);

	oldpwd = g_get_current_dir ();
	if (g_chdir (path) < 0) {
		g_warning ("could not find or read directory %s", path);
		g_free (oldpwd);
		return -1;
	}

	xo = g_regex_new ("%o", 0, 0, NULL);
	xi = g_regex_new ("%u", 0, 0, NULL);

	basename = g_path_get_basename (url);
	file = g_strconcat (path, basename, NULL);
	part = g_strconcat (file, ".part", NULL);

	if (force != 0 && g_file_test (part, G_FILE_TEST_EXISTS)) {
		g_unlink (part);
	}
	if (force != 0 && g_file_test (file, G_FILE_TEST_EXISTS)) {
		g_unlink (file);
	}

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

	if (!pk_backend_spawn (backend, finalcmd)) {
		result = -1;
		goto out;
	} else if (g_strrstr (xfercmd, "%o") != NULL) {
		/* using .part filename */
		if (g_rename (part, file) < 0) {
			g_warning ("could not rename %s", part);
			result = -1;
			goto out;
		}
	}

out:
	g_free (finalcmd);
	g_free (tempcmd);

	g_free (part);
	g_free (file);
	g_free (basename);

	g_regex_unref (xi);
	g_regex_unref (xo);

	g_chdir (oldpwd);
	g_free (oldpwd);

	return result;
}

static void
pk_backend_logcb (pmloglevel_t level, gchar *format, va_list args)
{
	gchar *output;

	g_return_if_fail (backend != NULL);

	if (format != NULL && format[0] != '\0') {
		output = g_strdup_vprintf (format, args);
	} else {
		return;
	}

	/* report important output to PackageKit */
	switch (level) {
		case PM_LOG_DEBUG:
		case PM_LOG_FUNCTION:
			g_debug ("%s", output);
			break;

		case PM_LOG_WARNING:
			g_warning ("%s", output);
			pk_backend_output (backend, output);
			break;

		default:
			g_warning ("%s", output);
			break;
	}

	g_free (output);
}

static gboolean
pk_backend_initialize_alpm (PkBackend *self, GError **error)
{
	struct utsname un;
	gchar *user_agent;

	g_return_val_if_fail (self != NULL, FALSE);

	/* PATH might have been nuked by D-Bus */
	g_setenv ("PATH", PK_BACKEND_DEFAULT_PATH, FALSE);

	uname (&un);
	user_agent = g_strdup_printf ("%s/%s (%s %s) libalpm/%s",
				      PACKAGE_TARNAME, PACKAGE_VERSION,
				      un.sysname, un.machine, alpm_version ());
	g_setenv ("HTTP_USER_AGENT", user_agent, FALSE);
	g_free (user_agent);

	g_debug ("initializing");
	if (alpm_initialize () < 0) {
		g_set_error_literal (error, ALPM_ERROR, pm_errno,
				     alpm_strerrorlast ());
		return FALSE;
	}

	backend = self;

	/* set some sane defaults */
	alpm_option_set_logcb (pk_backend_logcb);
	alpm_option_set_root (PK_BACKEND_DEFAULT_ROOT);
	alpm_option_set_dbpath (PK_BACKEND_DEFAULT_DBPATH);
	alpm_option_set_logfile (PK_BACKEND_DEFAULT_LOGFILE);

	return TRUE;
}

static void
pk_backend_destroy_alpm (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	if (backend != NULL) {
		if (alpm_trans_get_flags () != -1) {
			alpm_trans_release ();
		}
		alpm_release ();
		backend = NULL;
	}

	FREELIST (syncfirsts);
	FREELIST (holdpkgs);
	g_free (xfercmd);
}

void
pk_backend_initialize (PkBackend *self)
{
	GError *error = NULL;

	g_return_if_fail (self != NULL);

	if (!pk_backend_initialize_alpm (self, &error) ||
	    !pk_backend_initialize_databases (self, &error) ||
	    !pk_backend_initialize_groups (self, &error)) {
		g_error ("%s", error->message);
		g_error_free (error);
	}
}

void
pk_backend_destroy (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	pk_backend_destroy_groups (self);
	pk_backend_destroy_databases (self);
	pk_backend_destroy_alpm (self);
}

PkBitfield
pk_backend_get_filters (PkBackend *self)
{
	g_return_val_if_fail (self != NULL, 0);

	return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED, -1);
}

gchar *
pk_backend_get_mime_types (PkBackend *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	/* packages currently use .pkg.tar.gz and .pkg.tar.xz */
	return g_strdup ("application/x-compressed-tar;"
			 "application/x-xz-compressed-tar");
}

void
pk_backend_run (PkBackend *self, PkStatusEnum status, PkBackendThreadFunc func)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (func != NULL);

	g_static_mutex_lock (&mutex);

	if (cancellable != NULL) {
		g_warning ("cancellable was not NULL");
		g_object_unref (cancellable);
	}
	cancellable = g_cancellable_new ();

	g_static_mutex_unlock (&mutex);

	pk_backend_set_allow_cancel (self, TRUE);

	pk_backend_set_status (self, status);
	pk_backend_thread_create (self, func);
}

void
pk_backend_cancel (PkBackend *self)
{
	g_return_if_fail (self != NULL);

	g_static_mutex_lock (&mutex);

	if (cancellable != NULL) {
		g_cancellable_cancel (cancellable);
	}

	g_static_mutex_unlock (&mutex);
}

gboolean
pk_backend_cancelled (PkBackend *self)
{
	gboolean cancelled;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (cancellable != NULL, FALSE);

	g_static_mutex_lock (&mutex);

	cancelled = g_cancellable_is_cancelled (cancellable);

	g_static_mutex_unlock (&mutex);

	return cancelled;
}

gboolean
pk_backend_finish (PkBackend *self, GError *error)
{
	gboolean cancelled = FALSE;

	g_return_val_if_fail (self != NULL, FALSE);

	pk_backend_set_allow_cancel (self, FALSE);

	g_static_mutex_lock (&mutex);

	if (cancellable != NULL) {
		cancelled = g_cancellable_is_cancelled (cancellable);
		g_object_unref (cancellable);
		cancellable = NULL;
	}

	g_static_mutex_unlock (&mutex);

	if (error != NULL) {
		pk_backend_error (self, error);
		g_error_free (error);
	}

	if (cancelled) {
		pk_backend_set_status (self, PK_STATUS_ENUM_CANCEL);
	}

	pk_backend_thread_finished (self);
	return (error == NULL);
}
