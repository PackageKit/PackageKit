/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 David Zeuthen <davidz@redhat.com>
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <polkitbackend/polkitbackend.h>
#include <packagekit-glib/packagekit.h>
#include <glib/gi18n-lib.h>

#define PK_TYPE_ACTION_LOOKUP		(pk_action_lookup_get_type())
#define PK_ACTION_LOOKUP(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_ACTION_LOOKUP, PkActionLookup))
#define PK_ACTION_LOOKUP_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_ACTION_LOOKUP, PkActionLookupClass))
#define PK_ACTION_LOOKUP_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_ACTION_LOOKUP, PkActionLookupClass))
#define PK_IS_ACTION_LOOKUP(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_ACTION_LOOKUP))
#define PK_IS_ACTION_LOOKUP_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_ACTION_LOOKUP))

typedef struct _PkActionLookup PkActionLookup;
typedef struct _PkActionLookupClass PkActionLookupClass;

struct _PkActionLookup
{
	GObject parent;
};

struct _PkActionLookupClass
{
	GObjectClass parent_class;
};

GType	pk_action_lookup_get_type (void) G_GNUC_CONST;

static void polkit_backend_action_lookup_iface_init (PolkitBackendActionLookupIface *iface);

#define _G_IMPLEMENT_INTERFACE_DYNAMIC(TYPE_IFACE, iface_init)							\
{														\
	const GInterfaceInfo g_implement_interface_info = {							\
		(GInterfaceInitFunc) iface_init, NULL, NULL							\
	};													\
	g_type_module_add_interface (type_module, g_define_type_id, TYPE_IFACE, &g_implement_interface_info);	\
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (PkActionLookup,
				pk_action_lookup,
				G_TYPE_OBJECT,
				0,
				_G_IMPLEMENT_INTERFACE_DYNAMIC (POLKIT_BACKEND_TYPE_ACTION_LOOKUP,
								polkit_backend_action_lookup_iface_init))

/**
 * pk_action_lookup_init:
 **/
static void
pk_action_lookup_init (PkActionLookup *lookup)
{
}

/**
 * pk_action_lookup_class_finalize:
 **/
static void
pk_action_lookup_class_finalize (PkActionLookupClass *klass)
{
}

/**
 * pk_action_lookup_class_init:
 **/
static void
pk_action_lookup_class_init (PkActionLookupClass *klass)
{
}

/**
 * pk_action_lookup_get_message:
 **/
static gchar *
pk_action_lookup_get_message (PolkitBackendActionLookup *lookup, const gchar *action_id,
			      PolkitDetails *details, PolkitActionDescription *action_description)
{
	PkRoleEnum role = PK_ROLE_ENUM_UNKNOWN;
	gboolean only_trusted = TRUE;
	const gchar *role_text;
	const gchar *only_trusted_text;
	gchar *message = NULL;
	GString *string;

	if (!g_str_has_prefix (action_id, "org.freedesktop.packagekit."))
		goto out;

	/* get role */
	role_text = polkit_details_lookup (details, "role");
	if (role_text != NULL)
		role = pk_role_enum_from_text (role_text);

	/* get only-trusted */
	only_trusted_text = polkit_details_lookup (details, "only-trusted");
	if (only_trusted_text != NULL)
		only_trusted = g_str_equal (only_trusted_text, "true");

	/* use the message shipped in the policy file */
	if (only_trusted)
		goto out;

	/* UpdatePackages */
	if (role == PK_ROLE_ENUM_UPDATE_PACKAGES) {
		string = g_string_new ("");

		/* TRANSLATORS: is not GPG signed */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("The software is not from a trusted source.")));
		g_string_append (string, "\n");

		/* TRANSLATORS: user has to trust provider -- I know, this sucks */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("Do not update this package unless you are sure it is safe to do so.")));
		g_string_append (string, "\n\n");

		/* TRANSLATORS: warn the user that all bets are off */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("Malicious software can damage your computer or cause other harm.")));

		message = g_string_free (string, FALSE);
		goto out;
	}

	/* InstallPackages */
	if (role == PK_ROLE_ENUM_INSTALL_PACKAGES) {
		string = g_string_new ("");

		/* TRANSLATORS: is not GPG signed */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("The software is not from a trusted source.")));
		g_string_append (string, "\n");

		/* TRANSLATORS: user has to trust provider -- I know, this sucks */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("Do not install this package unless you are sure it is safe to do so.")));
		g_string_append (string, "\n\n");

		/* TRANSLATORS: warn the user that all bets are off */
		g_string_append (string, g_dgettext (GETTEXT_PACKAGE, N_("Malicious software can damage your computer or cause other harm.")));

		message = g_string_free (string, FALSE);
		goto out;
	}
