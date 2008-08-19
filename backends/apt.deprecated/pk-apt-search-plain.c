/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Ali Sabil <ali.sabil@gmail.com>
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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <pk-backend.h>
#include <pk-backend-spawn.h>

extern PkBackendSpawn *spawn;

/**
 * backend_get_groups:
 */
static PkBitfield
backend_get_groups (PkBackend *backend)
{
	return (PK_GROUP_ENUM_ACCESSORIES |
		PK_GROUP_ENUM_GAMES |
		PK_GROUP_ENUM_GRAPHICS |
		PK_GROUP_ENUM_INTERNET |
		PK_GROUP_ENUM_OFFICE |
		PK_GROUP_ENUM_OTHER |
		PK_GROUP_ENUM_PROGRAMMING |
		PK_GROUP_ENUM_MULTIMEDIA |
		PK_GROUP_ENUM_SYSTEM);
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return (PK_FILTER_ENUM_GUI |
		PK_FILTER_ENUM_INSTALLED |
		PK_FILTER_ENUM_DEVELOPMENT);
}

/**
 * backend_get_details:
 */

void
backend_get_details (PkBackend *backend, const gchar *package_id)
{
	pk_backend_spawn_helper (spawn, "get-details.py", package_id, NULL);
}

/**
 * backend_search_details:
 */

void
backend_search_details (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_text (filters);
	pk_backend_spawn_helper (spawn, "search-details.py", filters_texts_text, search, NULL);
	g_free (filters_text);
}

/**
 * backend_search_name:
 */
void
backend_search_name (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	gchar *filters_text;
	filters_text = pk_filter_bitfield_to_text (filters);
	pk_backend_spawn_helper (spawn, "search-name.py", filters_text, search, NULL);
	g_free (filters_text);
}

/**
 * backend_search_group:
 */
void
backend_search_group (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	gchar *filters_text;
	pk_backend_spawn_helper (spawn, "search-group.py", filters_text, search, NULL);
	g_free (filters_text);
}

/* don't need to do any setup/finalize in the plain search mode */
void backend_init_search(PkBackend *backend) {}
void backend_finish_search(PkBackend *backend) {}
