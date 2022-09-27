/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <string.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>

#include "pk-client.h"
#include "pk-client-helper.h"
#include "pk-control.h"
#include "pk-console-shared.h"
#include "pk-offline.h"
#include "pk-offline-private.h"
#include "pk-package-ids.h"
#include "pk-results.h"
#include "pk-task.h"
#include "pk-task-text.h"
#include "pk-task-wrapper.h"
#include "pk-transaction-list.h"
#include "pk-version.h"
#include "pk-control-sync.h"
#include "pk-client-sync.h"
#include "pk-debug.h"

/** ver:1.0 ***********************************************************/
static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
_g_test_hang_check_cb (gpointer user_data)
{
	guint timeout_ms = *((guint*) user_data);
	g_main_loop_quit (_test_loop);
	g_warning ("loop not completed in %ims", timeout_ms);
	g_assert_not_reached ();
	return FALSE;
}

/*
 * _g_test_loop_run_with_timeout:
 **/
static void
_g_test_loop_run_with_timeout (guint timeout_ms)
{
	g_assert (_test_loop_timeout_id == 0);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_check_cb, &timeout_ms);
	g_main_loop_run (_test_loop);
}

#if 0
static gboolean
_g_test_hang_wait_cb (gpointer user_data)
{
	g_main_loop_quit (_test_loop);
	_test_loop_timeout_id = 0;
	return FALSE;
}

/*
 * _g_test_loop_wait:
 **/
static void
_g_test_loop_wait (guint timeout_ms)
{
	g_assert (_test_loop_timeout_id == 0);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_wait_cb, &timeout_ms);
	g_main_loop_run (_test_loop);
}
#endif

/*
 * _g_test_loop_quit:
 **/
static void
_g_test_loop_quit (void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove (_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit (_test_loop);
		g_main_loop_unref (_test_loop);
		_test_loop = NULL;
	}
}

/**********************************************************************/

static void
pk_test_offline_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	_g_test_loop_quit ();
}

