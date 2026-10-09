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

/*
 * The generic module for spawned backends. The daemon loads it in place of
 * libpk_backend_<name>.so when it finds a manifest for <name> instead, and
 * calls pk_backend_initialize_manifest() with that manifest. From then on
 * every role is forwarded to the helper program named in the manifest.
 */

#include "config.h"

#include <glib.h>

#include <pk-backend.h>
#include <pk-backend-job.h>
#include <pk-backend-spawn.h>
#include <pk-backend-protocol.h>
#include <pk-shared.h>

/* timeouts */
#define PK_SPAWN_MODULE_HELLO_TIMEOUT	  10000
#define PK_SPAWN_MODULE_CANCEL_TIMEOUT	  5000
#define PK_SPAWN_MODULE_EXIT_TIMEOUT	  5000
#define PK_SPAWN_MODULE_DEFAULT_IDLE_TIME 10

typedef struct
{
	gchar *name;
	gchar *exec;
	gchar *description;
	gchar *author;
	gchar **env;
	guint idle_time;

	PkBackendSpawn *process;
	const gchar *log_domain;
	PkBackendHello *hello;
	gboolean awaiting_hello;
	guint hello_id;
	GMainLoop *hello_loop;
	GError *hello_error;
	gboolean exit_requested;
	guint idle_id;

	PkBackendJob *job;
	gchar *job_id;
	guint job_serial;
	gboolean job_finished;
	guint cancel_id;
} PkSpawnModulePrivate;

static PkSpawnModulePrivate *priv = NULL;

static gboolean pk_spawn_module_start (GError **error);

/* ---- job bookkeeping ---- */

static void
pk_spawn_module_job_done (void)
{
	priv->job_finished = TRUE;
	g_clear_handle_id (&priv->cancel_id, g_source_remove);
}

static void pk_spawn_module_fail_job (PkErrorEnum code, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);

static void
pk_spawn_module_fail_job (PkErrorEnum code, const gchar *format, ...)
{
	g_autofree gchar *message = NULL;
	va_list args;

	if (priv->job == NULL || priv->job_finished)
		return;
	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);
	if (!pk_backend_job_has_set_error_code (priv->job))
		pk_backend_job_error_code (priv->job, code, "%s", message);
	pk_spawn_module_job_done ();
	pk_backend_job_finished (priv->job);
}

static void
pk_spawn_module_send_run (void)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;

	line = pk_backend_protocol_build_run (priv->job, priv->job_id, &error);
	if (line == NULL || !pk_backend_spawn_send_line (priv->process, line, &error)) {
		pk_spawn_module_fail_job (PK_ERROR_ENUM_INTERNAL_ERROR,
					  "failed to send the request to the helper: %s",
					  error->message);
	}
}

/* ---- handshake ---- */

static void
pk_spawn_module_handshake_failed (const gchar *reason)
{
	priv->awaiting_hello = FALSE;
	g_clear_handle_id (&priv->hello_id, g_source_remove);
	g_log (priv->log_domain,
	       G_LOG_LEVEL_WARNING,
	       "%s: helper handshake failed: %s",
	       priv->name,
	       reason);
	if (priv->hello_loop != NULL) {
		g_clear_error (&priv->hello_error);
		g_set_error_literal (&priv->hello_error, G_IO_ERROR, G_IO_ERROR_FAILED, reason);
		g_main_loop_quit (priv->hello_loop);
	}
	pk_spawn_module_fail_job (PK_ERROR_ENUM_INTERNAL_ERROR,
				  "helper handshake failed: %s",
				  reason);
	pk_backend_spawn_kill (priv->process);
}

static gboolean
pk_spawn_module_hello_timeout_cb (gpointer user_data)
{
	priv->hello_id = 0;
	pk_spawn_module_handshake_failed ("no hello received in time");
	return G_SOURCE_REMOVE;
}

static void
pk_spawn_module_hello_received (const gchar *line)
{
	g_autoptr(GError) error = NULL;
	PkBackendHello *hello;

	hello = pk_backend_protocol_parse_hello (line, &error);
	if (hello == NULL) {
		pk_spawn_module_handshake_failed (error->message);
		return;
	}
	g_clear_pointer (&priv->hello, pk_backend_hello_free);
	priv->hello = hello;
	priv->awaiting_hello = FALSE;
	g_clear_handle_id (&priv->hello_id, g_source_remove);
	g_log (priv->log_domain, G_LOG_LEVEL_DEBUG, "%s: helper is ready", priv->name);

	if (priv->hello_loop != NULL)
		g_main_loop_quit (priv->hello_loop);
	else if (priv->job != NULL && !priv->job_finished)
		pk_spawn_module_send_run ();
}

/* ---- process callbacks ---- */

