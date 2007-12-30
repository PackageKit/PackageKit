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

/**
 * SECTION:pk-debug
 * @short_description: Debugging functions
 *
 * This file contains functions that can be used for debugging.
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

#include "pk-debug.h"

static gboolean do_verbose = FALSE;	/* if we should print out debugging */

/**
 * pk_print_line:
 **/
static void
pk_print_line (const gchar *func, const gchar *file, const int line, const gchar *buffer)
{
	gchar *str_time;
	time_t the_time;
	GThread *thread;

	time (&the_time);
	str_time = g_new0 (gchar, 255);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));
	thread = g_thread_self ();

	fprintf (stderr, "TI:%s\tTH:%p\tFI:%s\tFN:%s,%d\n - %s\n", str_time, thread, file, func, line, buffer);
	g_free (str_time);
}

/**
 * pk_debug_real:
 **/
void
pk_debug_real (const gchar *func, const gchar *file, const int line, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;

	if (do_verbose == FALSE) {
		return;
	}

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	pk_print_line (func, file, line, buffer);

	g_free(buffer);
}

/**
 * pk_warning_real:
 **/
void
pk_warning_real (const gchar *func, const gchar *file, const int line, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;

	if (do_verbose == FALSE) {
		return;
	}

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* flush other output */
	fflush (stdout);

	/* do extra stuff for a warning */
	fprintf (stderr, "*** WARNING ***\n");
	pk_print_line (func, file, line, buffer);

	g_free(buffer);
}

/**
 * pk_error_real:
 **/
void
pk_error_real (const gchar *func, const gchar *file, const int line, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* flush other output */
	fflush (stdout);

	/* do extra stuff for a warning */
	fprintf (stderr, "*** ERROR ***\n");
	pk_print_line (func, file, line, buffer);
	g_free(buffer);

	/* flush this message */
	fflush (stderr);
	exit (1);
}

/**
 * pk_debug_init:
 * @debug: If we should print out verbose logging
 **/
void
pk_debug_init (gboolean debug)
{
	do_verbose = debug;
	pk_debug ("Verbose debugging %s", (do_verbose) ? "enabled" : "disabled");
}

