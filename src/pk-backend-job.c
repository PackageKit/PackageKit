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

#include "pk-backend.h"
#include "pk-backend-job.h"
#include "pk-conf.h"
#include "pk-shared.h"
#include "pk-time.h"

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
	PkBackend		*backend;
	PkBackendJobVFuncItem	 vfunc_items[PK_BACKEND_SIGNAL_LAST];
	PkBitfield		 transaction_flags;
	PkConf			*conf;
	PkExitEnum		 exit;
	PkHintEnum		 allow_cancel;
	PkHintEnum		 background;
	PkHintEnum		 interactive;
	gboolean		 locked;
	PkPackage		*last_package;
	PkResults		*results;
	PkRoleEnum		 role;
	PkStatusEnum		 status;
	PkTime			*time;
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
	job->priv->user_data = NULL;
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
 * pk_backend_job_get_backend:
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
gboolean
pk_backend_job_set_frontend_socket (PkBackendJob *job, const gchar *frontend_socket)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);

	g_debug ("frontend_socket changed to %s", frontend_socket);
	g_free (job->priv->frontend_socket);
	job->priv->frontend_socket = g_strdup (frontend_socket);

	return TRUE;
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
	if (cache_age > cache_age_offset)
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
PkHintEnum
pk_backend_job_get_background (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), PK_HINT_ENUM_UNSET);
	return job->priv->background;
}

/**
 * pk_backend_job_set_background:
 **/
void
pk_backend_job_set_background (PkBackendJob *job, PkHintEnum background)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	job->priv->background = background;
}

/**
 * pk_backend_job_get_interactive:
 **/
PkHintEnum
pk_backend_job_get_interactive (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), PK_HINT_ENUM_UNSET);
	return job->priv->interactive;
}

/**
 * pk_backend_job_set_interactive:
 **/
void
pk_backend_job_set_interactive (PkBackendJob *job, PkHintEnum interactive)
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
 * Returns time running in ms
 */
guint
pk_backend_job_get_runtime (PkBackendJob *job)
{
	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), 0);
	return pk_time_get_elapsed (job->priv->time);
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
} PkBackendJobVFuncHelper;

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
	if (item != NULL) {
		item->vfunc (helper->job,
			     helper->object,
			     item->user_data);
	} else {
		g_warning ("tried to do signal %i when no longer connected",
			   helper->signal_kind);
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
			   GObject *object)
{
	PkBackendJobVFuncHelper *helper;
	PkBackendJobVFuncItem *item;

	/* call transaction vfunc if not disabled and set */
	item = &job->priv->vfunc_items[signal_kind];
	if (!item->enabled || item->vfunc == NULL)
		return;

	/* emit idle, TODO: do we ever need to cancel this? */
	helper = g_new0 (PkBackendJobVFuncHelper, 1);
	helper->job = job;
	helper->signal_kind = signal_kind;
	helper->object = object;
	g_idle_add_full (G_PRIORITY_HIGH_IDLE,
			 pk_backend_job_call_vfunc_idle_cb,
			 helper,
			 g_free);
}

/**
 * pk_backend_job_set_vfunc:
 * @backend: A valid PkBackend instance
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

	/* reset the timer */
	pk_time_reset (job->priv->time);

	g_debug ("setting role to %s", pk_role_enum_to_string (role));
	job->priv->role = role;
	job->priv->status = PK_STATUS_ENUM_WAIT;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_STATUS_CHANGED,
				   GUINT_TO_POINTER (job->priv->status));
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
				   GUINT_TO_POINTER (job->priv->locked));
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

	/* run original function */
	helper->func (helper->job, helper->job->priv->params, helper->user_data);

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
 *
 * @func: (scope call):
 **/
gboolean
pk_backend_job_thread_create (PkBackendJob *job,
			      PkBackendJobThreadFunc func,
			      gpointer user_data,
			      GDestroyNotify destroy_func)
{
	gboolean ret = TRUE;
	PkBackendJobThreadHelper *helper = NULL;

	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (job->priv->thread != NULL) {
		g_warning ("already has thread");
		return FALSE;
	}

	/* create a helper object to allow us to call a _setup() function */
	helper = g_new0 (PkBackendJobThreadHelper, 1);
	helper->job = g_object_ref (job);
	helper->func = func;
	helper->user_data = user_data;

	/* create a thread */
#if GLIB_CHECK_VERSION(2,31,0)
	job->priv->thread = g_thread_new ("PK-Backend",
					  pk_backend_job_thread_setup,
					  helper);
#else
	job->priv->thread = g_thread_create (pk_backend_job_thread_setup,
					     helper,
					     FALSE,
					     NULL);
#endif
	if (job->priv->thread == NULL) {
		g_warning ("failed to create thread");
		ret = FALSE;
		goto out;
	}
out:
	return ret;
}

