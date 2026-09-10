/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Jane Rachinger <jane400@postmarketos.org>
 */

#pragma once
#include "pk-enum.h"

typedef char gchar;

struct Match {
  gchar **prefix;
  gchar **suffix;
  PkGroupEnum group_enum;
};

static const struct Match **MATCHES;