/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <pk-package-id.h>
#include <pk-package-ids.h>
#include <pk-package-list.h>

#include <pk-debug.h>
#include <pk-common.h>
#include <pk-filter.h>
#include <pk-network.h>
#include <pk-package-list.h>
#include <pk-enum.h>
#include <pk-enum-list.h>

#include "pk-cache.h"
#include "pk-backend.h"
#include "pk-backend-internal.h"
#include "pk-engine.h"
#include "pk-transaction.h"
#include "pk-transaction-id.h"
#include "pk-transaction-db.h"
#include "pk-transaction-list.h"
#include "pk-inhibit.h"
#include "pk-marshal.h"
#include "pk-notify.h"
#include "pk-restart.h"

static void     pk_engine_class_init	(PkEngineClass *klass);
static void     pk_engine_init		(PkEngine      *engine);
static void     pk_engine_finalize	(GObject       *object);

#define PK_ENGINE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_ENGINE, PkEnginePrivate))

/**
 * PK_ENGINE_STATE_CHANGED_TIMEOUT:
 *
 * The timeout in seconds to wait when we get the StateHasChanged method.
 * We don't want to queue these transactions if one is already in progress.
 *
 * We probably also need to wait for NetworkManager to come back up if we are
 * resuming, and we probably don't want to be doing this at a busy time after
 * a yum tramsaction.
 */
#define PK_ENGINE_STATE_CHANGED_TIMEOUT		5 /* seconds */

struct PkEnginePrivate
{
	GTimer			*timer;
	gboolean		 restart_schedule;
	PkTransactionList	*transaction_list;
	PkTransactionDb		*transaction_db;
	PkCache			*cache;
	PkBackend		*backend;
	PkInhibit		*inhibit;
	PkNetwork		*network;
	PkNotify		*notify;
	PkRestart		*restart;
	PkEnumList		*actions;
	PkEnumList		*groups;
	PkEnumList		*filters;
	gboolean		 signal_state_timeout; /* don't queue StateHasChanged */
};

enum {
	PK_ENGINE_LOCKED,
	PK_ENGINE_TRANSACTION_LIST_CHANGED,
	PK_ENGINE_REPO_LIST_CHANGED,
	PK_ENGINE_RESTART_SCHEDULE,
	PK_ENGINE_UPDATES_CHANGED,
	PK_ENGINE_LAST_SIGNAL
};

static guint	     signals [PK_ENGINE_LAST_SIGNAL] = { 0 };

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
	if (!quark) {
		quark = g_quark_from_static_string ("pk_engine_error");
	}
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
	pk_debug ("reset timer");
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

	pk_debug ("emitting transaction-list-changed");
	g_signal_emit (engine, signals [PK_ENGINE_TRANSACTION_LIST_CHANGED], 0, transaction_list);
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_inhibit_locked_cb:
 **/
static void
pk_engine_inhibit_locked_cb (PkInhibit *inhibit, gboolean is_locked, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	pk_debug ("emitting locked %i", is_locked);
	g_signal_emit (engine, signals [PK_ENGINE_LOCKED], 0, is_locked);
}

/**
 * pk_engine_notify_repo_list_changed_cb:
 **/
static void
pk_engine_notify_repo_list_changed_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	pk_debug ("emitting repo-list-changed");
	g_signal_emit (engine, signals [PK_ENGINE_REPO_LIST_CHANGED], 0);
}

/**
 * pk_engine_notify_restart_schedule_cb:
 **/
static void
pk_engine_notify_restart_schedule_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	pk_debug ("emitting restart-schedule");
	g_signal_emit (engine, signals [PK_ENGINE_RESTART_SCHEDULE], 0);
}

/**
 * pk_engine_notify_updates_changed_cb:
 **/
static void
pk_engine_notify_updates_changed_cb (PkNotify *notify, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	pk_debug ("emitting updates-changed");
	g_signal_emit (engine, signals [PK_ENGINE_UPDATES_CHANGED], 0);
}

/**
 * pk_engine_finished_cb:
 **/
static void
pk_engine_finished_cb (PkBackend *backend, PkExitEnum exit, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));

	/* daemon is busy */
	pk_engine_reset_timer (engine);
}

/**
 * pk_engine_get_tid:
 **/
gboolean
pk_engine_get_tid (PkEngine *engine, gchar **tid, GError **error)
{
	gchar *new_tid;
	gboolean ret;

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetTid method called");
	new_tid = pk_transaction_id_generate ();

	ret = pk_transaction_list_create (engine->priv->transaction_list, new_tid);
	pk_debug ("sending tid: '%s'", new_tid);
	*tid =  g_strdup (new_tid);
	g_free (new_tid);

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

	pk_debug ("GetTransactionList method called");
	*transaction_list = pk_transaction_list_get_array (engine->priv->transaction_list);

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
	PkEngine *engine = PK_ENGINE (data);

	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("unreffing updates cache as state may have changed");
	pk_cache_invalidate (engine->priv->cache);

	pk_notify_updates_changed (engine->priv->notify);

	/* reset, now valid */
	engine->priv->signal_state_timeout = 0;

	return FALSE;
}