/**
 * pk_backend_job_set_percentage:
 **/
void
pk_backend_job_set_percentage (PkBackendJob *job, guint percentage)
{
	guint remaining;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: percentage %i", percentage);
		return;
	}

	/* set the same twice? */
	if (job->priv->percentage == percentage) {
		g_debug ("duplicate set of %i", percentage);
		return;
	}

	/* check over */
	if (percentage > PK_BACKEND_PERCENTAGE_INVALID) {
		pk_backend_job_message (job, PK_MESSAGE_ENUM_BACKEND_ERROR,
					"percentage value is invalid: %i",
					percentage);
		return;
	}

	/* check under */
	if (percentage < 100 &&
	    job->priv->percentage < 100 &&
	    percentage < job->priv->percentage) {
		pk_backend_job_message (job, PK_MESSAGE_ENUM_BACKEND_ERROR,
					"percentage value is going down to %i from %i",
					percentage,
					job->priv->percentage);
		return;
	}

	/* save in case we need this from coldplug */
	job->priv->percentage = percentage;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_PERCENTAGE,
				   GUINT_TO_POINTER (percentage));

	/* only compute time if we have data */
	if (percentage != PK_BACKEND_PERCENTAGE_INVALID) {
		/* needed for time remaining calculation */
		pk_time_add_data (job->priv->time, percentage);

		/* lets try this and print as debug */
		remaining = pk_time_get_remaining (job->priv->time);
		g_debug ("this will now take ~%i seconds", remaining);
		job->priv->remaining = remaining;
		pk_backend_job_call_vfunc (job,
					   PK_BACKEND_SIGNAL_REMAINING,
					   GUINT_TO_POINTER (remaining));
	}

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
		g_warning ("already set error, cannot process: speed %i", speed);
		return;
	}

	/* set the same twice? */
	if (job->priv->speed == speed) {
		g_debug ("duplicate set of %i", speed);
		return;
	}

	/* set new value */
	job->priv->speed = speed;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_SPEED,
				   GUINT_TO_POINTER (speed));
}

/**
 * pk_backend_job_set_download_size_remaining:
 **/
void
pk_backend_job_set_download_size_remaining (PkBackendJob *job, guint64 download_size_remaining)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: download-size-remaining");
		return;
	}

	/* set the same twice? */
	if (job->priv->download_size_remaining == download_size_remaining) {
		g_debug ("duplicate set of download_size_remaining");
		return;
	}

	/* set new value */
	job->priv->download_size_remaining = download_size_remaining;
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_DOWNLOAD_SIZE_REMAINING,
				   GUINT_TO_POINTER (download_size_remaining));
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
		g_warning ("already set error, cannot process: item-progress %i", percentage);
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
				   G_OBJECT (item));
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
	if (job->priv->status == status) {
		g_debug ("already set same status");
		return;
	}

	/* have we already set an error? */
	if (job->priv->set_error && status != PK_STATUS_ENUM_FINISHED) {
		g_warning ("already set error, cannot process: status %s",
			   pk_status_enum_to_string (status));
		return;
	}

	/* backends don't do this */
	if (status == PK_STATUS_ENUM_WAIT) {
		g_warning ("backend tried to WAIT, only the runner should set this value");
		pk_backend_job_message (job,
					PK_MESSAGE_ENUM_BACKEND_ERROR,
					"%s shouldn't use STATUS_WAIT",
					pk_role_enum_to_string (job->priv->role));
		return;
	}

	/* do we have to enumate a running call? */
	if (status != PK_STATUS_ENUM_RUNNING && status != PK_STATUS_ENUM_SETUP) {
		if (job->priv->status == PK_STATUS_ENUM_SETUP) {
			/* emit */
			pk_backend_job_call_vfunc (job,
						   PK_BACKEND_SIGNAL_STATUS_CHANGED,
						   GUINT_TO_POINTER (PK_STATUS_ENUM_RUNNING));
		}
	}

	job->priv->status = status;

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_STATUS_CHANGED,
				   GUINT_TO_POINTER (status));
}

/**
 * pk_backend_job_package_emulate_finished:
 **/
