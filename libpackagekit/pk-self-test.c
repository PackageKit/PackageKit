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
#include "egg-test.h"
#include "egg-debug.h"

/* prototypes */
void pk_package_id_test (EggTest *test);
void pk_package_id_tests (EggTest *test);
void pk_package_obj_test (EggTest *test);
void pk_package_list_test (EggTest *test);
void pk_enum_test (EggTest *test);
void pk_bitfield_test (EggTest *test);
void pk_common_test (EggTest *test);
void pk_enum_test_list (EggTest *test);
void pk_extra_test (EggTest *test);
void pk_client_test (EggTest *test);
void pk_control_test (EggTest *test);
void pk_task_list_test (EggTest *test);
void pk_catalog_test (EggTest *test);
void pk_update_detail_test (EggTest *test);
void pk_details_test (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest *test;

	g_type_init ();
	test = egg_test_init ();
	egg_debug_init (TRUE);

	/* tests go here */
	pk_common_test (test);
	pk_package_id_test (test);
	pk_package_id_tests (test);
	pk_package_obj_test (test);
	pk_package_list_test (test);
	pk_enum_test (test);
	pk_bitfield_test (test);
	pk_extra_test (test);
	pk_client_test (test);
	pk_catalog_test (test);
	pk_control_test (test);
	pk_task_list_test (test);
	pk_update_detail_test (test);
	pk_details_test (test);

	return (egg_test_finish (test));
}

