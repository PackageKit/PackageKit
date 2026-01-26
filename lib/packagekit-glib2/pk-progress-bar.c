/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2024-2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib.h>
#include <locale.h>
#include <langinfo.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "pk-progress-bar.h"
#include "pk-console-private.h"

typedef struct {
	guint			 position;
	gboolean		 move_forward;
} PkProgressBarPulseState;

struct PkProgressBarPrivate
{
	guint			 size;
	gint			 percentage;
	guint			 timer_id;
	PkProgressBarPulseState	 pulse_state;
	gint			 tty_fd;
	gchar			*old_start_text;
	guint			 term_width;
	gboolean		 use_unicode;

	gboolean		 allow_restart;
};

#define PK_PROGRESS_BAR_PERCENTAGE_INVALID	101
#define PK_PROGRESS_BAR_PULSE_TIMEOUT		40 /* ms */
#define PK_PROGRESS_BAR_DEFAULT_SIZE		30

/* space for percentage text (e.g., " 100%") or spacing */
static const guint PK_PERCENT_TEXT_WIDTH = 5;

G_DEFINE_TYPE_WITH_PRIVATE (PkProgressBar, pk_progress_bar, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (pk_progress_bar_get_instance_private (o))

/**
 * pk_progress_bar_get_terminal_width:
 */
static guint
pk_progress_bar_get_terminal_width (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	struct winsize w;

	if (priv->tty_fd < 0)
		return 80;

	if (ioctl (priv->tty_fd, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
		return w.ws_col;

	return 80;
}

/**
 * pk_progress_bar_console:
 */
static void
pk_progress_bar_console (PkProgressBar *self, const gchar *tmp)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	size_t count;
	ssize_t written;

	if (priv->tty_fd < 0)
		return;

	count = strlen (tmp);
	if (count == 0)
		return;

	written = write (priv->tty_fd, tmp, count);
	if (written < 0 || (size_t)written != count) {
		g_warning ("Only wrote %" G_GSSIZE_FORMAT
			   " of %" G_GSSIZE_FORMAT " bytes",
			   written, (gssize)count);
	}
}

/**
 * pk_progress_bar_set_padding:
 * @progress_bar: a valid #PkProgressBar instance
 * @padding: minimum size of progress bar text.
 *
 * Set minimum size of progress bar text - it will be padded with spaces to meet this requirement.
 *
 * This function is deprecated as of PackageKit 1.3.3 and has no effect.
 * Since this release, the progress bar text is automatically adjusted to fit the terminal width
 * and the progress bar is right-aligned in the terminal..
 *
 * Return value: %TRUE if changed
 */
gboolean
pk_progress_bar_set_padding (PkProgressBar *progress_bar, guint padding)
{
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);
	g_return_val_if_fail (padding < 1000, FALSE);

	return TRUE;
}

/**
 * pk_progress_bar_set_size:
 * @progress_bar: a valid #PkProgressBar instance
 * @size: width of progress bar in characters.
 *
 * Set the width of the progress bar.
 *
 * Return value: %TRUE if changed
 */
gboolean
pk_progress_bar_set_size (PkProgressBar *progress_bar, guint size)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);
	g_return_val_if_fail (size > 0 && size < G_MAXINT, FALSE);

	priv->size = size;
	return TRUE;
}

/**
 * pk_progress_bar_draw:
 */