static gboolean
pk_backend_job_package_emulate_finished (PkBackendJob *job)
{
	gboolean ret = FALSE;
	PkPackage *item;
	PkInfoEnum info;
	gchar *package_id = NULL;
	gchar *summary = NULL;

	/* simultaneous handles this on it's own */
	if (pk_backend_get_simultaneous_mode (job->priv->backend))
		goto out;

	/* first package in transaction */
	item = job->priv->last_package;
	if (item == NULL)
		goto out;

	/* get data */
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);

	/* already finished */
	if (info == PK_INFO_ENUM_FINISHED)
		goto out;

	/* only makes sense for some values */
	if (info == PK_INFO_ENUM_DOWNLOADING ||
	    info == PK_INFO_ENUM_UPDATING ||
	    info == PK_INFO_ENUM_INSTALLING ||
	    info == PK_INFO_ENUM_REMOVING ||
	    info == PK_INFO_ENUM_CLEANUP ||
	    info == PK_INFO_ENUM_OBSOLETING ||
	    info == PK_INFO_ENUM_REINSTALLING ||
	    info == PK_INFO_ENUM_DOWNGRADING) {
		pk_backend_job_package (job,
					PK_INFO_ENUM_FINISHED,
					package_id,
					summary);
		ret = TRUE;
	}
out:
	g_free (package_id);
	g_free (summary);
	return ret;
}

/**
 * pk_backend_job_package_emulate_finished_for_package:
 **/
static gboolean
pk_backend_job_package_emulate_finished_for_package (PkBackendJob *job, PkPackage *item)
{
	gboolean ret = FALSE;

	/* simultaneous handles this on it's own */
	if (pk_backend_get_simultaneous_mode (job->priv->backend)) {
		g_debug ("backend handling finished");
		goto out;
	}

	/* first package in transaction */
	if (job->priv->last_package == NULL) {
		g_debug ("first package, so no finished");
		goto out;
	}

	/* same package, just info change */
	if (pk_package_equal_id (job->priv->last_package, item)) {
		g_debug ("same package_id, ignoring");
		goto out;
	}

	/* emit the old package as finished */
	ret = pk_backend_job_package_emulate_finished (job);
out:
	return ret;
}

/**
 * pk_backend_strsafe:
 * @text: The input text to make safe
 *
 * Replaces chars in the text that may be dangerous, or that may print
 * incorrectly. These chars include new lines, tabs and line feed, and are
 * replaced by spaces.
 *
 * Return value: the new string with no insane chars
 **/
static gchar *
pk_backend_strsafe (const gchar *text)
{
	gchar *text_safe;
	gboolean ret;
	const gchar *delimiters;

	if (text == NULL)
		return NULL;

	/* is valid UTF8? */
	ret = g_utf8_validate (text, -1, NULL);
	if (!ret) {
		g_warning ("text '%s' was not valid UTF8!", text);
		return NULL;
	}

	/* rip out any insane characters */
	delimiters = "\\\f\r\t";
	text_safe = g_strdup (text);
	g_strdelimit (text_safe, delimiters, ' ');
	return text_safe;
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
	gchar *summary_safe = NULL;
	PkPackage *item = NULL;
	gboolean ret;
	GError *error = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (package_id != NULL);

	/* check we are valid */
	item = pk_package_new ();
	ret = pk_package_set_id (item, package_id, &error);
	if (!ret) {
		g_warning ("package_id %s invalid and cannot be processed: %s",
			   package_id, error->message);
		g_error_free (error);
		goto out;
	}

	/* replace unsafe chars */
	summary_safe = pk_backend_strsafe (summary);

	/* create a new package object AFTER we emulate the info value */
	g_object_set (item,
		      "info", info,
		      "summary", summary_safe,
		      NULL);

	/* is it the same? */
	ret = (job->priv->last_package != NULL && pk_package_equal (job->priv->last_package, item));
	if (ret) {
		g_debug ("skipping duplicate %s", package_id);
		goto out;
	}

	/* simulate the finish here when required */
	if (info != PK_INFO_ENUM_FINISHED)
		pk_backend_job_package_emulate_finished_for_package (job, item);

	/* update the 'last' package */
	if (job->priv->last_package != NULL)
		g_object_unref (job->priv->last_package);
	job->priv->last_package = g_object_ref (item);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: package %s", package_id);
		goto out;
	}

	/* we automatically set the transaction status for some PkInfoEnums if running
	 * in non-simultaneous transaction mode */
	if (!pk_backend_get_simultaneous_mode (job->priv->backend)) {
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
	}

	/* we've sent a package for this transaction */
	job->priv->has_sent_package = TRUE;

	/* emit */
	pk_backend_job_call_vfunc (job,
				PK_BACKEND_SIGNAL_PACKAGE,
				G_OBJECT (item));

	/* add to results if meaningful */
	if (info != PK_INFO_ENUM_FINISHED)
		pk_results_add_package (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (summary_safe);
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
	gchar *update_text_safe = NULL;
	PkUpdateDetail *item = NULL;
	GTimeVal timeval;
	gboolean ret;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (package_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: update_detail %s", package_id);
		goto out;
	}

	/* check the dates are not empty */
	if (issued_text != NULL && issued_text[0] == '\0')
		issued_text = NULL;
	if (updated_text != NULL && updated_text[0] == '\0')
		updated_text = NULL;

	/* check the issued dates are valid */
	if (issued_text != NULL) {
		ret = g_time_val_from_iso8601 (issued_text, &timeval);
		if (!ret)
			g_warning ("failed to parse issued '%s'", issued_text);
	}
	if (updated_text != NULL) {
		ret = g_time_val_from_iso8601 (updated_text, &timeval);
		if (!ret)
			g_warning ("failed to parse updated '%s'", updated_text);
	}

	/* replace unsafe chars */
	update_text_safe = pk_backend_strsafe (update_text);

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
		      "update-text", update_text_safe,
		      "changelog", changelog,
		      "state", state,
		      "issued", issued_text,
		      "updated", updated_text,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_UPDATE_DETAIL, G_OBJECT (item));
	pk_results_add_update_detail (job->priv->results, item);

	/* we parsed okay */
	ret = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (update_text_safe);
}

