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

#ifndef __HIF_BACKEND_H
#define __HIF_BACKEND_H

#include <glib.h>

#include <libdnf/dnf-advisory.h>
#include <libdnf/dnf-package.h>

#include <pk-backend.h>

PkInfoEnum	 dnf_advisory_kind_to_info_enum	(DnfAdvisoryKind	 kind);
void		 dnf_emit_package		(PkBackendJob		*job,
						 PkInfoEnum		 info,
						 DnfPackage		*pkg);
void		 dnf_emit_package_list		(PkBackendJob		*job,
						 PkInfoEnum		 info,
						 GPtrArray		*pkglist);
void		 dnf_emit_package_array		(PkBackendJob		*job,
						 PkInfoEnum		 info,
						 GPtrArray		*array);
void		 dnf_emit_package_list_filter	(PkBackendJob		*job,
						 PkBitfield		 filters,
						 GPtrArray		*pkglist);
PkBitfield	 dnf_get_filter_for_ids		(gchar			**package_ids);

G_END_DECLS

#endif /* __HIF_BACKEND_H */
