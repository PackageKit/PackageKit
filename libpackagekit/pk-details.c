/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-details
 * @short_description: Functionality to create a details struct
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include <pk-enum.h>
#include "pk-debug.h"
#include "pk-common.h"
#include "pk-details.h"

/**
 * pk_details_new:
 *
 * Creates a new #PkDetails object with default values
 *
 * Return value: a new #PkDetails object
 **/
PkDetails *
pk_details_new (void)
{
	PkDetails *detail;
	detail = g_new0 (PkDetails, 1);
	detail->package_id = NULL;
	detail->license = NULL;
	detail->group = 0;
	detail->description = NULL;
	detail->url = NULL;
	detail->size = 0;

	return detail;
}

/**
 * pk_details_new_from_data:
 *
 * Creates a new #PkDetails object with values.
 *
 * Return value: a new #PkDetails object
 **/
PkDetails *
pk_details_new_from_data (const gchar *package_id, const gchar *license, PkGroupEnum group,
			  const gchar *description, const gchar *url, guint64 size)
{
	PkDetails *detail = NULL;

	/* create new object */
	detail = pk_details_new ();
	detail->package_id = g_strdup (package_id);
	detail->license = g_strdup (license);
	detail->group = group;
	detail->description = g_strdup (description);
	detail->url = g_strdup (url);
	detail->size = size;

	return detail;
}

/**
 * pk_details_free:
 * @detail: the #PkDetails object
 *
 * Return value: %TRUE if the #PkDetails object was freed.
 **/
gboolean
pk_details_free (PkDetails *detail)
{
	if (detail == NULL) {
		return FALSE;
	}
	g_free (detail->package_id);
	g_free (detail->license);
	g_free (detail->description);
	g_free (detail->url);
	g_free (detail);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_details (LibSelfTest *test)
{
	gboolean ret;
	PkDetails *detail;

	if (libst_start (test, "PkDetails", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get an details object");
	detail = pk_details_new ();
	if (detail != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "test details");
	ret = pk_details_free (detail);
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	pk_details_free (detail);

	libst_end (test);
}
#endif

