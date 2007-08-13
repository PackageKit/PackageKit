/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-task-common.h"

/**
 * pk_task_common_exit_from_text:
 */
PkTaskExit
pk_task_common_exit_from_text (const gchar *exit)
{
	if (strcmp (exit, "success") == 0) {
		return PK_TASK_COMMON_EXIT_SUCCESS;
	}
	if (strcmp (exit, "failed") == 0) {
		return PK_TASK_COMMON_EXIT_FAILED;
	}
	if (strcmp (exit, "canceled") == 0) {
		return PK_TASK_COMMON_EXIT_CANCELED;
	}
	return PK_TASK_COMMON_EXIT_UNKNOWN;
}

/**
 * pk_task_common_status_from_text:
 **/
PkTaskStatus
pk_task_common_status_from_text (const gchar *status)
{
	if (strcmp (status, "setup") == 0) {
		return PK_TASK_COMMON_STATUS_SETUP;
	}
	if (strcmp (status, "query") == 0) {
		return PK_TASK_COMMON_STATUS_QUERY;
	}
	if (strcmp (status, "remove") == 0) {
		return PK_TASK_COMMON_STATUS_REMOVE;
	}
	if (strcmp (status, "download") == 0) {
		return PK_TASK_COMMON_STATUS_DOWNLOAD;
	}
	if (strcmp (status, "install") == 0) {
		return PK_TASK_COMMON_STATUS_INSTALL;
	}
	if (strcmp (status, "update") == 0) {
		return PK_TASK_COMMON_STATUS_UPDATE;
	}
	if (strcmp (status, "exit") == 0) {
		return PK_TASK_COMMON_STATUS_EXIT;
	}
	return PK_TASK_COMMON_STATUS_INVALID;
}

