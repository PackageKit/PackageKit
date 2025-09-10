/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2009 Richard Hughes <richard@hughsie.com>
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
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "pk-progress-bar.h"

typedef struct {
	guint			 position;
	gboolean		 move_forward;
} PkProgressBarPulseState;

struct PkProgressBarPrivate
{
	guint			 size;
	gint			 percentage;
	guint			 padding;
	guint			 timer_id;
	PkProgressBarPulseState	 pulse_state;
	gint			 tty_fd;
	gchar			*old_start_text;
};

#define PK_PROGRESS_BAR_PERCENTAGE_INVALID	101
#define PK_PROGRESS_BAR_PULSE_TIMEOUT		40 /* ms */

G_DEFINE_TYPE_WITH_PRIVATE (PkProgressBar, pk_progress_bar, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (pk_progress_bar_get_instance_private (o))

/*
 * pk_progress_bar_console:
 **/
static void
pk_progress_bar_console (PkProgressBar *self, const gchar *tmp)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	gssize count;
	gssize wrote;

	count = strlen (tmp) + 1;
	if (priv->tty_fd < 0)
		return;
	wrote = write (priv->tty_fd, tmp, count);
	if (wrote != count) {
		g_warning ("Only wrote %" G_GSSIZE_FORMAT
			   " of %" G_GSSIZE_FORMAT " bytes",
			   wrote, count);
	}
}

/**
 * pk_progress_bar_set_padding:
 * @progress_bar: a valid #PkProgressBar instance
 * @padding: minimum size of progress bar text.
 *
 * Set minimum size of progress bar text - it will be padded with spaces to meet this requirement.
 *
 * Return value: %TRUE if changed
 **/
gboolean
pk_progress_bar_set_padding (PkProgressBar *progress_bar, guint padding)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);
	g_return_val_if_fail (padding < 100, FALSE);

	priv->padding = padding;
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
 **/
gboolean
pk_progress_bar_set_size (PkProgressBar *progress_bar, guint size)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);
	g_return_val_if_fail (size < 100, FALSE);

	priv->size = size;
	return TRUE;
}

/*
 * pk_progress_bar_draw:
 **/
static gboolean
pk_progress_bar_draw (PkProgressBar *self, gint percentage)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	guint section;
	guint i;
	GString *str;

	/* no value yet */
	if (percentage == G_MININT)
		return FALSE;

	/* restore cursor */
	str = g_string_new ("");
	g_string_append_printf (str, "%c8", 0x1B);

	section = (guint) ((gfloat) priv->size / (gfloat) 100.0 * (gfloat) percentage);
	g_string_append (str, "[");
	for (i = 0; i < section; i++)
		g_string_append (str, "=");
	for (i = 0; i < priv->size - section; i++)
		g_string_append (str, " ");
	g_string_append (str, "] ");
	if (percentage >= 0 && percentage < 100) {
		/* TRANSLATORS: this is a percentage value we use in messages, e.g. "90%. */
		g_string_append_printf (str, _("(%i%%)"), percentage);
		g_string_append_printf (str, "  ");
	} else {
		g_string_append (str, "        ");
	}
	pk_progress_bar_console (self, str->str);
	g_string_free (str, TRUE);
	return TRUE;
}

/*
 * pk_progress_bar_pulse_bar:
 **/
static gboolean
pk_progress_bar_pulse_bar (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);
	gint i;
	GString *str;

	/* restore cursor */
	str = g_string_new ("");
	g_string_append_printf (str, "%c8", 0x1B);

	if (priv->pulse_state.move_forward) {
		if (priv->pulse_state.position == priv->size - 1)
			priv->pulse_state.move_forward = FALSE;
		else
			priv->pulse_state.position++;
	} else if (!priv->pulse_state.move_forward) {
		if (priv->pulse_state.position == 1)
			priv->pulse_state.move_forward = TRUE;
		else
			priv->pulse_state.position--;
	}

	g_string_append (str, "[");
	for (i = 0; i < (gint)priv->pulse_state.position-1; i++)
		g_string_append (str, " ");
	g_string_append (str, "==");
	for (i = 0; i < (gint) (priv->size - priv->pulse_state.position - 1); i++)
		g_string_append (str, " ");
	g_string_append (str, "] ");
	if (priv->percentage >= 0 && priv->percentage != PK_PROGRESS_BAR_PERCENTAGE_INVALID) {
		/* TRANSLATORS: this is a percentage value we use in messages, e.g. "90%. */
		g_string_append_printf (str, _("(%i%%)"), priv->percentage);
		g_string_append_printf (str, "  ");
	} else {
		g_string_append (str, "        ");
	}
	pk_progress_bar_console (self, str->str);
	g_string_free (str, TRUE);

	return TRUE;
}

