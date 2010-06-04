/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_PACKAGE_SACK_SYNC_H
#define __PK_PACKAGE_SACK_SYNC_H

#include <glib.h>
#include <packagekit-glib2/pk-package-sack.h>

G_BEGIN_DECLS

gboolean	 pk_package_sack_resolve		(PkPackageSack		*package_sack,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean	 pk_package_sack_get_details		(PkPackageSack		*package_sack,
							 GCancellable		*cancellable,
							 GError			**error);
gboolean	 pk_package_sack_get_update_detail	(PkPackageSack		*package_sack,
							 GCancellable		*cancellable,
							 GError			**error);

G_END_DECLS

#endif /* __PK_PACKAGE_SACK_SYNC_H */

