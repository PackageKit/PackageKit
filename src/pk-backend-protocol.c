/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The JSON-based protocol between the daemon and a spawned backend helper.
 */

#include "config.h"

#include <string.h>

#include <pk-enum.h>
#include <pk-package-id.h>
#include <pk-update-detail.h>
#include <pk-json-private.h>

#include "pk-backend.h"
#include "pk-backend-protocol.h"

void
pk_backend_hello_free (PkBackendHello *hello)
{
	if (hello == NULL)
		return;
	g_free (hello->name);
	g_free (hello->description);
	g_free (hello->author);
	g_strfreev (hello->mime_types);
	g_free (hello);
}

/* ---- request building ---- */

static gchar *
pk_backend_protocol_dump (json_t *root)
{
	gchar *line = json_dumps (root, JSON_COMPACT);
	g_assert (line != NULL);
	return line;
}

static json_t *
pk_backend_protocol_strv_to_json (gchar **strv)
{
	json_t *array = json_array ();
	for (guint i = 0; strv != NULL && strv[i] != NULL; i++)
		json_array_append_new (array, json_string (strv[i]));
	return array;
}

/* "a;b;c" from the pk_*_bitfield_to_string() helpers -> ["a","b","c"] */
static json_t *
pk_backend_protocol_joined_to_json (gchar *joined)
{
	g_auto(GStrv) parts = g_strsplit (joined != NULL ? joined : "", ";", -1);
	json_t *array = json_array ();
	g_free (joined);
	for (guint i = 0; parts[i] != NULL; i++) {
		if (parts[i][0] != '\0')
			json_array_append_new (array, json_string (parts[i]));
	}
	return array;
}

static json_t *
pk_backend_protocol_build_ctx (PkBackendJob *job)
{
	PkBackend *backend = pk_backend_job_get_backend (job);
	const gchar *frontend_socket = pk_backend_job_get_frontend_socket (job);
	guint cache_age = pk_backend_job_get_cache_age (job);
	g_autofree gchar *eulas = NULL;
	json_t *ctx = json_object ();
	json_t *proxy = json_object ();
	const struct
	{
		const gchar *key;
		const gchar *value;
	} proxies[] = {
		{ "http", pk_backend_job_get_proxy_http (job) },
		{ "https", pk_backend_job_get_proxy_https (job) },
		{ "ftp", pk_backend_job_get_proxy_ftp (job) },
		{ "socks", pk_backend_job_get_proxy_socks (job) },
		{ "no_proxy", pk_backend_job_get_no_proxy (job) },
		{ "pac", pk_backend_job_get_pac (job) },
	};

	json_object_set_new (ctx, "locale", json_string (pk_backend_job_get_locale (job)));
	json_object_set_new (ctx, "uid", json_integer (pk_backend_job_get_uid (job)));
	json_object_set_new (ctx,
			     "network",
			     json_boolean (backend != NULL && pk_backend_is_online (backend)));
	json_object_set_new (ctx, "background", json_boolean (pk_backend_job_get_background (job)));
	json_object_set_new (ctx,
			     "interactive",
			     json_boolean (pk_backend_job_get_interactive (job)));
	json_object_set_new (ctx,
			     "cache_age",
			     cache_age == G_MAXUINT ? json_null () : json_integer (cache_age));
	json_object_set_new (ctx,
			     "details_with_deps_size",
			     json_boolean (pk_backend_job_get_details_with_deps_size (job)));
	json_object_set_new (ctx,
			     "frontend_socket",
			     frontend_socket != NULL && frontend_socket[0] != '\0'
				 ? json_string (frontend_socket)
				 : json_null ());
	for (guint i = 0; i < G_N_ELEMENTS (proxies); i++) {
		if (proxies[i].value != NULL && proxies[i].value[0] != '\0')
			json_object_set_new (proxy, proxies[i].key, json_string (proxies[i].value));
	}
	json_object_set_new (ctx, "proxy", proxy);
	if (backend != NULL)
		eulas = pk_backend_get_accepted_eula_string (backend);
	json_object_set_new (ctx,
			     "accepted_eulas",
			     pk_backend_protocol_joined_to_json (g_steal_pointer (&eulas)));
	return ctx;
}

/**
 * pk_backend_protocol_build_run:
 *
 * Builds the `run` request for @job from its role and parameters.
 *
 * Returns: (transfer full): the request line, or %NULL with @error set for a
 * role that has no wire representation
 */
