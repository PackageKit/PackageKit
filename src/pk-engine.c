/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2011 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <packagekit-glib2/pk-version.h>
#ifdef USE_SECURITY_POLKIT
#include <polkit/polkit.h>
#endif

#include "pk-backend.h"
#include "pk-cache.h"
#include "pk-conf.h"
#include "pk-dbus.h"
#include "pk-engine.h"
#include "pk-marshal.h"
#include "pk-network.h"
#include "pk-notify.h"
#include "pk-plugin.h"
#include "pk-shared.h"
#include "pk-transaction-db.h"
#include "pk-transaction.h"
#include "pk-transaction-list.h"

static void     pk_engine_finalize	(GObject       *object);
static void	pk_engine_set_locked (PkEngine *engine, gboolean is_locked);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))

struct PkEnginePrivate
{
	GTimer			*timer;
	gboolean		 notify_clients_of_upgrade;
	gboolean		 shutdown_as_soon_as_possible;
	gboolean		 using_hardcoded_proxy;
	PkTransactionList	*transaction_list;
	PkTransactionDb		*transaction_db;
	PkCache			*cache;
	PkBackend		*backend;
	PkNetwork		*network;
	PkNotify		*notify;
	PkConf			*conf;
	PkDbus			*dbus;
	GFileMonitor		*monitor_conf;
	GFileMonitor		*monitor_binary;
	PkBitfield		 roles;
	PkBitfield		 groups;
	PkBitfield		 filters;
	gchar			*mime_types;
	gchar			*backend_name;
	gchar			*backend_description;
	gchar			*backend_author;
	gchar			*distro_id;
	guint			 timeout_priority;
	guint			 timeout_normal;
	guint			 timeout_priority_id;
	guint			 timeout_normal_id;
#ifdef USE_SECURITY_POLKIT
	PolkitAuthority		*authority;
#endif
	gboolean		 locked;
	PkNetworkEnum		 network_state;
	GPtrArray		*plugins;
	guint			 owner_id;
	GDBusNodeInfo		*introspection;
	GDBusConnection		*connection;
};

enum {
	SIGNAL_QUIT,
	SIGNAL_LAST
};

static guint	     signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkEngine, pk_engine, G_TYPE_OBJECT)

/* prototype */
gboolean pk_engine_filter_check (const gchar *filter, GError **error);

/**
 * pk_engine_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
pk_engine_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_engine_error");
	return quark;
}

/**
 * pk_engine_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_engine_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_ENGINE_ERROR_INVALID_STATE, "InvalidState"),
			ENUM_ENTRY (PK_ENGINE_ERROR_REFUSED_BY_POLICY, "RefusedByPolicy"),
			ENUM_ENTRY (PK_ENGINE_ERROR_CANNOT_SET_PROXY, "CannotSetProxy"),
			ENUM_ENTRY (PK_ENGINE_ERROR_CANNOT_SET_ROOT, "CannotSetRoot"),
			ENUM_ENTRY (PK_ENGINE_ERROR_NOT_SUPPORTED, "NotSupported"),
			ENUM_ENTRY (PK_ENGINE_ERROR_CANNOT_ALLOCATE_TID, "CannotAllocateTid"),
			ENUM_ENTRY (PK_ENGINE_ERROR_CANNOT_CHECK_AUTH, "CannotCheckAuth"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkEngineError", values);
	}
	return etype;
}

/**
 * pk_engine_reset_timer:
 **/
static void
pk_engine_reset_timer (PkEngine *engine)
{
	g_timer_reset (engine->priv->timer);
}

/**
 * pk_engine_transaction_list_changed_cb:
 **/
static void
pk_engine_transaction_list_changed_cb (PkTransactionList *tlist, PkEngine *engine)
{
	gchar **transaction_list;
	gboolean locked;

	g_return_if_fail (PK_IS_ENGINE (engine));

	/* automatically locked if the transaction cannot be cancelled */
	locked = pk_transaction_list_get_locked (tlist);
	pk_engine_set_locked (engine, locked);

	g_debug ("emitting transaction-list-changed");
	transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "TransactionListChanged",
				       g_variant_new ("(^a&s)",
						      transaction_list),
				       NULL);
	pk_engine_reset_timer (engine);
	g_strfreev (transaction_list);
}

/**
 * pk_engine_emit_changed:
 **/
static void
pk_engine_emit_changed (PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("emitting changed");
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "Changed",
				       NULL,
				       NULL);
}

/**
 * pk_engine_emit_property_changed:
 **/
static void
pk_engine_emit_property_changed (PkEngine *engine,
				 const gchar *property_name,
				 GVariant *property_value)
{
	GVariantBuilder builder;
	GVariantBuilder invalidated_builder;

	/* not yet connected */
	if (engine->priv->connection == NULL)
		return;

	/* build the dict */
	g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder,
			       "{sv}",
			       property_name,
			       property_value);
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       g_variant_new ("(sa{sv}as)",
				       PK_DBUS_INTERFACE,
				       &builder,
				       &invalidated_builder),
				       NULL);
}

/**
 * pk_engine_set_locked:
 **/
static void
pk_engine_set_locked (PkEngine *engine, gboolean is_locked)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* already set */
	if (engine->priv->locked == is_locked)
		return;

	engine->priv->locked = is_locked;

	/* emit */
	pk_engine_emit_property_changed (engine,
					 "Locked",
					 g_variant_new_boolean (is_locked));
	pk_engine_emit_changed (engine);
}

/**
 * pk_engine_notify_repo_list_changed_cb:
 **/
static void
pk_engine_notify_repo_list_changed_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("emitting repo-list-changed");
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "RepoListChanged",
				       NULL,
				       NULL);
}

/**
 * pk_engine_notify_updates_changed_cb:
 **/
static void
pk_engine_notify_updates_changed_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("emitting updates-changed");
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "UpdatesChanged",
				       NULL,
				       NULL);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkBackend *backend, PkExitEnum exit_enum, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* cannot be locked if the transaction is finished */
	pk_engine_set_locked (engine, FALSE);

	/* daemon is busy */
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_state_changed_cb:
 *
 * wait a little delay in case we get multiple requests or we need to setup state
 **/
static gboolean
pk_engine_state_changed_cb (gpointer data)
{
	PkNetworkEnum state;
	PkEngine *engine = PK_ENGINE (data);

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* if network is not up, then just reschedule */
	state = pk_network_get_network_state (engine->priv->network);
	if (state == PK_NETWORK_ENUM_OFFLINE) {
		/* wait another timeout of PK_ENGINE_STATE_CHANGED_x_TIMEOUT */
		return TRUE;
	}

	g_debug ("unreffing updates cache as state may have changed");
	pk_cache_invalidate (engine->priv->cache);

	pk_notify_updates_changed (engine->priv->notify);

	/* reset, now valid */
	engine->priv->timeout_priority_id = 0;
	engine->priv->timeout_normal_id = 0;

	/* reset the timer */
	pk_engine_reset_timer (engine);

	return FALSE;
}

