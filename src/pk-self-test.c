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
#include <egg-debug.h>

/* prototypes */
void egg_test_conf (EggTest *test);
void egg_test_inhibit (EggTest *test);
void egg_test_spawn (EggTest *test);
void egg_test_transaction_list (EggTest *test);
void egg_test_transaction_db (EggTest *test);
void egg_test_security (EggTest *test);
void egg_test_time (EggTest *test);
void egg_test_backend (EggTest *test);
void egg_test_backend_spawn (EggTest *test);
void egg_test_backend_dbus (EggTest *test);
void egg_test_file_monitor (EggTest *test);
void egg_test_engine (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest test;

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	g_type_init ();
	egg_test_init (&test);
	egg_debug_init (TRUE);

	/* components */
	egg_test_file_monitor (&test);
	egg_test_security (&test);
	egg_test_time (&test);
	egg_test_conf (&test);
	egg_test_inhibit (&test);
	egg_test_spawn (&test);
	egg_test_transaction_list (&test);
	egg_test_transaction_db (&test);

	/* backend stuff */
	egg_test_backend (&test);
	egg_test_backend_spawn (&test);
	egg_test_backend_dbus (&test);

	/* system */
	egg_test_engine (&test);

	return (egg_test_finish (&test));
}

