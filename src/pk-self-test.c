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

#include "config.h"
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "pk-debug.h"
#include "pk-self-test.h"

gboolean
pk_st_start (PkSelfTest *test, const gchar *name, PkSelfTestClass class)
{
	if (class == CLASS_AUTO && test->class == CLASS_MANUAL) {
		return FALSE;
	}
	if (class == CLASS_MANUAL && test->class == CLASS_AUTO) {
		return FALSE;
	}
	if (test->started == TRUE) {
		g_print ("Not ended test! Cannot start!\n");
		exit (1);
	}
	test->type = g_strdup (name);
	test->started = TRUE;
	if (test->level == LEVEL_NORMAL) {
		g_print ("%s...", test->type);
	}
	return TRUE;
}

void
pk_st_end (PkSelfTest *test)
{
	if (test->started == FALSE) {
		g_print ("Not started test! Cannot finish!\n");
		exit (1);
	}
	if (test->level == LEVEL_NORMAL) {
		g_print ("OK\n");
	}
	test->started = FALSE;
	g_free (test->type);
}

void
pk_st_title (PkSelfTest *test, const gchar *format, ...)
{
	va_list args;
	gchar va_args_buffer [1025];
	if (test->level == LEVEL_ALL) {
		va_start (args, format);
		g_vsnprintf (va_args_buffer, 1024, format, args);
		va_end (args);
		g_print ("> check #%u\t%s: \t%s...", test->total+1, test->type, va_args_buffer);
	}
	test->total++;
}

void
pk_st_success (PkSelfTest *test, const gchar *format, ...)
{
	va_list args;
	gchar va_args_buffer [1025];
	if (test->level == LEVEL_ALL) {
		if (format == NULL) {
			g_print ("...OK\n");
			goto finish;
		}
		va_start (args, format);
		g_vsnprintf (va_args_buffer, 1024, format, args);
		va_end (args);
		g_print ("...OK [%s]\n", va_args_buffer);
	}
finish:
	test->succeeded++;
}

void
pk_st_failed (PkSelfTest *test, const gchar *format, ...)
{
	va_list args;
	gchar va_args_buffer [1025];
	if (test->level == LEVEL_ALL || test->level == LEVEL_NORMAL) {
		if (format == NULL) {
			g_print ("FAILED\n");
			goto failed;
		}
		va_start (args, format);
		g_vsnprintf (va_args_buffer, 1024, format, args);
		va_end (args);
		g_print ("FAILED [%s]\n", va_args_buffer);
	}
failed:
	exit (1);
}

static void
pk_st_run_test (PkSelfTest *test, PkSelfTestFunc func)
{
	func (test);
}

int
main (int argc, char **argv)
{
	GOptionContext  *context;
	int retval;

	gboolean verbose = FALSE;
	char *class = NULL;
	char *level = NULL;
	char **tests = NULL;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show verbose debugging information", NULL },
		{ "class", '\0', 0, G_OPTION_ARG_STRING, &class,
		  "Debug class, [manual|auto|all]", NULL },
		{ "level", '\0', 0, G_OPTION_ARG_STRING, &level,
		  "Set the printing level, [quiet|normal|all]", NULL },
		{ "tests", '\0', 0, G_OPTION_ARG_STRING_ARRAY, &tests,
		  "Debug specific modules, [common,webcam,arrayfloat]", NULL },
		{ NULL}
	};

verbose = TRUE;

	context = g_option_context_new ("GNOME Power Manager Self Test");
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_type_init ();

	pk_debug_init (verbose);

	PkSelfTest ttest;
	PkSelfTest *test = &ttest;
	test->total = 0;
	test->succeeded = 0;
	test->type = NULL;
	test->started = FALSE;
	test->class = CLASS_AUTO;
	test->level = LEVEL_ALL;

	if (class != NULL) {
		if (strcmp (class, "auto") == 0) {
			test->class = CLASS_AUTO;
		} else if (strcmp (class, "all") == 0) {
			test->class = CLASS_ALL;
		} else if (strcmp (class, "manual") == 0) {
			test->class = CLASS_MANUAL;
		} else {
			g_print ("Invalid class specified\n");
			exit (1);
		}
	}

	if (level != NULL) {
		if (strcmp (level, "quiet") == 0) {
			test->level = LEVEL_QUIET;
		} else if (strcmp (level, "normal") == 0) {
			test->level = LEVEL_NORMAL;
		} else if (strcmp (level, "all") == 0) {
			test->level = LEVEL_ALL;
		} else {
			g_print ("Invalid level specified\n");
			exit (1);
		}
	}

	/* auto */
	pk_st_run_test (test, pk_st_spawn);

	g_print ("test passes (%u/%u) : ", test->succeeded, test->total);
	if (test->succeeded == test->total) {
		g_print ("ALL OKAY\n");
		retval = 0;
	} else {
		g_print ("%u FAILURE(S)\n", test->total - test->succeeded);
		retval = 1;
	}

	g_option_context_free (context);
	return retval;
}