gchar *
pk_backend_protocol_build_run (PkBackendJob *job, const gchar *job_id, GError **error)
{
	PkRoleEnum role = pk_backend_job_get_role (job);
	GVariant *params = pk_backend_job_get_parameters (job);
	g_autoptr(json_t) root = json_object ();
	g_auto(GStrv) strv = NULL;
	const gchar *s1 = NULL;
	const gchar *s2 = NULL;
	const gchar *s3 = NULL;
	guint64 bits = 0;
	guint32 u = 0;
	gboolean b1 = FALSE;
	gboolean b2 = FALSE;

	json_object_set_new (root, "op", json_string ("run"));
	json_object_set_new (root, "job", json_string (job_id));
	json_object_set_new (root, "role", json_string (pk_role_enum_to_string (role)));

#define FILTERS(bitfield)     \
	json_object_set_new ( \
	    root,             \
	    "filters",        \
	    pk_backend_protocol_joined_to_json (pk_filter_bitfield_to_string (bitfield)))
#define FLAGS(bitfield)                                           \
	json_object_set_new (root,                                \
			     "flags",                             \
			     pk_backend_protocol_joined_to_json ( \
				 pk_transaction_flag_bitfield_to_string (bitfield)))
#define STRV(key, value) json_object_set_new (root, key, pk_backend_protocol_strv_to_json ((value)))

	switch (role) {
	case PK_ROLE_ENUM_SEARCH_NAME:
	case PK_ROLE_ENUM_SEARCH_DETAILS:
	case PK_ROLE_ENUM_SEARCH_GROUP:
	case PK_ROLE_ENUM_SEARCH_FILE:
	case PK_ROLE_ENUM_WHAT_PROVIDES:
	case PK_ROLE_ENUM_RESOLVE:
		g_variant_get (params, "(t^as)", &bits, &strv);
		FILTERS (bits);
		STRV ("values", strv);
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
	case PK_ROLE_ENUM_GET_UPDATES:
	case PK_ROLE_ENUM_GET_REPO_LIST:
		g_variant_get (params, "(t)", &bits);
		FILTERS (bits);
		break;
	case PK_ROLE_ENUM_DEPENDS_ON:
	case PK_ROLE_ENUM_REQUIRED_BY:
		g_variant_get (params, "(t^asb)", &bits, &strv, &b1);
		FILTERS (bits);
		STRV ("package_ids", strv);
		json_object_set_new (root, "recursive", json_boolean (b1));
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
	case PK_ROLE_ENUM_GET_FILES:
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		g_variant_get (params, "(^as)", &strv);
		STRV ("package_ids", strv);
		break;
	case PK_ROLE_ENUM_GET_DETAILS_LOCAL:
	case PK_ROLE_ENUM_GET_FILES_LOCAL:
		g_variant_get (params, "(^as)", &strv);
		STRV ("files", strv);
		break;
	case PK_ROLE_ENUM_GET_DISTRO_UPGRADES:
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		g_variant_get (params, "(^as&s)", &strv, &s1);
		STRV ("package_ids", strv);
		json_object_set_new (root, "directory", json_string (s1));
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		g_variant_get (params, "(t^as)", &bits, &strv);
		FLAGS (bits);
		STRV ("package_ids", strv);
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		g_variant_get (params, "(t^as)", &bits, &strv);
		FLAGS (bits);
		STRV ("files", strv);
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		g_variant_get (params, "(t^asbb)", &bits, &strv, &b1, &b2);
		FLAGS (bits);
		STRV ("package_ids", strv);
		json_object_set_new (root, "allow_deps", json_boolean (b1));
		json_object_set_new (root, "autoremove", json_boolean (b2));
		break;
	case PK_ROLE_ENUM_INSTALL_SIGNATURE:
		g_variant_get (params, "(u&s&s)", &u, &s1, &s2);
		json_object_set_new (root,
				     "sig_type",
				     json_string (pk_sig_type_enum_to_string (u)));
		json_object_set_new (root, "key_id", json_string (s1));
		json_object_set_new (root, "package_id", json_string (s2));
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		g_variant_get (params, "(b)", &b1);
		json_object_set_new (root, "force", json_boolean (b1));
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
		g_variant_get (params, "(&sb)", &s1, &b1);
		json_object_set_new (root, "repo_id", json_string (s1));
		json_object_set_new (root, "enabled", json_boolean (b1));
		break;
	case PK_ROLE_ENUM_REPO_SET_DATA:
		g_variant_get (params, "(&s&s&s)", &s1, &s2, &s3);
		json_object_set_new (root, "repo_id", json_string (s1));
		json_object_set_new (root, "parameter", json_string (s2));
		json_object_set_new (root, "value", json_string (s3));
		break;
	case PK_ROLE_ENUM_REPO_REMOVE:
		g_variant_get (params, "(t&sb)", &bits, &s1, &b1);
		FLAGS (bits);
		json_object_set_new (root, "repo_id", json_string (s1));
		json_object_set_new (root, "autoremove", json_boolean (b1));
		break;
	case PK_ROLE_ENUM_UPGRADE_SYSTEM:
		g_variant_get (params, "(t&su)", &bits, &s1, &u);
		FLAGS (bits);
		json_object_set_new (root, "distro_id", json_string (s1));
		json_object_set_new (root,
				     "upgrade_kind",
				     json_string (pk_upgrade_kind_enum_to_string (u)));
		break;
	case PK_ROLE_ENUM_REPAIR_SYSTEM:
		g_variant_get (params, "(t)", &bits);
		FLAGS (bits);
		break;
	default:
		g_set_error (error,
			     PK_JSON_ERROR,
			     PK_JSON_ERROR_INVALID,
			     "role %s cannot be sent to a spawned backend",
			     pk_role_enum_to_string (role));
		return NULL;
	}
#undef FILTERS
#undef FLAGS
#undef STRV

	json_object_set_new (root, "ctx", pk_backend_protocol_build_ctx (job));
	return pk_backend_protocol_dump (root);
}

