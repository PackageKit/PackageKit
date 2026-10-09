/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2011 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2011-2014 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>

#include "pk-backend.h"
#include "pk-backend-process.h"
#include "pk-backend-protocol.h"
#include "pk-backend-spawn.h"
#include "pk-dbus.h"
#include "pk-engine.h"
#include "pk-spawn.h"
#include "pk-transaction-db.h"
#include "pk-transaction.h"
#include "pk-transaction-private.h"
#include "pk-scheduler.h"

#include <pk-json-private.h>

#define PK_TRANSACTION_ERROR_INPUT_INVALID 14
#define GET_DETAILS_TEST_DATA                                            \
	"details\tgimp;3.0.4-84;x86_64;Solus;\tGNU Image Manipulation "  \
	"Program\tGPL-3.0-or-later\tmultimedia\tGIMP is a mature image " \
	"editor.\thttps://www.gimp.org/\t"

/**
 * pk_test_conf_new:
 *
 * Loads the configuration generated for running from the build tree, which
 * points the backend loader at the modules built here.
 */
static GKeyFile *
pk_test_conf_new (void)
{
	GKeyFile *conf = g_key_file_new ();
	g_autoptr(GError) error = NULL;

	g_key_file_load_from_file (conf, PK_TEST_CONF_FILE, G_KEY_FILE_NONE, &error);
	g_assert_no_error (error);

	return conf;
}

/*
 * pk_test_remove_transaction_db:
 *
 * Deletes the transaction database a previous run may have left behind, so
 * the tests start from an empty one.
 */
static void
pk_test_remove_transaction_db (void)
{
	const gchar *filename;
	g_autoptr(GKeyFile) conf = pk_test_conf_new ();
	g_autoptr(PkTransactionDb) db = pk_transaction_db_new (conf);

	filename = pk_transaction_db_get_filename (db);
	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_debug ("Removing %s", filename);
		g_assert_cmpint (g_unlink (filename), ==, 0);
	}
}

/** ver:1.0 ***********************************************************/
static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
_g_test_hang_check_cb (gpointer user_data)
{
	guint timeout_ms = *((guint *) user_data);
	g_main_loop_quit (_test_loop);
	g_warning ("loop not completed in %ims", timeout_ms);
	g_assert_not_reached ();
	return FALSE;
}

static void
_g_test_loop_run_with_timeout (guint timeout_ms)
{
	g_assert_true (_test_loop_timeout_id == 0);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_check_cb, &timeout_ms);
	g_main_loop_run (_test_loop);
}

static gboolean
_g_test_hang_wait_cb (gpointer user_data)
{
	g_main_loop_quit (_test_loop);
	_test_loop_timeout_id = 0;
	return FALSE;
}

static void
_g_test_loop_wait (guint timeout_ms)
{
	g_assert_true (_test_loop_timeout_id == 0);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, _g_test_hang_wait_cb, &timeout_ms);
	g_main_loop_run (_test_loop);
}

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

static guint number_packages = 0;

static void
pk_test_backend_finished_cb (PkBackend *backend, PkExitEnum exit, gpointer user_data)
{
	_g_test_loop_quit ();
}

static void
pk_test_backend_watch_file_cb (PkBackend *backend, gpointer user_data)
{
	_g_test_loop_quit ();
}

static void
pk_test_backend_func_true (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	g_usleep (1000 * 1000);
	g_assert_cmpint (GPOINTER_TO_INT (user_data), ==, 999);
	/* trigger duplicate test */

	pk_backend_job_package_status (job,
				       "vips-doc;7.12.4-2.fc8;noarch;linva;",
				       PK_INFO_ENUM_AVAILABLE);
	pk_backend_job_package_status (job,
				       "vips-doc;7.12.4-2.fc8;noarch;linva;",
				       PK_INFO_ENUM_AVAILABLE);
}

static void
pk_test_backend_func_immediate_false (PkBackendJob *job, GVariant *params, gpointer user_data)
{}

static void
pk_test_backend_package_cb (PkBackend *backend, PkPackage *package, gpointer user_data)
{
	g_debug ("package:%s", pk_package_get_id (package));
	number_packages++;
}

static void
pk_test_backend_packages_cb (PkBackend *backend, GPtrArray *package_array, gpointer user_data)
{
	for (guint i = 0; i < package_array->len; i++) {
		PkPackage *package = g_ptr_array_index (package_array, i);

		g_debug ("package:%s", pk_package_get_id (package));
		number_packages++;
	}
}

static void
pk_test_backend_func (void)
{
	const gchar *text;
	gboolean ret;
	const gchar *filename;
	GError *error = NULL;
	g_autoptr(GKeyFile) conf = NULL;
	g_autoptr(PkBackend) backend = NULL;
	g_autoptr(PkBackendJob) job = NULL;

	/* get an backend */
	conf = pk_test_conf_new ();
	backend = pk_backend_new (conf);
	g_assert_true (backend != NULL);

	/* create a config file */
	filename = "/tmp/dave";
	ret = g_file_set_contents (filename, "foo", -1, NULL);
	g_assert_true (ret);

	/* set up a watch file on a config file */
	ret = pk_backend_watch_file (backend,
				     filename,
				     (PkBackendFileChanged) pk_test_backend_watch_file_cb,
				     NULL);
	g_assert_true (ret);

	/* change the config file */
	ret = g_file_set_contents (filename, "bar", -1, NULL);
	g_assert_true (ret);

	/* wait for config file change */
	_g_test_loop_run_with_timeout (5000);

	/* delete the config file */
	ret = g_unlink (filename);
	g_assert_true (!ret);

	/* wait for config file change */
	_g_test_loop_run_with_timeout (5000);

	/* connect */
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGE,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_package_cb),
				  NULL);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGES,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_packages_cb),
				  NULL);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_finished_cb),
				  NULL);

	/* get eula that does not exist */
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	g_assert_true (!ret);

	/* accept eula */
	pk_backend_accept_eula (backend, "license_foo");

	/* get eula that does exist */
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	g_assert_true (ret);

	/* accept eula (again) */
	pk_backend_accept_eula (backend, "license_foo");

	/* load an invalid backend */
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "invalid");
	ret = pk_backend_load (backend, &error);
	g_assert_error (error, 1, 0);
	g_assert_true (!ret);
	g_clear_error (&error);

	/* try to load a valid backend */
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "dummy");
	ret = pk_backend_load (backend, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* load an valid backend again */
	ret = pk_backend_load (backend, &error);
	g_assert_error (error, 1, 0);
	g_assert_true (!ret);
	g_clear_error (&error);

	/* get backend name */
	text = pk_backend_get_name (backend);
	g_assert_cmpstr (text, ==, "dummy");

	/* unlock an valid backend */
	ret = pk_backend_unload (backend);
	g_assert_true (ret);

	/* unlock an valid backend again */
	ret = pk_backend_unload (backend);
	g_assert_true (ret);

	/* check we are not finished */
	ret = pk_backend_job_get_is_finished (job);
	g_assert_true (!ret);

	/* check we have no error */
	ret = pk_backend_job_has_set_error_code (job);
	g_assert_true (!ret);

	/* wait for a thread to return true */
	ret = pk_backend_load (backend, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_job_thread_create (job,
					    pk_test_backend_func_true,
					    GINT_TO_POINTER (999),
					    NULL);
	g_assert_true (ret);

	/* wait for Finished */
	_g_test_loop_wait (2000);

	/* check duplicate filter */
	g_assert_cmpint (number_packages, ==, 1);

	/* reset */
	g_object_unref (job);
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);

	/* wait for a thread to return false (straight away) */
	ret = pk_backend_job_thread_create (job, pk_test_backend_func_immediate_false, NULL, NULL);
	g_assert_true (ret);

	/* wait for Finished */
	_g_test_loop_wait (10);

	/* reset */
	g_object_unref (job);
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);
	pk_backend_job_error_code (job, PK_ERROR_ENUM_GPG_FAILURE, "test error");

	/* get exit code from error code */
	g_assert_cmpint (pk_backend_job_get_exit_code (job), ==, PK_EXIT_ENUM_NEED_UNTRUSTED);
}

static guint _backend_spawn_number_packages = 0;

static void
pk_test_backend_spawn_finished_cb (PkBackendJob *job,
				   PkExitEnum exit,
				   PkBackendSpawn *backend_spawn)
{
	_g_test_loop_quit ();
}

static void
pk_test_backend_spawn_package_cb (PkBackend *backend,
				  PkInfoEnum info,
				  const gchar *package_id,
				  const gchar *summary,
				  PkBackendSpawn *backend_spawn)
{
	_backend_spawn_number_packages++;
}

static void
pk_test_backend_spawn_packages_cb (PkBackend *backend,
				   GPtrArray *package_array,
				   PkBackendSpawn *backend_spawn)
{
	_backend_spawn_number_packages += package_array->len;
}