/**
 * pk_engine_state_has_changed:
 *
 * This should be called when tools like pup, pirut and yum-cli
 * have finished their transaction, and the update cache may not be valid.
 **/
gboolean
pk_engine_state_has_changed (PkEngine *engine, GError **error)
{
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	if (engine->priv->signal_state_timeout != 0) {
		g_set_error (error, PK_ENGINE_ERROR, PK_ENGINE_ERROR_INVALID_STATE,
			     "Already asked to refresh state less than %i seconds ago",
			     PK_ENGINE_STATE_CHANGED_TIMEOUT);
		return FALSE;
	}

	/* wait a little delay in case we get multiple requests */
	engine->priv->signal_state_timeout = g_timeout_add_seconds (PK_ENGINE_STATE_CHANGED_TIMEOUT,
								    pk_engine_state_changed_cb, engine);

	return TRUE;
}

/**
 * pk_engine_get_actions:
 **/
gboolean
pk_engine_get_actions (PkEngine *engine, gchar **actions, GError **error)
{
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);
	*actions = pk_enum_list_to_string (engine->priv->actions);
	return TRUE;
}

/**
 * pk_engine_get_groups:
 **/
gboolean
pk_engine_get_groups (PkEngine *engine, gchar **groups, GError **error)
{
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);
	*groups = pk_enum_list_to_string (engine->priv->groups);
	return TRUE;
}

/**
 * pk_engine_get_filters:
 **/
gboolean
pk_engine_get_filters (PkEngine *engine, gchar **filters, GError **error)
{
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);
	*filters = pk_enum_list_to_string (engine->priv->filters);
	return TRUE;
}

/**
 * pk_engine_get_backend_detail:
 **/
gboolean
pk_engine_get_backend_detail (PkEngine *engine, gchar **name, gchar **author, GError **error)
{
	g_return_val_if_fail (PK_IS_ENGINE (engine), FALSE);

	pk_debug ("GetBackendDetail method called");
	pk_backend_get_backend_detail (engine->priv->backend, name, author);
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

	role = pk_role_enum_from_text (role_text);
	*seconds = pk_transaction_db_action_time_since (engine->priv->transaction_db, role);

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
		pk_debug ("engine idle zero as %i transactions in progress", size);
		return 0;
	}

	/* have we been updated? */
	if (engine->priv->restart_schedule) {
		pk_debug ("need to restart daemon *NOW*");
		pk_notify_restart_schedule (engine->priv->notify);
		return G_MAXUINT;
	}

	idle = (guint) g_timer_elapsed (engine->priv->timer, NULL);
	pk_debug ("engine idle=%i", idle);
	return idle;
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

	/* set up signal that emits 'au' */
	signals [PK_ENGINE_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_ENGINE_TRANSACTION_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOXED,
			      G_TYPE_NONE, 1, G_TYPE_STRV);

	g_type_class_add_private (klass, sizeof (PkEnginePrivate));
}

/**
 * pk_engine_restart_schedule_cb:
 **/
static void
pk_engine_restart_schedule_cb (PkRestart *restart, PkEngine *engine)
{
	g_return_if_fail (PK_IS_ENGINE (engine));
	pk_debug ("setting restart_schedule TRUE");
	engine->priv->restart_schedule = TRUE;
}

/**
 * pk_engine_get_actions_internal:
 *
 * You need to g_object_unref the returned object
 */
static PkEnumList *
pk_engine_get_actions_internal (PkEngine *engine)
{
	PkEnumList *elist;
	PkBackendDesc *desc;

	g_return_val_if_fail (engine != NULL, NULL);

	/* lets reduce pointer dereferences... */
	desc = engine->priv->backend->desc;

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);
	if (desc->cancel != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_CANCEL);
	}
	if (desc->get_depends != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_DEPENDS);
	}
	if (desc->get_description != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_DESCRIPTION);
	}
	if (desc->get_files != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_FILES);
	}
	if (desc->get_requires != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_REQUIRES);
	}
	if (desc->what_provides != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_WHAT_PROVIDES);
	}
	if (desc->get_updates != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_UPDATES);
	}
	if (desc->get_update_detail != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_UPDATE_DETAIL);
	}
	if (desc->install_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_INSTALL_PACKAGE);
	}
	if (desc->install_file != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_INSTALL_FILE);
	}
	if (desc->refresh_cache != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REFRESH_CACHE);
	}
	if (desc->remove_package != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REMOVE_PACKAGE);
	}
	if (desc->resolve != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_RESOLVE);
	}
	if (desc->rollback != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_ROLLBACK);
	}
	if (desc->search_details != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_DETAILS);
	}
	if (desc->search_file != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_FILE);
	}
	if (desc->search_group != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_GROUP);
	}
	if (desc->search_name != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_SEARCH_NAME);
	}
	if (desc->update_packages != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_UPDATE_PACKAGES);
	}
	if (desc->update_system != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_UPDATE_SYSTEM);
	}
	if (desc->get_repo_list != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_GET_REPO_LIST);
	}
	if (desc->repo_enable != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REPO_ENABLE);
	}
	if (desc->repo_set_data != NULL) {
		pk_enum_list_append (elist, PK_ROLE_ENUM_REPO_SET_DATA);
	}
	return elist;
}

