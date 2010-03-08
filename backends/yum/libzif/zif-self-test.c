/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Shishir Goel <crazyontheedge@gmail.com>
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

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include "egg-test.h"
#include "egg-debug.h"

/* prototypes */
void zif_completion_test (EggTest *test);
void zif_monitor_test (EggTest *test);
void zif_config_test (EggTest *test);
void zif_utils_test (EggTest *test);
void zif_package_test (EggTest *test);
void zif_package_local_test (EggTest *test);
void zif_store_local_test (EggTest *test);
void zif_groups_test (EggTest *test);
void zif_store_remote_test (EggTest *test);
void zif_repo_md_metalink_test (EggTest *test);
void zif_repo_md_primary_test (EggTest *test);
void zif_repo_md_filelists_test (EggTest *test);
void zif_repo_md_comps_test (EggTest *test);
void zif_repos_test (EggTest *test);
void zif_download_test (EggTest *test);
void zif_string_test (EggTest *test);
void zif_lock_test (EggTest *test);

int
main (int argc, char **argv)
{
	EggTest *test;

	g_type_init ();
	g_thread_init (NULL);
	test = egg_test_init ();
	egg_debug_init (TRUE);
	zif_init ();

	/* tests go here */
	zif_lock_test (test);
	zif_completion_test (test);
	zif_config_test (test);
	zif_string_test (test);
	zif_repo_md_metalink_test (test);
	zif_repo_md_mirrorlist_test (test);
	zif_repo_md_filelists_test (test);
	zif_repo_md_primary_test (test);
	zif_repo_md_comps_test (test);
	zif_download_test (test);
	zif_monitor_test (test);
	zif_package_test (test);
	zif_store_local_test (test);
	zif_package_local_test (test);
	zif_groups_test (test);
	zif_store_remote_test (test);
	zif_repos_test (test);
	zif_utils_test (test);

	return (egg_test_finish (test));
}

