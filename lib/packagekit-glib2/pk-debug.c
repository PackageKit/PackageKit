/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Richard Hughes <richard@hughsie.com>
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


#include <glib/gi18n.h>
#include <unistd.h>
#include <stdio.h>

#define __USE_GNU
#include <dlfcn.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <pk-debug.h>

#include "config.h"

static gboolean _verbose = FALSE;
static gboolean _console = FALSE;

/**
 * pk_debug_is_verbose:
 *
 * Returns: TRUE if we have debugging enabled
 **/
gboolean
pk_debug_is_verbose (void)
{
	/* local first */
	if (_verbose)
		 return TRUE;

	/* fall back to env variable */
	if (g_getenv ("VERBOSE") != NULL)
		 return TRUE;
	return FALSE;
}


/**
 * pk_debug_ignore_cb:
 **/
static void
pk_debug_ignore_cb (const gchar *log_domain, GLogLevelFlags log_level,
		    const gchar *message, gpointer user_data)
{
}

#define CONSOLE_RESET		0
#define CONSOLE_BLACK		30
#define CONSOLE_RED		31
#define CONSOLE_GREEN		32
#define CONSOLE_YELLOW		33
#define CONSOLE_BLUE		34
#define CONSOLE_MAGENTA		35
#define CONSOLE_CYAN		36
#define CONSOLE_WHITE		37

#define PK_DEBUG_LOG_DOMAIN_LENGTH	20

/**
 * pk_debug_handler_cb:
 **/
static void
pk_debug_handler_cb (const gchar *log_domain, GLogLevelFlags log_level,
		     const gchar *message, gpointer user_data)
{
	gchar str_time[255];
	time_t the_time;
	guint len;
	guint i;

	/* time header */
	time (&the_time);
	strftime (str_time, 254, "%H:%M:%S", localtime (&the_time));

	/* no color please, we're British */
	if (!_console) {
		if (log_level == G_LOG_LEVEL_DEBUG) {
			g_print ("%s\t%s\t%s\n", str_time, log_domain, message);
		} else {
			g_print ("***\n%s\t%s\t%s\n***\n", str_time, log_domain, message);
		}
		return;
	}

	/* time in green */
	g_print ("%c[%dm%s\t", 0x1B, CONSOLE_GREEN, str_time);

	/* log domain in either blue */
	if (g_strcmp0 (log_domain, G_LOG_DOMAIN) == 0)
		g_print ("%c[%dm%s%c[%dm", 0x1B, CONSOLE_BLUE, log_domain, 0x1B, CONSOLE_RESET);
	else
		g_print ("%c[%dm%s%c[%dm", 0x1B, CONSOLE_CYAN, log_domain, 0x1B, CONSOLE_RESET);

	/* pad with spaces */
	len = strlen (log_domain);
	for (i=len; i<PK_DEBUG_LOG_DOMAIN_LENGTH; i++)
		g_print (" ");

	/* critical is also in red */
	if (log_level == G_LOG_LEVEL_CRITICAL ||
	    log_level == G_LOG_LEVEL_WARNING ||
	    log_level == G_LOG_LEVEL_ERROR) {
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, CONSOLE_RED, message, 0x1B, CONSOLE_RESET);
	} else {
		/* debug in blue */
		g_print ("%c[%dm%s\n%c[%dm", 0x1B, CONSOLE_BLUE, message, 0x1B, CONSOLE_RESET);
	}
}

/**
 * pk_debug_pre_parse_hook:
 */
static gboolean
pk_debug_pre_parse_hook (GOptionContext *context, GOptionGroup *group, gpointer data, GError **error)
{
	const GOptionEntry main_entries[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &_verbose,
		  /* TRANSLATORS: turn on all debugging */
		  N_("Show debugging information for all files"), NULL },
		{ NULL}
	};

	/* add main entry */
	g_option_context_add_main_entries (context, main_entries, NULL);
	return TRUE;
}

/**
 * pk_debug_add_log_domain:
 */
