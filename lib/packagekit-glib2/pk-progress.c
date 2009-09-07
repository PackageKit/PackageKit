/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:pk-progress
 * @short_description: GObject class for PackageKit progress access
 *
 * A nice GObject to use for accessing PackageKit asynchronously
 */

#include "config.h"

#include <packagekit-glib2/pk-progress.h>
#include <packagekit-glib2/pk-enum.h>

#include "egg-debug.h"

static void     pk_progress_finalize	(GObject     *object);

#define PK_PROGRESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_PROGRESS, PkProgressPrivate))

/**
 * PkProgressPrivate:
 *
 * Private #PkProgress data
 **/
struct _PkProgressPrivate
{
	gchar				*package_id;
	gint				 percentage;
	gint				 subpercentage;
	gboolean			 allow_cancel;
	PkRoleEnum			 role;
	PkStatusEnum			 status;
	gboolean			 caller_active;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_PERCENTAGE,
	PROP_SUBPERCENTAGE,
	PROP_ALLOW_CANCEL,
	PROP_ROLE,
	PROP_STATUS,
	PROP_CALLER_ACTIVE,
	PROP_LAST
};

G_DEFINE_TYPE (PkProgress, pk_progress, G_TYPE_OBJECT)

/**
 * pk_progress_get_property:
 **/
static void
pk_progress_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkProgress *progress = PK_PROGRESS (object);
	PkProgressPrivate *priv = progress->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_PERCENTAGE:
		g_value_set_int (value, priv->percentage);
		break;
	case PROP_SUBPERCENTAGE:
		g_value_set_int (value, priv->subpercentage);
		break;
	case PROP_ALLOW_CANCEL:
		g_value_set_boolean (value, priv->allow_cancel);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	case PROP_ROLE:
		g_value_set_uint (value, priv->role);
		break;
	case PROP_CALLER_ACTIVE:
		g_value_set_boolean (value, priv->caller_active);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_progress_set_package_id:
 **/
static void
pk_progress_set_package_id (PkProgress *progress, const gchar *package_id)
{
	PkProgressPrivate *priv = progress->priv;
	if (g_strcmp0 (priv->package_id, package_id) == 0)
		return;
	g_free (priv->package_id);
	priv->package_id = g_strdup (package_id);
	egg_debug ("package_id now %s", package_id);
}

/**
 * pk_progress_set_percentage:
 **/
static void
pk_progress_set_percentage (PkProgress *progress, gint percentage)
{
	PkProgressPrivate *priv = progress->priv;
	if (priv->percentage == percentage)
		return;
	priv->percentage = percentage;
	egg_debug ("percentage now %i", percentage);
}

/**
 * pk_progress_set_subpercentage:
 **/
static void
pk_progress_set_subpercentage (PkProgress *progress, gint subpercentage)
{
	PkProgressPrivate *priv = progress->priv;
	if (priv->subpercentage == subpercentage)
		return;
	priv->subpercentage = subpercentage;
	egg_debug ("subpercentage now %i", subpercentage);
}

/**
 * pk_progress_set_status:
 **/
static void
pk_progress_set_status (PkProgress *progress, PkStatusEnum status)
{
	PkProgressPrivate *priv = progress->priv;
	if (priv->status == status)
		return;
	priv->status = status;
	egg_debug ("status now %s", pk_status_enum_to_text (status));
}

/**
 * pk_progress_set_role:
 **/
static void
pk_progress_set_role (PkProgress *progress, PkRoleEnum role)
{
	PkProgressPrivate *priv = progress->priv;
	if (priv->role == role)
		return;
	priv->role = role;
	egg_debug ("role now %s", pk_role_enum_to_text (role));
}

/**
 * pk_progress_set_allow_cancel:
 **/
static void
pk_progress_set_allow_cancel (PkProgress *progress, gboolean allow_cancel)
{
	PkProgressPrivate *priv = progress->priv;
	if (priv->allow_cancel == allow_cancel)
		return;
	priv->allow_cancel = allow_cancel;
	egg_debug ("allow-cancel now %i", allow_cancel);
}

