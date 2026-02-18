/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#include "matches.h"

// clang-format off
#define _PREFIX(_args) _args
#define _SUFFIX(_args) _args
#define STRUCT_PREFIX(_name) .prefix = _##_name##_prefix
#define STRUCT_SUFFIX(_name) .suffix = _##_name##_suffix
#define MAT_STRUCT_PREFIX(...) STRUCT_PREFIX
#define MAT_STRUCT_SUFFIX(...) STRUCT_SUFFIX
#define MAT_PREFIX(...) VALUE_PREFIX
#define MAT_SUFFIX(...) VALUE_SUFFIX

#define LEN_PREFIX(_args) LEN(##_args)

#define VALUE_PREFIX(_name, ...)                                         \
  static const gchar **_##_name##_prefix = { __VA_ARGS__, NULL}
#define VALUE_SUFFIX(_name, ...)                                         \
  static const gchar **_##_name##_suffix = { __VA_ARGS__, NULL}

#define MATCH(_name, _group_enum, arg1, arg2) \
    MAT_##arg1(_name, _##arg1); \
    MAT_##arg2(_name, _##arg2); \
    \
    static const struct Match match_##_name = { \
          .group_enum = _group_enum, \
          MAT_STRUCT_##arg1(_name), \
          MAT_STRUCT_##arg2(_name), \
  };

#include "matches_inc.h"
#undef MATCH

#define MATCH(name, _args...) &match##_name,

static const struct Match **MATCHES = {
#include "matches_inc.h"
    NULL,
};

#undef MATCH
// clang-format on