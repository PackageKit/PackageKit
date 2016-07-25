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
#include <gio/gunixfdlist.h>
#include <packagekit-glib2/pk-offline.h>
#include <packagekit-glib2/pk-offline-private.h>
#include <packagekit-glib2/pk-version.h>
#include <polkit/polkit.h>

#include "pk-backend.h"
#include "pk-dbus.h"
#include "pk-engine.h"
#include "pk-shared.h"
#include "pk-transaction-db.h"
#include "pk-transaction.h"
#include "pk-scheduler.h"

#ifndef glib_autoptr_cleanup_PolkitAuthorizationResult
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#endif

static void     pk_engine_finalize	(GObject       *object);
static void	pk_engine_set_locked (PkEngine *engine, gboolean is_locked);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))

/* how long to wait when we get the StateHasChanged method */
#define PK_ENGINE_STATE_CHANGED_TIMEOUT_PRIORITY	2 /* s */

/* how long to wait after the computer has been resumed or any system event */
#define PK_ENGINE_STATE_CHANGED_TIMEOUT_NORMAL		600 /* s */

struct PkEnginePrivate
{
	GTimer			*timer;
	gboolean		 notify_clients_of_upgrade;
	gboolean		 shutdown_as_soon_as_possible;
	PkScheduler		*scheduler;
	PkTransactionDb		*transaction_db;
	PkBackend		*backend;
	GNetworkMonitor		*network_monitor;
	GKeyFile		*conf;
	PkDbus			*dbus;
	GFileMonitor		*monitor_conf;
	GFileMonitor		*monitor_binary;
	GFileMonitor		*monitor_offline;
	GFileMonitor		*monitor_offline_upgrade;
	PkBitfield		 roles;
	PkBitfield		 groups;
	PkBitfield		 filters;
	gchar			**mime_types;
	const gchar		*backend_name;
	const gchar		*backend_description;
	const gchar		*backend_author;
	gchar			*distro_id;
	guint			 timeout_priority_id;
	guint			 timeout_normal_id;
	PolkitAuthority		*authority;
	gboolean		 locked;
	PkNetworkEnum		 network_state;
	guint			 owner_id;
	GDBusNodeInfo		*introspection;
	GDBusConnection		*connection;
#ifdef HAVE_SYSTEMD
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
G_DEFINE_QUARK (pk-engine-error-quark, pk_engine_error)

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

static void pk_engine_inhibit (PkEngine *engine);
static void pk_engine_uninhibit (PkEngine *engine);

/**
 * pk_engine_set_inhibited:
 **/
static void
pk_engine_set_inhibited (PkEngine *engine, gboolean inhibited)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* inhibit shutdown and suspend */
	if (inhibited)
		pk_engine_inhibit (engine);
	else
		pk_engine_uninhibit (engine);
}

/**
 * pk_engine_scheduler_changed_cb:
 **/
static void
pk_engine_scheduler_changed_cb (PkScheduler *scheduler, PkEngine *engine)
{
	g_auto(GStrv) transaction_list = NULL;

	g_return_if_fail (PK_IS_ENGINE (engine));

	/* automatically locked if the transaction cannot be cancelled */
	pk_engine_set_locked (engine, pk_scheduler_get_locked (scheduler));
	pk_engine_set_inhibited (engine, pk_scheduler_get_inhibited (scheduler));

	transaction_list = pk_scheduler_get_array (scheduler);
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "TransactionListChanged",
				       g_variant_new ("(^a&s)",
						      transaction_list),
				       NULL);
	pk_engine_reset_timer (engine);
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
 * pk_engine_emit_offline_property_changed:
 **/
static void
pk_engine_emit_offline_property_changed (PkEngine *engine,
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
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));

	if (property_value == NULL) {
		g_variant_builder_add (&invalidated_builder,
		                       "s",
		                       property_name);
	} else {
		g_variant_builder_add (&builder,
		                       "{sv}",
		                       property_name,
		                       property_value);
	}
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       g_variant_new ("(sa{sv}as)",
				       PK_DBUS_INTERFACE_OFFLINE,
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
#ifdef HAVE_SYSTEMD
	g_autoptr(GError) error = NULL;
	g_autoptr(GUnixFDList) out_fd_list = NULL;
	g_autoptr(GVariant) res = NULL;

	/* already inhibited */
	if (engine->priv->logind_fd != 0)
		return;

	/* not yet connected */
	if (engine->priv->logind_proxy == NULL) {
		g_warning ("no logind connection to use");
		return;
	}

	/* block shutdown and idle */
	res = g_dbus_proxy_call_with_unix_fd_list_sync (engine->priv->logind_proxy,
							"Inhibit",
							g_variant_new ("(ssss)",
								       "shutdown:idle",
								       "Package Updater",
								       "Package Update in Progress",
								       "block"),
							G_DBUS_CALL_FLAGS_NONE,
							-1,
							NULL, /* fd_list */
							&out_fd_list,
							NULL, /* GCancellable */
							&error);
	if (res == NULL) {
		g_warning ("Failed to Inhibit using logind: %s", error->message);
		return;
	}

	/* keep fd as cookie */
	if (g_unix_fd_list_get_length (out_fd_list) != 1) {
		g_warning ("invalid response from logind");
		return;
	}
	engine->priv->logind_fd = g_unix_fd_list_get (out_fd_list, 0, NULL);
	g_debug ("opened logind fd %i", engine->priv->logind_fd);
#endif
}

/**
 * pk_engine_uninhibit:
 **/
