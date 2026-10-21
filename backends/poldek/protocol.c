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

/*
 * JSON Lines framing for the spawned backend protocol. Nothing in here knows
 * about poldek, so all of it can be tested without a poldek installation.
 */

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gio/gio.h>
#include <pk-enum.h>

#include "protocol.h"

static int pk_poldek_protocol_fd = -1;
static gboolean pk_poldek_protocol_disconnected = FALSE;

void
pk_poldek_line_reader_init (PkPoldekLineReader *reader)
{
	reader->buf = g_string_new (NULL);
	reader->scanned = 0;
}

void
pk_poldek_line_reader_clear (PkPoldekLineReader *reader)
{
	if (reader->buf != NULL)
		g_string_free (reader->buf, TRUE);
	reader->buf = NULL;
	reader->scanned = 0;
}

void
pk_poldek_line_reader_feed (PkPoldekLineReader *reader, const gchar *data, gsize len)
{
	g_string_append_len (reader->buf, data, len);
}

/*
 * Returns the next complete line without its newline, or NULL if there is
 * none yet. @too_long is set when the pending data already exceeds the
 * protocol's line limit without a newline; the stream is unusable then.
 */
gchar *
pk_poldek_line_reader_next (PkPoldekLineReader *reader, gboolean *too_long)
{
	const gchar *nl;
	gchar *line;
	gsize len;

	*too_long = FALSE;
	nl = memchr (reader->buf->str + reader->scanned, '\n', reader->buf->len - reader->scanned);
	if (nl == NULL) {
		reader->scanned = reader->buf->len;
		*too_long = reader->buf->len > PK_POLDEK_PROTOCOL_MAX_LINE;
		return NULL;
	}
	len = nl - reader->buf->str;
	line = g_strndup (reader->buf->str, len);
	g_string_erase (reader->buf, 0, len + 1);
	reader->scanned = 0;
	return line;
}

/*
 * Parses one line from the daemon. Returns a JSON object with a string "op",
 * and "job" either absent or a string.
 */
json_t *
pk_poldek_protocol_parse_request (const gchar *line, GError **error)
{
	json_error_t jerr;
	json_t *msg;
	json_t *job;

	msg = json_loads (line, JSON_REJECT_DUPLICATES, &jerr);
	if (msg == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "not JSON: %s at column %d",
			     jerr.text,
			     jerr.column);
		return NULL;
	}
	if (!json_is_object (msg) || !json_is_string (json_object_get (msg, "op"))) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "not an object with a string \"op\"");
		json_decref (msg);
		return NULL;
	}
	job = json_object_get (msg, "job");
	if (job != NULL && !json_is_string (job)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "\"job\" is not a string");
		json_decref (msg);
		return NULL;
	}
	return msg;
}

/* The array of strings @key of @req, or NULL if it is missing or not an array of strings. */
gchar **
pk_poldek_request_get_strv (json_t *req, const gchar *key)
{
	json_t *array = json_object_get (req, key);
	g_autoptr(GStrvBuilder) builder = NULL;
	json_t *value;
	gsize i;

	if (!json_is_array (array))
		return NULL;
	builder = g_strv_builder_new ();
	json_array_foreach (array, i, value)
	{
		if (!json_is_string (value))
			return NULL;
		g_strv_builder_add (builder, json_string_value (value));
	}
	return g_strv_builder_end (builder);
}

/* Unknown filter names are ignored, as the daemon only sends the ones we listed. */
PkBitfield
pk_poldek_request_get_filters (json_t *req)
{
	g_auto(GStrv) names = pk_poldek_request_get_strv (req, "filters");
	PkBitfield filters = 0;

	for (guint i = 0; names != NULL && names[i] != NULL; i++) {
		PkFilterEnum filter = pk_filter_enum_from_string (names[i]);

		if (filter != PK_FILTER_ENUM_UNKNOWN)
			pk_bitfield_add (filters, filter);
	}
	return filters;
}

gboolean
pk_poldek_request_get_bool (json_t *req, const gchar *key)
{
	return json_is_true (json_object_get (req, key));
}

void
pk_poldek_protocol_set_fd (int fd)
{
	pk_poldek_protocol_fd = fd;
}

/*
 * Writes @msg as one line. Only the main thread calls this. Once the daemon
 * has gone away all further output is dropped: the running job still goes
 * on to a safe end, and the helper exits after it.
 */
gboolean
pk_poldek_protocol_send (json_t *msg)
{
	g_autofree gchar *line = NULL;
	gsize len;
	gsize done = 0;

	if (pk_poldek_protocol_disconnected)
		return FALSE;
	line = json_dumps (msg, JSON_COMPACT);
	g_assert (line != NULL);
	len = strlen (line);
	line[len++] = '\n';
	while (done < len) {
		/* MSG_NOSIGNAL instead of ignoring SIGPIPE, which rpm and its scriptlets would inherit */
		ssize_t n = send (pk_poldek_protocol_fd, line + done, len - done, MSG_NOSIGNAL);

		if (n < 0 && errno == ENOTSOCK)
			n = write (pk_poldek_protocol_fd, line + done, len - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			g_printerr ("poldek: cannot write to the daemon: %s\n", g_strerror (errno));
			pk_poldek_protocol_disconnected = TRUE;
			return FALSE;
		}
		done += n;
	}
	return TRUE;
}