/**
 * pk_engine_get_groups_internal:
 *
 * You need to g_object_unref the returned object
 */
static PkEnumList *
pk_engine_get_groups_internal (PkEngine *engine)
{
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_GROUP);
	if (engine->priv->backend->desc->get_groups != NULL) {
		engine->priv->backend->desc->get_groups (engine->priv->backend, elist);
	}
	return elist;
}

/**
 * pk_engine_get_filters_internal:
 *
 * You need to g_object_unref the returned object
 */
static PkEnumList *
pk_engine_get_filters_internal (PkEngine *engine)
{
	PkEnumList *elist;

	g_return_val_if_fail (engine != NULL, NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_FILTER);
	if (engine->priv->backend->desc->get_filters != NULL) {
		engine->priv->backend->desc->get_filters (engine->priv->backend, elist);
	}
	return elist;
}

/**
 * pk_engine_init:
 **/
static void
pk_engine_init (PkEngine *engine)
{
	DBusGConnection *connection;
	gboolean ret;

	engine->priv = PK_ENGINE_GET_PRIVATE (engine);
	engine->priv->restart_schedule = FALSE;

	/* setup the backend backend */
	engine->priv->backend = pk_backend_new ();
	g_signal_connect (engine->priv->backend, "finished",
			  G_CALLBACK (pk_engine_finished_cb), engine);

	/* lock database */
	ret = pk_backend_lock (engine->priv->backend);
	if (!ret) {
		pk_error ("could not lock backend, you need to restart the daemon");
	}

	/* we dont need this, just don't keep creating and destroying it */
	engine->priv->network = pk_network_new ();

	/* create a new backend so we can get the static stuff */
	engine->priv->actions = pk_engine_get_actions_internal (engine);
	engine->priv->groups = pk_engine_get_groups_internal (engine);
	engine->priv->filters = pk_engine_get_filters_internal (engine);

	engine->priv->timer = g_timer_new ();

	/* we save a cache of the latest update lists sowe can do cached responses */
	engine->priv->cache = pk_cache_new ();

	/* we need to be able to clear this */
	engine->priv->signal_state_timeout = 0;

	/* get another connection */
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	if (connection == NULL) {
		pk_error ("no connection");
	}

	/* add the interface */
	engine->priv->notify = pk_notify_new ();
	g_signal_connect (engine->priv->notify, "restart-schedule",
			  G_CALLBACK (pk_engine_notify_restart_schedule_cb), engine);
	g_signal_connect (engine->priv->notify, "repo-list-changed",
			  G_CALLBACK (pk_engine_notify_repo_list_changed_cb), engine);
	g_signal_connect (engine->priv->notify, "updates-changed",
			  G_CALLBACK (pk_engine_notify_updates_changed_cb), engine);

	/* add the interface */
	engine->priv->restart = pk_restart_new ();
	g_signal_connect (engine->priv->restart, "restart-schedule",
			  G_CALLBACK (pk_engine_restart_schedule_cb), engine);

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
	pk_debug ("engine finalise");

	/* unlock if we locked this */
	ret = pk_backend_unlock (engine->priv->backend);
	if (!ret) {
		pk_warning ("couldn't unlock the backend");
	}

	/* if we set an state changed notifier, clear */
	if (engine->priv->signal_state_timeout != 0) {
		g_source_remove (engine->priv->signal_state_timeout);
		engine->priv->signal_state_timeout = 0;
	}

	/* compulsory gobjects */
	g_timer_destroy (engine->priv->timer);
	g_object_unref (engine->priv->restart);
	g_object_unref (engine->priv->inhibit);
	g_object_unref (engine->priv->transaction_list);
	g_object_unref (engine->priv->transaction_db);
	g_object_unref (engine->priv->network);
	g_object_unref (engine->priv->notify);
	g_object_unref (engine->priv->backend);
	g_object_unref (engine->priv->cache);

	/* optional gobjects */
	if (engine->priv->actions != NULL) {
		g_object_unref (engine->priv->actions);
	}
	if (engine->priv->groups != NULL) {
		g_object_unref (engine->priv->groups);
	}
	if (engine->priv->filters != NULL) {
		g_object_unref (engine->priv->filters);
	}

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

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_engine (LibSelfTest *test)
{
	gboolean ret;
	PkEngine *engine;
	PkBackend *backend;

	if (libst_start (test, "PkEngine", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get a backend instance");
	backend = pk_backend_new ();
	if (backend != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/* set the type, as we have no pk-main doing this for us */
	/************************************************************/
	libst_title (test, "set the backend name");
	ret = pk_backend_set_name (backend, "dummy");
	if (ret) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "get an engine instance");
	engine = pk_engine_new ();
	if (engine != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	g_object_unref (backend);
	g_object_unref (engine);

	libst_end (test);
}
#endif

