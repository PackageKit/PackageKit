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

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <libselftest.h>
#include <egg-debug.h>

/* prototypes */
void egg_test_package_id (EggTest *test);
void egg_test_package_ids (EggTest *test);
void egg_test_package_obj (EggTest *test);
void egg_test_package_list (EggTest *test);
void egg_test_enum (EggTest *test);
void egg_test_bitfield (EggTest *test);
void egg_test_common (EggTest *test);
void egg_test_enum_list (EggTest *test);
void egg_test_extra (EggTest *test);
void egg_test_client (EggTest *test);
void egg_test_control (EggTest *test);
void egg_test_task_list (EggTest *test);
void egg_test_catalog (EggTest *test);
void egg_test_update_detail (EggTest *test);
void egg_test_details (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest test;

	g_type_init ();
	egg_test_init (&test);
	egg_debug_init (TRUE);

	/* tests go here */
	egg_test_common (&test);
	egg_test_package_id (&test);
	egg_test_package_ids (&test);
	egg_test_package_obj (&test);
	egg_test_package_list (&test);
	egg_test_enum (&test);
	egg_test_bitfield (&test);
	egg_test_extra (&test);
	egg_test_client (&test);
	egg_test_catalog (&test);
	egg_test_control (&test);
	egg_test_task_list (&test);
	egg_test_update_detail (&test);
	egg_test_details (&test);

	return (egg_test_finish (&test));
}