/**
 * pk_engine_emit_restart_schedule:
 **/
static void
pk_engine_emit_restart_schedule (PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("emitting restart-schedule");
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "RestartSchedule",
				       NULL,
				       NULL);
}

/**
 * pk_engine_get_seconds_idle:
 **/
guint
pk_engine_get_seconds_idle (PkEngine *engine)
{
	guint idle;
	guint size;

	g_return_val_if_fail (PK_IS_ENGINE (engine), 0);

	/* check for transactions running - a transaction that takes a *long* time might not
	 * give sufficient percentage updates to not be marked as idle */
	size = pk_transaction_list_get_size (engine->priv->transaction_list);
	if (size != 0) {
		g_debug ("engine idle zero as %i transactions in progress", size);
		return 0;
	}

	/* have we been updated? */
	if (engine->priv->notify_clients_of_upgrade) {
		pk_engine_emit_restart_schedule (engine);
		return G_MAXUINT;
	}

	/* do we need to shutdown quickly */
	if (engine->priv->shutdown_as_soon_as_possible) {
		g_debug ("need to restart daemon asap");
		return G_MAXUINT;
	}

	idle = (guint) g_timer_elapsed (engine->priv->timer, NULL);
	return idle;
}

/**
 * pk_engine_set_proxy_internal:
 **/
static gboolean
pk_engine_set_proxy_internal (PkEngine *engine, const gchar *sender,
			      const gchar *proxy_http,
			      const gchar *proxy_https,
			      const gchar *proxy_ftp,
			      const gchar *proxy_socks,
			      const gchar *no_proxy,
			      const gchar *pac)
{
	gboolean ret;
	guint uid;
	gchar *session = NULL;

	/* try to set the new proxy */
	ret = pk_backend_set_proxy (engine->priv->backend,
				    proxy_http,
				    proxy_https,
				    proxy_ftp,
				    proxy_socks,
				    no_proxy,
				    pac);
	if (!ret) {
		g_warning ("setting the proxy failed");
		goto out;
	}

	/* get uid */
	uid = pk_dbus_get_uid (engine->priv->dbus, sender);
	if (uid == G_MAXUINT) {
		g_warning ("failed to get the uid");
		goto out;
	}

	/* get session */
	session = pk_dbus_get_session (engine->priv->dbus, sender);
	if (session == NULL) {
		g_warning ("failed to get the session");
		goto out;
	}

	/* save to database */
	ret = pk_transaction_db_set_proxy (engine->priv->transaction_db,
					   uid, session,
					   proxy_http,
					   proxy_https,
					   proxy_ftp,
					   proxy_socks,
					   no_proxy,
					   pac);
	if (!ret) {
		g_warning ("failed to save the proxy in the database");
		goto out;
	}
out:
	g_free (session);
	return ret;
}

#ifdef USE_SECURITY_POLKIT
typedef struct {
	GDBusMethodInvocation	*context;
	PkEngine		*engine;
	gchar			*sender;
	gchar			*value1;
	gchar			*value2;
	gchar			*value3;
	gchar			*value4;
	gchar			*value5;
	gchar			*value6;
} PkEngineDbusState;
#endif

#ifdef USE_SECURITY_POLKIT
/**
 * pk_engine_action_obtain_authorization:
 **/
static void
pk_engine_action_obtain_proxy_authorization_finished_cb (PolkitAuthority *authority,
							 GAsyncResult *res,
							 PkEngineDbusState *state)
{
	PolkitAuthorizationResult *result;
	GError *error_local = NULL;
	GError *error;
	gboolean ret;
	PkEnginePrivate *priv = state->engine->priv;

	/* finish the call */
	result = polkit_authority_check_authorization_finish (priv->authority, res, &error_local);

	/* failed */
	if (result == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_PROXY,
				     "setting the proxy failed, could not check for auth: %s",
				     error_local->message);
		g_dbus_method_invocation_return_gerror (state->context, error);
		g_error_free (error_local);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		error = g_error_new_literal (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "failed to obtain auth");
		g_dbus_method_invocation_return_gerror (state->context, error);
		goto out;
	}

	/* admin already set value, so silently refuse value */
	if (priv->using_hardcoded_proxy) {
		g_debug ("cannot override admin set proxy");
		g_dbus_method_invocation_return_value (state->context, NULL);
		goto out;
	}

	/* try to set the new proxy and save to database */
	ret = pk_engine_set_proxy_internal (state->engine, state->sender,
					    state->value1,
					    state->value2,
					    state->value3,
					    state->value4,
					    state->value5,
					    state->value6);
	if (!ret) {
		error = g_error_new_literal (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "setting the proxy failed");
		g_dbus_method_invocation_return_gerror (state->context, error);
		goto out;
	}

	/* only set after the auth success */
	g_debug ("changing http proxy to %s for %s", state->value1, state->sender);
	g_debug ("changing https proxy to %s for %s", state->value2, state->sender);
	g_debug ("changing ftp proxy to %s for %s", state->value3, state->sender);
	g_debug ("changing socks proxy to %s for %s", state->value4, state->sender);
	g_debug ("changing no proxy to %s for %s", state->value5, state->sender);
	g_debug ("changing PAC proxy to %s for %s", state->value6, state->sender);

	/* all okay */
	g_dbus_method_invocation_return_value (state->context, NULL);
out:
	if (result != NULL)
		g_object_unref (result);

	/* unref state, we're done */
	g_object_unref (state->engine);
	g_free (state->sender);
	g_free (state->value1);
	g_free (state->value2);
	g_free (state);
}
#endif

/**
 * pk_engine_is_proxy_unchanged:
 **/