static void
pk_engine_uninhibit (PkEngine *engine)
{
#ifdef HAVE_SYSTEMD
	if (engine->priv->logind_fd == 0)
		return;
	g_debug ("closed logind fd %i", engine->priv->logind_fd);
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

	/* emit */
	pk_engine_emit_property_changed (engine,
					 "Locked",
					 g_variant_new_boolean (is_locked));
}

/**
 * pk_engine_backend_repo_list_changed_cb:
 **/
static void
pk_engine_backend_repo_list_changed_cb (PkBackend *backend, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("emitting RepoListChanged");
	g_dbus_connection_emit_signal (engine->priv->connection,
				       NULL,
				       PK_DBUS_PATH,
				       PK_DBUS_INTERFACE,
				       "RepoListChanged",
				       NULL,
				       NULL);
}

/**
 * pk_engine_backend_updates_changed_cb:
 **/
static void
pk_engine_backend_updates_changed_cb (PkBackend *backend, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("emitting UpdatesChanged");
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
	PkEngine *engine = PK_ENGINE (data);
	g_autoptr(GError) error = NULL;

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* we're done something low-level */
	if (!pk_offline_auth_invalidate (&error))
		g_warning ("failed to invalidate: %s", error->message);

	/* if network is not up, then just reschedule */
	if (!g_network_monitor_get_network_available (engine->priv->network_monitor)) {
		/* wait another timeout of PK_ENGINE_STATE_CHANGED_x_TIMEOUT */
		return TRUE;
	}

	pk_backend_updates_changed (engine->priv->backend);

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

	g_debug ("emitting RestartSchedule");
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
	g_return_val_if_fail (pk_is_thread_default (), 0);

	/* check for transactions running - a transaction that takes a *long* time might not
	 * give sufficient percentage updates to not be marked as idle */
	size = pk_scheduler_get_size (engine->priv->scheduler);
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
			      const gchar *pac,
			      GError **error)
{
	gboolean ret;
	guint uid;
	g_autofree gchar *session = NULL;

	/* get uid */
	uid = pk_dbus_get_uid (engine->priv->dbus, sender);
	if (uid == G_MAXUINT) {
		g_set_error_literal (error,
				     PK_ENGINE_ERROR,
				     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
				     "failed to get the uid");
		return FALSE;
	}

	/* get session */
	session = pk_dbus_get_session (engine->priv->dbus, sender);
	if (session == NULL) {
		g_set_error_literal (error,
				     PK_ENGINE_ERROR,
				     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
				     "failed to get the session");
		return FALSE;
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
		g_set_error_literal (error,
				     PK_ENGINE_ERROR,
				     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
				     "failed to save the proxy in the database");
		return FALSE;
	}
	return TRUE;
}

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

/**
 * pk_engine_action_obtain_authorization:
 **/
static void
pk_engine_action_obtain_proxy_authorization_finished_cb (PolkitAuthority *authority,
							 GAsyncResult *res,
							 PkEngineDbusState *state)
{
	GError *error = NULL;
	gboolean ret;
	PkEnginePrivate *priv = state->engine->priv;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(PolkitAuthorizationResult) result = NULL;

	/* finish the call */
	result = polkit_authority_check_authorization_finish (priv->authority, res, &error_local);

	/* failed */
	if (result == NULL) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_PROXY,
				     "setting the proxy failed, could not check for auth: %s",
				     error_local->message);
		g_dbus_method_invocation_return_gerror (state->context, error);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		error = g_error_new_literal (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "failed to obtain auth");
		g_dbus_method_invocation_return_gerror (state->context, error);
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
					    state->value6,
					    &error_local);
	if (!ret) {
		g_set_error (&error,
			     PK_ENGINE_ERROR,
			     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
			     "setting the proxy failed: %s",
			     error_local->message);
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
	/* unref state, we're done */
	g_object_unref (state->engine);
	g_free (state->sender);
	g_free (state->value1);
	g_free (state->value2);
	g_free (state);
}

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
	g_autofree gchar *session = NULL;
	g_autofree gchar *proxy_http_tmp = NULL;
	g_autofree gchar *proxy_https_tmp = NULL;
	g_autofree gchar *proxy_ftp_tmp = NULL;
	g_autofree gchar *proxy_socks_tmp = NULL;
	g_autofree gchar *no_proxy_tmp = NULL;
	g_autofree gchar *pac_tmp = NULL;

	/* get uid */
	uid = pk_dbus_get_uid (engine->priv->dbus, sender);
	if (uid == G_MAXUINT) {
		g_warning ("failed to get the uid for %s", sender);
		return FALSE;
	}

	/* get session */
	session = pk_dbus_get_session (engine->priv->dbus, sender);
	if (session == NULL) {
		g_warning ("failed to get the session for %s", sender);
		return FALSE;
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
		return FALSE;

	/* are different? */
	if (g_strcmp0 (proxy_http_tmp, proxy_http) != 0 ||
	    g_strcmp0 (proxy_https_tmp, proxy_https) != 0 ||
	    g_strcmp0 (proxy_ftp_tmp, proxy_ftp) != 0 ||
	    g_strcmp0 (proxy_socks_tmp, proxy_socks) != 0 ||
	    g_strcmp0 (no_proxy_tmp, no_proxy) != 0 ||
	    g_strcmp0 (pac_tmp, pac) != 0)
		return FALSE;
	return TRUE;
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
	PkEngineDbusState *state;
	g_autoptr(PolkitSubject) subject = NULL;

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

	/* reset the timer */
	pk_engine_reset_timer (engine);
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
	g_autoptr(PolkitAuthorizationResult) res = NULL;
	g_autoptr(PolkitSubject) subject = NULL;

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
	if (res == NULL)
		return PK_AUTHORIZE_ENUM_UNKNOWN;

	/* already yes */
	if (polkit_authorization_result_get_is_authorized (res))
		return PK_AUTHORIZE_ENUM_YES;

	/* could be yes with user input */
	if (polkit_authorization_result_get_is_challenge (res))
		return PK_AUTHORIZE_ENUM_INTERACTIVE;

	/* fall back to not letting user authenticate */
	return PK_AUTHORIZE_ENUM_NO;
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
 * pk_engine_offline_file_changed_cb:
 **/
static void
pk_engine_offline_file_changed_cb (GFileMonitor *file_monitor,
				   GFile *file, GFile *other_file,
				   GFileMonitorEvent event_type,
				   PkEngine *engine)
{
	gboolean ret;
	g_return_if_fail (PK_IS_ENGINE (engine));

	ret = g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS);
	pk_engine_emit_offline_property_changed (engine,
						 "UpdatePrepared",
						 g_variant_new_boolean (ret));
}

