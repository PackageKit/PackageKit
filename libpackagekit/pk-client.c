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

/**
 * SECTION:pk-client
 * @short_description: GObject class for PackageKit client access
 *
 * This file contains a nice GObject to use for accessing PackageKit
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include "pk-enum.h"
#include "pk-client.h"
#include "pk-connection.h"
#include "pk-package-id.h"
#include "pk-package-list.h"
#include "pk-debug.h"
#include "pk-marshal.h"
#include "pk-polkit-client.h"
#include "pk-common.h"

static void     pk_client_class_init	(PkClientClass *klass);
static void     pk_client_init		(PkClient      *client);
static void     pk_client_finalize	(GObject       *object);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct PkClientPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	GMainLoop		*loop;
	GHashTable		*hash;
	gboolean		 is_finished;
	gboolean		 use_buffer;
	gboolean		 synchronous;
	gboolean		 name_filter;
	gboolean		 promiscuous;
	gchar			*tid;
	PkPackageList		*package_list;
	PkConnection		*pconnection;
	PkPolkitClient		*polkit;
	PkRestartEnum		 require_restart;
	PkStatusEnum		 last_status;
	PkRoleEnum		 role;
	gboolean		 xcached_force;
	gboolean		 xcached_allow_deps;
	gboolean		 xcached_autoremove;
	gchar			*xcached_package_id;
	gchar			*xcached_transaction_id;
	gchar			*xcached_full_path;
	gchar			*xcached_filter;
	gchar			*xcached_search;
};

typedef enum {
	PK_CLIENT_DESCRIPTION,
	PK_CLIENT_ERROR_CODE,
	PK_CLIENT_FILES,
	PK_CLIENT_FINISHED,
	PK_CLIENT_PACKAGE,
	PK_CLIENT_PROGRESS_CHANGED,
	PK_CLIENT_UPDATES_CHANGED,
	PK_CLIENT_REQUIRE_RESTART,
	PK_CLIENT_MESSAGE,
	PK_CLIENT_TRANSACTION,
	PK_CLIENT_STATUS_CHANGED,
	PK_CLIENT_UPDATE_DETAIL,
	PK_CLIENT_REPO_SIGNATURE_REQUIRED,
	PK_CLIENT_CALLER_ACTIVE_CHANGED,
	PK_CLIENT_REPO_DETAIL,
	PK_CLIENT_ALLOW_CANCEL,
	PK_CLIENT_LOCKED,
	PK_CLIENT_LAST_SIGNAL
} PkSignals;

static guint signals [PK_CLIENT_LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

/**
 * pk_client_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
pk_client_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark) {
		quark = g_quark_from_static_string ("pk_client_error");
	}
	return quark;
}

/**
 * pk_client_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_client_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_CLIENT_ERROR_FAILED, "Failed"),
			ENUM_ENTRY (PK_CLIENT_ERROR_NO_TID, "NoTid"),
			ENUM_ENTRY (PK_CLIENT_ERROR_ALREADY_TID, "AlreadyTid"),
			ENUM_ENTRY (PK_CLIENT_ERROR_ROLE_UNKNOWN, "RoleUnkown"),
			ENUM_ENTRY (PK_CLIENT_ERROR_PROMISCUOUS, "Promiscuous"),
			ENUM_ENTRY (PK_CLIENT_ERROR_INVALID_PACKAGEID, "InvalidPackageId"),
			{ 0, 0, 0 }
		};
		etype = g_enum_register_static ("PkClientError", values);
	}
	return etype;
}

/******************************************************************************
 *                    LOCAL FUNCTIONS
 ******************************************************************************/

/**
 * pk_client_error_set:
 *
 * Sets the correct error code (if allowed) and print to the screen
 * as a warning.
 **/
static gboolean
pk_client_error_set (GError **error, gint code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;
	gboolean ret = TRUE;

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* dumb */
	if (error == NULL) {
		pk_warning ("%s", buffer);
		ret = FALSE;
		goto out;
	}

	/* already set */
	if (*error != NULL) {
		pk_warning ("not NULL error!");
		g_clear_error (error);
	}

	/* propogate */
	g_set_error (error, PK_CLIENT_ERROR, code, buffer);

out:
	g_free(buffer);
	return ret;
}

/**
 * pk_client_error_print:
 **/
