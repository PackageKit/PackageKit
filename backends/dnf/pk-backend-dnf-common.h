/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __PK_BACKEND_DNF_COMMON_H
#define __PK_BACKEND_DNF_COMMON_H

#include <glib.h>

G_BEGIN_DECLS

gboolean	pk_backend_setup_dnf_context (DnfContext *context,
					      GKeyFile *conf,
					      const gchar *release_ver,
					      GError **error);
gboolean	dnf_utils_refresh_repo_appstream (DnfRepo *repo, GError **error);

G_END_DECLS

#endif /* __PK_BACKEND_DNF_COMMON_H */