/**
 * pk_backend_job_require_restart:
 **/
void
pk_backend_job_require_restart (PkBackendJob *job,
				PkRestartEnum restart,
				const gchar *package_id)
{
	gboolean ret;
	PkRequireRestart *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: require-restart %s", pk_restart_enum_to_string (restart));
		goto out;
	}

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		g_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* form PkRequireRestart struct */
	item = pk_require_restart_new ();
	g_object_set (item,
		      "restart", restart,
		      "package-id", package_id,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_REQUIRE_RESTART, G_OBJECT (item));
	pk_results_add_require_restart (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
}

/**
 * pk_backend_job_message:
 **/
void
pk_backend_job_message (PkBackendJob *job,
			PkMessageEnum message,
			const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;
	PkMessage *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error && message != PK_MESSAGE_ENUM_BACKEND_ERROR) {
		g_warning ("already set error, cannot process: message %s", pk_message_enum_to_string (message));
		goto out;
	}

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* form PkMessage struct */
	item = pk_message_new ();
	g_object_set (item,
		      "type", message,
		      "details", buffer,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_MESSAGE, G_OBJECT (item));
	pk_results_add_message (job->priv->results, item);
out:
	g_free (buffer);
	if (item != NULL)
		g_object_unref (item);
}

/**
 * pk_backend_job_details:
 **/
void
pk_backend_job_details (PkBackendJob *job,
			const gchar *package_id,
			const gchar *license,
			PkGroupEnum group,
			const gchar *description,
			const gchar *url,
			gulong size)
{
	gchar *description_safe = NULL;
	PkDetails *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (package_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: details %s", package_id);
		goto out;
	}

	/* replace unsafe chars */
	description_safe = pk_backend_strsafe (description);

	/* form PkDetails struct */
	item = pk_details_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "license", license,
		      "group", group,
		      "description", description_safe,
		      "url", url,
		      "size", (guint64) size,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job,
			       PK_BACKEND_SIGNAL_DETAILS,
			       G_OBJECT (item));
	pk_results_add_details (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (description_safe);
}

/**
 * pk_backend_job_files:
 *
 * package_id is NULL when we are using this as a calback from DownloadPackages
 **/
