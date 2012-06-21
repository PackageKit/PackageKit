/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_BACKEND_JOB_H
#define __PK_BACKEND_JOB_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PK_TYPE_BACKEND_JOB		(pk_backend_job_get_type ())
#define PK_BACKEND_JOB(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_BACKEND_JOB, PkBackendJob))
#define PK_BACKEND_JOB_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_BACKEND_JOB, PkBackendJobClass))
#define PK_IS_BACKEND_JOB(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_BACKEND_JOB))
#define PK_IS_BACKEND_JOB_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_BACKEND_JOB))
#define PK_BACKEND_JOB_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_BACKEND_JOB, PkBackendJobClass))

typedef struct PkBackendJobPrivate PkBackendJobPrivate;

/* this is used to connect/disconnect backend signals */
typedef enum {
	PK_BACKEND_SIGNAL_ALLOW_CANCEL,
	PK_BACKEND_SIGNAL_DETAILS,
	PK_BACKEND_SIGNAL_ERROR_CODE,
	PK_BACKEND_SIGNAL_DISTRO_UPGRADE,
	PK_BACKEND_SIGNAL_FINISHED,
	PK_BACKEND_SIGNAL_MESSAGE,
	PK_BACKEND_SIGNAL_PACKAGE,
	PK_BACKEND_SIGNAL_ITEM_PROGRESS,
	PK_BACKEND_SIGNAL_FILES,
	PK_BACKEND_SIGNAL_PERCENTAGE,
	PK_BACKEND_SIGNAL_REMAINING,
	PK_BACKEND_SIGNAL_SPEED,
	PK_BACKEND_SIGNAL_DOWNLOAD_SIZE_REMAINING,
	PK_BACKEND_SIGNAL_REPO_DETAIL,
	PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED,
	PK_BACKEND_SIGNAL_EULA_REQUIRED,
	PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED,
	PK_BACKEND_SIGNAL_REQUIRE_RESTART,
	PK_BACKEND_SIGNAL_STATUS_CHANGED,
	PK_BACKEND_SIGNAL_UPDATE_DETAIL,
	PK_BACKEND_SIGNAL_CATEGORY,
	PK_BACKEND_SIGNAL_LAST
} PkBackendSignal;

typedef struct
{
	GObject			 parent;
	PkBackendJobPrivate	*priv;
} PkBackendJob;

typedef struct
{
	GObjectClass	parent_class;
} PkBackendJobClass;

GType		 pk_backend_job_get_type		(void);
PkBackendJob	*pk_backend_job_new			(void);

gpointer	 pk_backend_job_get_backend		(PkBackendJob	*job);
void		 pk_backend_job_set_backend		(PkBackendJob	*job,
							 gpointer	 backend);
gpointer	 pk_backend_job_get_user_data		(PkBackendJob	*job);
void		 pk_backend_job_set_user_data		(PkBackendJob	*job,
							 gpointer	 user_data);

G_END_DECLS

#endif /* __PK_BACKEND_JOB_H */

