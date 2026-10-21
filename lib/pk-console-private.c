/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <wchar.h>
#include <strings.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include "pk-enum.h"
#include "pk-package.h"

#include "pk-console-private.h"

/**
 * pk_console_str_width:
 * @text: the UTF-8 string to measure
 *
 * Calculate the display width of a UTF-8 string in terminal columns.
 *
 * Returns: the display width in terminal columns
 */
guint
pk_console_str_width (const gchar *text)
{
	guint width = 0;
	const gchar *p;
	wchar_t wc;
	gint char_width;
	mbstate_t state;

	if (text == NULL)
		return 0;

	memset (&state, 0, sizeof (mbstate_t));
	p = text;

	while (*p != '\0') {
		size_t bytes = mbrtowc (&wc, p, MB_CUR_MAX, &state);

		if (bytes == (size_t) -1 || bytes == (size_t) -2) {
			/* Invalid multibyte sequence, treat as single-width */
			width++;
			p++;
			memset (&state, 0, sizeof (mbstate_t));
			continue;
		}

		if (bytes == 0)
			break;

		char_width = wcwidth (wc);
		if (char_width < 0) {
			/* Non-printable character, treat as zero-width */
			char_width = 0;
		}

		width += char_width;
		p += bytes;
	}

	return width;
}

/**
 * pk_console_text_truncate:
 * @text: the UTF-8 string to truncate
 * @max_width: maximum display width in terminal columns
 *
 * Truncate a UTF-8 string to fit within a given display width
 */
gchar *
pk_console_text_truncate (const gchar *text, guint max_width)
{
	const gchar *p;
	guint width = 0;
	wchar_t wc;
	gint char_width;
	mbstate_t state;
	guint target_width;
	gboolean use_ellipsis;
	gsize byte_len;
	gchar *result;

	if (text == NULL)
		return g_strdup ("");

	if (max_width == 0)
		return g_strdup ("");

	/* Check if the string already fits */
	if (pk_console_str_width (text) <= max_width)
		return g_strdup (text);

	/* String needs truncation */
	memset (&state, 0, sizeof (mbstate_t));
	p = text;

	/* Reserve space for ellipsis if possible */
	target_width = max_width;
	use_ellipsis = max_width > 3;
	if (use_ellipsis)
		target_width = max_width - 3;

	while (*p != '\0') {
		size_t bytes = mbrtowc (&wc, p, MB_CUR_MAX, &state);

		if (bytes == (size_t) -1 || bytes == (size_t) -2) {
			/* Invalid multibyte sequence */
			if (width + 1 > target_width)
				break;
			width++;
			p++;
			memset (&state, 0, sizeof (mbstate_t));
			continue;
		}

		if (bytes == 0)
			break;

		char_width = wcwidth (wc);
		if (char_width < 0)
			char_width = 0;

		if (width + char_width > target_width)
			break;

		width += char_width;
		p += bytes;
	}

	byte_len = p - text;
	if (use_ellipsis) {
		result = g_malloc0 (byte_len + 4);
		memcpy (result, text, byte_len);
		memcpy (result + byte_len, "...", 3);
		return result;
	}

	return g_strndup (text, byte_len);
}

/**
 * pk_console_strpad:
 * @text: the UTF-8 string to pad
 * @width: desired display width in terminal columns
 *
 * Pad a UTF-8 string to exactly the specified display width
 */
gchar *
pk_console_strpad (const gchar *text, guint width)
{
	guint display_width;
	guint padding_needed;
	g_autofree gchar *padding = NULL;

	if (text == NULL)
		return g_strnfill (width, ' ');

	/* Calculate actual display width */
	display_width = pk_console_str_width (text);

	if (display_width >= width)
		return g_strdup (text);

	/* Add padding to reach desired width */
	padding_needed = width - display_width;

	padding = g_strnfill (padding_needed, ' ');
	return g_strdup_printf ("%s%s", text, padding);
}

/**
 * pk_console_get_width:
 *
 * Get the width of the terminal stdout is connected to. The COLUMNS
 * environment variable takes precedence, and if stdout is not a terminal
 * a fixed width is used, so output stays predictable when piped.
 *
 * Returns: the console width in columns
 */
