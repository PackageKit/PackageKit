/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * PkUpdateDetail:
 *
 * UpdateDetail object
 *
 * This GObject represents a update_detail from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include "pk-update-detail.h"
#include "pk-issue.h"
#include "pk-enum-types.h"

static void pk_update_detail_finalize (GObject *object);

/**
 * PkUpdateDetailPrivate:
 *
 * Private #PkUpdateDetail data
 **/
struct _PkUpdateDetailPrivate
{
	gchar *package_id;
	gchar **updates;
	gchar **obsoletes;
	GPtrArray *issues; /* (element-type PkIssue) */
	PkRestartEnum restart;
	gchar *update_text;
	gchar *changelog;
	PkUpdateStateEnum state;
	gchar *time_issued;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_UPDATES,
	PROP_OBSOLETES,
	PROP_ISSUES,
	PROP_RESTART,
	PROP_UPDATE_TEXT,
	PROP_CHANGELOG,
	PROP_STATE,
	PROP_TIME_ISSUED,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (PkUpdateDetail, pk_update_detail, PK_TYPE_SOURCE)
#define GET_PRIVATE(o) (pk_update_detail_get_instance_private (o))

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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->package_id;
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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->updates;
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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->obsoletes;
}

/**
 * pk_update_detail_get_issues:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the issue references of this update: tracker entries, CVE records
 * and advisory pages.
 *
 * Return value: (transfer none) (element-type PkIssue) (nullable): the issues
 *
 * Since: 2.0.0
 **/
GPtrArray *
pk_update_detail_get_issues (PkUpdateDetail *update_detail)
{
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->issues;
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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, PK_RESTART_ENUM_UNKNOWN);

	return priv->restart;
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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->update_text;
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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->changelog;
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
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, PK_UPDATE_STATE_ENUM_UNKNOWN);

	return priv->state;
}

/**
 * pk_update_detail_get_time_issued:
 * @update_detail: a #PkUpdateDetail instance
 *
 * Gets the ISO 8601 time the update was issued.
 *
 * Return value: (nullable): the time, or %NULL if unknown
 *
 * Since: 2.0.0
 **/
const gchar *
pk_update_detail_get_time_issued (PkUpdateDetail *update_detail)
{
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_return_val_if_fail (update_detail != NULL, NULL);

	return priv->time_issued;
}

/*
 * pk_update_detail_get_property:
 **/
