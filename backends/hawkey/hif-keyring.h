/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __HIF_KEYRING_H
#define __HIF_KEYRING_H

#include <glib.h>

#include <rpm/rpmkeyring.h>

gboolean	 hif_keyring_add_public_key	(rpmKeyring		 keyring,
						 const gchar		*filename,
						 GError			**error);
gboolean	 hif_keyring_add_public_keys	(rpmKeyring		 keyring,
						 GError			**error);
gboolean	 hif_keyring_check_untrusted_file (rpmKeyring		 keyring,
						 const gchar		*filename,
						 GError			**error);

#endif /* __HIF_KEYRING_H */
