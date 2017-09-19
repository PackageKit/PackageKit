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
 * SECTION:pk-repo-detail
 * @short_description: RepoDetail object
 *
 * This GObject represents a repo_detail from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-repo-detail.h>

static void     pk_repo_detail_finalize	(GObject     *object);

#define PK_REPO_DETAIL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_REPO_DETAIL, PkRepoDetailPrivate))

/**
 * PkRepoDetailPrivate:
 *
 * Private #PkRepoDetail data
 **/
struct _PkRepoDetailPrivate
{
	gchar				*repo_id;
	gchar				*description;
	gboolean			 enabled;
};

enum {
	PROP_0,
	PROP_REPO_ID,
	PROP_DESCRIPTION,
	PROP_ENABLED,
	PROP_LAST
};

G_DEFINE_TYPE (PkRepoDetail, pk_repo_detail, PK_TYPE_SOURCE)

/**
 * pk_repo_detail_get_id:
 * @repo_detail: a valid #PkRepoDetail instance
 *
 * Gets the repository ID.
 *
 * Return value: string ID, e.g. "fedora"
 *
 * Since: 0.9.1
 **/
const gchar *
pk_repo_detail_get_id (PkRepoDetail *repo_detail)
{
	g_return_val_if_fail (PK_IS_REPO_DETAIL (repo_detail), NULL);
	return repo_detail->priv->repo_id;
}

/**
 * pk_repo_detail_get_description:
 * @repo_detail: a valid #PkRepoDetail instance
 *
 * Gets the repository description.
 *
 * Return value: string ID, e.g. "Fedora 20 - i386"
 *
 * Since: 0.9.1
 **/
const gchar *
pk_repo_detail_get_description (PkRepoDetail *repo_detail)
{
	g_return_val_if_fail (PK_IS_REPO_DETAIL (repo_detail), NULL);
	return repo_detail->priv->description;
}

/**
 * pk_repo_detail_get_enabled:
 * @repo_detail: a valid #PkRepoDetail instance
 *
 * Gets the repository enabled status.
 *
 * Return value: %TRUE for enabled
 *
 * Since: 0.9.1
 **/
gboolean
pk_repo_detail_get_enabled (PkRepoDetail *repo_detail)
{
	g_return_val_if_fail (PK_IS_REPO_DETAIL (repo_detail), FALSE);
	return repo_detail->priv->enabled;
}

/*
 * pk_repo_detail_get_property:
 **/
static void
pk_repo_detail_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkRepoDetail *repo_detail = PK_REPO_DETAIL (object);
	PkRepoDetailPrivate *priv = repo_detail->priv;

	switch (prop_id) {
	case PROP_REPO_ID:
		g_value_set_string (value, priv->repo_id);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_repo_detail_set_property:
 **/
static void
pk_repo_detail_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkRepoDetail *repo_detail = PK_REPO_DETAIL (object);
	PkRepoDetailPrivate *priv = repo_detail->priv;

	switch (prop_id) {
	case PROP_REPO_ID:
		g_free (priv->repo_id);
		priv->repo_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_DESCRIPTION:
		g_free (priv->description);
		priv->description = g_strdup (g_value_get_string (value));
		break;
	case PROP_ENABLED:
		priv->enabled = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_repo_detail_class_init:
 **/
static void
pk_repo_detail_class_init (PkRepoDetailClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_repo_detail_finalize;
	object_class->get_property = pk_repo_detail_get_property;
	object_class->set_property = pk_repo_detail_set_property;

	/**
	 * PkRepoDetail:repo-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("repo-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REPO_ID, pspec);

	/**
	 * PkRepoDetail:description:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * PkRepoDetail:enabled:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_boolean ("enabled", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ENABLED, pspec);

	g_type_class_add_private (klass, sizeof (PkRepoDetailPrivate));
}

/*
 * pk_repo_detail_init:
 **/
static void
pk_repo_detail_init (PkRepoDetail *repo_detail)
{
	repo_detail->priv = PK_REPO_DETAIL_GET_PRIVATE (repo_detail);
}

/*
 * pk_repo_detail_finalize:
 **/
static void
pk_repo_detail_finalize (GObject *object)
{
	PkRepoDetail *repo_detail = PK_REPO_DETAIL (object);
	PkRepoDetailPrivate *priv = repo_detail->priv;

	g_free (priv->repo_id);
	g_free (priv->description);

	G_OBJECT_CLASS (pk_repo_detail_parent_class)->finalize (object);
}

/**
 * pk_repo_detail_new:
 *
 * Return value: a new #PkRepoDetail object.
 *
 * Since: 0.5.4
 **/
PkRepoDetail *
pk_repo_detail_new (void)
{
	PkRepoDetail *repo_detail;
	repo_detail = g_object_new (PK_TYPE_REPO_DETAIL, NULL);
	return PK_REPO_DETAIL (repo_detail);
}

