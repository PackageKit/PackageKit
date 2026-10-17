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

/* the parts of rpm -vh output */
typedef enum {
	PK_POLDEK_RPM_VERIFY,
	PK_POLDEK_RPM_PREPARE,
	PK_POLDEK_RPM_INSTALL,
	PK_POLDEK_RPM_CLEANUP,
} PkPoldekRpmPhase;

/* @label is NULL for the progress of verify and prepare, which are not about one package */
typedef void (*PkPoldekRpmPhaseFunc) (gpointer	       user_data,
				      PkPoldekRpmPhase phase);
typedef void (*PkPoldekRpmProgressFunc) (gpointer	  user_data,
					 PkPoldekRpmPhase phase,
					 const gchar	 *label,
					 guint		  percentage);

typedef struct
{
	GString		       *label;
	guint			hashes;
	guint			reported;
	PkPoldekRpmPhase	phase;
	PkPoldekRpmPhaseFunc	phase_cb;
	PkPoldekRpmProgressFunc progress_cb;
	gpointer		user_data;
} PkPoldekRpmParser;

void	 pk_poldek_rpm_parser_init (PkPoldekRpmParser	   *parser,
				    PkPoldekRpmPhaseFunc    phase_cb,
				    PkPoldekRpmProgressFunc progress_cb,
				    gpointer		    user_data);
void	 pk_poldek_rpm_parser_clear (PkPoldekRpmParser *parser);
void	 pk_poldek_rpm_parser_feed (PkPoldekRpmParser *parser,
				    const gchar	      *chunk);
gboolean pk_poldek_rpm_label_names (const gchar *label,
				    const gchar *name,
				    gint	 epoch,
				    const gchar *version,
				    const gchar *release);

G_END_DECLS
