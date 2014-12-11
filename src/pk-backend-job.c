/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Richard Hughes <richard@hughsie.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib/gprintf.h>

#include <packagekit-glib2/pk-results.h>

#include "pk-cleanup.h"
#include "pk-backend.h"
#include "pk-backend-job.h"
#include "pk-shared.h"

#ifdef PK_BUILD_DAEMON
  #include "pk-sysdep.h"
#endif

#define PK_BACKEND_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_BACKEND_JOB, PkBackendJobPrivate))

/**
 * PK_BACKEND_FINISHED_ERROR_TIMEOUT:
 *
 * The time in ms the backend has to call Finished() after ErrorCode()
 * If backends do not do this, they will be Finished() manually,
 * and a Message() will be sent to warn the developer
 */
#define PK_BACKEND_FINISHED_ERROR_TIMEOUT	2000 /* ms */

/**
 * PK_BACKEND_FINISHED_TIMEOUT_GRACE:
 *
 * The time in ms the backend waits after receiving Finished() before
 * propagating the signal to the other components.
 * This delay is required as some threads may take some time to cancel or a
 * spawned executable to disappear of the system DBUS.
 */
#define PK_BACKEND_FINISHED_TIMEOUT_GRACE	10 /* ms */

/**
 * PK_BACKEND_CANCEL_ACTION_TIMEOUT:
 *
 * The time in ms we cancel the transaction ourselves if the backend is ignoring
 * us. This means the backend will still be running, but results will not be
 * sent over the dbus interface.
 */
#define PK_BACKEND_CANCEL_ACTION_TIMEOUT	2000 /* ms */

typedef struct {
	gboolean		 enabled;
	PkBackendJobVFunc	 vfunc;
	gpointer		 user_data;
} PkBackendJobVFuncItem;

struct PkBackendJobPrivate
{
	gboolean		 finished;
	gboolean		 has_sent_package;
	gboolean		 set_error;
	gboolean		 set_eula;
	gboolean		 set_signature;
	gchar			*cmdline;
	gchar			*frontend_socket;
	gchar			*locale;
	gchar			*no_proxy;
	gchar			*pac;
	gchar			*proxy_ftp;
	gchar			*proxy_http;
	gchar			*proxy_https;
	gchar			*proxy_socks;
	gpointer		 user_data;
	GThread			*thread;
	guint64			 download_size_remaining;
	guint			 cache_age;
	guint			 download_files;
	guint			 percentage;
	guint			 remaining;
	guint			 speed;
	guint			 uid;
	GVariant		*params;
	GCancellable		*cancellable;
	PkBackend		*backend;
	PkBackendJobVFuncItem	 vfunc_items[PK_BACKEND_SIGNAL_LAST];
	PkBitfield		 transaction_flags;
	GKeyFile		*conf;
	PkExitEnum		 exit;
	gboolean		 allow_cancel;
	gboolean		 background;
	gboolean		 interactive;
	gboolean		 locked;
	PkPackage		*last_package;
	PkErrorEnum		 last_error_code;
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	GTimer			*timer;
	gboolean		 started;
};

G_DEFINE_TYPE (PkBackendJob, pk_backend_job, G_TYPE_OBJECT)

/**
 * pk_backend_job_reset:
 **/
void
pk_backend_job_reset (PkBackendJob *job)
{
	guint i;
	PkBackendJobVFuncItem *item;

	job->priv->finished = FALSE;
	job->priv->has_sent_package = FALSE;
	job->priv->set_error = FALSE;
	job->priv->allow_cancel = TRUE;
	job->priv->thread = NULL;
	job->priv->exit = PK_EXIT_ENUM_UNKNOWN;
	job->priv->role = PK_ROLE_ENUM_UNKNOWN;
	job->priv->status = PK_STATUS_ENUM_UNKNOWN;

	/* reset the vfuncs too */
	for (i = 0; i < PK_BACKEND_SIGNAL_LAST; i++) {
		item = &job->priv->vfunc_items[i];
		item->enabled = FALSE;
		item->vfunc = NULL;
		item->user_data = NULL;
	}
}

/**
 * pk_backend_job_get_vfunc_enabled:
 **/
gboolean
pk_backend_job_get_vfunc_enabled (PkBackendJob *job,
				  PkBackendJobSignal signal_kind)
{
	PkBackendJobVFuncItem *item;
	item = &job->priv->vfunc_items[signal_kind];
	if (!item->enabled)
		return FALSE;
	if (item->vfunc == NULL)
		return FALSE;
	return TRUE;
}

/**
 * pk_backend_job_get_cancellable:
 *
 * Return value: (transfer none): a #GCancellable
 **/
GCancellable *
pk_backend_job_get_cancellable (PkBackendJob *job)
{
	return job->priv->cancellable;
}

/**
 * pk_backend_job_is_cancelled:
 *
 * Return value: (transfer none): a #GCancellable
 **/
gboolean
pk_backend_job_is_cancelled (PkBackendJob *job)
{
	return g_cancellable_is_cancelled (job->priv->cancellable);
}

/**
 * pk_backend_job_get_backend:
 *
 * Return value: (transfer none): Associated PkBackend instance
 **/
gpointer
pk_backend_job_get_backend (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return job->priv->backend;
}

/**
 * pk_backend_job_set_backend:
 **/
void
pk_backend_job_set_backend (PkBackendJob *job, gpointer backend)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->backend = backend;
}

/**
 * pk_backend_job_get_user_data:
 *
 * Return value: (transfer none): Job user data
 **/
gpointer
pk_backend_job_get_user_data (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return job->priv->user_data;
}

/**
 * pk_backend_job_get_transaction_flags:
 **/