static void
pk_test_backend_spawn_func (void)
{
	PkBackendSpawn *backend_spawn;
	const gchar *text;
	gboolean ret;
	gchar *uri;
	g_autoptr(GKeyFile) conf = NULL;
	g_autoptr(PkBackend) backend = NULL;
	g_autoptr(PkBackendJob) job = NULL;
	g_autoptr(GError) error = NULL;

	/* get an backend_spawn */
	conf = pk_test_conf_new ();
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "test_spawn");
	backend_spawn = pk_backend_spawn_new (conf);
	g_assert_true (backend_spawn != NULL);

	/* private copy for unref testing */
	backend = pk_backend_new (conf);
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);

	/* get backend name */
	text = pk_backend_spawn_get_name (backend_spawn);
	g_assert_cmpstr (text, ==, NULL);

	/* set backend name */
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	g_assert_true (ret);

	/* get backend name */
	text = pk_backend_spawn_get_name (backend_spawn);
	g_assert_cmpstr (text, ==, "test_spawn");

	/* test pk_backend_spawn_inject_data Percentage1 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\t0", &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data Percentage2 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\tbrian", NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data Percentage3 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\t12345", NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data Percentage4 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\t", NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data Percentage5 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage", NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data NoPercentageUpdates */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "no-percentage-updates", NULL);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data failure */
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    job,
					    "error\tnot-present-woohoo\tdescription text",
					    NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data Status */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "status\tquery", NULL);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data RequireRestart */
	ret = pk_backend_spawn_inject_data (
	    backend_spawn,
	    job,
	    "requirerestart\tsystem\tgnome-power-manager;0.0.1;i386;origin;data",
	    NULL);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data RequireRestart invalid enum */
	ret = pk_backend_spawn_inject_data (
	    backend_spawn,
	    job,
	    "requirerestart\tmooville\tgnome-power-manager;0.0.1;i386;origin;data",
	    NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data RequireRestart invalid PackageId */
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    job,
					    "requirerestart\tsystem\tdetails about the restart",
					    NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data AllowUpdate1 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "allow-cancel\ttrue", NULL);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data AllowUpdate2 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "allow-cancel\tbrian", NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data details - valid (install size, download size) */
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    job,
					    GET_DETAILS_TEST_DATA "145158504\t20920696",
					    NULL);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data details - valid (huge install size, huge download size) - actual sizes from "0ad-data;0.27.0-11;x86_64;Solus" */
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    job,
					    GET_DETAILS_TEST_DATA "3526938164\t1368603575",
					    NULL);
	g_assert_true (ret);

	/* test pk_backend_spawn_inject_data details - invalid (invalid size, valid download size) */
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    job,
					    GET_DETAILS_TEST_DATA "INVALID-SIZE\t1368603575",
					    NULL);
	g_assert_true (!ret);

	/* test pk_backend_spawn_inject_data details - invalid (valid size, invalid download size) */
	ret = pk_backend_spawn_inject_data (backend_spawn,
					    job,
					    GET_DETAILS_TEST_DATA
					    "145158504\tINVALID-DOWNLOAD-SIZE",
					    NULL);
	g_assert_true (!ret);

	/* convert proxy uri (bare) */
	uri = pk_backend_convert_uri ("username:password@server:port");
	g_assert_cmpstr (uri, ==, "http://username:password@server:port/");
	g_free (uri);

	/* convert proxy uri (full) */
	uri = pk_backend_convert_uri ("http://username:password@server:port/");
	g_assert_cmpstr (uri, ==, "http://username:password@server:port/");
	g_free (uri);

	/* convert proxy uri (partial) */
	uri = pk_backend_convert_uri ("ftp://username:password@server:port");
	g_assert_cmpstr (uri, ==, "ftp://username:password@server:port/");
	g_free (uri);

	/* test pk_backend_spawn_parse_common_out Package */
	ret = pk_backend_spawn_inject_data (
	    backend_spawn,
	    job,
	    "package\tinstalled\tgnome-power-manager;0.0.1;i386;origin;data\tMore useless software",
	    NULL);
	g_assert_true (ret);

	/* manually unlock as we have no engine */
	ret = pk_backend_unload (backend);
	g_assert_true (ret);

	/* reset */
	g_object_unref (backend_spawn);

	/* new */
	backend_spawn = pk_backend_spawn_new (conf);

	/* set backend name */
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	g_assert_true (ret);

	/* so we can spin until we finish */
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_spawn_finished_cb),
				  backend_spawn);

	/* so we can count the returned packages */
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGE,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_spawn_package_cb),
				  backend_spawn);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGES,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_spawn_packages_cb),
				  backend_spawn);

	/* test search-name.sh running */
	ret = pk_backend_spawn_helper (backend_spawn, job, "search-name.sh", "none", "bar", NULL);
	g_assert_true (ret);

	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* test number of packages */
	g_assert_cmpint (_backend_spawn_number_packages, ==, 2);

	/* manually unlock as we have no engine */
	ret = pk_backend_unload (backend);
	g_assert_true (ret);

	/* done */
	g_object_unref (backend_spawn);
}

static void
pk_test_dbus_func (void)
{
	g_autoptr(PkDbus) dbus = NULL;

	dbus = pk_dbus_new ();
	g_assert_true (dbus != NULL);
}

PkSpawnExitType mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
guint stdout_count = 0;
guint finished_count = 0;

static void
pk_test_exit_cb (PkSpawn *spawn, PkSpawnExitType exit, gpointer user_data)
{
	g_debug ("spawn exit=%i", exit);
	mexit = exit;
	finished_count++;
	_g_test_loop_quit ();
}

static void
pk_test_stdout_cb (PkSpawn *spawn, const gchar *line, gpointer user_data)
{
	g_debug ("stdout '%s'", line);
	stdout_count++;
}

static gboolean
cancel_cb (gpointer data)
{
	PkSpawn *spawn = PK_SPAWN (data);
	pk_spawn_kill (spawn);
	return FALSE;
}

static void
new_spawn_object (PkSpawn **pspawn)
{
	g_autoptr(GKeyFile) conf = NULL;
	if (*pspawn != NULL)
		g_object_unref (*pspawn);
	conf = pk_test_conf_new ();
	*pspawn = pk_spawn_new (conf);
	g_signal_connect (*pspawn, "exit", G_CALLBACK (pk_test_exit_cb), NULL);
	g_signal_connect (*pspawn, "stdout", G_CALLBACK (pk_test_stdout_cb), NULL);
	stdout_count = 0;
}

static gboolean
idle_cb (gpointer user_data)
{
	/* make sure dispatcher has closed when run idle add */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT);
	return FALSE;
}

static void
pk_test_spawn_func (void)
{
	GError *error = NULL;
	gboolean ret;
	g_autoptr(PkSpawn) spawn = NULL;
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) envp = NULL;

	new_spawn_object (&spawn);

	/* make sure return error for missing file */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit ("pk-spawn-test-xxx.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_error (error, 1, 0);
	g_strfreev (argv);
	g_assert_true (!ret);
	g_clear_error (&error);

	/* make sure finished wasn't called */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_UNKNOWN);

	/* make sure run correct helper */
	mexit = -1;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_strfreev (argv);

	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* make sure finished okay */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SUCCESS);

	/* make sure finished was called only once */
	g_assert_cmpint (finished_count, ==, 1);

	/* make sure we got the right stdout data */
	g_assert_cmpint (stdout_count, ==, 4 + 11);

	/* get new object */
	new_spawn_object (&spawn);

	/* make sure we set the proxy */
	mexit = -1;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-proxy.sh", " ", 0);
	envp = g_strsplit ("http_proxy=username:password@server:port "
			   "ftp_proxy=username:password@server:port",
			   " ",
			   0);
	ret = pk_spawn_argv (spawn, argv, envp, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_strfreev (argv);
	g_strfreev (envp);

	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* get new object */
	new_spawn_object (&spawn);

	/* run a helper that ignores SIGTERM, so it has to be SIGKILLed */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test-ignore-term.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_strfreev (argv);

	g_timeout_add_seconds (1, cancel_cb, spawn);
	/* wait for finished (SIGKILL fires PK_SPAWN_SIGKILL_DELAY after the cancel) */
	_g_test_loop_run_with_timeout (10000);

	/* make sure finished in SIGKILL */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SIGKILL);

	/* get new object */
	new_spawn_object (&spawn);

	/* with SIGKILL disabled the helper is only ever sent SIGTERM */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test.sh", " ", 0);
	g_object_set (spawn, "allow-sigkill", FALSE, NULL);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_strfreev (argv);

	g_timeout_add_seconds (1, cancel_cb, spawn);
	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* make sure finished in SIGTERM */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SIGTERM);

	/* get new object */
	new_spawn_object (&spawn);

	/* run a helper that handles SIGTERM and exits cleanly */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test-sigterm.py", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_strfreev (argv);

	g_timeout_add (1000, cancel_cb, spawn);
	/* wait for finished */
	_g_test_loop_run_with_timeout (2000);

	/* make sure finished in SIGTERM */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SIGTERM);

	/* run lots of data for profiling */
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test-profiling.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_strfreev (argv);

	/* get new object */
	new_spawn_object (&spawn);

	/* run the dispatcher */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-dispatcher.py\tsearch-name\tnone\tpower manager",
			   "\t",
			   0);
	envp = g_strsplit ("NETWORK=TRUE LANG=C.UTF-8 BACKGROUND=TRUE INTERACTIVE=TRUE UID=500",
			   " ",
			   0);
	ret = pk_spawn_argv (spawn, argv, envp, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* wait 2+2 seconds for the dispatcher */
	_g_test_loop_wait (4000);

	/* we got a package (+finished)? */
	g_assert_cmpint (stdout_count, ==, 2);

	/* dispatcher still alive? */
	g_assert_true (pk_spawn_is_running (spawn));

	/* run the dispatcher with new input */
	ret = pk_spawn_argv (spawn, argv, envp, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* this may take a while */
	_g_test_loop_wait (100);

	/* we got another package (and finished) */
	g_assert_cmpint (stdout_count, ==, 4);

	/* see if pk_spawn_exit blocks (required) */
	g_idle_add (idle_cb, NULL);

	/* ask dispatcher to close */
	ret = pk_spawn_exit (spawn);
	g_assert_true (ret);

	/* ask dispatcher to close (again, should be closing) */
	ret = pk_spawn_exit (spawn);
	g_assert_true (!ret);

	/* this may take a while */
	_g_test_loop_wait (100);

	/* did dispatcher close? */
	g_assert_true (!pk_spawn_is_running (spawn));

	/* did we get the right exit code */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT);

	/* ask dispatcher to close (again) */
	ret = pk_spawn_exit (spawn);
	g_assert_true (!ret);
}

