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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>

#include "pk-time.h"

static void     pk_time_finalize	(GObject     *object);

#define PK_TIME_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TIME, PkTimePrivate))
#define PK_TIME_AVERAGE_DEFAULT_MIN		4     /* samples */
#define PK_TIME_AVERAGE_DEFAULT_MAX		10    /* samples */
#define PK_TIME_VALUE_DEFAULT_MIN		5     /* s */
#define PK_TIME_VALUE_DEFAULT_MAX		60*60 /* s */

struct PkTimePrivate
{
	guint			 time_offset; /* ms */
	guint			 last_percentage;
	guint			 average_min;
	guint			 average_max;
	guint			 value_min;
	guint			 value_max;
	GPtrArray		*array;
	GTimer			*timer;
};

typedef struct {
	guint			 percentage;
	guint			 time;
} PkTimeItem;

G_DEFINE_TYPE (PkTime, pk_time, G_TYPE_OBJECT)

/**
 * pk_time_set_average_limits:
 * @time: This class instance
 * @average_min: the smallest number of samples we will try to average
 * @average_max: the largest number of past samples we will try to average
 *
 * Return value: if we set the average limits correctly
 **/
gboolean
pk_time_set_average_limits (PkTime *pktime, guint average_min, guint average_max)
{
	g_return_val_if_fail (PK_IS_TIME (pktime), FALSE);
	pktime->priv->average_min = average_min;
	pktime->priv->average_max = average_max;
	return TRUE;
}

/**
 * pk_time_set_value_limits:
 * @pktime: This class instance
 * @average_min: the smallest value that is acceptable for time (in seconds)
 * @average_max: the largest value that is acceptable for time (in seconds)
 *
 * Return value: if we set the value limits correctly
 **/
gboolean
pk_time_set_value_limits (PkTime *pktime, guint value_min, guint value_max)
{
	g_return_val_if_fail (PK_IS_TIME (pktime), FALSE);
	pktime->priv->value_min = value_min;
	pktime->priv->value_max = value_max;
	return TRUE;
}

/**
 * pk_time_get_elapsed:
 *
 * Returns time running in ms
 **/
guint
pk_time_get_elapsed (PkTime *pktime)
{
	gdouble elapsed;

	g_return_val_if_fail (PK_IS_TIME (pktime), 0);

	elapsed = g_timer_elapsed (pktime->priv->timer, NULL);
	elapsed *= 1000;
	elapsed += pktime->priv->time_offset;

	return (guint) elapsed;
}

/**
 * pk_time_get_gradient:
 **/
static gfloat
pk_time_get_gradient (PkTimeItem *item1, PkTimeItem *item2)
{
	gfloat dy;
	gfloat dx;
	if (item1->time == item2->time)
		return 0;
	dy = (gfloat) (item1->percentage - item2->percentage);
	dx = (gfloat) (item1->time - item2->time);
	return dy/dx;
}

/**
 * pk_time_get_remaining:
 *
 * Returns time in seconds
 **/
guint
pk_time_get_remaining (PkTime *pktime)
{
	guint i;
	guint averaged = 0;
	guint length;
	gfloat grad;
	gfloat grad_ave = 0.0f;
	gfloat estimated;
	guint percentage_left;
	PkTimeItem *item;
	PkTimeItem *item_prev;

	g_return_val_if_fail (PK_IS_TIME (pktime), 0);

	/* check array is large enough */
	length = pktime->priv->array->len;
	if (length < 2)
		return 0;

	/* get as many as we can */
	for (i=length-1; i>0; i--) {
		item_prev = g_ptr_array_index (pktime->priv->array, i-1);
		item = g_ptr_array_index (pktime->priv->array, i);
		grad = pk_time_get_gradient (item, item_prev);
		if (grad < 0.00001 || grad > 100) {
			/* ignoring gradient */
		} else {
			grad_ave += grad;
			averaged++;
			if (averaged > pktime->priv->average_max) {
				break;
			}
		}
	}

	/* not enough samples for accurate time */
	if (averaged < pktime->priv->average_min)
		return 0;


	/* no valid gradients */
	if (averaged  == 0)
		return 0;

	/* normalise to the number of samples */
	grad_ave /= averaged;

	/* 100 percent to be complete */
	item = g_ptr_array_index (pktime->priv->array, length - 1);
	percentage_left = 100 - item->percentage;
	estimated = (gfloat) percentage_left / grad_ave;

	/* turn to ms */
	estimated /= 1000;
	g_debug ("percentage_left=%i, estimated=%.0fms",
		 percentage_left, estimated * 1000);

	if (estimated < pktime->priv->value_min) {
		estimated = 0;
	} else if (estimated > pktime->priv->value_max) {
		estimated = 0;
	}
	return (guint) estimated;
}