gboolean
pk_client_error_print (GError **error)
{
	const gchar *name;

	if (error != NULL && *error != NULL) {
		/* get some proper debugging */
		if ((*error)->domain == DBUS_GERROR &&
		    (*error)->code == DBUS_GERROR_REMOTE_EXCEPTION) {
			name = dbus_g_error_get_name (*error);
		} else {
			name = g_quark_to_string ((*error)->domain);
		}
		pk_warning ("ERROR: %s: %s", name, (*error)->message);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_client_error_fixup:
 **/
static gboolean
pk_client_error_fixup (GError **error)
{
	if (error != NULL && *error != NULL) {
		/* get some proper debugging */
		if ((*error)->domain == DBUS_GERROR &&
		    (*error)->code == DBUS_GERROR_REMOTE_EXCEPTION) {
			/* use one of our local codes */
			pk_debug ("fixing up code from %i", (*error)->code);
			(*error)->code = PK_CLIENT_ERROR_FAILED;
		}
		pk_client_error_print (error);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_client_set_tid:
 * @client: a valid #PkClient instance
 * @tid: a transaction id
 *
 * This method sets the transaction ID that should be used for the DBUS method
 * and then watched for any callback signals.
 * You cannot call pk_client_set_tid multiple times for one instance.
 *
 * Return value: %TRUE if set correctly
 **/
gboolean
pk_client_set_tid (PkClient *client, const gchar *tid, GError **error)
{
	if (client->priv->promiscuous) {
		pk_client_error_set (error, PK_CLIENT_ERROR_PROMISCUOUS,
				     "cannot set the tid on a promiscuous client");
		return FALSE;
	}
	if (client->priv->tid != NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_ALREADY_TID,
				     "cannot set the tid on an already set client");
		return FALSE;
	}
	client->priv->tid = g_strdup (tid);
	return TRUE;
}

/**
 * pk_client_set_promiscuous:
 * @client: a valid #PkClient instance
 * @enabled: if we should set promiscuous mode on
 *
 * If we set the client promiscuous then it listens to all signals from
 * all transactions. You can't set promiscuous mode on an already set tid
 * instance.
 *
 * Return value: %TRUE if we set the mode okay.
 **/
gboolean
pk_client_set_promiscuous (PkClient *client, gboolean enabled, GError **error)
{
	if (client->priv->tid != NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_PROMISCUOUS,
				     "cannot set promiscuous on a tid client");
		return FALSE;
	}
	client->priv->promiscuous = enabled;
	return TRUE;
}

/**
 * pk_client_get_tid:
 * @client: a valid #PkClient instance
 *
 * Return value: The transaction_id we are using for this client, or %NULL
 **/
gchar *
pk_client_get_tid (PkClient *client)
{
	if (client->priv->tid == NULL) {
		return NULL;
	}
	return g_strdup (client->priv->tid);
}

/**
 * pk_transaction_id_equal:
 * @client: a valid #PkClient instance
 * @tid1: the first transaction id
 * @tid2: the second transaction id
 *
 * Return value: %TRUE if tid1 and tid2 are equal
 * TODO: only compare first two sections...
 **/
G_GNUC_WARN_UNUSED_RESULT static gboolean
pk_transaction_id_equal (const gchar *tid1, const gchar *tid2)
{
	if (tid1 == NULL || tid2 == NULL) {
		pk_warning ("tid compare invalid '%s' and '%s'", tid1, tid2);
		return FALSE;
	}
	return pk_strequal (tid1, tid2);
}

/**
 * pk_client_set_use_buffer:
 * @client: a valid #PkClient instance
 * @use_buffer: if we should use the package buffer
 *
 * If the package buffer is enabled then after the transaction has completed
 * then the package list can be retrieved in one go, rather than processing
 * each package request async.
 * If this is not set true explicitly, then pk_client_package_buffer_get_size
 * will always return zero items.
 *
 * This is not forced on as there may be significant overhead if the list
 * contains many hundreds of items.
 *
 * Return value: %TRUE if the package buffer was enabled
 **/
gboolean
pk_client_set_use_buffer (PkClient *client, gboolean use_buffer, GError **error)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	client->priv->use_buffer = use_buffer;
	return TRUE;
}

/**
 * pk_client_set_synchronous:
 * @client: a valid #PkClient instance
 * @synchronous: if we should do the method synchronous
 *
 * Return value: %TRUE if the synchronous mode was enabled
 **/
gboolean
pk_client_set_synchronous (PkClient *client, gboolean synchronous, GError **error)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	client->priv->synchronous = synchronous;
	return TRUE;
}

/**
 * pk_client_set_name_filter:
 * @client: a valid #PkClient instance
 * @name_filter: if we should check for previous packages before we emit
 *
 * Return value: %TRUE if the name_filter mode was enabled
 **/
gboolean
pk_client_set_name_filter (PkClient *client, gboolean name_filter, GError **error)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	client->priv->name_filter = name_filter;
	return TRUE;
}

/**
 * pk_client_get_use_buffer:
 * @client: a valid #PkClient instance
 *
 * Return value: %TRUE if the package buffer is enabled
 **/
gboolean
pk_client_get_use_buffer (PkClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return client->priv->use_buffer;
}

/**
 * pk_client_get_require_restart:
 * @client: a valid #PkClient instance
 *
 * This method returns the 'worst' restart of all the transactions.
 * It is needed as multiple sub-transactions may emit require-restart with
 * different values, and we always want to get the most invasive of all.
 *
 * For instance, if a transaction emits RequireRestart(system) and then
 * RequireRestart(session) then pk_client_get_require_restart will return
 * system as a session restart is implied with a system restart.
 *
 * Return value: a #PkRestartEnum value, e.g. PK_RESTART_ENUM_SYSTEM
 **/
PkRestartEnum
pk_client_get_require_restart (PkClient *client)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return client->priv->require_restart;
}

/**
 * pk_client_package_buffer_get_size:
 * @client: a valid #PkClient instance
 *
 * Return value: The size of the package buffer.
 **/
guint
pk_client_package_buffer_get_size (PkClient *client)
{
	g_return_val_if_fail (client != NULL, 0);
	g_return_val_if_fail (PK_IS_CLIENT (client), 0);
	if (!client->priv->use_buffer) {
		return 0;
	}
	return pk_package_list_get_size (client->priv->package_list);
}

/**
 * pk_client_package_buffer_get_item:
 * @client: a valid #PkClient instance
 * @item: the item in the package buffer
 *
 * Return value: The #PkPackageItem or %NULL if not found or invalid
 **/
PkPackageItem *
pk_client_package_buffer_get_item (PkClient *client, guint item)
{
	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	if (!client->priv->use_buffer) {
		return NULL;
	}
	return pk_package_list_get_item (client->priv->package_list, item);
}

/**
 * pk_client_reset:
 * @client: a valid #PkClient instance
 *
 * Resetting the client way be needed if we canceled the request without
 * waiting for ::finished, or if we want to reuse the #PkClient without
 * unreffing and creating it again.
 *
 * Return value: %TRUE if we reset the client
 **/
gboolean
pk_client_reset (PkClient *client, GError **error)
{
	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	if (client->priv->is_finished != TRUE) {
		pk_debug ("not exit status, reset might be invalid");
	}
	g_free (client->priv->tid);
	client->priv->tid = NULL;
	client->priv->use_buffer = FALSE;
	client->priv->synchronous = FALSE;
	client->priv->name_filter = FALSE;
	client->priv->tid = NULL;
	client->priv->last_status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;

	/* clear hash */
	g_hash_table_remove_all (client->priv->hash);

	pk_package_list_clear (client->priv->package_list);
	return TRUE;
}

/******************************************************************************
 *                    SIGNALS
 ******************************************************************************/

/**
 * pk_client_should_proxy:
 */
