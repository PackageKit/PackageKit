/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <jansson.h>

G_BEGIN_DECLS

/*
 * Automatic cleanup for jansson references.
 */
G_DEFINE_AUTOPTR_CLEANUP_FUNC (json_t, json_decref)

/**
 * PK_JSON_ERROR:
 *
 * Error domain for JSON decoding and validation.
 */
#define PK_JSON_ERROR pk_json_error_quark ()

/**
 * PkJsonError:
 * @PK_JSON_ERROR_PARSE: the text is not valid JSON
 * @PK_JSON_ERROR_INVALID: well-formed JSON with an unexpected shape or type
 *
 * Error codes in the %PK_JSON_ERROR domain.
 */
typedef enum {
	PK_JSON_ERROR_PARSE,
	PK_JSON_ERROR_INVALID,
} PkJsonError;

static inline GQuark
pk_json_error_quark (void)
{
	return g_quark_from_static_string ("pk-json-error-quark");
}

/**
 * pk_json_set_error:
 * @error: (nullable): return location for a #GError
 * @code: a #PkJsonError
 * @json_error: the #json_error_t filled in by json_loads(), json_unpack() etc.
 *
 * Converts a jansson error into a #GError in the %PK_JSON_ERROR domain,
 * keeping jansson's message and position information.
 */
static inline void
pk_json_set_error (GError	     **error,
		   PkJsonError	       code,
		   const json_error_t *json_error)
{
	g_set_error (error,
		     PK_JSON_ERROR,
		     code,
		     "%s (line %d, column %d)",
		     json_error->text,
		     json_error->line,
		     json_error->column);
}

G_END_DECLS