static gboolean
pk_engine_is_proxy_unchanged (PkEngine *engine, const gchar *sender,
			      const gchar *proxy_http,
			      const gchar *proxy_https,
			      const gchar *proxy_ftp,
			      const gchar *proxy_socks,
			      const gchar *no_proxy,
			      const gchar *pac)
{
	guint uid;
	gboolean ret = FALSE;
	gchar *session = NULL;
	gchar *proxy_http_tmp = NULL;
	gchar *proxy_https_tmp = NULL;
	gchar *proxy_ftp_tmp = NULL;
	gchar *proxy_socks_tmp = NULL;
	gchar *no_proxy_tmp = NULL;
	gchar *pac_tmp = NULL;

	/* get uid */
	uid = pk_dbus_get_uid (engine->priv->dbus, sender);
	if (uid == G_MAXUINT) {
		g_warning ("failed to get the uid for %s", sender);
		goto out;
	}

	/* get session */
	session = pk_dbus_get_session (engine->priv->dbus, sender);
	if (session == NULL) {
		g_warning ("failed to get the session for %s", sender);
		goto out;
	}

	/* find out if they are the same as what we tried to set before */
	ret = pk_transaction_db_get_proxy (engine->priv->transaction_db,
					   uid,
					   session,
					   &proxy_http_tmp,
					   &proxy_https_tmp,
					   &proxy_ftp_tmp,
					   &proxy_socks_tmp,
					   &no_proxy_tmp,
					   &pac_tmp);
	if (!ret)
		goto out;

	/* are different? */
	if (g_strcmp0 (proxy_http_tmp, proxy_http) != 0 ||
	    g_strcmp0 (proxy_https_tmp, proxy_https) != 0 ||
	    g_strcmp0 (proxy_ftp_tmp, proxy_ftp) != 0 ||
	    g_strcmp0 (proxy_socks_tmp, proxy_socks) != 0 ||
	    g_strcmp0 (no_proxy_tmp, no_proxy) != 0 ||
	    g_strcmp0 (pac_tmp, pac) != 0)
		ret = FALSE;
out:
	g_free (session);
	g_free (proxy_http_tmp);
	g_free (proxy_https_tmp);
	g_free (proxy_ftp_tmp);
	g_free (proxy_socks_tmp);
	g_free (no_proxy_tmp);
	g_free (pac_tmp);
	return ret;
}

/**
 * pk_engine_set_proxy:
 **/
static void
pk_engine_set_proxy (PkEngine *engine,
		     const gchar *proxy_http,
		     const gchar *proxy_https,
		     const gchar *proxy_ftp,
		     const gchar *proxy_socks,
		     const gchar *no_proxy,
		     const gchar *pac,
		     GDBusMethodInvocation *context)
{
	guint len;
	GError *error = NULL;
	gboolean ret;
	const gchar *sender;
#ifdef USE_SECURITY_POLKIT
	PolkitSubject *subject;
	PkEngineDbusState *state;
#endif
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* blank is NULL */
	if (proxy_http != NULL && proxy_http[0] == '\0')
		proxy_http = NULL;
	if (proxy_ftp != NULL && proxy_ftp[0] == '\0')
		proxy_ftp = NULL;

	g_debug ("SetProxy method called: %s, %s", proxy_http, proxy_ftp);

	/* check length of http */
	len = pk_strlen (proxy_http, 1024);
	if (len == 1024) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "http proxy was too long");
		g_dbus_method_invocation_return_gerror (context, error);
		goto out;
	}

	/* check length of ftp */
	len = pk_strlen (proxy_ftp, 1024);
	if (len == 1024) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "ftp proxy was too long");
		g_dbus_method_invocation_return_gerror (context, error);
		goto out;
	}

	/* save sender */
	sender = g_dbus_method_invocation_get_sender (context);

	/* is exactly the same proxy? */
	ret = pk_engine_is_proxy_unchanged (engine, sender,
					    proxy_http,
					    proxy_https,
					    proxy_ftp,
					    proxy_socks,
					    no_proxy,
					    pac);
	if (ret) {
		g_debug ("not changing proxy as the same as before");
		g_dbus_method_invocation_return_value (context, NULL);
		goto out;
	}

#ifdef USE_SECURITY_POLKIT
	/* check subject */
	subject = polkit_system_bus_name_new (sender);

	/* cache state */
	state = g_new0 (PkEngineDbusState, 1);
	state->context = context;
	state->engine = g_object_ref (engine);
	state->sender = g_strdup (sender);
	state->value1 = g_strdup (proxy_http);
	state->value2 = g_strdup (proxy_https);
	state->value3 = g_strdup (proxy_ftp);
	state->value4 = g_strdup (proxy_socks);
	state->value5 = g_strdup (no_proxy);
	state->value6 = g_strdup (pac);

	/* do authorization async */
	polkit_authority_check_authorization (engine->priv->authority, subject,
					      "org.freedesktop.packagekit.system-network-proxy-configure",
					      NULL,
					      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
					      NULL,
					      (GAsyncReadyCallback) pk_engine_action_obtain_proxy_authorization_finished_cb,
					      state);
#else
	g_warning ("*** THERE IS NO SECURITY MODEL BEING USED!!! ***");

	/* try to set the new proxy and save to database */
	ret = pk_engine_set_proxy_internal (engine, sender,
					    proxy_http,
					    proxy_https,
					    proxy_ftp,
					    proxy_socks,
					    no_proxy,
					    pac);
	if (!ret) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "setting the proxy failed");
		g_dbus_method_invocation_return_gerror (context, error);
		goto out;
	}

	/* all okay */
	g_dbus_method_invocation_return_value (context, NULL);
#endif

	/* reset the timer */
	pk_engine_reset_timer (engine);

#ifdef USE_SECURITY_POLKIT
	g_object_unref (subject);
#endif
out:
	return;
}

/**
 * pk_engine_set_root_internal:
 **/
static gboolean
pk_engine_set_root_internal (PkEngine *engine, const gchar *root, const gchar *sender)
{
	gboolean ret;
	guint uid;
	gchar *session = NULL;

	/* try to set the new root */
	ret = pk_backend_set_root (engine->priv->backend, root);
	if (!ret) {
		g_warning ("setting the root failed");
		goto out;
	}

	/* get uid */
	uid = pk_dbus_get_uid (engine->priv->dbus, sender);
	if (uid == G_MAXUINT) {
		g_warning ("failed to get the uid");
		goto out;
	}

	/* get session */
	session = pk_dbus_get_session (engine->priv->dbus, sender);
	if (session == NULL) {
		g_warning ("failed to get the session");
		goto out;
	}

	/* save to database */
	ret = pk_transaction_db_set_root (engine->priv->transaction_db, uid, session, root);
	if (!ret) {
		g_warning ("failed to save the root in the database");
		goto out;
	}
out:
	g_free (session);
	return ret;
}

#ifdef USE_SECURITY_POLKIT
/**
 * pk_engine_action_obtain_authorization:
 **/