out:
	return message;
}

/**
 * pk_action_lookup_get_icon_name:
 **/
static gchar *
pk_action_lookup_get_icon_name (PolkitBackendActionLookup *lookup, const gchar *action_id,
				PolkitDetails *details, PolkitActionDescription *action_description)
{
	PkRoleEnum role = PK_ROLE_ENUM_UNKNOWN;
	gboolean only_trusted = TRUE;
	const gchar *role_text;
	const gchar *only_trusted_text;
	gchar *value = NULL;

	if (!g_str_has_prefix (action_id, "org.freedesktop.packagekit."))
		goto out;

	/* get role */
	role_text = polkit_details_lookup (details, "role");
	if (role_text != NULL)
		role = pk_role_enum_from_text (role_text);

	/* get only-trusted */
	only_trusted_text = polkit_details_lookup (details, "only-trusted");
	if (only_trusted_text != NULL)
		only_trusted = g_str_equal (only_trusted_text, "true");

	/* set proxy */
	if (role == PK_ROLE_ENUM_UNKNOWN) {
		value = g_strdup ("preferences-system-network-proxy");
		goto out;
	}

	/* only-trusted */
	if (!only_trusted) {
		value = g_strdup ("emblem-important");
		goto out;
	}

	/* fallback */
	value = g_strdup ("package-x-generic");

out:
	return value;
}

/**
 * pk_action_lookup_get_details:
 **/
static PolkitDetails *
pk_action_lookup_get_details (PolkitBackendActionLookup *lookup, const gchar *action_id,
			      PolkitDetails *action_details, PolkitActionDescription *action_description)
{
	const gchar *str;
	PolkitDetails *details;

	if (!g_str_has_prefix (action_id, "org.freedesktop.packagekit."))
		return NULL;

	details = polkit_details_new ();

	/* role */
	str = polkit_details_lookup (action_details, "role");
	if (str != NULL) {
		/* TRANSLATORS: the trasaction role, e.g. update-system */
		polkit_details_insert (details, _("Role"), str);
	}

	/* only-trusted */
	str = polkit_details_lookup (action_details, "only-trusted");
	if (str != NULL) {
		/* TRANSLATORS: if the transaction is forced to install only trusted packages */
		polkit_details_insert (details, _("Only trusted"), str);
	}

	return details;
}

/**
 * polkit_backend_action_lookup_iface_init:
 **/
static void
polkit_backend_action_lookup_iface_init (PolkitBackendActionLookupIface *iface)
{
	iface->get_message = pk_action_lookup_get_message;
	iface->get_icon_name = pk_action_lookup_get_icon_name;
	iface->get_details = pk_action_lookup_get_details;
}

/**
 * g_io_module_load:
 **/
void
g_io_module_load (GIOModule *module)
{
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	pk_action_lookup_register_type (G_TYPE_MODULE (module));

	g_io_extension_point_implement (POLKIT_BACKEND_ACTION_LOOKUP_EXTENSION_POINT_NAME,
					PK_TYPE_ACTION_LOOKUP,
					"PackageKit action lookup extension " PACKAGE_VERSION,
					0);
}

/**
 * g_io_module_unload:
 **/
void
g_io_module_unload (GIOModule *module)
{
}

