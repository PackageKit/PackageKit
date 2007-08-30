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

#ifndef __LIBSELFTEST_H
#define __LIBSELFTEST_H

#include <glib.h>
#include <string.h>

typedef enum
{
	CLASS_ALL,
	CLASS_AUTO,
	CLASS_MANUAL,
	CLASS_LAST
} LibSelfTestClass;

typedef enum
{
	LEVEL_QUIET,
	LEVEL_NORMAL,
	LEVEL_ALL,
	LEVEL_LAST
} LibSelfTestLevel;

typedef struct
{
	guint		 total;
	guint		 succeeded;
	gboolean	 started;
	LibSelfTestClass class;
	LibSelfTestLevel level;
	gchar		*type;
} LibSelfTest;

gboolean	libst_start	(LibSelfTest *test, const gchar *name, LibSelfTestClass class);
void		libst_end	(LibSelfTest *test);
void		libst_title	(LibSelfTest *test, const gchar *format, ...);
void		libst_success	(LibSelfTest *test, const gchar *format, ...);
void		libst_failed	(LibSelfTest *test, const gchar *format, ...);
void		libst_init	(LibSelfTest *test);
gint		libst_finish	(LibSelfTest *test);

#endif	/* __LIBSELFTEST_H */

