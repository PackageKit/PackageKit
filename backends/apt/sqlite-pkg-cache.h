#ifndef SQLITE_PKT_CACHE
#define SQLITE_PKT_CACHE

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

typedef enum {
	SEARCH_NAME = 1,
	SEARCH_DETAILS,
	SEARCH_FILE
} SearchDepth;

#include <pk-backend.h>

void sqlite_init_cache(PkBackend *backend, const char* dbname, void (*build_db)(PkBackend *, sqlite3 *db));
void sqlite_search_details (PkBackend *backend, const gchar *filter, const gchar *search);
void sqlite_search_name (PkBackend *backend, const gchar *filter, const gchar *search);
void backend_search_common(PkBackend * backend, const gchar * filter, const gchar * search, SearchDepth which, PkBackendThreadFunc func);
void sqlite_get_description (PkBackend *backend, const gchar *package_id);

#endif
