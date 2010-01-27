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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <glib.h>
#include <glib-object.h>
#include "egg-test.h"
#include <egg-debug.h>

/* prototypes */
void egg_string_test (EggTest *test);
void pk_lsof_test (EggTest *test);
void pk_proc_test (EggTest *test);
void pk_conf_test (EggTest *test);
void pk_store_test (EggTest *test);
void pk_inhibit_test (EggTest *test);
void pk_spawn_test (EggTest *test);
void pk_transaction_list_test (EggTest *test);
void pk_transaction_db_test (EggTest *test);
void pk_time_test (EggTest *test);
void pk_backend_test (EggTest *test);
void pk_backend_test_spawn (EggTest *test);
void pk_file_monitor_test (EggTest *test);
void pk_engine_test (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest *test;

	if (! g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	test = egg_test_init ();
	egg_debug_init (&argc, &argv);

	/* egg */
	egg_string_test (test);

	/* components */
	pk_proc_test (test);
	pk_lsof_test (test);
	pk_file_monitor_test (test);
	pk_time_test (test);
	pk_conf_test (test);
	pk_store_test (test);
	pk_inhibit_test (test);
	pk_spawn_test (test);
	pk_transaction_list_test (test);
	pk_transaction_db_test (test);

	/* backend stuff */
	pk_backend_test (test);
	pk_backend_test_spawn (test);

	/* system */
	pk_engine_test (test);

	return (egg_test_finish (test));
}

