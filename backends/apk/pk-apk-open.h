/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#pragma once

// clang-format off
#include "pk-backend.h"
// clang-format on
#include "pk-backend-job.h"

#include <apk/apk_context.h>
#include <apk/apk_database.h>
#include <apk/apk_defines.h>
#include <apk/apk_package.h>

typedef struct apk_database ApkDatabase;
void free_apk_database(struct apk_database *db);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkDatabase, free_apk_database)

typedef struct {
  guint apk_flags;
  gboolean force_refresh_cache;
  const gchar *cache_dir;
} OpenApkOptions;

int open_apk(PkBackend *backend, PkBackendJob *job, OpenApkOptions options, struct apk_ctx *ctx,
             struct apk_database *db);

int check_world(PkBackendJob *job, struct apk_database *db);