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

#include <glib.h>
#include <glib-object.h>
#include <libselftest.h>
#include <pk-debug.h>

/* prototypes */
void libst_package_id (LibSelfTest *test);
void libst_package_ids (LibSelfTest *test);
void libst_package_list (LibSelfTest *test);
void libst_enum (LibSelfTest *test);
void libst_common (LibSelfTest *test);
void libst_enum_list (LibSelfTest *test);
void libst_extra (LibSelfTest *test);
void libst_client (LibSelfTest *test);
void libst_control (LibSelfTest *test);
void libst_task_list (LibSelfTest *test);

int
main (int argc, char **argv)
{
	LibSelfTest test;

	g_type_init ();
	libst_init (&test);
	pk_debug_init (TRUE);

	/* tests go here */
	libst_common (&test);
	libst_package_id (&test);
	libst_package_ids (&test);
	libst_package_list (&test);
	libst_enum (&test);
	libst_extra (&test);
	libst_client (&test);
	libst_control (&test);
	libst_task_list (&test);

	return (libst_finish (&test));
}

