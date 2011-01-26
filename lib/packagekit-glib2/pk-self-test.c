/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
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

#include "pk-catalog.h"
#include "pk-client.h"
#include "pk-client-helper.h"
#include "pk-common.h"
#include "pk-control.h"
#include "pk-console-shared.h"
#include "pk-desktop.h"
#include "pk-enum.h"
#include "pk-package.h"
#include "pk-package-id.h"
#include "pk-package-ids.h"
#include "pk-package-sack.h"
#include "pk-results.h"
#include "pk-task.h"
#include "pk-task-text.h"
#include "pk-task-wrapper.h"
#include "pk-transaction-list.h"
#include "pk-version.h"
#include "pk-control-sync.h"
#include "pk-client-sync.h"
#include "pk-progress-bar.h"
#include "pk-service-pack.h"
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

/**
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

/**
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

/**
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
pk_test_bitfield_func (void)
{
	gchar *text;
	PkBitfield filter;
	gint value;
	PkBitfield values;

	/* check we can convert filter bitfield to text (none) */
	text = pk_filter_bitfield_to_string (pk_bitfield_value (PK_FILTER_ENUM_NONE));
	g_assert_cmpstr (text, ==, "none");
	g_free (text);

	/* check we can invert a bit 1 -> 0 */
	values = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) | pk_bitfield_value (PK_FILTER_ENUM_NOT_NEWEST);
	pk_bitfield_invert (values, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	g_assert_cmpint (values, ==, pk_bitfield_value (PK_FILTER_ENUM_NOT_NEWEST));

	/* check we can invert a bit 0 -> 1 */
	values = 0;
	pk_bitfield_invert (values, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	g_assert_cmpint (values, ==, pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT));

	/* check we can convert filter bitfield to text (single) */
	text = pk_filter_bitfield_to_string (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT));
	g_assert_cmpstr (text, ==, "~devel");
	g_free (text);

	/* check we can convert filter bitfield to text (plural) */
	text = pk_filter_bitfield_to_string (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		   pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		   pk_bitfield_value (PK_FILTER_ENUM_NEWEST));
	g_assert_cmpstr (text, ==, "~devel;gui;newest");
	g_free (text);

	/* check we can convert filter text to bitfield (none) */
	filter = pk_filter_bitfield_from_string ("none");
	g_assert_cmpint (filter, ==, pk_bitfield_value (PK_FILTER_ENUM_NONE));

	/* check we can convert filter text to bitfield (single) */
	filter = pk_filter_bitfield_from_string ("~devel");
	g_assert_cmpint (filter, ==, pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT));

	/* check we can convert filter text to bitfield (plural) */
	filter = pk_filter_bitfield_from_string ("~devel;gui;newest");
	g_assert_cmpint (filter, ==, (pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		       pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		       pk_bitfield_value (PK_FILTER_ENUM_NEWEST)));

	/* check we can add / remove bitfield */
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		 pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		 pk_bitfield_value (PK_FILTER_ENUM_NEWEST);
	pk_bitfield_add (filter, PK_FILTER_ENUM_NOT_FREE);
	pk_bitfield_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_bitfield_to_string (filter);
	g_assert_cmpstr (text, ==, "gui;~free;newest");
	g_free (text);

	/* check we can test enum presence */
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT) |
		 pk_bitfield_value (PK_FILTER_ENUM_GUI) |
		 pk_bitfield_value (PK_FILTER_ENUM_NEWEST);
	g_assert (pk_bitfield_contain (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT));

	/* check we can test enum false-presence */
	g_assert (!pk_bitfield_contain (filter, PK_FILTER_ENUM_FREE));

	/* check we can add / remove bitfield to nothing */
	filter = pk_bitfield_value (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	pk_bitfield_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_bitfield_to_string (filter);
	g_assert_cmpstr (text, ==, "none");
	g_free (text);

	/* role bitfield from enums (unknown) */
	values = pk_bitfield_from_enums (PK_ROLE_ENUM_UNKNOWN, -1);
	g_assert_cmpint (values, ==, pk_bitfield_value (PK_ROLE_ENUM_UNKNOWN));

	/* role bitfield from enums (random) */
	values = pk_bitfield_from_enums (PK_ROLE_ENUM_SEARCH_GROUP, PK_ROLE_ENUM_SEARCH_DETAILS, -1);
	g_assert_cmpint (values, ==, (pk_bitfield_value (PK_ROLE_ENUM_SEARCH_DETAILS) |
		       pk_bitfield_value (PK_ROLE_ENUM_SEARCH_GROUP)));

	/* group bitfield from enums (unknown) */
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_UNKNOWN, -1);
	g_assert_cmpint (values, ==, pk_bitfield_value (PK_GROUP_ENUM_UNKNOWN));

	/* group bitfield from enums (random) */
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, -1);
	g_assert_cmpint (values, ==, (pk_bitfield_value (PK_GROUP_ENUM_ACCESSIBILITY)));

	/* group bitfield to text (unknown) */
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_UNKNOWN, -1);
	text = pk_group_bitfield_to_string (values);
	g_assert_cmpstr (text, ==, "unknown");
	g_free (text);

	/* group bitfield to text (first and last) */
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, PK_GROUP_ENUM_UNKNOWN, -1);
	text = pk_group_bitfield_to_string (values);
	g_assert_cmpstr (text, ==, "unknown;accessibility");
	g_free (text);

	/* group bitfield to text (random) */
	values = pk_bitfield_from_enums (PK_GROUP_ENUM_UNKNOWN, PK_GROUP_ENUM_REPOS, -1);
	text = pk_group_bitfield_to_string (values);
	g_assert_cmpstr (text, ==, "unknown;repos");
	g_free (text);

	/* priority check missing */
	values = pk_bitfield_value (PK_ROLE_ENUM_SEARCH_DETAILS) |
		 pk_bitfield_value (PK_ROLE_ENUM_SEARCH_GROUP);
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_FILE, -1);
	g_assert_cmpint (value, ==, -1);

	/* priority check first */
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	g_assert_cmpint (value, ==, PK_ROLE_ENUM_SEARCH_GROUP);

	/* priority check second, correct */
	value = pk_bitfield_contain_priority (values, PK_ROLE_ENUM_SEARCH_FILE, PK_ROLE_ENUM_SEARCH_GROUP, -1);
	g_assert_cmpint (value, ==, PK_ROLE_ENUM_SEARCH_GROUP);
}

