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
static void	pk_engine_plugin_phase	(PkEngine *engine, PkPluginPhase phase);

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
	PkBitfield		 provides;
	PkBitfield		 groups;
	PkBitfield		 filters;
	gchar			**mime_types;
	const gchar		*backend_name;
	const gchar		*backend_description;
	const gchar		*backend_author;
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
#ifdef PK_BUILD_SYSTEMD
	GDBusProxy		*logind_proxy;
	gint			 logind_fd;
#endif
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
 * pk_engine_inhibit:
 **/
static void
pk_engine_inhibit (PkEngine *engine)
{
#ifdef PK_BUILD_SYSTEMD
	GVariant *res;
	GError *error = NULL;

	/* not yet connected */
	if (engine->priv->logind_proxy == NULL) {
		g_warning ("no logind connection to use");
		return;
	}

	/* block shutdown and idle */
	res = g_dbus_proxy_call_sync (engine->priv->logind_proxy,
				      "Inhibit",
				      g_variant_new ("(ssss)",
						     "shutdown:idle",
						     "Package Updater",
						     "Package Update in Progress",
						     "block"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL, /* GCancellable */
				      &error);
	if (res == NULL) {
		g_warning ("Failed to Inhibit using logind: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* keep fd as cookie */
	g_variant_get (res, "(h)", &engine->priv->logind_fd);
	g_debug ("got logind cookie %i", engine->priv->logind_fd);
out:
	if (res != NULL)
		g_variant_unref (res);
#endif
}

/**
 * pk_engine_uninhibit:
 **/
static void
pk_engine_uninhibit (PkEngine *engine)
{
#ifdef PK_BUILD_SYSTEMD
	if (engine->priv->logind_fd == 0) {
		g_warning ("no fd to close");
		return;
	}
	close (engine->priv->logind_fd);
	engine->priv->logind_fd = 0;
#endif
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

	/* inhibit shutdown and suspend */
	if (is_locked)
		pk_engine_inhibit (engine);
	else
		pk_engine_uninhibit (engine);

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

	/* run the plugin hook */
	pk_engine_plugin_phase (engine, PK_PLUGIN_PHASE_STATE_CHANGED);

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
	gboolean ret = FALSE;
	guint uid;
	gchar *session = NULL;

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
	ret = pk_engine_set_proxy_internal (state->engine,
					    state->sender,
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
					 g_variant_new_uint32 (network_state));
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
	case PK_PLUGIN_PHASE_STATE_CHANGED:
		function = "pk_plugin_state_changed";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	g_assert (function != NULL);

	/* run each plugin */
	for (i = 0; i < engine->priv->plugins->len; i++) {
		plugin = g_ptr_array_index (engine->priv->plugins, i);
		ret = g_module_symbol (plugin->module,
				       function,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		g_debug ("run %s on %s",
			 function,
			 g_module_name (plugin->module));

		/* use the master PkBackend instance in case the plugin
		 * wants to check if roles are supported in initialize */
		plugin->backend = engine->priv->backend;
		plugin_func (plugin);
		plugin->backend = NULL;
		g_debug ("finished %s", function);
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
 * pk_engine_init:
 **/
gboolean
pk_engine_load_backend (PkEngine *engine, GError **error)
{
	gboolean ret;

	/* load any backend init */
	ret = pk_backend_load (engine->priv->backend, error);
	if (!ret)
		goto out;

	/* load anything that can fail */
	ret = pk_transaction_db_load (engine->priv->transaction_db, error);
	if (!ret)
		goto out;

	/* create a new backend so we can get the static stuff */
	engine->priv->roles = pk_backend_get_roles (engine->priv->backend);
	engine->priv->provides = pk_backend_get_provides (engine->priv->backend);
	engine->priv->groups = pk_backend_get_groups (engine->priv->backend);
	engine->priv->filters = pk_backend_get_filters (engine->priv->backend);
	engine->priv->mime_types = pk_backend_get_mime_types (engine->priv->backend);
	engine->priv->backend_name = pk_backend_get_name (engine->priv->backend);
	engine->priv->backend_description = pk_backend_get_description (engine->priv->backend);
	engine->priv->backend_author = pk_backend_get_author (engine->priv->backend);
out:
	return ret;
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
	if (g_strcmp0 (property_name, "Provides") == 0) {
		retval = g_variant_new_uint64 (engine->priv->provides);
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
		retval = g_variant_new_strv ((const gchar * const *) engine->priv->mime_types, -1);
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

	/* return an error */
	g_set_error (error,
		     PK_ENGINE_ERROR,
		     PK_ENGINE_ERROR_NOT_SUPPORTED,
		     "failed to get property '%s'",
		     property_name);
out:
	return retval;
}

/**
 * pk_engine_package_name_in_strv:
 **/
static gboolean
pk_engine_package_name_in_strv (gchar **strv, PkPackage *pkg)
{
	guint i;
	for (i = 0; strv[i] != NULL; i++) {
		if (g_strcmp0 (strv[i], pk_package_get_name (pkg)) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * pk_engine_get_package_history_pkg:
 *
 * Create a 'a{sv}' GVariant instance from all the PkTransactionPast data
 **/
static GVariant *
pk_engine_get_package_history_pkg (PkTransactionPast *item, PkPackage *pkg)
{
	GDateTime *datetime;
	GVariantBuilder builder;
	GVariant *value = NULL;

	/* get the modification date */
	datetime = pk_transaction_past_get_datetime (item);
	if (datetime == NULL)
		goto out;

	/* add to results */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}", "info",
			       g_variant_new_uint32 (pk_package_get_info (pkg)));
	g_variant_builder_add (&builder, "{sv}", "source",
			       g_variant_new_string (pk_package_get_data (pkg)));
	g_variant_builder_add (&builder, "{sv}", "version",
			       g_variant_new_string (pk_package_get_version (pkg)));
	g_variant_builder_add (&builder, "{sv}", "timestamp",
			       g_variant_new_uint64 (g_date_time_to_unix (datetime)));
	g_variant_builder_add (&builder, "{sv}", "user-id",
			       g_variant_new_uint32 (pk_transaction_past_get_uid (item)));
	value = g_variant_builder_end (&builder);
out:
	if (datetime != NULL)
		g_date_time_unref (datetime);
	return value;
}
/**
 * pk_engine_get_package_history:
 **/
static GVariant *
pk_engine_get_package_history (PkEngine *engine,
			       gchar **package_names,
			       guint max_size,
			       GError **error)
{
	const gchar *data;
	const gchar *pkgname;
	gboolean ret;
	gchar **package_lines;
	GHashTable *pkgname_hash;
	GList *keys = NULL;
	GList *l;
	GList *list;
	GPtrArray *array = NULL;
	guint i;
	GVariantBuilder builder;
	GVariant *value = NULL;
	PkPackage *package_tmp;
	PkTransactionPast *item;

	list = pk_transaction_db_get_list (engine->priv->transaction_db, max_size);

	/* simplify the loop */
	if (max_size == 0)
		max_size = G_MAXUINT;

	pkgname_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	package_tmp = pk_package_new ();
	for (l = list; l != NULL; l = l->next) {
		item = PK_TRANSACTION_PAST (l->data);

		/* ignore anything that failed */
		if (!pk_transaction_past_get_succeeded (item))
			continue;

		/* split up data */
		data = pk_transaction_past_get_data (item);
		if (data == NULL)
			continue;
		package_lines = g_strsplit (data, "\n", -1);
		for (i = 0; package_lines[i] != NULL; i++) {
			ret = pk_package_parse (package_tmp, package_lines[i], error);
			g_assert (ret);
			/* not the package we care about */
			if (!pk_engine_package_name_in_strv (package_names, package_tmp))
				continue;
			value = pk_engine_get_package_history_pkg (item, package_tmp);
			if (value == NULL)
				continue;

			/* find the array */
			pkgname = pk_package_get_name (package_tmp);
			array = g_hash_table_lookup (pkgname_hash, pkgname);
			if (array == NULL) {
				array = g_ptr_array_new ();
				g_hash_table_insert (pkgname_hash,
						     g_strdup (pkgname),
						     array);
			}
			g_ptr_array_add (array, value);
		}
		g_strfreev (package_lines);
	}

	/* no history returns an empty array */
	if (g_hash_table_size (pkgname_hash) == 0) {
		value = g_variant_new_array (G_VARIANT_TYPE ("{saa{sv}}"), NULL, 0);
		goto out;
	}

	/* we have a hash of pkgname:GPtrArray where the GPtrArray is an array
	 * of GVariants of type a{sv} */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	keys = g_hash_table_get_keys (pkgname_hash);
	for (l = keys; l != NULL; l = l->next) {
		pkgname = l->data;
		array = g_hash_table_lookup (pkgname_hash, pkgname);
		/* create aa{sv} */
		value = g_variant_new_array (NULL,
					     (GVariant * const *) array->pdata,
					     MIN (array->len, max_size));
		g_variant_builder_add (&builder, "{s@aa{sv}}", pkgname, value);
	}
	value = g_variant_builder_end (&builder);
out:
	g_list_free (keys);
	g_hash_table_unref (pkgname_hash);
	g_object_unref (package_tmp);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);
	return value;
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
	GVariant *tuple = NULL;
	PkAuthorizeEnum result_enum;
	PkEngine *engine = PK_ENGINE (user_data);
	PkRoleEnum role;
	gchar **transaction_list;
	gchar **array = NULL;
	gchar **package_names;
	guint size;
	gboolean is_priority = TRUE;

	g_return_if_fail (PK_IS_ENGINE (engine));

	/* reset the timer */
	pk_engine_reset_timer (engine);

	if (g_strcmp0 (method_name, "GetTimeSinceAction") == 0) {
		g_variant_get (parameters, "(u)", &role);
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

	if (g_strcmp0 (method_name, "GetPackageHistory") == 0) {
		g_variant_get (parameters, "(^a&su)", &package_names, &size);
		if (package_names == NULL || g_strv_length (package_names) == 0) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_NOT_SUPPORTED,
							       "history for package name invalid");
			goto out;
		}
		value = pk_engine_get_package_history (engine, package_names, size, &error);
		if (value == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_NOT_SUPPORTED,
							       "history for package name %s failed: %s",
							       package_names[0],
							       error->message);
			g_error_free (error);
			goto out;
		}
		tuple = g_variant_new_tuple (&value, 1);
		g_dbus_method_invocation_return_value (invocation, tuple);
		goto out;
	}

	if (g_strcmp0 (method_name, "CreateTransaction") == 0) {

		g_debug ("CreateTransaction method called");
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

		g_debug ("sending object path: '%s'", data);
		value = g_variant_new ("(o)", data);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}

	if (g_strcmp0 (method_name, "GetTransactionList") == 0) {
		transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);
		value = g_variant_new ("(^a&o)", transaction_list);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}

	if (g_strcmp0 (method_name, "SuggestDaemonQuit") == 0) {

		/* attempt to kill background tasks */
		pk_transaction_list_cancel_queued (engine->priv->transaction_list);
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
		value = g_variant_new ("(u)", result_enum);
		g_dbus_method_invocation_return_value (invocation, value);
		goto out;
	}
out:
	g_strfreev (array);
	g_free (data);
}

#ifdef PK_BUILD_SYSTEMD
/**
 * pk_engine_proxy_logind_cb:
 **/
static void
pk_engine_proxy_logind_cb (GObject *source_object,
			   GAsyncResult *res,
			   gpointer user_data)
{
	GError *error = NULL;
	PkEngine *engine = PK_ENGINE (user_data);

	engine->priv->logind_proxy = g_dbus_proxy_new_finish (res, &error);
	if (engine->priv->logind_proxy == NULL) {
		g_warning ("failed to connect to logind: %s", error->message);
		g_error_free (error);
	}
}
#endif

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

#ifdef PK_BUILD_SYSTEMD
	/* connect to logind */
	g_dbus_proxy_new (connection,
			  G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
			  NULL,
			  "org.freedesktop.login1",
			  "/org/freedesktop/login1",
			  "org.freedesktop.login1.Manager",
			  NULL, /* GCancellable */
			  pk_engine_proxy_logind_cb,
			  engine);
#endif

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
	gchar *filename;
	gchar *proxy_http;
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

	/* proxy the network state */
	engine->priv->network = pk_network_new ();
	g_signal_connect (engine->priv->network, "state-changed",
			  G_CALLBACK (pk_engine_network_state_changed_cb), engine);
	engine->priv->network_state = pk_network_get_network_state (engine->priv->network);

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

	/* ignore the users proxy setting */
	if (proxy_http != NULL)
		engine->priv->using_hardcoded_proxy = TRUE;

	/* get the StateHasChanged timeouts */
	engine->priv->timeout_priority = (guint) pk_conf_get_int (engine->priv->conf, "StateChangedTimeoutPriority");
	engine->priv->timeout_normal = (guint) pk_conf_get_int (engine->priv->conf, "StateChangedTimeoutNormal");

	engine->priv->transaction_list = pk_transaction_list_new ();
	pk_transaction_list_set_backend (engine->priv->transaction_list,
					 engine->priv->backend);
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

	g_free (proxy_http);
}

/**
 * pk_engine_finalize:
 * @object: The object to finalize
 **/
static void
pk_engine_finalize (GObject *object)
{
	gboolean ret;
	PkEngine *engine;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_ENGINE (object));

	engine = PK_ENGINE (object);

	g_return_if_fail (engine->priv != NULL);

	/* run the plugins */
	pk_engine_plugin_phase (engine,
				PK_PLUGIN_PHASE_DESTROY);

	/* if we set an state changed notifier, clear */
	if (engine->priv->timeout_priority_id != 0) {
		g_source_remove (engine->priv->timeout_priority_id);
		engine->priv->timeout_priority_id = 0;
	}
	if (engine->priv->timeout_normal_id != 0) {
		g_source_remove (engine->priv->timeout_normal_id);
		engine->priv->timeout_normal_id = 0;
	}

	/* unlock if we locked this */
	ret = pk_backend_unload (engine->priv->backend);
	if (!ret)
		g_warning ("couldn't unload the backend");

	/* unown */
	if (engine->priv->owner_id > 0)
		g_bus_unown_name (engine->priv->owner_id);

	if (engine->priv->introspection != NULL)
		g_dbus_node_info_unref (engine->priv->introspection);
	if (engine->priv->connection != NULL)
		g_object_unref (engine->priv->connection);

#ifdef PK_BUILD_SYSTEMD
	/* uninhibit */
	if (engine->priv->logind_fd != 0)
		close (engine->priv->logind_fd);
	if (engine->priv->logind_proxy != NULL)
		g_object_unref (engine->priv->logind_proxy);
#endif

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
	g_strfreev (engine->priv->mime_types);
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

