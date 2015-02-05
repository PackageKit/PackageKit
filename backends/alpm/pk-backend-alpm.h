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

/* libalpm up to and including version 9.0.0 (pacman 4.2.0) lack proper
 * versioning. A patch has been proposed, though:
 * https://lists.archlinux.org/pipermail/pacman-dev/2014-December/019759.html
 *
 * If ALPM_VERSION_NUMBER is *not* defined we test for
 * ALPM_EVENT_PACKAGE_OPERATION_START, which is libalpm >= 9.0.0 only and
 * define ALPM_VERSION_NUMBER on our own. */

#ifndef ALPM_VERSION_NUMBER
#	ifndef ALPM_EVENT_PACKAGE_OPERATION_START
#		define	ALPM_VERSION_NUMBER	0x080200
#	else
#		define	ALPM_VERSION_NUMBER	0x090000
#	endif
#endif

typedef struct {
	gsize		environment_initialized;
	alpm_db_t	*localdb;
	alpm_list_t	*syncfirsts;
	alpm_list_t	*holdpkgs;
	alpm_handle_t	*alpm;
	GFileMonitor    *monitor;
} PkBackendAlpmPrivate;

void		 pk_alpm_run		(PkBackendJob *job, PkStatusEnum status,
					 PkBackendJobThreadFunc func, gpointer data);

gboolean	 pk_alpm_finish		(PkBackendJob *job, GError *error);
