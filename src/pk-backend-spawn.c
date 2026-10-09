/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025-2026 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

/**
 * PkBackendSpawn:
 *
 * Runs one spawned-backend helper process and handles its lifetime,
 * the stdout/stderr forwarding and the communication fd.
 */

#include "config.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include "pk-backend-spawn.h"
#include "pk-shared.h"

/* how long to wait for SIGTERM to work before sending SIGKILL */
#define PK_BACKEND_SPAWN_SIGKILL_DELAY 5000 /* ms */

/* how long to keep reading remaining output after the process has exited before giving up */
#define PK_BACKEND_SPAWN_DRAIN_TIMEOUT 1000 /* ms */

#define PK_BACKEND_SPAWN_DEFAULT_PATH "/usr/bin:/bin"

struct _PkBackendSpawn
{
	GObject parent;

	gchar *name;
	gchar *log_domain;
	gchar *log_context;
	gboolean background;
	gboolean allow_sigkill;
	gboolean inherit_environment;

	GSubprocess *subprocess;
	gint proto_fd;
	GDataInputStream *proto_in;
	GOutputStream *proto_out;
	GDataInputStream *stdout_in;
	GDataInputStream *stderr_in;
	GCancellable *cancellable;

	guint deadline_id;
	guint sigkill_id;
	guint drain_id;

	gboolean sent_sigterm;
	gboolean sent_sigkill;
	gboolean waited;
	guint readers_open;
	gboolean exited_emitted;
	PkBackendSpawnExitType exit_type;
	gint exit_status;
};

enum {
	SIGNAL_LINE,
	SIGNAL_EXITED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_BACKGROUND,
	PROP_ALLOW_SIGKILL,
	PROP_INHERIT_ENVIRONMENT,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkBackendSpawn, pk_backend_spawn, G_TYPE_OBJECT)

static void pk_backend_spawn_read_protocol_line (PkBackendSpawn *self);
static void pk_backend_spawn_read_log_line (PkBackendSpawn *self, GDataInputStream *stream);

/**
 * pk_backend_spawn_exit_type_to_string:
 */
const gchar *
pk_backend_spawn_exit_type_to_string (PkBackendSpawnExitType exit_type)
{
	switch (exit_type) {
	case PK_BACKEND_SPAWN_EXIT_SUCCESS:
		return "success";
	case PK_BACKEND_SPAWN_EXIT_FAILED:
		return "failed";
	case PK_BACKEND_SPAWN_EXIT_SIGTERM:
		return "sigterm";
	case PK_BACKEND_SPAWN_EXIT_SIGKILL:
		return "sigkill";
	case PK_BACKEND_SPAWN_EXIT_SIGNAL:
		return "signal";
	default:
		return "unknown";
	}
}

static const gchar *
pk_backend_spawn_log_prefix (PkBackendSpawn *self)
{
	return self->log_context != NULL ? self->log_context : self->name;
}

/**
 * pk_backend_spawn_child_setup:
 *
 * Reset the signal mask to empty so the helper starts with all signals unblocked:
 * we rely on SIGTERM to stop a helper that no longer answers, but a signal that is
 * blocked in the inherited mask stays pending and never reaches the helper's handler.
 */
static void
pk_backend_spawn_child_setup (gpointer user_data)
{
	sigset_t set;
	sigemptyset (&set);
	sigprocmask (SIG_SETMASK, &set, NULL);
}

static void
pk_backend_spawn_reset_state (PkBackendSpawn *self)
{
	g_clear_handle_id (&self->deadline_id, g_source_remove);
	g_clear_handle_id (&self->sigkill_id, g_source_remove);
	g_clear_handle_id (&self->drain_id, g_source_remove);

	if (self->cancellable != NULL)
		g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);

	g_clear_object (&self->proto_in);
	g_clear_object (&self->proto_out);
	g_clear_object (&self->stdout_in);
	g_clear_object (&self->stderr_in);
	if (self->proto_fd != -1) {
		close (self->proto_fd);
		self->proto_fd = -1;
	}
	g_clear_object (&self->subprocess);

	self->sent_sigterm = FALSE;
	self->sent_sigkill = FALSE;
	self->waited = FALSE;
	self->readers_open = 0;
	self->exited_emitted = FALSE;
	self->exit_type = PK_BACKEND_SPAWN_EXIT_UNKNOWN;
	self->exit_status = 0;
}

