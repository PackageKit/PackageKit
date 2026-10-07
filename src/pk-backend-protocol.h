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

#include <glib.h>
#include <pk-bitfield.h>

#include "pk-backend-job.h"

G_BEGIN_DECLS

/* see docs/backend-spawn-protocol.md */
#define PK_BACKEND_PROTOCOL_VERSION  1
#define PK_BACKEND_PROTOCOL_MAX_LINE (16 * 1024 * 1024)

typedef struct
{
	gchar *name;
	gchar *description;
	gchar *author;
	PkBitfield roles;
	PkBitfield filters;
	PkBitfield groups;
	gchar **mime_types;
} PkBackendHello;

void pk_backend_hello_free (PkBackendHello *hello);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PkBackendHello, pk_backend_hello_free)

/* requests: daemon -> helper, one line each without the trailing newline */
gchar *pk_backend_protocol_build_hello (void);
gchar *pk_backend_protocol_build_run (PkBackendJob *job, const gchar *job_id, GError **error);
gchar *pk_backend_protocol_build_cancel (const gchar *job_id);
gchar *pk_backend_protocol_build_exit (void);

/* events: helper -> daemon */
PkBackendHello *pk_backend_protocol_parse_hello (const gchar *line, GError **error);
gboolean pk_backend_protocol_handle_event (const gchar *line,
					   const gchar *job_id,
					   PkBackendJob *job,
					   gboolean *finished,
					   GError **error);

G_END_DECLS