PkBitfield
pk_backend_job_get_transaction_flags (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), 0);
	return job->priv->transaction_flags;
}


/**
 * pk_backend_job_set_transaction_flags:
 **/
void
pk_backend_job_set_transaction_flags (PkBackendJob *job,
				      PkBitfield transaction_flags)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->transaction_flags = transaction_flags;
}

/**
 * pk_backend_job_set_proxy:
 **/
void
pk_backend_job_set_proxy (PkBackendJob	*job,
		      const gchar *proxy_http,
		      const gchar *proxy_https,
		      const gchar *proxy_ftp,
		      const gchar *proxy_socks,
		      const gchar *no_proxy,
		      const gchar *pac)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_free (job->priv->proxy_http);
	g_free (job->priv->proxy_https);
	g_free (job->priv->proxy_ftp);
	g_free (job->priv->proxy_socks);
	g_free (job->priv->no_proxy);
	g_free (job->priv->pac);
	job->priv->proxy_http = g_strdup (proxy_http);
	job->priv->proxy_https = g_strdup (proxy_https);
	job->priv->proxy_ftp = g_strdup (proxy_ftp);
	job->priv->proxy_socks = g_strdup (proxy_socks);
	job->priv->no_proxy = g_strdup (no_proxy);
	job->priv->pac = g_strdup (pac);
}

/**
 * pk_backend_job_get_proxy_http:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_job_get_proxy_http (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->proxy_http);
}

/**
 * pk_backend_job_get_proxy_https:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_job_get_proxy_https (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->proxy_https);
}

/**
 * pk_backend_job_get_proxy_ftp:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_job_get_proxy_ftp (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->proxy_ftp);
}

/**
 * pk_backend_job_get_proxy_socks:
 *
 * Return value: proxy string in the form username:password@server:port
 **/
gchar *
pk_backend_job_get_proxy_socks (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->proxy_socks);
}

/**
 * pk_backend_job_get_no_proxy:
 *
 * Return value: comma seporated value of proxy exlude string
 **/
gchar *
pk_backend_job_get_no_proxy (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->no_proxy);
}

/**
 * pk_backend_job_get_pac:
 *
 * Return value: proxy PAC filename
 **/
gchar *
pk_backend_job_get_pac (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->pac);
}

/**
 * pk_backend_job_set_cmdline:
 **/
void
pk_backend_job_set_cmdline (PkBackendJob *job, const gchar *cmdline)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	g_free (job->priv->cmdline);
	job->priv->cmdline = g_strdup (cmdline);
	g_debug ("install cmdline now %s", job->priv->cmdline);
}

/**
 * pk_backend_job_get_cmdline:
 **/
const gchar *
pk_backend_job_get_cmdline (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return job->priv->cmdline;
}

/**
 * pk_backend_job_set_uid:
 **/
void
pk_backend_job_set_uid (PkBackendJob *job, guint uid)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	if (job->priv->uid == uid)
		return;

	job->priv->uid = uid;
	g_debug ("install uid now %i", job->priv->uid);
}

/**
 * pk_backend_job_get_uid:
 **/
guint
pk_backend_job_get_uid (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	return job->priv->uid;
}


/**
 * pk_backend_job_get_locale:
 *
 * Return value: session locale, e.g. en_GB
 **/
gchar *
pk_backend_job_get_locale (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->locale);
}

/**
 * pk_backend_job_set_locale:
 **/
void
pk_backend_job_set_locale (PkBackendJob *job, const gchar *code)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (code != NULL);

	if (g_strcmp0 (job->priv->locale, code) == 0)
		return;

	g_debug ("locale changed to %s", code);
	g_free (job->priv->locale);
	job->priv->locale = g_strdup (code);
}

/**
 * pk_backend_job_get_parameters:
 **/
GVariant *
pk_backend_job_get_parameters (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return job->priv->params;
}

/**
 * pk_backend_job_set_parameters:
 **/
void
pk_backend_job_set_parameters (PkBackendJob *job, GVariant *params)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (params != NULL);

	job->priv->params = g_variant_ref_sink (params);
}

/**
 * pk_backend_job_get_frontend_socket:
 *
 * Return value: session frontend_socket, e.g. /tmp/socket.345
 **/
gchar *
pk_backend_job_get_frontend_socket (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), NULL);
	return g_strdup (job->priv->frontend_socket);
}

/**
 * pk_backend_job_set_frontend_socket:
 **/
void
pk_backend_job_set_frontend_socket (PkBackendJob *job, const gchar *frontend_socket)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	if (g_strcmp0 (job->priv->frontend_socket, frontend_socket) == 0)
		return;

	g_debug ("frontend_socket changed to %s", frontend_socket);
	g_free (job->priv->frontend_socket);
	job->priv->frontend_socket = g_strdup (frontend_socket);
}

/**
 * pk_backend_job_get_cache_age:
 *
 * Gets the maximum cache age in seconds.
 *
 * Return value: the cache age in seconds, or 0 for unset or %G_MAXUINT for 'infinity'
 **/
guint
pk_backend_job_get_cache_age (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), 0);
	return job->priv->cache_age;
}

/**
 * pk_backend_job_set_cache_age:
 **/
void
pk_backend_job_set_cache_age (PkBackendJob *job, guint cache_age)
{
	const guint cache_age_offset = 60 * 30;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* We offset the cache age by 30 minutes if possible to
	 * account for the possible delay in running the transaction,
	 * for example:
	 *
	 * Update check set to once per 3 days
	 * GUI starts checking for updates on Monday at 12:00
	 * Update check completes on Monday at 12:01
	 * GUI starts checking for updates on Thursday at 12:00 (exactly 3 days later)
	 * Cache is 2 days 23 hours 59 minutes old
	 * Backend sees it's not 3 days old, does nothing
	 */
	if (cache_age != G_MAXUINT && cache_age > cache_age_offset)
		cache_age -= cache_age_offset;

	g_debug ("cache-age changed to %i", cache_age);
	job->priv->cache_age = cache_age;
}