/**
 * pk_backend_spawn_maybe_emit_exited:
 *
 * Emit ::exited once the process has been reaped and all output has been
 * read (or we gave up waiting for it).
 */
static void
pk_backend_spawn_maybe_emit_exited (PkBackendSpawn *self)
{
	PkBackendSpawnExitType exit_type;
	gint exit_status;

	if (!self->waited || self->readers_open > 0 || self->exited_emitted)
		return;
	self->exited_emitted = TRUE;

	exit_type = self->exit_type;
	exit_status = self->exit_status;
	g_log (self->log_domain,
	       G_LOG_LEVEL_DEBUG,
	       "%s: helper exited: %s (%i)",
	       pk_backend_spawn_log_prefix (self),
	       pk_backend_spawn_exit_type_to_string (exit_type),
	       exit_status);

	/* tear down before emitting, so handlers see a stopped process and
	 * may start a new one from inside the handler */
	pk_backend_spawn_reset_state (self);
	g_signal_emit (self, signals[SIGNAL_EXITED], 0, exit_type, exit_status);
}

static gboolean
pk_backend_spawn_drain_timeout_cb (gpointer user_data)
{
	PkBackendSpawn *self = PK_BACKEND_SPAWN (user_data);

	self->drain_id = 0;
	if (self->readers_open > 0) {
		g_log (self->log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "%s: giving up on remaining helper output (%u streams still open)",
		       pk_backend_spawn_log_prefix (self),
		       self->readers_open);
		/* abort the outstanding reads; their callbacks close the readers */
		g_cancellable_cancel (self->cancellable);
	}
	return G_SOURCE_REMOVE;
}

static void
pk_backend_spawn_wait_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(PkBackendSpawn) self = PK_BACKEND_SPAWN (user_data);
	GSubprocess *subprocess = G_SUBPROCESS (source);
	g_autoptr(GError) error = NULL;

	if (!g_subprocess_wait_finish (subprocess, res, &error)) {
		/* only happens when we were cancelled during teardown */
		g_log (self->log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "%s: waiting for helper failed: %s",
		       pk_backend_spawn_log_prefix (self),
		       error->message);
		return;
	}

	/* the object may have been reset and restarted meanwhile */
	if (subprocess != self->subprocess)
		return;

	if (g_subprocess_get_if_exited (subprocess)) {
		self->exit_status = g_subprocess_get_exit_status (subprocess);
		self->exit_type = self->exit_status == 0 ? PK_BACKEND_SPAWN_EXIT_SUCCESS
							 : PK_BACKEND_SPAWN_EXIT_FAILED;
	} else if (g_subprocess_get_if_signaled (subprocess)) {
		self->exit_status = g_subprocess_get_term_sig (subprocess);
		if (self->exit_status == SIGKILL && self->sent_sigkill)
			self->exit_type = PK_BACKEND_SPAWN_EXIT_SIGKILL;
		else if (self->exit_status == SIGTERM && self->sent_sigterm)
			self->exit_type = PK_BACKEND_SPAWN_EXIT_SIGTERM;
		else
			self->exit_type = PK_BACKEND_SPAWN_EXIT_SIGNAL;
	} else {
		self->exit_status = -1;
		self->exit_type = PK_BACKEND_SPAWN_EXIT_FAILED;
	}
	self->waited = TRUE;

	/* no need to escalate or enforce a deadline any more */
	g_clear_handle_id (&self->deadline_id, g_source_remove);
	g_clear_handle_id (&self->sigkill_id, g_source_remove);

	/* let the readers deliver what is still buffered, but not forever */
	if (self->readers_open > 0 && self->drain_id == 0) {
		self->drain_id = g_timeout_add (PK_BACKEND_SPAWN_DRAIN_TIMEOUT,
						pk_backend_spawn_drain_timeout_cb,
						self);
		g_source_set_name_by_id (self->drain_id, "[PkBackendSpawn] drain");
	}

	pk_backend_spawn_maybe_emit_exited (self);
}