static GVariant *
pk_engine_offline_get_prepared_upgrade_property (GError **error)
{
	GVariantBuilder builder;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;

	if (!pk_offline_get_prepared_upgrade (&name, &version, error))
		return NULL;

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
	if (name != NULL)
		g_variant_builder_add (&builder, "{sv}",
		                       "name",
		                       g_variant_new ("s", name));
	if (version != NULL)
		g_variant_builder_add (&builder, "{sv}",
		                       "version",
		                       g_variant_new ("s", version));
	return g_variant_builder_end (&builder);
}

/**
 * pk_engine_offline_upgrade_file_changed_cb:
 **/
static void
pk_engine_offline_upgrade_file_changed_cb (GFileMonitor *file_monitor,
                                           GFile *file, GFile *other_file,
                                           GFileMonitorEvent event_type,
                                           PkEngine *engine)
{
	GVariant *prepared_upgrade;
	gboolean ret;
	g_return_if_fail (PK_IS_ENGINE (engine));

	ret = g_file_test (PK_OFFLINE_PREPARED_UPGRADE_FILENAME, G_FILE_TEST_EXISTS);
	pk_engine_emit_offline_property_changed (engine,
						 "UpgradePrepared",
						 g_variant_new_boolean (ret));

	prepared_upgrade = pk_engine_offline_get_prepared_upgrade_property (NULL);
	pk_engine_emit_offline_property_changed (engine,
						 "PreparedUpgrade",
						 prepared_upgrade);

}

/**
 * pk_engine_get_network_state:
 **/
static PkNetworkEnum
pk_engine_get_network_state (GNetworkMonitor *network_monitor)
{
	if (!g_network_monitor_get_network_available (network_monitor))
		return PK_NETWORK_ENUM_OFFLINE;
	/* this isn't exactly true, but it's what the UI expects */
	if (g_network_monitor_get_network_metered (network_monitor))
		return PK_NETWORK_ENUM_MOBILE;
	return PK_NETWORK_ENUM_ONLINE;
}

/**
 * pk_engine_network_state_changed_cb:
 **/
static void
pk_engine_network_state_changed_cb (GNetworkMonitor *network_monitor,
				    gboolean available,
				    PkEngine *engine)
{
	PkNetworkEnum network_state;
	g_return_if_fail (PK_IS_ENGINE (engine));

	network_state = pk_engine_get_network_state (network_monitor);
	if (network_state == engine->priv->network_state)
		return;
	engine->priv->network_state = network_state;

	/* emit */
	pk_engine_emit_property_changed (engine,
					 "NetworkState",
					 g_variant_new_uint32 (network_state));
}

/**
 * pk_engine_setup_file_monitors:
 **/
static void
pk_engine_setup_file_monitors (PkEngine *engine)
{
	const gchar *filename = "/etc/PackageKit/PackageKit.conf";
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_binary = NULL;
	g_autoptr(GFile) file_conf = NULL;

	/* monitor the binary file for changes */
	file_binary = g_file_new_for_path (LIBEXECDIR "/packagekitd");
	engine->priv->monitor_binary = g_file_monitor_file (file_binary,
							    G_FILE_MONITOR_NONE,
							    NULL,
							    &error);
	if (engine->priv->monitor_binary == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   LIBEXECDIR "/packagekitd", error->message);
		return;
	}
	g_signal_connect (engine->priv->monitor_binary, "changed",
			  G_CALLBACK (pk_engine_binary_file_changed_cb), engine);

	/* monitor config file for changes */
	g_debug ("setting config file watch on %s", filename);
	file_conf = g_file_new_for_path (filename);
	engine->priv->monitor_conf = g_file_monitor_file (file_conf,
							  G_FILE_MONITOR_NONE,
							  NULL,
							  &error);
	if (engine->priv->monitor_conf == NULL) {
		g_warning ("Failed to set watch on %s: %s", filename, error->message);
		return;
	}
	g_signal_connect (engine->priv->monitor_conf, "changed",
			  G_CALLBACK (pk_engine_conf_file_changed_cb), engine);

	/* set up the prepared update monitor */
	engine->priv->monitor_offline = pk_offline_get_prepared_monitor (NULL, &error);
	if (engine->priv->monitor_offline == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   PK_OFFLINE_PREPARED_FILENAME, error->message);
		return;
	}
	g_signal_connect (engine->priv->monitor_offline, "changed",
			  G_CALLBACK (pk_engine_offline_file_changed_cb), engine);

	/* set up the prepared system upgrade monitor */
	engine->priv->monitor_offline_upgrade = pk_offline_get_prepared_upgrade_monitor (NULL, &error);
	if (engine->priv->monitor_offline_upgrade == NULL) {
		g_warning ("Failed to set watch on %s: %s",
			   PK_OFFLINE_PREPARED_UPGRADE_FILENAME, error->message);
		return;
	}
	g_signal_connect (engine->priv->monitor_offline_upgrade, "changed",
			  G_CALLBACK (pk_engine_offline_upgrade_file_changed_cb), engine);
}