static void
pk_engine_action_obtain_root_authorization_finished_cb (PolkitAuthority *authority,
							GAsyncResult *res,
							PkEngineDbusState *state)
{
	PolkitAuthorizationResult *result;
	GError *error_local = NULL;
	GError *error;
	gboolean ret;
	PkEnginePrivate *priv = state->engine->priv;

	/* finish the call */
	result = polkit_authority_check_authorization_finish (priv->authority, res, &error_local);

	/* failed */
	if (result == NULL) {
		error = g_error_new (PK_ENGINE_ERROR,
				     PK_ENGINE_ERROR_CANNOT_SET_ROOT,
				     "could not check for auth: %s",
				     error_local->message);
		g_dbus_method_invocation_return_gerror (state->context, error);
		g_error_free (error_local);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_ROOT,
					     "failed to obtain auth");
		g_dbus_method_invocation_return_gerror (state->context, error);
		goto out;
	}

	/* try to set the new root and save to database */
	ret = pk_engine_set_root_internal (state->engine, state->value1, state->sender);
	if (!ret) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_ROOT,
					     "setting the root failed");
		g_dbus_method_invocation_return_gerror (state->context, error);
		goto out;
	}

	/* save these so we can set them after the auth success */
	g_debug ("changing root to %s for %s", state->value1, state->sender);

	/* all okay */
	g_dbus_method_invocation_return_value (state->context, NULL);
out:
	if (result != NULL)
		g_object_unref (result);

	/* unref state, we're done */
	g_object_unref (state->engine);
	g_free (state->sender);
	g_free (state->value1);
	g_free (state->value2);
	g_free (state);
}
#endif

/**
 * pk_engine_is_root_unchanged:
 **/
static gboolean
pk_engine_is_root_unchanged (PkEngine *engine, const gchar *sender, const gchar *root)
{
	guint uid;
	gboolean ret = FALSE;
	gchar *session = NULL;
	gchar *root_tmp = NULL;

	/* get uid */
	uid = pk_dbus_get_uid (engine->priv->dbus, sender);
	if (uid == G_MAXUINT) {
		g_warning ("failed to get the uid for %s", sender);
		goto out;
	}

	/* get session */
	session = pk_dbus_get_session (engine->priv->dbus, sender);
	if (session == NULL) {
		g_warning ("failed to get the session for %s", sender);
		goto out;
	}

	/* find out if they are the same as what we tried to set before */
	ret = pk_transaction_db_get_root (engine->priv->transaction_db, uid, session, &root_tmp);
	if (!ret)
		goto out;

	/* are different? */
	if (g_strcmp0 (root_tmp, root) != 0)
		ret = FALSE;
out:
	g_free (session);
	g_free (root_tmp);
	return ret;
}

/**
 * pk_engine_set_root:
 **/
static void
pk_engine_set_root (PkEngine *engine,
		    const gchar *root,
		    GDBusMethodInvocation *context)
{
	guint len;
	GError *error = NULL;
	gboolean ret;
	const gchar *sender;
#ifdef USE_SECURITY_POLKIT
	PolkitSubject *subject;
	PkEngineDbusState *state;
#endif
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* blank is default */
	if (root == NULL ||
	    root[0] == '\0')
		root = "/";

	g_debug ("SetRoot method called: %s", root);

	/* check length of root */
	len = pk_strlen (root, 1024);
	if (len == 1024) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "root was too long: %s", root);
		g_dbus_method_invocation_return_gerror (context, error);
		goto out;
	}

	/* check prefix of root */
	if (root[0] != '/') {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "root is not absolute: %s", root);
		g_dbus_method_invocation_return_gerror (context, error);
		goto out;
	}

	/* save sender */
	sender = g_dbus_method_invocation_get_sender (context);

	/* is exactly the same root? */
	ret = pk_engine_is_root_unchanged (engine, sender, root);
	if (ret) {
		g_debug ("not changing root as the same as before");
		g_dbus_method_invocation_return_value (context, NULL);
		goto out;
	}

	/* '/' is the default root, which doesn't need additional authentication */
	if (g_strcmp0 (root, "/") == 0) {
		ret = pk_engine_set_root_internal (engine, root, sender);
		if (ret) {
			g_debug ("using default root, so no need to authenticate");
			g_dbus_method_invocation_return_value (context, NULL);
		} else {
			error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "%s", "setting the root failed");
			g_dbus_method_invocation_return_gerror (context, error);
		}
		goto out;
	}

#ifdef USE_SECURITY_POLKIT
	/* check subject */
	subject = polkit_system_bus_name_new (sender);

	/* cache state */
	state = g_new0 (PkEngineDbusState, 1);
	state->context = context;
	state->engine = g_object_ref (engine);
	state->sender = g_strdup (sender);
	state->value1 = g_strdup (root);

	/* do authorization async */
	polkit_authority_check_authorization (engine->priv->authority, subject,
					      "org.freedesktop.packagekit.system-change-install-root",
					      NULL,
					      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
					      NULL,
					      (GAsyncReadyCallback) pk_engine_action_obtain_root_authorization_finished_cb,
					      state);
#else
	g_warning ("*** THERE IS NO SECURITY MODEL BEING USED!!! ***");

	/* try to set the new root and save to database */
	ret = pk_engine_set_root_internal (engine, root, sender);
	if (!ret) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "%s", "setting the root failed");
		g_dbus_method_invocation_return_gerror (context, error);
		goto out;
	}

	/* all okay */
	g_dbus_method_invocation_return_value (context, NULL);
#endif

	/* reset the timer */
	pk_engine_reset_timer (engine);

#ifdef USE_SECURITY_POLKIT
	g_object_unref (subject);
#endif
out:
	return;
}

/**
 * pk_engine_can_authorize:
 **/
static PkAuthorizeEnum
pk_engine_can_authorize_action_id (PkEngine *engine,
				   const gchar *action_id,
				   const gchar *sender,
				   GError **error)
{
#ifdef USE_SECURITY_POLKIT
	gboolean ret;
	PkAuthorizeEnum authorize;
	PolkitAuthorizationResult *res;
	PolkitSubject *subject;

	/* check subject */
	subject = polkit_system_bus_name_new (sender);

	/* check authorization (okay being sync as there's no blocking on the user) */
	res = polkit_authority_check_authorization_sync (engine->priv->authority,
							 subject,
							 action_id,
							 NULL,
							 POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
							 NULL,
							 error);
	if (res == NULL) {
		authorize = PK_AUTHORIZE_ENUM_UNKNOWN;
		goto out;
	}

	/* already yes */
	ret = polkit_authorization_result_get_is_authorized (res);
	if (ret) {
		authorize = PK_AUTHORIZE_ENUM_YES;
		goto out;
	}

	/* could be yes with user input */
	ret = polkit_authorization_result_get_is_challenge (res);
	if (ret) {
		authorize = PK_AUTHORIZE_ENUM_INTERACTIVE;
		goto out;
	}

	/* fall back to not letting user authenticate */
	authorize = PK_AUTHORIZE_ENUM_NO;
out:
	if (res != NULL)
		g_object_unref (res);
	g_object_unref (subject);
	return authorize;
#else
	return PK_AUTHORIZE_ENUM_YES;
#endif
}