gchar *
pk_backend_protocol_build_hello (void)
{
	g_autoptr(json_t) root = json_pack ("{s:s, s:i, s:s}",
					    "op",
					    "hello",
					    "protocol",
					    PK_BACKEND_PROTOCOL_VERSION,
					    "daemon_version",
					    PROJECT_VERSION);
	return pk_backend_protocol_dump (root);
}

gchar *
pk_backend_protocol_build_cancel (const gchar *job_id)
{
	g_autoptr(json_t) root = json_pack ("{s:s, s:s}", "op", "cancel", "job", job_id);
	return pk_backend_protocol_dump (root);
}

gchar *
pk_backend_protocol_build_exit (void)
{
	g_autoptr(json_t) root = json_pack ("{s:s}", "op", "exit");
	return pk_backend_protocol_dump (root);
}

/* ---- event parsing ---- */

/* Field accessors. Every wrong type, missing required key or bad enum value
 * is a protocol error: the helper is buggy, and failing loudly beats
 * silently dropping data. */

static gboolean pk_backend_protocol_invalid (GError **error, const gchar *fmt, ...)
    G_GNUC_PRINTF (2, 3);

static gboolean
pk_backend_protocol_invalid (GError **error, const gchar *fmt, ...)
{
	g_autofree gchar *message = NULL;
	va_list args;

	va_start (args, fmt);
	message = g_strdup_vprintf (fmt, args);
	va_end (args);
	g_set_error_literal (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID, message);
	return FALSE;
}

static gboolean
pk_backend_protocol_get_string (json_t *obj,
				const gchar *key,
				gboolean nullable,
				const gchar **out,
				GError **error)
{
	json_t *value = json_object_get (obj, key);
	if (value == NULL || json_is_null (value)) {
		if (!nullable)
			return pk_backend_protocol_invalid (error, "missing field '%s'", key);
		*out = NULL;
		return TRUE;
	}
	if (!json_is_string (value))
		return pk_backend_protocol_invalid (error, "field '%s' must be a string", key);
	*out = json_string_value (value);
	return TRUE;
}

static gboolean
pk_backend_protocol_get_int (json_t *obj,
			     const gchar *key,
			     gboolean nullable,
			     gint64 *out,
			     GError **error)
{
	json_t *value = json_object_get (obj, key);
	if (value == NULL || json_is_null (value)) {
		if (!nullable)
			return pk_backend_protocol_invalid (error, "missing field '%s'", key);
		*out = -1;
		return TRUE;
	}
	if (!json_is_integer (value))
		return pk_backend_protocol_invalid (error, "field '%s' must be an integer", key);
	*out = json_integer_value (value);
	return TRUE;
}

static gboolean
pk_backend_protocol_get_bool (json_t *obj, const gchar *key, gboolean *out, GError **error)
{
	json_t *value = json_object_get (obj, key);
	if (!json_is_boolean (value))
		return pk_backend_protocol_invalid (error, "field '%s' must be a boolean", key);
	*out = json_is_true (value);
	return TRUE;
}

static gboolean
pk_backend_protocol_get_array (json_t *obj, const gchar *key, json_t **out, GError **error)
{
	json_t *value = json_object_get (obj, key);
	if (!json_is_array (value))
		return pk_backend_protocol_invalid (error, "field '%s' must be an array", key);
	*out = value;
	return TRUE;
}

static gboolean
pk_backend_protocol_get_strv (json_t *obj, const gchar *key, gchar ***out, GError **error)
{
	json_t *array = json_object_get (obj, key);
	g_autoptr(GPtrArray) strv = g_ptr_array_new_with_free_func (g_free);
	json_t *item;

	if (array != NULL && !json_is_null (array)) {
		gsize i;

		if (!json_is_array (array))
			return pk_backend_protocol_invalid (error,
							    "field '%s' must be an array",
							    key);
		json_array_foreach (array, i, item)
		{
			if (!json_is_string (item))
				return pk_backend_protocol_invalid (
				    error,
				    "field '%s' must contain only strings",
				    key);
			g_ptr_array_add (strv, g_strdup (json_string_value (item)));
		}
	}
	g_ptr_array_add (strv, NULL);
	*out = (gchar **) g_ptr_array_free (g_steal_pointer (&strv), FALSE);
	return TRUE;
}

/**
 * pk_backend_protocol_get_enum:
 *
 * Enum lookup: `from_string` returns `unknown_value` for bad spellings
 */