/**
 * pk_engine_load_backend:
 **/
gboolean
pk_engine_load_backend (PkEngine *engine, GError **error)
{
	/* load any backend init */
	if (!pk_backend_load (engine->priv->backend, error))
		return FALSE;

	/* load anything that can fail */
	engine->priv->authority = polkit_authority_get_sync (NULL, error);
	if (engine->priv->authority == NULL)
		return FALSE;
	if (!pk_transaction_db_load (engine->priv->transaction_db, error))
		return FALSE;

	/* create a new backend so we can get the static stuff */
	engine->priv->roles = pk_backend_get_roles (engine->priv->backend);
	engine->priv->groups = pk_backend_get_groups (engine->priv->backend);
	engine->priv->filters = pk_backend_get_filters (engine->priv->backend);
	engine->priv->mime_types = pk_backend_get_mime_types (engine->priv->backend);
	engine->priv->backend_name = pk_backend_get_name (engine->priv->backend);
	engine->priv->backend_description = pk_backend_get_description (engine->priv->backend);
	engine->priv->backend_author = pk_backend_get_author (engine->priv->backend);
	return TRUE;
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
 * pk_engine_offline_get_property:
 **/
static GVariant *
pk_engine_offline_get_property (GDBusConnection *connection_, const gchar *sender,
				const gchar *object_path, const gchar *interface_name,
				const gchar *property_name, GError **error,
				gpointer user_data)
{
	PkEngine *engine = PK_ENGINE (user_data);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	if (g_strcmp0 (property_name, "TriggerAction") == 0) {
		PkOfflineAction action = pk_offline_get_action (NULL);
		return g_variant_new_string (pk_offline_action_to_string (action));
	}

	/* stat the file */
	if (g_strcmp0 (property_name, "UpdatePrepared") == 0) {
		gboolean ret;
		ret = g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS);
		return g_variant_new_boolean (ret);
	}

	/* stat the file */
	if (g_strcmp0 (property_name, "UpgradePrepared") == 0) {
		gboolean ret;
		ret = g_file_test (PK_OFFLINE_PREPARED_UPGRADE_FILENAME, G_FILE_TEST_EXISTS);
		return g_variant_new_boolean (ret);
	}

	/* look at the symlink target */
	if (g_strcmp0 (property_name, "UpdateTriggered") == 0) {
		g_autofree gchar *link = NULL;
		link = g_file_read_link (PK_OFFLINE_TRIGGER_FILENAME, NULL);
		return g_variant_new_boolean (g_strcmp0 (link, PK_OFFLINE_PREPARED_FILENAME) == 0);
	}

	/* look at the symlink target */
	if (g_strcmp0 (property_name, "UpgradeTriggered") == 0) {
		g_autofree gchar *link = NULL;
		link = g_file_read_link (PK_OFFLINE_TRIGGER_FILENAME, NULL);
		return g_variant_new_boolean (g_strcmp0 (link, PK_OFFLINE_PREPARED_UPGRADE_FILENAME) == 0);
	}

	if (g_strcmp0 (property_name, "PreparedUpgrade") == 0) {
		return pk_engine_offline_get_prepared_upgrade_property (error);
	}

	/* return an error */
	g_set_error (error,
		     PK_ENGINE_ERROR,
		     PK_ENGINE_ERROR_NOT_SUPPORTED,
		     "failed to get property '%s'",
		     property_name);
	return NULL;
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
	PkEngine *engine = PK_ENGINE (user_data);

	g_return_val_if_fail (pk_is_thread_default (), NULL);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	if (g_strcmp0 (property_name, "VersionMajor") == 0)
		return g_variant_new_uint32 (PK_MAJOR_VERSION);
	if (g_strcmp0 (property_name, "VersionMinor") == 0)
		return g_variant_new_uint32 (PK_MINOR_VERSION);
	if (g_strcmp0 (property_name, "VersionMicro") == 0)
		return g_variant_new_uint32 (PK_MICRO_VERSION);
	if (g_strcmp0 (property_name, "BackendName") == 0)
		return g_variant_new_string (engine->priv->backend_name);
	if (g_strcmp0 (property_name, "BackendDescription") == 0)
		return _g_variant_new_maybe_string (engine->priv->backend_description);
	if (g_strcmp0 (property_name, "BackendAuthor") == 0)
		return _g_variant_new_maybe_string (engine->priv->backend_author);
	if (g_strcmp0 (property_name, "Roles") == 0)
		return g_variant_new_uint64 (engine->priv->roles);
	if (g_strcmp0 (property_name, "Groups") == 0)
		return g_variant_new_uint64 (engine->priv->groups);
	if (g_strcmp0 (property_name, "Filters") == 0)
		return g_variant_new_uint64 (engine->priv->filters);
	if (g_strcmp0 (property_name, "MimeTypes") == 0)
		return g_variant_new_strv ((const gchar * const *) engine->priv->mime_types, -1);
	if (g_strcmp0 (property_name, "Locked") == 0)
		return g_variant_new_boolean (engine->priv->locked);
	if (g_strcmp0 (property_name, "NetworkState") == 0)
		return g_variant_new_uint32 (engine->priv->network_state);
	if (g_strcmp0 (property_name, "DistroId") == 0)
		return _g_variant_new_maybe_string (engine->priv->distro_id);

	/* return an error */
	g_set_error (error,
		     PK_ENGINE_ERROR,
		     PK_ENGINE_ERROR_NOT_SUPPORTED,
		     "failed to get property '%s'",
		     property_name);
	return NULL;
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
	GVariantBuilder builder;
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add (&builder, "{sv}", "info",
			       g_variant_new_uint32 (pk_package_get_info (pkg)));
	g_variant_builder_add (&builder, "{sv}", "source",
			       g_variant_new_string (pk_package_get_data (pkg)));
	g_variant_builder_add (&builder, "{sv}", "version",
			       g_variant_new_string (pk_package_get_version (pkg)));
	g_variant_builder_add (&builder, "{sv}", "timestamp",
			       g_variant_new_uint64 (pk_transaction_past_get_timestamp (item)));
	g_variant_builder_add (&builder, "{sv}", "user-id",
			       g_variant_new_uint32 (pk_transaction_past_get_uid (item)));
	return g_variant_builder_end (&builder);
}

