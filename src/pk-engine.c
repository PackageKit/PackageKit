/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <packagekit-glib2/pk-version.h>
#ifdef USE_SECURITY_POLKIT
#include <polkit/polkit.h>
#endif

#include "egg-string.h"

#include "pk-network.h"
#include "pk-cache.h"
#include "pk-shared.h"
#include "pk-backend.h"
#include "pk-engine.h"
#include "pk-transaction.h"
#include "pk-transaction-db.h"
#include "pk-transaction-list.h"
#include "pk-inhibit.h"
#include "pk-marshal.h"
#include "pk-notify.h"
#include "pk-file-monitor.h"
#include "pk-conf.h"
#include "pk-dbus.h"

static void     pk_engine_finalize	(GObject       *object);

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
	PkInhibit		*inhibit;
	PkNetwork		*network;
	PkNotify		*notify;
	PkConf			*conf;
	PkDbus			*dbus;
	PkFileMonitor		*file_monitor_conf;
	PkFileMonitor		*file_monitor_binary;
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
};

enum {
	SIGNAL_TRANSACTION_LIST_CHANGED,
	SIGNAL_REPO_LIST_CHANGED,
	SIGNAL_RESTART_SCHEDULE,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_CHANGED,
	SIGNAL_QUIT,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_VERSION_MAJOR,
	PROP_VERSION_MINOR,
	PROP_VERSION_MICRO,
	PROP_BACKEND_NAME,
	PROP_BACKEND_DESCRIPTION,
	PROP_BACKEND_AUTHOR,
	PROP_ROLES,
	PROP_GROUPS,
	PROP_FILTERS,
	PROP_MIME_TYPES,
	PROP_LOCKED,
	PROP_NETWORK_STATE,
	PROP_DISTRO_ID,
	PROP_LAST,
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
	g_debug ("reset timer");
	g_timer_reset (engine->priv->timer);
}

/**
 * pk_engine_transaction_list_changed_cb:
 **/
static void
pk_engine_transaction_list_changed_cb (PkTransactionList *tlist, PkEngine *engine)
{
	gchar **transaction_list;

	g_return_if_fail (PK_IS_ENGINE (engine));

	transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);

	g_debug ("emitting transaction-list-changed");
	g_signal_emit (engine, signals[SIGNAL_TRANSACTION_LIST_CHANGED], 0, transaction_list);
	pk_engine_reset_timer (engine);

	g_strfreev (transaction_list);
}

/**
 * pk_engine_inhibit_locked_cb:
 **/
static void
pk_engine_inhibit_locked_cb (PkInhibit *inhibit, gboolean is_locked, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* already set */
	if (engine->priv->locked == is_locked)
		return;

	engine->priv->locked = is_locked;

	/* emit */
	g_debug ("emitting changed");
	g_signal_emit (engine, signals[SIGNAL_CHANGED], 0);
}

/**
 * pk_engine_notify_repo_list_changed_cb:
 **/
static void
pk_engine_notify_repo_list_changed_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	g_debug ("emitting repo-list-changed");
	g_signal_emit (engine, signals[SIGNAL_REPO_LIST_CHANGED], 0);
}

/**
 * pk_engine_notify_updates_changed_cb:
 **/
static void
pk_engine_notify_updates_changed_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	g_debug ("emitting updates-changed");
	g_signal_emit (engine, signals[SIGNAL_UPDATES_CHANGED], 0);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkBackend *backend, PkExitEnum exit_enum, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* daemon is busy */
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_get_tid:
 **/
