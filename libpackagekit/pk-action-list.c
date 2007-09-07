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
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-action-list.h"

/**
 * pk_action_list_new:
 **/
PkActionList *
pk_action_list_new (PkTaskAction action, ...)
{
	va_list args;
	guint i;
	PkActionList *alist;
	PkTaskAction action_temp;

	/* create a new list. A list must have at least one entry */
	alist = g_ptr_array_new ();
	g_ptr_array_add (alist, GUINT_TO_POINTER(action));

	/* process the valist */
	va_start (args, action);
	for (i=0;; i++) {
		action_temp = va_arg (args, PkTaskAction);
		if (action_temp == 0) break;
		g_ptr_array_add (alist, GUINT_TO_POINTER(action_temp));
	}
	va_end (args);

	return alist;
}


/**
 * pk_action_list_new_from_string:
 **/
PkActionList *
pk_action_list_new_from_string (const gchar *actions)
{
	PkActionList *alist;
	gchar **sections;
	guint i;
	PkTaskAction action_temp;

	if (actions == NULL) {
		pk_warning ("actions null");
		return FALSE;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (actions, ";", 0);

	/* create a new list. A list must have at least one entry */
	alist = g_ptr_array_new ();

	for (i=0; sections[i]; i++) {
		action_temp = pk_action_enum_from_text (sections[i]);
		g_ptr_array_add (alist, GUINT_TO_POINTER(action_temp));
	}
	g_strfreev (sections);
	return alist;
}

/**
 * pk_action_list_free:
 **/
gboolean
pk_action_list_free (PkActionList *alist)
{
	g_ptr_array_free (alist, TRUE);
	return TRUE;
}

/**
 * pk_action_list_to_string:
 **/
gchar *
pk_action_list_to_string (PkActionList *alist)
{
	guint i;
	GString *string;
	PkTaskAction action;

	string = g_string_new ("");
	for (i=0; i<alist->len; i++) {
		action = GPOINTER_TO_UINT (g_ptr_array_index (alist, i));
		g_string_append (string, pk_action_enum_to_text (action));
		g_string_append (string, ";");
	}

	/* remove last ';' */
	g_string_set_size (string, string->len - 1);

	return g_string_free (string, FALSE);
}

/**
 * pk_action_list_append:
 **/
gboolean
pk_action_list_append (PkActionList *alist, PkTaskAction action)
{
	g_ptr_array_add (alist, GUINT_TO_POINTER(action));
	return TRUE;
}

/**
 * pk_action_list_contains:
 **/
gboolean
pk_action_list_contains (PkActionList *alist, PkTaskAction action)
{
	guint i;
	for (i=0; i<alist->len; i++) {
		if (GPOINTER_TO_UINT (g_ptr_array_index (alist, i)) == action) {
			return TRUE;
		}
	}
	return FALSE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_action_list (LibSelfTest *test)
{
	if (libst_start (test, "PkActionList", CLASS_AUTO) == FALSE) {
		return;
	}
	libst_end (test);
}
#endif