/**
 * pk_backend_spawn_reader_closed:
 *
 * One of the three streams hit EOF or was cancelled.
 */
static void
pk_backend_spawn_reader_closed (PkBackendSpawn *self)
{
	g_return_if_fail (self->readers_open > 0);
	self->readers_open--;
	pk_backend_spawn_maybe_emit_exited (self);
}

static void
pk_backend_spawn_protocol_line_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(PkBackendSpawn) self = PK_BACKEND_SPAWN (user_data);
	GDataInputStream *stream = G_DATA_INPUT_STREAM (source);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;
	gsize len = 0;

	line = g_data_input_stream_read_line_finish (stream, res, &len, &error);
	if (stream != self->proto_in)
		return; /* stale callback from a previous instance */
	if (line == NULL) {
		/* a helper killed with our request still unread resets the socket */
		if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED)) {
			g_log (self->log_domain,
			       G_LOG_LEVEL_WARNING,
			       "%s: failed to read from helper protocol socket: %s",
			       pk_backend_spawn_log_prefix (self),
			       error->message);
		}
		pk_backend_spawn_reader_closed (self);
		return;
	}

	/* strip a CR that a careless helper may have added */
	if (len > 0 && line[len - 1] == '\r')
		line[len - 1] = '\0';

	g_signal_emit (self, signals[SIGNAL_LINE], 0, line);

	/* the handler may have killed or restarted us */
	if (stream == self->proto_in)
		pk_backend_spawn_read_protocol_line (self);
}

static void
pk_backend_spawn_read_protocol_line (PkBackendSpawn *self)
{
	g_data_input_stream_read_line_async (self->proto_in,
					     G_PRIORITY_DEFAULT,
					     self->cancellable,
					     pk_backend_spawn_protocol_line_cb,
					     g_object_ref (self));
}

static void
pk_backend_spawn_log_line_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(PkBackendSpawn) self = PK_BACKEND_SPAWN (user_data);
	GDataInputStream *stream = G_DATA_INPUT_STREAM (source);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;
	g_autofree gchar *valid = NULL;
	gboolean is_stderr;

	line = g_data_input_stream_read_line_finish (stream, res, NULL, &error);
	if (stream != self->stdout_in && stream != self->stderr_in)
		return; /* stale callback from a previous instance */
	is_stderr = stream == self->stderr_in;
	if (line == NULL) {
		if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_log (self->log_domain,
			       G_LOG_LEVEL_WARNING,
			       "%s: failed to read helper %s: %s",
			       pk_backend_spawn_log_prefix (self),
			       is_stderr ? "stderr" : "stdout",
			       error->message);
		}
		pk_backend_spawn_reader_closed (self);
		return;
	}

	/* we never know what garbage we receive from the helper, so keep the log valid UTF-8 */
	valid = g_utf8_make_valid (line, -1);
	if (is_stderr)
		g_log (self->log_domain,
		       G_LOG_LEVEL_WARNING,
		       "%s: stderr: %s",
		       pk_backend_spawn_log_prefix (self),
		       valid);
	else
		g_log (self->log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "%s: stdout: %s",
		       pk_backend_spawn_log_prefix (self),
		       valid);

	pk_backend_spawn_read_log_line (self, stream);
}

static void
pk_backend_spawn_read_log_line (PkBackendSpawn *self, GDataInputStream *stream)
{
	g_data_input_stream_read_line_async (stream,
					     G_PRIORITY_DEFAULT,
					     self->cancellable,
					     pk_backend_spawn_log_line_cb,
					     g_object_ref (self));
}

static GDataInputStream *
pk_backend_spawn_wrap_line_reader (GInputStream *base)
{
	GDataInputStream *stream = g_data_input_stream_new (base);
	g_data_input_stream_set_newline_type (stream, G_DATA_STREAM_NEWLINE_TYPE_LF);
	return stream;
}