/**
 * pk_backend_job_set_user_data:
 **/
void
pk_backend_job_set_user_data (PkBackendJob *job, gpointer user_data)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->user_data = user_data;
}

/**
 * pk_backend_job_get_background:
 **/
gboolean
pk_backend_job_get_background (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	return job->priv->background;
}

/**
 * pk_backend_job_set_background:
 **/
void
pk_backend_job_set_background (PkBackendJob *job, gboolean background)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->background = background;
}

/**
 * pk_backend_job_get_interactive:
 **/
gboolean
pk_backend_job_get_interactive (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	return job->priv->interactive;
}

/**
 * pk_backend_job_set_interactive:
 **/
void
pk_backend_job_set_interactive (PkBackendJob *job, gboolean interactive)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->interactive = interactive;
}

/**
 * pk_backend_job_get_role:
 **/
PkRoleEnum
pk_backend_job_get_role (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), PK_ROLE_ENUM_UNKNOWN);
	return job->priv->role;
}

/**
 * pk_backend_job_get_runtime:
 *
 * Return value: time running in ms
 */
guint
pk_backend_job_get_runtime (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), 0);
	return g_timer_elapsed (job->priv->timer, NULL) * 1000;
}

/**
 * pk_backend_job_get_is_finished:
 **/
gboolean
pk_backend_job_get_is_finished (PkBackendJob *job)
{
	return job->priv->finished;
}

/**
 * pk_backend_job_get_is_error_set:
 **/
gboolean
pk_backend_job_get_is_error_set (PkBackendJob *job)
{
	return job->priv->set_error;
}

/* used to call vfuncs in the main daemon thread */
typedef struct {
	PkBackendJob		*job;
	PkBackendJobSignal	 signal_kind;
	GObject			*object;
	GDestroyNotify		 destroy_func;
} PkBackendJobVFuncHelper;

/**
 * pk_backend_job_signal_to_string:
 **/
static const gchar *
pk_backend_job_signal_to_string (PkBackendJobSignal id)
{
	if (id == PK_BACKEND_SIGNAL_ALLOW_CANCEL)
		return "AllowCancel";
	if (id == PK_BACKEND_SIGNAL_DETAILS)
		return "Details";
	if (id == PK_BACKEND_SIGNAL_ERROR_CODE)
		return "ErrorCode";
	if (id == PK_BACKEND_SIGNAL_DISTRO_UPGRADE)
		return "DistroUpgrade";
	if (id == PK_BACKEND_SIGNAL_FINISHED)
		return "Finished";
	if (id == PK_BACKEND_SIGNAL_PACKAGE)
		return "Package";
	if (id == PK_BACKEND_SIGNAL_ITEM_PROGRESS)
		return "ItemProgress";
	if (id == PK_BACKEND_SIGNAL_FILES)
		return "Files";
	if (id == PK_BACKEND_SIGNAL_PERCENTAGE)
		return "Percentage";
	if (id == PK_BACKEND_SIGNAL_SPEED)
		return "Speed";
	if (id == PK_BACKEND_SIGNAL_DOWNLOAD_SIZE_REMAINING)
		return "DownloadSizeRemaining";
	if (id == PK_BACKEND_SIGNAL_REPO_DETAIL)
		return "RepoDetail";
	if (id == PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED)
		return "RepoSignatureRequired";
	if (id == PK_BACKEND_SIGNAL_EULA_REQUIRED)
		return "EulaRequired";
	if (id == PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED)
		return "MediaChangeRequired";
	if (id == PK_BACKEND_SIGNAL_REQUIRE_RESTART)
		return "RequireRestart";
	if (id == PK_BACKEND_SIGNAL_STATUS_CHANGED)
		return "StatusChanged";
	if (id == PK_BACKEND_SIGNAL_LOCKED_CHANGED)
		return "LockedChanged";
	if (id == PK_BACKEND_SIGNAL_UPDATE_DETAIL)
		return "UpdateDetail";
	if (id == PK_BACKEND_SIGNAL_CATEGORY)
		return "Category";
	return NULL;
}

/**
 * pk_backend_job_vfunc_event_free:
 **/
static void
pk_backend_job_vfunc_event_free (PkBackendJobVFuncHelper *helper)
{
	if (helper->destroy_func != NULL)
		helper->destroy_func (helper->object);
	g_object_unref (helper->job);
	g_free (helper);
}

/**
 * pk_backend_job_call_vfunc_idle_cb:
 **/
static gboolean
pk_backend_job_call_vfunc_idle_cb (gpointer user_data)
{
	PkBackendJobVFuncHelper *helper = (PkBackendJobVFuncHelper *) user_data;
	PkBackendJobVFuncItem *item;

	/* call transaction vfunc on main thread */
	item = &helper->job->priv->vfunc_items[helper->signal_kind];
	if (item != NULL && item->vfunc != NULL) {
		item->vfunc (helper->job, helper->object, item->user_data);
	} else {
		g_warning ("tried to do signal %s when no longer connected",
			   pk_backend_job_signal_to_string (helper->signal_kind));
	}
	return FALSE;
}

/**
 * pk_backend_job_call_vfunc:
 *
 * This method can be called in any thread, and the vfunc is guaranteed
 * to be called idle in the main thread.
 **/