static gboolean
pk_backend_protocol_get_enum (json_t *obj,
			      const gchar *key,
			      guint (*from_string) (const gchar *),
			      guint unknown_value,
			      guint *out,
			      GError **error)
{
	const gchar *text = NULL;
	if (!pk_backend_protocol_get_string (obj, key, FALSE, &text, error))
		return FALSE;
	*out = from_string (text);
	if (*out == unknown_value)
		return pk_backend_protocol_invalid (error,
						    "field '%s' has unknown value '%s'",
						    key,
						    text);
	return TRUE;
}

#define GET_ENUM(obj, key, prefix, out)                                                      \
	pk_backend_protocol_get_enum (obj,                                                   \
				      key,                                                   \
				      (guint (*) (const gchar *)) prefix##_enum_from_string, \
				      (guint) prefix##_ENUM_UNKNOWN_VALUE,                   \
				      (guint *) (out),                                       \
				      error)

/* Lazy hack: the PK_*_ENUM_UNKNOWN constants do not share a naming scheme, so map them */
#define pk_status_ENUM_UNKNOWN_VALUE	     PK_STATUS_ENUM_UNKNOWN
#define pk_info_ENUM_UNKNOWN_VALUE	     PK_INFO_ENUM_UNKNOWN
#define pk_severity_ENUM_UNKNOWN_VALUE	     PK_SEVERITY_ENUM_NONE
#define pk_restart_ENUM_UNKNOWN_VALUE	     PK_RESTART_ENUM_UNKNOWN
#define pk_error_ENUM_UNKNOWN_VALUE	     PK_ERROR_ENUM_UNKNOWN
#define pk_group_ENUM_UNKNOWN_VALUE	     PK_GROUP_ENUM_UNKNOWN
#define pk_update_state_ENUM_UNKNOWN_VALUE   PK_UPDATE_STATE_ENUM_UNKNOWN
#define pk_sig_type_ENUM_UNKNOWN_VALUE	     PK_SIGTYPE_ENUM_UNKNOWN
#define pk_distro_upgrade_ENUM_UNKNOWN_VALUE PK_DISTRO_UPGRADE_ENUM_UNKNOWN
#define pk_role_ENUM_UNKNOWN_VALUE	     PK_ROLE_ENUM_UNKNOWN

static gboolean
pk_backend_protocol_get_package_id (json_t *obj,
				    const gchar *key,
				    gboolean nullable,
				    const gchar **out,
				    GError **error)
{
	if (!pk_backend_protocol_get_string (obj, key, nullable, out, error))
		return FALSE;
	if (*out != NULL && !pk_package_id_check (*out))
		return pk_backend_protocol_invalid (error,
						    "field '%s' is not a valid package id: %s",
						    key,
						    *out);
	return TRUE;
}

static gboolean
pk_backend_protocol_get_percentage (json_t *obj, guint *out, GError **error)
{
	gint64 value = 0;
	if (!pk_backend_protocol_get_int (obj, "value", TRUE, &value, error))
		return FALSE;
	if (value == -1) {
		*out = PK_BACKEND_PERCENTAGE_INVALID;
		return TRUE;
	}
	if (value < 0 || value > 100)
		return pk_backend_protocol_invalid (error,
						    "percentage %" G_GINT64_FORMAT " out of range",
						    value);
	*out = (guint) value;
	return TRUE;
}

static PkBitfield
pk_backend_protocol_strv_to_bitfield (gchar **strv, PkBitfield (*from_string) (const gchar *))
{
	g_autofree gchar *joined = g_strjoinv (";", strv);
	return strv[0] != NULL ? from_string (joined) : 0;
}

static json_t *
pk_backend_protocol_load (const gchar *line, GError **error)
{
	json_error_t jerr;
	json_t *root;

	if (strlen (line) > PK_BACKEND_PROTOCOL_MAX_LINE) {
		pk_backend_protocol_invalid (error,
					     "message exceeds %u bytes",
					     PK_BACKEND_PROTOCOL_MAX_LINE);
		return NULL;
	}
	root = json_loads (line, JSON_REJECT_DUPLICATES, &jerr);
	if (root == NULL) {
		pk_json_set_error (error, PK_JSON_ERROR_PARSE_FAILED, &jerr);
		return NULL;
	}
	if (!json_is_object (root)) {
		json_decref (root);
		pk_backend_protocol_invalid (error, "message is not a JSON object");
		return NULL;
	}
	return root;
}

/**
 * pk_backend_protocol_parse_hello:
 *
 * Parses the helper's `hello` event sent in reply to ours.
 *
 * Returns: (transfer full): the backend description, or %NULL with @error set
 */