/**
 * pk_engine_class_init:
 * @klass: The PkEngineClass
 **/
static void
pk_engine_class_init (PkEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_engine_finalize;

	/* signals */
	signals[SIGNAL_QUIT] =
		g_signal_new ("quit",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkEnginePrivate));
}

/**
 * pk_engine_conf_file_changed_cb:
 *
 * A config file has changed, we need to reload the daemon
 **/
static void
pk_engine_conf_file_changed_cb (GFileMonitor *file_monitor,
				GFile *file,
				GFile *other_file,
				GFileMonitorEvent event_type,
				PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	g_debug ("setting shutdown_as_soon_as_possible TRUE");
	engine->priv->shutdown_as_soon_as_possible = TRUE;
}

/**
 * pk_engine_binary_file_changed_cb:
 **/
static void
pk_engine_binary_file_changed_cb (GFileMonitor *file_monitor,
				  GFile *file,
				  GFile *other_file,
				  GFileMonitorEvent event_type,
				  PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	g_debug ("setting notify_clients_of_upgrade TRUE");
	engine->priv->notify_clients_of_upgrade = TRUE;
}

/**
 * pk_engine_network_state_changed_cb:
 **/
static void
pk_engine_network_state_changed_cb (PkNetwork *network, PkNetworkEnum network_state, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* already set */
	if (engine->priv->network_state == network_state)
		return;

	engine->priv->network_state = network_state;

	/* emit */
	pk_engine_emit_property_changed (engine,
					 "NetworkState",
					 g_variant_new_string (pk_network_enum_to_string (network_state)));
	pk_engine_emit_changed (engine);
}


/**
 * pk_engine_load_plugin:
 */
static void
pk_engine_load_plugin (PkEngine *engine,
		       const gchar *filename)
{
	gboolean ret;
	GModule *module;
	PkPlugin *plugin;
	PkPluginGetDescFunc plugin_desc = NULL;

	module = g_module_open (filename,
				0);
	if (module == NULL) {
		g_warning ("failed to open plugin %s: %s",
			   filename, g_module_error ());
		goto out;
	}

	/* get description */
	ret = g_module_symbol (module,
			       "pk_plugin_get_description",
			       (gpointer *) &plugin_desc);
	if (!ret) {
		g_warning ("Plugin %s requires description",
			   filename);
		g_module_close (module);
		goto out;
	}

	/* print what we know */
	plugin = g_new0 (PkPlugin, 1);
	plugin->module = module;
	plugin->backend = g_object_ref (engine->priv->backend);
	g_debug ("opened plugin %s: %s",
		 filename, plugin_desc ());

	/* add to array */
	g_ptr_array_add (engine->priv->plugins,
			 plugin);
out:
	return;
}

/**
 * pk_engine_load_plugins:
 */
static void
pk_engine_load_plugins (PkEngine *engine)
{
	const gchar *filename_tmp;
	gchar *filename_plugin;
	gchar *path;
	GDir *dir;
	GError *error = NULL;

	/* search in the plugin directory for plugins */
	path = g_build_filename (LIBDIR, "packagekit-plugins", NULL);
	dir = g_dir_open (path, 0, &error);
	if (dir == NULL) {
		g_warning ("failed to open plugin directory: %s",
			   error->message);
		g_error_free (error);
		goto out;
	}

	/* try to open each plugin */
	g_debug ("searching for plugins in %s", path);
	do {
		filename_tmp = g_dir_read_name (dir);
		if (filename_tmp == NULL)
			break;
		if (!g_str_has_suffix (filename_tmp, ".so"))
			continue;
		filename_plugin = g_build_filename (path,
						    filename_tmp,
						    NULL);
		pk_engine_load_plugin (engine,
					    filename_plugin);
		g_free (filename_plugin);
	} while (TRUE);
out:
	if (dir != NULL)
		g_dir_close (dir);
	g_free (path);
}

/**
 * pk_engine_plugin_free:
 **/
static void
pk_engine_plugin_free (PkPlugin *plugin)
{
	g_free (plugin->priv);
	g_object_unref (plugin->backend);
	g_module_close (plugin->module);
	g_free (plugin);
}


/**
 * pk_engine_plugin_phase:
 **/
static void
pk_engine_plugin_phase (PkEngine *engine,
			PkPluginPhase phase)
{
	guint i;
	const gchar *function = NULL;
	gboolean ret;
	PkPluginFunc plugin_func = NULL;
	PkPlugin *plugin;

	switch (phase) {
	case PK_PLUGIN_PHASE_INIT:
		function = "pk_plugin_initialize";
		break;
	case PK_PLUGIN_PHASE_DESTROY:
		function = "pk_plugin_destroy";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_assert (function != NULL);

	/* run each plugin */
	for (i=0; i<engine->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (engine->priv->plugins, i);
		ret = g_module_symbol (plugin->module,
				       function,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		g_debug ("run %s on %s",
			 function,
			 g_module_name (plugin->module));
		plugin_func (plugin);
	}
}

/**
 * pk_engine_setup_file_monitors:
 **/
static void
pk_engine_setup_file_monitors (PkEngine *engine)
{
	GError *error = NULL;
	GFile *file_conf = NULL;
	GFile *file_binary = NULL;
	gchar *filename = NULL;

	/* monitor the binary file for changes */
	file_binary = g_file_new_for_path (LIBEXECDIR "/packagekitd");
	engine->priv->monitor_binary = g_file_monitor_file (file_binary,
							    G_FILE_MONITOR_NONE,
							    NULL,
							    &error);
	if (engine->priv->monitor_binary == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   LIBEXECDIR "/packagekitd",
			   error->message);
		g_error_free (error);
		goto out;
	}
	g_signal_connect (engine->priv->monitor_binary, "changed",
			  G_CALLBACK (pk_engine_binary_file_changed_cb), engine);

	/* monitor config file for changes */
	filename = pk_conf_get_filename ();
	g_debug ("setting config file watch on %s", filename);
	file_conf = g_file_new_for_path (filename);
	engine->priv->monitor_conf = g_file_monitor_file (file_conf,
							  G_FILE_MONITOR_NONE,
							  NULL,
							  &error);
	if (engine->priv->monitor_conf == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   filename,
			   error->message);
		g_error_free (error);
		goto out;
	}
	g_signal_connect (engine->priv->monitor_conf, "changed",
			  G_CALLBACK (pk_engine_conf_file_changed_cb), engine);
out:
	g_free (filename);
	if (file_conf != NULL)
		g_object_unref (file_conf);
	if (file_binary != NULL)
		g_object_unref (file_binary);
}