static gboolean
pk_progress_bar_draw (PkProgressBar *self, gint percentage)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) str = NULL;
	guint term_width;
	guint available_width;
	guint bar_width;
	guint text_width;

	/* no value yet */
	if (percentage == G_MININT)
		return FALSE;

	/* clamp percentage */
	if (percentage < 0)
		percentage = 0;
	if (percentage > 100)
		percentage = 100;

	str = g_string_sized_new (256);

	/* move cursor to start of line and clear it */
	g_string_append (str, "\r\033[K");

	term_width = pk_progress_bar_get_terminal_width (self);

	/* calculate available width for text + bar */
	available_width = term_width > PK_PERCENT_TEXT_WIDTH ? term_width - PK_PERCENT_TEXT_WIDTH : term_width;

	/* determine bar width (use configured size or auto-calculate) */
	bar_width = priv->size;
	if (bar_width > available_width / 2)
		bar_width = available_width / 2;
	if (bar_width < 10)
		bar_width = 10;

	/* text width is what's left */
	if (available_width > bar_width + 3)
		text_width = available_width - bar_width - 3; /* -3 for space and brackets */
	else
		text_width = 0;

	/* truncate and pad the current text to exact width */
	if (priv->old_start_text != NULL && text_width > 0) {
		g_autofree gchar *truncated = NULL;
		g_autofree gchar *display_text = NULL;
		truncated = pk_console_text_truncate (priv->old_start_text, text_width);
		display_text = pk_console_strpad (truncated, text_width);
		g_string_append (str, display_text);
	} else {
		gsize old_len = str->len;
		g_string_set_size(str, old_len + text_width);
		memset(str->str + old_len, ' ', text_width);
		str->str[str->len] = '\0';
	}

	if (priv->use_unicode) {
		/* use Unicode block characters: █ = full, ▓ = 3/4, ▒ = 1/2, ░ = 1/4 */
		guint filled_chars = (percentage * bar_width) / 100;
		guint remainder = (percentage * bar_width) % 100;

		g_string_append (str, " [");

		/* full blocks */
		for (guint i = 0; i < filled_chars; i++)
			g_string_append (str, "█");

		/* partial block based on remainder */
		if (filled_chars < bar_width) {
			if (remainder >= 75)
				g_string_append (str, "▓");
			else if (remainder >= 50)
				g_string_append (str, "▒");
			else if (remainder >= 25)
				g_string_append (str, "░");
			else
				g_string_append (str, " ");
			filled_chars++;
		}

		/* empty space */
		for (guint i = filled_chars; i < bar_width; i++)
			g_string_append (str, " ");

		g_string_append (str, "]");
	} else {
		/* fallback to ASCII */
		guint filled = (percentage * bar_width) / 100;

		g_string_append (str, " [");
		for (guint i = 0; i < filled; i++)
			g_string_append (str, "=");
		for (guint i = filled; i < bar_width; i++)
			g_string_append (str, " ");
		g_string_append (str, "]");
	}

	/* percentage text */
	g_string_append_printf (str, " %3d%%", percentage);

	pk_progress_bar_console (self, str->str);

	return TRUE;
}

/**
 * pk_progress_bar_pulse_bar:
 */
