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
#include <dbus/dbus-glib.h>

#include <polkit/polkit.h>
#include <polkit-dbus/polkit-dbus.h>

#include <pk-enum.h>

#include "pk-debug.h"
#include "pk-security.h"

#define PK_SECURITY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SECURITY, PkSecurityPrivate))

struct PkSecurityPrivate
{
	PolKitContext		*pk_context;
	DBusConnection		*connection;
};

G_DEFINE_TYPE (PkSecurity, pk_security, G_TYPE_OBJECT)

/**
 * pk_security_can_do_action:
 **/
static PolKitResult
pk_security_can_do_action (PkSecurity *security, const gchar *dbus_sender, const gchar *action)
{
	PolKitResult pk_result;
	PolKitAction *pk_action;
	PolKitCaller *pk_caller;
	DBusError dbus_error;

	/* set action */
	pk_action = polkit_action_new ();
	polkit_action_set_action_id (pk_action, action);

	/* set caller */
	pk_debug ("using caller %s", dbus_sender);
	dbus_error_init (&dbus_error);
	pk_caller = polkit_caller_new_from_dbus_name (security->priv->connection, dbus_sender, &dbus_error);
	if (pk_caller == NULL) {
		if (dbus_error_is_set (&dbus_error)) {
			pk_error ("error: polkit_caller_new_from_dbus_name(): %s: %s\n",
				  dbus_error.name, dbus_error.message);
		}
	}

	pk_result = polkit_context_can_caller_do_action (security->priv->pk_context, pk_action, pk_caller);
	pk_debug ("PolicyKit result = '%s'", polkit_result_to_string_representation (pk_result));

	polkit_action_unref (pk_action);
	polkit_caller_unref (pk_caller);

	return pk_result;
}

/**
 * pk_security_action_is_allowed:
 *
 * Only valid from an async caller, which is fine, as we won't prompt the user
 * when not async.
 **/
gboolean
pk_security_action_is_allowed (PkSecurity *security, const gchar *dbus_sender,
			       PkRoleEnum role, gchar **error_detail)
{
	PolKitResult pk_result;
	const gchar *policy = NULL;

	g_return_val_if_fail (security != NULL, FALSE);
	g_return_val_if_fail (PK_IS_SECURITY (security), FALSE);

	/* map the roles to policykit rules */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGE ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		policy = "org.freedesktop.packagekit.update";
	} else if (role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		policy = "org.freedesktop.packagekit.remove";
	} else if (role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		policy = "org.freedesktop.packagekit.install";
	} else if (role == PK_ROLE_ENUM_INSTALL_FILE) {
		policy = "org.freedesktop.packagekit.localinstall";
	} else if (role == PK_ROLE_ENUM_ROLLBACK) {
		policy = "org.freedesktop.packagekit.rollback";
	} else if (role == PK_ROLE_ENUM_REPO_ENABLE ||
		   role == PK_ROLE_ENUM_REPO_SET_DATA) {
		policy = "org.freedesktop.packagekit.repo-change";
	} else if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
		policy = "org.freedesktop.packagekit.refresh-cache";
	} else {
		pk_error ("policykit type required for '%s'", pk_role_enum_to_text (role));
	}

	/* get the dbus sender */
	pk_result = pk_security_can_do_action (security, dbus_sender, policy);
	if (pk_result != POLKIT_RESULT_YES) {
		if (error_detail != NULL) {
			*error_detail = g_strdup_printf ("%s %s", policy, polkit_result_to_string_representation (pk_result));
		}
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_security_finalize:
 **/
static void
pk_security_finalize (GObject *object)
{
	PkSecurity *security;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_SECURITY (object));
	security = PK_SECURITY (object);

	/* unref PolicyKit */
	polkit_context_unref (security->priv->pk_context);

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
 * initialises the security class. NOTE: We expect security objects
 * to *NOT* be removed or added during the session.
 * We only control the first security object if there are more than one.
 **/
static void
pk_security_init (PkSecurity *security)
{
	PolKitError *pk_error;
	polkit_bool_t retval;
	DBusError dbus_error;

	security->priv = PK_SECURITY_GET_PRIVATE (security);

	pk_debug ("Using PolicyKit security framework");

	/* get a connection to the bus */
	dbus_error_init (&dbus_error);
	security->priv->connection = dbus_bus_get (DBUS_BUS_SYSTEM, &dbus_error);
	if (security->priv->connection == NULL) {
		pk_error ("failed to get system connection %s: %s\n", dbus_error.name, dbus_error.message);
	}

	/* get PolicyKit context */
	security->priv->pk_context = polkit_context_new ();
	pk_error = NULL;
	retval = polkit_context_init (security->priv->pk_context, &pk_error);
	if (retval == FALSE) {
		pk_error ("Could not init PolicyKit context: %s", polkit_error_get_error_message (pk_error));
		polkit_error_free (pk_error);
	}
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