void
pk_debug_add_log_domain (const gchar *log_domain)
{
	if (_verbose) {
		g_log_set_fatal_mask (log_domain, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
		g_log_set_handler (log_domain,
				   G_LOG_LEVEL_ERROR |
				   G_LOG_LEVEL_CRITICAL |
				   G_LOG_LEVEL_DEBUG |
				   G_LOG_LEVEL_WARNING,
				   pk_debug_handler_cb, NULL);
	} else {
		/* hide all debugging */
		g_log_set_handler (log_domain,
				   G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_WARNING,
				   pk_debug_ignore_cb, NULL);
	}
}

/**
 * pk_debug_set_verbose:
 */
void
pk_debug_set_verbose (gboolean verbose)
{
	_verbose = verbose;
	_console = (isatty (fileno (stdout)) == 1);
}

/**
 * pk_debug_post_parse_hook:
 */
static gboolean
pk_debug_post_parse_hook (GOptionContext *context, GOptionGroup *group, gpointer data, GError **error)
{
	/* verbose? */
	pk_debug_add_log_domain (G_LOG_DOMAIN);
	_console = (isatty (fileno (stdout)) == 1);
	g_debug ("Verbose debugging %s (on console %i)", _verbose ? "enabled" : "disabled", _console);
	return TRUE;
}

/**
 * pk_debug_sigsegv_cb:
 **/
static void
pk_debug_sigsegv_cb (int sig)
{
	const gchar *filename;
	Dl_info dlinfo;
	gchar procname[256];
	gint ret, i = 0;
	unw_context_t context;
	unw_cursor_t cursor;
	unw_proc_info_t pip;
	unw_word_t off;

	pip.unwind_info = NULL;
	ret = unw_getcontext (&context);
	if  (ret) {
		g_warning ("unw_getcontext: %d\n", ret);
		goto out;
	}

	ret = unw_init_local (&cursor, &context);
	if  (ret) {
		g_warning ("unw_init_local: %d\n", ret);
		goto out;
	}

	ret = unw_step (&cursor);
	while  (ret > 0) {
		ret = unw_get_proc_info (&cursor, &pip);
		if  (ret) {
			g_warning ("unw_get_proc_info: %d\n", ret);
			break;
		}

		ret = unw_get_proc_name (&cursor, procname, 256, &off);
		if  (ret && ret != -UNW_ENOMEM) {
			if  (ret != -UNW_EUNSPEC)
				g_warning ("unw_get_proc_name: %d\n", ret);
			procname[0] = '?';
			procname[1] = 0;
		}

		if  (dladdr ( (void *) (pip.start_ip + off), &dlinfo) && dlinfo.dli_fname &&
		    *dlinfo.dli_fname)
			filename = dlinfo.dli_fname;
		else
			filename = "?";

		g_print ("%u: %s  (%s%s+0x%x) [%p]\n", i++, filename, procname,
			 ret == -UNW_ENOMEM ? "..." : "",  (int)off,  (void *) (pip.start_ip + off));

		ret = unw_step (&cursor);
		if  (ret < 0)
			g_warning ("unw_step: %d\n", ret);
	}
out:
	raise (SIGTRAP);
	return;
}


/**
 * pk_debug_segfault_backtrace: (skip)
 *
 * Registers with a signal handler so that the backtrace is shown to the user.
 */
void
pk_debug_segfault_backtrace (void)
{
	signal (SIGSEGV, pk_debug_sigsegv_cb);
}

/**
 * pk_debug_get_option_group: (skip)
 *
 * Returns a #GOptionGroup for the commandline arguments recognized
 * by debugging. You should add this group to your #GOptionContext
 * with g_option_context_add_group(), if you are using
 * g_option_context_parse() to parse your commandline arguments.
 *
 * Returns: a #GOptionGroup for the commandline arguments
 */
GOptionGroup *
pk_debug_get_option_group (void)
{
	GOptionGroup *group;
	group = g_option_group_new ("debug", _("Debugging Options"), _("Show debugging options"), NULL, NULL);
	g_option_group_set_parse_hooks (group, pk_debug_pre_parse_hook, pk_debug_post_parse_hook);
	return group;
}