/*
 * pk_progress_bar_draw_pulse_bar:
 **/
static void
pk_progress_bar_draw_pulse_bar (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);

	/* have we already got zero percent? */
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
 **/
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
		pk_progress_bar_draw (progress_bar, 0);
		pk_progress_bar_draw_pulse_bar (progress_bar);
	} else {
		g_clear_handle_id (&priv->timer_id, g_source_remove);
		pk_progress_bar_draw (progress_bar, percentage);
	}
out:
	return TRUE;
}

/*
 * pk_strpad:
 * @data: the input string
 * @length: the desired length of the output string, with padding
 *
 * Returns the text padded to a length with spaces. If the string is
 * longer than length then a longer string is returned.
 *
 * Return value: The padded string
 **/
static gchar *
pk_strpad (const gchar *data, guint length)
{
	gint size;
	guint data_len;
	gchar *text;
	gchar *padding;

	if (data == NULL)
		return g_strnfill (length, ' ');

	/* ITS4: ignore, only used for formatting */
	data_len = strlen (data);

	/* calculate */
	size = (length - data_len);
	if (size <= 0)
		return g_strdup (data);

	padding = g_strnfill (size, ' ');
	text = g_strdup_printf ("%s%s", data, padding);
	g_free (padding);
	return text;
}

/**
 * pk_progress_bar_start:
 * @progress_bar: a valid #PkProgressBar instance
 * @text: text to show in progress bar.
 *
 * Start showing progress.
 *
 * Return value: %TRUE if progress bar started
 **/
gboolean
pk_progress_bar_start (PkProgressBar *progress_bar, const gchar *text)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);
	gchar *text_pad;
	GString *str;

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);

	/* same as last time */
	if (priv->old_start_text != NULL && text != NULL) {
		if (g_strcmp0 (priv->old_start_text, text) == 0)
			return TRUE;
	}
	g_free (priv->old_start_text);
	priv->old_start_text = g_strdup (text);

	/* finish old value */
	str = g_string_new ("");
	if (priv->percentage != G_MININT) {
		pk_progress_bar_draw (progress_bar, 100);
		g_string_append (str, "\n");
	}

	/* make these all the same length */
	text_pad = pk_strpad (text, priv->padding);
	g_string_append (str, text_pad);

	/* save cursor in new position */
	g_string_append_printf (str, "%c7", 0x1B);
	pk_progress_bar_console (progress_bar, str->str);

	/* reset */
	if (priv->percentage == G_MININT)
		priv->percentage = 0;
	pk_progress_bar_draw (progress_bar, 0);

	g_string_free (str, TRUE);
	g_free (text_pad);
	return TRUE;
}

/**
 * pk_progress_bar_end:
 * @progress_bar: a valid #PkProgressBar instance
 *
 * Stop showing progress.
 *
 * Return value: %TRUE if progress bar stopped
 **/
gboolean
pk_progress_bar_end (PkProgressBar *progress_bar)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(progress_bar);
	GString *str;

	g_return_val_if_fail (PK_IS_PROGRESS_BAR (progress_bar), FALSE);

	/* never drawn */
	if (priv->percentage == G_MININT)
		return FALSE;

	priv->percentage = G_MININT;
	pk_progress_bar_draw (progress_bar, 100);
	str = g_string_new ("");
	g_string_append_printf (str, "\n");
	pk_progress_bar_console (progress_bar, str->str);
	g_string_free (str, TRUE);

	return TRUE;
}

/*
 * pk_progress_bar_finalize:
 **/
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

/*
 * pk_progress_bar_class_init:
 **/
static void
pk_progress_bar_class_init (PkProgressBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_progress_bar_finalize;
}

/*
 * pk_progress_bar_init:
 **/
static void
pk_progress_bar_init (PkProgressBar *self)
{
	PkProgressBarPrivate *priv = GET_PRIVATE(self);

	self->priv = priv;
	priv->size = 10;
	priv->percentage = G_MININT;
	priv->padding = 0;
	priv->timer_id = 0;
	priv->tty_fd = open ("/dev/tty", O_RDWR, 0);
	if (priv->tty_fd < 0)
		priv->tty_fd = open ("/dev/console", O_RDWR, 0);
	if (priv->tty_fd < 0)
		priv->tty_fd = open ("/dev/stdout", O_RDWR, 0);
}

/**
 * pk_progress_bar_new:
 *
 * #PkProgressBar is a console text progress bar.
 *
 * Return value: A new #PkProgressBar instance
 **/
PkProgressBar *
pk_progress_bar_new (void)
{
	PkProgressBar *self;
	self = g_object_new (PK_TYPE_PROGRESS_BAR, NULL);
	return PK_PROGRESS_BAR (self);
}
