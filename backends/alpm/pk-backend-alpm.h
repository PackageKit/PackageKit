/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Andreas Obergrusberger <tradiaz@yahoo.de>
 * Copyright (C) 2008-2010 Valeriy Lyasotskiy <onestep@ukr.net>
 * Copyright (C) 2010-2011 Jonathan Conder <jonno.conder@gmail.com>
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

#include <alpm.h>
#include <gio/gio.h>
#include <pk-backend.h>

extern PkBackend *backend;
extern GCancellable *cancellable;

extern alpm_handle_t *alpm;
extern pmdb_t *localdb;

extern gchar *xfercmd;
extern alpm_list_t *holdpkgs;
extern alpm_list_t *syncfirsts;

gint		 pk_backend_fetchcb	(const gchar *url, const gchar *path,
					 gint force);

void		 pk_backend_run		(PkBackend *self, PkStatusEnum status,
					 PkBackendThreadFunc func);

void		 pk_backend_cancel	(PkBackend *self);

gboolean	 pk_backend_cancelled	(PkBackend *self);

gboolean	 pk_backend_finish	(PkBackend *self, GError *error);
