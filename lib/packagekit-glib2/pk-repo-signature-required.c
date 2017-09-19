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
 * SECTION:pk-repo-signature-required
 * @short_description: RepoSignatureRequired object
 *
 * This GObject represents a repo_signature_required from a transaction.
 * These objects represent single items of data from the transaction, and are
 * often present in lists (#PkResults) or just refcounted in client programs.
 */

#include "config.h"

#include <glib-object.h>

#include <packagekit-glib2/pk-repo-signature-required.h>
#include <packagekit-glib2/pk-enum.h>
#include <packagekit-glib2/pk-enum-types.h>

static void     pk_repo_signature_required_finalize	(GObject     *object);

#define PK_REPO_SIGNATURE_REQUIRED_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_REPO_SIGNATURE_REQUIRED, PkRepoSignatureRequiredPrivate))

/**
 * PkRepoSignatureRequiredPrivate:
 *
 * Private #PkRepoSignatureRequired data
 **/
struct _PkRepoSignatureRequiredPrivate
{
	gchar				*package_id;
	gchar				*repository_name;
	gchar				*key_url;
	gchar				*key_userid;
	gchar				*key_id;
	gchar				*key_fingerprint;
	gchar				*key_timestamp;
	PkSigTypeEnum			 type;
};

enum {
	PROP_0,
	PROP_PACKAGE_ID,
	PROP_REPOSITORY_NAME,
	PROP_KEY_URL,
	PROP_KEY_USERID,
	PROP_KEY_ID,
	PROP_KEY_FINGERPRINT,
	PROP_KEY_TIMESTAMP,
	PROP_TYPE,
	PROP_LAST
};

G_DEFINE_TYPE (PkRepoSignatureRequired, pk_repo_signature_required, PK_TYPE_SOURCE)

/*
 * pk_repo_signature_required_get_property:
 **/
static void
pk_repo_signature_required_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkRepoSignatureRequired *repo_signature_required = PK_REPO_SIGNATURE_REQUIRED (object);
	PkRepoSignatureRequiredPrivate *priv = repo_signature_required->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_value_set_string (value, priv->package_id);
		break;
	case PROP_REPOSITORY_NAME:
		g_value_set_string (value, priv->repository_name);
		break;
	case PROP_KEY_URL:
		g_value_set_string (value, priv->key_url);
		break;
	case PROP_KEY_USERID:
		g_value_set_string (value, priv->key_userid);
		break;
	case PROP_KEY_ID:
		g_value_set_string (value, priv->key_id);
		break;
	case PROP_KEY_FINGERPRINT:
		g_value_set_string (value, priv->key_fingerprint);
		break;
	case PROP_KEY_TIMESTAMP:
		g_value_set_string (value, priv->key_timestamp);
		break;
	case PROP_TYPE:
		g_value_set_enum (value, priv->type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_repo_signature_required_set_property:
 **/
static void
pk_repo_signature_required_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PkRepoSignatureRequired *repo_signature_required = PK_REPO_SIGNATURE_REQUIRED (object);
	PkRepoSignatureRequiredPrivate *priv = repo_signature_required->priv;

	switch (prop_id) {
	case PROP_PACKAGE_ID:
		g_free (priv->package_id);
		priv->package_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_REPOSITORY_NAME:
		g_free (priv->repository_name);
		priv->repository_name = g_strdup (g_value_get_string (value));
		break;
	case PROP_KEY_URL:
		g_free (priv->key_url);
		priv->key_url = g_strdup (g_value_get_string (value));
		break;
	case PROP_KEY_USERID:
		g_free (priv->key_userid);
		priv->key_userid = g_strdup (g_value_get_string (value));
		break;
	case PROP_KEY_ID:
		g_free (priv->key_id);
		priv->key_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_KEY_FINGERPRINT:
		g_free (priv->key_fingerprint);
		priv->key_fingerprint = g_strdup (g_value_get_string (value));
		break;
	case PROP_KEY_TIMESTAMP:
		g_free (priv->key_timestamp);
		priv->key_timestamp = g_strdup (g_value_get_string (value));
		break;
	case PROP_TYPE:
		priv->type = g_value_get_enum (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * pk_repo_signature_required_class_init:
 **/
static void
pk_repo_signature_required_class_init (PkRepoSignatureRequiredClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_repo_signature_required_finalize;
	object_class->get_property = pk_repo_signature_required_get_property;
	object_class->set_property = pk_repo_signature_required_set_property;

	/**
	 * PkRepoSignatureRequired:package-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("package-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PACKAGE_ID, pspec);

	/**
	 * PkRepoSignatureRequired:repository-name:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("repository-name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REPOSITORY_NAME, pspec);

	/**
	 * PkRepoSignatureRequired:key-url:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("key-url", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KEY_URL, pspec);

	/**
	 * PkRepoSignatureRequired:key-userid:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("key-userid", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KEY_USERID, pspec);

	/**
	 * PkRepoSignatureRequired:key-id:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("key-id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KEY_ID, pspec);

	/**
	 * PkRepoSignatureRequired:key-fingerprint:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("key-fingerprint", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KEY_FINGERPRINT, pspec);

	/**
	 * PkRepoSignatureRequired:key-timestamp:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_string ("key-timestamp", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_KEY_TIMESTAMP, pspec);

	/**
	 * PkRepoSignatureRequired:type:
	 *
	 * Since: 0.5.4
	 */
	pspec = g_param_spec_enum ("type", NULL, NULL,
				   PK_TYPE_SIG_TYPE_ENUM, PK_SIGTYPE_ENUM_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_TYPE, pspec);

	g_type_class_add_private (klass, sizeof (PkRepoSignatureRequiredPrivate));
}

/*
 * pk_repo_signature_required_init:
 **/
static void
pk_repo_signature_required_init (PkRepoSignatureRequired *repo_signature_required)
{
	repo_signature_required->priv = PK_REPO_SIGNATURE_REQUIRED_GET_PRIVATE (repo_signature_required);
}

/*
 * pk_repo_signature_required_finalize:
 **/
static void
pk_repo_signature_required_finalize (GObject *object)
{
	PkRepoSignatureRequired *repo_signature_required = PK_REPO_SIGNATURE_REQUIRED (object);
	PkRepoSignatureRequiredPrivate *priv = repo_signature_required->priv;

	g_free (priv->package_id);
	g_free (priv->repository_name);
	g_free (priv->key_url);
	g_free (priv->key_userid);
	g_free (priv->key_id);
	g_free (priv->key_fingerprint);
	g_free (priv->key_timestamp);

	G_OBJECT_CLASS (pk_repo_signature_required_parent_class)->finalize (object);
}

/**
 * pk_repo_signature_required_new:
 *
 * Return value: a new PkRepoSignatureRequired object.
 *
 * Since: 0.5.4
 **/
PkRepoSignatureRequired *
pk_repo_signature_required_new (void)
{
	PkRepoSignatureRequired *repo_signature_required;
	repo_signature_required = g_object_new (PK_TYPE_REPO_SIGNATURE_REQUIRED, NULL);
	return PK_REPO_SIGNATURE_REQUIRED (repo_signature_required);
}