/**
 * pk_engine_is_package_history_interesing:
 **/
static gboolean
pk_engine_is_package_history_interesing (PkPackage *package)
{
	gboolean ret;

	switch (pk_package_get_info (package)) {
	case PK_INFO_ENUM_INSTALLING:
	case PK_INFO_ENUM_REMOVING:
	case PK_INFO_ENUM_UPDATING:
		ret = TRUE;
		break;
	default:
		ret = FALSE;
		break;
	}
	return ret;
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
	gchar *key;
	gint64 timestamp;
	GList *l;
	GList *list;
	GPtrArray *array = NULL;
	guint i;
	GVariantBuilder builder;
	GVariant *value = NULL;
	PkTransactionPast *item;
	g_autoptr(GHashTable) deduplicate_hash = NULL;
	g_autoptr(GHashTable) pkgname_hash = NULL;
	g_autoptr(GList) keys = NULL;
	g_autoptr(PkPackage) package_tmp = NULL;

	list = pk_transaction_db_get_list (engine->priv->transaction_db, max_size);

	/* simplify the loop */
	if (max_size == 0)
		max_size = G_MAXUINT;

	pkgname_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	deduplicate_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	package_tmp = pk_package_new ();
	for (l = list; l != NULL; l = l->next) {
		g_auto(GStrv) package_lines = NULL;
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
			g_autoptr(GError) error_local = NULL;
			ret = pk_package_parse (package_tmp,
						package_lines[i],
						&error_local);
			if (!ret) {
				g_warning ("Failed to parse package: '%s': %s",
					   package_lines[i], error_local->message);
				continue;
			}

			/* not the package we care about */
			if (!pk_engine_package_name_in_strv (package_names, package_tmp))
				continue;

			/* not a state we care about */
			if (!pk_engine_is_package_history_interesing (package_tmp))
				continue;

			/* transactions without a timestamp are not interesting */
			timestamp = pk_transaction_past_get_timestamp (item);
			if (timestamp == 0)
				continue;

			/* de-duplicate the entry, in the case of multiarch */
			key = g_strdup_printf ("%s-%" G_GINT64_FORMAT,
					       pk_package_get_name (package_tmp),
					       timestamp);
			if (g_hash_table_lookup (deduplicate_hash, key) != NULL) {
				g_free (key);
				continue;
			}
			g_hash_table_insert (deduplicate_hash, key, package_lines[i]);

			/* get the blob for this data item */
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
	const gchar *tmp = NULL;
	gboolean ret;
	guint time_since;
	GVariant *value = NULL;
	GVariant *tuple = NULL;
	PkAuthorizeEnum result_enum;
	PkEngine *engine = PK_ENGINE (user_data);
	PkRoleEnum role;
	gchar **transaction_list;
	gchar **package_names;
	guint size;
	gboolean is_priority = TRUE;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *data = NULL;
	g_auto(GStrv) array = NULL;

	g_return_if_fail (PK_IS_ENGINE (engine));
	g_return_if_fail (pk_is_thread_default ());

	/* reset the timer */
	pk_engine_reset_timer (engine);

	if (g_strcmp0 (method_name, "GetTimeSinceAction") == 0) {
		g_variant_get (parameters, "(u)", &role);
		time_since = pk_transaction_db_action_time_since (engine->priv->transaction_db,
								  role);
		value = g_variant_new ("(u)", time_since);
		g_dbus_method_invocation_return_value (invocation, value);
		return;
	}

	if (g_strcmp0 (method_name, "GetDaemonState") == 0) {
		data = pk_scheduler_get_state (engine->priv->scheduler);
		value = g_variant_new ("(s)", data);
		g_dbus_method_invocation_return_value (invocation, value);
		return;
	}

	if (g_strcmp0 (method_name, "GetPackageHistory") == 0) {
		g_variant_get (parameters, "(^a&su)", &package_names, &size);
		if (package_names == NULL || g_strv_length (package_names) == 0) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_NOT_SUPPORTED,
							       "history for package name invalid");
			return;
		}
		value = pk_engine_get_package_history (engine, package_names, size, &error);
		if (value == NULL) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_NOT_SUPPORTED,
							       "history for package name %s failed: %s",
							       package_names[0],
							       error->message);
			return;
		}
		tuple = g_variant_new_tuple (&value, 1);
		g_dbus_method_invocation_return_value (invocation, tuple);
		return;
	}

	if (g_strcmp0 (method_name, "CreateTransaction") == 0) {

		g_debug ("CreateTransaction method called");
		data = pk_transaction_db_generate_id (engine->priv->transaction_db);
		g_assert (data != NULL);
		ret = pk_scheduler_create (engine->priv->scheduler,
					   data, sender, &error);
		if (!ret) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_CANNOT_CHECK_AUTH,
							       "could not create transaction %s: %s",
							       data,
							       error->message);
			return;
		}

		g_debug ("sending object path: '%s'", data);
		value = g_variant_new ("(o)", data);
		g_dbus_method_invocation_return_value (invocation, value);
		return;
	}

	if (g_strcmp0 (method_name, "GetTransactionList") == 0) {
		transaction_list = pk_scheduler_get_array (engine->priv->scheduler);
		value = g_variant_new ("(^a&o)", transaction_list);
		g_free (transaction_list);
		g_dbus_method_invocation_return_value (invocation, value);
		return;
	}

	if (g_strcmp0 (method_name, "SuggestDaemonQuit") == 0) {

		/* attempt to kill background tasks */
		pk_scheduler_cancel_queued (engine->priv->scheduler);
		pk_scheduler_cancel_background (engine->priv->scheduler);

		/* can we exit straight away */
		size = pk_scheduler_get_size (engine->priv->scheduler);
		if (size == 0) {
			g_debug ("emitting quit");
			g_signal_emit (engine, signals[SIGNAL_QUIT], 0);
			g_dbus_method_invocation_return_value (invocation, NULL);
			return;
		}

		/* This will wait from 0..10 seconds, depending on the status of
		 * pk_main_timeout_check_cb() - usually it should be a few seconds
		 * after the last transaction */
		engine->priv->shutdown_as_soon_as_possible = TRUE;
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}

	if (g_strcmp0 (method_name, "StateHasChanged") == 0) {

		/* have we already scheduled priority? */
		if (engine->priv->timeout_priority_id != 0) {
			g_debug ("Already asked to refresh priority state less than %i seconds ago",
				 PK_ENGINE_STATE_CHANGED_TIMEOUT_PRIORITY);
			g_dbus_method_invocation_return_value (invocation, NULL);
			return;
		}

		/* don't bombard the user 10 seconds after resuming */
		g_variant_get (parameters, "(&s)", &tmp);
		if (g_strcmp0 (tmp, "resume") == 0)
			is_priority = FALSE;

		/* are we normal, and already scheduled normal? */
		if (!is_priority && engine->priv->timeout_normal_id != 0) {
			g_debug ("Already asked to refresh normal state less than %i seconds ago",
				 PK_ENGINE_STATE_CHANGED_TIMEOUT_NORMAL);
			g_dbus_method_invocation_return_value (invocation, NULL);
			return;
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
				g_timeout_add_seconds (PK_ENGINE_STATE_CHANGED_TIMEOUT_PRIORITY,
						       pk_engine_state_changed_cb, engine);
			g_source_set_name_by_id (engine->priv->timeout_priority_id,
						 "[PkEngine] priority");
		} else {
			engine->priv->timeout_normal_id =
				g_timeout_add_seconds (PK_ENGINE_STATE_CHANGED_TIMEOUT_NORMAL,
						       pk_engine_state_changed_cb, engine);
			g_source_set_name_by_id (engine->priv->timeout_normal_id, "[PkEngine] normal");
		}
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
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
		return;
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
			return;
		}

		/* all okay */
		value = g_variant_new ("(u)", result_enum);
		g_dbus_method_invocation_return_value (invocation, value);
		return;
	}
}

