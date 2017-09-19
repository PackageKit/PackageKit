/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-update-detail
 * @short_description: UpdateDetail object
 *
 * This GObject represents a update_detail from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-update-detail.h>
#include <packagekit-glib2/pk-enum-types.h>

static void     pk_update_detail_finalize	(GObject     *object);

#define PK_UPDATE_DETAIL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_UPDATE_DETAIL, PkUpdateDetailPrivate))

/**
 * PkUpdateDetailPrivate:
 *
 * Private #PkUpdateDetail data
 **/
struct _PkUpdateDetailPrivate
{
	gchar				*package_id;
	gchar				**updates;
	gchar				**obsoletes;
	gchar				**vendor_urls;
	gchar				**bugzilla_urls;
	gchar				**cve_urls;
	PkRestartEnum			 restart;
	gchar				*update_text;
	gchar				*changelog;
	PkUpdateStateEnum		 state;
	gchar				*issued;
	gchar				*updated;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_UPDATES,
	PROP_OBSOLETES,
	PROP_VENDOR_URLS,
	PROP_BUGZILLA_URLS,
	PROP_CVE_URLS,
	PROP_RESTART,
	PROP_UPDATE_TEXT,
	PROP_CHANGELOG,
	PROP_STATE,
	PROP_ISSUED,
	PROP_UPDATED,
	PROP_LAST
};

G_DEFINE_TYPE (PkUpdateDetail, pk_update_detail, PK_TYPE_SOURCE)
/**
 * pk_update_detail_get_package_id:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail package ID.
 *
 * Return value: string values
 *
 * Since:0.8.8
 **/
const gchar *
pk_update_detail_get_package_id (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->package_id;
}

/**
 * pk_update_detail_get_updates:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail updates IDs.
 *
 * Return value: (transfer none): string values
 *
 * Since:0.8.8
 **/
gchar **
pk_update_detail_get_updates (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->updates;
}

/**
 * pk_update_detail_get_obsoletes:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail obsoletes IDs.
 *
 * Return value: (transfer none): string values
 *
 * Since:0.8.8
 **/
gchar **
pk_update_detail_get_obsoletes (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->obsoletes;
}

/**
 * pk_update_detail_get_vendor_urls:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail vendor URLs.
 *
 * Return value: (transfer none): string values
 *
 * Since:0.8.8
 **/
gchar **
pk_update_detail_get_vendor_urls (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->vendor_urls;
}

/**
 * pk_update_detail_get_bugzilla_urls:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail bugzilla URLs.
 *
 * Return value: (transfer none): string values
 *
 * Since:0.8.8
 **/
gchar **
pk_update_detail_get_bugzilla_urls (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->bugzilla_urls;
}

/**
 * pk_update_detail_get_cve_urls:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail CVE URLs.
 *
 * Return value: (transfer none): string values
 *
 * Since:0.8.8
 **/
gchar **
pk_update_detail_get_cve_urls (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->cve_urls;
}

/**
 * pk_update_detail_get_restart:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail restart kind.
 *
 * Return value: enum values
 *
 * Since:0.8.8
 **/
PkRestartEnum
pk_update_detail_get_restart (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, PK_RESTART_ENUM_UNKNOWN);
	return update_detail->priv->restart;
}

/**
 * pk_update_detail_get_update_text:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail update text.
 *
 * Return value: string values
 *
 * Since:0.8.8
 **/
const gchar *
pk_update_detail_get_update_text (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->update_text;
}

/**
 * pk_update_detail_get_changelog:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail changelog.
 *
 * Return value: string values
 *
 * Since:0.8.8
 **/
const gchar *
pk_update_detail_get_changelog (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->changelog;
}

/**
 * pk_update_detail_get_state:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail state.
 *
 * Return value: enum value
 *
 * Since:0.8.8
 **/
PkUpdateStateEnum
pk_update_detail_get_state (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, PK_UPDATE_STATE_ENUM_UNKNOWN);
	return update_detail->priv->state;
}

/**
 * pk_update_detail_get_issued:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail issued string.
 *
 * Return value: string values
 *
 * Since:0.8.8
 **/
const gchar *
pk_update_detail_get_issued (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->issued;
}

/**
 * pk_update_detail_get_updated:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the update detail updated string.
 *
 * Return value: string values
 *
 * Since:0.8.8
 **/
const gchar *
pk_update_detail_get_updated (PkUpdateDetail *update_detail)
{
	g_return_val_if_fail (update_detail != NULL, NULL);
	return update_detail->priv->updated;
}

/*
 * pk_update_detail_get_property:
 **/