static void
pk_backend_job_call_vfunc (PkBackendJob *job,
			   PkBackendJobSignal signal_kind,
			   gpointer object,
			   GDestroyNotify destroy_func)
{
	PkBackendJobVFuncHelper *helper;
	PkBackendJobVFuncItem *item;
	guint priority = G_PRIORITY_DEFAULT_IDLE;
	_cleanup_source_unref_ GSource *source = NULL;

	/* call transaction vfunc if not disabled and set */
	item = &job->priv->vfunc_items[signal_kind];
	if (!item->enabled || item->vfunc == NULL)
		return;

	/* order this last if others are still pending */
	if (signal_kind == PK_BACKEND_SIGNAL_FINISHED)
		priority = G_PRIORITY_LOW;

	/* emit idle */
	helper = g_new0 (PkBackendJobVFuncHelper, 1);
	helper->job = g_object_ref (job);
	helper->signal_kind = signal_kind;
	helper->object = object;
	helper->destroy_func = destroy_func;
	source = g_idle_source_new ();
	g_source_set_priority (source, priority);
	g_source_set_callback (source,
			       pk_backend_job_call_vfunc_idle_cb,
			       helper,
			       (GDestroyNotify) pk_backend_job_vfunc_event_free);
	g_source_set_name (source, "[PkBackendJob] idle_event_cb");
	g_source_attach (source, NULL);
}

/**
 * pk_backend_job_set_vfunc:
 * @job: A valid PkBackendJob
 * @signal_kind: Kind of the backend signal we want to connect
 * @vfunc: (scope call): The function we want to call
 * @user_data: User data we want to pass to the callback
 *
 * Connect backend
 **/
void
pk_backend_job_set_vfunc (PkBackendJob *job,
			  PkBackendJobSignal signal_kind,
			  PkBackendJobVFunc vfunc,
			  gpointer user_data)
{
	PkBackendJobVFuncItem *item;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	item = &job->priv->vfunc_items[signal_kind];
	item->enabled = TRUE;
	item->vfunc = vfunc;
	item->user_data = user_data;
}

/**
 * pk_backend_job_set_role:
 **/
void
pk_backend_job_set_role (PkBackendJob *job, PkRoleEnum role)
{
	/* Should only be called once... */
	if (job->priv->role != PK_ROLE_ENUM_UNKNOWN &&
	    job->priv->role != role) {
		g_warning ("cannot set role to %s, already %s",
			   pk_role_enum_to_string (role),
			   pk_role_enum_to_string (job->priv->role));
	}

	g_timer_reset (job->priv->timer);
	job->priv->role = role;
	job->priv->status = PK_STATUS_ENUM_WAIT;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_STATUS_CHANGED,
				   GUINT_TO_POINTER (job->priv->status),
				   NULL);
}

/**
 * pk_backend_job_set_locked:
 *
 * Set if your backend job currently locks the cache, so no other tool will
 * have write  access on it. (read-only transactions will still be permitted)
 **/
void
pk_backend_job_set_locked (PkBackendJob *job, gboolean locked)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	job->priv->locked = locked;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_LOCKED_CHANGED,
				   GUINT_TO_POINTER (job->priv->locked),
				   NULL);
}

/**
 * pk_backend_job_get_locked:
 **/
gboolean
pk_backend_job_get_locked (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	return job->priv->locked;
}

/* simple helper to work around the GThread one pointer limit */
typedef struct {
	PkBackend		*backend;
	PkBackendJob		*job;
	PkBackendJobThreadFunc	 func;
	gpointer		 user_data;
	GDestroyNotify		 destroy_func;
} PkBackendJobThreadHelper;

/**
 * pk_backend_job_thread_setup:
 **/
static gpointer
pk_backend_job_thread_setup (gpointer thread_data)
{
	PkBackendJobThreadHelper *helper = (PkBackendJobThreadHelper *) thread_data;

	/* run original function with automatic locking */
	pk_backend_thread_start (helper->backend, helper->job, helper->func);
	helper->func (helper->job, helper->job->priv->params, helper->user_data);
	pk_backend_job_finished (helper->job);
	pk_backend_thread_stop (helper->backend, helper->job, helper->func);

	/* set idle IO priority */
#ifdef PK_BUILD_DAEMON
	if (helper->job->priv->background == TRUE) {
		g_debug ("setting ioprio class to idle");
		pk_ioprio_set_idle (0);
	}
#endif

	/* unref the thread here as it holds a reference itself and we do
	 * not need to join() this at any stage */
	g_thread_unref (helper->job->priv->thread);

	/* destroy helper */
	g_object_unref (helper->job);
	if (helper->destroy_func != NULL)
		helper->destroy_func (helper->user_data);
	g_free (helper);

	/* no return value */
	return NULL;
}

/**
 * pk_backend_job_thread_create:
 * @func: (scope call):
 **/
gboolean
pk_backend_job_thread_create (PkBackendJob *job,
			      PkBackendJobThreadFunc func,
			      gpointer user_data,
			      GDestroyNotify destroy_func)
{
	PkBackendJobThreadHelper *helper = NULL;

	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (pk_is_thread_default (), FALSE);

	if (job->priv->thread != NULL) {
		g_warning ("already has thread");
		return FALSE;
	}

	/* create a helper object to allow us to call a _setup() function */
	helper = g_new0 (PkBackendJobThreadHelper, 1);
	helper->job = g_object_ref (job);
	helper->backend = job->priv->backend;
	helper->func = func;
	helper->user_data = user_data;

	/* create a thread */
	job->priv->thread = g_thread_new ("PK-Backend",
					  pk_backend_job_thread_setup,
					  helper);
	if (job->priv->thread == NULL) {
		g_warning ("failed to create thread");
		return FALSE;
	}
	return TRUE;
}

