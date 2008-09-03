/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <glib/gprintf.h>

#include "libselftest.h"

/**
 * libst_init:
 **/
void
libst_init (LibSelfTest *test)
{
	test->total = 0;
	test->succeeded = 0;
	test->type = NULL;
	test->started = FALSE;
	test->timer = g_timer_new ();
	test->loop = g_main_loop_new (NULL, FALSE);
	test->hang_loop_id = 0;
}

/**
 * libst_loopquit:
 **/
void
libst_loopquit (LibSelfTest *test)
{
	/* disable the loop watch */
	if (test->hang_loop_id != 0) {
		g_source_remove (test->hang_loop_id);
		test->hang_loop_id = 0;
	}
	g_main_loop_quit (test->loop);
}

/**
 * libst_hang_check:
 **/
static gboolean
libst_hang_check (gpointer data)
{
	LibSelfTest *test = (LibSelfTest *) data;
	g_main_loop_quit (test->loop);
	return FALSE;
}

/**
 * libst_loopwait:
 **/
void
libst_loopwait (LibSelfTest *test, guint timeout)
{
	test->hang_loop_id = g_timeout_add (timeout, libst_hang_check, test);
	g_main_loop_run (test->loop);
}

/**
 * libst_loopcheck:
 **/
void
libst_loopcheck (LibSelfTest *test)
{
	guint elapsed = libst_elapsed (test);
	libst_title (test, "did we timeout out of the loop");
	if (test->hang_loop_id == 0) {
		libst_success (test, "loop blocked for %ims", elapsed);
	} else {
		libst_failed (test, "hangcheck saved us after %ims", elapsed);
	}
}

/**
 * libst_set_user_data:
 **/
void
libst_set_user_data (LibSelfTest *test, gpointer user_data)
{
	test->user_data = user_data;
}

/**
 * libst_get_user_data:
 **/
gpointer
libst_get_user_data (LibSelfTest *test)
{
	return test->user_data;
}

/**
 * libst_finish:
 **/
gint
libst_finish (LibSelfTest *test)
{
	gint retval;
	g_print ("test passes (%u/%u) : ", test->succeeded, test->total);
	if (test->succeeded == test->total) {
		g_print ("ALL OKAY\n");
		retval = 0;
	} else {
		g_print ("%u FAILURE(S)\n", test->total - test->succeeded);
		retval = 1;
	}

	g_timer_destroy (test->timer);
	g_main_loop_unref (test->loop);

	return retval;
}

/**
 * libst_elapsed:
 *
 * Returns ms
 **/
guint
libst_elapsed (LibSelfTest *test)
{
	gdouble time;
	time = g_timer_elapsed (test->timer, NULL);
	return (guint) (time * 1000.0f);
}

/**
 * libst_start:
 **/
gboolean
libst_start (LibSelfTest *test, const gchar *name)
{
	if (test->started == TRUE) {
		g_print ("Not ended test! Cannot start!\n");
		exit (1);
	}
	test->type = g_strdup (name);
	test->started = TRUE;
	g_print ("%s...", test->type);
	return TRUE;
}

/**
 * libst_end:
 **/
void
libst_end (LibSelfTest *test)
{
	if (test->started == FALSE) {
		g_print ("Not started test! Cannot finish!\n");
		exit (1);
	}
	g_print ("OK\n");

	/* disable hang check */
	if (test->hang_loop_id != 0) {
		g_source_remove (test->hang_loop_id);
		test->hang_loop_id = 0;
	}

	test->started = FALSE;
	g_free (test->type);
}

/**
 * libst_title:
 **/
void
libst_title (LibSelfTest *test, const gchar *format, ...)
{
	va_list args;
	gchar *va_args_buffer = NULL;

	/* reset the value libst_elapsed replies with */
	g_timer_reset (test->timer);

	va_start (args, format);
	g_vasprintf (&va_args_buffer, format, args);
	va_end (args);
	g_print ("> check #%u\t%s: \t%s...", test->total+1, test->type, va_args_buffer);
	g_free(va_args_buffer);

	test->total++;
}

/**
 * libst_success:
 **/
void
libst_success (LibSelfTest *test, const gchar *format, ...)
{
	va_list args;
	gchar *va_args_buffer = NULL;

	if (format == NULL) {
		g_print ("...OK\n");
		goto finish;
	}
	va_start (args, format);
	g_vasprintf (&va_args_buffer, format, args);
	va_end (args);
	g_print ("...OK [%s]\n", va_args_buffer);
	g_free(va_args_buffer);
finish:
	test->succeeded++;
}

/**
 * libst_failed:
 **/
void
libst_failed (LibSelfTest *test, const gchar *format, ...)
{
	va_list args;
	gchar *va_args_buffer = NULL;
	if (format == NULL) {
		g_print ("FAILED\n");
		goto failed;
	}
	va_start (args, format);
	g_vasprintf (&va_args_buffer, format, args);
	va_end (args);
	g_print ("FAILED [%s]\n", va_args_buffer);
	g_free(va_args_buffer);
failed:
	exit (1);
}

/**
 * libst_get_data_file:
 **/
gchar *
libst_get_data_file (const gchar *filename)
{
	gboolean ret;
	gchar *full;

	/* check to see if we are being run in the build root */
	full = g_build_filename ("..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret) {
		return full;
	}
	g_free (full);

	/* check to see if we are being run in make check */
	full = g_build_filename ("..", "..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret) {
		return full;
	}
	g_free (full);
	return NULL;
}