static void
pk_test_catalog_lookup_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkCatalog *catalog = PK_CATALOG (object);
	GError *error = NULL;
	GPtrArray *array;
	guint i;
	PkPackage *package;

	/* get the results */
	array = pk_catalog_lookup_finish (catalog, res, &error);
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, ==, 3);

	/* list for shits and giggles */
	for (i=0; i<array->len; i++) {
		package = g_ptr_array_index (array, i);
		g_debug ("%i\t%s", i, pk_package_get_id (package));
	}
	g_ptr_array_unref (array);
	_g_test_loop_quit ();
}

static void
pk_test_catalog_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
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
pk_test_catalog_func (void)
{
	PkCatalog *catalog;

	catalog = pk_catalog_new ();
	g_assert (catalog != NULL);

	/* lookup catalog */
	pk_catalog_lookup_async (catalog, TESTDATADIR "/test.catalog", NULL,
				 (PkProgressCallback) pk_test_catalog_progress_cb, NULL,
				 (GAsyncReadyCallback) pk_test_catalog_lookup_cb, NULL);
	_g_test_loop_run_with_timeout (150000);
	g_debug ("resolvd, searched, etc. in %f", g_test_timer_elapsed ());

	g_object_unref (catalog);
}

/**
 * pk_test_client_helper_output_cb:
 **/