/**
 * pk_backend_job_set_percentage:
 **/
void
pk_backend_job_set_percentage (PkBackendJob *job, guint percentage)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: percentage %i", percentage);
		return;
	}

	/* set the same twice? */
	if (job->priv->percentage == percentage)
		return;

	/* check over */
	if (percentage > PK_BACKEND_PERCENTAGE_INVALID) {
		g_warning ("percentage value is invalid: %i", percentage);
		return;
	}

	/* check under */
	if (percentage < 100 &&
	    job->priv->percentage < 100 &&
	    percentage < job->priv->percentage) {
		g_warning ("percentage value is going down to %i from %i",
			   percentage, job->priv->percentage);
		return;
	}

	/* save in case we need this from coldplug */
	job->priv->percentage = percentage;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_PERCENTAGE,
				   GUINT_TO_POINTER (percentage),
				   NULL);
}

/**
 * pk_backend_job_set_speed:
 **/
void
pk_backend_job_set_speed (PkBackendJob *job, guint speed)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: speed %i", speed);
		return;
	}

	/* set the same twice? */
	if (job->priv->speed == speed)
		return;

	/* set new value */
	job->priv->speed = speed;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_SPEED,
				   GUINT_TO_POINTER (speed),
				   NULL);
}

/**
 * pk_backend_job_set_download_size_remaining:
 **/
void
pk_backend_job_set_download_size_remaining (PkBackendJob *job, guint64 download_size_remaining)
{
	guint64 *tmp;
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: download-size-remaining");
		return;
	}

	/* set the same twice? */
	if (job->priv->download_size_remaining == download_size_remaining)
		return;

	/* set new value */
	job->priv->download_size_remaining = download_size_remaining;

	/* we can't squash a 64bit value into a pointer on a 32bit arch */
	tmp = g_new0 (guint64, 1);
	*tmp = download_size_remaining;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_DOWNLOAD_SIZE_REMAINING,
				   tmp,
				   g_free);
}

/**
 * pk_backend_job_set_item_progress:
 **/
void
pk_backend_job_set_item_progress (PkBackendJob *job,
				  const gchar *package_id,
				  PkStatusEnum status,
				  guint percentage)
{
	PkItemProgress *item;
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: item-progress %i", percentage);
		return;
	}

	/* invalid number? */
	if (percentage > 100 && percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		g_debug ("invalid number %i", percentage);
		return;
	}

	/* emit */
	item = g_object_new (PK_TYPE_ITEM_PROGRESS,
			     "package-id", package_id,
			     "status", status,
			     "percentage", percentage,
			     NULL);
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_ITEM_PROGRESS,
				   g_object_ref (item),
				   g_object_unref);
	g_object_unref (item);
}

/**
 * pk_backend_job_set_status:
 **/
void
pk_backend_job_set_status (PkBackendJob *job, PkStatusEnum status)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* already this? */
	if (job->priv->status == status)
		return;

	/* have we already set an error? */
	if (job->priv->set_error && status != PK_STATUS_ENUM_FINISHED) {
		g_warning ("already set error: status %s",
			   pk_status_enum_to_string (status));
		return;
	}

	/* backends don't do this */
	if (status == PK_STATUS_ENUM_WAIT) {
		g_warning ("backend tried to status WAIT");
		return;
	}

	/* do we have to enumate a running call? */
	if (status != PK_STATUS_ENUM_RUNNING && status != PK_STATUS_ENUM_SETUP) {
		if (job->priv->status == PK_STATUS_ENUM_SETUP) {
			/* emit */
			pk_backend_job_call_vfunc (job,
						   PK_BACKEND_SIGNAL_STATUS_CHANGED,
						   GUINT_TO_POINTER (PK_STATUS_ENUM_RUNNING),
						   NULL);
		}
	}

	job->priv->status = status;

	/* don't emit some states when simulating */
	if (pk_bitfield_contain (job->priv->transaction_flags,
				 PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		switch (status) {
		case PK_STATUS_ENUM_DOWNLOAD:
		case PK_STATUS_ENUM_UPDATE:
		case PK_STATUS_ENUM_INSTALL:
		case PK_STATUS_ENUM_REMOVE:
		case PK_STATUS_ENUM_CLEANUP:
		case PK_STATUS_ENUM_OBSOLETE:
			return;
		default:
			break;
		}
	}

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_STATUS_CHANGED,
				   GUINT_TO_POINTER (status),
				   NULL);
}

/**
 * pk_backend_job_package:
 **/
void
pk_backend_job_package (PkBackendJob *job,
			PkInfoEnum info,
			const gchar *package_id,
			const gchar *summary)
{
	gboolean ret;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkPackage *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (package_id != NULL);

	/* check we are valid */
	item = pk_package_new ();
	ret = pk_package_set_id (item, package_id, &error);
	if (!ret) {
		g_warning ("package_id %s invalid and cannot be processed: %s",
			   package_id, error->message);
		return;
	}
	pk_package_set_info (item, info);
	pk_package_set_summary (item, summary);

	/* is it the same? */
	ret = (job->priv->last_package != NULL &&
	       pk_package_equal (job->priv->last_package, item));
	if (ret)
		return;

	/* update the 'last' package */
	if (job->priv->last_package != NULL)
		g_object_unref (job->priv->last_package);
	job->priv->last_package = g_object_ref (item);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: package %s", package_id);
		return;
	}

	/* we automatically set the transaction status  */
	if (info == PK_INFO_ENUM_DOWNLOADING)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
	else if (info == PK_INFO_ENUM_UPDATING)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	else if (info == PK_INFO_ENUM_INSTALLING)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_INSTALL);
	else if (info == PK_INFO_ENUM_REMOVING)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_REMOVE);
	else if (info == PK_INFO_ENUM_CLEANUP)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_CLEANUP);
	else if (info == PK_INFO_ENUM_OBSOLETING)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_OBSOLETE);

	/* we've sent a package for this transaction */
	job->priv->has_sent_package = TRUE;

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_PACKAGE,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_update_detail:
 **/