static void
pk_spawn_module_line_cb (PkBackendSpawn *process, const gchar *line, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	gboolean finished = FALSE;
	gboolean in_job = priv->job != NULL && !priv->job_finished;

	if (priv->awaiting_hello) {
		pk_spawn_module_hello_received (line);
		return;
	}
	if (!pk_backend_protocol_handle_event (priv->log_domain,
					       line,
					       in_job ? priv->job_id : NULL,
					       in_job ? priv->job : NULL,
					       &finished,
					       &error)) {
		g_log (priv->log_domain,
		       G_LOG_LEVEL_WARNING,
		       "%s: protocol error, terminating the helper: %s",
		       priv->name,
		       error->message);
		pk_spawn_module_fail_job (PK_ERROR_ENUM_INTERNAL_ERROR,
					  "protocol error from the helper: %s",
					  error->message);
		pk_backend_spawn_kill (priv->process);
		return;
	}
	if (finished)
		pk_spawn_module_job_done ();
}

static void
pk_spawn_module_exited_cb (PkBackendSpawn *process, gint exit_type, gint status, gpointer user_data)
{
	const gchar *how = pk_backend_spawn_exit_type_to_string (exit_type);
	gboolean exit_requested = priv->exit_requested;

	priv->exit_requested = FALSE;
	if (priv->awaiting_hello) {
		g_autofree gchar *reason = NULL;
		reason = g_strdup_printf ("helper exited before saying hello (%s, status %d)",
					  how,
					  status);
		pk_spawn_module_handshake_failed (reason);
		return;
	}
	if (priv->job == NULL || priv->job_finished) {
		if (exit_type == PK_BACKEND_SPAWN_EXIT_SUCCESS && exit_requested)
			g_log (priv->log_domain,
			       G_LOG_LEVEL_DEBUG,
			       "%s: helper exited",
			       priv->name);
		else
			g_log (priv->log_domain,
			       G_LOG_LEVEL_WARNING,
			       "%s: helper exited unexpectedly while idle (%s, status %d)",
			       priv->name,
			       how,
			       status);
		return;
	}
	if (exit_requested) {
		/* a job arrived while the helper was shutting down: start over */
		g_autoptr(GError) error = NULL;
		if (!pk_spawn_module_start (&error)) {
			pk_spawn_module_fail_job (PK_ERROR_ENUM_INTERNAL_ERROR,
						  "failed to start the helper: %s",
						  error->message);
		}
		return;
	}
	if (exit_type == PK_BACKEND_SPAWN_EXIT_SIGTERM ||
	    exit_type == PK_BACKEND_SPAWN_EXIT_SIGKILL) {
		pk_spawn_module_fail_job (
		    PK_ERROR_ENUM_PROCESS_KILL,
		    "the helper was killed after it ignored the cancel request");
	} else {
		g_log (priv->log_domain,
		       G_LOG_LEVEL_WARNING,
		       "%s: helper exited during job %s (%s, status %d)",
		       priv->name,
		       priv->job_id,
		       how,
		       status);
		pk_spawn_module_fail_job (PK_ERROR_ENUM_INTERNAL_ERROR,
					  "the helper exited during the job (%s, status %d)",
					  how,
					  status);
	}
}

/* ---- lifecycle ---- */

static gboolean
pk_spawn_module_start (GError **error)
{
	g_autofree gchar *hello = NULL;

	g_object_set (priv->process,
		      "background",
		      priv->job != NULL && pk_backend_job_get_background (priv->job),
		      NULL);
	if (!pk_backend_spawn_start (priv->process,
				     priv->exec,
				     (const gchar *const *) priv->env,
				     error))
		return FALSE;
	priv->awaiting_hello = TRUE;
	priv->exit_requested = FALSE;
	hello = pk_backend_protocol_build_hello ();
	if (!pk_backend_spawn_send_line (priv->process, hello, error)) {
		priv->awaiting_hello = FALSE;
		pk_backend_spawn_kill (priv->process);
		return FALSE;
	}
	priv->hello_id = g_timeout_add (PK_SPAWN_MODULE_HELLO_TIMEOUT,
					pk_spawn_module_hello_timeout_cb,
					NULL);
	g_source_set_name_by_id (priv->hello_id, "[PkSpawnModule] hello timeout");
	return TRUE;
}