PkBackendHello *
pk_backend_protocol_parse_hello (const gchar *line, GError **error)
{
	g_autoptr(json_t) root = pk_backend_protocol_load (line, error);
	g_autoptr(PkBackendHello) hello = NULL;
	const gchar *ev = NULL;
	const gchar *text = NULL;
	gint64 protocol = 0;
	gboolean parallel = FALSE;
	g_auto(GStrv) roles = NULL;
	g_auto(GStrv) filters = NULL;
	g_auto(GStrv) groups = NULL;

	if (root == NULL)
		return NULL;
	if (!pk_backend_protocol_get_string (root, "ev", FALSE, &ev, error))
		return NULL;
	if (g_strcmp0 (ev, "hello") != 0) {
		pk_backend_protocol_invalid (error, "expected hello, got '%s'", ev);
		return NULL;
	}
	if (!pk_backend_protocol_get_int (root, "protocol", FALSE, &protocol, error))
		return NULL;
	if (protocol != PK_BACKEND_PROTOCOL_VERSION) {
		pk_backend_protocol_invalid (error,
					     "helper speaks protocol version %" G_GINT64_FORMAT
					     ", daemon needs %u",
					     protocol,
					     PK_BACKEND_PROTOCOL_VERSION);
		return NULL;
	}
	if (!pk_backend_protocol_get_bool (root, "parallel", &parallel, error))
		return NULL;
	if (parallel) {
		pk_backend_protocol_invalid (
		    error,
		    "parallel jobs are not supported in protocol version 1");
		return NULL;
	}

	hello = g_new0 (PkBackendHello, 1);
	if (!pk_backend_protocol_get_string (root, "name", TRUE, &text, error))
		return NULL;
	hello->name = g_strdup (text);
	if (!pk_backend_protocol_get_string (root, "description", TRUE, &text, error))
		return NULL;
	hello->description = g_strdup (text);
	if (!pk_backend_protocol_get_string (root, "author", TRUE, &text, error))
		return NULL;
	hello->author = g_strdup (text);
	if (!pk_backend_protocol_get_strv (root, "roles", &roles, error) ||
	    !pk_backend_protocol_get_strv (root, "filters", &filters, error) ||
	    !pk_backend_protocol_get_strv (root, "groups", &groups, error) ||
	    !pk_backend_protocol_get_strv (root, "mime_types", &hello->mime_types, error))
		return NULL;
	for (guint i = 0; roles[i] != NULL; i++) {
		if (pk_role_enum_from_string (roles[i]) == PK_ROLE_ENUM_UNKNOWN) {
			pk_backend_protocol_invalid (error, "unknown role '%s'", roles[i]);
			return NULL;
		}
	}
	hello->roles = pk_backend_protocol_strv_to_bitfield (roles, pk_role_bitfield_from_string);
	hello->filters = pk_backend_protocol_strv_to_bitfield (filters,
							       pk_filter_bitfield_from_string);
	hello->groups = pk_backend_protocol_strv_to_bitfield (groups,
							      pk_group_bitfield_from_string);
	return g_steal_pointer (&hello);
}

static gboolean
pk_backend_protocol_handle_packages (json_t *root, PkBackendJob *job, GError **error)
{
	g_autoptr(GPtrArray) packages = g_ptr_array_new_with_free_func (g_object_unref);
	json_t *items = NULL;
	json_t *item;
	gsize i;

	if (!pk_backend_protocol_get_array (root, "items", &items, error))
		return FALSE;
	json_array_foreach (items, i, item)
	{
		const gchar *package_id = NULL;
		const gchar *summary = NULL;
		const gchar *severity_text = NULL;
		PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
		PkSeverityEnum severity = PK_SEVERITY_ENUM_NONE;

		if (!json_is_object (item))
			return pk_backend_protocol_invalid (error,
							    "packages items must be objects");
		if (!pk_backend_protocol_get_package_id (item,
							 "package_id",
							 FALSE,
							 &package_id,
							 error) ||
		    !GET_ENUM (item, "info", pk_info, &info) ||
		    !pk_backend_protocol_get_string (item, "summary", TRUE, &summary, error) ||
		    !pk_backend_protocol_get_string (item, "severity", TRUE, &severity_text, error))
			return FALSE;
		if (severity_text != NULL && !GET_ENUM (item, "severity", pk_severity, &severity))
			return FALSE;
		pk_backend_packages_add (packages,
					 info,
					 package_id,
					 summary != NULL ? summary : "",
					 severity);
	}
	pk_backend_job_packages (job, packages);
	return TRUE;
}