void
pk_backend_job_update_detail (PkBackendJob *job,
			      const gchar *package_id,
			      gchar **updates,
			      gchar **obsoletes,
			      gchar **vendor_urls,
			      gchar **bugzilla_urls,
			      gchar **cve_urls,
			      PkRestartEnum restart,
			      const gchar *update_text,
			      const gchar *changelog,
			      PkUpdateStateEnum state,
			      const gchar *issued_text,
			      const gchar *updated_text)
{
	GTimeVal timeval;
	_cleanup_object_unref_ PkUpdateDetail *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (package_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: update_detail %s", package_id);
		return;
	}

	/* check the dates are not empty */
	if (issued_text != NULL && issued_text[0] == '\0')
		issued_text = NULL;
	if (updated_text != NULL && updated_text[0] == '\0')
		updated_text = NULL;

	/* check the issued dates are valid */
	if (issued_text != NULL) {
		if (!g_time_val_from_iso8601 (issued_text, &timeval))
			g_warning ("failed to parse issued '%s'", issued_text);
	}
	if (updated_text != NULL) {
		if (!g_time_val_from_iso8601 (updated_text, &timeval))
			g_warning ("failed to parse updated '%s'", updated_text);
	}

	/* form PkUpdateDetail struct */
	item = pk_update_detail_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "updates", updates,
		      "obsoletes", obsoletes,
		      "vendor-urls", vendor_urls,
		      "bugzilla-urls", bugzilla_urls,
		      "cve-urls", cve_urls,
		      "restart", restart,
		      "update-text", update_text,
		      "changelog", changelog,
		      "state", state,
		      "issued", issued_text,
		      "updated", updated_text,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_UPDATE_DETAIL,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_require_restart:
 **/
void
pk_backend_job_require_restart (PkBackendJob *job,
				PkRestartEnum restart,
				const gchar *package_id)
{
	_cleanup_object_unref_ PkRequireRestart *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: require-restart %s", pk_restart_enum_to_string (restart));
		return;
	}

	/* check we are valid */
	if (!pk_package_id_check (package_id)) {
		g_warning ("package_id invalid and cannot be processed: %s", package_id);
		return;
	}

	/* form PkRequireRestart struct */
	item = pk_require_restart_new ();
	g_object_set (item,
		      "restart", restart,
		      "package-id", package_id,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_REQUIRE_RESTART,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_details:
 **/
void
pk_backend_job_details (PkBackendJob *job,
			const gchar *package_id,
			const gchar *summary,
			const gchar *license,
			PkGroupEnum group,
			const gchar *description,
			const gchar *url,
			gulong size)
{
	_cleanup_object_unref_ PkDetails *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (package_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: details %s", package_id);
		return;
	}

	/* form PkDetails struct */
	item = pk_details_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "summary", summary,
		      "license", license,
		      "group", group,
		      "description", description,
		      "url", url,
		      "size", (guint64) size,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
			       PK_BACKEND_SIGNAL_DETAILS,
			       g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_files:
 *
 * package_id is NULL when we are using this as a calback from DownloadPackages
 **/
void
pk_backend_job_files (PkBackendJob *job,
		      const gchar *package_id,
		      gchar **files)
{
	_cleanup_object_unref_ PkFiles *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (files != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: files %s", package_id);
		return;
	}

	/* check we are valid if specified */
	if (package_id != NULL) {
		if (!pk_package_id_check (package_id)) {
			g_warning ("package_id invalid and cannot be processed: %s", package_id);
			return;
		}
	}

	/* form PkFiles struct */
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_FILES,
				   g_object_ref (item),
				   g_object_unref);

	/* success */
	job->priv->download_files++;
}

/**
 * pk_backend_job_distro_upgrade:
 **/
void
pk_backend_job_distro_upgrade (PkBackendJob *job,
			       PkDistroUpgradeEnum state,
			       const gchar *name,
			       const gchar *summary)
{
	_cleanup_object_unref_ PkDistroUpgrade *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (state != PK_DISTRO_UPGRADE_ENUM_UNKNOWN);
	g_return_if_fail (name != NULL);
	g_return_if_fail (summary != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: distro-upgrade");
		return;
	}

	/* form PkDistroUpgrade struct */
	item = pk_distro_upgrade_new ();
	g_object_set (item,
		      "state", state,
		      "name", name,
		      "summary", summary,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_DISTRO_UPGRADE,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_repo_signature_required:
 **/
void
pk_backend_job_repo_signature_required (PkBackendJob *job,
					const gchar *package_id,
					const gchar *repository_name,
					const gchar *key_url,
					const gchar *key_userid,
					const gchar *key_id,
					const gchar *key_fingerprint,
					const gchar *key_timestamp,
					PkSigTypeEnum type)
{
	_cleanup_object_unref_ PkRepoSignatureRequired *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (repository_name != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: repo-sig-reqd");
		return;
	}

	/* check we don't do this more than once */
	if (job->priv->set_signature) {
		g_warning ("already asked for a signature, cannot process");
		return;
	}

	/* form PkRepoSignatureRequired struct */
	item = pk_repo_signature_required_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "repository-name", repository_name,
		      "key-url", key_url,
		      "key-userid", key_userid,
		      "key-id", key_id,
		      "key-fingerprint", key_fingerprint,
		      "key-timestamp", key_timestamp,
		      "type", type,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED,
				   g_object_ref (item),
				   g_object_unref);

	/* success */
	job->priv->set_signature = TRUE;
}

/**
 * pk_backend_job_eula_required:
 **/
void
pk_backend_job_eula_required (PkBackendJob *job,
			      const gchar *eula_id,
			      const gchar *package_id,
			      const gchar *vendor_name,
			      const gchar *license_agreement)
{
	_cleanup_object_unref_ PkEulaRequired *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (eula_id != NULL);
	g_return_if_fail (package_id != NULL);
	g_return_if_fail (vendor_name != NULL);
	g_return_if_fail (license_agreement != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: eula required");
		return;
	}

	/* check we don't do this more than once */
	if (job->priv->set_eula) {
		g_warning ("already asked for a signature, cannot process");
		return;
	}

	/* form PkEulaRequired struct */
	item = pk_eula_required_new ();
	g_object_set (item,
		      "eula-id", eula_id,
		      "package-id", package_id,
		      "vendor-name", vendor_name,
		      "license-agreement", license_agreement,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_EULA_REQUIRED,
				   g_object_ref (item),
				   g_object_unref);

	/* success */
	job->priv->set_eula = TRUE;
}

/**
 * pk_backend_job_media_change_required:
 **/
void
pk_backend_job_media_change_required (PkBackendJob *job,
				      PkMediaTypeEnum media_type,
				      const gchar *media_id,
				      const gchar *media_text)
{
	_cleanup_object_unref_ PkMediaChangeRequired *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (media_id != NULL);
	g_return_if_fail (media_text != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: media change required");
		return;
	}

	/* form PkMediaChangeRequired struct */
	item = pk_media_change_required_new ();
	g_object_set (item,
		      "media-type", media_type,
		      "media-id", media_id,
		      "media-text", media_text,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_repo_detail:
 **/
void
pk_backend_job_repo_detail (PkBackendJob *job,
			    const gchar *repo_id,
			    const gchar *description,
			    gboolean enabled)
{
	_cleanup_object_unref_ PkRepoDetail *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (repo_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: repo-detail %s", repo_id);
		return;
	}

	/* form PkRepoDetail struct */
	item = pk_repo_detail_new ();
	g_object_set (item,
		      "repo-id", repo_id,
		      "description", description,
		      "enabled", enabled,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_REPO_DETAIL,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_category:
 **/
void
pk_backend_job_category (PkBackendJob *job,
			 const gchar *parent_id,
			 const gchar *cat_id,
			 const gchar *name,
			 const gchar *summary,
			 const gchar *icon)
{
	_cleanup_object_unref_ PkCategory *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (cat_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error: category %s", cat_id);
		return;
	}

	/* form PkCategory struct */
	item = pk_category_new ();
	g_object_set (item,
		      "parent-id", parent_id,
		      "cat-id", cat_id,
		      "name", name,
		      "summary", summary,
		      "icon", icon,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_CATEGORY,
				   g_object_ref (item),
				   g_object_unref);
}

/**
 * pk_backend_job_error_code_is_need_untrusted:
 **/
static gboolean
pk_backend_job_error_code_is_need_untrusted (PkErrorEnum error_code)
{
	gboolean ret = FALSE;
	switch (error_code) {
		case PK_ERROR_ENUM_GPG_FAILURE:
		case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
		case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
		case PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED:
		case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
			ret = TRUE;
			break;
		default:
			break;
	}
	return ret;
}

/**
 * pk_backend_job_error_code:
 **/
void
pk_backend_job_error_code (PkBackendJob *job,
			   PkErrorEnum error_code,
			   const gchar *format, ...)
{
	va_list args;
	gboolean need_untrusted;
	_cleanup_free_ gchar *buffer = NULL;
	_cleanup_object_unref_ PkError *error = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	/* did we set a duplicate error? (we can override LOCK_REQUIRED errors,
	 * so the transaction list can fail transactions) */
	if (job->priv->set_error) {
		if (job->priv->last_error_code == PK_ERROR_ENUM_LOCK_REQUIRED) {
			/* reset the exit status, we're resetting the error now */
			job->priv->exit = PK_EXIT_ENUM_UNKNOWN;
			job->priv->finished = FALSE;
		} else {
			g_warning ("More than one error emitted! You tried to set '%s'", buffer);
			return;
		}
	}
	job->priv->set_error = TRUE;

	/* some error codes have a different exit code */
	need_untrusted = pk_backend_job_error_code_is_need_untrusted (error_code);
	if (need_untrusted)
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_NEED_UNTRUSTED);
	else if (error_code == PK_ERROR_ENUM_CANCELLED_PRIORITY)
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_CANCELLED_PRIORITY);
	else if (job->priv->exit == PK_EXIT_ENUM_UNKNOWN)
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_FAILED);

	/* set the hint that RepairSystem is needed */
	if (error_code == PK_ERROR_ENUM_UNFINISHED_TRANSACTION) {
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_REPAIR_REQUIRED);
	}

	/* save so we can check the parallel failure later */
	job->priv->last_error_code = error_code;

	/* form PkError struct */
	error = pk_error_new ();
	g_object_set (error,
		      "code", error_code,
		      "details", buffer,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_ERROR_CODE,
				   g_object_ref (error),
				   g_object_unref);
}

/**
 * pk_backend_job_has_set_error_code:
 **/
gboolean
pk_backend_job_has_set_error_code (PkBackendJob *job)
{
	return job->priv->set_error;
}

/**
 * pk_backend_job_set_started:
 **/
void
pk_backend_job_set_started (PkBackendJob *job, gboolean started)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->started = started;
}

/**
 * pk_backend_job_get_started:
 **/
gboolean
pk_backend_job_get_started (PkBackendJob *job)
{
	return job->priv->started;
}

/**
 * pk_backend_job_set_allow_cancel:
 **/
void
pk_backend_job_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error && allow_cancel) {
		g_warning ("already set error: allow-cancel %i", allow_cancel);
		return;
	}

	/* same as last state? */
	if (job->priv->allow_cancel == allow_cancel)
		return;

	/* emit */
	job->priv->allow_cancel = allow_cancel;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_ALLOW_CANCEL,
				   GUINT_TO_POINTER (allow_cancel),
				   NULL);
}

