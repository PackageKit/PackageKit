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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __EGG_TEST_H
#define __EGG_TEST_H

#include <glib.h>
#include <string.h>

typedef struct EggTest EggTest;

gboolean	 egg_test_start			(EggTest *test, const gchar *name);
void		 egg_test_end			(EggTest *test);
void		 egg_test_title			(EggTest *test, const gchar *format, ...);
void		 egg_test_title_assert		(EggTest *test, const gchar *text, gboolean value);
void		 egg_test_assert		(EggTest *test, gboolean value);
void		 egg_test_success		(EggTest *test, const gchar *format, ...);
void		 egg_test_failed		(EggTest *test, const gchar *format, ...) G_GNUC_NORETURN;
EggTest		*egg_test_init			(void);
gint		 egg_test_finish		(EggTest *test);
guint		 egg_test_elapsed		(EggTest *test);
void		 egg_test_loop_quit		(EggTest *test);
void		 egg_test_loop_wait		(EggTest *test, guint timeout);
void		 egg_test_loop_check		(EggTest *test);
void		 egg_test_set_user_data		(EggTest *test, gpointer user_data);
gpointer	 egg_test_get_user_data		(EggTest *test);
gchar		*egg_test_get_data_file		(const gchar *filename);

#endif	/* __EGG_TEST_H */