static gboolean
pk_backend_protocol_handle_details (json_t *root, PkBackendJob *job, GError **error)
{
	json_t *items = NULL;
	json_t *item;
	gsize i;

	if (!pk_backend_protocol_get_array (root, "items", &items, error))
		return FALSE;
	json_array_foreach (items, i, item)
	{
		const gchar *package_id = NULL;
		const gchar *summary = NULL;
		const gchar *license = NULL;
		const gchar *description = NULL;
		const gchar *url = NULL;
		PkGroupEnum group = PK_GROUP_ENUM_UNKNOWN;
		gint64 size = -1;
		gint64 download_size = -1;

		if (!json_is_object (item))
			return pk_backend_protocol_invalid (error, "details items must be objects");
		if (!pk_backend_protocol_get_package_id (item,
							 "package_id",
							 FALSE,
							 &package_id,
							 error) ||
		    !pk_backend_protocol_get_string (item, "summary", TRUE, &summary, error) ||
		    !pk_backend_protocol_get_string (item, "license", TRUE, &license, error) ||
		    !GET_ENUM (item, "group", pk_group, &group) ||
		    !pk_backend_protocol_get_string (item,
						     "description",
						     TRUE,
						     &description,
						     error) ||
		    !pk_backend_protocol_get_string (item, "url", TRUE, &url, error) ||
		    !pk_backend_protocol_get_int (item, "size", TRUE, &size, error) ||
		    !pk_backend_protocol_get_int (item,
						  "download_size",
						  TRUE,
						  &download_size,
						  error))
			return FALSE;
		pk_backend_job_details (job,
					package_id,
					summary,
					license,
					group,
					description,
					url,
					size < 0 ? G_MAXULONG : (gulong) size,
					download_size < 0 ? G_MAXUINT64 : (guint64) download_size);
	}
	return TRUE;
}

static gboolean
pk_backend_protocol_handle_update_details (json_t *root, PkBackendJob *job, GError **error)
{
	g_autoptr(GPtrArray) details = g_ptr_array_new_with_free_func (g_object_unref);
	json_t *items = NULL;
	json_t *item;
	gsize i;

	if (!pk_backend_protocol_get_array (root, "items", &items, error))
		return FALSE;
	json_array_foreach (items, i, item)
	{
		const gchar *package_id = NULL;
		const gchar *update_text = NULL;
		const gchar *changelog = NULL;
		const gchar *issued = NULL;
		const gchar *updated = NULL;
		PkRestartEnum restart = PK_RESTART_ENUM_UNKNOWN;
		PkUpdateStateEnum state = PK_UPDATE_STATE_ENUM_UNKNOWN;
		g_auto(GStrv) updates = NULL;
		g_auto(GStrv) obsoletes = NULL;
		g_auto(GStrv) vendor_urls = NULL;
		g_auto(GStrv) bugzilla_urls = NULL;
		g_auto(GStrv) cve_urls = NULL;

		if (!json_is_object (item))
			return pk_backend_protocol_invalid (error,
							    "update-details items must be objects");
		if (!pk_backend_protocol_get_package_id (item,
							 "package_id",
							 FALSE,
							 &package_id,
							 error) ||
		    !pk_backend_protocol_get_strv (item, "updates", &updates, error) ||
		    !pk_backend_protocol_get_strv (item, "obsoletes", &obsoletes, error) ||
		    !pk_backend_protocol_get_strv (item, "vendor_urls", &vendor_urls, error) ||
		    !pk_backend_protocol_get_strv (item, "bugzilla_urls", &bugzilla_urls, error) ||
		    !pk_backend_protocol_get_strv (item, "cve_urls", &cve_urls, error) ||
		    !GET_ENUM (item, "restart", pk_restart, &restart) ||
		    !pk_backend_protocol_get_string (item,
						     "update_text",
						     TRUE,
						     &update_text,
						     error) ||
		    !pk_backend_protocol_get_string (item, "changelog", TRUE, &changelog, error) ||
		    !GET_ENUM (item, "state", pk_update_state, &state) ||
		    !pk_backend_protocol_get_string (item, "issued", TRUE, &issued, error) ||
		    !pk_backend_protocol_get_string (item, "updated", TRUE, &updated, error))
			return FALSE;
		g_ptr_array_add (details,
				 pk_update_detail_new_full (package_id,
							    updates,
							    obsoletes,
							    vendor_urls,
							    bugzilla_urls,
							    cve_urls,
							    restart,
							    update_text,
							    changelog,
							    state,
							    issued,
							    updated));
	}
	pk_backend_job_update_details (job, details);
	return TRUE;
}

static gboolean
pk_backend_protocol_handle_repo_details (json_t *root, PkBackendJob *job, GError **error)
{
	json_t *items = NULL;
	json_t *item;
	gsize i;

	if (!pk_backend_protocol_get_array (root, "items", &items, error))
		return FALSE;
	json_array_foreach (items, i, item)
	{
		const gchar *repo_id = NULL;
		const gchar *description = NULL;
		gboolean enabled = FALSE;

		if (!json_is_object (item))
			return pk_backend_protocol_invalid (error,
							    "repo-details items must be objects");
		if (!pk_backend_protocol_get_string (item, "repo_id", FALSE, &repo_id, error) ||
		    !pk_backend_protocol_get_string (item,
						     "description",
						     TRUE,
						     &description,
						     error) ||
		    !pk_backend_protocol_get_bool (item, "enabled", &enabled, error))
			return FALSE;
		pk_backend_job_repo_detail (job, repo_id, description, enabled);
	}
	return TRUE;
}