static void
pk_test_offline_func (void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *data = NULL;
	g_autoptr(PkClient) client = NULL;
	g_auto(GStrv) package_ids = NULL;

	/* set up an offline update */
	client = pk_client_new ();
	package_ids = pk_package_ids_from_string ("powertop;1.8-1.fc8;i386;fedora");
	pk_client_update_packages_async (client,
					 pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD, -1),
					 package_ids,
					 NULL,
					 NULL, NULL,
					 pk_test_offline_cb, NULL);
	_g_test_loop_run_with_timeout (25000);
	g_assert (g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (!g_file_test (PK_OFFLINE_TRIGGER_FILENAME, G_FILE_TEST_EXISTS));

	/* check prepared contents */
	ret = g_file_get_contents (PK_OFFLINE_PREPARED_FILENAME, &data, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpstr (data, ==, "powertop;1.8-1.fc8;i386;fedora");

	/* trigger */
	ret = pk_offline_trigger_with_flags (PK_OFFLINE_ACTION_REBOOT, PK_OFFLINE_FLAGS_INTERACTIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (g_file_test (PK_OFFLINE_TRIGGER_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (g_file_test (PK_OFFLINE_ACTION_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (!g_file_test (PK_OFFLINE_RESULTS_FILENAME, G_FILE_TEST_EXISTS));

	/* cancel the trigger */
	ret = pk_offline_cancel_with_flags (PK_OFFLINE_FLAGS_INTERACTIVE, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (!g_file_test (PK_OFFLINE_TRIGGER_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (g_file_test (PK_OFFLINE_ACTION_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (!g_file_test (PK_OFFLINE_RESULTS_FILENAME, G_FILE_TEST_EXISTS));

	/* ensure a cache update kills the prepared update file */
	pk_client_refresh_cache_async (client, FALSE, NULL, NULL, NULL,
				       pk_test_offline_cb, NULL);
	_g_test_loop_run_with_timeout (25000);
	g_assert (!g_file_test (PK_OFFLINE_PREPARED_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (!g_file_test (PK_OFFLINE_TRIGGER_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (g_file_test (PK_OFFLINE_ACTION_FILENAME, G_FILE_TEST_EXISTS));
	g_assert (!g_file_test (PK_OFFLINE_RESULTS_FILENAME, G_FILE_TEST_EXISTS));
}

/*
 * pk_test_client_helper_output_cb:
 **/
static gboolean
pk_test_client_helper_output_cb (GSocket *socket, GIOCondition condition, gpointer user_data)
{
	GError *error = NULL;
	gsize len;
	gchar buffer[6] = {0};
	gboolean ret = TRUE;

	/* the helper process exited */
	if ((condition & G_IO_HUP) > 0) {
		g_debug ("socket was disconnected");
		ret = FALSE;
		goto out;
	}

	/* there is data */
	if ((condition & G_IO_IN) > 0) {
		len = g_socket_receive (socket, buffer, 6, NULL, &error);
		g_assert_no_error (error);
		g_assert_cmpint (len, >, 0);

		/* good for us */
		if (strncmp (buffer, "pong\n", len) == 0) {
			_g_test_loop_quit ();
			goto out;
		}
		/* bad */
		g_warning ("child returned unexpected data: %s", buffer);
	}
out:
	return ret;
}

static void
pk_test_client_helper_func (void)
{
	gboolean ret;
	GError *error = NULL;
	gchar *filename;
	PkClientHelper *client_helper;
	const gchar *argv[] = { TESTDATADIR "/pk-client-helper-test.py", NULL };
	const gchar *envp[] = { "DAVE=1", NULL };
	GSocket *socket = NULL;
	GSocketAddress *address = NULL;
	gsize wrote;
	GSource *source;

	client_helper = pk_client_helper_new ();
	g_assert (client_helper != NULL);

	/* unref without using */
	g_object_unref (client_helper);

	/* new object */
	client_helper = pk_client_helper_new ();
	g_assert (client_helper != NULL);

	/* create a socket filename */
	filename = g_build_filename (g_get_tmp_dir (), "pk-self-test.socket", NULL);

	/* ensure previous sockets are deleted */
	g_unlink (filename);

	/* start a demo program */
	ret = pk_client_helper_start (client_helper, filename, (gchar**) argv, (gchar**) envp, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));

	/* create socket */
	socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
	g_assert_no_error (error);
	g_assert (socket != NULL);
	g_socket_set_blocking (socket, FALSE);
	g_socket_set_keepalive (socket, TRUE);

	/* connect to it */
	address = g_unix_socket_address_new (filename);
	ret = g_socket_connect (socket, address, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* socket has data */
	source = g_socket_create_source (socket, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, NULL);
	g_source_set_callback (source, G_SOURCE_FUNC (pk_test_client_helper_output_cb), NULL, NULL);
	g_source_attach (source, NULL);

	/* send some data */
	wrote = g_socket_send (socket, "ping\n", 5, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (wrote, ==, 5);

	/* run for a few seconds */
	_g_test_loop_run_with_timeout (1000);

	/* stop the demo program */
	ret = pk_client_helper_stop (client_helper, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* delete socket */
	g_unlink (filename);

	g_free (filename);
	g_object_unref (socket);
	g_object_unref (address);
	g_object_unref (client_helper);
}

static void
pk_test_client_resolve_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	gboolean idle;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	packages = pk_results_get_package_array (results);
	g_assert (packages != NULL);

	/* check idle */
	g_object_get (client, "idle", &idle, NULL);
	g_assert (idle);
	g_assert_cmpint (packages->len, ==, 2);

	g_ptr_array_unref (packages);

	g_debug ("results exit enum = %s", pk_exit_enum_to_string (exit_enum));

	g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_client_get_details_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	GPtrArray *details;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	details = pk_results_get_details_array (results);
	g_assert (details != NULL);
	g_assert_cmpint (details->len, ==, 1);

	g_ptr_array_unref (details);

	g_debug ("results exit enum = %s", pk_exit_enum_to_string (exit_enum));

	g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_client_get_updates_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	PkPackageSack *sack;
	guint size;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	sack = pk_results_get_package_sack (results);
	g_assert (sack != NULL);

	/* check size */
	size = pk_package_sack_get_size (sack);
	g_assert_cmpint (size, ==, 3);

	g_object_unref (sack);

	g_debug ("results exit enum = %s", pk_exit_enum_to_string (exit_enum));

	g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_client_search_name_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	PkError *error_code = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_CANCELLED);

	/* check error code */
	error_code = pk_results_get_error_code (results);
	g_assert_cmpint (pk_error_get_code (error_code), ==, PK_ERROR_ENUM_TRANSACTION_CANCELLED);
	g_assert_cmpstr (pk_error_get_details (error_code), ==, "The task was stopped successfully");
//	g_assert_cmpstr (pk_error_get_details (error_code), ==, "transaction was cancelled");

	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_client_search_name_cancellable_cancelled_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	results = pk_client_generic_finish (client, res, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_assert (results == NULL);
	_g_test_loop_quit ();
}

static guint _progress_cb = 0;
static guint _status_cb = 0;
static guint _package_cb = 0;
static guint _allow_cancel_cb = 0;
gchar *_tid = NULL;

static void
pk_test_client_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	gchar *tid;
	if (type == PK_PROGRESS_TYPE_PACKAGE_ID)
		_package_cb++;
	if (type == PK_PROGRESS_TYPE_PERCENTAGE)
		_progress_cb++;
	if (type == PK_PROGRESS_TYPE_ALLOW_CANCEL)
		_allow_cancel_cb++;
	if (type == PK_PROGRESS_TYPE_STATUS)
		_status_cb++;

	/* get the running transaction id if we've not set it before */
	g_object_get (progress, "transaction-id", &tid, NULL);
	if (tid != NULL && _tid == NULL)
		_tid = g_strdup (tid);
	g_free (tid);
}

static gboolean
pk_test_client_cancel_cb (GCancellable *cancellable)
{
	g_debug ("cancelling method");
	g_cancellable_cancel (cancellable);
	return FALSE;
}

static void
pk_test_client_download_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;
	PkFiles *item;
	GPtrArray *array = NULL;
	gchar *package_id = NULL;
	gchar **files = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	/* check number */
	array = pk_results_get_files_array (results);
	g_assert_cmpint (array->len, ==, 2);

	/* check a result */
	item = g_ptr_array_index (array, 0);
	g_object_get (item,
		      "package-id", &package_id,
		      "files", &files,
		      NULL);
	g_assert_cmpstr (package_id, ==, "powertop-common;1.8-1.fc8;i386;fedora");
	g_assert_cmpint (g_strv_length (files), ==, 1);
	g_assert_cmpstr (files[0], ==, "/tmp/powertop-common-1.8-1.fc8.rpm");

	g_strfreev (files);
	g_free (package_id);
	g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
	_g_test_loop_quit ();
}

#if 0
static void
pk_test_client_recursive_signal_cb (PkControl *control, gpointer user_data)
{
	gboolean ret;
	ret = pk_control_get_properties (control, NULL, NULL);
	g_assert (ret);
}
#endif

static void
pk_test_client_notify_idle_cb (PkClient *client, GParamSpec *pspec, gpointer user_data)
{
	gboolean idle;
	g_object_get (client, "idle", &idle, NULL);
	g_debug ("idle=%i", idle);
}

static void
pk_test_client_update_system_socket_test_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) categories = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	/* make sure we handled the ping/pong frontend-socket thing, which is 5 + 1 */
	categories = pk_results_get_category_array (results);
	g_assert_cmpint (categories->len, ==, 1);
	_g_test_loop_quit ();
}

static void
pk_test_client_func (void)
{
	gchar **package_ids;
//	gchar *file;
	gboolean ret;
	gchar **values;
	GError *error = NULL;
	PkProgress *progress;
	gchar *tid;
	PkRoleEnum role;
	PkStatusEnum status;
//	PkResults *results;
	g_autoptr(GCancellable) cancellable = NULL;
	g_autoptr(PkClient) client = NULL;

#if 0
	/* test user temp */
	file = pk_client_get_user_temp ("self-test", NULL);
	g_assert (g_str_has_suffix (file, "/.PackageKit/self-test"));
	g_assert (g_str_has_prefix (file, "/home/"));
	g_free (file);

	/* test native TRUE */
	ret = pk_client_is_file_native ("/tmp");
	g_assert (ret);

	/* test native FALSE */
	ret = pk_client_is_file_native ("/tmp/.gvfs/moo");
	g_assert (!ret);

	/* test resolve NULL */
	file = pk_client_real_path (NULL);
	g_assert (file == NULL);

	/* test resolve /etc/hosts */
	file = pk_client_real_path ("/etc/hosts");
	g_assert_cmpstr (file, ==, "/etc/hosts");
	g_free (file);

	/* test resolve /etc/../etc/hosts */
	file = pk_client_real_path ("/etc/../etc/hosts");
	g_assert_cmpstr (file, ==, "/etc/hosts");
	g_free (file);
#endif

	/* get client */
	client = pk_client_new ();
	g_signal_connect (client, "notify::idle",
		  G_CALLBACK (pk_test_client_notify_idle_cb), NULL);
	g_assert (client != NULL);

	/* check idle */
	g_object_get (client, "idle", &ret, NULL);
	g_assert (ret);

	/* resolve package */
	package_ids = pk_package_ids_from_string ("glib2;2.14.0;i386;fedora&powertop");
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, NULL,
		 (PkProgressCallback) pk_test_client_progress_cb, NULL,
		 (GAsyncReadyCallback) pk_test_client_resolve_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* check idle */
	g_object_get (client, "idle", &ret, NULL);
	g_assert (ret);

	/* get progress of past transaction */
	progress = pk_client_get_progress (client, _tid, NULL, &error);
	g_object_get (progress,
		      "transaction-id", &tid,
		      "role", &role,
		      "status", &status,
		      NULL);
	g_assert_cmpstr (tid, ==, _tid);
	g_assert_cmpint (role, ==, PK_ROLE_ENUM_RESOLVE);
	g_assert_cmpint (status, ==, PK_STATUS_ENUM_FINISHED);
	g_debug ("got progress in %f", g_test_timer_elapsed ());
	g_object_unref (progress);
	g_free (tid);
	g_free (_tid);

	/* got updates */
	g_assert_cmpint (_progress_cb, >, 0);
	g_assert_cmpint (_status_cb, >, 0);

	/* reset */
	_progress_cb = 0;
	_status_cb = 0;
//	_package_cb = 0;

	/* get details about package */
	package_ids = pk_package_ids_from_id ("powertop;1.8-1.fc8;i386;fedora");
	pk_client_get_details_async (client, package_ids, NULL,
		     (PkProgressCallback) pk_test_client_progress_cb, NULL,
		     (GAsyncReadyCallback) pk_test_client_get_details_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* got updates */
	g_assert_cmpint (_progress_cb, >, 0);
	g_assert_cmpint (_status_cb, >, 0);

	/* reset */
	_progress_cb = 0;
	_status_cb = 0;
//	_package_cb = 0;

	/* get updates */
	pk_client_get_updates_async (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), NULL,
		     (PkProgressCallback) pk_test_client_progress_cb, NULL,
		     (GAsyncReadyCallback) pk_test_client_get_updates_cb, NULL);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("got updates in %f", g_test_timer_elapsed ());

	/* it takes more than 50ms to get the progress of the transaction, and if
	 * getting updates from internal cache, then it'll take a shed load less
	 * than this to complete */
	if (g_test_timer_elapsed () > 100) {
		/* got status updates */
		g_assert_cmpint (_status_cb, >, 0);
	}

	/* search by name */
	cancellable = g_cancellable_new ();
	values = g_strsplit ("power", "&", -1);
	pk_client_search_names_async (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), values, cancellable,
		     (PkProgressCallback) pk_test_client_progress_cb, NULL,
		     (GAsyncReadyCallback) pk_test_client_search_name_cb, NULL);
	g_timeout_add (500, G_SOURCE_FUNC (pk_test_client_cancel_cb), cancellable);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("cancelled in %f", g_test_timer_elapsed ());

	/* ensure we abort with error if we cancel */
	pk_client_search_names_async (client, pk_bitfield_value (PK_FILTER_ENUM_NONE), values, cancellable,
		     (PkProgressCallback) pk_test_client_progress_cb, NULL,
		     (GAsyncReadyCallback) pk_test_client_search_name_cancellable_cancelled_cb, NULL);
	_g_test_loop_run_with_timeout (15000);

	g_strfreev (values);

	/* okay now */
	g_cancellable_reset (cancellable);

	/* do the update-packages role to trigger the fake pipe stuff */
	package_ids = pk_package_ids_from_string ("testsocket;0.1;i386;fedora");
	pk_client_update_packages_async (client, 0, package_ids,
					 NULL,
					 (PkProgressCallback) pk_test_client_progress_cb, NULL,
					 (GAsyncReadyCallback) pk_test_client_update_system_socket_test_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (15000);

	/* ensure previous files are deleted */
	g_unlink ("/tmp/powertop-1.8-1.fc8.rpm");
	g_unlink ("/tmp/powertop-common-1.8-1.fc8.rpm");

	/* do downloads */
	package_ids = pk_package_ids_from_id ("powertop;1.8-1.fc8;i386;fedora");
	pk_client_download_packages_async (client, package_ids, "/tmp", cancellable,
		   (PkProgressCallback) pk_test_client_progress_cb, NULL,
		   (GAsyncReadyCallback) pk_test_client_download_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("downloaded and copied in %f", g_test_timer_elapsed ());

	/* test recursive signal handling */
#if 0
	g_signal_connect (client->priv->control, "repo-list-changed", G_CALLBACK (pk_test_client_recursive_signal_cb), NULL);
	results = pk_client_repo_set_data (client, "dave", "moo", "data", NULL, NULL, NULL, NULL);
	g_assert (results != NULL);
	g_object_unref (results);
#endif
}

static void
pk_test_console_func (void)
{
	gboolean ret;

	/* get prompt 1 */
	ret = pk_console_get_prompt ("press enter", TRUE);
	g_assert (ret);

	/* get prompt 2 */
	ret = pk_console_get_prompt ("press enter", TRUE);
	g_assert (ret);

	/* get prompt 3 */
	ret = pk_console_get_prompt ("press Y", TRUE);
	g_assert (ret);

	/* get prompt 3 */
	ret = pk_console_get_prompt ("press N", TRUE);
	g_assert (!ret);
}

static guint _refcount = 0;

static void
pk_test_control_get_tid_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gchar *tid;

	/* get the result */
	tid = pk_control_get_tid_finish (control, res, &error);
	g_assert_no_error (error);
	g_assert (tid != NULL);

	g_debug ("tid = %s", tid);
	g_free (tid);
	if (--_refcount == 0)
		_g_test_loop_quit ();
}

static void
pk_test_control_get_properties_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	gboolean ret;
	PkBitfield roles;
	PkBitfield filters;
	PkBitfield groups;
	gchar **mime_types;
	gchar *text;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get values */
	g_object_get (control,
		      "mime-types", &mime_types,
		      "roles", &roles,
		      "filters", &filters,
		      "groups", &groups,
		      NULL);

	/* check mime_types */
	text = g_strjoinv (";", mime_types);
	g_assert_cmpstr (text, ==, "application/x-rpm;application/x-deb");
	g_free (text);
	g_strfreev (mime_types);

	/* check roles */
	text = pk_role_bitfield_to_string (roles);
	g_assert_cmpstr (text, ==, "cancel;depends-on;get-details;get-files;get-packages;get-repo-list;"
		     "required-by;get-update-detail;get-updates;install-files;install-packages;install-signature;"
		     "refresh-cache;remove-packages;repo-enable;repo-set-data;resolve;"
		     "search-details;search-file;search-group;search-name;update-packages;"
		     "what-provides;download-packages;get-distro-upgrades;"
		     "get-old-transactions;repair-system;get-details-local;"
		     "get-files-local;upgrade-system");
	g_free (text);

	/* check filters */
	text = pk_filter_bitfield_to_string (filters);
	g_assert_cmpstr (text, ==, "installed;devel;gui");
	g_free (text);

	/* check groups */
	text = pk_group_bitfield_to_string (groups);
	g_assert_cmpstr (text, ==, "accessibility;games;system");
	g_debug ("groups = %s", text);

	g_free (text);

	if (--_refcount == 0)
		_g_test_loop_quit ();
}

static void
pk_test_control_get_time_since_action_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	guint seconds;

	/* get the result */
	seconds = pk_control_get_time_since_action_finish (control, res, &error);
	g_assert_no_error (error);
	g_assert_cmpint (seconds, !=, 0);

	_g_test_loop_quit ();
}

static void
pk_test_control_can_authorize_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkControl *control = PK_CONTROL (object);
	GError *error = NULL;
	PkAuthorizeEnum auth;

	/* get the result */
	auth = pk_control_can_authorize_finish (control, res, &error);
	g_assert_no_error (error);
	g_assert_cmpint (auth, !=, PK_AUTHORIZE_ENUM_UNKNOWN);

	_g_test_loop_quit ();
}

static void
pk_test_control_func (void)
{
	PkControl *control;
	guint version;
	GError *error = NULL;
	gboolean ret;
	gchar *text;
	PkBitfield roles;
	guint i;
	const guint LOOP_SIZE = 5;

	/* get control */
	control = pk_control_new ();
	g_assert (control != NULL);

	/* get TID async */
	_refcount = 1;
	pk_control_get_tid_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_tid_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got tid in %f", g_test_timer_elapsed ());

	/* get multiple TIDs async */
	_refcount = LOOP_SIZE;
	for (i = 0; i < _refcount; i++) {
		g_debug ("getting #%i", i+1);
		pk_control_get_tid_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_tid_cb, NULL);
	}
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got %i tids in %f", LOOP_SIZE, g_test_timer_elapsed ());

	/* get properties async */
	_refcount = 1;
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_properties_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got properties types in %f", g_test_timer_elapsed ());

	/* get properties async (again, to test caching) */
	_refcount = 1;
	pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_properties_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got properties in %f", g_test_timer_elapsed ());

	/* do multiple requests async */
	_refcount = LOOP_SIZE * 4;
	for (i = 0; i < _refcount; i++) {
		g_debug ("getting #%i", i+1);
		pk_control_get_tid_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_tid_cb, NULL);
		pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_properties_cb, NULL);
		pk_control_get_tid_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_tid_cb, NULL);
		pk_control_get_properties_async (control, NULL, (GAsyncReadyCallback) pk_test_control_get_properties_cb, NULL);
	}
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got %i 2*properties and 2*tids in %f", LOOP_SIZE, g_test_timer_elapsed ());

	/* get time since async */
	pk_control_get_time_since_action_async (control, PK_ROLE_ENUM_GET_UPDATES, NULL, (GAsyncReadyCallback) pk_test_control_get_time_since_action_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got get time since in %f", g_test_timer_elapsed ());

	/* get auth state async */
	pk_control_can_authorize_async (control, "org.freedesktop.packagekit.system-update", NULL,
		(GAsyncReadyCallback) pk_test_control_can_authorize_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("get auth state in %f", g_test_timer_elapsed ());

	/* version major */
	g_object_get (control, "version-major", &version, NULL);
	g_assert_cmpint (version, ==, PK_MAJOR_VERSION);

	/* version minor */
	g_object_get (control, "version-minor", &version, NULL);
	g_assert_cmpint (version, ==, PK_MINOR_VERSION);

	/* version micro */
	g_object_get (control, "version-micro", &version, NULL);
	g_assert_cmpint (version, ==, PK_MICRO_VERSION);

	/* get properties sync */
	ret = pk_control_get_properties (control, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get data */
	g_object_get (control,
		      "roles", &roles,
		      NULL);

	/* check data */
	text = pk_role_bitfield_to_string (roles);
	g_assert_cmpstr (text, ==, "cancel;depends-on;get-details;get-files;get-packages;get-repo-list;"
		     "required-by;get-update-detail;get-updates;install-files;install-packages;install-signature;"
		     "refresh-cache;remove-packages;repo-enable;repo-set-data;resolve;"
		     "search-details;search-file;search-group;search-name;update-packages;"
		     "what-provides;download-packages;get-distro-upgrades;"
		     "get-old-transactions;repair-system;get-details-local;"
		     "get-files-local;upgrade-system");
	g_free (text);

	g_object_unref (control);
}

static void
pk_test_package_sack_resolve_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	g_assert_no_error (error);
	g_assert (ret);

	_g_test_loop_quit ();
}

static void
pk_test_package_sack_details_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	g_assert_no_error (error);
	g_assert (ret);

	_g_test_loop_quit ();
}

static void
pk_test_package_sack_update_detail_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkPackageSack *sack = PK_PACKAGE_SACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the result */
	ret = pk_package_sack_merge_generic_finish (sack, res, &error);
	g_assert_no_error (error);
	g_assert (ret);

	_g_test_loop_quit ();
}

/*
 * pk_test_package_sack_filter_cb:
 **/
static gboolean
pk_test_package_sack_filter_cb (PkPackage *package, gpointer user_data)
{
	if (pk_package_get_info (package) == PK_INFO_ENUM_UNKNOWN)
		return FALSE;
	return TRUE;
}

static void
pk_test_package_sack_func (void)
{
	gboolean ret;
	PkPackageSack *sack;
	PkPackage *package;
	gchar *text;
	gchar **strv;
	guint size;
	PkInfoEnum info = PK_INFO_ENUM_UNKNOWN;
	guint64 bytes;

	sack = pk_package_sack_new ();
	g_assert (sack != NULL);

	/* get size of unused package sack */
	size = pk_package_sack_get_size (sack);
	g_assert (size == 0);

	/* remove package not present */
	ret = pk_package_sack_remove_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (!ret);

	/* find package not present */
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (package == NULL);

	/* add package */
	ret = pk_package_sack_add_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora", NULL);
	g_assert (ret);

	/* get size of package sack */
	size = pk_package_sack_get_size (sack);
	g_assert (size == 1);

	/* merge resolve results */
	pk_package_sack_resolve_async (sack, NULL, NULL, NULL, (GAsyncReadyCallback) pk_test_package_sack_resolve_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* find package which is present */
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (package != NULL);

	/* check new summary */
	g_object_get (package,
		      "info", &info,
		      "summary", &text,
		      NULL);
	g_assert_cmpstr (text, ==, "Power consumption monitor");

	/* check new info */
	g_assert_cmpint (info, ==, PK_INFO_ENUM_INSTALLED);

	g_free (text);
	g_object_unref (package);

	/* merge details results */
	pk_package_sack_get_details_async (sack, NULL, NULL, NULL, (GAsyncReadyCallback) pk_test_package_sack_details_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got details in %f", g_test_timer_elapsed ());

	/* find package which is present */
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (package != NULL);

	/* check new url */
	g_object_get (package,
		      "url", &text,
		      NULL);
	g_assert_cmpstr (text, ==, "http://live.gnome.org/powertop");
	g_object_unref (package);
	g_free (text);

	/* merge update detail results */
	pk_package_sack_get_update_detail_async (sack, NULL, NULL, NULL, (GAsyncReadyCallback) pk_test_package_sack_update_detail_cb, NULL);
	_g_test_loop_run_with_timeout (5000);
	g_debug ("got update detail in %f", g_test_timer_elapsed ());

	/* find package which is present */
	package = pk_package_sack_find_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (package != NULL);

	/* check new vendor url */
	g_object_get (package,
		      "update-vendor-urls", &strv,
		      NULL);
	g_assert (strv != NULL);
	g_assert_cmpstr (strv[0], ==, "http://www.distro-update.org/page?moo");
	g_strfreev (strv);

	g_object_unref (package);

	/* chck size in bytes */
	bytes = pk_package_sack_get_total_bytes (sack);
	g_assert_cmpint (bytes, ==, 103424);

	/* remove package */
	ret = pk_package_sack_remove_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (ret);

	/* get size of package sack */
	size = pk_package_sack_get_size (sack);
	g_assert_cmpint (size, ==, 0);

	/* remove already removed package */
	ret = pk_package_sack_remove_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora");
	g_assert (!ret);

	/* remove by filter */
	pk_package_sack_add_package_by_id (sack, "powertop;1.8-1.fc8;i386;fedora", NULL);
	pk_package_sack_add_package_by_id (sack, "powertop-debuginfo;1.8-1.fc8;i386;fedora", NULL);
	ret = pk_package_sack_remove_by_filter (sack, pk_test_package_sack_filter_cb, NULL);
	g_assert (ret);

	/* check all removed */
	size = pk_package_sack_get_size (sack);
	g_assert_cmpint (size, ==, 0);

	g_object_unref (sack);
}

static void
pk_test_task_install_packages_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkTask *task = PK_TASK (object);
	g_autoptr(GError) error = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	g_assert (results == NULL);
	g_assert_cmpstr (error->message, ==, "could not do untrusted question as no klass support");

	_g_test_loop_quit ();
}

static void
pk_test_task_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
		      "status", &status,
		      NULL);
		g_debug ("now %s", pk_status_enum_to_string (status));
	}
}

static void
pk_test_task_func (void)
{
	PkTask *task;
	gchar **package_ids;

	task = pk_task_new ();
	g_assert (task != NULL);

	/* install package */
	package_ids = pk_package_ids_from_id ("glib2;2.14.0;i386;fedora");
	pk_task_install_packages_async (task, package_ids, NULL,
		        (PkProgressCallback) pk_test_task_progress_cb, NULL,
		        (GAsyncReadyCallback) pk_test_task_install_packages_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (150000);
	g_debug ("installed in %f", g_test_timer_elapsed ());

	g_object_unref (task);
}

static void
pk_test_task_text_install_packages_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkTaskText *task = PK_TASK_TEXT (object);
	GError *error = NULL;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK (task), res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	packages = pk_results_get_package_array (results);
	g_assert (packages != NULL);
	g_assert_cmpint (packages->len, ==, 4);

	g_ptr_array_unref (packages);

	g_debug ("results exit enum = %s", pk_exit_enum_to_string (exit_enum));

	if (results != NULL)
		g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_task_text_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
		      "status", &status,
		      NULL);
		g_debug ("now %s", pk_status_enum_to_string (status));
	}
}

static void
pk_test_task_text_func (void)
{
	PkTaskText *task;
	gchar **package_ids;

	task = pk_task_text_new ();
	g_assert (task != NULL);

	/* For testing, you will need to manually do:
	pkcon repo-set-data dummy use-gpg 1
	pkcon repo-set-data dummy use-eula 1
	pkcon repo-set-data dummy use-media 1
	*/

	/* install package */
	package_ids = pk_package_ids_from_id ("vips-doc;7.12.4-2.fc8;noarch;linva");
	pk_task_install_packages_async (PK_TASK (task), package_ids, NULL,
		        (PkProgressCallback) pk_test_task_text_progress_cb, NULL,
		        (GAsyncReadyCallback) pk_test_task_text_install_packages_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (150000);
	g_debug ("installed in %f", g_test_timer_elapsed ());

	g_object_unref (task);
}

static void
pk_test_task_wrapper_install_packages_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkTaskWrapper *task = PK_TASK_WRAPPER (object);
	GError *error = NULL;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;

	/* get the results */
	results = pk_task_generic_finish (PK_TASK (task), res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	packages = pk_results_get_package_array (results);
	g_assert (packages != NULL);
	g_assert_cmpint (packages->len, ==, 4);

	g_ptr_array_unref (packages);

	g_debug ("results exit enum = %s", pk_exit_enum_to_string (exit_enum));

	if (results != NULL)
		g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_task_wrapper_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	PkStatusEnum status;
	if (type == PK_PROGRESS_TYPE_STATUS) {
		g_object_get (progress,
		      "status", &status,
		      NULL);
		g_debug ("now %s", pk_status_enum_to_string (status));
	}
}

static void
pk_test_task_wrapper_func (void)
{
	PkTaskWrapper *task;
	gchar **package_ids;

	task = pk_task_wrapper_new ();
	g_assert (task != NULL);

	/* install package */
	package_ids = pk_package_ids_from_id ("vips-doc;7.12.4-2.fc8;noarch;linva");
	pk_task_install_packages_async (PK_TASK (task), package_ids, NULL,
		        (PkProgressCallback) pk_test_task_wrapper_progress_cb, NULL,
		        (GAsyncReadyCallback) pk_test_task_wrapper_install_packages_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (150000);
	g_debug ("installed in %f", g_test_timer_elapsed ());

	g_object_unref (task);
}

guint _added = 0;
guint _removed = 0;
//guint _refcount = 0;

static void
pk_test_transaction_list_resolve_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_SUCCESS);

	if (results != NULL)
		g_object_unref (results);
	if (--_refcount == 0)
		_g_test_loop_quit ();
}

static void
pk_test_transaction_list_added_cb (PkTransactionList *tlist, const gchar *tid, gpointer user_data)
{
	g_debug ("added %s", tid);
	_added++;
}

static void
pk_test_transaction_list_removed_cb (PkTransactionList *tlist, const gchar *tid, gpointer user_data)
{
	g_debug ("removed %s", tid);
	_removed++;
}

static gboolean
pk_transaction_list_delay_cb (gpointer user_data)
{
	_g_test_loop_quit ();
	return FALSE;
}

static void
pk_test_transaction_list_func (void)
{
	PkTransactionList *tlist;
	PkClient *client;
	gchar **package_ids;

	/* get transaction_list object */
	tlist = pk_transaction_list_new ();
	g_assert (tlist != NULL);
	g_signal_connect (tlist, "added",
		  G_CALLBACK (pk_test_transaction_list_added_cb), NULL);
	g_signal_connect (tlist, "removed",
		  G_CALLBACK (pk_test_transaction_list_removed_cb), NULL);

	/* get client */
	client = pk_client_new ();
	g_assert (client != NULL);

	/* resolve package */
	package_ids = pk_package_ids_from_string ("glib2;2.14.0;i386;fedora&powertop");
	_refcount = 2;
	pk_client_resolve_async (client,
				 pk_bitfield_value (PK_FILTER_ENUM_INSTALLED),
				 package_ids,
				 NULL,
				 NULL,
				 NULL,
				 (GAsyncReadyCallback) pk_test_transaction_list_resolve_cb,
				 NULL);
	pk_client_resolve_async (client,
				 pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED),
				 package_ids,
				 NULL,
				 NULL,
				 NULL,
				 (GAsyncReadyCallback) pk_test_transaction_list_resolve_cb,
				 NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* wait for remove */
	g_timeout_add (100, G_SOURCE_FUNC (pk_transaction_list_delay_cb), NULL);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* correct number of added signals */
	g_assert_cmpint (_added, ==, 2);

	/* correct number of removed signals */
	g_assert_cmpint (_removed, ==, 2);

	g_object_unref (tlist);
	g_object_unref (client);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	pk_debug_set_verbose (TRUE);
	pk_debug_add_log_domain (G_LOG_DOMAIN);

#ifndef PK_ENABLE_DAEMON_TESTS
	return 0;
#endif

	/* some libraries need to know */
	g_setenv ("PK_SELF_TEST", "1", TRUE);

	/* tests go here */
	if(0) g_test_add_func ("/packagekit-glib2/offline", pk_test_offline_func);
	g_test_add_func ("/packagekit-glib2/control", pk_test_control_func);
	g_test_add_func ("/packagekit-glib2/transaction-list", pk_test_transaction_list_func);
	g_test_add_func ("/packagekit-glib2/client-helper", pk_test_client_helper_func);
	g_test_add_func ("/packagekit-glib2/client", pk_test_client_func);
	g_test_add_func ("/packagekit-glib2/package-sack", pk_test_package_sack_func);
	g_test_add_func ("/packagekit-glib2/task", pk_test_task_func);
	g_test_add_func ("/packagekit-glib2/task-wrapper", pk_test_task_wrapper_func);
	g_test_add_func ("/packagekit-glib2/task-text", pk_test_task_text_func);
	g_test_add_func ("/packagekit-glib2/console", pk_test_console_func);

	return g_test_run ();
}