static gboolean
pk_progress_bar_pulse_bar (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GString) str = NULL;
	guint term_width;
	guint available;
	guint bar_width;
	guint text_width;

	/* update position */
	if (priv->pulse_state.move_forward) {
		if (priv->pulse_state.position >= priv->size - 2)
			priv->pulse_state.move_forward = FALSE;
		else
			priv->pulse_state.position++;
	} else {
		if (priv->pulse_state.position <= 1)
			priv->pulse_state.move_forward = TRUE;
		else
			priv->pulse_state.position--;
	}

	str = g_string_sized_new (256);

	/* move cursor to start of line and clear it */
	g_string_append (str, "\r\033[K");

	/* calculate dimensions */
	term_width = pk_progress_bar_get_terminal_width (self);
	available = term_width > PK_PERCENT_TEXT_WIDTH ? term_width - PK_PERCENT_TEXT_WIDTH : term_width;

	bar_width = priv->size;
	if (bar_width > available / 2)
		bar_width = available / 2;
	if (bar_width < 10)
		bar_width = 10;

	if (available > bar_width + 3)
		text_width = available - bar_width - 3;
	else
		text_width = 0;

	/* truncate and pad the current text to exact width */
	if (priv->old_start_text != NULL && text_width > 0) {
		g_autofree gchar *truncated = NULL;
		g_autofree gchar *display_text = NULL;
		truncated = pk_console_text_truncate (priv->old_start_text, text_width);
		display_text = pk_console_strpad (truncated, text_width);
		g_string_append (str, display_text);
	} else {
		gsize old_len = str->len;
		g_string_set_size(str, old_len + text_width);
		memset(str->str + old_len, ' ', text_width);
		str->str[str->len] = '\0';
	}

	g_string_append (str, " [");
	if (priv->use_unicode) {
		for (guint i = 0; i < bar_width; i++) {
			if (i == priv->pulse_state.position)
				g_string_append (str, "▓");
			else if (i == priv->pulse_state.position - 1 || i == priv->pulse_state.position + 1)
				g_string_append (str, "░");
			else
				g_string_append (str, " ");
		}
	} else {
		/* ASCII fallback */
		for (guint i = 0; i < bar_width; i++) {
			if (i == priv->pulse_state.position || i == priv->pulse_state.position + 1)
				g_string_append (str, "=");
			else
				g_string_append (str, " ");
		}
	}
	g_string_append (str, "]");

	/* show percentage if available */
	if (priv->percentage >= 0 && priv->percentage <= 100 &&
	    priv->percentage != PK_PROGRESS_BAR_PERCENTAGE_INVALID) {
		g_string_append_printf (str, " %3d%%", priv->percentage);
	} else {
		g_string_append (str, "     ");
	}

	pk_progress_bar_console (self, str->str);

	return TRUE;
}

/**
 * pk_progress_bar_draw_pulse_bar:
 */
static void
pk_progress_bar_draw_pulse_bar (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);

	/* have we already got a pulse timer? */
	if (priv->timer_id != 0)
		return;

	priv->pulse_state.position = 1;
	priv->pulse_state.move_forward = TRUE;
	priv->timer_id = g_timeout_add (PK_PROGRESS_BAR_PULSE_TIMEOUT,
					G_SOURCE_FUNC (pk_progress_bar_pulse_bar), self);
	g_source_set_name_by_id (priv->timer_id, "[PkProgressBar] pulse");
}

/**
 * pk_progress_bar_set_percentage:
 * @progress_bar: a valid #PkProgressBar instance
 * @percentage: percentage value to set (0-100).
 *
 * Set the percentage value of the progress bar.
 *
 * Return value: %TRUE if changed
 */
gboolean
pk_progress_bar_set_percentage (PkProgressBar *progress_bar, gint percentage)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);
	g_return_val_if_fail (percentage <= PK_PROGRESS_BAR_PERCENTAGE_INVALID, FALSE);

	/* never called pk_progress_bar_start() */
	if (priv->percentage == G_MININT)
		pk_progress_bar_start (progress_bar, "FIXME: need to call pk_progress_bar_start() earlier!");

	/* check for old percentage */
	if (percentage == priv->percentage) {
		g_debug ("skipping as the same");
		goto out;
	}

	/* save */
	priv->percentage = percentage;

	/* either pulse or display */
	if (percentage < 0 || percentage > 100) {
		pk_progress_bar_draw_pulse_bar (progress_bar);
	} else {
		g_clear_handle_id (&priv->timer_id, g_source_remove);
		pk_progress_bar_draw (progress_bar, percentage);
	}
out:
	return TRUE;
}

/**
 * pk_progress_bar_start:
 * @progress_bar: a valid #PkProgressBar instance
 * @text: text to show in progress bar.
 *
 * Start showing progress.
 *
 * Return value: %TRUE if progress bar started
 */
gboolean
pk_progress_bar_start (PkProgressBar *progress_bar, const gchar *text)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);

	/* same as last time */
	if (priv->old_start_text != NULL && text != NULL) {
		if (g_strcmp0 (priv->old_start_text, text) == 0)
			return TRUE;
	}

	/* finish old progress bar if exists */
	if (priv->percentage != G_MININT) {
		pk_progress_bar_draw (progress_bar, 100);
		if (!priv->allow_restart)
			pk_progress_bar_console (progress_bar, "\n");
	}

	g_free (priv->old_start_text);
	priv->old_start_text = g_strdup (text);

	/* reset */
	priv->percentage = 0;
	pk_progress_bar_draw (progress_bar, 0);

	return TRUE;
}

