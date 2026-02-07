#pragma once

#include "pk-backend.h"

#include "pk-backend-job.h"

#include <apk/apk_database.h>
#include <apk/apk_defines.h>
#include <apk/apk_package.h>

gint pk_apk_query(PkBackend *backend, PkBackendJob *job, PkBitfield filters,
                  struct apk_ctx *ctx, struct apk_database *db, gchar **search,
                  guint64 apk_query_flags, gboolean mode_search,
                  gboolean as_details);

gint pk_apk_find_package_id(PkBackend *backend, PkBackendJob *job,
                            struct apk_ctx *ctx, struct apk_database *db,
                            gchar **package_ids, struct apk_package_array **out,
                            struct apk_string_array **failed_out);
