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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>

#include <glib/gi18n.h>
#include <glib.h>
#include <packagekit-glib/packagekit.h>

#include <config.h>

#include "egg-debug.h"
#include "pk-security.h"

#define PK_SECURITY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SECURITY, PkSecurityPrivate))

struct PkSecurityPrivate
{
	gpointer		data;
};

typedef gpointer PkSecurityCaller_;

G_DEFINE_TYPE (PkSecurity, pk_security, G_TYPE_OBJECT)

/**
 * pk_security_caller_new_from_sender:
 **/
PkSecurityCaller *
pk_security_caller_new_from_sender (PkSecurity *security, const gchar *sender)
{
	g_return_val_if_fail (PK_IS_SECURITY (security), NULL);
	return NULL;
}

/**
 * pk_security_caller_unref:
 **/
void
pk_security_caller_unref (PkSecurityCaller *caller)
{
	return;
}

/**
 * pk_security_get_uid:
 **/
guint
pk_security_get_uid (PkSecurity *security, PkSecurityCaller *caller)
{
	g_return_val_if_fail (PK_IS_SECURITY (security), -1);
	return -1;
}

/**
 * pk_security_get_cmdline:
 **/
gchar *
pk_security_get_cmdline (PkSecurity *security, PkSecurityCaller *caller)
{
	g_return_val_if_fail (PK_IS_SECURITY (security), NULL);
	return NULL;
}

/**
 * pk_security_action_is_allowed:
 **/
gboolean
pk_security_action_is_allowed (PkSecurity *security, PkSecurityCaller *caller, gboolean trusted, PkRoleEnum role, gchar **error_detail)
{
	g_return_val_if_fail (PK_IS_SECURITY (security), FALSE);
	return TRUE;
}

/**
 * pk_security_finalize:
 **/
static void
pk_security_finalize (GObject *object)
{
	PkSecurity *security;
	g_return_if_fail (PK_IS_SECURITY (object));
	security = PK_SECURITY (object);
	G_OBJECT_CLASS (pk_security_parent_class)->finalize (object);
}

/**
 * pk_security_class_init:
 **/
static void
pk_security_class_init (PkSecurityClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_security_finalize;
	g_type_class_add_private (klass, sizeof (PkSecurityPrivate));
}

/**
 * pk_security_init:
 *
 * initializes the security class. NOTE: We expect security objects
 * to *NOT* be removed or added during the session.
 * We only control the first security object if there are more than one.
 **/
static void
pk_security_init (PkSecurity *security)
{
	egg_debug ("Using dummy security framework");
	egg_warning ("*** THERE IS NO SECURITY MODEL BEING USED!!! ***");
}

/**
 * pk_security_new:
 * Return value: A new security class instance.
 **/
PkSecurity *
pk_security_new (void)
{
	PkSecurity *security;
	security = g_object_new (PK_TYPE_SECURITY, NULL);
	return PK_SECURITY (security);
}

#ifdef EGG_TEST
#include "egg-test.h"

void
pk_security_test (EggTest *test)
{
	return;
}
#endif