/**
 * _g_variant_new_maybe_string:
 **/
static GVariant *
_g_variant_new_maybe_string (const gchar *value)
{
	if (value == NULL)
		return g_variant_new_string ("");
	return g_variant_new_string (value);
}

/**
 * pk_engine_daemon_get_property:
 **/
static GVariant *
pk_engine_daemon_get_property (GDBusConnection *connection_, const gchar *sender,
			       const gchar *object_path, const gchar *interface_name,
			       const gchar *property_name, GError **error,
			       gpointer user_data)
{
	GVariant *retval = NULL;
	PkEngine *engine = PK_ENGINE (user_data);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	if (g_strcmp0 (property_name, "VersionMajor") == 0) {
		retval = g_variant_new_uint32 (PK_MAJOR_VERSION);
		goto out;
	}
	if (g_strcmp0 (property_name, "VersionMinor") == 0) {
		retval = g_variant_new_uint32 (PK_MINOR_VERSION);
		goto out;
	}
	if (g_strcmp0 (property_name, "VersionMicro") == 0) {
		retval = g_variant_new_uint32 (PK_MICRO_VERSION);
		goto out;
	}
	if (g_strcmp0 (property_name, "BackendName") == 0) {
		retval = g_variant_new_string (engine->priv->backend_name);
		goto out;
	}
	if (g_strcmp0 (property_name, "BackendDescription") == 0) {
		retval = _g_variant_new_maybe_string (engine->priv->backend_description);
		goto out;
	}
	if (g_strcmp0 (property_name, "BackendAuthor") == 0) {
		retval = _g_variant_new_maybe_string (engine->priv->backend_author);
		goto out;
	}
	if (g_strcmp0 (property_name, "Roles") == 0) {
		retval = g_variant_new_uint64 (engine->priv->roles);
		goto out;
	}
	if (g_strcmp0 (property_name, "Groups") == 0) {
		retval = g_variant_new_uint64 (engine->priv->groups);
		goto out;
	}
	if (g_strcmp0 (property_name, "Filters") == 0) {
		retval = g_variant_new_uint64 (engine->priv->filters);
		goto out;
	}
	if (g_strcmp0 (property_name, "MimeTypes") == 0) {
		retval = g_variant_new_string (engine->priv->mime_types);
		goto out;
	}
	if (g_strcmp0 (property_name, "Locked") == 0) {
		retval = g_variant_new_boolean (engine->priv->locked);
		goto out;
	}
	if (g_strcmp0 (property_name, "NetworkState") == 0) {
		retval = g_variant_new_uint32 (engine->priv->network_state);
		goto out;
	}
	if (g_strcmp0 (property_name, "DistroId") == 0) {
		retval = _g_variant_new_maybe_string (engine->priv->distro_id);
		goto out;
	}
	g_critical ("failed to get property %s",
		    property_name);
out:
	return retval;
}

/**
 * pk_engine_daemon_method_call:
 **/
static void
pk_engine_daemon_method_call (GDBusConnection *connection_, const gchar *sender,
			      const gchar *object_path, const gchar *interface_name,
			      const gchar *method_name, GVariant *parameters,
			      GDBusMethodInvocation *invocation, gpointer user_data)
{
	gchar *data = NULL;
	const gchar *tmp = NULL;
	gboolean ret;
	GError *error = NULL;
	guint time_since;
	GVariant *value = NULL;
	PkAuthorizeEnum result_enum;
	PkEngine *engine = PK_ENGINE (user_data);
	PkRoleEnum role;
	gchar **transaction_list;
	gchar **array = NULL;
	guint size;
	gboolean is_priority = TRUE;

	g_return_if_fail (PK_IS_ENGINE (engine));

	/* reset the timer */
	pk_engine_reset_timer (engine);

	if (g_strcmp0 (method_name, "GetTimeSinceAction") == 0) {
		g_variant_get (parameters, "(&s)", &tmp);
		role = pk_role_enum_from_string (tmp);
		time_since = pk_transaction_db_action_time_since (engine->priv->transaction_db,
								  role);
		value = g_variant_new ("(u)", time_since);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetDaemonState") == 0) {
		data = pk_transaction_list_get_state (engine->priv->transaction_list);
		value = g_variant_new ("(s)", data);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetTid") == 0) {

		g_debug ("GetTid method called");
		data = pk_transaction_db_generate_id (engine->priv->transaction_db);
		g_assert (data != NULL);
		ret = pk_transaction_list_create (engine->priv->transaction_list,
						  data, sender, &error);
		if (!ret) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_CANNOT_CHECK_AUTH,
							       "could not create transaction %s: %s",
							       data,
							       error->message);
			g_error_free (error);
			goto out;
		}

		g_debug ("sending tid: '%s'", data);
		value = g_variant_new ("(s)", data);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetTransactionList") == 0) {
		transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);
		value = g_variant_new ("(^a&s)", transaction_list);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}

	if (g_strcmp0 (method_name, "SuggestDaemonQuit") == 0) {

		/* attempt to kill background tasks */
		pk_transaction_list_cancel_background (engine->priv->transaction_list);

		/* can we exit straight away */
		size = pk_transaction_list_get_size (engine->priv->transaction_list);
		if (size == 0) {
			g_debug ("emitting quit");
			g_signal_emit (engine, signals[SIGNAL_QUIT], 0);
			g_dbus_method_invocation_return_value (invocation, NULL);
			goto out;
		}

		/* This will wait from 0..10 seconds, depending on the status of
		 * pk_main_timeout_check_cb() - usually it should be a few seconds
		 * after the last transaction */
		engine->priv->shutdown_as_soon_as_possible = TRUE;
		g_dbus_method_invocation_return_value (invocation, NULL);
		goto out;
	}

	if (g_strcmp0 (method_name, "StateHasChanged") == 0) {

		/* have we already scheduled priority? */
		if (engine->priv->timeout_priority_id != 0) {
			g_debug ("Already asked to refresh priority state less than %i seconds ago",
				 engine->priv->timeout_priority);
			g_dbus_method_invocation_return_value (invocation, NULL);
			goto out;
		}

		/* don't bombard the user 10 seconds after resuming */
		g_variant_get (parameters, "(&s)", &tmp);
		if (g_strcmp0 (tmp, "resume") == 0)
			is_priority = FALSE;

		/* are we normal, and already scheduled normal? */
		if (!is_priority && engine->priv->timeout_normal_id != 0) {
			g_debug ("Already asked to refresh normal state less than %i seconds ago",
				 engine->priv->timeout_normal);
			g_dbus_method_invocation_return_value (invocation, NULL);
			goto out;
		}

		/* are we priority, and already scheduled normal? */
		if (is_priority && engine->priv->timeout_normal_id != 0) {
			/* clear normal, as we are about to schedule a priority */
			g_source_remove (engine->priv->timeout_normal_id);
			engine->priv->timeout_normal_id = 0;
		}

		/* wait a little delay in case we get multiple requests */
		if (is_priority) {
			engine->priv->timeout_priority_id =
				g_timeout_add_seconds (engine->priv->timeout_priority,
						       pk_engine_state_changed_cb, engine);
			g_source_set_name_by_id (engine->priv->timeout_priority_id,
						 "[PkEngine] priority");
		} else {
			engine->priv->timeout_normal_id =
				g_timeout_add_seconds (engine->priv->timeout_normal,
						       pk_engine_state_changed_cb, engine);
			g_source_set_name_by_id (engine->priv->timeout_normal_id, "[PkEngine] normal");
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		goto out;
	}

	if (g_strcmp0 (method_name, "SetRoot") == 0) {
		g_variant_get (parameters, "(&s)", &tmp);
		pk_engine_set_root (engine,
				    tmp,
				    invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "SetProxy") == 0) {

		array = g_new0 (gchar *, 7);
		g_variant_get (parameters, "(ssssss)",
			       &array[0],
			       &array[1],
			       &array[2],
			       &array[3],
			       &array[4],
			       &array[5]);
		pk_engine_set_proxy (engine,
				     array[0],
				     array[1],
				     array[2],
				     array[3],
				     array[4],
				     array[5],
				     invocation);
		goto out;
	}

	if (g_strcmp0 (method_name, "CanAuthorize") == 0) {

		g_variant_get (parameters, "(&s)", &tmp);
		result_enum = pk_engine_can_authorize_action_id (engine,
								 tmp,
								 sender,
								 &error);
		if (result_enum == PK_AUTHORIZE_ENUM_UNKNOWN) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_CANNOT_CHECK_AUTH,
							       "failed to check authorisation %s: %s",
							       tmp,
							       error->message);
			g_error_free (error);
			goto out;
		}

		/* all okay */
		value = g_variant_new ("(s)", pk_authorize_type_enum_to_string (result_enum));
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}
out:
	g_strfreev (array);
	g_free (data);
}