/* ---- PkBackendProcess ---- */

static GPtrArray *bp_lines = NULL;
static gint bp_exit_type = -1;
static gint bp_exit_status = -1;
static guint bp_exited_count = 0;

static void
pk_test_backend_process_line_cb (PkBackendProcess *process, const gchar *line, gpointer user_data)
{
	g_debug ("protocol line '%s'", line);
	g_ptr_array_add (bp_lines, g_strdup (line));
	if (g_str_has_prefix (line, "echo:") || g_strcmp0 (line, "ready") == 0)
		_g_test_loop_quit ();
}

static void
pk_test_backend_process_exited_cb (PkBackendProcess *process,
				   gint exit_type,
				   gint status,
				   gpointer user_data)
{
	bp_exit_type = exit_type;
	bp_exit_status = status;
	bp_exited_count++;
	g_assert_false (pk_backend_process_is_running (process));
	_g_test_loop_quit ();
}

static PkBackendProcess *
pk_test_backend_process_new (void)
{
	PkBackendProcess *process = pk_backend_process_new ("test");
	g_signal_connect (process, "line", G_CALLBACK (pk_test_backend_process_line_cb), NULL);
	g_signal_connect (process, "exited", G_CALLBACK (pk_test_backend_process_exited_cb), NULL);
	g_clear_pointer (&bp_lines, g_ptr_array_unref);
	bp_lines = g_ptr_array_new_with_free_func (g_free);
	bp_exit_type = -1;
	bp_exit_status = -1;
	bp_exited_count = 0;
	return process;
}

static gboolean
pk_test_backend_process_kill_cb (gpointer user_data)
{
	pk_backend_process_kill (PK_BACKEND_PROCESS (user_data));
	return G_SOURCE_REMOVE;
}

static void
pk_test_backend_process_func (void)
{
	g_autoptr(PkBackendProcess) process = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *extra_env[] = { "PK_TEST_EXTRA=yes", NULL };
	gboolean ret;

	/* a missing executable fails synchronously and never emits ::exited */
	process = pk_test_backend_process_new ();
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-does-not-exist.sh",
					NULL,
					&error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false (ret);
	g_assert_false (pk_backend_process_is_running (process));
	g_clear_error (&error);
	ret = pk_backend_process_send_line (process, "hello", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);
	g_assert_false (ret);
	g_clear_error (&error);
	_g_test_loop_wait (100);
	g_assert_cmpuint (bp_exited_count, ==, 0);

	/* protocol round trip on fd 3, stdout noise ignored, stderr logged as a warning */
	g_clear_object (&process);
	process = pk_test_backend_process_new ();
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "*stderr: helper started, PATH=*");
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-echo.py",
					extra_env,
					&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_true (pk_backend_process_is_running (process));

	ret = pk_backend_process_send_line (process, "hello world", &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_lines->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (bp_lines, 0), ==, "echo:hello world");

	/* second request on the same instance */
	ret = pk_backend_process_send_line (process, "again", &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_lines->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (bp_lines, 1), ==, "echo:again");

	/* a deadline that is cleared in time does nothing */
	pk_backend_process_set_exit_deadline (process, 100);
	pk_backend_process_clear_exit_deadline (process);
	_g_test_loop_wait (300);
	g_assert_true (pk_backend_process_is_running (process));
	g_assert_cmpuint (bp_exited_count, ==, 0);

	/* clean exit on request */
	ret = pk_backend_process_send_line (process, "exit", &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_exited_count, ==, 1);
	g_assert_cmpint (bp_exit_type, ==, PK_BACKEND_PROCESS_EXIT_SUCCESS);
	g_assert_cmpint (bp_exit_status, ==, 0);
	g_assert_false (pk_backend_process_is_running (process));
	g_test_assert_expected_messages ();

	/* the same object can be started again; a non-zero exit is reported as failed */
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "*stderr: helper started, PATH=*");
	bp_exited_count = 0;
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-echo.py",
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-echo.py",
					NULL,
					&error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
	g_assert_false (ret);
	g_clear_error (&error);
	ret = pk_backend_process_send_line (process, "die", &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_exited_count, ==, 1);
	g_assert_cmpint (bp_exit_type, ==, PK_BACKEND_PROCESS_EXIT_FAILED);
	g_assert_cmpint (bp_exit_status, ==, 3);
	g_test_assert_expected_messages ();

	/* a helper that handles SIGTERM gets to say goodbye and exits cleanly */
	g_clear_object (&process);
	process = pk_test_backend_process_new ();
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-sigterm.py",
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_lines->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (bp_lines, 0), ==, "ready");
	pk_backend_process_set_exit_deadline (process, 200);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_exited_count, ==, 1);
	g_assert_cmpint (bp_exit_type, ==, PK_BACKEND_PROCESS_EXIT_SUCCESS);
	/* the goodbye line was delivered before ::exited */
	g_assert_cmpuint (bp_lines->len, ==, 2);
	g_assert_cmpstr (g_ptr_array_index (bp_lines, 1), ==, "bye");

	/* a helper without a SIGTERM handler dies from it */
	g_clear_object (&process);
	process = pk_test_backend_process_new ();
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "*stderr: helper started, PATH=*");
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-echo.py",
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_timeout_add (200, pk_test_backend_process_kill_cb, process);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_exited_count, ==, 1);
	g_assert_cmpint (bp_exit_type, ==, PK_BACKEND_PROCESS_EXIT_SIGTERM);
	g_assert_cmpint (bp_exit_status, ==, SIGTERM);
	g_test_assert_expected_messages ();

	/* a helper that ignores SIGTERM is SIGKILLed after the escalation delay */
	g_clear_object (&process);
	process = pk_test_backend_process_new ();
	g_object_set (process, "allow-sigkill", TRUE, NULL);
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "*ignored SIGTERM, sending SIGKILL*");
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-ignore-term.sh",
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	/* wait until the helper has installed its trap */
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpstr (g_ptr_array_index (bp_lines, 0), ==, "ready");
	pk_backend_process_set_exit_deadline (process, 200);
	_g_test_loop_run_with_timeout (10000);
	g_assert_cmpuint (bp_exited_count, ==, 1);
	g_assert_cmpint (bp_exit_type, ==, PK_BACKEND_PROCESS_EXIT_SIGKILL);
	g_assert_cmpint (bp_exit_status, ==, SIGKILL);
	g_test_assert_expected_messages ();

	/* without allow-sigkill the same helper survives SIGTERM well beyond the
	 * escalation delay; a second kill with allow-sigkill enabled then ends it
	 * immediately */
	g_clear_object (&process);
	process = pk_test_backend_process_new ();
	ret = pk_backend_process_start (process,
					TESTDATADIR "/pk-backend-process-ignore-term.sh",
					NULL,
					&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpstr (g_ptr_array_index (bp_lines, 0), ==, "ready");
	pk_backend_process_kill (process);
	_g_test_loop_wait (6000);
	g_assert_true (pk_backend_process_is_running (process));
	g_assert_cmpuint (bp_exited_count, ==, 0);
	g_object_set (process, "allow-sigkill", TRUE, NULL);
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "*ignored SIGTERM, sending SIGKILL*");
	pk_backend_process_kill (process);
	_g_test_loop_run_with_timeout (5000);
	g_assert_cmpuint (bp_exited_count, ==, 1);
	g_assert_cmpint (bp_exit_type, ==, PK_BACKEND_PROCESS_EXIT_SIGKILL);
	g_test_assert_expected_messages ();

	g_clear_object (&process);
	g_clear_pointer (&bp_lines, g_ptr_array_unref);
}