/**
 * pk_backend_job_get_allow_cancel:
 **/
gboolean
pk_backend_job_get_allow_cancel (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	return job->priv->allow_cancel;
}

/**
 * pk_backend_job_not_implemented_yet:
 **/
void
pk_backend_job_not_implemented_yet (PkBackendJob *job, const gchar *method)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (method != NULL);

	pk_backend_job_error_code (job,
				   PK_ERROR_ENUM_NOT_SUPPORTED,
				   "the method '%s' is not implemented yet",
				   method);

	/* don't wait, do this now */
	pk_backend_job_finished (job);
}

/**
 * pk_backend_job_set_exit_code:
 *
 * Should only be used internally, or from PkRunner when setting CANCELLED.
 **/
void
pk_backend_job_set_exit_code (PkBackendJob *job, PkExitEnum exit_enum)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	if (job->priv->exit != PK_EXIT_ENUM_UNKNOWN) {
		g_warning ("already set exit status: old=%s, new=%s",
			    pk_exit_enum_to_string (job->priv->exit),
			    pk_exit_enum_to_string (exit_enum));
		return;
	}

	/* new value */
	job->priv->exit = exit_enum;
}

/**
 * pk_backend_job_get_exit_code:
 **/
PkExitEnum
pk_backend_job_get_exit_code (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), PK_EXIT_ENUM_UNKNOWN);
	return job->priv->exit;
}

