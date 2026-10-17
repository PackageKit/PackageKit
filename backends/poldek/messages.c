/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Marcin Banasiak <megabajt@pld-linux.org>
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

/*
 * What poldek reports while it works: its log, which also carries rpm's
 * output, and vfile's download progress. Errors are kept for the job's error
 * details; rpm output and download progress go to the running transaction.
 * poldek keeps both hooks process wide.
 */

#include <string.h>

#include <poldek/log.h>
#include <poldek/poldek.h>
#include <vfile/vfile.h>

#include "messages.h"

static GString *pk_poldek_messages_errors = NULL;
static gboolean pk_poldek_messages_error_open = FALSE;
static const PkPoldekSink *pk_poldek_messages_sink = NULL;

static void
pk_poldek_messages_log_cb (void *data, int pri, const char *message)
{
	/* rpm's own errors come with its output, as information */
	if ((pri & LOGERR) || ((pri & LOGINFO) && strstr (message, "error: ") != NULL)) {
		/* a continuation belongs to the previous error, anything else starts a new one */
		if ((pri & LOGOPT_CONT) == 0 && pk_poldek_messages_errors->len > 0 &&
		    pk_poldek_messages_error_open)
			g_string_append_c (pk_poldek_messages_errors, '\n');
		g_string_append (pk_poldek_messages_errors, message);
		pk_poldek_messages_error_open = TRUE;
	}
	if ((pri & LOGINFO) && pk_poldek_messages_sink != NULL &&
	    pk_poldek_messages_sink->rpm_output != NULL)
		pk_poldek_messages_sink->rpm_output (pk_poldek_messages_sink->user_data, message);
}

static void *
pk_poldek_messages_bar_new (void *data, const char *label)
{
	return g_strdup (label);
}

static void
pk_poldek_messages_bar_progress (void *bar, long total, long amount)
{
	if (pk_poldek_messages_sink != NULL && pk_poldek_messages_sink->download != NULL)
		pk_poldek_messages_sink->download (pk_poldek_messages_sink->user_data,
						   bar,
						   total,
						   amount);
}

static void
pk_poldek_messages_bar_reset (void *bar)
{}

static void
pk_poldek_messages_bar_free (void *bar)
{
	g_free (bar);
}

/* vfile stores the pointer */
static struct vf_progress pk_poldek_messages_progress = {
	.data = NULL,
	.new = pk_poldek_messages_bar_new,
	.progress = pk_poldek_messages_bar_progress,
	.reset = pk_poldek_messages_bar_reset,
	.free = pk_poldek_messages_bar_free,
};

/*
 * After every poldek_new(), which sets the default appender anew and so
 * drops all others: ours, then poldek's terminal one, whose output on stdout
 * goes to the daemon's log.
 */
void
pk_poldek_messages_setup (struct poldek_ctx *ctx)
{
	if (pk_poldek_messages_errors == NULL)
		pk_poldek_messages_errors = g_string_new (NULL);
	poldek_log_set_appender ("packagekit", NULL, NULL, 0, pk_poldek_messages_log_cb);
	poldek_log_add_appender ("_TTY", NULL, NULL, 0, NULL);
	poldek_configure (ctx, POLDEK_CONF_VFILEPROGRESS, &pk_poldek_messages_progress);
}

/* at the start of a job */
void
pk_poldek_messages_reset (void)
{
	if (pk_poldek_messages_errors != NULL)
		g_string_truncate (pk_poldek_messages_errors, 0);
	pk_poldek_messages_error_open = FALSE;
}

/* The errors poldek logged since the last reset, NULL if none. */
gchar *
pk_poldek_messages_take_errors (void)
{
	gchar *errors;

	if (pk_poldek_messages_errors == NULL || pk_poldek_messages_errors->len == 0)
		return NULL;
	errors = g_strdup (g_strstrip (pk_poldek_messages_errors->str));
	pk_poldek_messages_reset ();
	return errors;
}

/* @sink is kept, not copied: set NULL again before it goes away */
void
pk_poldek_messages_set_sink (const PkPoldekSink *sink)
{
	pk_poldek_messages_sink = sink;
}
