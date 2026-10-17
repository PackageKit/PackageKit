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
#include <pk-enum.h>

G_BEGIN_DECLS

/* errors whose code is a PkErrorEnum, so they can be reported as they are */
#define PK_POLDEK_ERROR (pk_poldek_error_quark ())
GQuark pk_poldek_error_quark (void);

/* One run request, from the reader thread that creates it to the main thread that runs it. */
typedef struct
{
	gchar	*id;
	gint	 cancelled;
	gboolean error_sent;
	gboolean finished;
	/* result items not sent yet, all for the event batch_ev */
	gchar	*batch_ev;
	json_t	*batch;
} PkPoldekJob;

PkPoldekJob *pk_poldek_job_new (const gchar *id);
void	     pk_poldek_job_free (PkPoldekJob *job);
void	     pk_poldek_job_cancel (PkPoldekJob *job);
gboolean     pk_poldek_job_is_cancelled (PkPoldekJob *job);
void	     pk_poldek_job_emit (PkPoldekJob *job,
				 const gchar *ev,
				 json_t	     *fields);
void	     pk_poldek_job_error (PkPoldekJob *job,
				  PkErrorEnum  code,
				  const gchar *format,
				  ...) G_GNUC_PRINTF (3, 4);
void	     pk_poldek_job_take_error (PkPoldekJob *job,
				       GError	   *error);
void	     pk_poldek_job_queue (PkPoldekJob *job,
				  const gchar *ev,
				  json_t      *item);
void	     pk_poldek_job_finish (PkPoldekJob *job);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PkPoldekJob, pk_poldek_job_free)

G_END_DECLS
