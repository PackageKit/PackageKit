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

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>

#include "pk-console.h"

const gchar *summary =
	"PackageKit Console Interface\n"
	"\n"
	"Subcommands:\n"
	"  search name|details|group|file data\n"
	"  install <package_id>\n"
	"  install-file <file>\n"
	"  remove <package_id>\n"
	"  update <package_id>\n"
	"  refresh\n"
	"  resolve\n"
	"  force-refresh\n"
	"  update-system\n"
	"  get updates\n"
	"  get depends <package_id>\n"
	"  get requires <package_id>\n"
	"  get description <package_id>\n"
	"  get files <package_id>\n"
	"  get updatedetail <package_id>\n"
	"  get actions\n"
	"  get groups\n"
	"  get filters\n"
	"  get transactions\n"
	"  get repos\n"
	"  enable-repo <repo_id>\n"
	"  disable-repo <repo_id>\n"
	"  set-repo-data <repo_id> <parameter> <value>\n"
	"\n"
	"  package_id is typically gimp;2:2.4.0-0.rc1.1.fc8;i386;development";

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GError *error = NULL;
	gboolean verbose = FALSE;
	gboolean program_version = FALSE;
	gboolean nowait = FALSE;
	GOptionContext *context;
	gchar *options_help;

	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			"Show extra debugging information", NULL },
		{ "version", '\0', 0, G_OPTION_ARG_NONE, &program_version,
			"Show the program version and exit", NULL},
		{ "nowait", 'n', 0, G_OPTION_ARG_NONE, &nowait,
			"Exit without waiting for actions to complete", NULL},
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("SUBCOMMAND"));
	g_option_context_set_summary (context, summary) ;
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	/* Save the usage string in case command parsing fails. */
	options_help = g_option_context_get_help (context, TRUE, NULL);
	g_option_context_free (context);

	if (program_version == TRUE) {
		g_print (VERSION "\n");
		return 0;
	}

	if (argc < 2) {
		g_print (options_help);
		return 1;
	}

	/* run the commands */
	pk_console_run (argc, argv, !nowait, &error);
	if (error != NULL) {
		g_print ("Error:\n  %s\n\n", error->message);
		g_error_free (error);
		g_print (options_help);
	}

	g_free (options_help);
	return 0;
}