/**
 * pk_backend_job_finished:
 **/
void
pk_backend_job_finished (PkBackendJob *job)
{
	const gchar *role_text;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* check we have not already finished */
	if (job->priv->finished) {
		g_warning ("already finished");
		return;
	}

	/* find out what we just did */
	role_text = pk_role_enum_to_string (job->priv->role);

	/* ensure the same number of ::Files() were sent as packages for DownloadPackages */
	if (!job->priv->set_error &&
	    job->priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    job->priv->download_files == 0) {
		g_warning ("required multiple Files() for each package_id!");
	}

	/* check we sent at least one status calls */
	if (job->priv->set_error == FALSE &&
	    job->priv->status == PK_STATUS_ENUM_SETUP) {
		g_warning ("required status signals for %s!", role_text);
	}

	/* drop any inhibits */
	pk_backend_job_set_allow_cancel (job, TRUE);

	/* mark as finished for the UI that might only be watching status */
	pk_backend_job_set_status (job, PK_STATUS_ENUM_FINISHED);

	/* we can't ever be re-used */
	job->priv->finished = TRUE;

	/* this wasn't set otherwise, assume success */
	if (job->priv->exit == PK_EXIT_ENUM_UNKNOWN)
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_SUCCESS);

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_FINISHED,
				   GUINT_TO_POINTER (job->priv->exit),
				   NULL);
}

/**
 * pk_backend_job_finalize:
 **/
static void
pk_backend_job_finalize (GObject *object)
{
	PkBackendJob *job;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_BACKEND_JOB (object));
	g_return_if_fail (pk_is_thread_default ());
	job = PK_BACKEND_JOB (object);

	if (pk_backend_job_get_started (job)) {
		g_warning ("finalized job without stopping it before");
		pk_backend_stop_job (job->priv->backend, job);
	}

	g_free (job->priv->proxy_http);
	g_free (job->priv->proxy_https);
	g_free (job->priv->proxy_ftp);
	g_free (job->priv->proxy_socks);
	g_free (job->priv->no_proxy);
	g_free (job->priv->pac);
	g_free (job->priv->cmdline);
	g_free (job->priv->locale);
	g_free (job->priv->frontend_socket);
	if (job->priv->last_package != NULL) {
		g_object_unref (job->priv->last_package);
		job->priv->last_package = NULL;
	}
	if (job->priv->params != NULL)
		g_variant_unref (job->priv->params);
	g_timer_destroy (job->priv->timer);
	g_key_file_unref (job->priv->conf);
	g_object_unref (job->priv->cancellable);

	G_OBJECT_CLASS (pk_backend_job_parent_class)->finalize (object);
}

/**
 * pk_backend_job_class_init:
 **/
static void
pk_backend_job_class_init (PkBackendJobClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_backend_job_finalize;
	g_type_class_add_private (klass, sizeof (PkBackendJobPrivate));
}

/**
 * pk_backend_job_init:
 **/
static void
pk_backend_job_init (PkBackendJob *job)
{
	job->priv = PK_BACKEND_JOB_GET_PRIVATE (job);
	job->priv->timer = g_timer_new ();
	job->priv->cancellable = g_cancellable_new ();
	job->priv->last_error_code = PK_ERROR_ENUM_UNKNOWN;
	pk_backend_job_reset (job);
}

/**
 * pk_backend_job_new:
 *
 * Return value: A new job class instance.
 **/
PkBackendJob *
pk_backend_job_new (GKeyFile *conf)
{
	PkBackendJob *job;
	job = g_object_new (PK_TYPE_BACKEND_JOB, NULL);
	job->priv->conf = g_key_file_ref (conf);
	return PK_BACKEND_JOB (job);
}

