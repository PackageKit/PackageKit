#pragma once

#include <glib.h>

#include "pk-backend.h"

#include "pk-backend-job.h"
#include <apk/apk_package.h>

// https://blog.sebastianwick.net/posts/glib-ownership-best-practices/

#define _pk_apk_auto(_type)                                                    \
  __attribute__((cleanup(__free_##_type))) struct _type

#define DEFINE_PK_APK_FREE(_type)                                              \
  static inline void __free_##_type(struct _type **p) {                        \
    if (*p != NULL)                                                            \
      _type##_free(p);                                                         \
  }

DEFINE_PK_APK_FREE(apk_package_array)
DEFINE_PK_APK_FREE(apk_string_array)
DEFINE_PK_APK_FREE(apk_dependency_array)

#undef DEFINE_PK_APK_FREE

typedef struct apk_ctx ApkContext;
void free_apk_ctx(struct apk_ctx *ctx);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkContext, free_apk_ctx);

typedef struct apk_database ApkDatabase;
void free_apk_database(struct apk_database *db);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkDatabase, free_apk_database)

typedef struct {
  guint apk_flags;
  gboolean force_refresh_cache;
  const gchar *cache_dir;
} OpenApkOptions;

int open_apk(OpenApkOptions options, ApkContext **ctx_writeback,
             ApkDatabase **db_writeback);

struct apk_package;

gchar *convert_apk_to_pkgid(struct apk_package *package);
void convert_apk_to_job_details(PkBackendJob *job, struct apk_package *package);
void convert_apk_to_package(PkBackendJob *job, struct apk_package *package);

int check_world(PkBackendJob *job, struct apk_database *db);