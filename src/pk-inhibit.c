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

#ifdef HAVE_INHIBITIG_H
#  include <inhibitig.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib/gi18n.h>
#include <glib.h>

#include "pk-debug.h"
#include "pk-inhibit.h"

#define PK_INHIBIT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_INHIBIT, PkInhibitPrivate))

struct PkInhibitPrivate
{
	GPtrArray		*array;
};

G_DEFINE_TYPE (PkInhibit, pk_inhibit, G_TYPE_OBJECT)
static gpointer pk_inhibit_object = NULL;

/**
 * pk_inhibit_add:
 **/
gboolean
pk_inhibit_add (PkInhibit *inhibit, gpointer data)
{
	g_return_val_if_fail (inhibit != NULL, FALSE);
	g_return_val_if_fail (PK_IS_INHIBIT (inhibit), FALSE);
	return TRUE;
}

/**
 * pk_inhibit_remove:
 **/
gboolean
pk_inhibit_remove (PkInhibit *inhibit, gpointer data)
{
	g_return_val_if_fail (inhibit != NULL, FALSE);
	g_return_val_if_fail (PK_IS_INHIBIT (inhibit), FALSE);
	return TRUE;
}

/**
 * pk_inhibit_finalize:
 **/
static void
pk_inhibit_finalize (GObject *object)
{
	PkInhibit *inhibit;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_INHIBIT (object));
	inhibit = PK_INHIBIT (object);

	g_ptr_array_free (inhibit->priv->array, TRUE);

	G_OBJECT_CLASS (pk_inhibit_parent_class)->finalize (object);
}

/**
 * pk_inhibit_class_init:
 **/
static void
pk_inhibit_class_init (PkInhibitClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_inhibit_finalize;
	g_type_class_add_private (klass, sizeof (PkInhibitPrivate));
}

/**
 * pk_inhibit_init:
 *
 * initialises the inhibit class. NOTE: We expect inhibit objects
 * to *NOT* be removed or added during the session.
 * We only control the first inhibit object if there are more than one.
 **/
static void
pk_inhibit_init (PkInhibit *inhibit)
{
	inhibit->priv = PK_INHIBIT_GET_PRIVATE (inhibit);
	inhibit->priv->array = g_ptr_array_new ();
}

/**
 * pk_inhibit_new:
 * Return value: A new inhibit class instance.
 **/
PkInhibit *
pk_inhibit_new (void)
{
	if (pk_inhibit_object != NULL) {
		g_object_ref (pk_inhibit_object);
	} else {
		pk_inhibit_object = g_object_new (PK_TYPE_INHIBIT, NULL);
		g_object_add_weak_pointer (pk_inhibit_object, &pk_inhibit_object);
	}
	return PK_INHIBIT (pk_inhibit_object);
}