void
pk_engine_get_tid (PkEngine *engine, DBusGMethodInvocation *context)
{
	gchar *new_tid;
	gboolean ret;
	gchar *sender = NULL;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_ENGINE (engine));

	g_debug ("GetTid method called");
	sender = dbus_g_method_get_sender (context);
	new_tid = pk_transaction_db_generate_id (engine->priv->transaction_db);

	ret = pk_transaction_list_create (engine->priv->transaction_list, new_tid, sender, &error_local);
	if (!ret) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_ALLOCATE_TID, "could not create transaction: %s", error_local->message);
		dbus_g_method_return_error (context, error);
		g_error_free (error_local);
		goto out;
	}

	g_debug ("sending tid: '%s'", new_tid);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	/* return TID */
	dbus_g_method_return (context, new_tid);
out:
	g_free (new_tid);
	g_free (sender);
}

/**
 * pk_engine_get_daemon_state:
 **/
gboolean
pk_engine_get_daemon_state (PkEngine *engine, gchar **state, GError **error)
{
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	if (state != NULL)
		*state = pk_transaction_list_get_state (engine->priv->transaction_list);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	return TRUE;
}

/**
 * pk_engine_get_transaction_list:
 **/
gboolean
pk_engine_get_transaction_list (PkEngine *engine, gchar ***transaction_list, GError **error)
{
	g_return_val_if_fail (engine != NULL, FALSE);
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	g_debug ("GetTransactionList method called");
	if (transaction_list != NULL)
		*transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	return TRUE;
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
 * pk_engine_state_has_changed:
 *
 * This should be called when tools like pup, pirut and yum-cli
 * have finished their transaction, and the update cache may not be valid.
 **/
gboolean
pk_engine_state_has_changed (PkEngine *engine, const gchar *reason, GError **error)
{
	gboolean is_priority = TRUE;

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* have we already scheduled priority? */
	if (engine->priv->timeout_priority_id != 0) {
		g_warning ("Already asked to refresh priority state less than %i seconds ago",
			     engine->priv->timeout_priority);
		goto out;
	}

	/* don't bombard the user 10 seconds after resuming */
	if (g_strcmp0 (reason, "resume") == 0)
		is_priority = FALSE;

	/* are we normal, and already scheduled normal? */
	if (!is_priority && engine->priv->timeout_normal_id != 0) {
		g_warning ("Already asked to refresh normal state less than %i seconds ago",
			     engine->priv->timeout_normal);
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
		engine->priv->timeout_priority_id = g_timeout_add_seconds (engine->priv->timeout_priority,
									   pk_engine_state_changed_cb, engine);
#if GLIB_CHECK_VERSION(2,25,8)
		g_source_set_name_by_id (engine->priv->timeout_priority_id, "[PkEngine] priority");
#endif
	} else {
		engine->priv->timeout_normal_id = g_timeout_add_seconds (engine->priv->timeout_normal,
									 pk_engine_state_changed_cb, engine);
#if GLIB_CHECK_VERSION(2,25,8)
		g_source_set_name_by_id (engine->priv->timeout_normal_id, "[PkEngine] normal");
#endif
	}

	/* reset the timer */
	pk_engine_reset_timer (engine);
out:
	return TRUE;
}

/**
 * pk_engine_get_time_since_action:
 *
 * @seconds: Number of seconds since the role was called, or zero is unknown
 **/
gboolean
pk_engine_get_time_since_action	(PkEngine *engine, const gchar *role_text, guint *seconds, GError **error)
{
	PkRoleEnum role;

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	role = pk_role_enum_from_string (role_text);
	*seconds = pk_transaction_db_action_time_since (engine->priv->transaction_db, role);

	/* reset the timer */
	pk_engine_reset_timer (engine);

	return TRUE;
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
		g_debug ("emitting restart-schedule because of binary change");
		g_signal_emit (engine, signals[SIGNAL_RESTART_SCHEDULE], 0);
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
 * pk_engine_suggest_daemon_quit:
 **/
gboolean
pk_engine_suggest_daemon_quit (PkEngine *engine, GError **error)
{
	guint size;

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	/* can we exit straight away */
	size = pk_transaction_list_get_size (engine->priv->transaction_list);
	if (size == 0) {
		g_debug ("emitting quit");
		g_signal_emit (engine, signals[SIGNAL_QUIT], 0);
		return TRUE;
	}

	/* This will wait from 0..10 seconds, depending on the status of
	 * pk_main_timeout_check_cb() - usually it should be a few seconds
	 * after the last transaction */
	engine->priv->shutdown_as_soon_as_possible = TRUE;
	return TRUE;
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
	DBusGMethodInvocation	*context;
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
		dbus_g_method_return_error (state->context, error);
		g_error_free (error_local);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		error = g_error_new_literal (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "failed to obtain auth");
		dbus_g_method_return_error (state->context, error);
		goto out;
	}

	/* admin already set value, so silently refuse value */
	if (priv->using_hardcoded_proxy) {
		g_debug ("cannot override admin set proxy");
		dbus_g_method_return (state->context);
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
		dbus_g_method_return_error (state->context, error);
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
	dbus_g_method_return (state->context);
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
void
pk_engine_set_proxy (PkEngine *engine,
		     const gchar *proxy_http,
		     const gchar *proxy_https,
		     const gchar *proxy_ftp,
		     const gchar *proxy_socks,
		     const gchar *no_proxy,
		     const gchar *pac,
		     DBusGMethodInvocation *context)
{
	guint len;
	GError *error = NULL;
	gboolean ret;
	gchar *sender = NULL;
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
	len = egg_strlen (proxy_http, 1024);
	if (len == 1024) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "http proxy was too long");
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* check length of ftp */
	len = egg_strlen (proxy_ftp, 1024);
	if (len == 1024) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_PROXY,
					     "ftp proxy was too long");
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* save sender */
	sender = dbus_g_method_get_sender (context);

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
		dbus_g_method_return (context);
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
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* all okay */
	dbus_g_method_return (context);
#endif

	/* reset the timer */
	pk_engine_reset_timer (engine);

#ifdef USE_SECURITY_POLKIT
	g_object_unref (subject);
#endif
out:
	g_free (sender);
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
		dbus_g_method_return_error (state->context, error);
		g_error_free (error_local);
		goto out;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized (result)) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_ROOT,
					     "failed to obtain auth");
		dbus_g_method_return_error (state->context, error);
		goto out;
	}

	/* try to set the new root and save to database */
	ret = pk_engine_set_root_internal (state->engine, state->value1, state->sender);
	if (!ret) {
		error = g_error_new_literal (PK_ENGINE_ERROR,
					     PK_ENGINE_ERROR_CANNOT_SET_ROOT,
					     "setting the root failed");
		dbus_g_method_return_error (state->context, error);
		goto out;
	}

	/* save these so we can set them after the auth success */
	g_debug ("changing root to %s for %s", state->value1, state->sender);

	/* all okay */
	dbus_g_method_return (state->context);
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
void
pk_engine_set_root (PkEngine *engine, const gchar *root, DBusGMethodInvocation *context)
{
	guint len;
	GError *error = NULL;
	gboolean ret;
	gchar *sender = NULL;
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
	len = egg_strlen (root, 1024);
	if (len == 1024) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "root was too long: %s", root);
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* check prefix of root */
	if (root[0] != '/') {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "root is not absolute: %s", root);
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* save sender */
	sender = dbus_g_method_get_sender (context);

	/* is exactly the same root? */
	ret = pk_engine_is_root_unchanged (engine, sender, root);
	if (ret) {
		g_debug ("not changing root as the same as before");
		dbus_g_method_return (context);
		goto out;
	}

	/* '/' is the default root, which doesn't need additional authentication */
	if (g_strcmp0 (root, "/") == 0) {
		ret = pk_engine_set_root_internal (engine, root, sender);
		if (ret) {
			g_debug ("using default root, so no need to authenticate");
			dbus_g_method_return (context);
		} else {
			error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_SET_ROOT, "%s", "setting the root failed");
			dbus_g_method_return_error (context, error);
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
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* all okay */
	dbus_g_method_return (context);
#endif

	/* reset the timer */
	pk_engine_reset_timer (engine);

#ifdef USE_SECURITY_POLKIT
	g_object_unref (subject);
#endif
out:
	g_free (sender);
}

