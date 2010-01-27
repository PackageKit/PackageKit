/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <glib/gprintf.h>

#include "egg-test.h"

struct EggTest {
	guint		 total;
	guint		 succeeded;
	gboolean	 started;
	gboolean	 titled;
	gchar		*type;
	GTimer		*timer;
	GMainLoop	*loop;
	guint		 hang_loop_id;
	gpointer	 user_data;
};

/**
 * egg_test_init:
 **/
EggTest *
egg_test_init ()
{
	EggTest *test;
	test = g_new (EggTest, 1);
	test->total = 0;
	test->succeeded = 0;
	test->type = NULL;
	test->started = FALSE;
	test->titled = FALSE;
	test->timer = g_timer_new ();
	test->loop = g_main_loop_new (NULL, FALSE);
	test->hang_loop_id = 0;
	return test;
}

/**
 * egg_test_loop_quit:
 **/
void
egg_test_loop_quit (EggTest *test)
{
	/* disable the loop watch */
	if (test->hang_loop_id != 0) {
		g_source_remove (test->hang_loop_id);
		test->hang_loop_id = 0;
	}
	g_main_loop_quit (test->loop);
}

/**
 * egg_test_hang_check:
 **/
static gboolean
egg_test_hang_check (gpointer data)
{
	EggTest *test = (EggTest *) data;
	g_main_loop_quit (test->loop);
	return FALSE;
}

/**
 * egg_test_loop_wait:
 **/
void
egg_test_loop_wait (EggTest *test, guint timeout)
{
	test->hang_loop_id = g_timeout_add (timeout, egg_test_hang_check, test);
	g_main_loop_run (test->loop);
}

/**
 * egg_test_loop_check:
 **/
void
egg_test_loop_check (EggTest *test)
{
	guint elapsed = egg_test_elapsed (test);
	egg_test_title (test, "did we timeout out of the loop");
	if (test->hang_loop_id == 0) {
		egg_test_success (test, "loop blocked for %ims", elapsed);
	} else {
		egg_test_failed (test, "hangcheck saved us after %ims", elapsed);
	}
}

/**
 * egg_test_set_user_data:
 **/
void
egg_test_set_user_data (EggTest *test, gpointer user_data)
{
	test->user_data = user_data;
}

/**
 * egg_test_get_user_data:
 **/
gpointer
egg_test_get_user_data (EggTest *test)
{
	return test->user_data;
}

/**
 * egg_test_finish:
 **/
gint
egg_test_finish (EggTest *test)
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
	g_free (test);

	return retval;
}

/**
 * egg_test_elapsed:
 *
 * Returns: time in ms
 **/
guint
egg_test_elapsed (EggTest *test)
{
	gdouble time_s;
	time_s = g_timer_elapsed (test->timer, NULL);
	return (guint) (time_s * 1000.0f);
}

/**
 * egg_test_start:
 **/
gboolean
egg_test_start (EggTest *test, const gchar *name)
{
	if (test->started) {
		g_print ("Not ended test! Cannot start!\n");
		exit (1);
	}
	test->type = g_strdup (name);
	test->started = TRUE;
	return TRUE;
}

/**
 * egg_test_end:
 **/
void
egg_test_end (EggTest *test)
{
	if (test->started == FALSE) {
		g_print ("Not started test! Cannot finish!\n");
		exit (1);
	}

	/* disable hang check */
	if (test->hang_loop_id != 0) {
		g_source_remove (test->hang_loop_id);
		test->hang_loop_id = 0;
	}

	/* remove all the test callbacks */
	while (g_source_remove_by_user_data (test))
		g_print ("WARNING: removed callback for test module");

	/* check we don't have any pending iterations */
	if (g_main_context_pending (NULL)) {
		g_print ("WARNING: Pending event in context! Running to completion... ");
		while (g_main_context_pending (NULL))
			g_main_context_iteration (NULL, TRUE);
		g_print ("Done!\n");
	}

	test->started = FALSE;
	g_free (test->type);
}

/**
 * egg_test_title:
 **/
void
egg_test_title (EggTest *test, const gchar *format, ...)
{
	va_list args;
	gchar *va_args_buffer = NULL;

	/* already titled? */
	if (test->titled) {
		g_print ("Already titled!\n");
		exit (1);
	}

	/* reset the value egg_test_elapsed replies with */
	g_timer_reset (test->timer);

	va_start (args, format);
	g_vasprintf (&va_args_buffer, format, args);
	va_end (args);
	g_print ("> check #%u\t%s: \t%s...", test->total+1, test->type, va_args_buffer);
	g_free (va_args_buffer);

	test->titled = TRUE;
	test->total++;
}

/**
 * egg_test_success:
 **/
void
egg_test_success (EggTest *test, const gchar *format, ...)
{
	va_list args;
	gchar *va_args_buffer = NULL;

	/* not titled? */
	if (!test->titled) {
		g_print ("Not titled!\n");
		exit (1);
	}
	if (format == NULL) {
		g_print ("...OK\n");
		goto finish;
	}
	va_start (args, format);
	g_vasprintf (&va_args_buffer, format, args);
	va_end (args);
	g_print ("...OK [%s]\n", va_args_buffer);
	g_free (va_args_buffer);
finish:
	test->titled = FALSE;
	test->succeeded++;
}

/**
 * egg_test_failed:
 **/
void
egg_test_failed (EggTest *test, const gchar *format, ...)
{
	va_list args;
	gchar *va_args_buffer = NULL;

	/* not titled? */
	if (!test->titled) {
		g_print ("Not titled!\n");
		exit (1);
	}
	if (format == NULL) {
		g_print ("FAILED\n");
		goto failed;
	}
	va_start (args, format);
	g_vasprintf (&va_args_buffer, format, args);
	va_end (args);
	g_print ("FAILED [%s]\n", va_args_buffer);
	g_free (va_args_buffer);
failed:
	exit (1);
}

/**
 * egg_test_assert:
 **/
void
egg_test_assert (EggTest *test, gboolean value)
{
	if (value)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
}

/**
 * egg_test_title_assert:
 **/
void
egg_test_title_assert (EggTest *test, const gchar *text, gboolean value)
{
	egg_test_title (test, "%s", text);
	if (value)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
}

/**
 * egg_test_get_data_file:
 **/
gchar *
egg_test_get_data_file (const gchar *filename)
{
	gboolean ret;
	gchar *full;

	/* check to see if we are being run in the build root */
	full = g_build_filename ("..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		return full;
	g_free (full);

	/* check to see if we are being run in the build root */
	full = g_build_filename ("..", "..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		return full;
	g_free (full);

	/* check to see if we are being run in make check */
	full = g_build_filename ("..", "..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		return full;
	g_free (full);
	full = g_build_filename ("..", "..", "..", "data", "tests", filename, NULL);
	ret = g_file_test (full, G_FILE_TEST_EXISTS);
	if (ret)
		return full;
	g_print ("[WARN] failed to find '%s'\n", full);
	g_free (full);
	return NULL;
}