guint
pk_console_get_width (void)
{
	const gchar *columns;
	struct winsize w;

	columns = g_getenv ("COLUMNS");
	if (columns != NULL) {
		guint64 value = g_ascii_strtoull (columns, NULL, 10);
		if (value > 0 && value <= G_MAXUINT16)
			return (guint) value;
	}

	if (isatty (STDOUT_FILENO) && ioctl (STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
		return w.ws_col;

	return PK_CONSOLE_DEFAULT_WIDTH;
}

/**
 * pk_console_text_wrap:
 * @text: the UTF-8 text to wrap
 * @width: maximum display width of a line in terminal columns
 * @indent: number of spaces lines after the first one are indented by
 *
 * Wrap text at word boundaries, so that no line is wider than @width
 * including its indentation. Existing line breaks are preserved, and words
 * wider than the available space are put on a line of their own.
 *
 * Returns: (transfer full): the wrapped text
 */
gchar *
pk_console_text_wrap (const gchar *text, guint width, guint indent)
{
	GString *str = g_string_new (NULL);
	g_auto(GStrv) lines = NULL;
	g_autofree gchar *indent_str = g_strnfill (indent, ' ');
	guint avail;

	if (text == NULL)
		return g_string_free (str, FALSE);

	/* always leave some space for the text itself */
	avail = width > indent + 20 ? width - indent : 20;

	lines = g_strsplit (text, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_auto(GStrv) words = NULL;
		guint line_width = 0;

		if (i > 0) {
			g_string_append_c (str, '\n');
			if (lines[i][0] != '\0')
				g_string_append (str, indent_str);
		}

		/* keep short lines untouched, they may be preformatted */
		if (pk_console_str_width (lines[i]) <= avail) {
			g_string_append (str, lines[i]);
			continue;
		}

		words = g_strsplit (lines[i], " ", -1);
		for (guint j = 0; words[j] != NULL; j++) {
			guint word_width = pk_console_str_width (words[j]);

			if (line_width > 0 && line_width + 1 + word_width > avail) {
				g_string_append_c (str, '\n');
				g_string_append (str, indent_str);
				line_width = 0;
			} else if (j > 0) {
				g_string_append_c (str, ' ');
				line_width++;
			}
			g_string_append (str, words[j]);
			line_width += word_width;
		}
	}

	return g_string_free (str, FALSE);
}

/**
 * pk_console_package_sort_func:
 * @a: a pointer to a #PkPackage pointer
 * @b: a pointer to a #PkPackage pointer
 *
 * Sort function for a #GPtrArray of #PkPackage objects.
 */
gint
pk_console_package_sort_func (gconstpointer a, gconstpointer b)
{
	return pk_package_compare (*(PkPackage **) a, *(PkPackage **) b);
}

/* the flexible column of a table is never truncated to less than this */
#define PK_CONSOLE_TABLE_MIN_FLEXIBLE 12

typedef struct
{
	gchar *text;
	gchar *color;
} PkConsoleTableCell;

struct PkConsoleTable
{
	guint n_columns;
	guint indent;
	gint flexible;
	gchar **header;
	GPtrArray *rows; /* of PkConsoleTableCell[n_columns] */
};

/**
 * pk_console_table_new:
 * @n_columns: the number of columns
 *
 * Create a new table for console output. Columns are aligned to the widest
 * cell, and columns that are entirely empty are left out when rendering.
 *
 * Returns: (transfer full): a new #PkConsoleTable
 */
PkConsoleTable *
pk_console_table_new (guint n_columns)
{
	PkConsoleTable *table = g_new0 (PkConsoleTable, 1);

	g_return_val_if_fail (n_columns > 0, NULL);

	table->n_columns = n_columns;
	table->flexible = -1;
	table->rows = g_ptr_array_new ();
	return table;
}

/**
 * pk_console_table_free:
 * @table: a #PkConsoleTable
 */
void
pk_console_table_free (PkConsoleTable *table)
{
	if (table == NULL)
		return;

	for (guint i = 0; i < table->rows->len; i++) {
		PkConsoleTableCell *row = g_ptr_array_index (table->rows, i);
		for (guint j = 0; j < table->n_columns; j++) {
			g_free (row[j].text);
			g_free (row[j].color);
		}
		g_free (row);
	}
	g_ptr_array_unref (table->rows);
	g_strfreev (table->header);
	g_free (table);
}

static gchar **
pk_console_table_collect_va (PkConsoleTable *table, const gchar *first, va_list args)
{
	gchar **cells = g_new0 (gchar *, table->n_columns + 1);

	for (guint i = 0; i < table->n_columns; i++) {
		const gchar *text = i == 0 ? first : va_arg (args, const gchar *);
		cells[i] = g_strdup (text != NULL ? text : "");
	}

	return cells;
}

/**
 * pk_console_table_set_header:
 * @table: a #PkConsoleTable
 * @first: the first column title
 * @...: the other column titles, one per column
 *
 * Set the column titles. An empty or %NULL title leaves the header cell blank.
 */
void
pk_console_table_set_header (PkConsoleTable *table, const gchar *first, ...)
{
	va_list args;

	g_strfreev (table->header);
	va_start (args, first);
	table->header = pk_console_table_collect_va (table, first, args);
	va_end (args);
}

/**
 * pk_console_table_set_indent:
 * @table: a #PkConsoleTable
 * @indent: the number of spaces to indent every line by
 */
void
pk_console_table_set_indent (PkConsoleTable *table, guint indent)
{
	table->indent = indent;
}

/**
 * pk_console_table_set_flexible:
 * @table: a #PkConsoleTable
 * @column: the column index
 *
 * Set the column that is truncated if the table is too wide.
 */
void
pk_console_table_set_flexible (PkConsoleTable *table, guint column)
{
	g_return_if_fail (column < table->n_columns);
	table->flexible = (gint) column;
}

/**
 * pk_console_table_add_row:
 * @table: a #PkConsoleTable
 * @first: the text of the first cell
 * @...: the text of the other cells, one per column
 *
 * Append a row. Cells that are %NULL are left empty.
 */
void
pk_console_table_add_row (PkConsoleTable *table, const gchar *first, ...)
{
	va_list args;
	PkConsoleTableCell *row = g_new0 (PkConsoleTableCell, table->n_columns);
	g_auto(GStrv) cells = NULL;

	va_start (args, first);
	cells = pk_console_table_collect_va (table, first, args);
	va_end (args);

	for (guint i = 0; i < table->n_columns; i++)
		row[i].text = g_steal_pointer (&cells[i]);
	g_ptr_array_add (table->rows, row);
}

/**
 * pk_console_table_set_color:
 * @table: a #PkConsoleTable
 * @column: the column index
 * @color: (nullable): an ANSI escape sequence
 *
 * Set the color of a cell in the most recently added row. The color is
 * only used if the table is rendered with colors enabled.
 */
void
pk_console_table_set_color (PkConsoleTable *table, guint column, const gchar *color)
{
	PkConsoleTableCell *row;

	g_return_if_fail (column < table->n_columns);
	g_return_if_fail (table->rows->len > 0);

	row = g_ptr_array_index (table->rows, table->rows->len - 1);
	g_free (row[column].color);
	row[column].color = g_strdup (color);
}

static void
pk_console_table_append_cell (GString *str,
			      const gchar *text,
			      const gchar *color,
			      guint width,
			      gboolean pad)
{
	guint text_width;

	if (text == NULL)
		text = "";
	text_width = pk_console_str_width (text);
	if (color != NULL && text[0] != '\0')
		g_string_append_printf (str, "%s%s\033[0m", color, text);
	else
		g_string_append (str, text);

	for (guint i = text_width; pad && i < width; i++)
		g_string_append_c (str, ' ');
}

/**
 * pk_console_table_render:
 * @table: a #PkConsoleTable
 * @max_width: the maximum line width, e.g. from pk_console_get_width()
 * @colors: %TRUE to use colors and a bold header
 *
 * Render the table. If it would be wider than @max_width, the flexible
 * column is truncated, down to a minimum width.
 *
 * Returns: (transfer full): the rendered table, every line ending in a newline
 */
gchar *
pk_console_table_render (PkConsoleTable *table, guint max_width, gboolean colors)
{
	GString *str = g_string_new (NULL);
	g_autofree guint *widths = g_new0 (guint, table->n_columns);
	g_autofree gboolean *visible = g_new0 (gboolean, table->n_columns);
	gint last_visible = -1;
	guint total;

	/* calculate column widths, a column only exists if it has content */
	for (guint i = 0; i < table->rows->len; i++) {
		PkConsoleTableCell *row = g_ptr_array_index (table->rows, i);
		for (guint j = 0; j < table->n_columns; j++) {
			if (row[j].text[0] == '\0')
				continue;
			visible[j] = TRUE;
			widths[j] = MAX (widths[j], pk_console_str_width (row[j].text));
		}
	}
	for (guint j = 0; j < table->n_columns; j++) {
		if (!visible[j])
			continue;
		if (table->header != NULL)
			widths[j] = MAX (widths[j], pk_console_str_width (table->header[j]));
		last_visible = (gint) j;
	}
	if (last_visible < 0)
		return g_string_free (str, FALSE);

	/* shrink the flexible column if we are too wide */
	total = table->indent;
	for (guint j = 0; j < table->n_columns; j++) {
		if (visible[j])
			total += widths[j] + (j == (guint) last_visible ? 0 : 2);
	}
	if (total > max_width && table->flexible >= 0 && visible[table->flexible]) {
		guint excess = total - max_width;
		guint min_width = MIN (widths[table->flexible], PK_CONSOLE_TABLE_MIN_FLEXIBLE);
		widths[table->flexible] = MAX (
		    min_width,
		    widths[table->flexible] > excess ? widths[table->flexible] - excess : 0);
	}

	for (gint i = table->header != NULL ? -1 : 0; i < (gint) table->rows->len; i++) {
		PkConsoleTableCell *row = i >= 0 ? g_ptr_array_index (table->rows, i) : NULL;
		gint last_filled = -1;
		gboolean first = TRUE;

		/* do not pad the last cell of a line with spaces */
		for (guint j = 0; j < table->n_columns; j++) {
			const gchar *text = row != NULL ? row[j].text : table->header[j];
			if (visible[j] && text[0] != '\0')
				last_filled = (gint) j;
		}

		for (guint j = 0; j < table->indent; j++)
			g_string_append_c (str, ' ');
		if (row == NULL && colors)
			g_string_append (str, "\033[1m");

		for (guint j = 0; j < table->n_columns && (gint) j <= last_filled; j++) {
			g_autofree gchar *truncated = NULL;
			const gchar *text;
			const gchar *color = NULL;

			if (!visible[j])
				continue;
			if (!first)
				g_string_append (str, "  ");
			first = FALSE;

			text = row != NULL ? row[j].text : table->header[j];
			if (row != NULL && colors)
				color = row[j].color;
			if ((gint) j == table->flexible) {
				truncated = pk_console_text_truncate (text, widths[j]);
				text = truncated;
			}
			pk_console_table_append_cell (str,
						      text,
						      color,
						      widths[j],
						      (gint) j < last_filled);
		}

		if (row == NULL && colors)
			g_string_append (str, "\033[0m");
		g_string_append_c (str, '\n');
	}

	return g_string_free (str, FALSE);
}

/**
 * pk_console_get_number:
 * @question: question to ask user
 * @maxnum: maximum number allowed
 *
 * Get a number from the console.
 *
 * Return value: a number entered by the user.
 **/
guint
pk_console_get_number (const gchar *question, guint maxnum)
{
	gint answer = 0;
	gint retval;

	/* pretty print */
	g_print ("%s", question);

	do {
		char buffer[64];

		/* swallow the \n at end of line too */
		if (!fgets (buffer, sizeof (buffer), stdin))
			break;
		if (strlen (buffer) == sizeof (buffer) - 1)
			continue;

		/* get a number */
		retval = sscanf (buffer, "%u", &answer);

		/* positive */
		if (retval == 1 && answer > 0 && answer <= (gint) maxnum)
			break;
		g_print (_("Please enter a number from 1 to %i: "), maxnum);
	} while (TRUE);
	return answer;
}

/*
 * pk_readline_unbuffered:
 **/
static GString *
pk_readline_unbuffered (const gchar *prompt)
{
	const gchar *tty_name;
	FILE *tty;
	GString *str = NULL;
	struct termios ts, ots;

	tty_name = ctermid (NULL);
	if (tty_name == NULL) {
		g_warning ("Cannot get terminal: %s", strerror (errno));
		goto out;
	}

	tty = fopen (tty_name, "r+");
	if (tty == NULL) {
		g_warning ("Error opening terminal for the process (`%s'): %s",
			   tty_name,
			   strerror (errno));
		goto out;
	}

	fprintf (tty, "%s", prompt);
	fflush (tty);
	setbuf (tty, NULL);

	/* based on polkitagenttextlistener.c, but use TCSADRAIN rather than
	 * TCSAFLUSH: we only toggle ECHONL here, so there is no reason to discard
	 * input that has already been queued. TCSAFLUSH would race with anything
	 * that answers the prompt the moment it appears (e.g. the test harness or
	 * piped input) and silently drop the reply, leaving us blocked in getc(). */
	tcgetattr (fileno (tty), &ts);
	ots = ts;
	ts.c_lflag &= ~(ECHONL);
	tcsetattr (fileno (tty), TCSADRAIN, &ts);

	str = g_string_new (NULL);
	while (TRUE) {
		gint c;
		c = getc (tty);
		if (c == '\n') {
			/* ok, done */
			break;
		} else if (c == EOF) {
			g_warning ("Got unexpected EOF.");
			break;
		} else {
			gchar c_safe = (gchar) c;
			g_string_append_len (str, (const gchar *) &c_safe, 1);
		}
	}
	tcsetattr (fileno (tty), TCSAFLUSH, &ots);
	putc ('\n', tty);

	fclose (tty);
out:
	return str;
}

static gboolean
pk_console_answer_is (const gchar *answer, const gchar *english, const gchar *translated)
{
	g_autofree gchar *answer_folded = NULL;
	g_autofree gchar *translated_folded = NULL;
	g_autofree gchar *answer_normalized = NULL;
	g_autofree gchar *translated_normalized = NULL;

	/* always accept the English answer, so scripts keep working */
	if (strcasecmp (answer, english) == 0)
		return TRUE;

	if (!g_utf8_validate (answer, -1, NULL))
		return FALSE;
	answer_folded = g_utf8_casefold (answer, -1);
	translated_folded = g_utf8_casefold (translated, -1);
	answer_normalized = g_utf8_normalize (answer_folded, -1, G_NORMALIZE_DEFAULT);
	translated_normalized = g_utf8_normalize (translated_folded, -1, G_NORMALIZE_DEFAULT);
	return g_strcmp0 (answer_normalized, translated_normalized) == 0;
}

/**
 * pk_console_get_prompt:
 * @question: question to ask user
 * @defaultyes: %TRUE if the default answer is yes
 *
 * Prompt the user with a yes/no question.
 *
 * Return value: %TRUE if the user accepted the question.
 **/
gboolean
pk_console_get_prompt (const gchar *question, gboolean defaultyes)
{
	gboolean ret = FALSE;
	gboolean valid = FALSE;
	gchar *prompt;
	GString *string;

	/* TRANSLATORS: shown after a yes/no question, the capital letter is the
	 * default answer; the letters must match the translated "y" and "n" answers */
	prompt = g_strdup_printf ("%s %s ", question, defaultyes ? _("[Y/n]") : _("[N/y]"));
	while (!valid) {
		string = pk_readline_unbuffered (prompt);
		if (string == NULL)
			break;
		if (string->len == 0) {
			if (defaultyes) {
				valid = TRUE;
				ret = TRUE;
			} else {
				valid = TRUE;
				ret = FALSE;
			}
		}
		if (pk_console_answer_is (string->str,
					  "y",
					  /* TRANSLATORS: short "yes" answer, must match [Y/n] */
					  C_ ("Answer", "y")) ||
		    pk_console_answer_is (string->str,
					  "yes",
					  /* TRANSLATORS: full "yes" answer */
					  C_ ("Answer", "yes"))) {
			valid = TRUE;
			ret = TRUE;
		}
		if (pk_console_answer_is (string->str,
					  "n",
					  /* TRANSLATORS: short "no" answer, must match [N/y] */
					  C_ ("Answer", "n")) ||
		    pk_console_answer_is (string->str,
					  "no",
					  /* TRANSLATORS: full "no" answer */
					  C_ ("Answer", "no"))) {
			valid = TRUE;
			ret = FALSE;
		}
		g_string_free (string, TRUE);
	}
	g_free (prompt);
	return ret;
}

/**
 * pk_status_enum_to_localised_text:
 * @status: a #PkStatusEnum
 *
 * Convert a #PkStatusEnum to a localized string suitable for showing to a user.
 *
 * Return value: a localized string.
 **/
const gchar *
pk_status_enum_to_localised_text (PkStatusEnum status)
{
	const gchar *text = NULL;
	switch (status) {
	case PK_STATUS_ENUM_UNKNOWN:
		/* TRANSLATORS: This is when the transaction status is not known */
		text = _("Unknown state");
		break;
	case PK_STATUS_ENUM_SETUP:
		/* TRANSLATORS: transaction state, the daemon is in the process of starting */
		text = _("Starting");
		break;
	case PK_STATUS_ENUM_WAIT:
		/* TRANSLATORS: transaction state, the transaction is waiting for another to complete */
		text = _("Waiting in queue");
		break;
	case PK_STATUS_ENUM_RUNNING:
		/* TRANSLATORS: transaction state, just started */
		text = _("Running");
		break;
	case PK_STATUS_ENUM_QUERY:
		/* TRANSLATORS: transaction state, is querying data */
		text = _("Querying");
		break;
	case PK_STATUS_ENUM_INFO:
		/* TRANSLATORS: transaction state, getting data from a server */
		text = _("Getting information");
		break;
	case PK_STATUS_ENUM_REMOVE:
		/* TRANSLATORS: transaction state, removing packages */
		text = _("Removing packages");
		break;
	case PK_STATUS_ENUM_DOWNLOAD:
		/* TRANSLATORS: transaction state, downloading package files */
		text = _("Downloading packages");
		break;
	case PK_STATUS_ENUM_INSTALL:
		/* TRANSLATORS: transaction state, installing packages */
		text = _("Installing packages");
		break;
	case PK_STATUS_ENUM_REFRESH_CACHE:
		/* TRANSLATORS: transaction state, refreshing internal lists */
		text = _("Refreshing software list");
		break;
	case PK_STATUS_ENUM_UPDATE:
		/* TRANSLATORS: transaction state, installing updates */
		text = _("Installing updates");
		break;
	case PK_STATUS_ENUM_CLEANUP:
		/* TRANSLATORS: transaction state, removing old packages, and cleaning config files */
		text = _("Cleaning up packages");
		break;
	case PK_STATUS_ENUM_OBSOLETE:
		/* TRANSLATORS: transaction state, obsoleting old packages */
		text = _("Obsoleting packages");
		break;
	case PK_STATUS_ENUM_DEP_RESOLVE:
		/* TRANSLATORS: transaction state, checking the transaction before we do it */
		text = _("Resolving dependencies");
		break;
	case PK_STATUS_ENUM_SIG_CHECK:
		/* TRANSLATORS: transaction state, checking if we have all the security keys for the operation */
		text = _("Checking signatures");
		break;
	case PK_STATUS_ENUM_TEST_COMMIT:
		/* TRANSLATORS: transaction state, when we're doing a test transaction */
		text = _("Testing changes");
		break;
	case PK_STATUS_ENUM_COMMIT:
		/* TRANSLATORS: transaction state, when we're writing to the system package database */
		text = _("Committing changes");
		break;
	case PK_STATUS_ENUM_REQUEST:
		/* TRANSLATORS: transaction state, requesting data from a server */
		text = _("Requesting data");
		break;
	case PK_STATUS_ENUM_FINISHED:
		/* TRANSLATORS: transaction state, all done! */
		text = _("Finished");
		break;
	case PK_STATUS_ENUM_CANCEL:
		/* TRANSLATORS: transaction state, in the process of cancelling */
		text = _("Cancelling");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading repository information");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading list of packages");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading file lists");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading lists of changes");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
		/* TRANSLATORS: transaction state, downloading metadata */
		text = _("Downloading update information");
		break;
	case PK_STATUS_ENUM_REPACKAGING:
		/* TRANSLATORS: transaction state, repackaging delta files */
		text = _("Repackaging files");
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
		/* TRANSLATORS: transaction state, loading databases */
		text = _("Loading cache");
		break;
	case PK_STATUS_ENUM_WAITING_FOR_LOCK:
		/* TRANSLATORS: transaction state, when we're waiting for the native tools to exit */
		text = _("Waiting for package manager lock");
		break;
	case PK_STATUS_ENUM_WAITING_FOR_AUTH:
		/* TRANSLATORS: transaction state, waiting for user to type in a password */
		text = _("Waiting for authentication");
		break;
	case PK_STATUS_ENUM_COPY_FILES:
		/* TRANSLATORS: transaction state, we are copying package files before or after the transaction */
		text = _("Copying files");
		break;
	case PK_STATUS_ENUM_RUN_HOOK:
		/* TRANSLATORS: transaction state, we are running hooks before or after the transaction */
		text = _("Running hooks");
		break;
	default:
		g_warning ("status unrecognised: %s", pk_status_enum_to_string (status));
	}
	return text;
}