/**
 * pk_engine_on_bus_acquired_cb:
 **/
static void
pk_engine_on_bus_acquired_cb (GDBusConnection *connection,
			      const gchar *name,
			      gpointer user_data)
{
	PkEngine *engine = PK_ENGINE (user_data);
	guint registration_id;

	static const GDBusInterfaceVTable interface_vtable = {
		pk_engine_daemon_method_call,
		pk_engine_daemon_get_property,
		NULL
	};

	/* save copy for emitting signals */
	engine->priv->connection = g_object_ref (connection);

	/* register org.freedesktop.PackageKit */
	registration_id = g_dbus_connection_register_object (connection,
							     PK_DBUS_PATH,
							     engine->priv->introspection->interfaces[0],
							     &interface_vtable,
							     engine,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);
}


/**
 * pk_engine_on_name_acquired_cb:
 **/
static void
pk_engine_on_name_acquired_cb (GDBusConnection *connection_,
			       const gchar *name,
			       gpointer user_data)
{
	g_debug ("PkEngine: acquired name: %s", name);
}


/**
 * pk_engine_on_name_lost_cb:
 **/
static void
pk_engine_on_name_lost_cb (GDBusConnection *connection_,
			   const gchar *name,
			   gpointer user_data)
{
	PkEngine *engine = PK_ENGINE (user_data);
	g_debug ("PkEngine: lost name: %s", name);
	g_signal_emit (engine, signals[SIGNAL_QUIT], 0);
}

/**
 * pk_engine_init:
 **/
