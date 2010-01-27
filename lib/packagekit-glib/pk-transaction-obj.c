/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-transaction-obj
 * @short_description: Functionality to create a transaction struct
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-transaction-obj.h>

#include "egg-debug.h"

/**
 * pk_transaction_obj_new:
 *
 * Creates a new #PkTransactionObj object with default values
 *
 * Return value: a new #PkTransactionObj object
 **/
PkTransactionObj *
pk_transaction_obj_new (void)
{
	PkTransactionObj *obj;
	obj = g_new0 (PkTransactionObj, 1);
	obj->tid = NULL;
	obj->timespec = NULL;
	obj->succeeded = FALSE;
	obj->role = PK_ROLE_ENUM_UNKNOWN;
	obj->duration = 0;
	obj->data = NULL;
	obj->uid = 0;
	obj->cmdline = NULL;
	return obj;
}

/**
 * pk_transaction_obj_new_from_data:
 *
 * Creates a new #PkTransactionObj object with values.
 *
 * Return value: a new #PkTransactionObj object
 **/
PkTransactionObj *
pk_transaction_obj_new_from_data (const gchar *tid, const gchar *timespec,
				  gboolean succeeded, PkRoleEnum role,
				  guint duration, const gchar *data,
				  guint uid, const gchar *cmdline)
{
	PkTransactionObj *obj = NULL;

	/* create new object */
	obj = pk_transaction_obj_new ();
	obj->tid = g_strdup (tid);
	obj->timespec = g_strdup (timespec);
	obj->succeeded = succeeded;
	obj->role = role;
	obj->duration = duration;
	obj->data = g_strdup (data);
	obj->uid = uid;
	obj->cmdline = g_strdup (cmdline);
	return obj;
}

/**
 * pk_transaction_obj_copy:
 *
 * Return value: a new #PkTransactionObj object
 **/
PkTransactionObj *
pk_transaction_obj_copy (const PkTransactionObj *obj)
{
	g_return_val_if_fail (obj != NULL, NULL);
	return pk_transaction_obj_new_from_data (obj->tid, obj->timespec,
						 obj->succeeded, obj->role,
						 obj->duration, obj->data,
						 obj->uid, obj->cmdline);
}

/**
 * pk_transaction_obj_free:
 * @obj: the #PkTransactionObj object
 *
 * Return value: %TRUE if the #PkTransactionObj object was freed.
 **/
gboolean
pk_transaction_obj_free (PkTransactionObj *obj)
{
	if (obj == NULL) {
		return FALSE;
	}
	g_free (obj->tid);
	g_free (obj->timespec);
	g_free (obj->data);
	g_free (obj->cmdline);
	g_free (obj);
	return TRUE;
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
egg_test_transaction (EggTest *test)
{
	gboolean ret;
	PkTransactionObj *obj;

	if (!egg_test_start (test, "PkTransactionObj"))
		return;

	/************************************************************
	 ****************          IDENT           ******************
	 ************************************************************/

	/************************************************************/
	egg_test_title (test, "get an upgrade object");
	obj = pk_transaction_obj_new ();
	egg_test_assert (test, obj != NULL);

	/************************************************************/
	egg_test_title (test, "test upgrade");
	ret = pk_transaction_obj_free (obj);
	egg_test_assert (test, ret);

	egg_test_end (test);
}
#endif

