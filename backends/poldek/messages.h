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

G_BEGIN_DECLS

struct poldek_ctx;

/* where the running transaction wants what poldek reports */
typedef struct
{
	void (*rpm_output) (gpointer	 user_data,
			    const gchar *text);
	void (*download) (gpointer     user_data,
			  const gchar *label,
			  glong	       total,
			  glong	       amount);
	gpointer user_data;
} PkPoldekSink;

void   pk_poldek_messages_setup (struct poldek_ctx *ctx);
void   pk_poldek_messages_reset (void);
gchar *pk_poldek_messages_take_errors (void);
void   pk_poldek_messages_set_sink (const PkPoldekSink *sink);

G_END_DECLS