static void
pk_update_detail_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkUpdateDetail *update_detail = PK_UPDATE_DETAIL (object);
	PkUpdateDetailPrivate *priv = update_detail->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_UPDATES:
		g_value_set_boxed (value, priv->updates);
		break;
	case PROP_OBSOLETES:
		g_value_set_boxed (value, priv->obsoletes);
		break;
	case PROP_VENDOR_URLS:
		g_value_set_boxed (value, priv->vendor_urls);
		break;
	case PROP_BUGZILLA_URLS:
		g_value_set_boxed (value, priv->bugzilla_urls);
		break;
	case PROP_CVE_URLS:
		g_value_set_boxed (value, priv->cve_urls);
		break;
	case PROP_RESTART:
		g_value_set_enum (value, priv->restart);
		break;
	case PROP_UPDATE_TEXT:
		g_value_set_string (value, priv->update_text);
		break;
	case PROP_CHANGELOG:
		g_value_set_string (value, priv->changelog);
		break;
	case PROP_STATE:
		g_value_set_enum (value, priv->state);
		break;
	case PROP_ISSUED:
		g_value_set_string (value, priv->issued);
		break;
	case PROP_UPDATED:
		g_value_set_string (value, priv->updated);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_update_detail_set_property:
 **/
static void
pk_update_detail_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkUpdateDetail *update_detail = PK_UPDATE_DETAIL (object);
	PkUpdateDetailPrivate *priv = update_detail->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATES:
		g_strfreev (priv->updates);
		priv->updates = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_OBSOLETES:
		g_strfreev (priv->obsoletes);
		priv->obsoletes = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_VENDOR_URLS:
		g_strfreev (priv->vendor_urls);
		priv->vendor_urls = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_BUGZILLA_URLS:
		g_strfreev (priv->bugzilla_urls);
		priv->bugzilla_urls = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_CVE_URLS:
		g_strfreev (priv->cve_urls);
		priv->cve_urls = g_strdupv (g_value_get_boxed (value));
		break;
	case PROP_RESTART:
		priv->restart = g_value_get_enum (value);
		break;
	case PROP_UPDATE_TEXT:
		g_free (priv->update_text);
		priv->update_text = g_strdup (g_value_get_string (value));
		break;
	case PROP_CHANGELOG:
		g_free (priv->changelog);
		priv->changelog = g_strdup (g_value_get_string (value));
		break;
	case PROP_STATE:
		priv->state = g_value_get_enum (value);
		break;
	case PROP_ISSUED:
		g_free (priv->issued);
		priv->issued = g_strdup (g_value_get_string (value));
		break;
	case PROP_UPDATED:
		g_free (priv->updated);
		priv->updated = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_update_detail_class_init:
 **/
static void
pk_update_detail_class_init (PkUpdateDetailClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_update_detail_finalize;
	object_class->get_property = pk_update_detail_get_property;
	object_class->set_property = pk_update_detail_set_property;

	/**
	 * PkUpdateDetail:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkUpdateDetail:updates:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("updates", NULL, NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATES, pspec);

	/**
	 * PkUpdateDetail:obsoletes:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("obsoletes", NULL, NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_OBSOLETES, pspec);

	/**
	 * PkUpdateDetail:vendor-urls:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("vendor-urls", NULL, NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_VENDOR_URLS, pspec);

	/**
	 * PkUpdateDetail:bugzilla-urls:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("bugzilla-urls", NULL, NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_BUGZILLA_URLS, pspec);

	/**
	 * PkUpdateDetail:cve-urls:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("cve-urls", NULL, NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CVE_URLS, pspec);

	/**
	 * PkUpdateDetail:restart:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("restart", NULL, NULL,
				   PK_TYPE_RESTART_ENUM, PK_RESTART_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_RESTART, pspec);

	/**
	 * PkUpdateDetail:update-text:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-text", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATE_TEXT, pspec);

	/**
	 * PkUpdateDetail:changelog:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("changelog", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CHANGELOG, pspec);

	/**
	 * PkUpdateDetail:state:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("state", NULL, NULL,
				   PK_TYPE_UPDATE_STATE_ENUM, PK_UPDATE_STATE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * PkUpdateDetail:issued:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("issued", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ISSUED, pspec);

	/**
	 * PkUpdateDetail:updated:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("updated", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_UPDATED, pspec);

	g_type_class_add_private (klass, sizeof (PkUpdateDetailPrivate));
}

/*
 * pk_update_detail_init:
 **/
static void
pk_update_detail_init (PkUpdateDetail *update_detail)
{
	update_detail->priv = PK_UPDATE_DETAIL_GET_PRIVATE (update_detail);
}

/*
 * pk_update_detail_finalize:
 **/
static void
pk_update_detail_finalize (GObject *object)
{
	PkUpdateDetail *update_detail = PK_UPDATE_DETAIL (object);
	PkUpdateDetailPrivate *priv = update_detail->priv;

	g_free (priv->package_id);
	g_strfreev (priv->updates);
	g_strfreev (priv->obsoletes);
	g_strfreev (priv->vendor_urls);
	g_strfreev (priv->bugzilla_urls);
	g_free (priv->cve_urls);
	g_free (priv->update_text);
	g_free (priv->changelog);
	g_free (priv->issued);
	g_free (priv->updated);

	G_OBJECT_CLASS (pk_update_detail_parent_class)->finalize (object);
}

/**
 * pk_update_detail_new:
 *
 * Return value: a new #PkUpdateDetail object.
 *
 * Since: 0.5.4
 **/
PkUpdateDetail *
pk_update_detail_new (void)
{
	PkUpdateDetail *update_detail;
	update_detail = g_object_new (PK_TYPE_UPDATE_DETAIL, NULL);
	return PK_UPDATE_DETAIL (update_detail);
}

