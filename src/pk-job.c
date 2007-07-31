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

#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "pk-job.h"

static void     pk_job_class_init (PkJobClass *klass);
static void     pk_job_init       (PkJob      *job);
static void     pk_job_finalize   (GObject    *object);

#define PK_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_JOB, PkJobPrivate))

struct PkJobPrivate
{
	guint			 current_job;
};

static gpointer pk_job_object = NULL;

G_DEFINE_TYPE (PkJob, pk_job, G_TYPE_OBJECT)

/**
 * pk_job_class_init:
 * @klass: This class instance
 **/
static void
pk_job_class_init (PkJobClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_job_finalize;
	g_type_class_add_private (klass, sizeof (PkJobPrivate));
}

/**
 * pk_job_get_unique:
 */
guint
pk_job_get_unique (PkJob *job)
{
	job->priv->current_job++;
	g_debug ("allocating job %i", job->priv->current_job);
	return job->priv->current_job;
}

/**
 * pk_job_init:
 */
static void
pk_job_init (PkJob *job)
{
	job->priv = PK_JOB_GET_PRIVATE (job);
	job->priv->current_job = 0;
}

/**
 * pk_job_coldplug:
 *
 * @object: This job instance
 */
static void
pk_job_finalize (GObject *object)
{
	PkJob *job;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_JOB (object));

	job = PK_JOB (object);
	g_return_if_fail (job->priv != NULL);
	G_OBJECT_CLASS (pk_job_parent_class)->finalize (object);
}

/**
 * pk_job_new:
 * Return value: new PkJob instance.
 **/
PkJob *
pk_job_new (void)
{
	if (pk_job_object != NULL) {
		g_object_ref (pk_job_object);
	} else {
		pk_job_object = g_object_new (PK_TYPE_JOB, NULL);
		g_object_add_weak_pointer (pk_job_object, &pk_job_object);
	}
	return PK_JOB (pk_job_object);
}

