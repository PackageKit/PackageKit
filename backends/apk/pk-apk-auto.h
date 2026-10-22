/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#pragma once

#include <apk/apk_context.h>
#include <apk/apk_database.h>
#include <apk/apk_package.h>
#include <glib.h>

#define _pk_apk_auto(_type)                                                    \
  __attribute__((cleanup(__free_##_type))) struct _type

#define DEFINE_PK_APK_FREE_PTR(_type)                                          \
  static inline void __free_##_type(struct _type **p) {                        \
    if (*p != NULL)                                                            \
      _type##_free(p);                                                         \
  }

DEFINE_PK_APK_FREE_PTR(apk_package_array)
DEFINE_PK_APK_FREE_PTR(apk_string_array)
DEFINE_PK_APK_FREE_PTR(apk_dependency_array)

static inline void __free_apk_ctx(struct apk_ctx *ac) { apk_ctx_free(ac); }

#undef DEFINE_PK_APK_FREE_PTR
