/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:zif-download
 * @short_description: Generic object to download packages.
 *
 * This object is a trivial wrapper around libsoup.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <libsoup/soup.h>

#include "zif-config.h"
#include "zif-download.h"
#include "zif-completion.h"

#include "egg-debug.h"

#define ZIF_DOWNLOAD_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), ZIF_TYPE_DOWNLOAD, ZifDownloadPrivate))

/**
 * ZifDownloadPrivate:
 *
 * Private #ZifDownload data
 **/
struct _ZifDownloadPrivate
{
	gchar			*proxy;
	SoupSession		*session;
	SoupMessage		*msg;
	ZifCompletion		*completion;
	ZifConfig		*config;
};

static gpointer zif_download_object = NULL;

G_DEFINE_TYPE (ZifDownload, zif_download, G_TYPE_OBJECT)

/**
 * zif_download_file_got_chunk_cb:
 **/
static void
zif_download_file_got_chunk_cb (SoupMessage *msg, SoupBuffer *chunk, ZifDownload *download)
{
	guint percentage;
	guint header_size;
	guint body_length;

	/* get data */
	body_length = (guint) msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* how can this happen */
	if (header_size < body_length) {
		egg_warning ("length=%i/%i", body_length, header_size);
		goto out;
	}

	/* calulate percentage */
	percentage = (100 * body_length) / header_size;
	if (percentage == 100) {
		egg_warning ("ignoring percentage: %i", percentage);
		goto out;
	}

	egg_debug ("DOWNLOAD: %i%% (%i, %i) - %p, %p", percentage, body_length, header_size, msg, download);
	zif_completion_set_percentage (download->priv->completion, percentage);

out:
	return;
}

/**
 * zif_download_file_finished_cb:
 **/
static void
zif_download_file_finished_cb (SoupMessage *msg, ZifDownload *download)
{
	egg_debug ("done!");
	g_object_unref (download->priv->msg);
	download->priv->msg = NULL;
}

/**
 * zif_download_cancel:
 **/
gboolean
zif_download_cancel (ZifDownload *download, GError **error)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);

	if (download->priv->msg == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "no download in progress");
		goto out;
	}

	/* cancel */
	soup_session_cancel_message (download->priv->session, download->priv->msg, SOUP_STATUS_CANCELLED);
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_download_file:
 * @download: the #ZifDownload object
 * @uri: the full remote URI
 * @filename: the local filename to save to
 * @cancellable: a #GCancellable which is used to cancel tasks, or %NULL
 * @completion: a #ZifCompletion to use for progress reporting
 * @error: a #GError which is used on failure, or %NULL
 *
 * Downloads a file.
 *
 * Return value: %TRUE for success, %FALSE for failure
 **/
gboolean
zif_download_file (ZifDownload *download, const gchar *uri, const gchar *filename, GCancellable *cancellable, ZifCompletion *completion, GError **error)
{
	gboolean ret = FALSE;
	SoupURI *base_uri;
	SoupMessage *msg = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (download->priv->msg == NULL, FALSE);
	g_return_val_if_fail (download->priv->session != NULL, FALSE);

	/* save an instance of the completion object */
	download->priv->completion = g_object_ref (completion);

	base_uri = soup_uri_new (uri);
	if (base_uri == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "could not parse uri: %s", uri);
		goto out;
	}

	/* GET package */
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "could not setup message");
		goto out;
	}

	/* we want progress updates */
	g_signal_connect (msg, "got-chunk", G_CALLBACK (zif_download_file_got_chunk_cb), download);
	g_signal_connect (msg, "finished", G_CALLBACK (zif_download_file_finished_cb), download);

	/* we need this for cancelling */
	download->priv->msg = g_object_ref (msg);

	/* request */
