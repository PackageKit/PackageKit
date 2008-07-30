/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <glib.h>
#include <stdio.h>
#include <glib/gi18n.h>

#include <pk-debug.h>
#include <pk-common.h>
#include "pk-tools-common.h"

/**
 * pk_console_get_number:
 **/
guint
pk_console_get_number (const gchar *question, guint maxnum)
{
	gint answer = 0;
	gint retval;

	/* pretty print */
	g_print ("%s", question);

	do {
		/* get a number */
		retval = scanf("%u", &answer);

		/* positive */
		if (retval == 1 && answer > 0 && answer <= maxnum) {
			break;
		}
		g_print (_("Please enter a number from 1 to %i: "), maxnum);
	} while (TRUE);
	return answer;
}

/**
 * pk_console_get_prompt:
 **/
gboolean
pk_console_get_prompt (const gchar *question, gboolean defaultyes)
{
	gchar answer = '\0';

	/* pretty print */
	g_print ("%s", question);
	if (defaultyes) {
		g_print (" [Y/n] ");
	} else {
		g_print (" [N/y] ");
	}

	do {
		/* ITS4: ignore, we are copying into the same variable, not a string */
		answer = (gchar) getchar();

		/* positive */
		if (answer == 'y' || answer == 'Y') {
			return TRUE;
		}
		/* negative */
		if (answer == 'n' || answer == 'N') {
			return FALSE;
		}

		/* default choice */
		if (answer == '\n' && defaultyes) {
			return TRUE;
		}
		if (answer == '\n' && defaultyes == FALSE) {
			return FALSE;
		}
	} while (TRUE);

	/* keep GCC happy */
	return FALSE;
}