/* ---- PkBackendProtocol ---- */

static guint proto_packages = 0;
static guint proto_percentage = 0;
static gint proto_error = -1;
static gint proto_exit = -1;

static void
pk_test_backend_protocol_packages_cb (PkBackendJob *job, GPtrArray *packages, gpointer user_data)
{
	proto_packages += packages->len;
}

static void
pk_test_backend_protocol_percentage_cb (PkBackendJob *job, guint percentage, gpointer user_data)
{
	proto_percentage = percentage;
}

static void
pk_test_backend_protocol_error_cb (PkBackendJob *job, PkError *item, gpointer user_data)
{
	proto_error = pk_error_get_code (item);
}

static void
pk_test_backend_protocol_finished_cb (PkBackendJob *job, PkExitEnum exit, gpointer user_data)
{
	proto_exit = exit;
}

static PkBackendJob *
pk_test_backend_protocol_job_new (GKeyFile *conf)
{
	PkBackendJob *job = pk_backend_job_new (conf);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGES,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_protocol_packages_cb),
				  NULL);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PERCENTAGE,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_protocol_percentage_cb),
				  NULL);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_ERROR_CODE,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_protocol_error_cb),
				  NULL);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  PK_BACKEND_JOB_VFUNC (pk_test_backend_protocol_finished_cb),
				  NULL);
	proto_packages = 0;
	proto_percentage = 0;
	proto_error = -1;
	proto_exit = -1;
	return job;
}