typedef enum {
	PK_ENGINE_OFFLINE_ROLE_CANCEL,
	PK_ENGINE_OFFLINE_ROLE_CLEAR_RESULTS,
	PK_ENGINE_OFFLINE_ROLE_TRIGGER,
	PK_ENGINE_OFFLINE_ROLE_TRIGGER_UPGRADE,
	PK_ENGINE_OFFLINE_ROLE_LAST
} PkEngineOfflineRole;

typedef struct {
	GDBusMethodInvocation	*invocation;
	PkEngine		*engine;
	PkEngineOfflineRole	 role;
	PkOfflineAction		 action;
} PkEngineOfflineAsyncHelper;

/**
 * pk_engine_offline_helper_free:
 **/
static void
pk_engine_offline_helper_free (PkEngineOfflineAsyncHelper *helper)
{
	g_object_unref (helper->engine);
	g_object_unref (helper->invocation);
	g_free (helper);
}

/**
 * pk_engine_offline_helper_cb:
 **/
static void
pk_engine_offline_helper_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	PkEngineOfflineAsyncHelper *helper = (PkEngineOfflineAsyncHelper *) user_data;
	PkOfflineAction action;
	GVariant *prepared_upgrade;
	gboolean ret;
	g_autofree gchar *link = NULL;
	g_autofree gchar *name = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(PolkitAuthorizationResult) result = NULL;

	/* finish the call */
	result = polkit_authority_check_authorization_finish (POLKIT_AUTHORITY (source), res, &error);
	if (result == NULL) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       PK_ENGINE_ERROR,
						       PK_ENGINE_ERROR_DENIED,
						       "could not check for auth: %s",
						       error->message);
		pk_engine_offline_helper_free (helper);
		return;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       PK_ENGINE_ERROR,
						       PK_ENGINE_ERROR_DENIED,
						       "failed to obtain auth");
		pk_engine_offline_helper_free (helper);
		return;
	}

	switch (helper->role) {
	case PK_ENGINE_OFFLINE_ROLE_CLEAR_RESULTS:
		ret = pk_offline_auth_clear_results (&error);
		break;
	case PK_ENGINE_OFFLINE_ROLE_CANCEL:
		ret = pk_offline_auth_cancel (&error);
		break;
	case PK_ENGINE_OFFLINE_ROLE_TRIGGER:
		ret = pk_offline_auth_trigger (helper->action, &error);
		break;
	case PK_ENGINE_OFFLINE_ROLE_TRIGGER_UPGRADE:
		ret = pk_offline_auth_trigger_upgrade (helper->action, &error);
		break;
	default:
		g_assert_not_reached ();
	}
	if (!ret) {
		g_dbus_method_invocation_return_error (helper->invocation,
						       PK_ENGINE_ERROR,
						       PK_ENGINE_ERROR_INVALID_STATE,
						       "%s", error->message);
		pk_engine_offline_helper_free (helper);
		return;
	}

	/* refresh cached dbus properties */
	action = pk_offline_get_action (NULL);
	pk_engine_emit_offline_property_changed (helper->engine,
						 "TriggerAction",
						 g_variant_new_string (pk_offline_action_to_string (action)));

	link = g_file_read_link (PK_OFFLINE_TRIGGER_FILENAME, NULL);
	pk_engine_emit_offline_property_changed (helper->engine,
						 "UpdateTriggered",
						 g_variant_new_boolean (g_strcmp0 (link, PK_OFFLINE_PREPARED_FILENAME) == 0));
	pk_engine_emit_offline_property_changed (helper->engine,
						 "UpgradeTriggered",
						 g_variant_new_boolean (g_strcmp0 (link, PK_OFFLINE_PREPARED_UPGRADE_FILENAME) == 0));

	prepared_upgrade = pk_engine_offline_get_prepared_upgrade_property (NULL);
	pk_engine_emit_offline_property_changed (helper->engine,
						 "PreparedUpgrade",
						 prepared_upgrade);

	g_dbus_method_invocation_return_value (helper->invocation, NULL);
	pk_engine_offline_helper_free (helper);
}