static void
pk_update_detail_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkUpdateDetail *update_detail = PK_UPDATE_DETAIL (object);
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

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
	case PROP_ISSUES:
		g_value_set_boxed (value, priv->issues);
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
	case PROP_TIME_ISSUED:
		g_value_set_string (value, priv->time_issued);
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
pk_update_detail_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	PkUpdateDetail *update_detail = PK_UPDATE_DETAIL (object);
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_value_dup_string (value);
		break;
	case PROP_UPDATES:
		g_strfreev (priv->updates);
		priv->updates = g_value_dup_boxed (value);
		break;
	case PROP_OBSOLETES:
		g_strfreev (priv->obsoletes);
		priv->obsoletes = g_value_dup_boxed (value);
		break;
	case PROP_ISSUES:
		g_clear_pointer (&priv->issues, g_ptr_array_unref);
		priv->issues = g_value_dup_boxed (value);
		break;
	case PROP_RESTART:
		priv->restart = g_value_get_enum (value);
		break;
	case PROP_UPDATE_TEXT:
		g_free (priv->update_text);
		priv->update_text = g_value_dup_string (value);
		break;
	case PROP_CHANGELOG:
		g_free (priv->changelog);
		priv->changelog = g_value_dup_string (value);
		break;
	case PROP_STATE:
		priv->state = g_value_get_enum (value);
		break;
	case PROP_TIME_ISSUED:
		g_free (priv->time_issued);
		priv->time_issued = g_value_dup_string (value);
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
	pspec = g_param_spec_string ("package-id",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkUpdateDetail:updates:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("updates",
				    NULL,
				    NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_UPDATES, pspec);

	/**
	 * PkUpdateDetail:obsoletes:
	 *
	 * Since: 0.8.1
	 */
	pspec = g_param_spec_boxed ("obsoletes",
				    NULL,
				    NULL,
				    G_TYPE_STRV,
				    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_OBSOLETES, pspec);

	/**
	 * PkUpdateDetail:issues: (element-type PkIssue)
	 *
	 * The issue references of the update. The array is shared, not
	 * copied, so it must own its elements via pk_issue_free().
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_boxed ("issues",
				    NULL,
				    NULL,
				    G_TYPE_PTR_ARRAY,
				    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_ISSUES, pspec);

	/**
	 * PkUpdateDetail:restart:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("restart",
				   NULL,
				   NULL,
				   PK_TYPE_RESTART_ENUM,
				   PK_RESTART_ENUM_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_RESTART, pspec);

	/**
	 * PkUpdateDetail:update-text:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("update-text",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_UPDATE_TEXT, pspec);

	/**
	 * PkUpdateDetail:changelog:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("changelog",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_CHANGELOG, pspec);

	/**
	 * PkUpdateDetail:state:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("state",
				   NULL,
				   NULL,
				   PK_TYPE_UPDATE_STATE_ENUM,
				   PK_UPDATE_STATE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * PkUpdateDetail:time-issued:
	 *
	 * The ISO 8601 time the update was issued.
	 *
	 * Since: 2.0.0
	 */
	pspec = g_param_spec_string ("time-issued",
				     NULL,
				     NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
	g_object_class_install_property (object_class, PROP_TIME_ISSUED, pspec);
}

/*
 * pk_update_detail_init:
 **/
static void
pk_update_detail_init (PkUpdateDetail *update_detail)
{
	update_detail->priv = GET_PRIVATE(update_detail);
}

/*
 * pk_update_detail_finalize:
 **/
static void
pk_update_detail_finalize (GObject *object)
{
	PkUpdateDetail *update_detail = PK_UPDATE_DETAIL (object);
	PkUpdateDetailPrivate *priv = GET_PRIVATE(update_detail);

	g_clear_pointer (&priv->package_id, g_free);
	g_clear_pointer (&priv->updates, g_strfreev);
	g_clear_pointer (&priv->obsoletes, g_strfreev);
	g_clear_pointer (&priv->issues, g_ptr_array_unref);
	g_clear_pointer (&priv->update_text, g_free);
	g_clear_pointer (&priv->changelog, g_free);
	g_clear_pointer (&priv->time_issued, g_free);

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

/**
 * pk_update_detail_new_full:
 * @package_id: the package-id of the update
 * @updates: (nullable) (array zero-terminated=1): the package-ids that are updated
 * @obsoletes: (nullable) (array zero-terminated=1): the package-ids that are obsoleted
 * @issues: (nullable) (element-type PkIssue): the issue references of this update
 * @restart: the #PkRestartEnum required by this update
 * @update_text: (nullable): a description of the update
 * @changelog: (nullable): the changelog of the update
 * @state: the #PkUpdateStateEnum of the update
 * @time_issued: (nullable): the ISO 8601 time the update was issued
 *
 * Creates a new #PkUpdateDetail with all its properties set.
 * Empty and %NULL values are treated the same.
 *
 * Returns: (transfer full): a new #PkUpdateDetail object.
 *
 * Since: 2.0.0
 **/
PkUpdateDetail *
pk_update_detail_new_full (const gchar *package_id,
			   gchar **updates,
			   gchar **obsoletes,
			   GPtrArray *issues,
			   PkRestartEnum restart,
			   const gchar *update_text,
			   const gchar *changelog,
			   PkUpdateStateEnum state,
			   const gchar *time_issued)
{
	g_return_val_if_fail (package_id != NULL, NULL);

	return g_object_new (PK_TYPE_UPDATE_DETAIL,
			     "package-id",
			     package_id,
			     "updates",
			     updates,
			     "obsoletes",
			     obsoletes,
			     "issues",
			     issues,
			     "restart",
			     restart,
			     "update-text",
			     update_text,
			     "changelog",
			     changelog,
			     "state",
			     state,
			     "time-issued",
			     time_issued,
			     NULL);
}