static void
pk_test_backend_protocol_func (void)
{
	g_autoptr(GKeyFile) conf = pk_test_conf_new ();
	g_autoptr(PkBackendJob) job = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *line = NULL;
	g_autoptr(json_t) root = NULL;
	g_autoptr(PkBackendHello) hello = NULL;
	const gchar *values[] = { "power manager", "foo", NULL };
	gboolean finished = FALSE;
	gboolean ret;
	json_error_t jerr;

	/* request: search-name carries filters, values and the context */
	job = pk_test_backend_protocol_job_new (conf);
	pk_backend_job_set_role (job, PK_ROLE_ENUM_SEARCH_NAME);
	pk_backend_job_set_parameters (
	    job,
	    g_variant_new ("(t^as)",
			   pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
						   PK_FILTER_ENUM_NOT_DEVELOPMENT,
						   -1),
			   values));
	pk_backend_job_set_locale (job, "de_DE.UTF-8");
	pk_backend_job_set_uid (job, 1000);
	pk_backend_job_set_proxy (job, "http://proxy:3128", NULL, NULL, NULL, "localhost", NULL);
	line = pk_backend_protocol_build_run (job, "7", &error);
	g_assert_no_error (error);
	g_assert_nonnull (line);
	root = json_loads (line, 0, &jerr);
	g_assert_nonnull (root);
	g_assert_cmpstr (json_string_value (json_object_get (root, "op")), ==, "run");
	g_assert_cmpstr (json_string_value (json_object_get (root, "job")), ==, "7");
	g_assert_cmpstr (json_string_value (json_object_get (root, "role")), ==, "search-name");
	g_assert_cmpuint (json_array_size (json_object_get (root, "filters")), ==, 2);
	g_assert_cmpstr (json_string_value (json_array_get (json_object_get (root, "filters"), 1)),
			 ==,
			 "~devel");
	g_assert_cmpstr (json_string_value (json_array_get (json_object_get (root, "values"), 0)),
			 ==,
			 "power manager");
	g_assert_cmpstr (
	    json_string_value (json_object_get (json_object_get (root, "ctx"), "locale")),
	    ==,
	    "de_DE.UTF-8");
	g_assert_cmpint (
	    json_integer_value (json_object_get (json_object_get (root, "ctx"), "uid")),
	    ==,
	    1000);
	g_assert_true (json_is_null (json_object_get (json_object_get (root, "ctx"), "cache_age")));
	g_assert_cmpstr (
	    json_string_value (
		json_object_get (json_object_get (json_object_get (root, "ctx"), "proxy"), "http")),
	    ==,
	    "http://proxy:3128");
	g_assert_null (
	    json_object_get (json_object_get (json_object_get (root, "ctx"), "proxy"), "https"));
	g_clear_pointer (&line, g_free);
	g_clear_pointer (&root, json_decref);

	/* request: install-signature gets its type from the parameters */
	g_clear_object (&job);
	job = pk_test_backend_protocol_job_new (conf);
	pk_backend_job_set_role (job, PK_ROLE_ENUM_INSTALL_SIGNATURE);
	pk_backend_job_set_parameters (
	    job,
	    g_variant_new ("(uss)", PK_SIGTYPE_ENUM_GPG, "ABCD", "foo;1;x86_64;main;"));
	line = pk_backend_protocol_build_run (job, "8", &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (line, "\"sig_type\":\"gpg\""));
	g_clear_pointer (&line, g_free);

	/* request: a role without wire representation is refused */
	g_clear_object (&job);
	job = pk_test_backend_protocol_job_new (conf);
	pk_backend_job_set_role (job, PK_ROLE_ENUM_CANCEL);
	line = pk_backend_protocol_build_run (job, "9", &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_null (line);
	g_clear_error (&error);

	/* the fixed requests */
	line = pk_backend_protocol_build_hello ();
	g_assert_nonnull (strstr (line, "\"protocol\":1"));
	g_clear_pointer (&line, g_free);
	line = pk_backend_protocol_build_cancel ("7");
	g_assert_cmpstr (line, ==, "{\"op\":\"cancel\",\"job\":\"7\"}");
	g_clear_pointer (&line, g_free);
	line = pk_backend_protocol_build_exit ();
	g_assert_cmpstr (line, ==, "{\"op\":\"exit\"}");
	g_clear_pointer (&line, g_free);

	/* hello: good */
	hello = pk_backend_protocol_parse_hello (
	    "{\"ev\":\"hello\",\"protocol\":1,\"name\":\"test\","
	    "\"roles\":[\"search-name\",\"install-packages\"],"
	    "\"filters\":[\"installed\"],\"groups\":[\"games\"],"
	    "\"mime_types\":[\"application/x-tar\"],\"parallel\":false}",
	    &error);
	g_assert_no_error (error);
	g_assert_nonnull (hello);
	g_assert_cmpstr (hello->name, ==, "test");
	g_assert_null (hello->description);
	g_assert_true (pk_bitfield_contain (hello->roles, PK_ROLE_ENUM_SEARCH_NAME));
	g_assert_true (pk_bitfield_contain (hello->roles, PK_ROLE_ENUM_INSTALL_PACKAGES));
	g_assert_false (pk_bitfield_contain (hello->roles, PK_ROLE_ENUM_REMOVE_PACKAGES));
	g_assert_true (pk_bitfield_contain (hello->filters, PK_FILTER_ENUM_INSTALLED));
	g_assert_true (pk_bitfield_contain (hello->groups, PK_GROUP_ENUM_GAMES));
	g_assert_cmpstr (hello->mime_types[0], ==, "application/x-tar");
	g_clear_pointer (&hello, pk_backend_hello_free);

	/* hello: wrong version, parallel, unknown role, not a hello */
	hello = pk_backend_protocol_parse_hello (
	    "{\"ev\":\"hello\",\"protocol\":2,\"roles\":[],\"parallel\":false}",
	    &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_null (hello);
	g_clear_error (&error);
	hello = pk_backend_protocol_parse_hello (
	    "{\"ev\":\"hello\",\"protocol\":1,\"roles\":[],\"parallel\":true}",
	    &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_clear_error (&error);
	hello = pk_backend_protocol_parse_hello (
	    "{\"ev\":\"hello\",\"protocol\":1,\"roles\":[\"fly\"],\"parallel\":false}",
	    &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_clear_error (&error);
	hello = pk_backend_protocol_parse_hello ("{\"ev\":\"status\",\"job\":\"1\"}", &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_clear_error (&error);

	/* events: fatal protocol errors */
	g_clear_object (&job);
	job = pk_test_backend_protocol_job_new (conf);
	ret = pk_backend_protocol_handle_event ("this is not json", "1", job, &finished, &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_PARSE_FAILED);
	g_assert_false (ret);
	g_clear_error (&error);
	ret = pk_backend_protocol_handle_event ("[1,2]", "1", job, &finished, &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_false (ret);
	g_clear_error (&error);
	ret = pk_backend_protocol_handle_event ("{\"job\":\"1\"}", "1", job, &finished, &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_false (ret);
	g_clear_error (&error);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"status\",\"job\":\"1\",\"status\":\"dancing\"}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_false (ret);
	g_clear_error (&error);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"percentage\",\"job\":\"1\",\"value\":\"50\"}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_false (ret);
	g_clear_error (&error);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"packages\",\"job\":\"1\",\"items\":[{\"package_id\":\"broken\",\"info\":"
	    "\"installed\"}]}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_error (error, PK_JSON_ERROR, PK_JSON_ERROR_INVALID);
	g_assert_false (ret);
	g_clear_error (&error);

	/* events: ignored ones */
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "ignoring unknown event 'dance' from helper");
	ret = pk_backend_protocol_handle_event ("{\"ev\":\"dance\",\"job\":\"1\"}",
						"1",
						job,
						&finished,
						&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_test_assert_expected_messages ();
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"percentage\",\"job\":\"2\",\"value\":99}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"percentage\",\"job\":\"1\",\"value\":99}",
	    NULL,
	    NULL,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"log\",\"job\":null,\"level\":\"debug\",\"message\":\"hi\"}",
	    NULL,
	    NULL,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_wait (10);
	g_assert_cmpuint (proto_percentage, ==, 0);

	/* events: the happy path of a job */
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"status\",\"job\":\"1\",\"status\":\"query\"}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"percentage\",\"job\":\"1\",\"value\":null}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	_g_test_loop_wait (10);
	g_assert_cmpuint (proto_percentage, ==, PK_BACKEND_PERCENTAGE_INVALID);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"percentage\",\"job\":\"1\",\"value\":42}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"packages\",\"job\":\"1\",\"items\":["
	    "{\"package_id\":\"foo;1;x86_64;main;\",\"info\":\"installed\",\"summary\":\"A foo\"},"
	    "{\"package_id\":\"bar;2;x86_64;main;\",\"info\":\"available\",\"summary\":\"A "
	    "bar\",\"severity\":\"security\"}]}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_protocol_handle_event (
	    "{\"ev\":\"package-status\",\"job\":\"1\",\"package_id\":\"foo;1;x86_64;main;\","
	    "\"info\":\"installing\"}",
	    "1",
	    job,
	    &finished,
	    &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = pk_backend_protocol_handle_event ("{\"ev\":\"error\",\"job\":\"1\",\"code\":"
						"\"package-not-found\",\"details\":\"no\\nsuch\"}",
						"1",
						job,
						&finished,
						&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_false (finished);
	ret = pk_backend_protocol_handle_event ("{\"ev\":\"finished\",\"job\":\"1\"}",
						"1",
						job,
						&finished,
						&error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_true (finished);
	_g_test_loop_wait (50);
	g_assert_cmpuint (proto_percentage, ==, 42);
	g_assert_cmpuint (proto_packages, ==, 2);
	g_assert_cmpint (proto_error, ==, PK_ERROR_ENUM_PACKAGE_NOT_FOUND);
	g_assert_cmpint (proto_exit, ==, PK_EXIT_ENUM_FAILED);
}

static void
pk_test_transaction_func (void)
{
	gboolean ret;
	GError *error = NULL;
	g_autoptr(GDBusNodeInfo) introspection = NULL;
	g_autoptr(PkTransaction) transaction = NULL;
	g_autoptr(GKeyFile) conf = NULL;

	introspection = pk_load_introspection (PK_DBUS_INTERFACE_TRANSACTION ".xml", NULL);
	g_assert_true (introspection != NULL);

	/* get PkTransaction object */
	conf = pk_test_conf_new ();
	transaction = pk_transaction_new (conf, introspection);
	g_assert_true (transaction != NULL);

	/* validate incorrect text */
	ret = pk_transaction_strvalidate ("richard$\xffhughes", &error);
	g_assert_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID);
	g_assert_true (!ret);
	g_clear_error (&error);

	/* validate correct text */
	ret = pk_transaction_strvalidate ("richardhughes", &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_clear_error (&error);

	/* reject control characters used as the spawn stdin protocol delimiters */
	ret = pk_transaction_strvalidate ("evil\tsecond", &error);
	g_assert_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID);
	g_assert_true (!ret);
	g_clear_error (&error);

	ret = pk_transaction_strvalidate ("evil\ninstall-packages", &error);
	g_assert_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID);
	g_assert_true (!ret);
	g_clear_error (&error);

	ret = pk_transaction_strvalidate ("evil\rsecond", &error);
	g_assert_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID);
	g_assert_true (!ret);
	g_clear_error (&error);

	/* validate distro IDs */
	{
		const gchar *valid[] = { "fedora-14", "42",
					 "rawhide",   "openSUSE-Leap-15.6",
					 "24.04",     "1.0_alpha+1",
					 NULL };
		const gchar *invalid[] = { "",
					   "..",
					   "../../../tmp/pwn",
					   "fedora-14/../../etc",
					   "fedora\\14",
					   ".hidden",
					   "-42",
					   "fedora 14",
					   "fedora;14",
					   "fedora\n14",
					   "fedora..14",
					   NULL };

		for (guint i = 0; valid[i] != NULL; i++) {
			ret = pk_transaction_distro_id_validate (valid[i], &error);
			g_assert_no_error (error);
			g_assert_true (ret);
			g_clear_error (&error);
		}
		for (guint i = 0; invalid[i] != NULL; i++) {
			ret = pk_transaction_distro_id_validate (invalid[i], &error);
			g_assert_error (error,
					PK_TRANSACTION_ERROR,
					PK_TRANSACTION_ERROR_INPUT_INVALID);
			g_assert_true (!ret);
			g_clear_error (&error);
		}
	}
}

static void
pk_test_transaction_db_func (void)
{
	guint value;
	gchar *tid;
	gboolean ret;
	gdouble ms;
	GError *error = NULL;
	g_autoptr(PkTransactionDb) db = NULL;
	g_autoptr(GKeyFile) conf = pk_test_conf_new ();
	g_autofree gchar *proxy_http = NULL;
	g_autofree gchar *proxy_ftp = NULL;

	/* remove the self check file */
	pk_test_remove_transaction_db ();

	/* check we created quickly */
	g_test_timer_start ();
	db = pk_transaction_db_new (conf);
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ms = g_test_timer_elapsed ();
	g_assert_cmpfloat (ms, <, 1.5);
	g_object_unref (db);

	/* check we opened quickly */
	g_test_timer_start ();
	db = pk_transaction_db_new (conf);
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ms = g_test_timer_elapsed ();
	g_assert_cmpfloat (ms, <, 0.1);

	/* do we get the correct time on a blank database */
	value = pk_transaction_db_action_time_since (db, PK_ROLE_ENUM_REFRESH_CACHE);
	g_assert_cmpint (value, ==, G_MAXUINT);

	/* get an tid object */
	g_test_timer_start ();
	tid = pk_transaction_db_generate_id (db);
	ms = g_test_timer_elapsed ();
	g_assert_cmpfloat (ms, <, 0.002);
	g_free (tid);

	/* get an tid object (no wait) */
	g_test_timer_start ();
	tid = pk_transaction_db_generate_id (db);
	ms = g_test_timer_elapsed ();
	g_assert_cmpfloat (ms, <, 0.005);
	g_free (tid);

	/* set the correct time */
	ret = pk_transaction_db_action_time_reset (db, PK_ROLE_ENUM_REFRESH_CACHE);
	g_assert_true (ret);

	/* do the deferred write */
	g_test_timer_start ();
	_g_test_loop_wait (2000);

	/* do we get the correct time */
	value = pk_transaction_db_action_time_since (db, PK_ROLE_ENUM_REFRESH_CACHE);
	g_assert_cmpint (value, >, 1);
	g_assert_cmpint (value, <=, 4);

	/* can we set the proxies */
	ret = pk_transaction_db_set_proxy (db,
					   500,
					   "session1",
					   "127.0.0.1:80",
					   NULL,
					   "127.0.0.1:21",
					   NULL,
					   NULL,
					   NULL);
	g_assert_true (ret);

	/* can we set the proxies (overwrite) */
	ret = pk_transaction_db_set_proxy (db,
					   500,
					   "session1",
					   "127.0.0.1:80",
					   NULL,
					   "127.0.0.1:21",
					   NULL,
					   NULL,
					   NULL);
	g_assert_true (ret);

	/* can we get the proxies (non-existant user) */
	ret = pk_transaction_db_get_proxy (db,
					   501,
					   "session1",
					   &proxy_http,
					   NULL,
					   &proxy_ftp,
					   NULL,
					   NULL,
					   NULL);
	g_assert_true (ret);
	g_assert_cmpstr (proxy_http, ==, NULL);
	g_assert_cmpstr (proxy_ftp, ==, NULL);

	/* can we get the proxies (non-existant session) */
	ret = pk_transaction_db_get_proxy (db,
					   500,
					   "session2",
					   &proxy_http,
					   NULL,
					   &proxy_ftp,
					   NULL,
					   NULL,
					   NULL);
	g_assert_true (ret);
	g_assert_cmpstr (proxy_http, ==, NULL);
	g_assert_cmpstr (proxy_ftp, ==, NULL);

	/* can we get the proxies (match) */
	ret = pk_transaction_db_get_proxy (db,
					   500,
					   "session1",
					   &proxy_http,
					   NULL,
					   &proxy_ftp,
					   NULL,
					   NULL,
					   NULL);
	g_assert_true (ret);
	g_assert_cmpstr (proxy_http, ==, "127.0.0.1:80");
	g_assert_cmpstr (proxy_ftp, ==, "127.0.0.1:21");
}

static PkTransactionDb *db = NULL;

static void
pk_test_scheduler_finished_cb (PkTransaction *transaction,
			       const gchar *exit_text,
			       guint time,
			       gpointer user_data)
{
	_g_test_loop_quit ();
}

static gchar *
pk_test_scheduler_create_transaction (PkScheduler *tlist)
{
	gchar *tid;
	gboolean ret;
	GError *error = NULL;

	/* get tid */
	tid = pk_transaction_db_generate_id (db);

	/* create PkTransaction instance */
	ret = pk_scheduler_create (tlist, tid, ":org.freedesktop.PackageKit", &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	return tid;
}

/* ---- generic spawn module, driven through the real loader and a Python helper ---- */

static guint sm_packages = 0;
static guint sm_details = 0;
static guint sm_update_details = 0;
static guint sm_files = 0;
static guint sm_repo_details = 0;
static guint sm_distro_upgrades = 0;
static guint sm_item_progress = 0;
static guint sm_require_restart = 0;
static guint sm_eula = 0;
static guint sm_repo_sig = 0;
static guint sm_percentage = 0;
static gint sm_status = -1;
static gint sm_error = -1;
static gint sm_exit = -1;

static void
pk_test_spawn_module_packages_cb (PkBackendJob *job, GPtrArray *packages, gpointer user_data)
{
	sm_packages += packages->len;
}

static void
pk_test_spawn_module_details_cb (PkBackendJob *job, PkDetails *item, gpointer user_data)
{
	g_assert_cmpstr (pk_details_get_license (item), ==, "MIT");
	g_assert_cmpuint (pk_details_get_size (item), ==, 1234);
	sm_details++;
}

static void
pk_test_spawn_module_update_details_cb (PkBackendJob *job, GPtrArray *items, gpointer user_data)
{
	sm_update_details += items->len;
}

static void
pk_test_spawn_module_files_cb (PkBackendJob *job, PkFiles *item, gpointer user_data)
{
	g_assert_cmpuint (g_strv_length (pk_files_get_files (item)), ==, 2);
	sm_files++;
}

static void
pk_test_spawn_module_repo_detail_cb (PkBackendJob *job, PkRepoDetail *item, gpointer user_data)
{
	sm_repo_details++;
}

static void
pk_test_spawn_module_distro_upgrade_cb (PkBackendJob *job,
					PkDistroUpgrade *item,
					gpointer user_data)
{
	sm_distro_upgrades++;
}

static void
pk_test_spawn_module_item_progress_cb (PkBackendJob *job, PkItemProgress *item, gpointer user_data)
{
	sm_item_progress++;
}

static void
pk_test_spawn_module_require_restart_cb (PkBackendJob *job,
					 PkRequireRestart *item,
					 gpointer user_data)
{
	sm_require_restart++;
}

static void
pk_test_spawn_module_eula_cb (PkBackendJob *job, PkEulaRequired *item, gpointer user_data)
{
	g_assert_cmpstr (pk_eula_required_get_eula_id (item), ==, "eula-1");
	sm_eula++;
}

static void
pk_test_spawn_module_repo_sig_cb (PkBackendJob *job,
				  PkRepoSignatureRequired *item,
				  gpointer user_data)
{
	g_autofree gchar *key_id = NULL;
	g_object_get (item, "key-id", &key_id, NULL);
	g_assert_cmpstr (key_id, ==, "ABCDEF");
	sm_repo_sig++;
}

static void
pk_test_spawn_module_percentage_cb (PkBackendJob *job, guint percentage, gpointer user_data)
{
	sm_percentage = percentage;
}

static void
pk_test_spawn_module_status_cb (PkBackendJob *job, PkStatusEnum status, gpointer user_data)
{
	sm_status = status;
}

static void
pk_test_spawn_module_error_cb (PkBackendJob *job, PkError *item, gpointer user_data)
{
	sm_error = pk_error_get_code (item);
}

static void
pk_test_spawn_module_finished_cb (PkBackendJob *job, PkExitEnum exit, gpointer user_data)
{
	sm_exit = exit;
	_g_test_loop_quit ();
}

static PkBackendJob *
pk_test_spawn_module_job_new (GKeyFile *conf, PkBackend *backend)
{
	PkBackendJob *job = pk_backend_job_new (conf);
	struct
	{
		PkBackendJobSignal signal;
		gpointer cb;
	} vfuncs[] = {
		{ PK_BACKEND_SIGNAL_PACKAGES, pk_test_spawn_module_packages_cb },
		{ PK_BACKEND_SIGNAL_DETAILS, pk_test_spawn_module_details_cb },
		{ PK_BACKEND_SIGNAL_UPDATE_DETAILS, pk_test_spawn_module_update_details_cb },
		{ PK_BACKEND_SIGNAL_FILES, pk_test_spawn_module_files_cb },
		{ PK_BACKEND_SIGNAL_REPO_DETAIL, pk_test_spawn_module_repo_detail_cb },
		{ PK_BACKEND_SIGNAL_DISTRO_UPGRADE, pk_test_spawn_module_distro_upgrade_cb },
		{ PK_BACKEND_SIGNAL_ITEM_PROGRESS, pk_test_spawn_module_item_progress_cb },
		{ PK_BACKEND_SIGNAL_REQUIRE_RESTART, pk_test_spawn_module_require_restart_cb },
		{ PK_BACKEND_SIGNAL_EULA_REQUIRED, pk_test_spawn_module_eula_cb },
		{ PK_BACKEND_SIGNAL_REPO_SIGNATURE_REQUIRED, pk_test_spawn_module_repo_sig_cb },
		{ PK_BACKEND_SIGNAL_PERCENTAGE, pk_test_spawn_module_percentage_cb },
		{ PK_BACKEND_SIGNAL_STATUS_CHANGED, pk_test_spawn_module_status_cb },
		{ PK_BACKEND_SIGNAL_ERROR_CODE, pk_test_spawn_module_error_cb },
		{ PK_BACKEND_SIGNAL_FINISHED, pk_test_spawn_module_finished_cb },
	};

	pk_backend_job_set_backend (job, backend);
	for (guint i = 0; i < G_N_ELEMENTS (vfuncs); i++)
		pk_backend_job_set_vfunc (job,
					  vfuncs[i].signal,
					  PK_BACKEND_JOB_VFUNC (vfuncs[i].cb),
					  NULL);
	sm_packages = sm_details = sm_update_details = sm_files = sm_repo_details = 0;
	sm_distro_upgrades = sm_item_progress = sm_require_restart = sm_eula = sm_repo_sig = 0;
	sm_percentage = 0;
	sm_status = sm_error = sm_exit = -1;
	pk_backend_start_job (backend, job);
	return job;
}

/* waits for the job to finish, then releases it */
static void
pk_test_spawn_module_job_wait (PkBackend *backend, PkBackendJob *job, guint timeout_ms)
{
	_g_test_loop_run_with_timeout (timeout_ms);
	g_assert_true (pk_backend_job_get_is_finished (job));
	pk_backend_stop_job (backend, job);
	g_object_unref (job);
}

static void
pk_test_spawn_module_func (void)
{
	gboolean ret;
	const gchar *package_ids[] = { "foo;1.0;x86_64;main;", "bar;2.1;x86_64;main;", NULL };
	const gchar *values[] = { "ba", "foo", NULL };
	g_autoptr(GError) error = NULL;
	g_autoptr(GKeyFile) conf = NULL;
	g_autoptr(PkBackend) backend = NULL;
	g_autoptr(PkBackend) broken = NULL;
	g_autofree gchar *root_dir = NULL;
	g_autofree gchar *broken_dir = NULL;
	g_autofree gchar *broken_manifest = NULL;
	g_auto(GStrv) mime_types = NULL;
	PkBackendJob *job;

	conf = pk_test_conf_new ();
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "test_spawn");
	g_key_file_set_integer (conf, "Daemon", "BackendShutdownTimeout", 1);

	/* no module of that name, but a manifest: the spawn module is loaded and says hello */
	backend = pk_backend_new (conf);
	ret = pk_backend_load (backend, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	g_assert_cmpstr (pk_backend_get_name (backend), ==, "test_spawn");
	g_assert_cmpstr (pk_backend_get_description (backend), ==, "Test spawn backend");
	g_assert_cmpstr (pk_backend_get_author (backend), ==, "PackageKit developers");
	g_assert_true (
	    pk_bitfield_contain (pk_backend_get_roles (backend), PK_ROLE_ENUM_SEARCH_NAME));
	g_assert_true (pk_bitfield_contain (pk_backend_get_roles (backend), PK_ROLE_ENUM_RESOLVE));
	g_assert_false (
	    pk_bitfield_contain (pk_backend_get_roles (backend), PK_ROLE_ENUM_SEARCH_GROUP));
	g_assert_true (
	    pk_bitfield_contain (pk_backend_get_filters (backend), PK_FILTER_ENUM_DEVELOPMENT));
	g_assert_true (pk_bitfield_contain (pk_backend_get_groups (backend), PK_GROUP_ENUM_SYSTEM));
	mime_types = pk_backend_get_mime_types (backend);
	g_assert_cmpstr (mime_types[0], ==, "application/x-test");
	g_assert_null (mime_types[1]);

	/* a query: two batches of packages, progress, status */
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_search_names (backend,
				 job,
				 pk_bitfield_value (PK_FILTER_ENUM_INSTALLED),
				 (gchar **) values);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_exit, ==, PK_EXIT_ENUM_SUCCESS);
	g_assert_cmpint (sm_error, ==, -1);
	g_assert_cmpuint (sm_packages, ==, 3);
	g_assert_cmpuint (sm_percentage, ==, 100);
	g_assert_cmpint (sm_status, ==, PK_STATUS_ENUM_FINISHED);

	/* the other result events, on the same helper */
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_get_details (backend, job, (gchar **) package_ids);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_exit, ==, PK_EXIT_ENUM_SUCCESS);
	g_assert_cmpuint (sm_details, ==, 2);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_get_update_detail (backend, job, (gchar **) package_ids);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_update_details, ==, 2);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_get_files (backend, job, (gchar **) package_ids);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_files, ==, 2);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_get_repo_list (backend, job, 0);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_repo_details, ==, 2);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_get_distro_upgrades (backend, job);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_distro_upgrades, ==, 1);

	/* transaction progress events */
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_install_packages (backend,
				     job,
				     pk_bitfield_value (PK_TRANSACTION_FLAG_ENUM_SIMULATE),
				     (gchar **) package_ids);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_exit, ==, PK_EXIT_ENUM_SUCCESS);
	g_assert_cmpuint (sm_item_progress, ==, 2);
	g_assert_cmpuint (sm_require_restart, ==, 1);

	/* interaction events */
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_repo_enable (backend, job, "main", TRUE);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_eula, ==, 1);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_repo_set_data (backend, job, "main", "key", "value");
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_repo_sig, ==, 1);

	/* errors: a specific one, and an exception in the helper */
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_install_signature (backend, job, PK_SIGTYPE_ENUM_GPG, "KEY", package_ids[0]);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_error, ==, PK_ERROR_ENUM_GPG_FAILURE);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_resolve (backend, job, 0, (gchar **) values);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_error, ==, PK_ERROR_ENUM_INTERNAL_ERROR);
	g_assert_cmpint (sm_exit, ==, PK_EXIT_ENUM_FAILED);

	/* in-band cancel: the helper stops at its next check */
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_install_packages (backend, job, 0, (gchar **) package_ids);
	pk_backend_cancel (backend, job);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_error, ==, PK_ERROR_ENUM_TRANSACTION_CANCELLED);

	/* the helper exits mid-job: internal error, and the next job gets a fresh helper */
	g_test_expect_message ("PackageKit-Spawn",
			       G_LOG_LEVEL_WARNING,
			       "*helper exited during job*");
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_refresh_cache (backend, job, TRUE);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_error, ==, PK_ERROR_ENUM_INTERNAL_ERROR);

	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_refresh_cache (backend, job, FALSE);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpint (sm_exit, ==, PK_EXIT_ENUM_SUCCESS);

	/* idle exit after BackendShutdownTimeout, restart on the next job */
	_g_test_loop_wait (2500);
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_search_names (backend, job, 0, (gchar **) values);
	pk_test_spawn_module_job_wait (backend, job, 5000);
	g_assert_cmpuint (sm_packages, ==, 3);

	/* cancel escalation: cancel and SIGTERM are ignored, SIGKILL is allowed by the manifest */
	g_test_expect_message ("PackageKit-Spawn", G_LOG_LEVEL_WARNING, "*did not finish job*");
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "*ignored SIGTERM, sending SIGKILL*");
	job = pk_test_spawn_module_job_new (conf, backend);
	pk_backend_remove_packages (backend, job, 0, (gchar **) package_ids, FALSE, FALSE);
	pk_backend_cancel (backend, job);
	pk_test_spawn_module_job_wait (backend, job, 15000);
	g_assert_cmpint (sm_error, ==, PK_ERROR_ENUM_PROCESS_KILL);
	g_test_assert_expected_messages ();

	ret = pk_backend_unload (backend);
	g_assert_true (ret);

	/* a manifest whose helper cannot be started fails the load */
	root_dir = pk_util_get_root_dir (conf);
	broken_dir = g_build_filename (root_dir, PK_BACKENDS_DIR, "test_broken", NULL);
	broken_manifest = g_build_filename (broken_dir, "backend.conf", NULL);
	g_assert_cmpint (g_mkdir_with_parents (broken_dir, 0755), ==, 0);
	ret = g_file_set_contents (broken_manifest, "[Backend]\nExec=does-not-exist\n", -1, NULL);
	g_assert_true (ret);
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "test_broken");
	broken = pk_backend_new (conf);
	ret = pk_backend_load (broken, &error);
	g_assert_false (ret);
	g_assert_nonnull (error);
	g_assert_nonnull (strstr (error->message, "does-not-exist"));
}

