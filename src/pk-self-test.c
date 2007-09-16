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

#include <glib.h>
#include <glib-object.h>
#include <libselftest.h>
#include <pk-debug.h>

/* prototypes */
void libst_spawn (LibSelfTest *test);
void libst_thread_list (LibSelfTest *test);

int
main (int argc, char **argv)
{
	LibSelfTest test;

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	g_type_init ();
	libst_init (&test);
	pk_debug_init (TRUE);

	/* tests go here */
	libst_spawn (&test);
	libst_thread_list (&test);

	return (libst_finish (&test));
}