static gboolean
pk_backend_protocol_handle_distro_upgrades (json_t *root, PkBackendJob *job, GError **error)
{
	json_t *items = NULL;
	json_t *item;
	gsize i;

	if (!pk_backend_protocol_get_array (root, "items", &items, error))
		return FALSE;
	json_array_foreach (items, i, item)
	{
		const gchar *name = NULL;
		const gchar *summary = NULL;
		PkDistroUpgradeEnum type = PK_DISTRO_UPGRADE_ENUM_UNKNOWN;

		if (!json_is_object (item))
			return pk_backend_protocol_invalid (
			    error,
			    "distro-upgrades items must be objects");
		if (!GET_ENUM (item, "type", pk_distro_upgrade, &type) ||
		    !pk_backend_protocol_get_string (item, "name", FALSE, &name, error) ||
		    !pk_backend_protocol_get_string (item, "summary", TRUE, &summary, error))
			return FALSE;
		pk_backend_job_distro_upgrade (job, type, name, summary);
	}
	return TRUE;
}

/**
 * pk_backend_protocol_handle_event:
 * @line: one line from the helper
 * @job_id: id of the running job, or %NULL if none is running
 * @job: the running job, or %NULL
 * @finished: (out): set to %TRUE when @line was the job's `finished` event
 * @error: return location for a #GError
 *
 * Applies one helper event to @job. Events for other jobs and events of
 * unknown type are logged and ignored.
 *
 * Returns: %FALSE on a protocol error, after which the helper cannot be
 * trusted and should be terminated
 */
