/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Tom Parker <palfrey@tevp.net>
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

#ifndef __PK_APT_SEARCH_H
#define __PK_APT_SEARCH_H

#include <glib.h>
#include <pk-backend.h>

void backend_init_search(PkBackend *backend);

void backend_get_description (PkBackend *backend, const gchar *package_id);
void backend_search_details (PkBackend *backend, const gchar *filter, const gchar *search);
void backend_search_name (PkBackend *backend, const gchar *filter, const gchar *search);
void backend_search_group (PkBackend *backend, const gchar *filter, const gchar *search);

#endif
