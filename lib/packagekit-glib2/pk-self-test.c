/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include "egg-test.h"
#include "egg-debug.h"

int
main (int argc, char **argv)
{
	EggTest *test;

	g_type_init ();
	test = egg_test_init ();
	egg_debug_init (TRUE);

	/* tests go here */
	pk_common_test (test);
	pk_enum_test (test);
	pk_bitfield_test (test);
	pk_progress_test (test);
	pk_results_test (test);
	pk_package_test (test);
	pk_control_test (test);
	pk_control_sync_test (test);
	pk_client_test (test);
	pk_package_sack_test (test);

	return (egg_test_finish (test));
}

