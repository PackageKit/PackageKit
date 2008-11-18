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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

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
#include <packagekit-glib/packagekit.h>

#include "egg-debug.h"
#include "egg-string.h"

#include "pk-security.h"

#define PK_SECURITY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SECURITY, PkSecurityPrivate))

struct PkSecurityPrivate
{
	PolKitContext		*pk_context;
	DBusConnection		*connection;
};

typedef PolKitCaller PkSecurityCaller_;

G_DEFINE_TYPE (PkSecurity, pk_security, G_TYPE_OBJECT)
static gpointer pk_security_object = NULL;

/**
 * pk_security_caller_new_from_sender:
 **/
PkSecurityCaller *
pk_security_caller_new_from_sender (PkSecurity *security, const gchar *sender)
{
	PolKitCaller *caller;
	DBusError dbus_error;

	g_return_val_if_fail (PK_IS_SECURITY (security), FALSE);

	/* get the PolKitCaller information */
	dbus_error_init (&dbus_error);
	caller = polkit_caller_new_from_dbus_name (security->priv->connection, sender, &dbus_error);
	if (dbus_error_is_set (&dbus_error)) {
		egg_warning ("failed to get caller %s: %s\n", dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
	}

	return (PkSecurityCaller*) caller;
}

/**
 * pk_security_caller_unref:
 **/
void
pk_security_caller_unref (PkSecurityCaller *caller)
{
	if (caller != NULL)
		polkit_caller_unref ((PolKitCaller *) caller);
}

/**
 * pk_security_get_uid:
 **/
guint
pk_security_get_uid (PkSecurity *security, PkSecurityCaller *caller)
{
	polkit_bool_t retval;
	guint uid;

	g_return_val_if_fail (PK_IS_SECURITY (security), -1);

	/* get uid */
	retval = polkit_caller_get_uid ((PolKitCaller *) caller, &uid);
	if (!retval) {
		egg_warning ("failed to get UID");
		uid = -1;
	}

	return uid;
}

/**
 * pk_security_get_cmdline:
 **/
gchar *
pk_security_get_cmdline (PkSecurity *security, PkSecurityCaller *caller)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *cmdline = NULL;
	GError *error = NULL;
	polkit_bool_t retval;
	pid_t pid;

	g_return_val_if_fail (PK_IS_SECURITY (security), NULL);

	/* get pid */
	retval = polkit_caller_get_pid ((PolKitCaller *) caller, &pid);
	if (!retval) {
		egg_warning ("failed to get PID");
		goto out;
	}

	/* get command line from proc */
	filename = g_strdup_printf ("/proc/%i/cmdline", pid);
	ret = g_file_get_contents (filename, &cmdline, NULL, &error);
	if (!ret) {
		egg_warning ("failed to get cmdline: %s", error->message);
		g_error_free (error);
	}
out:
	g_free (filename);
	return cmdline;
}

/**
 * pk_security_role_to_action:
 **/
static const gchar *
pk_security_role_to_action (PkSecurity *security, gboolean trusted, PkRoleEnum role)
{
	const gchar *policy = NULL;

	g_return_val_if_fail (PK_IS_SECURITY (security), NULL);

	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES ||
	    role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		policy = "org.freedesktop.packagekit.system-update";
	} else if (role == PK_ROLE_ENUM_INSTALL_SIGNATURE) {
		policy = "org.freedesktop.packagekit.system-trust-signing-key";
	} else if (role == PK_ROLE_ENUM_ROLLBACK) {
		policy = "org.freedesktop.packagekit.system-rollback";
	} else if (role == PK_ROLE_ENUM_REPO_ENABLE ||
		   role == PK_ROLE_ENUM_REPO_SET_DATA) {
		policy = "org.freedesktop.packagekit.system-sources-configure";
	} else if (role == PK_ROLE_ENUM_REFRESH_CACHE) {
		policy = "org.freedesktop.packagekit.system-sources-refresh";
	} else if (role == PK_ROLE_ENUM_SET_PROXY_PRIVATE) {
		policy = "org.freedesktop.packagekit.system-network-proxy-configure";
	} else if (role == PK_ROLE_ENUM_REMOVE_PACKAGES) {
		policy = "org.freedesktop.packagekit.package-remove";
	} else if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		policy = "org.freedesktop.packagekit.package-install";
	} else if (role == PK_ROLE_ENUM_INSTALL_FILES && trusted) {
		policy = "org.freedesktop.packagekit.package-install";
	} else if (role == PK_ROLE_ENUM_INSTALL_FILES && !trusted) {
		policy = "org.freedesktop.packagekit.package-install-untrusted";
	} else if (role == PK_ROLE_ENUM_ACCEPT_EULA) {
		policy = "org.freedesktop.packagekit.package-eula-accept";
	}
	return policy;
}

/**
 * pk_security_action_is_allowed:
 **/
