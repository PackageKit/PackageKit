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

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_SPAWN (pk_backend_spawn_get_type ())
G_DECLARE_FINAL_TYPE (PkBackendSpawn, pk_backend_spawn, PK, BACKEND_SPAWN, GObject)

/**
 * PK_BACKEND_SPAWN_PROTOCOL_FD:
 *
 * File descriptor number the protocol socket is mapped to in the helper.
 */
#define PK_BACKEND_SPAWN_PROTOCOL_FD 3

/**
 * PK_BACKEND_SPAWN_PROTOCOL_FD_ENV:
 *
 * Environment variable telling the helper which descriptor carries the
 * protocol socket.
 */
#define PK_BACKEND_SPAWN_PROTOCOL_FD_ENV "PK_BACKEND_PROTOCOL_FD"

/**
 * PkBackendSpawnExitType:
 * @PK_BACKEND_SPAWN_EXIT_UNKNOWN: not started yet, or still running
 * @PK_BACKEND_SPAWN_EXIT_SUCCESS: exited with status 0
 * @PK_BACKEND_SPAWN_EXIT_FAILED: exited with a non-zero status
 * @PK_BACKEND_SPAWN_EXIT_SIGTERM: terminated by the SIGTERM we sent
 * @PK_BACKEND_SPAWN_EXIT_SIGKILL: terminated by the SIGKILL we sent
 * @PK_BACKEND_SPAWN_EXIT_SIGNAL: terminated by a signal we did not send
 *
 * How the spawned backend process ended.
 */
typedef enum {
	PK_BACKEND_SPAWN_EXIT_UNKNOWN,
	PK_BACKEND_SPAWN_EXIT_SUCCESS,
	PK_BACKEND_SPAWN_EXIT_FAILED,
	PK_BACKEND_SPAWN_EXIT_SIGTERM,
	PK_BACKEND_SPAWN_EXIT_SIGKILL,
	PK_BACKEND_SPAWN_EXIT_SIGNAL,
} PkBackendSpawnExitType;

PkBackendSpawn *pk_backend_spawn_new (const gchar *name);

const gchar *pk_backend_spawn_exit_type_to_string (PkBackendSpawnExitType exit_type);

gboolean pk_backend_spawn_start (PkBackendSpawn *self,
				 const gchar *executable,
				 const gchar *const *extra_env,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean pk_backend_spawn_is_running (PkBackendSpawn *self);
gboolean pk_backend_spawn_send_line (PkBackendSpawn *self,
				     const gchar *line,
				     GError **error) G_GNUC_WARN_UNUSED_RESULT;

void pk_backend_spawn_kill (PkBackendSpawn *self);
void pk_backend_spawn_set_exit_deadline (PkBackendSpawn *self, guint timeout_ms);
void pk_backend_spawn_clear_exit_deadline (PkBackendSpawn *self);

const gchar *pk_backend_spawn_get_log_domain (PkBackendSpawn *self);
void pk_backend_spawn_set_log_context (PkBackendSpawn *self, const gchar *context);

G_END_DECLS