/**
 * pk_progress_bar_end:
 * @progress_bar: a valid #PkProgressBar instance
 *
 * Stop showing progress.
 *
 * Return value: %TRUE if progress bar stopped
 */
gboolean
pk_progress_bar_end (PkProgressBar *progress_bar)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);
	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);

	/* never drawn */
	if (priv->percentage == G_MININT)
		return FALSE;

	/* stop pulse timer if running */
	g_clear_handle_id (&priv->timer_id, g_source_remove);

	/* draw final state and newline */
	priv->percentage = G_MININT;
	pk_progress_bar_draw (progress_bar, 100);
	pk_progress_bar_console (progress_bar, "\n");

	return TRUE;
}

/**
 * pk_progress_bar_set_allow_restart:
 * @progress_bar: a valid #PkProgressBar instance
 * @allow_restart: whether restarting is allowed
 *
 * Set whether the progress bar can be restarted, changing its status text
 * instead of creating a new progress bar.
 * If set to %FALSE, calling %pk_progress_bar_start() on a running progress
 * bar will create a new one, otherwise the existing one will be overridden.
 *
 */
void
pk_progress_bar_set_allow_restart (PkProgressBar* progress_bar, gboolean allow_restart)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);
	g_return_if_fail (PK_IS_PROGRESS_BAR (progress_bar));
	priv->allow_restart = allow_restart;
}

/**
 * pk_progress_bar_finalize:
 */
static void
pk_progress_bar_finalize (GObject *object)
{
	PkProgressBar *self = PK_PROGRESS_BAR (object);
	PkProgressBarPrivate *priv = GET_PRIVATE(self);

	g_clear_pointer (&priv->old_start_text, g_free);
	g_clear_handle_id (&priv->timer_id, g_source_remove);
	if (priv->tty_fd >= 0)
		close (priv->tty_fd);

	G_OBJECT_CLASS (pk_progress_bar_parent_class)->finalize (object);
}

/**
 * pk_progress_bar_class_init:
 */
static void
pk_progress_bar_class_init (PkProgressBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_progress_bar_finalize;
}

/**
 * pk_progress_bar_init:
 */
static void
pk_progress_bar_init (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	const char *codeset;

	self->priv = priv;
	priv->size = PK_PROGRESS_BAR_DEFAULT_SIZE;
	priv->percentage = G_MININT;
	priv->timer_id = 0;
	priv->term_width = 80;

	/* check if we can use Unicode */
	codeset = nl_langinfo (CODESET);
	priv->use_unicode = codeset != NULL &&
						(g_ascii_strcasecmp (codeset, "UTF-8") == 0 ||
						 g_ascii_strcasecmp (codeset, "utf8") == 0);

	/* try to open TTY */
	priv->tty_fd = open ("/dev/tty", O_RDWR, 0);
	if (priv->tty_fd < 0)
		priv->tty_fd = open ("/dev/console", O_RDWR, 0);
	if (priv->tty_fd < 0)
		priv->tty_fd = open ("/dev/stdout", O_RDWR, 0);

	/* get initial terminal width */
	if (priv->tty_fd >= 0)
		priv->term_width = pk_progress_bar_get_terminal_width (self);
}

/**
 * pk_progress_bar_new:
 *
 * #PkProgressBar is a console text progress bar.
 *
 * Return value: A new #PkProgressBar instance
 */
PkProgressBar *
pk_progress_bar_new (void)
{
	PkProgressBar *self;
	self = g_object_new (PK_TYPE_PROGRESS_BAR, NULL);
	return PK_PROGRESS_BAR (self);
}