/**
 * pk_engine_offline_method_call:
 **/
static void
pk_engine_offline_method_call (GDBusConnection *connection_, const gchar *sender,
			       const gchar *object_path, const gchar *interface_name,
			       const gchar *method_name, GVariant *parameters,
			       GDBusMethodInvocation *invocation, gpointer user_data)
{
	PkEngine *engine = PK_ENGINE (user_data);
	PkEngineOfflineAsyncHelper *helper;
	g_autoptr(GError) error = NULL;
	g_autoptr(PolkitSubject) subject = NULL;

	g_return_if_fail (PK_IS_ENGINE (engine));

	/* reset the timer */
	pk_engine_reset_timer (engine);

	/* set up polkit */
	subject = polkit_system_bus_name_new (sender);

	if (g_strcmp0 (method_name, "Cancel") == 0) {
		helper = g_new0 (PkEngineOfflineAsyncHelper, 1);
		helper->engine = g_object_ref (engine);
		helper->role = PK_ENGINE_OFFLINE_ROLE_CANCEL;
		helper->invocation = g_object_ref (invocation);
		polkit_authority_check_authorization (engine->priv->authority, subject,
						      "org.freedesktop.packagekit.trigger-offline-update",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      pk_engine_offline_helper_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "ClearResults") == 0) {
		helper = g_new0 (PkEngineOfflineAsyncHelper, 1);
		helper->engine = g_object_ref (engine);
		helper->role = PK_ENGINE_OFFLINE_ROLE_CLEAR_RESULTS;
		helper->invocation = g_object_ref (invocation);
		polkit_authority_check_authorization (engine->priv->authority, subject,
						      "org.freedesktop.packagekit.clear-offline-update",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      pk_engine_offline_helper_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "Trigger") == 0) {
		const gchar *tmp;
		PkOfflineAction action;
		g_variant_get (parameters, "(&s)", &tmp);
		action = pk_offline_action_from_string (tmp);
		if (action == PK_OFFLINE_ACTION_UNKNOWN) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_NOT_SUPPORTED,
							       "action %s unsupported",
							       tmp);
			return;
		}
		helper = g_new0 (PkEngineOfflineAsyncHelper, 1);
		helper->engine = g_object_ref (engine);
		helper->role = PK_ENGINE_OFFLINE_ROLE_TRIGGER;
		helper->invocation = g_object_ref (invocation);
		helper->action = action;
		polkit_authority_check_authorization (engine->priv->authority, subject,
						      "org.freedesktop.packagekit.trigger-offline-update",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      pk_engine_offline_helper_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "TriggerUpgrade") == 0) {
		const gchar *tmp;
		PkOfflineAction action;
		g_variant_get (parameters, "(&s)", &tmp);
		action = pk_offline_action_from_string (tmp);
		if (action == PK_OFFLINE_ACTION_UNKNOWN) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_NOT_SUPPORTED,
							       "action %s unsupported",
							       tmp);
			return;
		}
		helper = g_new0 (PkEngineOfflineAsyncHelper, 1);
		helper->engine = g_object_ref (engine);
		helper->role = PK_ENGINE_OFFLINE_ROLE_TRIGGER_UPGRADE;
		helper->invocation = g_object_ref (invocation);
		helper->action = action;
		polkit_authority_check_authorization (engine->priv->authority, subject,
						      "org.freedesktop.packagekit.trigger-offline-upgrade",
						      NULL,
						      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
						      NULL,
						      pk_engine_offline_helper_cb,
						      helper);
		return;
	}
	if (g_strcmp0 (method_name, "GetPrepared") == 0) {
		g_auto(GStrv) package_ids = NULL;
		GVariant *value = NULL;

		package_ids = pk_offline_get_prepared_ids (&error);
		if (package_ids == NULL && error->code != PK_OFFLINE_ERROR_NO_DATA) {
			g_dbus_method_invocation_return_error (invocation,
							       PK_ENGINE_ERROR,
							       PK_ENGINE_ERROR_INVALID_STATE,
							       "%s", error->message);
			return;
		}

		if (package_ids != NULL) {
			value = g_variant_new ("(^as)", package_ids);
		} else {
			value = g_variant_new ("(as)", NULL);
		}
		g_dbus_method_invocation_return_value (invocation, value);
		return;
	}
}

