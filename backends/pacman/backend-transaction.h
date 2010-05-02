/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008, 2009 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010 Jonathan Conder <j@skurvy.no-ip.org>
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

#include <pacman.h>
#include <pk-backend.h>

typedef enum {
	PACMAN_TRANSACTION_INSTALL,
	PACMAN_TRANSACTION_REMOVE,
	PACMAN_TRANSACTION_SYNC,
	PACMAN_TRANSACTION_UPDATE,
	PACMAN_TRANSACTION_LAST
} PacmanTransactionType;

gboolean		 backend_initialize_downloads	(PkBackend		*backend,
							 GError			**error);
void			 backend_destroy_downloads	(PkBackend		*backend);

PacmanTransaction	*backend_transaction_simulate	(PkBackend		*backend,
							 PacmanTransactionType	 type,
							 guint32		 flags,
							 const PacmanList	*targets);
PacmanTransaction	*backend_transaction_run	(PkBackend		*backend,
							 PacmanTransactionType	 type,
							 guint32		 flags,
							 const PacmanList	*targets);

gboolean		 backend_transaction_finished	(PkBackend		*backend,
							 PacmanTransaction	*transaction);