static void
pk_test_scheduler_func (void)
{
	gboolean ret;
	gchar *tid;
	guint size;
	gchar **array;
	PkTransaction *transaction;
	GError *error = NULL;
	g_autofree gchar *tid_item1 = NULL;
	g_autofree gchar *tid_item2 = NULL;
	g_autofree gchar *tid_item3 = NULL;
	g_autoptr(GKeyFile) conf = NULL;
	g_autoptr(PkBackend) backend = NULL;
	g_autoptr(PkScheduler) tlist = NULL;

	/* remove the self check file */
	pk_test_remove_transaction_db ();

	conf = pk_test_conf_new ();
	db = pk_transaction_db_new (conf);
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* try to load a valid backend */
	backend = pk_backend_new (conf);
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "dummy");
	g_key_file_set_string (conf, "Daemon", "MaximumPackagesToProcess", "1000");
	ret = pk_backend_load (backend, NULL);
	g_assert_true (ret);

	/* get a transaction list object */
	tlist = pk_scheduler_new (conf);
	g_assert_true (tlist != NULL);

	/* make sure we get a valid tid */
	pk_scheduler_set_backend (tlist, backend);
	tid = pk_transaction_db_generate_id (db);
	g_assert_true (tid != NULL);

	/* create a transaction object */
	ret = pk_scheduler_create (tlist, tid, ":org.freedesktop.PackageKit", &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* make sure we get the right object back */
	transaction = pk_scheduler_get_transaction (tlist, tid);
	g_assert_true (transaction != NULL);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_NEW);

	/* get size one we have in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 1);

	/* get transactions (committed, not finished) in progress */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	/* add again the same tid (should fail) */
	ret = pk_scheduler_create (tlist, tid, ":org.freedesktop.PackageKit", &error);
	g_assert_error (error, 1, 0);
	g_assert_true (!ret);
	g_clear_error (&error);

	/* remove without ever committing */
	ret = pk_scheduler_remove (tlist, tid);
	g_assert_true (ret);

	/* get size none we have in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 0);

	/* get a new tid */
	g_free (tid);
	tid = pk_transaction_db_generate_id (db);

	/* create another transaction */
	ret = pk_scheduler_create (tlist, tid, ":org.freedesktop.PackageKit", &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* get from db */
	transaction = pk_scheduler_get_transaction (tlist, tid);
	g_assert_true (transaction != NULL);
	g_signal_connect (transaction,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);

	/* this tests the run-on-commit action */
	pk_transaction_get_updates (transaction,
				    g_variant_new ("(t)", pk_bitfield_value (PK_FILTER_ENUM_NONE)),
				    NULL);

	/* make sure transaction has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_RUNNING);

	/* get present role */
	ret = pk_scheduler_role_present (tlist, PK_ROLE_ENUM_GET_UPDATES);
	g_assert_true (ret);

	/* get non-present role */
	ret = pk_scheduler_role_present (tlist, PK_ROLE_ENUM_SEARCH_NAME);
	g_assert_true (!ret);

	/* get size we have in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 1);

	/* get transactions (committed, not finished) in progress */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 1);
	g_strfreev (array);

	/* wait for Finished */
	_g_test_loop_run_with_timeout (2000);

	/* get size one we have in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 1);

	/* get transactions (committed, not finished) in progress (none) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	/* remove already removed */
	ret = pk_scheduler_remove (tlist, tid);
	g_assert_true (!ret);

	/* wait for Cleanup */
	_g_test_loop_wait (10000);

	/* make sure queue empty */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 0);

	g_free (tid);

	/* create three instances in list */
	tid_item1 = pk_test_scheduler_create_transaction (tlist);
	tid_item2 = pk_test_scheduler_create_transaction (tlist);
	tid_item3 = pk_test_scheduler_create_transaction (tlist);

	/* get all transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 3);

	/* get transactions (committed, not finished) committed */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	transaction = pk_scheduler_get_transaction (tlist, tid_item1);
	g_signal_connect (transaction,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	g_signal_connect (transaction,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	g_signal_connect (transaction,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);

	/* this starts one action */
	array = g_strsplit ("dave", " ", -1);
	transaction = pk_scheduler_get_transaction (tlist, tid_item1);
	pk_transaction_make_exclusive (transaction);
	pk_transaction_search_details (
	    transaction,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* this should be chained after the first action completes */
	array = g_strsplit ("power", " ", -1);
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	pk_transaction_make_exclusive (transaction);
	pk_transaction_search_names (
	    transaction,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* this starts be chained after the second action completes */
	array = g_strsplit ("paul", " ", -1);
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	pk_transaction_make_exclusive (transaction);
	pk_transaction_search_details (
	    transaction,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* get transactions (committed, not finished) in progress (all) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 3);
	g_strfreev (array);

	/* wait for first action */
	_g_test_loop_run_with_timeout (10000);

	/* get all transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 3);

	/* get transactions (committed, not finished) (two, first one finished) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 2);
	g_strfreev (array);

	/* make sure transaction1 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item1);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction2 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_RUNNING);

	/* make sure transaction3 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_READY);

	/* wait for second action */
	_g_test_loop_run_with_timeout (10000);

	/* get all transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 3);

	/* get transactions (committed, not finished) in progress (one) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 1);
	g_strfreev (array);

	/* make sure transaction1 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item1);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction2 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction3 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_RUNNING);

	/* wait for third action */
	_g_test_loop_run_with_timeout (10000);

	/* get all transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 3);

	/* get transactions (committed, not finished) in progress (none) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	/* make sure transaction1 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item1);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction2 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction3 has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_FINISHED);

	/* wait for Cleanup */
	_g_test_loop_wait (10000);

	/* get transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, <, 3); /* at least one should have timed out */

	/* get transactions (committed, not finished) in progress (neither - again) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	g_object_unref (db);
}

static void
pk_test_scheduler_parallel_func (void)
{
	guint size;
	gboolean ret;
	guint i;
	gchar **array;
	PkTransaction *transaction1;
	PkTransaction *transaction2;
	PkTransaction *transaction3;
	GError *error = NULL;
	g_autofree gchar *tid_item1 = NULL;
	g_autofree gchar *tid_item2 = NULL;
	g_autofree gchar *tid_item3 = NULL;
	g_autofree gchar *tid_item4 = NULL;
	g_autofree gchar *tid_item5 = NULL;
	g_autoptr(GKeyFile) conf = NULL;
	g_autoptr(PkBackend) backend = NULL;
	g_autoptr(PkScheduler) tlist = NULL;

	conf = pk_test_conf_new ();
	db = pk_transaction_db_new (conf);
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* try to load a valid backend */
	g_key_file_set_string (conf, "Daemon", "MaximumPackagesToProcess", "1000");
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "dummy");
	backend = pk_backend_new (conf);
	ret = pk_backend_load (backend, NULL);
	g_assert_true (ret);

	/* get a transaction list object */
	tlist = pk_scheduler_new (conf);
	g_assert_true (tlist != NULL);

	pk_scheduler_set_backend (tlist, backend);

	/* create three instances in list */
	tid_item1 = pk_test_scheduler_create_transaction (tlist);
	tid_item2 = pk_test_scheduler_create_transaction (tlist);
	tid_item3 = pk_test_scheduler_create_transaction (tlist);
	tid_item4 = pk_test_scheduler_create_transaction (tlist);
	tid_item5 = pk_test_scheduler_create_transaction (tlist);

	/* get all transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 5);

	/* get transactions (committed, not finished) committed */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	transaction1 = pk_scheduler_get_transaction (tlist, tid_item1);
	g_signal_connect (transaction1,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item2);
	g_signal_connect (transaction1,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item3);
	g_signal_connect (transaction1,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_signal_connect (transaction1,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item5);
	g_signal_connect (transaction1,
			  "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb),
			  NULL);

	/* this starts one action */
	array = g_strsplit ("dave", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item1);
	pk_transaction_search_details (
	    transaction1,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* run a second (and exclusive!) action in parallel */
	array = g_strsplit ("libawesome;42;i386;debian;", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item2);
	pk_transaction_skip_auth_checks (transaction1, TRUE);
	pk_transaction_install_packages (
	    transaction1,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* run a third action in parallel */
	array = g_strsplit ("power", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item3);
	pk_transaction_search_names (
	    transaction1,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* run a fourth (and exclusive!) action in parallel */
	array = g_strsplit ("foobar;1.1.0;i386;debian;", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	pk_transaction_skip_auth_checks (transaction1, TRUE);
	pk_transaction_install_packages (
	    transaction1,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* get transactions (committed, not finished) in progress (all should be RUNNING now) */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 4);
	g_strfreev (array);

	/* wait for one action to complete */
	_g_test_loop_run_with_timeout (10000);

	/* make sure transaction4 (second exclusive) has correct flags (should be waiting for transaction2 to complete) */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_READY);

	/* make sure transaction3 (non-exlusive) is running (should still be running, because it was run at last) */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item3);
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_RUNNING);

	/* make sure transaction2 (exlusive) is running too */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item2);
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_RUNNING);

	/* run a fifth (non-exclusive) action in parallel to the running exclusive */
	array = g_strsplit ("paul", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item5);
	pk_transaction_search_details (
	    transaction1,
	    g_variant_new ("(t^as)", pk_bitfield_value (PK_FILTER_ENUM_NONE), array),
	    NULL);
	g_strfreev (array);

	/* get all transactions in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 5);

	/* wait for all non-exclusive actions to complete */
	i = 0;
	while (TRUE) {
		_g_test_loop_run_with_timeout (10000 - i * 20);
		i++;

		/* ensure transaction objects are up-to-date */
		transaction1 = pk_scheduler_get_transaction (tlist, tid_item1);
		transaction2 = pk_scheduler_get_transaction (tlist, tid_item3);
		transaction3 = pk_scheduler_get_transaction (tlist, tid_item5);

		if (i >= 100 || transaction1 == NULL || transaction2 == NULL ||
		    transaction3 == NULL) {
			g_print ("Dumping scheduler state:\n%s\n", pk_scheduler_get_state (tlist));
			g_warning ("did not reach state where all non-exclusive transactions are "
				   "finished");
			g_assert_not_reached ();
		}

		if (pk_transaction_get_state (transaction1) == PK_TRANSACTION_STATE_FINISHED &&
		    pk_transaction_get_state (transaction2) == PK_TRANSACTION_STATE_FINISHED &&
		    pk_transaction_get_state (transaction3) == PK_TRANSACTION_STATE_FINISHED)
			break;
	}

	/* we should have two exlusive transactions left */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 2);
	g_strfreev (array);

	/* wait for first exclusive transaction to complete */
	_g_test_loop_run_with_timeout (10000);

	/* make sure transaction2 (first exclusive) is FINISHED */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item2);
	g_assert_cmpint (pk_transaction_get_state (transaction1),
			 ==,
			 PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction4 (second exclusive) is RUNNING now */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_RUNNING);

	/* wait for last exclusive transaction to complete */
	_g_test_loop_run_with_timeout (20000);

	/* make sure transaction4 (second exclusive) is now finished too */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_assert_cmpint (pk_transaction_get_state (transaction1),
			 ==,
			 PK_TRANSACTION_STATE_FINISHED);

	/* we shouldn't have transactions left */
	array = pk_scheduler_get_array (tlist);
	size = g_strv_length (array);
	g_assert_cmpint (size, ==, 0);
	g_strfreev (array);

	g_object_unref (db);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* components */
	g_test_add_func ("/packagekit/transaction", pk_test_transaction_func);
	g_test_add_func ("/packagekit/dbus", pk_test_dbus_func);
	g_test_add_func ("/packagekit/spawn", pk_test_spawn_func);
	g_test_add_func ("/packagekit/backend-process", pk_test_backend_process_func);
	g_test_add_func ("/packagekit/backend-protocol", pk_test_backend_protocol_func);
	g_test_add_func ("/packagekit/spawn-module", pk_test_spawn_module_func);
	g_test_add_func ("/packagekit/scheduler", pk_test_scheduler_func);
	g_test_add_func ("/packagekit/scheduler-parallel", pk_test_scheduler_parallel_func);
	g_test_add_func ("/packagekit/transaction-db", pk_test_transaction_db_func);

	/* backend stuff */
	g_test_add_func ("/packagekit/backend", pk_test_backend_func);
	g_test_add_func ("/packagekit/backend_spawn", pk_test_backend_spawn_func);

	return g_test_run ();
}
