#pragma once

#include <glib.h>


typedef struct apk_ctx ApkContext;
void free_apk_ctx(struct apk_ctx *ctx);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkContext, free_apk_ctx);

typedef struct apk_database ApkDatabase;
void free_apk_database(struct apk_database *db);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkDatabase, free_apk_database)

typedef struct apk_package_array ApkPackageArray;
void free_apk_package_array(struct apk_package_array *array);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkPackageArray, free_apk_package_array)

typedef struct apk_string_array ApkStringArray;
void free_apk_string_array(struct apk_string_array *array);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ApkStringArray, free_apk_string_array)