static gboolean
pk_client_should_proxy (PkClient *client, const gchar *tid)
{
	/* are we promiscuous? */
	if (client->priv->promiscuous) {
		g_free (client->priv->tid);
		client->priv->tid = g_strdup (tid);
		return TRUE;
	}

	/* check to see if we have been assigned yet */
	if (client->priv->tid == NULL) {
		/* silently fail to avoid cluttering up the logs */
		return FALSE;
	}

	/* are we the right on? */
	if (pk_transaction_id_equal (tid, client->priv->tid)) {
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_client_finished_cb:
 */
static void
pk_client_finished_cb (DBusGProxy  *proxy,
		       gchar	   *tid,
		       const gchar *exit_text,
		       guint        runtime,
		       PkClient    *client)
{
	PkExitEnum exit;

	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	exit = pk_exit_enum_from_text (exit_text);
	pk_debug ("emit finished %s, %i", exit_text, runtime);

	/* only this instance is finished, and do it before the signal so we can reset */
	client->priv->is_finished = TRUE;

	g_signal_emit (client, signals [PK_CLIENT_FINISHED], 0, exit, runtime);

	/* check we are still valid */
	if (!PK_IS_CLIENT (client)) {
		pk_debug ("client was g_object_unref'd in finalise, object no longer valid");
		return;
	}

	/* exit our private loop */
	if (client->priv->synchronous) {
		g_main_loop_quit (client->priv->loop);
	}
}

/**
 * pk_client_progress_changed_cb:
 */
static void
pk_client_progress_changed_cb (DBusGProxy  *proxy, const gchar *tid,
			       guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit progress-changed %i, %i, %i, %i", percentage, subpercentage, elapsed, remaining);
	g_signal_emit (client , signals [PK_CLIENT_PROGRESS_CHANGED], 0,
		       percentage, subpercentage, elapsed, remaining);
}

/**
 * pk_client_status_changed_cb:
 */
static void
pk_client_status_changed_cb (DBusGProxy  *proxy,
				         const gchar *tid,
				         const gchar *status_text,
				         PkClient    *client)
{
	PkStatusEnum status;

	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	status = pk_status_enum_from_text (status_text);

	pk_debug ("emit status-changed %s", status_text);
	g_signal_emit (client , signals [PK_CLIENT_STATUS_CHANGED], 0, status);

	client->priv->last_status = status;
}

/**
 * pk_client_package_cb:
 */
static void
pk_client_package_cb (DBusGProxy   *proxy,
		      const gchar  *tid,
		      const gchar  *info_text,
		      const gchar  *package_id,
		      const gchar  *summary,
		      PkClient     *client)
{
	PkInfoEnum info;
	PkPackageId *pid;
	const gchar *data;

	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	/* filter repeat names */
	if (client->priv->name_filter) {
		/* get the package name */
		pid = pk_package_id_new_from_string (package_id);
		pk_debug ("searching hash for %s", pid->name);

		/* is already in the cache? */
		data = (const gchar *) g_hash_table_lookup (client->priv->hash, pid->name);
		if (data != NULL) {
			pk_package_id_free (pid);
			pk_debug ("ignoring as name filter is on, and previous found; %s", data);
			return;
		}

		/* add to the cache */
		pk_debug ("adding %s into the hash", pid->name);
		g_hash_table_insert (client->priv->hash, g_strdup (pid->name), g_strdup (pid->name));
		pk_package_id_free (pid);
	}


	pk_debug ("emit package %s, %s, %s", info_text, package_id, summary);
	info = pk_info_enum_from_text (info_text);
	g_signal_emit (client , signals [PK_CLIENT_PACKAGE], 0, info, package_id, summary);

	/* cache */
	if (client->priv->use_buffer || client->priv->synchronous) {
		pk_debug ("adding to cache array package %i, %s, %s", info, package_id, summary);
		pk_package_list_add (client->priv->package_list, info, package_id, summary);
	}
}

/**
 * pk_client_updates_changed_cb:
 */
static void
pk_client_updates_changed_cb (DBusGProxy *proxy, const gchar *tid, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* we always emit, even if the tid does not match */
	pk_debug ("emitting updates-changed");
	g_signal_emit (client, signals [PK_CLIENT_UPDATES_CHANGED], 0);

}

/**
 * pk_client_transaction_cb:
 */
static void
pk_client_transaction_cb (DBusGProxy *proxy,
			  const gchar *tid, const gchar *old_tid, const gchar *timespec,
			  gboolean succeeded, const gchar *role_text, guint duration,
			  const gchar *data, PkClient *client)
{
	PkRoleEnum role;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	role = pk_role_enum_from_text (role_text);
	pk_debug ("emitting transaction %s, %s, %i, %s, %i, %s", old_tid, timespec,
		  succeeded, role_text, duration, data);
	g_signal_emit (client, signals [PK_CLIENT_TRANSACTION], 0, old_tid, timespec,
		       succeeded, role, duration, data);
}

/**
 * pk_client_update_detail_cb:
 */
static void
pk_client_update_detail_cb (DBusGProxy  *proxy,
			    const gchar *tid,
			    const gchar *package_id,
			    const gchar *updates,
			    const gchar *obsoletes,
			    const gchar *vendor_url,
			    const gchar *bugzilla_url,
			    const gchar *cve_url,
			    const gchar *restart_text,
			    const gchar *update_text,
			    PkClient    *client)
{
	PkRestartEnum restart;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit update-detail %s, %s, %s, %s, %s, %s, %s, %s",
		  package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart_text, update_text);
	restart = pk_restart_enum_from_text (restart_text);
	g_signal_emit (client , signals [PK_CLIENT_UPDATE_DETAIL], 0,
		       package_id, updates, obsoletes, vendor_url, bugzilla_url, cve_url, restart, update_text);
}

/**
 * pk_client_description_cb:
 */
static void
pk_client_description_cb (DBusGProxy  *proxy,
			  const gchar *tid,
			  const gchar *package_id,
			  const gchar *license,
			  const gchar *group_text,
			  const gchar *description,
			  const gchar *url,
			  guint64      size,
			  PkClient    *client)
{
	PkGroupEnum group;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	group = pk_group_enum_from_text (group_text);
	pk_debug ("emit description %s, %s, %i, %s, %s, %ld",
		  package_id, license, group, description, url, (long int) size);
	g_signal_emit (client , signals [PK_CLIENT_DESCRIPTION], 0,
		       package_id, license, group, description, url, size);
}

/**
 * pk_client_files_cb:
 */
static void
pk_client_files_cb (DBusGProxy  *proxy,
		    const gchar *tid,
		    const gchar *package_id,
		    const gchar *filelist,
		    PkClient    *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit files %s, %s", package_id, filelist);
	g_signal_emit (client , signals [PK_CLIENT_FILES], 0, package_id,
		       filelist);
}

/**
 * pk_client_repo_signature_required_cb:
 **/
static void
pk_client_repo_signature_required_cb (DBusGProxy *proxy, const gchar *tid, const gchar *repository_name,
				      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				      const gchar *key_fingerprint, const gchar *key_timestamp,
				      const gchar *type_text, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit repo_signature_required tid:%s, %s, %s, %s, %s, %s, %s, %s",
		  tid, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
	g_signal_emit (client, signals [PK_CLIENT_REPO_SIGNATURE_REQUIRED], 0,
		       repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
}

/**
 * pk_client_repo_detail_cb:
 **/
static void
pk_client_repo_detail_cb (DBusGProxy *proxy, const gchar *tid, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit repo-detail %s, %s, %i", repo_id, description, enabled);
	g_signal_emit (client, signals [PK_CLIENT_REPO_DETAIL], 0, repo_id, description, enabled);
}

/**
 * pk_client_error_code_cb:
 */
static void
pk_client_error_code_cb (DBusGProxy  *proxy,
			 const gchar *tid,
			 const gchar *code_text,
			 const gchar *details,
			 PkClient    *client)
{
	PkErrorCodeEnum code;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	code = pk_error_enum_from_text (code_text);
	pk_debug ("emit error-code %i, %s", code, details);
	g_signal_emit (client , signals [PK_CLIENT_ERROR_CODE], 0, code, details);
}

/**
 * pk_client_locked_cb:
 */
static void
pk_client_locked_cb (DBusGProxy *proxy, gboolean is_locked, PkClient *client)
{
	pk_debug ("emit locked %i", is_locked);
	g_signal_emit (client , signals [PK_CLIENT_LOCKED], 0, is_locked);
}

/**
 * pk_client_allow_cancel_cb:
 */
static void
pk_client_allow_cancel_cb (DBusGProxy *proxy, const gchar *tid,
			      gboolean allow_cancel, PkClient *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit allow-cancel %i", allow_cancel);
	g_signal_emit (client , signals [PK_CLIENT_ALLOW_CANCEL], 0, allow_cancel);
}

/**
 * pk_client_get_allow_cancel:
 */
gboolean
pk_client_get_allow_cancel (PkClient *client, gboolean *allow_cancel, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_NO_TID, "Transaction ID not set");
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetAllowCancel", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, allow_cancel,
				 G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_caller_active_changed_cb:
 */
static void
pk_client_caller_active_changed_cb (DBusGProxy  *proxy,
				    const gchar *tid,
				    gboolean     is_active,
				    PkClient    *client)
{
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	pk_debug ("emit caller-active-changed %i", is_active);
	g_signal_emit (client , signals [PK_CLIENT_CALLER_ACTIVE_CHANGED], 0, is_active);
}

/**
 * pk_client_require_restart_cb:
 */
static void
pk_client_require_restart_cb (DBusGProxy  *proxy,
			      const gchar *tid,
			      const gchar *restart_text,
			      const gchar *details,
			      PkClient    *client)
{
	PkRestartEnum restart;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	restart = pk_restart_enum_from_text (restart_text);
	pk_debug ("emit require-restart %i, %s", restart, details);
	g_signal_emit (client , signals [PK_CLIENT_REQUIRE_RESTART], 0, restart, details);
	if (restart > client->priv->require_restart) {
		client->priv->require_restart = restart;
		pk_debug ("restart status now %s", pk_restart_enum_to_text (restart));
	}
}

/**
 * pk_client_message_cb:
 */
static void
pk_client_message_cb (DBusGProxy  *proxy, const gchar *tid,
		      const gchar *message_text, const gchar *details, PkClient *client)
{
	PkMessageEnum message;
	g_return_if_fail (client != NULL);
	g_return_if_fail (PK_IS_CLIENT (client));

	/* not us, ignore */
	if (!pk_client_should_proxy (client, tid)) {
		return;
	}

	message = pk_message_enum_from_text (message_text);
	pk_debug ("emit message %i, %s", message, details);
	g_signal_emit (client , signals [PK_CLIENT_MESSAGE], 0, message, details);
}

/******************************************************************************
 *                    TRANSACTION ID USING METHODS
 ******************************************************************************/

/**
 * pk_client_get_status:
 **/
gboolean
pk_client_get_status (PkClient *client, PkStatusEnum *status, GError **error)
{
	gboolean ret;
	gchar *status_text;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (status != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_NO_TID, "Transaction ID not set");
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetStatus", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_INVALID);
	pk_client_error_fixup (error);
	if (ret) {
		*status = pk_status_enum_from_text (status_text);
		g_free (status_text);
	}
	return ret;
}

/**
 * pk_client_get_package:
 **/
gboolean
pk_client_get_package (PkClient *client, gchar **package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_NO_TID, "Transaction ID not set");
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetPackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_progress:
 **/
gboolean
pk_client_get_progress (PkClient *client, guint *percentage, guint *subpercentage,
			guint *elapsed, guint *remaining, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_NO_TID, "Transaction ID not set");
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetProgress", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_UINT, subpercentage,
				 G_TYPE_UINT, elapsed,
				 G_TYPE_UINT, remaining,
				 G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_role:
 **/
gboolean
pk_client_get_role (PkClient *client, PkRoleEnum *role, gchar **package_id, GError **error)
{
	gboolean ret;
	gchar *role_text;
	gchar *package_id_temp;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (role != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we have a valid transaction */
	if (client->priv->tid == NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_NO_TID, "Transaction ID not set");
		*role = PK_ROLE_ENUM_UNKNOWN;
		return FALSE;
	}

	/* we can avoid a trip to the daemon */
	if (!client->priv->promiscuous && package_id == NULL) {
		*role = client->priv->role;
		return TRUE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetRole", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &role_text,
				 G_TYPE_STRING, &package_id_temp,
				 G_TYPE_INVALID);
	if (ret) {
		*role = pk_role_enum_from_text (role_text);
		g_free (role_text);
		if (package_id != NULL) {
			*package_id = package_id_temp;
		} else {
			g_free (package_id_temp);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_cancel:
 **/
gboolean
pk_client_cancel (PkClient *client, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we have an tid */
	if (client->priv->tid == NULL) {
		pk_debug ("Transaction ID not set, assumed never used");
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "Cancel", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/******************************************************************************
 *                    TRANSACTION ID CREATING METHODS
 ******************************************************************************/

/**
 * pk_client_allocate_transaction_id:
 **/
static gboolean
pk_client_allocate_transaction_id (PkClient *client, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	if (client->priv->tid != NULL) {
		pk_client_error_set (error, PK_CLIENT_ERROR_ALREADY_TID, "Already has transaction ID");
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetTid", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &client->priv->tid,
				 G_TYPE_INVALID);
	if (ret) {
		pk_debug ("Got tid: '%s'", client->priv->tid);
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_updates:
 **/
gboolean
pk_client_get_updates (PkClient *client, const gchar *filter, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (filter != NULL, FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATES;

	ret = dbus_g_proxy_call (client->priv->proxy, "GetUpdates", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_update_system_action:
 **/
gboolean
pk_client_update_system_action (PkClient *client, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "UpdateSystem", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_update_system:
 **/
gboolean
pk_client_update_system (PkClient *client, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_UPDATE_SYSTEM;

	/* hopefully do the operation first time */
	ret = pk_client_update_system_action (client, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_update_system_action (client, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_search_name:
 **/
gboolean
pk_client_search_name (PkClient *client, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_NAME;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	ret = dbus_g_proxy_call (client->priv->proxy, "SearchName", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_search_details:
 **/
gboolean
pk_client_search_details (PkClient *client, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	ret = dbus_g_proxy_call (client->priv->proxy, "SearchDetails", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_search_group:
 **/
gboolean
pk_client_search_group (PkClient *client, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_GROUP;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	ret = dbus_g_proxy_call (client->priv->proxy, "SearchGroup", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_search_file:
 **/
gboolean
pk_client_search_file (PkClient *client, const gchar *filter, const gchar *search, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_FILE;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_search = g_strdup (search);

	ret = dbus_g_proxy_call (client->priv->proxy, "SearchFile", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_depends:
 **/
gboolean
pk_client_get_depends (PkClient *client, const gchar *package_id, gboolean recursive, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DEPENDS;
	client->priv->xcached_package_id = g_strdup (package_id);
	client->priv->xcached_force = recursive;

	ret = dbus_g_proxy_call (client->priv->proxy, "GetDepends", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_BOOLEAN, recursive,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_requires:
 **/
gboolean
pk_client_get_requires (PkClient *client, const gchar *package_id, gboolean recursive, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_REQUIRES;
	client->priv->xcached_package_id = g_strdup (package_id);
	client->priv->xcached_force = recursive;

	ret = dbus_g_proxy_call (client->priv->proxy, "GetRequires", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_BOOLEAN, recursive,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_update_detail:
 **/
gboolean
pk_client_get_update_detail (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	client->priv->xcached_package_id = g_strdup (package_id);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetUpdateDetail", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_rollback:
 **/
gboolean
pk_client_rollback (PkClient *client, const gchar *transaction_id, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_ROLLBACK;
	client->priv->xcached_transaction_id = g_strdup (transaction_id);

	ret = dbus_g_proxy_call (client->priv->proxy, "Rollback", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, transaction_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_resolve:
 **/
gboolean
pk_client_resolve (PkClient *client, const gchar *filter, const gchar *package, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_RESOLVE;
	client->priv->xcached_filter = g_strdup (filter);
	client->priv->xcached_package_id = g_strdup (package);

	ret = dbus_g_proxy_call (client->priv->proxy, "Resolve", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, filter,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_description:
 **/
gboolean
pk_client_get_description (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DESCRIPTION;
	client->priv->xcached_package_id = g_strdup (package_id);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetDescription", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_files:
 **/
gboolean
pk_client_get_files (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_FILES;
	client->priv->xcached_package_id = g_strdup (package_id);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetFiles", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret) {
		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
		}
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_remove_package_action:
 **/
gboolean
pk_client_remove_package_action (PkClient *client, const gchar *package_id,
				 gboolean allow_deps, gboolean autoremove,
				 GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "RemovePackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_BOOLEAN, allow_deps,
				 G_TYPE_BOOLEAN, autoremove,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_remove_package:
 **/
gboolean
pk_client_remove_package (PkClient *client, const gchar *package_id, gboolean allow_deps, gboolean autoremove, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REMOVE_PACKAGE;
	client->priv->xcached_allow_deps = allow_deps;
	client->priv->xcached_autoremove = autoremove;
	client->priv->xcached_package_id = g_strdup (package_id);

	/* hopefully do the operation first time */
	ret = pk_client_remove_package_action (client, package_id, allow_deps, autoremove, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_remove_package_action (client, package_id, allow_deps, autoremove, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_refresh_cache_action:
 **/
gboolean
pk_client_refresh_cache_action (PkClient *client, gboolean force, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "RefreshCache", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_BOOLEAN, force,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_refresh_cache:
 **/
gboolean
pk_client_refresh_cache (PkClient *client, gboolean force, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REFRESH_CACHE;
	client->priv->xcached_force = force;

	/* hopefully do the operation first time */
	ret = pk_client_refresh_cache_action (client, force, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_refresh_cache_action (client, force, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_install_package_action:
 **/
gboolean
pk_client_install_package_action (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "InstallPackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_install_package:
 **/
gboolean
pk_client_install_package (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_PACKAGE;
	client->priv->xcached_package_id = g_strdup (package_id);

	/* hopefully do the operation first time */
	ret = pk_client_install_package_action (client, package_id, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_install_package_action (client, package_id, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_update_package_action:
 **/
gboolean
pk_client_update_package_action (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "UpdatePackage", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_update_package:
 **/
gboolean
pk_client_update_package (PkClient *client, const gchar *package_id, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);

	/* check the PackageID here to avoid a round trip if invalid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		pk_client_error_set (error, PK_CLIENT_ERROR_INVALID_PACKAGEID,
				     "package_id '%s' is not valid", package_id);
		return FALSE;
	}

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_UPDATE_PACKAGE;
	client->priv->xcached_package_id = g_strdup (package_id);

	/* hopefully do the operation first time */
	ret = pk_client_update_package_action (client, package_id, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_update_package_action (client, package_id, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_install_file_action:
 **/
gboolean
pk_client_install_file_action (PkClient *client, const gchar *file, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "InstallFile", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, file,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_install_file:
 **/
gboolean
pk_client_install_file (PkClient *client, const gchar *file, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (file != NULL, FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_FILE;
	client->priv->xcached_full_path = g_strdup (file);

	/* hopefully do the operation first time */
	ret = pk_client_install_file_action (client, file, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_install_file_action (client, file, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}


/**
 * pk_client_service_pack_action:
 **/
gboolean
pk_client_service_pack_action (PkClient *client, const gchar *location, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "ServicePack", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, location,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_service_pack:
 **/
gboolean
pk_client_service_pack (PkClient *client, const gchar *location, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (location != NULL, FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SERVICE_PACK;
	client->priv->xcached_full_path = g_strdup (location);

	/* hopefully do the operation first time */
	ret = pk_client_service_pack_action (client, location, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_service_pack_action (client, location, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_get_repo_list:
 */
gboolean
pk_client_get_repo_list (PkClient *client, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}
	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_REPO_LIST;

	ret = dbus_g_proxy_call (client->priv->proxy, "GetRepoList", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_repo_enable_action:
 **/
gboolean
pk_client_repo_enable_action (PkClient *client, const gchar *repo_id, gboolean enabled, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "RepoEnable", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, repo_id,
				 G_TYPE_BOOLEAN, enabled,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_repo_enable:
 */
gboolean
pk_client_repo_enable (PkClient *client, const gchar *repo_id, gboolean enabled, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REPO_ENABLE;

	/* hopefully do the operation first time */
	ret = pk_client_repo_enable_action (client, repo_id, enabled, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_repo_enable_action (client, repo_id, enabled, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/**
 * pk_client_repo_set_data_action:
 **/
gboolean
pk_client_repo_set_data_action (PkClient *client, const gchar *repo_id,
				const gchar *parameter, const gchar *value, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "RepoSetData", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_STRING, repo_id,
				 G_TYPE_STRING, parameter,
				 G_TYPE_STRING, value,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * pk_client_repo_set_data:
 */
gboolean
pk_client_repo_set_data (PkClient *client, const gchar *repo_id, const gchar *parameter,
			 const gchar *value, GError **error)
{
	gboolean ret;
	GError *error_pk = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);
	g_return_val_if_fail (parameter != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REPO_SET_DATA;

	/* hopefully do the operation first time */
	ret = pk_client_repo_set_data_action (client, repo_id, parameter, value, &error_pk);

	/* we were refused by policy */
	if (!ret && pk_polkit_client_error_denied_by_policy (error_pk)) {
		/* try to get auth */
		if (pk_polkit_client_gain_privilege_str (client->priv->polkit, error_pk->message)) {
			/* clear old error */
			g_clear_error (&error_pk);
			/* retry the action now we have got auth */
			ret = pk_client_repo_set_data_action (client, repo_id, parameter, value, &error_pk);
		}
	}
	/* we failed one of these, return the error to the user */
	if (!ret) {
		pk_client_error_fixup (&error_pk);
		g_propagate_error (error, error_pk);
	}

	/* spin until finished */
	if (ret && client->priv->synchronous) {
		g_main_loop_run (client->priv->loop);
	}

	return ret;
}

/******************************************************************************
 *                    NON-TRANSACTION ID METHODS
 ******************************************************************************/

/**
 * pk_client_get_actions:
 **/
PkEnumList *
pk_client_get_actions (PkClient *client)
{
	gboolean ret;
	GError *error = NULL;
	gchar *actions;
	PkEnumList *elist;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_ROLE);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetActions", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &actions,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetActions failed :%s", error->message);
		g_error_free (error);
		return elist;
	}

	/* convert to enumerated types */
	pk_enum_list_from_string (elist, actions);
	g_free (actions);
	return elist;
}

/**
 * pk_client_get_backend_detail:
 **/
gboolean
pk_client_get_backend_detail (PkClient *client, gchar **name, gchar **author, GError **error)
{
	gboolean ret;
	gchar *tname;
	gchar *tauthor;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetBackendDetail", error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tname,
				 G_TYPE_STRING, &tauthor,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		pk_client_error_fixup (error);
		return FALSE;
	}

	/* copy needed bits */
	if (name != NULL) {
		*name = tname;
	} else {
		g_free (tauthor);
	}
	/* copy needed bits */
	if (author != NULL) {
		*author = tauthor;
	} else {
		g_free (tauthor);
	}
	return ret;
}

/**
 * pk_client_get_time_since_action:
 **/
gboolean
pk_client_get_time_since_action (PkClient *client, PkRoleEnum role, guint *seconds, GError **error)
{
	gboolean ret;
	const gchar *role_text;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	role_text = pk_role_enum_to_text (role);
	ret = dbus_g_proxy_call (client->priv->proxy, "GetTimeSinceAction", error,
				 G_TYPE_STRING, role_text,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, seconds,
				 G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_is_caller_active:
 **/
gboolean
pk_client_is_caller_active (PkClient *client, gboolean *is_active, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	ret = dbus_g_proxy_call (client->priv->proxy, "IsCallerActive", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, is_active,
				 G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_groups:
 **/
PkEnumList *
pk_client_get_groups (PkClient *client)
{
	gboolean ret;
	GError *error = NULL;
	gchar *groups;
	PkEnumList *elist;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_GROUP);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetGroups", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &groups,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetGroups failed :%s", error->message);
		g_error_free (error);
		return elist;
	}

	/* convert to enumerated types */
	pk_enum_list_from_string (elist, groups);
	g_free (groups);
	return elist;
}

/**
 * pk_client_get_old_transactions:
 **/
gboolean
pk_client_get_old_transactions (PkClient *client, guint number, GError **error)
{
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* check to see if we already have a transaction */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	ret = dbus_g_proxy_call (client->priv->proxy, "GetOldTransactions", error,
				 G_TYPE_STRING, client->priv->tid,
				 G_TYPE_UINT, number,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_get_filters:
 **/
PkEnumList *
pk_client_get_filters (PkClient *client)
{
	gboolean ret;
	GError *error = NULL;
	gchar *filters;
	PkEnumList *elist;

	g_return_val_if_fail (client != NULL, NULL);
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);

	elist = pk_enum_list_new ();
	pk_enum_list_set_type (elist, PK_ENUM_LIST_TYPE_FILTER);

	ret = dbus_g_proxy_call (client->priv->proxy, "GetFilters", &error,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &filters,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		pk_warning ("GetFilters failed :%s", error->message);
		g_error_free (error);
		return elist;
	}

	/* convert to enumerated types */
	pk_enum_list_from_string (elist, filters);
	g_free (filters);
	return elist;
}

/**
 * pk_client_requeue:
 * @client: a valid #PkClient instance
 *
 * We might need to requeue if we want to take an existing #PkClient instance
 * and re-run it after completion. Doing this allows us to do things like
 * re-searching when the output list may have changed state.
 *
 * Return value: %TRUE if we could requeue the client
 */
gboolean
pk_client_requeue (PkClient *client, GError **error)
{
	PkRoleEnum role;
	gboolean ret;

	g_return_val_if_fail (client != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	/* we are no longer waiting, we are setting up */
	if (client->priv->role == PK_ROLE_ENUM_UNKNOWN) {
		pk_client_error_set (error, PK_CLIENT_ERROR_ROLE_UNKNOWN, "role unknown for reque");
		return FALSE;
	}

	/* save the role */
	role = client->priv->role;

	/* reset this client, which doesn't clear cached data */
	pk_client_reset (client, NULL);

	/* restore the role */
	client->priv->role = role;

	/* do the correct action with the cached parameters */
	if (client->priv->role == PK_ROLE_ENUM_GET_DEPENDS) {
		ret = pk_client_get_depends (client, client->priv->xcached_package_id,
				       client->priv->xcached_force, error);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL) {
		ret = pk_client_get_update_detail (client, client->priv->xcached_package_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_RESOLVE) {
		ret = pk_client_resolve (client, client->priv->xcached_filter,
				   client->priv->xcached_package_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_ROLLBACK) {
		ret = pk_client_rollback (client, client->priv->xcached_transaction_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_DESCRIPTION) {
		ret = pk_client_get_description (client, client->priv->xcached_package_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_FILES) {
		ret = pk_client_get_files (client, client->priv->xcached_package_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_REQUIRES) {
		ret = pk_client_get_requires (client, client->priv->xcached_package_id,
					client->priv->xcached_force, error);
	} else if (client->priv->role == PK_ROLE_ENUM_GET_UPDATES) {
		ret = pk_client_get_updates (client, client->priv->xcached_filter, error);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_DETAILS) {
		ret = pk_client_search_details (client, client->priv->xcached_filter,
					  client->priv->xcached_search, error);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_FILE) {
		ret = pk_client_search_file (client, client->priv->xcached_filter,
				       client->priv->xcached_search, error);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_GROUP) {
		ret = pk_client_search_group (client, client->priv->xcached_filter,
					client->priv->xcached_search, error);
	} else if (client->priv->role == PK_ROLE_ENUM_SEARCH_NAME) {
		ret = pk_client_search_name (client, client->priv->xcached_filter,
				       client->priv->xcached_search, error);
	} else if (client->priv->role == PK_ROLE_ENUM_INSTALL_PACKAGE) {
		ret = pk_client_install_package (client, client->priv->xcached_package_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_INSTALL_FILE) {
		ret = pk_client_install_file (client, client->priv->xcached_full_path, error);
	} else if (client->priv->role == PK_ROLE_ENUM_SERVICE_PACK) {
		ret = pk_client_service_pack (client, client->priv->xcached_full_path, error);
	} else if (client->priv->role == PK_ROLE_ENUM_REFRESH_CACHE) {
		ret = pk_client_refresh_cache (client, client->priv->xcached_force, error);
	} else if (client->priv->role == PK_ROLE_ENUM_REMOVE_PACKAGE) {
		ret = pk_client_remove_package (client, client->priv->xcached_package_id,
					  client->priv->xcached_allow_deps, client->priv->xcached_autoremove, error);
	} else if (client->priv->role == PK_ROLE_ENUM_UPDATE_PACKAGE) {
		ret = pk_client_update_package (client, client->priv->xcached_package_id, error);
	} else if (client->priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM) {
		ret = pk_client_update_system (client, error);
	} else {
		pk_client_error_set (error, PK_CLIENT_ERROR_ROLE_UNKNOWN, "role unknown for reque");
		return FALSE;
	}
	pk_client_error_fixup (error);
	return ret;
}

/**
 * pk_client_class_init:
 **/
static void
pk_client_class_init (PkClientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_client_finalize;

	signals [PK_CLIENT_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PK_CLIENT_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [PK_CLIENT_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
	signals [PK_CLIENT_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_CLIENT_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOL_UINT_UINT_STRING,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_DESCRIPTION] =
		g_signal_new ("description",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_UINT_STRING_STRING_UINT64,
			      G_TYPE_NONE, 6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_UINT64);
	signals [PK_CLIENT_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	signals [PK_CLIENT_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_UINT,
			      G_TYPE_NONE, 7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	signals [PK_CLIENT_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOLEAN,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	signals [PK_CLIENT_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	signals [PK_CLIENT_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_CLIENT_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_CLIENT_CALLER_ACTIVE_CHANGED] =
		g_signal_new ("caller-active-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [PK_CLIENT_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkClientPrivate));
}

/**
 * pk_client_connect:
 **/
static void
pk_client_connect (PkClient *client)
{
	pk_debug ("connect");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkClient *client)
{
	pk_debug ("connected=%i", connected);

	/* TODO: if PK re-started mid-transaction then show a big fat warning */
}

/**
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	GError *error = NULL;
	DBusGProxy *proxy = NULL;

	client->priv = PK_CLIENT_GET_PRIVATE (client);
	client->priv->tid = NULL;
	client->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	client->priv->loop = g_main_loop_new (NULL, FALSE);
	client->priv->use_buffer = FALSE;
	client->priv->promiscuous = FALSE;
	client->priv->last_status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->require_restart = PK_RESTART_ENUM_NONE;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;
	client->priv->package_list = pk_package_list_new ();
	client->priv->xcached_package_id = NULL;
	client->priv->xcached_transaction_id = NULL;
	client->priv->xcached_full_path = NULL;
	client->priv->xcached_filter = NULL;
	client->priv->xcached_search = NULL;

	/* check dbus connections, exit if not valid */
	client->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		pk_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* watch for PackageKit on the bus, and try to connect up at start */
	client->priv->pconnection = pk_connection_new ();
	g_signal_connect (client->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_connection_changed_cb), client);
	if (pk_connection_valid (client->priv->pconnection)) {
		pk_client_connect (client);
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (client->priv->connection,
					   PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (proxy == NULL) {
		g_error ("Cannot connect to PackageKit.");
	}
	client->priv->proxy = proxy;

	/* use PolicyKit */
	client->priv->polkit = pk_polkit_client_new ();

	/* ProgressChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT_UINT_UINT_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT,
					   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);

	/* AllowCancel */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_BOOLEAN,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* Locked */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* StatusChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	/* ErrorCode, RequireRestart, Message */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* CallerActiveChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_BOOLEAN,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* Description */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_UINT64,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
					   G_TYPE_INVALID);

	/* Files */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_INVALID);

	/* Repo Signature Required */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Package */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* RepoDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_BOOL,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* UpdateDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* Transaction */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_BOOL_STRING_UINT_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "ProgressChanged",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ProgressChanged",
				     G_CALLBACK (pk_client_progress_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "StatusChanged",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "StatusChanged",
				     G_CALLBACK (pk_client_status_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_client_package_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Transaction",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Transaction",
				     G_CALLBACK (pk_client_transaction_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "UpdatesChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "UpdatesChanged",
				     G_CALLBACK (pk_client_updates_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "UpdateDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_client_update_detail_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Description",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
				 G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Description",
				     G_CALLBACK (pk_client_description_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Files",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Files",
				     G_CALLBACK (pk_client_files_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RepoSignatureRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RepoSignatureRequired",
				     G_CALLBACK (pk_client_repo_signature_required_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RepoDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RepoDetail",
				     G_CALLBACK (pk_client_repo_detail_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "ErrorCode",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_client_error_code_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "RequireRestart",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_client_require_restart_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Message",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Message",
				     G_CALLBACK (pk_client_message_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "CallerActiveChanged",
				 G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "CallerActiveChanged",
				     G_CALLBACK (pk_client_caller_active_changed_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "AllowCancel", G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "AllowCancel",
				     G_CALLBACK (pk_client_allow_cancel_cb), client, NULL);

	dbus_g_proxy_add_signal (proxy, "Locked", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Locked",
				     G_CALLBACK (pk_client_locked_cb), client, NULL);
}

/**
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CLIENT (object));
	client = PK_CLIENT (object);
	g_return_if_fail (client->priv != NULL);

	/* free cached strings */
	g_free (client->priv->xcached_package_id);
	g_free (client->priv->xcached_transaction_id);
	g_free (client->priv->xcached_full_path);
	g_free (client->priv->xcached_filter);
	g_free (client->priv->xcached_search);
	g_free (client->priv->tid);

	/* clear the loop, if we were using it */
	if (client->priv->synchronous) {
		g_main_loop_quit (client->priv->loop);
	}
	g_main_loop_unref (client->priv->loop);

	/* free the hash table */
	g_hash_table_destroy (client->priv->hash);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Finished",
				        G_CALLBACK (pk_client_finished_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ProgressChanged",
				        G_CALLBACK (pk_client_progress_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "StatusChanged",
				        G_CALLBACK (pk_client_status_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_client_updates_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Package",
				        G_CALLBACK (pk_client_package_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Transaction",
				        G_CALLBACK (pk_client_transaction_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Description",
				        G_CALLBACK (pk_client_description_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Files",
				        G_CALLBACK (pk_client_files_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RepoSignatureRequired",
				        G_CALLBACK (pk_client_repo_signature_required_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ErrorCode",
				        G_CALLBACK (pk_client_error_code_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RequireRestart",
				        G_CALLBACK (pk_client_require_restart_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Message",
				        G_CALLBACK (pk_client_message_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "CallerActiveChanged",
					G_CALLBACK (pk_client_caller_active_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "AllowCancel",
				        G_CALLBACK (pk_client_allow_cancel_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Locked",
				        G_CALLBACK (pk_client_locked_cb), client);

	/* free the proxy */
	g_object_unref (G_OBJECT (client->priv->proxy));
	g_object_unref (client->priv->pconnection);
	g_object_unref (client->priv->polkit);
	g_object_unref (client->priv->package_list);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 **/
PkClient *
pk_client_new (void)
{
	PkClient *client;
	client = g_object_new (PK_TYPE_CLIENT, NULL);
	return PK_CLIENT (client);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>
#include <glib/gstdio.h>

static gboolean finished = FALSE;

static void
libst_client_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, gpointer data)
{
	finished = TRUE;
	/* this is actually quite common */
	g_object_unref (client);
}

void
libst_client (LibSelfTest *test)
{
	PkClient *client;

	if (libst_start (test, "PkClient", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "get client");
	client = pk_client_new ();
	if (client != NULL) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/* check use after finalise */
	g_signal_connect (client, "finished",
			  G_CALLBACK (libst_client_finished_cb), NULL);

	/************************************************************/
	libst_title (test, "do any method");
	/* we don't care if this fails */
	pk_client_set_synchronous (client, TRUE, NULL);
	pk_client_search_name (client, "none", "moooo", NULL);
	libst_success (test, "did something");

	/************************************************************/
	libst_title (test, "we finished?");
	if (finished) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	libst_end (test);
}
#endif

