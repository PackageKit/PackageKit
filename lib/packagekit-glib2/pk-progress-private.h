/*
 * Copyright 2025 Corentin Noël <corentin.noel@collabora.com>
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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_PROGRESS_PRIVATE_H
#define __PK_PROGRESS_PRIVATE_H

#include <glib-object.h>
#include <packagekit-glib2/pk-progress.h>

G_BEGIN_DECLS

PkProgress *pk_progress_new_with_callback	(PkProgressCallback	 callback,
						 gpointer		 user_data);

G_END_DECLS

#endif /* __PK_PROGRESS_PRIVATE_H */
