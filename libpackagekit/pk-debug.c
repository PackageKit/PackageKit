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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "pk-debug.h"

#define CONSOLE_RESET		0
#define CONSOLE_BLACK 		30
#define CONSOLE_RED		31
#define CONSOLE_GREEN		32
#define CONSOLE_YELLOW		33
#define CONSOLE_BLUE		34
#define CONSOLE_MAGENTA		35
#define CONSOLE_CYAN		36
#define CONSOLE_WHITE		37

#define PK_LOG_FILE		PK_LOG_DIR "/PackageKit"

static gboolean do_verbose = FALSE;	/* if we should print out debugging */
static gboolean do_logging = FALSE;	/* if we should write to a file */
static gboolean is_console = FALSE;
static gint fd = -1;

/**
 * pk_debug_set_logging:
 **/
void
pk_debug_set_logging (gboolean enabled)
{
	do_logging = enabled;
	if (enabled) {
		pk_debug ("now logging to %s", PK_LOG_FILE);
	}
}

/**
 * pk_set_console_mode:
 **/
static void
pk_set_console_mode (guint console_code)
{
	gchar command[13];

	/* don't put extra commands into logs */
	if (!is_console) {
		return;
	}
	/* Command is the control command to the terminal */
	sprintf (command, "%c[%dm", 0x1B, console_code);
	printf ("%s", command);
}

/**
 * pk_log_line:
 **/
static void
pk_log_line (const gchar *buffer)
{
	ssize_t count;
	/* open a file */
	if (fd == -1) {
		mkdir (PK_LOG_DIR, 0777);
		fd = open (PK_LOG_FILE, O_WRONLY|O_APPEND|O_CREAT, 0777);
		if (fd == -1) {
			g_error ("could not open log: '%s'", PK_LOG_FILE);
		}
	}
	/* whole line */
	count = write (fd, buffer, strlen (buffer));
	if (count == -1) {
		g_warning ("could not write %s", buffer);
	}
	/* newline */
	count = write (fd, "\n", 1);
	if (count == -1) {
		g_warning ("could not write newline");
	}
}

/**
 * pk_print_line:
 **/
static void
pk_print_line (const gchar *func, const gchar *file, const int line, const gchar *buffer, guint color)
{
	gchar *str_time;
	gchar *header;
	time_t the_time;
	GThread *thread;

	time (&the_time);
	str_time = g_new0 (gchar, 255);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));
	thread = g_thread_self ();

	/* generate header text */
	header = g_strdup_printf ("TI:%s\tTH:%p\tFI:%s\tFN:%s,%d", str_time, thread, file, func, line);
	g_free (str_time);

	/* always in light green */
	pk_set_console_mode (CONSOLE_GREEN);
	printf ("%s\n", header);

	/* different colours according to the severity */
	pk_set_console_mode (color);
	printf (" - %s\n", buffer);
	pk_set_console_mode (CONSOLE_RESET);

	/* log to a file */
	if (do_logging) {
		pk_log_line (header);
		pk_log_line (buffer);
	}

	/* flush this output, as we need to debug */
	fflush (stdout);

	g_free (header);
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

	pk_print_line (func, file, line, buffer, CONSOLE_BLUE);

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

	/* do extra stuff for a warning */
	if (!is_console) {
		printf ("*** WARNING ***\n");
	}
	pk_print_line (func, file, line, buffer, CONSOLE_RED);

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

	/* do extra stuff for a warning */
	if (!is_console) {
		printf ("*** ERROR ***\n");
	}
	pk_print_line (func, file, line, buffer, CONSOLE_RED);
	g_free(buffer);

	exit (1);
}

/**
 * pk_debug_enabled:
 *
 * Returns: TRUE if we have debugging enabled
 **/
gboolean
pk_debug_enabled (void)
{
	return do_verbose;
}

/**
 * pk_debug_init:
 * @debug: If we should print out verbose logging
 **/
void
pk_debug_init (gboolean debug)
{
	do_verbose = debug;
	/* check if we are on console */
	if (isatty (fileno (stdout)) == 1) {
		is_console = TRUE;
	}
	pk_debug ("Verbose debugging %i (on console %i)", do_verbose, is_console);
}