/**
 * pk_engine_can_authorize:
 **/
static PkAuthorizeEnum
pk_engine_can_authorize_action_id (PkEngine *engine, const gchar *action_id, DBusGMethodInvocation *context, GError **error)
{
#ifdef USE_SECURITY_POLKIT
	gboolean ret;
	gchar *sender = NULL;
	PkAuthorizeEnum authorize;
	PolkitAuthorizationResult *res;
	PolkitSubject *subject;

	/* check subject */
	sender = dbus_g_method_get_sender (context);
	subject = polkit_system_bus_name_new (sender);

	/* check authorization (okay being sync as there's no blocking on the user) */
	res = polkit_authority_check_authorization_sync (engine->priv->authority, subject, action_id,
							 NULL, POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE, NULL, error);
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
	g_free (sender);
	return authorize;
#else
	return PK_AUTHORIZE_ENUM_YES;
#endif
}

/**
 * pk_engine_can_authorize:
 **/
void
pk_engine_can_authorize (PkEngine *engine, const gchar *action_id, DBusGMethodInvocation *context)
{
	gboolean ret;
	PkAuthorizeEnum result_enum;
	GError *error;
	GError *error_local = NULL;

	g_return_if_fail (PK_IS_ENGINE (engine));

	/* check is an action id */
	ret = g_str_has_prefix (action_id, "org.freedesktop.packagekit.");
	if (!ret) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_CHECK_AUTH,
				     "action_id '%s' has the wrong prefix", action_id);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* can we do this action? */
	result_enum = pk_engine_can_authorize_action_id (engine, action_id, context, &error_local);
	if (result_enum == PK_AUTHORIZE_ENUM_UNKNOWN) {
		error = g_error_new (PK_ENGINE_ERROR, PK_ENGINE_ERROR_CANNOT_CHECK_AUTH,
				     "failed to check authorisation %s: %s", action_id, error_local->message);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		return;
	}

	/* all okay */
	dbus_g_method_return (context, pk_authorize_type_enum_to_string (result_enum));
}