static gchar **
pk_backend_spawn_build_environment (PkBackendSpawn *self, const gchar *const *extra_env)
{
	g_auto(GStrv) envp = NULL;
	const gchar *path;

	if (self->inherit_environment) {
		envp = g_get_environ ();
	} else {
		envp = g_new0 (gchar *, 1);
		path = g_getenv ("PATH");
		envp = g_environ_setenv (envp,
					 "PATH",
					 path != NULL ? path : PK_BACKEND_SPAWN_DEFAULT_PATH,
					 TRUE);
	}

	envp = g_environ_setenv (envp,
				 PK_BACKEND_SPAWN_PROTOCOL_FD_ENV,
				 G_STRINGIFY (PK_BACKEND_SPAWN_PROTOCOL_FD),
				 TRUE);

	for (guint i = 0; extra_env != NULL && extra_env[i] != NULL; i++) {
		g_auto(GStrv) kv = g_strsplit (extra_env[i], "=", 2);
		if (kv[0] == NULL || kv[1] == NULL) {
			g_log (self->log_domain,
			       G_LOG_LEVEL_WARNING,
			       "ignoring malformed environment entry '%s'",
			       extra_env[i]);
			continue;
		}
		envp = g_environ_setenv (envp, kv[0], kv[1], TRUE);
	}

	return g_steal_pointer (&envp);
}

static void
pk_backend_spawn_apply_background_priority (PkBackendSpawn *self)
{
	const gchar *identifier;
	GPid pid;

	if (!self->background)
		return;

	identifier = g_subprocess_get_identifier (self->subprocess);
	if (identifier == NULL)
		return;
	pid = (GPid) g_ascii_strtoll (identifier, NULL, 10);
	if (pid <= 0)
		return;

#if HAVE_SETPRIORITY
	g_log (self->log_domain,
	       G_LOG_LEVEL_DEBUG,
	       "%s: renice helper to 10",
	       pk_backend_spawn_log_prefix (self));
	if (setpriority (PRIO_PROCESS, pid, 10) != 0)
		g_log (self->log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "failed to renice helper: %s",
		       g_strerror (errno));
#endif
	g_log (self->log_domain,
	       G_LOG_LEVEL_DEBUG,
	       "%s: setting helper ioprio class to idle",
	       pk_backend_spawn_log_prefix (self));
	pk_ioprio_set_idle (pid);
}

/**
 * pk_backend_spawn_start:
 * @self: a #PkBackendSpawn
 * @executable: absolute path of the helper program
 * @extra_env: (nullable): additional `KEY=VALUE` entries for the helper's environment
 * @error: return location for a #GError
 *
 * Starts the helper with no arguments. The protocol socket is mapped to
 * %PK_BACKEND_SPAWN_PROTOCOL_FD in the child and announced in
 * %PK_BACKEND_SPAWN_PROTOCOL_FD_ENV. Unless #PkBackendSpawn:inherit-environment
 * is set, the helper only gets `PATH`, that variable and @extra_env.
 *
 * Returns: %TRUE if the process was started
 */