static void
pk_engine_init (PkEngine *engine)
{
	gboolean ret;
	gchar *filename;
	gchar *root;
	gchar *proxy_http;
	gchar *proxy_https;
	gchar *proxy_ftp;
	gchar *proxy_socks;
	gchar *no_proxy;
	gchar *pac;
	GError *error = NULL;

	engine->priv = PK_ENGINE_GET_PRIVATE (engine);

	/* load introspection from file */
	engine->priv->introspection = pk_load_introspection (DATADIR "/dbus-1/interfaces/"
							     PK_DBUS_INTERFACE ".xml",
							     &error);
	if (engine->priv->introspection == NULL) {
		g_error ("PkEngine: failed to load daemon introspection: %s",
			 error->message);
		g_error_free (error);
	}

	/* use the config file */
	engine->priv->conf = pk_conf_new ();

	/* clear the download cache */
	filename = g_build_filename (LOCALSTATEDIR, "cache", "PackageKit", "downloads", NULL);
	g_debug ("clearing download cache at %s", filename);
	pk_directory_remove_contents (filename);
	g_free (filename);

	/* setup the backend backend */
	engine->priv->backend = pk_backend_new ();
	g_signal_connect (engine->priv->backend, "finished",
			  G_CALLBACK (pk_engine_finished_cb), engine);

	/* lock database */
	ret = pk_backend_open (engine->priv->backend);
	if (!ret)
		g_error ("could not lock backend, you need to restart the daemon");

	/* proxy the network state */
	engine->priv->network = pk_network_new ();
	g_signal_connect (engine->priv->network, "state-changed",
			  G_CALLBACK (pk_engine_network_state_changed_cb), engine);
	engine->priv->network_state = pk_network_get_network_state (engine->priv->network);

	/* create a new backend so we can get the static stuff */
	engine->priv->roles = pk_backend_get_roles (engine->priv->backend);
	engine->priv->groups = pk_backend_get_groups (engine->priv->backend);
	engine->priv->filters = pk_backend_get_filters (engine->priv->backend);
	engine->priv->mime_types = pk_backend_get_mime_types (engine->priv->backend);
	engine->priv->backend_name = pk_backend_get_name (engine->priv->backend);
	engine->priv->backend_description = pk_backend_get_description (engine->priv->backend);
	engine->priv->backend_author = pk_backend_get_author (engine->priv->backend);

	/* try to get the distro id */
	engine->priv->distro_id = pk_get_distro_id ();

	engine->priv->timer = g_timer_new ();

	/* we save a cache of the latest update lists sowe can do cached responses */
	engine->priv->cache = pk_cache_new ();

	/* we need the uid and the session for the proxy setting mechanism */
	engine->priv->dbus = pk_dbus_new ();

	/* we need to be able to clear this */
	engine->priv->timeout_priority_id = 0;
	engine->priv->timeout_normal_id = 0;

	/* add the interface */
	engine->priv->notify = pk_notify_new ();
	g_signal_connect (engine->priv->notify, "repo-list-changed",
			  G_CALLBACK (pk_engine_notify_repo_list_changed_cb), engine);
	g_signal_connect (engine->priv->notify, "updates-changed",
			  G_CALLBACK (pk_engine_notify_updates_changed_cb), engine);

	/* setup file watches */
	pk_engine_setup_file_monitors (engine);

#ifdef USE_SECURITY_POLKIT
	/* protect the session SetProxy with a PolicyKit action */
	engine->priv->authority = polkit_authority_get_sync (NULL, &error);
	if (engine->priv->authority == NULL) {
		g_error ("failed to get pokit authority: %s", error->message);
		g_error_free (error);
	}
#endif

	/* set the default proxy */
	proxy_http = pk_conf_get_string (engine->priv->conf, "ProxyHTTP");
	proxy_https = pk_conf_get_string (engine->priv->conf, "ProxyHTTPS");
	proxy_ftp = pk_conf_get_string (engine->priv->conf, "ProxyFTP");
	proxy_socks = pk_conf_get_string (engine->priv->conf, "ProxySOCKS");
	no_proxy = pk_conf_get_string (engine->priv->conf, "NoProxy");
	pac = pk_conf_get_string (engine->priv->conf, "PAC");
	pk_backend_set_proxy (engine->priv->backend,
			      proxy_http,
			      proxy_https,
			      proxy_ftp,
			      proxy_socks,
			      no_proxy,
			      pac);

	/* if any of these is set, we ignore the users proxy setting */
	if (proxy_http != NULL || proxy_https != NULL || proxy_ftp != NULL)
		engine->priv->using_hardcoded_proxy = TRUE;

	g_free (proxy_http);
	g_free (proxy_https);
	g_free (proxy_ftp);
	g_free (proxy_socks);
	g_free (no_proxy);
	g_free (pac);

	/* set the default root */
	root = pk_conf_get_string (engine->priv->conf, "UseRoot");
	pk_backend_set_root (engine->priv->backend, root);
	g_free (root);

	/* get the StateHasChanged timeouts */
	engine->priv->timeout_priority = (guint) pk_conf_get_int (engine->priv->conf, "StateChangedTimeoutPriority");
	engine->priv->timeout_normal = (guint) pk_conf_get_int (engine->priv->conf, "StateChangedTimeoutNormal");

	engine->priv->transaction_list = pk_transaction_list_new ();
	g_signal_connect (engine->priv->transaction_list, "changed",
			  G_CALLBACK (pk_engine_transaction_list_changed_cb), engine);

	/* get plugins */
	engine->priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) pk_engine_plugin_free);
	pk_engine_load_plugins (engine);
	pk_transaction_list_set_plugins (engine->priv->transaction_list,
					 engine->priv->plugins);

	/* we use a trasaction db to store old transactions */
	engine->priv->transaction_db = pk_transaction_db_new ();

	/* own the object */
	engine->priv->owner_id =
		g_bus_own_name (G_BUS_TYPE_SYSTEM,
				PK_DBUS_SERVICE,
				G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
				G_BUS_NAME_OWNER_FLAGS_REPLACE,
				pk_engine_on_bus_acquired_cb,
				pk_engine_on_name_acquired_cb,
				pk_engine_on_name_lost_cb,
				engine, NULL);

	/* initialize plugins */
	pk_engine_plugin_phase (engine,
				PK_PLUGIN_PHASE_INIT);
}

/**
 * pk_engine_finalize:
 * @object: The object to finalize
 **/
static void
pk_engine_finalize (GObject *object)
{
	PkEngine *engine;
	gboolean ret;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_ENGINE (object));

	engine = PK_ENGINE (object);

	g_return_if_fail (engine->priv != NULL);

	/* run the plugins */
	pk_engine_plugin_phase (engine,
				PK_PLUGIN_PHASE_DESTROY);

	/* unlock if we locked this */
	ret = pk_backend_close (engine->priv->backend);
	if (!ret)
		g_warning ("couldn't unlock the backend");

	/* if we set an state changed notifier, clear */
	if (engine->priv->timeout_priority_id != 0) {
		g_source_remove (engine->priv->timeout_priority_id);
		engine->priv->timeout_priority_id = 0;
	}
	if (engine->priv->timeout_normal_id != 0) {
		g_source_remove (engine->priv->timeout_normal_id);
		engine->priv->timeout_normal_id = 0;
	}

	/* unown */
	if (engine->priv->owner_id > 0)
		g_bus_unown_name (engine->priv->owner_id);

	if (engine->priv->introspection != NULL)
		g_dbus_node_info_unref (engine->priv->introspection);
	if (engine->priv->connection != NULL)
		g_object_unref (engine->priv->connection);

	/* compulsory gobjects */
	g_timer_destroy (engine->priv->timer);
	g_object_unref (engine->priv->monitor_conf);
	g_object_unref (engine->priv->monitor_binary);
	g_object_unref (engine->priv->transaction_list);
	g_object_unref (engine->priv->transaction_db);
	g_object_unref (engine->priv->network);
#ifdef USE_SECURITY_POLKIT
	g_object_unref (engine->priv->authority);
#endif
	g_object_unref (engine->priv->notify);
	g_object_unref (engine->priv->backend);
	g_object_unref (engine->priv->cache);
	g_object_unref (engine->priv->conf);
	g_object_unref (engine->priv->dbus);
	g_ptr_array_unref (engine->priv->plugins);
	g_free (engine->priv->mime_types);
	g_free (engine->priv->backend_name);
	g_free (engine->priv->backend_description);
	g_free (engine->priv->backend_author);
	g_free (engine->priv->distro_id);

	G_OBJECT_CLASS (pk_engine_parent_class)->finalize (object);
}

/**
 * pk_engine_new:
 *
 * Return value: a new PkEngine object.
 **/
PkEngine *
pk_engine_new (void)
{
	PkEngine *engine;
	engine = g_object_new (PK_TYPE_ENGINE, NULL);
	return PK_ENGINE (engine);
}