/**
 * pk_progress_set_caller_active:
 **/
static void
pk_progress_set_caller_active (PkProgress *progress, gboolean caller_active)
{
	PkProgressPrivate *priv = progress->priv;
	if (priv->caller_active == caller_active)
		return;
	priv->caller_active = caller_active;
	egg_debug ("caller-active now %i", caller_active);
}

/**
 * pk_progress_set_property:
 **/
static void
pk_progress_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkProgress *progress = PK_PROGRESS (object);

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		pk_progress_set_package_id (progress, g_value_get_string (value));
		break;
	case PROP_PERCENTAGE:
		pk_progress_set_percentage (progress, g_value_get_int (value));
		break;
	case PROP_SUBPERCENTAGE:
		pk_progress_set_subpercentage (progress, g_value_get_int (value));
		break;
	case PROP_ALLOW_CANCEL:
		pk_progress_set_allow_cancel (progress, g_value_get_boolean (value));
		break;
	case PROP_STATUS:
		pk_progress_set_status (progress, g_value_get_uint (value));
		break;
	case PROP_ROLE:
		pk_progress_set_role (progress, g_value_get_uint (value));
		break;
	case PROP_CALLER_ACTIVE:
		pk_progress_set_caller_active (progress, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_progress_class_init:
 **/
static void
pk_progress_class_init (PkProgressClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_progress_get_property;
	object_class->set_property = pk_progress_set_property;
	object_class->finalize = pk_progress_finalize;

	/**
	 * PkPackage:package-id:
	 */
	pspec = g_param_spec_string ("package-id", NULL,
				     "The full package_id, e.g. 'gnome-power-manager;0.1.2;i386;fedora'",
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkProgress:percentage:
	 */
	pspec = g_param_spec_int ("percentage", NULL, NULL,
				  -1, G_MAXINT, -1,
				  G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);

	/**
	 * PkProgress:subpercentage:
	 */
	pspec = g_param_spec_int ("subpercentage", NULL, NULL,
				  -1, G_MAXINT, -1,
				  G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SUBPERCENTAGE, pspec);

	/**
	 * PkPackage:allow-cancel:
	 */
	pspec = g_param_spec_boolean ("allow-cancel", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ALLOW_CANCEL, pspec);

	/**
	 * PkProgress:status:
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, PK_STATUS_ENUM_UNKNOWN, PK_STATUS_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkProgress:role:
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, PK_ROLE_ENUM_UNKNOWN, PK_ROLE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkPackage:caller-active:
	 */
	pspec = g_param_spec_boolean ("caller-active", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CALLER_ACTIVE, pspec);

	g_type_class_add_private (klass, sizeof (PkProgressPrivate));
}

/**
 * pk_progress_init:
 **/
static void
pk_progress_init (PkProgress *progress)
{
	progress->priv = PK_PROGRESS_GET_PRIVATE (progress);
}

/**
 * pk_progress_finalize:
 **/
static void
pk_progress_finalize (GObject *object)
{
	PkProgress *progress = PK_PROGRESS (object);
	PkProgressPrivate *priv = progress->priv;

	g_free (priv->package_id);

	G_OBJECT_CLASS (pk_progress_parent_class)->finalize (object);
}

/**
 * pk_progress_new:
 *
 * PkProgress is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new %PkProgress instance
 **/
PkProgress *
pk_progress_new (void)
{
	PkProgress *progress;
	progress = g_object_new (PK_TYPE_PROGRESS, NULL);
	return PK_PROGRESS (progress);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_progress_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkProgress *progress;

	if (!egg_test_start (test, "PkProgress"))
		return;

	/************************************************************/
	egg_test_title (test, "get progress");
	progress = pk_progress_new ();
	egg_test_assert (test, progress != NULL);

	g_object_unref (progress);

	egg_test_end (test);
}
#endif