gboolean
pk_backend_spawn_start (PkBackendSpawn *self,
			const gchar *executable,
			const gchar *const *extra_env,
			GError **error)
{
	g_autoptr(GSubprocessLauncher) launcher = NULL;
	g_auto(GStrv) envp = NULL;
	g_autoptr(GInputStream) protocol_base_in = NULL;
	const gchar *argv[2] = { executable, NULL };
	gint fds[2];

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (self), FALSE);
	g_return_val_if_fail (executable != NULL, FALSE);

	if (self->subprocess != NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_BUSY,
			     "helper %s is already running",
			     self->name);
		return FALSE;
	}
	pk_backend_spawn_reset_state (self);

	if (!g_file_test (executable, G_FILE_TEST_IS_EXECUTABLE)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "helper %s is not an executable file",
			     executable);
		return FALSE;
	}

	if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "failed to create protocol socket pair: %s",
			     g_strerror (errno));
		return FALSE;
	}
	self->proto_fd = fds[0];

	launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
					      G_SUBPROCESS_FLAGS_STDERR_PIPE);
	g_subprocess_launcher_set_stdin_file_path (launcher, "/dev/null");
	envp = pk_backend_spawn_build_environment (self, extra_env);
	g_subprocess_launcher_set_environ (launcher, envp);
	/* the launcher owns fds[1] from here on and closes it after spawning */
	g_subprocess_launcher_take_fd (launcher, fds[1], PK_BACKEND_SPAWN_PROTOCOL_FD);
	g_subprocess_launcher_set_child_setup (launcher, pk_backend_spawn_child_setup, NULL, NULL);

	g_log (self->log_domain,
	       G_LOG_LEVEL_DEBUG,
	       "%s: starting helper %s",
	       pk_backend_spawn_log_prefix (self),
	       executable);
	self->subprocess = g_subprocess_launcher_spawnv (launcher, argv, error);
	if (self->subprocess == NULL) {
		g_prefix_error (error, "failed to start helper %s: ", executable);
		pk_backend_spawn_reset_state (self);
		return FALSE;
	}

	pk_backend_spawn_apply_background_priority (self);

	self->cancellable = g_cancellable_new ();

	protocol_base_in = g_unix_input_stream_new (self->proto_fd, FALSE);
	self->proto_in = pk_backend_spawn_wrap_line_reader (protocol_base_in);
	self->proto_out = g_unix_output_stream_new (self->proto_fd, FALSE);
	self->stdout_in = pk_backend_spawn_wrap_line_reader (
	    g_subprocess_get_stdout_pipe (self->subprocess));
	self->stderr_in = pk_backend_spawn_wrap_line_reader (
	    g_subprocess_get_stderr_pipe (self->subprocess));

	self->readers_open = 3;
	pk_backend_spawn_read_protocol_line (self);
	pk_backend_spawn_read_log_line (self, self->stdout_in);
	pk_backend_spawn_read_log_line (self, self->stderr_in);

	g_subprocess_wait_async (self->subprocess,
				 NULL,
				 pk_backend_spawn_wait_cb,
				 g_object_ref (self));
	return TRUE;
}

/**
 * pk_backend_spawn_is_running:
 *
 * Returns: %TRUE between a successful pk_backend_spawn_start() and the
 * #PkBackendSpawn::exited signal
 */
gboolean
pk_backend_spawn_is_running (PkBackendSpawn *self)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (self), FALSE);
	return self->subprocess != NULL && !self->waited;
}

/**
 * pk_backend_spawn_send_line:
 * @self: a #PkBackendSpawn
 * @line: text without a trailing newline
 * @error: return location for a #GError
 *
 * Writes @line plus a newline to the helper's protocol socket.
 * The write is synchronous.
 *
 * Returns: %TRUE if the whole line was written
 */
gboolean
pk_backend_spawn_send_line (PkBackendSpawn *self, const gchar *line, GError **error)
{
	g_autofree gchar *buf = NULL;

	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (self), FALSE);
	g_return_val_if_fail (line != NULL, FALSE);

	if (!pk_backend_spawn_is_running (self)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_CONNECTED,
			     "helper %s is not running",
			     self->name);
		return FALSE;
	}

	buf = g_strconcat (line, "\n", NULL);
	if (!g_output_stream_write_all (self->proto_out, buf, strlen (buf), NULL, NULL, error)) {
		g_prefix_error (error, "failed to send to helper %s: ", self->name);
		return FALSE;
	}
	return TRUE;
}

static gboolean
pk_backend_spawn_sigkill_cb (gpointer user_data)
{
	PkBackendSpawn *self = PK_BACKEND_SPAWN (user_data);

	self->sigkill_id = 0;
	if (!pk_backend_spawn_is_running (self))
		return G_SOURCE_REMOVE;

	g_log (self->log_domain,
	       G_LOG_LEVEL_WARNING,
	       "%s: helper ignored SIGTERM, sending SIGKILL",
	       pk_backend_spawn_log_prefix (self));
	self->sent_sigkill = TRUE;
	g_subprocess_force_exit (self->subprocess);
	return G_SOURCE_REMOVE;
}

/**
 * pk_backend_spawn_kill:
 *
 * Sends SIGTERM to the helper now. If #PkBackendSpawn:allow-sigkill is set
 * and the helper is still alive after %PK_BACKEND_SPAWN_SIGKILL_DELAY, it
 * is sent SIGKILL. Calling this again while SIGTERM is pending sends SIGKILL
 * at once if allowed. Does nothing if the helper is not running.
 */