void
pk_backend_job_files (PkBackendJob *job,
		      const gchar *package_id,
		      const gchar *filelist)
{
	gboolean ret;
	PkFiles *item = NULL;
	gchar **files = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (filelist != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: files %s", package_id);
		goto out;
	}

	/* check we are valid */
	ret = pk_package_id_check (package_id);
	if (!ret) {
		g_warning ("package_id invalid and cannot be processed: %s", package_id);
		goto out;
	}

	/* form PkFiles struct */
	files = g_strsplit (filelist, ";", -1);
	item = pk_files_new ();
	g_object_set (item,
		      "package-id", package_id,
		      "files", files,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_FILES, G_OBJECT (item));
	pk_results_add_files (job->priv->results, item);

	/* success */
	job->priv->download_files++;
out:
	g_strfreev (files);
	if (item != NULL)
		g_object_unref (item);
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
	gchar *name_safe = NULL;
	gchar *summary_safe = NULL;
	PkDistroUpgrade *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (state != PK_DISTRO_UPGRADE_ENUM_UNKNOWN);
	g_return_if_fail (name != NULL);
	g_return_if_fail (summary != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: distro-upgrade");
		goto out;
	}

	/* replace unsafe chars */
	name_safe = pk_backend_strsafe (name);
	summary_safe = pk_backend_strsafe (summary);

	/* form PkDistroUpgrade struct */
	item = pk_distro_upgrade_new ();
	g_object_set (item,
		      "state", state,
		      "name", name_safe,
		      "summary", summary_safe,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_DISTRO_UPGRADE, G_OBJECT (item));
	pk_results_add_distro_upgrade (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (name_safe);
	g_free (summary_safe);
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
	PkRepoSignatureRequired *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (repository_name != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: repo-sig-reqd");
		goto out;
	}

	/* check we don't do this more than once */
	if (job->priv->set_signature) {
		g_warning ("already asked for a signature, cannot process");
		goto out;
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
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED, G_OBJECT (item));
	pk_results_add_repo_signature_required (job->priv->results, item);

	/* success */
	job->priv->set_signature = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
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
	PkEulaRequired *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (eula_id != NULL);
	g_return_if_fail (package_id != NULL);
	g_return_if_fail (vendor_name != NULL);
	g_return_if_fail (license_agreement != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: eula required");
		goto out;
	}

	/* check we don't do this more than once */
	if (job->priv->set_eula) {
		g_warning ("already asked for a signature, cannot process");
		goto out;
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
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_EULA_REQUIRED, G_OBJECT (item));
	pk_results_add_eula_required (job->priv->results, item);

	/* success */
	job->priv->set_eula = TRUE;
out:
	if (item != NULL)
		g_object_unref (item);
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
	PkMediaChangeRequired *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (media_id != NULL);
	g_return_if_fail (media_text != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: media change required");
		goto out;
	}

	/* form PkMediaChangeRequired struct */
	item = pk_media_change_required_new ();
	g_object_set (item,
		      "media-type", media_type,
		      "media-id", media_id,
		      "media-text", media_text,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_MEDIA_CHANGE_REQUIRED, G_OBJECT (item));
	pk_results_add_media_change_required (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
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
	gchar *description_safe = NULL;
	PkRepoDetail *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (repo_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: repo-detail %s", repo_id);
		goto out;
	}

	/* replace unsafe chars */
	description_safe = pk_backend_strsafe (description);

	/* form PkRepoDetail struct */
	item = pk_repo_detail_new ();
	g_object_set (item,
		      "repo-id", repo_id,
		      "description", description,
		      "enabled", enabled,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_REPO_DETAIL, G_OBJECT (item));
	pk_results_add_repo_detail (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (description_safe);
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
	gchar *summary_safe = NULL;
	PkCategory *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));
	g_return_if_fail (cat_id != NULL);

	/* have we already set an error? */
	if (job->priv->set_error) {
		g_warning ("already set error, cannot process: category %s", cat_id);
		goto out;
	}

	/* replace unsafe chars */
	summary_safe = pk_backend_strsafe (summary);

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
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_CATEGORY, G_OBJECT (item));
	pk_results_add_category (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (summary_safe);
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
	gchar *buffer;
	gboolean need_untrusted;
	PkError *item = NULL;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	va_start (args, format);
	buffer = g_strdup_vprintf (format, args);
	va_end (args);

	/* did we set a duplicate error? */
	if (job->priv->set_error) {
		g_warning ("More than one error emitted! You tried to set '%s'", buffer);
		goto out;
	}
	job->priv->set_error = TRUE;

	/* some error codes have a different exit code */
	need_untrusted = pk_backend_job_error_code_is_need_untrusted (error_code);
	if (need_untrusted)
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_NEED_UNTRUSTED);
	else if (error_code == PK_ERROR_ENUM_CANCELLED_PRIORITY)
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_CANCELLED_PRIORITY);
	else
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_FAILED);

	/* set the hint that RepairSystem is needed */
	if (error_code == PK_ERROR_ENUM_UNFINISHED_TRANSACTION) {
		pk_backend_job_set_exit_code (job, PK_EXIT_ENUM_REPAIR_REQUIRED);
	}

	/* form PkError struct */
	item = pk_error_new ();
	g_object_set (item,
		      "code", error_code,
		      "details", buffer,
		      NULL);

	/* emit */
	pk_backend_job_call_vfunc (job, PK_BACKEND_SIGNAL_ERROR_CODE, G_OBJECT (item));
	pk_results_set_error_code (job->priv->results, item);