static gboolean
pk_spawn_module_idle_cb (gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;

	priv->idle_id = 0;
	if (priv->job != NULL || !pk_backend_spawn_is_running (priv->process))
		return G_SOURCE_REMOVE;
	g_log (priv->log_domain,
	       G_LOG_LEVEL_DEBUG,
	       "%s: helper is idle, asking it to exit",
	       priv->name);
	line = pk_backend_protocol_build_exit ();
	if (!pk_backend_spawn_send_line (priv->process, line, &error)) {
		g_log (priv->log_domain, G_LOG_LEVEL_WARNING, "%s: %s", priv->name, error->message);
		pk_backend_spawn_kill (priv->process);
		return G_SOURCE_REMOVE;
	}
	priv->exit_requested = TRUE;
	pk_backend_spawn_set_exit_deadline (priv->process, PK_SPAWN_MODULE_EXIT_TIMEOUT);
	return G_SOURCE_REMOVE;
}

static gboolean
pk_spawn_module_cancel_timeout_cb (gpointer user_data)
{
	priv->cancel_id = 0;
	if (priv->job != NULL && !priv->job_finished) {
		g_log (priv->log_domain,
		       G_LOG_LEVEL_WARNING,
		       "%s: helper did not finish job %s after cancel, terminating it",
		       priv->name,
		       priv->job_id);
		pk_backend_spawn_kill (priv->process);
	}
	return G_SOURCE_REMOVE;
}

/* ---- module interface ---- */

gboolean pk_backend_initialize_manifest (GKeyFile *conf,
					 PkBackend *backend,
					 const gchar *manifest_path,
					 GError **error);

void
pk_backend_destroy (PkBackend *backend)
{
	if (priv == NULL)
		return;
	g_clear_handle_id (&priv->idle_id, g_source_remove);
	g_clear_handle_id (&priv->cancel_id, g_source_remove);
	g_clear_handle_id (&priv->hello_id, g_source_remove);
	if (priv->process != NULL) {
		g_signal_handlers_disconnect_by_func (priv->process, pk_spawn_module_line_cb, NULL);
		g_signal_handlers_disconnect_by_func (priv->process,
						      pk_spawn_module_exited_cb,
						      NULL);
		if (pk_backend_spawn_is_running (priv->process)) {
			g_autofree gchar *line = pk_backend_protocol_build_exit ();
			if (!pk_backend_spawn_send_line (priv->process, line, NULL))
				pk_backend_spawn_kill (priv->process);
		}
		g_object_unref (priv->process);
	}
	g_clear_pointer (&priv->hello, pk_backend_hello_free);
	g_clear_error (&priv->hello_error);
	g_strfreev (priv->env);
	g_free (priv->name);
	g_free (priv->exec);
	g_free (priv->description);
	g_free (priv->author);
	g_free (priv->job_id);
	g_free (priv);
	priv = NULL;
}

gboolean
pk_backend_initialize_manifest (GKeyFile *conf,
				PkBackend *backend,
				const gchar *manifest_path,
				GError **error)
{
	g_autoptr(GKeyFile) manifest = g_key_file_new ();
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *dir = g_path_get_dirname (manifest_path);
	g_autofree gchar *root_dir = pk_util_get_root_dir (conf);
	g_autofree gchar *python_dir = NULL;
	g_autofree gchar *exec = NULL;
	gint idle_time;

	g_return_val_if_fail (priv == NULL, FALSE);

	if (!g_key_file_load_from_file (manifest, manifest_path, G_KEY_FILE_NONE, error)) {
		g_prefix_error (error, "failed to read manifest %s: ", manifest_path);
		return FALSE;
	}
	exec = g_key_file_get_string (manifest, "Backend", "Exec", error);
	if (exec == NULL) {
		g_prefix_error (error, "invalid manifest %s: ", manifest_path);
		return FALSE;
	}

	priv = g_new0 (PkSpawnModulePrivate, 1);
	priv->name = g_path_get_basename (dir);
	priv->exec = g_path_is_absolute (exec) ? g_steal_pointer (&exec)
					       : g_build_filename (dir, exec, NULL);
	priv->description = g_key_file_get_string (manifest, "Backend", "Description", NULL);
	priv->author = g_key_file_get_string (manifest, "Backend", "Author", NULL);
	idle_time = g_key_file_get_integer (conf, "Daemon", "BackendShutdownTimeout", NULL);
	priv->idle_time = idle_time > 0 ? idle_time : PK_SPAWN_MODULE_DEFAULT_IDLE_TIME;
	python_dir = g_build_filename (root_dir, PK_PYTHON_DIR, NULL);
	priv->env = g_new0 (gchar *, 2);
	priv->env[0] = g_strconcat ("PYTHONPATH=", python_dir, NULL);

	priv->process = pk_backend_spawn_new (priv->name);
	priv->log_domain = pk_backend_spawn_get_log_domain (priv->process);
	g_object_set (priv->process,
		      "allow-sigkill",
		      g_key_file_get_boolean (manifest, "Backend", "AllowSigkill", NULL),
		      "inherit-environment",
		      g_key_file_get_boolean (conf, "Daemon", "KeepEnvironment", NULL),
		      NULL);
	g_signal_connect (priv->process, "line", G_CALLBACK (pk_spawn_module_line_cb), NULL);
	g_signal_connect (priv->process, "exited", G_CALLBACK (pk_spawn_module_exited_cb), NULL);

	/* say hello now, so roles and filters are known before the first transaction */
	if (pk_spawn_module_start (&local_error)) {
		priv->hello_loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (priv->hello_loop);
		g_clear_pointer (&priv->hello_loop, g_main_loop_unref);
	}
	if (priv->hello == NULL) {
		if (local_error != NULL)
			g_propagate_error (error, g_steal_pointer (&local_error));
		else
			g_propagate_error (error, g_steal_pointer (&priv->hello_error));
		g_prefix_error (error, "helper %s: ", priv->exec);
		pk_backend_destroy (backend);
		return FALSE;
	}
	priv->idle_id = g_timeout_add_seconds (priv->idle_time, pk_spawn_module_idle_cb, NULL);
	g_source_set_name_by_id (priv->idle_id, "[PkSpawnModule] idle exit");
	return TRUE;
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	if (priv == NULL)
		return "Spawned backend";
	if (priv->hello->description != NULL)
		return priv->hello->description;
	return priv->description != NULL ? priv->description : priv->name;
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
	if (priv == NULL)
		return "";
	if (priv->hello->author != NULL)
		return priv->hello->author;
	return priv->author != NULL ? priv->author : "";
}

