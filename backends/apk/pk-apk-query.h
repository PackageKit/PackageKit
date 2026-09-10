/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#pragma once

// clang-format off
#include "pk-backend.h"
// clang-format on
#include "pk-backend-job.h"

struct apk_ctx;
struct apk_database;
struct apk_package_array;
struct apk_string_array;

gint pk_apk_query(PkBackend *backend, PkBackendJob *job, PkBitfield filters,
                  struct apk_ctx *ctx, struct apk_database *db, gchar **search,
                  guint64 apk_query_flags, gboolean mode_search,
                  gboolean as_details);

gint pk_apk_find_package_id(PkBackend *backend, PkBackendJob *job,
                            struct apk_ctx *ctx, struct apk_database *db,
                            gchar **package_ids, struct apk_package_array **out,
                            struct apk_string_array **failed_out);

void pk_apk_query_from_files(PkBackend *backend, PkBackendJob *job,
                             struct apk_ctx *ctx, struct apk_database *db,
                             gchar **files, gboolean as_details);