#ifdef HAVE_SYSTEMD
/**
 * pk_engine_proxy_logind_cb:
 **/
static void
pk_engine_proxy_logind_cb (GObject *source_object,
			   GAsyncResult *res,
			   gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	PkEngine *engine = PK_ENGINE (user_data);

	engine->priv->logind_proxy = g_dbus_proxy_new_finish (res, &error);
	if (engine->priv->logind_proxy == NULL)
		g_warning ("failed to connect to logind: %s", error->message);
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
	static const GDBusInterfaceVTable iface_daemon_vtable = {
		pk_engine_daemon_method_call,
		pk_engine_daemon_get_property,
		NULL
	};
	static const GDBusInterfaceVTable iface_offline_vtable = {
		pk_engine_offline_method_call,
		pk_engine_offline_get_property,
		NULL
	};

	/* save copy for emitting signals */
	engine->priv->connection = g_object_ref (connection);

#ifdef HAVE_SYSTEMD
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
							     &iface_daemon_vtable,
							     engine,  /* user_data */
							     NULL,  /* user_data_free_func */
							     NULL); /* GError** */
	g_assert (registration_id > 0);
	registration_id = g_dbus_connection_register_object (connection,
							     PK_DBUS_PATH,
							     engine->priv->introspection->interfaces[1],
							     &iface_offline_vtable,
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
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = NULL;

	engine->priv = PK_ENGINE_GET_PRIVATE (engine);

	/* load introspection */
	engine->priv->introspection = pk_load_introspection (PK_DBUS_INTERFACE ".xml",
							     &error);
	if (engine->priv->introspection == NULL) {
		g_error ("PkEngine: failed to load daemon introspection: %s",
			 error->message);
	}

	/* clear the download cache */
	filename = g_build_filename (LOCALSTATEDIR, "cache", "PackageKit", "downloads", NULL);
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_debug ("clearing download cache at %s", filename);
		pk_directory_remove_contents (filename);
	}

	/* proxy the network state */
	engine->priv->network_monitor = g_network_monitor_get_default ();
	g_signal_connect (engine->priv->network_monitor, "network-changed",
			  G_CALLBACK (pk_engine_network_state_changed_cb), engine);
	engine->priv->network_state = pk_engine_get_network_state (engine->priv->network_monitor);

	/* try to get the distro id */
	engine->priv->distro_id = pk_get_distro_id ();

	engine->priv->timer = g_timer_new ();

	/* we need the uid and the session for the proxy setting mechanism */
	engine->priv->dbus = pk_dbus_new ();

	/* we need to be able to clear this */
	engine->priv->timeout_priority_id = 0;
	engine->priv->timeout_normal_id = 0;

	/* setup file watches */
	pk_engine_setup_file_monitors (engine);

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
}

/**
 * pk_engine_finalize:
 * @object: The object to finalize
 **/
static void
pk_engine_finalize (GObject *object)
{
	PkEngine *engine;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_ENGINE (object));

	engine = PK_ENGINE (object);

	g_return_if_fail (engine->priv != NULL);

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
	if (!pk_backend_unload (engine->priv->backend))
		g_warning ("couldn't unload the backend");

	/* unown */
	if (engine->priv->owner_id > 0)
		g_bus_unown_name (engine->priv->owner_id);

	if (engine->priv->introspection != NULL)
		g_dbus_node_info_unref (engine->priv->introspection);
	if (engine->priv->connection != NULL)
		g_object_unref (engine->priv->connection);

#ifdef HAVE_SYSTEMD
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
	g_object_unref (engine->priv->monitor_offline);
	g_object_unref (engine->priv->monitor_offline_upgrade);
	g_object_unref (engine->priv->scheduler);
	g_object_unref (engine->priv->transaction_db);
	if (engine->priv->authority != NULL)
		g_object_unref (engine->priv->authority);
	g_object_unref (engine->priv->backend);
	g_key_file_unref (engine->priv->conf);
	g_object_unref (engine->priv->dbus);
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
pk_engine_new (GKeyFile *conf)
{
	PkEngine *engine;
	engine = g_object_new (PK_TYPE_ENGINE, NULL);
	engine->priv->conf = g_key_file_ref (conf);
	engine->priv->backend = pk_backend_new (engine->priv->conf);
	g_signal_connect (engine->priv->backend, "repo-list-changed",
			  G_CALLBACK (pk_engine_backend_repo_list_changed_cb), engine);
	g_signal_connect (engine->priv->backend, "updates-changed",
			  G_CALLBACK (pk_engine_backend_updates_changed_cb), engine);
	engine->priv->scheduler = pk_scheduler_new (engine->priv->conf);
	pk_scheduler_set_backend (engine->priv->scheduler,
				  engine->priv->backend);
	g_signal_connect (engine->priv->scheduler, "changed",
			  G_CALLBACK (pk_engine_scheduler_changed_cb), engine);
	return PK_ENGINE (engine);
}