void
pk_backend_spawn_kill (PkBackendSpawn *self)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (self));

	g_clear_handle_id (&self->deadline_id, g_source_remove);
	if (!pk_backend_spawn_is_running (self))
		return;
	if (self->sent_sigterm) {
		/* asked to kill harder: skip the rest of the grace period */
		if (self->allow_sigkill && !self->sent_sigkill) {
			g_clear_handle_id (&self->sigkill_id, g_source_remove);
			pk_backend_spawn_sigkill_cb (self);
		}
		return;
	}

	g_log (self->log_domain,
	       G_LOG_LEVEL_DEBUG,
	       "%s: sending SIGTERM to helper",
	       pk_backend_spawn_log_prefix (self));
	self->sent_sigterm = TRUE;
	g_subprocess_send_signal (self->subprocess, SIGTERM);

	if (self->allow_sigkill) {
		self->sigkill_id = g_timeout_add (PK_BACKEND_SPAWN_SIGKILL_DELAY,
						  pk_backend_spawn_sigkill_cb,
						  self);
		g_source_set_name_by_id (self->sigkill_id, "[PkBackendSpawn] sigkill");
	}
}

static gboolean
pk_backend_spawn_deadline_cb (gpointer user_data)
{
	PkBackendSpawn *self = PK_BACKEND_SPAWN (user_data);

	self->deadline_id = 0;
	if (pk_backend_spawn_is_running (self)) {
		g_log (self->log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "%s: helper did not exit in time",
		       pk_backend_spawn_log_prefix (self));
		pk_backend_spawn_kill (self);
	}
	return G_SOURCE_REMOVE;
}

/**
 * pk_backend_spawn_set_exit_deadline:
 * @self: a #PkBackendSpawn
 * @timeout_ms: grace period in milliseconds
 *
 * Gives the helper @timeout_ms to exit by itself, after which
 * pk_backend_spawn_kill() is called.
 * A later call replaces the pending deadline.
 */
void
pk_backend_spawn_set_exit_deadline (PkBackendSpawn *self, guint timeout_ms)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (self));

	g_clear_handle_id (&self->deadline_id, g_source_remove);
	if (!pk_backend_spawn_is_running (self))
		return;
	self->deadline_id = g_timeout_add (timeout_ms, pk_backend_spawn_deadline_cb, self);
	g_source_set_name_by_id (self->deadline_id, "[PkBackendSpawn] exit deadline");
}

/**
 * pk_backend_spawn_clear_exit_deadline:
 *
 * Cancels a pending deadline set with pk_backend_spawn_set_exit_deadline().
 * An escalation already started by pk_backend_spawn_kill() continues.
 */
void
pk_backend_spawn_clear_exit_deadline (PkBackendSpawn *self)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (self));
	g_clear_handle_id (&self->deadline_id, g_source_remove);
}

/**
 * pk_backend_spawn_get_log_domain:
 *
 * Returns: the GLib log domain used for everything about this helper,
 * "PackageKit-<name>", valid as long as @self is
 */
const gchar *
pk_backend_spawn_get_log_domain (PkBackendSpawn *self)
{
	g_return_val_if_fail (PK_IS_BACKEND_SPAWN (self), NULL);
	return self->log_domain;
}

/**
 * pk_backend_spawn_set_log_context:
 * @self: a #PkBackendSpawn
 * @context: (nullable): prefix for log lines, e.g. "portage"; %NULL resets to the name
 *
 * Sets the prefix used when the helper's stdout and stderr are logged, so
 * that output can be attributed to the running job.
 */
void
pk_backend_spawn_set_log_context (PkBackendSpawn *self, const gchar *context)
{
	g_return_if_fail (PK_IS_BACKEND_SPAWN (self));
	g_free (self->log_context);
	self->log_context = g_strdup (context);
}