static gboolean
pk_test_client_helper_output_cb (GSocket *socket, GIOCondition condition, gpointer user_data)
{
	GError *error = NULL;
	gsize len;
	gchar buffer[6];
	gboolean ret = TRUE;

	/* the helper process exited */
	if ((condition & G_IO_HUP) > 0) {
		g_warning ("socket was disconnected");
		ret = FALSE;
		goto out;
	}

	/* there is data */
	if ((condition & G_IO_IN) > 0) {
		len = g_socket_receive (socket, buffer, 6, NULL, &error);
		g_assert_no_error (error);
		g_assert_cmpint (len, >, 0);

		/* good for us */
		if (buffer != NULL &&
		    strncmp (buffer, "pong\n", len) == 0) {
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
	g_source_set_callback (source, (GSourceFunc) pk_test_client_helper_output_cb, NULL, NULL);
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
	if (type == PK_PROGRESS_TYPE_SUBPERCENTAGE)
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
	PkResults *results = NULL;
	GPtrArray *messages;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	g_assert_no_error (error);
	g_assert (results != NULL);

	/* make sure we handled the ping/pong frontend-socket thing, which is 5 + 1 */
	messages = pk_results_get_message_array (results);
	g_assert_cmpint (messages->len, ==, 6);
	g_ptr_array_unref (messages);

	g_object_unref (results);
	_g_test_loop_quit ();
}

static void
pk_test_client_func (void)
{
	PkClient *client;
	gchar **package_ids;
//	gchar *file;
	GCancellable *cancellable;
	gboolean ret;
	gchar **values;
	GError *error = NULL;
	PkProgress *progress;
	gchar *tid;
	PkRoleEnum role;
	PkStatusEnum status;
//	PkResults *results;

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
	g_timeout_add (1000, (GSourceFunc) pk_test_client_cancel_cb, cancellable);
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

	/* do the update-system role to trigger the fake pipe stuff */
	pk_client_update_system_async (client, TRUE, NULL,
				       (PkProgressCallback) pk_test_client_progress_cb, NULL,
				       (GAsyncReadyCallback) pk_test_client_update_system_socket_test_cb, NULL);
	_g_test_loop_run_with_timeout (15000);

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

	g_object_unref (cancellable);
	g_object_unref (client);
}

static void
pk_test_common_func (void)
{
	gchar *present;
	GDate *date;

	/************************************************************
	 **************            iso8601           ****************
	 ************************************************************/
	/* get present iso8601 */
	present = pk_iso8601_present ();
	g_assert (present != NULL);
	g_free (present);

	/************************************************************
	 **************        Date handling         ****************
	 ************************************************************/
	/* zero length date */
	date = pk_iso8601_to_date ("");
	g_assert (date == NULL);

	/* no day specified */
	date = pk_iso8601_to_date ("2004-01");
	g_assert (date == NULL);

	/* date _and_ time specified */
	date = pk_iso8601_to_date ("2009-05-08 13:11:12");
	g_assert_cmpint (date->day, ==, 8);
	g_assert_cmpint (date->month, ==, 5);
	g_assert_cmpint (date->year, ==, 2009);
	g_date_free (date);

	/* correct date format */
	date = pk_iso8601_to_date ("2004-02-01");
	g_assert_cmpint (date->day, ==, 1);
	g_assert_cmpint (date->month, ==, 2);
	g_assert_cmpint (date->year, ==, 2004);
	g_date_free (date);
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
	gchar *text;

	/* get the result */
	ret = pk_control_get_properties_finish (control, res, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get values */
	g_object_get (control,
		      "mime-types", &text,
		      "roles", &roles,
		      "filters", &filters,
		      "groups", &groups,
		      NULL);

	/* check mime_types */
	g_assert_cmpstr (text, ==, "application/x-rpm;application/x-deb");
	g_free (text);

	/* check roles */
	text = pk_role_bitfield_to_string (roles);
	g_assert_cmpstr (text, ==, "cancel;get-depends;get-details;get-files;get-packages;get-repo-list;"
		     "get-requires;get-update-detail;get-updates;install-files;install-packages;install-signature;"
		     "refresh-cache;remove-packages;repo-enable;repo-set-data;resolve;rollback;"
		     "search-details;search-file;search-group;search-name;update-packages;update-system;"
		     "what-provides;download-packages;get-distro-upgrades;simulate-install-packages;"
		     "simulate-remove-packages;simulate-update-packages;upgrade-system");
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
	for (i=0; i<_refcount; i++) {
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
	for (i=0; i<_refcount; i++) {
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
	g_assert_cmpstr (text, ==, "cancel;get-depends;get-details;get-files;get-packages;get-repo-list;"
		     "get-requires;get-update-detail;get-updates;install-files;install-packages;install-signature;"
		     "refresh-cache;remove-packages;repo-enable;repo-set-data;resolve;rollback;"
		     "search-details;search-file;search-group;search-name;update-packages;update-system;"
		     "what-provides;download-packages;get-distro-upgrades;simulate-install-packages;"
		     "simulate-remove-packages;simulate-update-packages;upgrade-system");
	g_free (text);

	g_object_unref (control);
}

static void
pk_test_desktop_func (void)
{
	PkDesktop *desktop;
	gboolean ret;
	gchar *package;
	GPtrArray *array;
	GError *error = NULL;

	desktop = pk_desktop_new ();
	g_assert (desktop != NULL);

	/* get package when not valid */
	package = pk_desktop_get_package_for_file (desktop, "/usr/share/applications/gpk-update-viewer.desktop", NULL);
	g_assert (package == NULL);

	/* file does not exist */
	ret = g_file_test (PK_DESKTOP_DEFAULT_DATABASE, G_FILE_TEST_EXISTS);
	if (!ret) {
		g_warning ("skipping checks as database does not exist");
		goto out;
	}

	/* open database */
	ret = pk_desktop_open_database (desktop, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get package */
	package = pk_desktop_get_package_for_file (desktop, "/usr/share/applications/gpk-update-viewer.desktop", NULL);

	/* dummy, not yum */
	if (g_strcmp0 (package, "vips-doc") == 0); {
		g_debug ("created db with dummy, skipping remaining tests");
		goto out;
	}
	g_assert_cmpstr (package, ==, "gnome-packagekit");
	g_free (package);

	/* get files */
	array = pk_desktop_get_files_for_package (desktop, "gnome-packagekit", NULL);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >=, 5);
	g_ptr_array_unref (array);

	/* get shown files */
	array = pk_desktop_get_shown_for_package (desktop, "gnome-packagekit", NULL);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >=, 3);
	g_ptr_array_unref (array);
out:
	g_object_unref (desktop);
}

static void
pk_test_enum_func (void)
{
	const gchar *string;
	PkRoleEnum role_value;
	guint i;

	/* find value */
	role_value = pk_role_enum_from_string ("search-file");
	g_assert_cmpint (role_value, ==, PK_ROLE_ENUM_SEARCH_FILE);

	/* find string */
	string = pk_role_enum_to_string (PK_ROLE_ENUM_SEARCH_FILE);
	g_assert_cmpstr (string, ==, "search-file");

	/* check we convert all the role bitfield */
	for (i=1; i<PK_ROLE_ENUM_LAST; i++) {
		string = pk_role_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the status bitfield */
	for (i=1; i<PK_STATUS_ENUM_LAST; i++) {
		string = pk_status_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the exit bitfield */
	for (i=0; i<PK_EXIT_ENUM_LAST; i++) {
		string = pk_exit_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the filter bitfield */
	for (i=0; i<PK_FILTER_ENUM_LAST; i++) {
		string = pk_filter_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the restart bitfield */
	for (i=0; i<PK_RESTART_ENUM_LAST; i++) {
		string = pk_restart_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the error_code bitfield */
	for (i=0; i<PK_ERROR_ENUM_LAST; i++) {
		string = pk_error_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the group bitfield */
	for (i=1; i<PK_GROUP_ENUM_LAST; i++) {
		string = pk_group_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the info bitfield */
	for (i=1; i<PK_INFO_ENUM_LAST; i++) {
		string = pk_info_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the sig_type bitfield */
	for (i=0; i<PK_SIGTYPE_ENUM_LAST; i++) {
		string = pk_sig_type_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the upgrade bitfield */
	for (i=0; i<PK_DISTRO_UPGRADE_ENUM_LAST; i++) {
		string = pk_distro_upgrade_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the license bitfield */
	for (i=0; i<PK_LICENSE_ENUM_LAST; i++) {
		string = pk_license_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}

	/* check we convert all the media type bitfield */
	for (i=0; i<PK_MEDIA_TYPE_ENUM_LAST; i++) {
		string = pk_media_type_enum_to_string (i);
		if (string == NULL) {
			/* so we get the value of i in the assert text */
			g_assert_cmpint (0, ==, i);
			break;
		}
	}
}

static void
pk_test_package_id_func (void)
{
	gboolean ret;
	gchar *text;
	gchar **sections;

	/* check not valid - NULL */
	ret = pk_package_id_check (NULL);
	g_assert (!ret);

	/* check not valid - no name */
	ret = pk_package_id_check (";0.0.1;i386;fedora");
	g_assert (!ret);

	/* check not valid - invalid */
	ret = pk_package_id_check ("moo;0.0.1;i386");
	g_assert (!ret);

	/* check valid */
	ret = pk_package_id_check ("moo;0.0.1;i386;fedora");
	g_assert (ret);

	/* id build */
	text = pk_package_id_build ("moo", "0.0.1", "i386", "fedora");
	g_assert_cmpstr (text, ==, "moo;0.0.1;i386;fedora");
	g_free (text);

	/* id build partial */
	text = pk_package_id_build ("moo", NULL, NULL, NULL);
	g_assert_cmpstr (text, ==, "moo;;;");
	g_free (text);

	/* test printable */
	text = pk_package_id_to_printable ("moo;0.0.1;i386;fedora");
	g_assert_cmpstr (text, ==, "moo-0.0.1.i386");
	g_free (text);

	/* test printable no arch */
	text = pk_package_id_to_printable ("moo;0.0.1;;");
	g_assert_cmpstr (text, ==, "moo-0.0.1");
	g_free (text);

	/* test printable just name */
	text = pk_package_id_to_printable ("moo;;;");
	g_assert_cmpstr (text, ==, "moo");
	g_free (text);

	/* test on real packageid */
	sections = pk_package_id_split ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;all;");
	g_assert (sections != NULL);
	g_assert_cmpstr (sections[0], ==, "kde-i18n-csb");
	g_assert_cmpstr (sections[1], ==, "4:3.5.8~pre20071001-0ubuntu1");
	g_assert_cmpstr (sections[2], ==, "all");
	g_assert_cmpstr (sections[3], ==, "");
	g_strfreev (sections);

	/* test on short packageid */
	sections = pk_package_id_split ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;;");
	g_assert (sections != NULL);
	g_assert_cmpstr (sections[0], ==, "kde-i18n-csb");
	g_assert_cmpstr (sections[1], ==, "4:3.5.8~pre20071001-0ubuntu1");
	g_assert_cmpstr (sections[2], ==, "");
	g_assert_cmpstr (sections[3], ==, "");
	g_strfreev (sections);

	/* test fail under */
	sections = pk_package_id_split ("foo;moo");
	g_assert (sections == NULL);

	/* test fail over */
	sections = pk_package_id_split ("foo;moo;dave;clive;dan");
	g_assert (sections == NULL);

	/* test fail missing first */
	sections = pk_package_id_split (";0.1.2;i386;data");
	g_assert (sections == NULL);
}

static void
pk_test_package_ids_func (void)
{
	gboolean ret;
	gchar *package_ids_blank[] = {};
	gchar **package_ids;

	/* parse va_list */
	package_ids = pk_package_ids_from_string ("foo;0.0.1;i386;fedora&bar;0.1.1;noarch;livna");
	g_assert (package_ids != NULL);

	/* verify size */
	g_assert_cmpint (g_strv_length (package_ids), ==, 2);

	/* verify blank */
	ret = pk_package_ids_check (package_ids_blank);
	g_assert (!ret);

	/* verify */
	ret = pk_package_ids_check (package_ids);
	g_assert (ret);

	g_strfreev (package_ids);
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

/**
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
		      "update-vendor-url", &text,
		      NULL);
	g_assert_cmpstr (text, ==, "http://www.distro-update.org/page?moo;Bugfix release for powertop");

	g_free (text);
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
pk_test_progress_func (void)
{
	PkProgress *progress;

	progress = pk_progress_new ();
	g_assert (progress != NULL);

	g_object_unref (progress);
}

static void
pk_test_progress_bar (void)
{
	PkProgressBar *progress_bar;

	progress_bar = pk_progress_bar_new ();
	g_assert (progress_bar != NULL);

	g_object_unref (progress_bar);
}

static void
pk_test_results_func (void)
{
	gboolean ret;
	PkResults *results;
	PkExitEnum exit_enum;
	GPtrArray *packages;
	PkPackage *item;
	PkInfoEnum info;
	gchar *package_id;
	gchar *summary;

	/* get results */
	results = pk_results_new ();
	g_assert (results != NULL);

	/* get exit code of unset results */
	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_UNKNOWN);

	/* get package list of unset results */
	packages = pk_results_get_package_array (results);
	g_assert_cmpint (packages->len, ==, 0);
	g_ptr_array_unref (packages);

	/* set valid exit code */
	ret = pk_results_set_exit_code (results, PK_EXIT_ENUM_CANCELLED);
	g_assert (ret);

	/* get exit code of set results */
	exit_enum = pk_results_get_exit_code (results);
	g_assert_cmpint (exit_enum, ==, PK_EXIT_ENUM_CANCELLED);

	/* add package */
	item = pk_package_new ();
	g_object_set (item,
		      "info", PK_INFO_ENUM_AVAILABLE,
		      "package-id", "gnome-power-manager;0.1.2;i386;fedora",
		      "summary", "Power manager for GNOME",
		      NULL);
	ret = pk_results_add_package (results, item);
	g_object_unref (item);
	g_assert (ret);

	/* get package list of set results */
	packages = pk_results_get_package_array (results);
	g_assert_cmpint (packages->len, ==, 1);

	/* check data */
	item = g_ptr_array_index (packages, 0);
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);
	g_assert_cmpint (info, ==, PK_INFO_ENUM_AVAILABLE);
	g_assert_cmpstr ("gnome-power-manager;0.1.2;i386;fedora", ==, package_id);
	g_assert_cmpstr ("Power manager for GNOME", ==, summary);
	g_object_ref (item);
	g_ptr_array_unref (packages);
	g_free (package_id);
	g_free (summary);

	/* check ref */
	g_object_get (item,
		      "info", &info,
		      "package-id", &package_id,
		      "summary", &summary,
		      NULL);
	g_assert_cmpint (info, ==, PK_INFO_ENUM_AVAILABLE);
	g_assert_cmpstr ("gnome-power-manager;0.1.2;i386;fedora", ==, package_id);
	g_assert_cmpstr ("Power manager for GNOME", ==, summary);
	g_object_unref (item);
	g_free (package_id);
	g_free (summary);

	g_object_unref (results);
}

static void
pk_test_service_pack_create_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkServicePack *pack = PK_SERVICE_PACK (object);
	GError *error = NULL;
	gboolean ret;

	/* get the results */
	ret = pk_service_pack_generic_finish (pack, res, &error);
	g_assert_no_error (error);
	g_assert (ret);

	_g_test_loop_quit ();
}

static void
pk_test_service_pack_progress_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
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
pk_test_service_pack_func (void)
{
	PkServicePack *pack;
	gchar **package_ids;

	pack = pk_service_pack_new ();
	g_assert (pack != NULL);

	/* install package */
	package_ids = pk_package_ids_from_id ("glib2;2.14.0;i386;fedora");
	pk_service_pack_create_for_package_ids_async (pack, "dave.servicepack", package_ids, NULL, NULL,
		        (PkProgressCallback) pk_test_service_pack_progress_cb, NULL,
		        (GAsyncReadyCallback) pk_test_service_pack_create_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (150000);
	g_debug ("installed in %f", g_test_timer_elapsed ());

	g_object_unref (pack);
}

static void
pk_test_task_install_packages_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	PkTask *task = PK_TASK (object);
	GError *error = NULL;
	PkResults *results;

	/* get the results */
	results = pk_task_generic_finish (task, res, &error);
	g_assert (results == NULL);
	g_assert_cmpstr (error->message, ==, "could not do untrusted question as no klass support");

	g_error_free (error);
	if (results != NULL)
		g_object_unref (results);
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
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, NULL, NULL, NULL,
		 (GAsyncReadyCallback) pk_test_transaction_list_resolve_cb, NULL);
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), package_ids, NULL, NULL, NULL,
		 (GAsyncReadyCallback) pk_test_transaction_list_resolve_cb, NULL);
	g_strfreev (package_ids);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* wait for remove */
	g_timeout_add (100, (GSourceFunc) pk_transaction_list_delay_cb, NULL);
	_g_test_loop_run_with_timeout (15000);
	g_debug ("resolved in %f", g_test_timer_elapsed ());

	/* correct number of added signals */
	g_assert_cmpint (_added, ==, 2);

	/* correct number of removed signals */
	g_assert_cmpint (_removed, ==, 2);

	g_object_unref (tlist);
	g_object_unref (client);
}

static void
pk_test_package_func (void)
{
	gboolean ret;
	PkPackage *package;
	const gchar *id;
	gchar *text;

	/* get package */
	package = pk_package_new ();
	g_assert (package != NULL);

	/* get id of unset package */
	id = pk_package_get_id (package);
	g_assert_cmpstr (id, ==, NULL);

	/* get id of unset package */
	g_object_get (package, "package-id", &text, NULL);
	g_assert_cmpstr (text, ==, NULL);
	g_free (text);

	/* set invalid id */
	ret = pk_package_set_id (package, "gnome-power-manager", NULL);
	g_assert (!ret);

	/* set invalid id (sections) */
	ret = pk_package_set_id (package, "gnome-power-manager;0.1.2;i386", NULL);
	g_assert (!ret);

	/* set invalid name */
	ret = pk_package_set_id (package, ";0.1.2;i386;fedora", NULL);
	g_assert (!ret);

	/* set valid name */
	ret = pk_package_set_id (package, "gnome-power-manager;0.1.2;i386;fedora", NULL);
	g_assert (ret);

	/* get id of set package */
	id = pk_package_get_id (package);
	g_assert_cmpstr (id, ==, "gnome-power-manager;0.1.2;i386;fedora");

	/* get name of set package */
	g_object_get (package, "package-id", &text, NULL);
	g_assert_cmpstr (text, ==, "gnome-power-manager;0.1.2;i386;fedora");
	g_free (text);

	g_object_unref (package);
}

int
main (int argc, char **argv)
{
	g_type_init ();

	g_test_init (&argc, &argv, NULL);

	pk_debug_set_verbose (TRUE);
	pk_debug_add_log_domain (G_LOG_DOMAIN);

	/* tests go here */
	g_test_add_func ("/packagekit-glib2/common", pk_test_common_func);
	g_test_add_func ("/packagekit-glib2/enum", pk_test_enum_func);
	g_test_add_func ("/packagekit-glib2/desktop", pk_test_desktop_func);
	g_test_add_func ("/packagekit-glib2/bitfield", pk_test_bitfield_func);
	g_test_add_func ("/packagekit-glib2/package-id", pk_test_package_id_func);
	g_test_add_func ("/packagekit-glib2/package-ids", pk_test_package_ids_func);
	g_test_add_func ("/packagekit-glib2/progress", pk_test_progress_func);
	g_test_add_func ("/packagekit-glib2/results", pk_test_results_func);
	g_test_add_func ("/packagekit-glib2/package", pk_test_package_func);
	g_test_add_func ("/packagekit-glib2/control", pk_test_control_func);
	g_test_add_func ("/packagekit-glib2/transaction-list", pk_test_transaction_list_func);
	g_test_add_func ("/packagekit-glib2/client-helper", pk_test_client_helper_func);
	g_test_add_func ("/packagekit-glib2/client", pk_test_client_func);
	g_test_add_func ("/packagekit-glib2/catalog", pk_test_catalog_func);
	g_test_add_func ("/packagekit-glib2/package-sack", pk_test_package_sack_func);
	g_test_add_func ("/packagekit-glib2/task", pk_test_task_func);
	g_test_add_func ("/packagekit-glib2/task-wrapper", pk_test_task_wrapper_func);
	g_test_add_func ("/packagekit-glib2/task-text", pk_test_task_text_func);
	g_test_add_func ("/packagekit-glib2/console", pk_test_console_func);
	g_test_add_func ("/packagekit-glib2/progress-bar", pk_test_progress_bar);
	g_test_add_func ("/packagekit-glib2/service-pack", pk_test_service_pack_func);

	return g_test_run ();
}

