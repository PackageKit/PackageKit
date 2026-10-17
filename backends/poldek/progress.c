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
 * Progress from rpm -vh, which poldek passes on in chunks that split lines
 * anywhere. rpm 6 writes a label padded to a column, then 40 '#' as it goes:
 *
 *   Verifying...                          ########################################
 *   Preparing...                          ########################################
 *   Updating / installing...
 *   foo-1.1-1                             ########################################
 *   Cleaning up / removing...
 *   foo-1.0-1                             ########################################
 *
 * A package is named by its NAME-[EPOCH:]VERSION-RELEASE, without the arch,
 * cut to the column the bars start at.
 */

#include <string.h>

#include "progress.h"

/* rpm's hash marks for a complete bar, and the width of the label before them */
#define PK_POLDEK_RPM_HASHES	  40
#define PK_POLDEK_RPM_LABEL_WIDTH 38

static gboolean
pk_poldek_rpm_label_is (const gchar *label, const gchar *full)
{
	if (strlen (full) > PK_POLDEK_RPM_LABEL_WIDTH)
		return strlen (label) == PK_POLDEK_RPM_LABEL_WIDTH &&
		       strncmp (label, full, PK_POLDEK_RPM_LABEL_WIDTH) == 0;
	return strcmp (label, full) == 0;
}

/*
 * Whether rpm's @label for a bar names the package: rpm writes its NEVR,
 * which has the epoch whenever the package has the tag, 0 too.
 */
gboolean
pk_poldek_rpm_label_names (const gchar *label,
			   const gchar *name,
			   gint epoch,
			   const gchar *version,
			   const gchar *release)
{
	g_autofree gchar *nvr = g_strdup_printf ("%s-%s-%s", name, version, release);
	g_autofree gchar *nevr = g_strdup_printf ("%s-%d:%s-%s", name, epoch, version, release);

	return pk_poldek_rpm_label_is (label, nvr) || pk_poldek_rpm_label_is (label, nevr);
}

void
pk_poldek_rpm_parser_init (PkPoldekRpmParser *parser,
			   PkPoldekRpmPhaseFunc phase_cb,
			   PkPoldekRpmProgressFunc progress_cb,
			   gpointer user_data)
{
	parser->label = g_string_new (NULL);
	parser->hashes = 0;
	parser->reported = 0;
	parser->phase = PK_POLDEK_RPM_VERIFY;
	parser->phase_cb = phase_cb;
	parser->progress_cb = progress_cb;
	parser->user_data = user_data;
}

void
pk_poldek_rpm_parser_clear (PkPoldekRpmParser *parser)
{
	g_string_free (parser->label, TRUE);
	parser->label = NULL;
}

static void
pk_poldek_rpm_parser_set_phase (PkPoldekRpmParser *parser, PkPoldekRpmPhase phase)
{
	parser->phase = phase;
	parser->phase_cb (parser->user_data, phase);
}

/* the first '#' of a line: what the bar is about */
static void
pk_poldek_rpm_parser_start_bar (PkPoldekRpmParser *parser)
{
	g_strstrip (parser->label->str);
	g_string_set_size (parser->label, strlen (parser->label->str));
	if (strcmp (parser->label->str, "Verifying...") == 0)
		pk_poldek_rpm_parser_set_phase (parser, PK_POLDEK_RPM_VERIFY);
	else if (strcmp (parser->label->str, "Preparing...") == 0)
		pk_poldek_rpm_parser_set_phase (parser, PK_POLDEK_RPM_PREPARE);
}

static void
pk_poldek_rpm_parser_report (PkPoldekRpmParser *parser)
{
	gboolean package;

	if (parser->hashes == parser->reported)
		return;
	parser->reported = parser->hashes;
	package = parser->phase == PK_POLDEK_RPM_INSTALL || parser->phase == PK_POLDEK_RPM_CLEANUP;
	parser->progress_cb (parser->user_data,
			     parser->phase,
			     package ? parser->label->str : NULL,
			     MIN (parser->hashes, PK_POLDEK_RPM_HASHES) * 100 /
				 PK_POLDEK_RPM_HASHES);
}

static void
pk_poldek_rpm_parser_end_line (PkPoldekRpmParser *parser)
{
	if (parser->hashes > 0) {
		pk_poldek_rpm_parser_report (parser);
	} else {
		g_strstrip (parser->label->str);
		if (strcmp (parser->label->str, "Updating / installing...") == 0)
			pk_poldek_rpm_parser_set_phase (parser, PK_POLDEK_RPM_INSTALL);
		else if (strcmp (parser->label->str, "Cleaning up / removing...") == 0)
			pk_poldek_rpm_parser_set_phase (parser, PK_POLDEK_RPM_CLEANUP);
	}
	g_string_truncate (parser->label, 0);
	parser->hashes = 0;
	parser->reported = 0;
}

void
pk_poldek_rpm_parser_feed (PkPoldekRpmParser *parser, const gchar *chunk)
{
	for (const gchar *c = chunk; *c != '\0'; c++) {
		if (*c == '\n') {
			pk_poldek_rpm_parser_end_line (parser);
		} else if (*c == '#') {
			if (parser->hashes == 0)
				pk_poldek_rpm_parser_start_bar (parser);
			parser->hashes++;
		} else if (parser->hashes == 0) {
			g_string_append_c (parser->label, *c);
		}
	}
	/* a bar grows over several chunks */
	if (parser->hashes > 0)
		pk_poldek_rpm_parser_report (parser);
}
