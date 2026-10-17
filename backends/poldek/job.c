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

#include <stdarg.h>
#include <string.h>

#include "job.h"
#include "protocol.h"

/* items per result event, so a large result does not become one huge line */
#define PK_POLDEK_JOB_BATCH_SIZE 500

G_DEFINE_QUARK (pk-poldek-error-quark, pk_poldek_error)

PkPoldekJob *
pk_poldek_job_new (const gchar *id)
{
	PkPoldekJob *job = g_new0 (PkPoldekJob, 1);

	job->id = g_strdup (id);
	return job;
}

void
pk_poldek_job_free (PkPoldekJob *job)
{
	g_free (job->id);
	g_free (job->batch_ev);
	if (job->batch != NULL)
		json_decref (job->batch);
	g_free (job);
}

/* safe from any thread */
void
pk_poldek_job_cancel (PkPoldekJob *job)
{
	g_atomic_int_set (&job->cancelled, 1);
}

gboolean
pk_poldek_job_is_cancelled (PkPoldekJob *job)
{
	return g_atomic_int_get (&job->cancelled) != 0;
}

static void
pk_poldek_job_send (PkPoldekJob *job, const gchar *ev, json_t *msg)
{
	g_return_if_fail (!job->finished);
	/* json_pack() fails on text that is not UTF-8: losing the event beats stopping in a transaction */
	if (msg == NULL) {
		g_printerr ("poldek: %s event of job %s not sent, its text is not UTF-8\n",
			    ev,
			    job->id);
		return;
	}
	json_object_set_new (msg, "ev", json_string (ev));
	json_object_set_new (msg, "job", json_string (job->id));
	pk_poldek_protocol_send (msg);
	json_decref (msg);
}

static void
pk_poldek_job_flush (PkPoldekJob *job)
{
	g_autofree gchar *ev = g_steal_pointer (&job->batch_ev);
	json_t *items = g_steal_pointer (&job->batch);

	if (items != NULL)
		pk_poldek_job_send (job, ev, json_pack ("{s:o}", "items", items));
}

/*
 * Sends event @ev for @job, after any queued result items. @fields is a JSON
 * object with the event's other fields; the reference is stolen.
 */
void
pk_poldek_job_emit (PkPoldekJob *job, const gchar *ev, json_t *fields)
{
	pk_poldek_job_flush (job);
	pk_poldek_job_send (job, ev, fields);
}

/*
 * Queues @item (stolen) for a result event @ev with an "items" array, such as
 * "packages" or "details", so a query sends a few large events.
 */
void
pk_poldek_job_queue (PkPoldekJob *job, const gchar *ev, json_t *item)
{
	if (item == NULL) {
		g_printerr ("poldek: %s item of job %s not sent, its text is not UTF-8\n",
			    ev,
			    job->id);
		return;
	}
	if (job->batch != NULL && strcmp (job->batch_ev, ev) != 0)
		pk_poldek_job_flush (job);
	if (job->batch == NULL) {
		job->batch_ev = g_strdup (ev);
		job->batch = json_array ();
	}
	json_array_append_new (job->batch, item);
	if (json_array_size (job->batch) >= PK_POLDEK_JOB_BATCH_SIZE)
		pk_poldek_job_flush (job);
}

/* Only the first error of a job is sent; the daemon would ignore the others anyway. */
void
pk_poldek_job_error (PkPoldekJob *job, PkErrorEnum code, const gchar *format, ...)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *details = NULL;
	va_list args;

	if (job->error_sent)
		return;
	job->error_sent = TRUE;
	va_start (args, format);
	text = g_strdup_vprintf (format, args);
	va_end (args);
	/* rpm and poldek messages name files in whatever encoding they have */
	details = g_utf8_make_valid (text, -1);
	pk_poldek_job_emit (
	    job,
	    "error",
	    json_pack ("{s:s, s:s}", "code", pk_error_enum_to_string (code), "details", details));
}

/* Reports @error, a PK_POLDEK_ERROR, and frees it. */
void
pk_poldek_job_take_error (PkPoldekJob *job, GError *error)
{
	g_assert (error->domain == PK_POLDEK_ERROR);
	pk_poldek_job_error (job, error->code, "%s", error->message);
	g_error_free (error);
}

void
pk_poldek_job_finish (PkPoldekJob *job)
{
	if (job->finished)
		return;
	pk_poldek_job_emit (job, "finished", json_object ());
	job->finished = TRUE;
}