/**
 * pk_engine_get_property:
 **/
static void
pk_engine_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkEngine *engine = PK_ENGINE(object);
	gchar *tmp = NULL;

	switch (prop_id) {
	case PROP_VERSION_MAJOR:
		g_value_set_uint (value, PK_MAJOR_VERSION);
		break;
	case PROP_VERSION_MINOR:
		g_value_set_uint (value, PK_MINOR_VERSION);
		break;
	case PROP_VERSION_MICRO:
		g_value_set_uint (value, PK_MICRO_VERSION);
		break;
	case PROP_BACKEND_NAME:
		g_value_set_string (value, engine->priv->backend_name);
		break;
	case PROP_BACKEND_DESCRIPTION:
		g_value_set_string (value, engine->priv->backend_description);
		break;
	case PROP_BACKEND_AUTHOR:
		g_value_set_string (value, engine->priv->backend_author);
		break;
	case PROP_ROLES:
		tmp = pk_role_bitfield_to_string (engine->priv->roles);
		g_value_set_string (value, tmp);
		break;
	case PROP_GROUPS:
		tmp = pk_group_bitfield_to_string (engine->priv->groups);
		g_value_set_string (value, tmp);
		break;
	case PROP_FILTERS:
		tmp = pk_filter_bitfield_to_string (engine->priv->filters);
		g_value_set_string (value, tmp);
		break;
	case PROP_MIME_TYPES:
		g_value_set_string (value, engine->priv->mime_types);
		break;
	case PROP_LOCKED:
		g_value_set_boolean (value, engine->priv->locked);
		break;
	case PROP_NETWORK_STATE:
		g_value_set_string (value, pk_network_enum_to_string (engine->priv->network_state));
		break;
	case PROP_DISTRO_ID:
		g_value_set_string (value, engine->priv->distro_id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
	g_free (tmp);
}

/**
 * pk_engine_set_property:
 **/
static void
pk_engine_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_engine_class_init:
 * @klass: The PkEngineClass
 **/
static void
pk_engine_class_init (PkEngineClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_engine_finalize;
	object_class->get_property = pk_engine_get_property;
	object_class->set_property = pk_engine_set_property;

	/**
	 * PkEngine:version-major:
	 */
	pspec = g_param_spec_uint ("version-major", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MAJOR, pspec);

	/**
	 * PkEngine:version-minor:
	 */
	pspec = g_param_spec_uint ("version-minor", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MINOR, pspec);

	/**
	 * PkEngine:version-micro:
	 */
	pspec = g_param_spec_uint ("version-micro", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_VERSION_MICRO, pspec);

	/**
	 * PkEngine:backend-name:
	 */
	pspec = g_param_spec_string ("backend-name", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_BACKEND_NAME, pspec);

	/**
	 * PkEngine:backend-description:
	 */
	pspec = g_param_spec_string ("backend-description", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_BACKEND_DESCRIPTION, pspec);

	/**
	 * PkEngine:backend-author:
	 */
	pspec = g_param_spec_string ("backend-author", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_BACKEND_AUTHOR, pspec);

	/**
	 * PkEngine:roles:
	 */
	pspec = g_param_spec_string ("roles", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_ROLES, pspec);

	/**
	 * PkEngine:groups:
	 */
	pspec = g_param_spec_string ("groups", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_GROUPS, pspec);

	/**
	 * PkEngine:filters:
	 */
	pspec = g_param_spec_string ("filters", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_FILTERS, pspec);

	/**
	 * PkEngine:mime-types:
	 */
	pspec = g_param_spec_string ("mime-types", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_MIME_TYPES, pspec);

	/**
	 * PkEngine:locked:
	 */
	pspec = g_param_spec_boolean ("locked", NULL, NULL,
				      FALSE,
				      G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_LOCKED, pspec);

	/**
	 * PkEngine:network-state:
	 */
	pspec = g_param_spec_string ("network-state", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_NETWORK_STATE, pspec);

	/**
	 * PkEngine:distro-id:
	 */
	pspec = g_param_spec_string ("distro-id", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_DISTRO_ID, pspec);

	/* signals */
	signals[SIGNAL_TRANSACTION_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);
	signals[SIGNAL_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[SIGNAL_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
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
pk_engine_conf_file_changed_cb (PkFileMonitor *file_monitor, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	g_debug ("setting shutdown_as_soon_as_possible TRUE");
	engine->priv->shutdown_as_soon_as_possible = TRUE;
}

/**
 * pk_engine_binary_file_changed_cb:
 **/
static void
pk_engine_binary_file_changed_cb (PkFileMonitor *file_monitor, PkEngine *engine)
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
	g_debug ("emitting changed");
	g_signal_emit (engine, signals[SIGNAL_CHANGED], 0);
}

/**
 * pk_engine_init:
 **/
static void
pk_engine_init (PkEngine *engine)
{
	DBusGConnection *connection;
	gboolean ret;
	gchar *filename;
	gchar *root;
	gchar *proxy_http;
	gchar *proxy_https;
	gchar *proxy_ftp;
	gchar *proxy_socks;
	gchar *no_proxy;
	gchar *pac;
#if defined(USE_SECURITY_POLKIT_NEW) && defined(HAVE_POLKIT_AUTHORITY_GET_SYNC)
	GError *error = NULL;
#endif

	engine->priv = PK_ENGINE_GET_PRIVATE (engine);
	engine->priv->notify_clients_of_upgrade = FALSE;
	engine->priv->shutdown_as_soon_as_possible = FALSE;
	engine->priv->mime_types = NULL;
	engine->priv->backend_name = NULL;
	engine->priv->backend_description = NULL;
	engine->priv->backend_author = NULL;
	engine->priv->locked = FALSE;
	engine->priv->distro_id = NULL;

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
	ret = pk_backend_lock (engine->priv->backend);
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

	/* we allow fallback to these legacy methods */
	if (pk_bitfield_contain (engine->priv->roles, PK_ROLE_ENUM_GET_DEPENDS))
		pk_bitfield_add (engine->priv->roles, PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES);
	if (pk_bitfield_contain (engine->priv->roles, PK_ROLE_ENUM_GET_REQUIRES))
		pk_bitfield_add (engine->priv->roles, PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES);
	if (pk_bitfield_contain (engine->priv->roles, PK_ROLE_ENUM_GET_DEPENDS))
		pk_bitfield_add (engine->priv->roles, PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES);

	engine->priv->timer = g_timer_new ();

	/* we save a cache of the latest update lists sowe can do cached responses */
	engine->priv->cache = pk_cache_new ();

	/* we need the uid and the session for the proxy setting mechanism */
	engine->priv->dbus = pk_dbus_new ();

	/* we need to be able to clear this */
	engine->priv->timeout_priority_id = 0;
	engine->priv->timeout_normal_id = 0;

	/* get another connection */
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	if (connection == NULL)
		g_error ("no connection");

	/* add the interface */
	engine->priv->notify = pk_notify_new ();
	g_signal_connect (engine->priv->notify, "repo-list-changed",
			  G_CALLBACK (pk_engine_notify_repo_list_changed_cb), engine);
	g_signal_connect (engine->priv->notify, "updates-changed",
			  G_CALLBACK (pk_engine_notify_updates_changed_cb), engine);

	/* monitor the config file for changes */
	engine->priv->file_monitor_conf = pk_file_monitor_new ();
	filename = pk_conf_get_filename ();
	pk_file_monitor_set_file (engine->priv->file_monitor_conf, filename);
	g_signal_connect (engine->priv->file_monitor_conf, "file-changed",
			  G_CALLBACK (pk_engine_conf_file_changed_cb), engine);
	g_free (filename);

#ifdef USE_SECURITY_POLKIT
	/* protect the session SetProxy with a PolicyKit action */
#if defined(USE_SECURITY_POLKIT_NEW) && defined(HAVE_POLKIT_AUTHORITY_GET_SYNC)
	engine->priv->authority = polkit_authority_get_sync (NULL, &error);
	if (engine->priv->authority == NULL) {
		g_error ("failed to get pokit authority: %s", error->message);
		g_error_free (error);
	}
#else
	engine->priv->authority = polkit_authority_get ();
#endif
#endif

	/* monitor the binary file for changes */
	engine->priv->file_monitor_binary = pk_file_monitor_new ();
	pk_file_monitor_set_file (engine->priv->file_monitor_binary, SBINDIR "/packagekitd");
	g_signal_connect (engine->priv->file_monitor_binary, "file-changed",
			  G_CALLBACK (pk_engine_binary_file_changed_cb), engine);

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

	engine->priv->inhibit = pk_inhibit_new ();
	g_signal_connect (engine->priv->inhibit, "locked",
			  G_CALLBACK (pk_engine_inhibit_locked_cb), engine);

	/* we use a trasaction db to store old transactions and to do rollbacks */
	engine->priv->transaction_db = pk_transaction_db_new ();
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

	/* unlock if we locked this */
	ret = pk_backend_unlock (engine->priv->backend);
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

	/* compulsory gobjects */
	g_timer_destroy (engine->priv->timer);
	g_object_unref (engine->priv->file_monitor_conf);
	g_object_unref (engine->priv->file_monitor_binary);
	g_object_unref (engine->priv->inhibit);
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