gboolean
pk_backend_protocol_handle_event (const gchar *log_domain,
				  const gchar *line,
				  const gchar *job_id,
				  PkBackendJob *job,
				  gboolean *finished,
				  GError **error)
{
	g_autoptr(json_t) root = pk_backend_protocol_load (line, error);
	const gchar *ev = NULL;
	const gchar *ev_job = NULL;
	const gchar *package_id = NULL;
	const gchar *text = NULL;
	gint64 i64 = 0;
	gboolean b = FALSE;
	guint value = 0;
	guint status = 0;

	*finished = FALSE;
	if (root == NULL)
		return FALSE;
	if (!pk_backend_protocol_get_string (root, "ev", FALSE, &ev, error) ||
	    !pk_backend_protocol_get_string (root, "job", TRUE, &ev_job, error))
		return FALSE;

	/* log is the only event that may stand outside a job */
	if (g_strcmp0 (ev, "log") == 0) {
		const gchar *level = NULL;
		if (!pk_backend_protocol_get_string (root, "level", FALSE, &level, error) ||
		    !pk_backend_protocol_get_string (root, "message", FALSE, &text, error))
			return FALSE;
		if (g_strcmp0 (level, "debug") == 0)
			g_log (log_domain, G_LOG_LEVEL_DEBUG, "helper: %s", text);
		else if (g_strcmp0 (level, "info") == 0)
			g_log (log_domain, G_LOG_LEVEL_INFO, "helper: %s", text);
		else
			g_log (log_domain, G_LOG_LEVEL_WARNING, "helper: %s", text);
		return TRUE;
	}

	if (g_strcmp0 (ev, "hello") == 0) {
		g_log (log_domain,
		       G_LOG_LEVEL_WARNING,
		       "ignoring unexpected hello event from spawned backend");
		return TRUE;
	}
	if (job == NULL || g_strcmp0 (ev_job, job_id) != 0) {
		g_log (log_domain,
		       G_LOG_LEVEL_DEBUG,
		       "ignoring event '%s' for job '%s' (running: '%s')",
		       ev,
		       ev_job != NULL ? ev_job : "(none)",
		       job_id != NULL ? job_id : "(none)");
		return TRUE;
	}

	if (g_strcmp0 (ev, "finished") == 0) {
		pk_backend_job_finished (job);
		*finished = TRUE;

	} else if (g_strcmp0 (ev, "status") == 0) {
		if (!GET_ENUM (root, "status", pk_status, &status))
			return FALSE;
		pk_backend_job_set_status (job, status);

	} else if (g_strcmp0 (ev, "percentage") == 0) {
		if (!pk_backend_protocol_get_percentage (root, &value, error))
			return FALSE;
		pk_backend_job_set_percentage (job, value);

	} else if (g_strcmp0 (ev, "item-progress") == 0) {
		if (!pk_backend_protocol_get_package_id (root,
							 "package_id",
							 FALSE,
							 &package_id,
							 error) ||
		    !GET_ENUM (root, "status", pk_status, &status) ||
		    !pk_backend_protocol_get_percentage (root, &value, error))
			return FALSE;
		pk_backend_job_set_item_progress (job, package_id, status, value);

	} else if (g_strcmp0 (ev, "speed") == 0) {
		if (!pk_backend_protocol_get_int (root, "value", FALSE, &i64, error))
			return FALSE;
		pk_backend_job_set_speed (job, (guint) i64);

	} else if (g_strcmp0 (ev, "download-size-remaining") == 0) {
		if (!pk_backend_protocol_get_int (root, "value", FALSE, &i64, error))
			return FALSE;
		pk_backend_job_set_download_size_remaining (job, (guint64) i64);

	} else if (g_strcmp0 (ev, "allow-cancel") == 0) {
		if (!pk_backend_protocol_get_bool (root, "value", &b, error))
			return FALSE;
		pk_backend_job_set_allow_cancel (job, b);

	} else if (g_strcmp0 (ev, "require-restart") == 0) {
		if (!GET_ENUM (root, "restart", pk_restart, &value) ||
		    !pk_backend_protocol_get_package_id (root,
							 "package_id",
							 FALSE,
							 &package_id,
							 error))
			return FALSE;
		pk_backend_job_require_restart (job, value, package_id);

	} else if (g_strcmp0 (ev, "packages") == 0) {
		return pk_backend_protocol_handle_packages (root, job, error);

	} else if (g_strcmp0 (ev, "package-status") == 0) {
		if (!pk_backend_protocol_get_package_id (root,
							 "package_id",
							 FALSE,
							 &package_id,
							 error) ||
		    !GET_ENUM (root, "info", pk_info, &value))
			return FALSE;
		pk_backend_job_package_status (job, package_id, value);

	} else if (g_strcmp0 (ev, "details") == 0) {
		return pk_backend_protocol_handle_details (root, job, error);

	} else if (g_strcmp0 (ev, "update-details") == 0) {
		return pk_backend_protocol_handle_update_details (root, job, error);

	} else if (g_strcmp0 (ev, "files") == 0) {
		g_auto(GStrv) files = NULL;
		if (!pk_backend_protocol_get_package_id (root,
							 "package_id",
							 TRUE,
							 &package_id,
							 error) ||
		    !pk_backend_protocol_get_strv (root, "files", &files, error))
			return FALSE;
		pk_backend_job_files (job, package_id, files);

	} else if (g_strcmp0 (ev, "repo-details") == 0) {
		return pk_backend_protocol_handle_repo_details (root, job, error);

	} else if (g_strcmp0 (ev, "distro-upgrades") == 0) {
		return pk_backend_protocol_handle_distro_upgrades (root, job, error);

	} else if (g_strcmp0 (ev, "error") == 0) {
		if (!GET_ENUM (root, "code", pk_error, &value) ||
		    !pk_backend_protocol_get_string (root, "details", TRUE, &text, error))
			return FALSE;
		pk_backend_job_error_code (job, value, "%s", text != NULL ? text : "");

	} else if (g_strcmp0 (ev, "eula-required") == 0) {
		const gchar *eula_id = NULL;
		const gchar *vendor = NULL;
		if (!pk_backend_protocol_get_string (root, "eula_id", FALSE, &eula_id, error) ||
		    !pk_backend_protocol_get_package_id (root,
							 "package_id",
							 FALSE,
							 &package_id,
							 error) ||
		    !pk_backend_protocol_get_string (root, "vendor_name", TRUE, &vendor, error) ||
		    !pk_backend_protocol_get_string (root,
						     "license_agreement",
						     FALSE,
						     &text,
						     error))
			return FALSE;
		pk_backend_job_eula_required (job, eula_id, package_id, vendor, text);

	} else if (g_strcmp0 (ev, "repo-signature-required") == 0) {
		const gchar *repo = NULL;
		const gchar *key_url = NULL;
		const gchar *key_userid = NULL;
		const gchar *key_id = NULL;
		const gchar *key_fingerprint = NULL;
		const gchar *key_timestamp = NULL;
		if (!pk_backend_protocol_get_package_id (root,
							 "package_id",
							 TRUE,
							 &package_id,
							 error) ||
		    !pk_backend_protocol_get_string (root,
						     "repository_name",
						     FALSE,
						     &repo,
						     error) ||
		    !pk_backend_protocol_get_string (root, "key_url", TRUE, &key_url, error) ||
		    !pk_backend_protocol_get_string (root,
						     "key_userid",
						     TRUE,
						     &key_userid,
						     error) ||
		    !pk_backend_protocol_get_string (root, "key_id", TRUE, &key_id, error) ||
		    !pk_backend_protocol_get_string (root,
						     "key_fingerprint",
						     TRUE,
						     &key_fingerprint,
						     error) ||
		    !pk_backend_protocol_get_string (root,
						     "key_timestamp",
						     TRUE,
						     &key_timestamp,
						     error) ||
		    !GET_ENUM (root, "type", pk_sig_type, &value))
			return FALSE;
		pk_backend_job_repo_signature_required (job,
							package_id,
							repo,
							key_url,
							key_userid,
							key_id,
							key_fingerprint,
							key_timestamp,
							value);
	} else {
		g_log (log_domain,
		       G_LOG_LEVEL_WARNING,
		       "ignoring unknown event '%s' from helper",
		       ev);
	}
	return TRUE;
}
