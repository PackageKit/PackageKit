/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
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
#include <jansson.h>
#include <pk-bitfield.h>

G_BEGIN_DECLS

/* see docs/extra/backend-spawn-protocol.md */
#define PK_POLDEK_PROTOCOL_VERSION  1
#define PK_POLDEK_PROTOCOL_MAX_LINE (16 * 1024 * 1024)

typedef struct
{
	GString *buf;
	gsize	 scanned;
} PkPoldekLineReader;

void	   pk_poldek_line_reader_init (PkPoldekLineReader *reader);
void	   pk_poldek_line_reader_clear (PkPoldekLineReader *reader);
void	   pk_poldek_line_reader_feed (PkPoldekLineReader *reader,
				       const gchar	  *data,
				       gsize		   len);
gchar	  *pk_poldek_line_reader_next (PkPoldekLineReader *reader,
				       gboolean		  *too_long);

json_t	  *pk_poldek_protocol_parse_request (const gchar *line,
					     GError	**error);

gchar	 **pk_poldek_request_get_strv (json_t	   *req,
				       const gchar *key);
PkBitfield pk_poldek_request_get_filters (json_t *req);
gboolean   pk_poldek_request_get_bool (json_t	   *req,
				       const gchar *key);

void	   pk_poldek_protocol_set_fd (int fd);
gboolean   pk_poldek_protocol_send (json_t *msg);

G_END_DECLS