PkBitfield
pk_backend_get_roles (PkBackend *backend)
{
	/* cancel is handled by this module, not by the helper */
	return priv->hello->roles | pk_bitfield_value (PK_ROLE_ENUM_CANCEL);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
	return priv->hello->filters;
}

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
	return priv->hello->groups;
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
	return g_strdupv (priv->hello->mime_types);
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
	g_autofree gchar *context = NULL;

	if (priv->job != NULL) {
		pk_backend_job_error_code (job,
					   PK_ERROR_ENUM_LOCK_REQUIRED,
					   "the spawned backend is busy with another job");
		return;
	}
	g_clear_handle_id (&priv->idle_id, g_source_remove);
	priv->job = job;
	priv->job_finished = FALSE;
	g_free (priv->job_id);
	priv->job_id = g_strdup_printf ("%u", ++priv->job_serial);
	context = g_strdup_printf ("%s job %s", priv->name, priv->job_id);
	pk_backend_spawn_set_log_context (priv->process, context);
}

void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
	if (job != priv->job)
		return;
	g_clear_handle_id (&priv->cancel_id, g_source_remove);
	priv->job = NULL;
	priv->job_finished = FALSE;
	pk_backend_spawn_set_log_context (priv->process, NULL);
	if (pk_backend_spawn_is_running (priv->process)) {
		priv->idle_id = g_timeout_add_seconds (priv->idle_time,
						       pk_spawn_module_idle_cb,
						       NULL);
		g_source_set_name_by_id (priv->idle_id, "[PkSpawnModule] idle exit");
	}
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;

	if (job != priv->job || priv->job_finished || !pk_backend_spawn_is_running (priv->process))
		return;
	line = pk_backend_protocol_build_cancel (priv->job_id);
	if (!pk_backend_spawn_send_line (priv->process, line, &error)) {
		g_log (priv->log_domain, G_LOG_LEVEL_WARNING, "%s: %s", priv->name, error->message);
		pk_backend_spawn_kill (priv->process);
		return;
	}
	if (priv->cancel_id == 0) {
		priv->cancel_id = g_timeout_add (PK_SPAWN_MODULE_CANCEL_TIMEOUT,
						 pk_spawn_module_cancel_timeout_cb,
						 NULL);
		g_source_set_name_by_id (priv->cancel_id, "[PkSpawnModule] cancel timeout");
	}
}

void
pk_backend_run_job (PkBackend *backend, PkBackendJob *job)
{
	g_autoptr(GError) error = NULL;

	if (job != priv->job) {
		if (!pk_backend_job_has_set_error_code (job))
			pk_backend_job_error_code (job,
						   PK_ERROR_ENUM_INTERNAL_ERROR,
						   "job was not started on the spawned backend");
		pk_backend_job_finished (job);
		return;
	}
	if (!pk_backend_spawn_is_running (priv->process)) {
		/* the request is sent once the new helper has said hello */
		if (!pk_spawn_module_start (&error)) {
			pk_spawn_module_fail_job (PK_ERROR_ENUM_INTERNAL_ERROR,
						  "failed to start the helper: %s",
						  error->message);
		}
		return;
	}
	/* a helper that is still shutting down is restarted from the exited callback */
	if (priv->awaiting_hello || priv->exit_requested)
		return;
	pk_spawn_module_send_run ();
}