static void
pk_backend_spawn_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkBackendSpawn *self = PK_BACKEND_SPAWN (object);

	switch (prop_id) {
	case PROP_BACKGROUND:
		g_value_set_boolean (value, self->background);
		break;
	case PROP_ALLOW_SIGKILL:
		g_value_set_boolean (value, self->allow_sigkill);
		break;
	case PROP_INHERIT_ENVIRONMENT:
		g_value_set_boolean (value, self->inherit_environment);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pk_backend_spawn_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	PkBackendSpawn *self = PK_BACKEND_SPAWN (object);

	switch (prop_id) {
	case PROP_BACKGROUND:
		self->background = g_value_get_boolean (value);
		break;
	case PROP_ALLOW_SIGKILL:
		self->allow_sigkill = g_value_get_boolean (value);
		break;
	case PROP_INHERIT_ENVIRONMENT:
		self->inherit_environment = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
pk_backend_spawn_finalize (GObject *object)
{
	PkBackendSpawn *self = PK_BACKEND_SPAWN (object);

	if (pk_backend_spawn_is_running (self)) {
		g_log (self->log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "%s: helper still running in finalize, sending SIGTERM",
		       pk_backend_spawn_log_prefix (self));
		g_subprocess_send_signal (self->subprocess, SIGTERM);
	}
	pk_backend_spawn_reset_state (self);

	g_free (self->name);
	g_free (self->log_domain);
	g_free (self->log_context);

	G_OBJECT_CLASS (pk_backend_spawn_parent_class)->finalize (object);
}

static void
pk_backend_spawn_class_init (PkBackendSpawnClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->finalize = pk_backend_spawn_finalize;
	object_class->get_property = pk_backend_spawn_get_property;
	object_class->set_property = pk_backend_spawn_set_property;

	/**
	 * PkBackendSpawn:background:
	 *
	 * Start the helper with lowered CPU and I/O priority.
	 */
	pspec = g_param_spec_boolean ("background",
				      NULL,
				      NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_BACKGROUND, pspec);

	/**
	 * PkBackendSpawn:allow-sigkill:
	 *
	 * Whether a helper that ignores SIGTERM may be sent SIGKILL. This
	 * makes cancellation reliable, but may corrupt package databases the
	 * helper had open.
	 */
	pspec = g_param_spec_boolean ("allow-sigkill",
				      NULL,
				      NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_ALLOW_SIGKILL, pspec);

	/**
	 * PkBackendSpawn:inherit-environment:
	 *
	 * Pass the daemon's whole environment to the helper instead of a
	 * minimal one. Corresponds to KeepEnvironment in PackageKit.conf.
	 */
	pspec = g_param_spec_boolean ("inherit-environment",
				      NULL,
				      NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_INHERIT_ENVIRONMENT, pspec);

	/**
	 * PkBackendSpawn::line:
	 * @line: one line received on the protocol socket, without the newline
	 */
	signals[SIGNAL_LINE] = g_signal_new ("line",
					     G_TYPE_FROM_CLASS (object_class),
					     G_SIGNAL_RUN_LAST,
					     0,
					     NULL,
					     NULL,
					     g_cclosure_marshal_VOID__STRING,
					     G_TYPE_NONE,
					     1,
					     G_TYPE_STRING);

	/**
	 * PkBackendSpawn::exited:
	 * @exit_type: a #PkBackendSpawnExitType
	 * @status: the exit status, or the signal number for the signal exit types
	 *
	 * Emitted once after the helper has ended and all its output has been
	 * delivered. The process is no longer running when this is emitted.
	 */
	signals[SIGNAL_EXITED] = g_signal_new ("exited",
					       G_TYPE_FROM_CLASS (object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       NULL,
					       G_TYPE_NONE,
					       2,
					       G_TYPE_INT,
					       G_TYPE_INT);
}

static void
pk_backend_spawn_init (PkBackendSpawn *self)
{
	self->proto_fd = -1;
	self->exit_type = PK_BACKEND_SPAWN_EXIT_UNKNOWN;
}

/**
 * pk_backend_spawn_new:
 * @name: backend name, used in log messages
 *
 * Returns: (transfer full): a new #PkBackendSpawn
 */
PkBackendSpawn *
pk_backend_spawn_new (const gchar *name)
{
	PkBackendSpawn *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_object_new (PK_TYPE_BACKEND_SPAWN, NULL);
	self->name = g_strdup (name);
	self->log_domain = g_strdup_printf ("PackageKit-%s", name);
	return self;
}
