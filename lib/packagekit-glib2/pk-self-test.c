/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib-object.h>
#include "egg-test.h"
#include "egg-debug.h"
#include "egg-string.h"

#include "pk-catalog.h"
#include "pk-client.h"
#include "pk-common.h"
#include "pk-control.h"
#include "pk-console-shared.h"
#include "pk-desktop.h"
#include "pk-enum.h"
#include "pk-package.h"
#include "pk-package-id.h"
#include "pk-package-ids.h"
#include "pk-package-sack.h"
#include "pk-results.h"
#include "pk-task.h"
#include "pk-task-text.h"
#include "pk-task-wrapper.h"
#include "pk-transaction-list.h"
#include "pk-version.h"

int
main (int argc, char **argv)
{
	EggTest *test;

	g_type_init ();
	test = egg_test_init ();
	egg_debug_init (&argc, &argv);

	/* tests go here */
	egg_string_test (test);
	pk_common_test (test);
	pk_enum_test (test);
	pk_desktop_test (test);
	pk_bitfield_test (test);
	pk_package_id_test (test);
	pk_package_ids_test (test);
	pk_progress_test (test);
	pk_results_test (test);
	pk_package_test (test);
	pk_control_test (test);
	pk_transaction_list_test (test);
	pk_client_test (test);
	pk_catalog_test (test);
	pk_package_sack_test (test);
	pk_task_test (test);
	pk_task_wrapper_test (test);
	pk_task_text_test (test);
	pk_console_test (test);

	return (egg_test_finish (test));
}