/**
 * pk_time_add_data:
 **/
gboolean
pk_time_add_data (PkTime *pktime, guint percentage)
{
	PkTimeItem *item;
	guint elapsed;

	g_return_val_if_fail (PK_IS_TIME (pktime), FALSE);

	/* check we are going up */
	if (percentage < pktime->priv->last_percentage) {
		g_warning ("percentage cannot go down!");
		return FALSE;
	}
	pktime->priv->last_percentage = percentage;

	/* get runtime in ms */
	elapsed = pk_time_get_elapsed (pktime);

	/* create a new object and add to the array */
	item = g_new0 (PkTimeItem, 1);
	item->time = elapsed;
	item->percentage = percentage;
	g_ptr_array_add (pktime->priv->array, item);

	return TRUE;
}

/**
 * pk_time_advance_clock:
 *
 * This function is only really useful for testing the PkTime functionality.
 **/
void
pk_time_advance_clock (PkTime *pktime, guint offset)
{
	pktime->priv->time_offset += offset;
}

/**
 * pk_time_free_data:
 **/
static gboolean
pk_time_free_data (PkTime *pktime)
{
	guint i;
	guint length;
	gpointer mem;

	g_return_val_if_fail (PK_IS_TIME (pktime), FALSE);

	length = pktime->priv->array->len;
	for (i=0; i<length; i++) {
		mem = g_ptr_array_index (pktime->priv->array, 0);
		g_ptr_array_remove_index (pktime->priv->array, 0);
		g_free (mem);
	}
	return TRUE;
}

/**
 * pk_time_reset:
 **/
gboolean
pk_time_reset (PkTime *pktime)
{
	g_return_val_if_fail (PK_IS_TIME (pktime), FALSE);

	pktime->priv->time_offset = 0;
	pktime->priv->last_percentage = 0;
	pktime->priv->average_min = PK_TIME_AVERAGE_DEFAULT_MIN;
	pktime->priv->average_max = PK_TIME_AVERAGE_DEFAULT_MAX;
	pktime->priv->value_min = PK_TIME_VALUE_DEFAULT_MIN;
	pktime->priv->value_max = PK_TIME_VALUE_DEFAULT_MAX;
	g_timer_reset (pktime->priv->timer);
	pk_time_free_data (pktime);

	return TRUE;
}

/**
 * pk_time_class_init:
 * @klass: The PkTimeClass
 **/
static void
pk_time_class_init (PkTimeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_time_finalize;
	g_type_class_add_private (klass, sizeof (PkTimePrivate));
}

/**
 * pk_time_init:
 * @time: This class instance
 **/
static void
pk_time_init (PkTime *pktime)
{
	pktime->priv = PK_TIME_GET_PRIVATE (pktime);
	pktime->priv->array = g_ptr_array_new ();
	pktime->priv->timer = g_timer_new ();
	pk_time_reset (pktime);
}

/**
 * pk_time_finalize:
 * @object: The object to finalize
 **/
static void
pk_time_finalize (GObject *object)
{
	PkTime *pktime;

	g_return_if_fail (PK_IS_TIME (object));

	pktime = PK_TIME (object);
	g_return_if_fail (pktime->priv != NULL);
	g_ptr_array_foreach (pktime->priv->array, (GFunc) g_free, NULL);
	g_ptr_array_free (pktime->priv->array, TRUE);
	g_timer_destroy (pktime->priv->timer);

	G_OBJECT_CLASS (pk_time_parent_class)->finalize (object);
}

/**
 * pk_time_new:
 *
 * Return value: a new PkTime object.
 **/
PkTime *
pk_time_new (void)
{
	PkTime *pktime;
	pktime = g_object_new (PK_TYPE_TIME, NULL);
	return PK_TIME (pktime);
}