gboolean
pk_security_action_is_allowed (PkSecurity *security, PkSecurityCaller *caller, gboolean trusted, PkRoleEnum role, gchar **error_detail)
{
	gboolean ret = FALSE;
	PolKitResult result;
	const gchar *policy;
	PolKitAction *action;

	g_return_val_if_fail (PK_IS_SECURITY (security), FALSE);
	g_return_val_if_fail (caller != NULL, FALSE);

	/* map the roles to policykit rules */
	policy = pk_security_role_to_action (security, trusted, role);
	if (policy == NULL) {
		egg_warning ("policykit type required for '%s'", pk_role_enum_to_text (role));
		return FALSE;
	}

	/* set action */
	action = polkit_action_new ();
	if (action == NULL) {
		egg_warning ("error: polkit_action_new failed");
		goto out;
	}
	polkit_action_set_action_id (action, policy);

	/* set caller */
	result = polkit_context_is_caller_authorized (security->priv->pk_context, action, (PolKitCaller *) caller, TRUE, NULL);
	egg_debug ("PolicyKit result = '%s'", polkit_result_to_string_representation (result));
	if (result != POLKIT_RESULT_YES) {
		if (error_detail != NULL)
			*error_detail = g_strdup_printf ("%s %s", policy, polkit_result_to_string_representation (result));
		goto out;
	}

	/* yippee */
	ret = TRUE;

out:
	if (action != NULL)
		polkit_action_unref (action);
	return ret;
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
 * pk_security_io_watch_have_data:
 **/
static gboolean
pk_security_io_watch_have_data (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
	int fd;
	PolKitContext *pk_context = user_data;
	fd = g_io_channel_unix_get_fd (channel);
	polkit_context_io_func (pk_context, fd);
	return TRUE;
}

/**
 * pk_security_io_add_watch:
 **/
static int
pk_security_io_add_watch (PolKitContext *pk_context, int fd)
{
	guint id = 0;
	GIOChannel *channel;
	channel = g_io_channel_unix_new (fd);
	if (channel == NULL)
		return id;
	id = g_io_add_watch (channel, G_IO_IN, pk_security_io_watch_have_data, pk_context);
	if (id == 0) {
		g_io_channel_unref (channel);
		return id;
	}
	g_io_channel_unref (channel);
	return id;
}

/**
 * pk_security_io_remove_watch:
 **/
static void
pk_security_io_remove_watch (PolKitContext *pk_context, int watch_id)
{
	g_source_remove (watch_id);
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
	PolKitError *pk_error;
	polkit_bool_t retval;
	DBusError dbus_error;

	security->priv = PK_SECURITY_GET_PRIVATE (security);

	egg_debug ("Using PolicyKit security framework");

	/* get a connection to the bus */
	dbus_error_init (&dbus_error);
	security->priv->connection = dbus_bus_get (DBUS_BUS_SYSTEM, &dbus_error);
	if (security->priv->connection == NULL) {
		if (dbus_error_is_set (&dbus_error)) {
			egg_warning ("failed to get system connection %s: %s\n", dbus_error.name, dbus_error.message);
			dbus_error_free (&dbus_error);
		}
	}

	/* get PolicyKit context */
	security->priv->pk_context = polkit_context_new ();

	/* watch for changes */
	polkit_context_set_io_watch_functions (security->priv->pk_context,
					       pk_security_io_add_watch,
					       pk_security_io_remove_watch);

	pk_error = NULL;
	retval = polkit_context_init (security->priv->pk_context, &pk_error);
	if (retval == FALSE) {
		egg_warning ("Could not init PolicyKit context: %s", polkit_error_get_error_message (pk_error));
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
	if (pk_security_object != NULL) {
		g_object_ref (pk_security_object);
	} else {
		pk_security_object = g_object_new (PK_TYPE_SECURITY, NULL);
		g_object_add_weak_pointer (pk_security_object, &pk_security_object);
	}
	return PK_SECURITY (pk_security_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_security_test (EggTest *test)
{
	PkSecurity *security;
	const gchar *action;
	gboolean ret;
	gchar *error;

	if (!egg_test_start (test, "PkSecurity"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	security = pk_security_new ();
	egg_test_assert (test, security != NULL);
	egg_test_title_assert (test, "check connection", security->priv->connection != NULL);
	egg_test_title_assert (test, "check PolKit context", security->priv->pk_context != NULL);

	/************************************************************/
	egg_test_title (test, "map valid role to action");
	action = pk_security_role_to_action (security, FALSE, PK_ROLE_ENUM_UPDATE_PACKAGES);
	if (egg_strequal (action, "org.freedesktop.packagekit.system-update"))
		egg_test_success (test, NULL, error);
	else
		egg_test_failed (test, "did not get correct action '%s'", action);

	/************************************************************/
	egg_test_title (test, "map invalid role to action");
	action = pk_security_role_to_action (security, FALSE, PK_ROLE_ENUM_SEARCH_NAME);
	if (action == NULL)
		egg_test_success (test, NULL, error);
	else
		egg_test_failed (test, "did not get correct action '%s'", action);

	/************************************************************/
	egg_test_title (test, "get the default backend");
	error = NULL;
	ret = pk_security_action_is_allowed (security, NULL, FALSE, PK_ROLE_ENUM_UPDATE_PACKAGES, &error);
	if (ret == FALSE)
		egg_test_success (test, "did not authenticate update-package, error '%s'", error);
	else
		egg_test_failed (test, "authenticated update-package!");
	g_free (error);

	g_object_unref (security);

	egg_test_end (test);
}
#endif