out:
	if (item != NULL)
		g_object_unref (item);
	g_free (buffer);
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
 * pk_backend_job_set_allow_cancel:
 **/
void
pk_backend_job_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel)
{
	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* have we already set an error? */
	if (job->priv->set_error && allow_cancel) {
		g_warning ("already set error, cannot process: allow-cancel %i", allow_cancel);
		return;
	}

	/* same as last state? */
	if (job->priv->allow_cancel == (PkHintEnum) allow_cancel) {
		g_debug ("ignoring same allow-cancel state");
		return;
	}

	/* emit */
	pk_backend_job_call_vfunc (job,
				   PK_BACKEND_SIGNAL_ALLOW_CANCEL,
				   GUINT_TO_POINTER (allow_cancel));
	job->priv->allow_cancel = allow_cancel;
}

/**
 * pk_backend_job_get_allow_cancel:
 **/
gboolean
pk_backend_job_get_allow_cancel (PkBackendJob *job)
{
	gboolean allow_cancel = FALSE;

	g_return_val_if_fail (PK_IS_BACKEND_JOB (job), FALSE);

	/* return FALSE if we never set state */
	if (job->priv->allow_cancel != PK_HINT_ENUM_UNSET)
		allow_cancel = job->priv->allow_cancel;

	return allow_cancel;
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
 * pk_backend_job_use_background:
 **/
gboolean
pk_backend_job_use_background (PkBackendJob *job)
{
	gboolean ret;

	/* check we are allowed */
	ret = pk_conf_get_bool (job->priv->conf, "UseIdleBandwidth");
	if (!ret)
		return FALSE;

	/* the session has set it one way or the other */
	if (job->priv->background == PK_HINT_ENUM_TRUE)
		return TRUE;
	if (job->priv->background == PK_HINT_ENUM_FALSE)
		return FALSE;

	return FALSE;
}

/**
 * pk_backend_job_finished:
 **/
void
pk_backend_job_finished (PkBackendJob *job)
{
	const gchar *role_text;

	g_return_if_fail (PK_IS_BACKEND_JOB (job));

	/* find out what we just did */
	role_text = pk_role_enum_to_string (job->priv->role);
	g_debug ("finished role %s", role_text);

	/* check we have not already finished */
	if (job->priv->finished) {
		g_warning ("already finished");
		return;
	}

	/* ensure the same number of ::Files() were sent as packages for DownloadPackages */
	if (!job->priv->set_error &&
	    job->priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES &&
	    job->priv->download_files == 0) {
		pk_backend_job_message (job, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send multiple Files() for each package_id!");
	}

	/* check we sent at least one status calls */
	if (job->priv->set_error == FALSE &&
	    job->priv->status == PK_STATUS_ENUM_SETUP) {
		pk_backend_job_message (job, PK_MESSAGE_ENUM_BACKEND_ERROR,
				    "Backends should send status <value> signals for %s!", role_text);
		g_warning ("GUI will remain unchanged!");
	}

	/* emulate the last finished package if not done already */
	pk_backend_job_package_emulate_finished (job);

	/* make any UI insensitive */
	pk_backend_job_set_allow_cancel (job, FALSE);

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
				   GUINT_TO_POINTER (job->priv->exit));
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
	job = PK_BACKEND_JOB (object);

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
	g_object_unref (job->priv->results);
	g_object_unref (job->priv->time);
	g_object_unref (job->priv->conf);

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
	job->priv->results = pk_results_new ();
	job->priv->time = pk_time_new ();
	job->priv->conf = pk_conf_new ();
	pk_backend_job_reset (job);
}

/**
 * pk_backend_job_new:
 * Return value: A new job class instance.
 **/
PkBackendJob *
pk_backend_job_new (void)
{
	PkBackendJob *job;
	job = g_object_new (PK_TYPE_BACKEND_JOB, NULL);
	return PK_BACKEND_JOB (job);
}