//	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	/* send sync */
	soup_session_send_message (download->priv->session, msg);

	/* find length */
	if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to get valid response for %s: %s", uri, soup_status_get_phrase (msg->status_code));
		goto out;
	}

	/* write file */
	ret = g_file_set_contents (filename, msg->response_body->data, msg->response_body->length, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (1, 0, "failed to write file: %s",  error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_object_unref (download->priv->completion);
	download->priv->completion = NULL;
	if (base_uri != NULL)
		soup_uri_free (base_uri);
	if (msg != NULL)
		g_object_unref (msg);
	return ret;
}

/**
 * zif_download_set_proxy:
 **/
gboolean
zif_download_set_proxy (ZifDownload *download, const gchar *http_proxy, GError **error)
{
	gboolean ret = FALSE;
	SoupURI *proxy = NULL;
	guint connection_timeout;

	g_return_val_if_fail (ZIF_IS_DOWNLOAD (download), FALSE);

	/* get default value from the config file */
	connection_timeout = zif_config_get_uint (download->priv->config, "connection_timeout", NULL);
	if (connection_timeout == G_MAXUINT)
		connection_timeout = 5;

	/* setup the session */
	download->priv->session = soup_session_sync_new_with_options (SOUP_SESSION_PROXY_URI, proxy,
								      SOUP_SESSION_USER_AGENT, "zif",
								      SOUP_SESSION_TIMEOUT, connection_timeout,
								      NULL);
	if (download->priv->session == NULL) {
		if (error != NULL)
			*error = g_error_new (1, 0, "could not setup session");
		goto out;
	}
	ret = TRUE;
out:
	return ret;
}

/**
 * zif_download_finalize:
 **/
static void
zif_download_finalize (GObject *object)
{
	ZifDownload *download;

	g_return_if_fail (object != NULL);
	g_return_if_fail (ZIF_IS_DOWNLOAD (object));
	download = ZIF_DOWNLOAD (object);

	g_free (download->priv->proxy);
	if (download->priv->msg != NULL)
		g_object_unref (download->priv->msg);
	if (download->priv->session != NULL)
		g_object_unref (download->priv->session);
	g_object_unref (download->priv->config);

	G_OBJECT_CLASS (zif_download_parent_class)->finalize (object);
}

/**
 * zif_download_class_init:
 **/
static void
zif_download_class_init (ZifDownloadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = zif_download_finalize;

	g_type_class_add_private (klass, sizeof (ZifDownloadPrivate));
}

/**
 * zif_download_init:
 **/
static void
zif_download_init (ZifDownload *download)
{
	download->priv = ZIF_DOWNLOAD_GET_PRIVATE (download);
	download->priv->msg = NULL;
	download->priv->session = NULL;
	download->priv->proxy = NULL;
	download->priv->completion = NULL;
	download->priv->config = zif_config_new ();
}

/**
 * zif_download_new:
 *
 * Return value: A new download class instance.
 **/
ZifDownload *
zif_download_new (void)
{
	if (zif_download_object != NULL) {
		g_object_ref (zif_download_object);
	} else {
		zif_download_object = g_object_new (ZIF_TYPE_DOWNLOAD, NULL);
		g_object_add_weak_pointer (zif_download_object, &zif_download_object);
	}
	return ZIF_DOWNLOAD (zif_download_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static guint _updates = 0;

static void
zif_download_progress_changed (ZifDownload *download, guint value, gpointer data)
{
	_updates++;
}

static gboolean
zif_download_cancel_cb (ZifDownload *download)
{
	gboolean ret;
	GError *error = NULL;
	ret = zif_download_cancel (download, &error);
	if (!ret)
		egg_error ("failed to cancel '%s'", error->message);
	return FALSE;
}

void
zif_download_test (EggTest *test)
{
	ZifDownload *download;
	ZifCompletion *completion;
	gboolean ret;
	GError *error = NULL;

	if (!egg_test_start (test, "ZifDownload"))
		return;

	/************************************************************/
	egg_test_title (test, "get download");
	download = zif_download_new ();
	egg_test_assert (test, download != NULL);

	completion = zif_completion_new ();
	g_signal_connect (completion, "percentage-changed", G_CALLBACK (zif_download_progress_changed), NULL);

	/************************************************************/
	egg_test_title (test, "set proxy");
	ret = zif_download_set_proxy (download, NULL, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "cancel not yet started download");
	ret = zif_download_cancel (download, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "download file");
	ret = zif_download_file (download, "http://people.freedesktop.org/~hughsient/temp/Screenshot.png", "../test/downloads", NULL, completion, &error);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	/************************************************************/
	egg_test_title (test, "enough updates");
	if (_updates > 5)
		egg_test_success (test, "got %i updates", _updates);
	else
		egg_test_failed (test, "got %i updates", _updates);

	/* setup cancel */
	g_timeout_add (50, (GSourceFunc) zif_download_cancel_cb, download);

	/************************************************************/
	egg_test_title (test, "download second file (should be cancelled)");
	zif_completion_reset (completion);
	ret = zif_download_file (download, "http://people.freedesktop.org/~hughsient/temp/Screenshot.png", "../test/downloads", NULL, completion, &error);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to load '%s'", error->message);

	g_object_unref (download);
	g_object_unref (completion);

	egg_test_end (test);
}
#endif

