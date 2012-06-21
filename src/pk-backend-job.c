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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>

#include "pk-backend.h"
#include "pk-backend-job.h"

#define PK_BACKEND_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_JOB, PkBackendJobPrivate))

struct PkBackendJobPrivate
{
	gpointer		 user_data;
	PkBackend		*backend;
};

G_DEFINE_TYPE (PkBackendJob, pk_backend_job, G_TYPE_OBJECT)

/**
 * pk_backend_job_get_backend:
 **/
gpointer
pk_backend_job_get_backend (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return job->priv->backend;
}

/**
 * pk_backend_job_set_backend:
 **/
void
pk_backend_job_set_backend (PkBackendJob *job, gpointer backend)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->backend = backend;
}

/**
 * pk_backend_job_get_user_data:
 **/
gpointer
pk_backend_job_get_user_data (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return job->priv->user_data;
}

/**
 * pk_backend_job_set_user_data:
 **/
void
pk_backend_job_set_user_data (PkBackendJob *job, gpointer user_data)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->user_data = user_data;
}

/**
 * pk_backend_job_finalize:
 **/
static void
pk_backend_job_finalize (GObject *object)
{
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_BACKEND_JOB (object));

	G_OBJECT_CLASS (pk_backend_job_parent_class)->finalize (object);
}

/**
 * pk_backend_job_class_init:
 **/
static void
pk_backend_job_class_init (PkBackendJobClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_job_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendJobPrivate));
}

/**
 * pk_backend_job_init:
 **/
static void
pk_backend_job_init (PkBackendJob *job)
{
	job->priv = PK_BACKEND_JOB_GET_PRIVATE (job);
}

/**
 * pk_backend_job_new:
 * Return value: A new job class instance.
 **/
PkBackendJob *
pk_backend_job_new (void)
{
	PkBackendJob *job;
	job = g_object_new (PK_TYPE_BACKEND_JOB, NULL);
	return PK_BACKEND_JOB (job);
}

