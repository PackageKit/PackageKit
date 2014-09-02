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

#include <config.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>

#include "pk-cleanup.h"
#include "pk-backend.h"
#include "pk-backend-spawn.h"
#include "pk-dbus.h"
#include "pk-engine.h"
#include "pk-notify.h"
#include "pk-spawn.h"
#include "pk-time.h"
#include "pk-transaction-db.h"
#include "pk-transaction.h"
#include "pk-transaction-private.h"
#include "pk-scheduler.h"


#define PK_TRANSACTION_ERROR_INPUT_INVALID	14

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

static guint number_packages = 0;

/**
 * pk_test_backend_finished_cb:
 **/
static void
pk_test_backend_finished_cb (PkBackend *backend, PkExitEnum exit, gpointer user_data)
{
	_g_test_loop_quit ();
}

/**
 * pk_test_backend_watch_file_cb:
 **/
static void
pk_test_backend_watch_file_cb (PkBackend *backend, gpointer user_data)
{
	_g_test_loop_quit ();
}

static void
pk_test_backend_func_true (PkBackendJob *job,
			   GVariant *params,
			   gpointer user_data)
{
	g_usleep (1000*1000);
	g_assert_cmpint (GPOINTER_TO_INT (user_data), ==, 999);
	/* trigger duplicate test */

	pk_backend_job_package (job, PK_INFO_ENUM_AVAILABLE,
				"vips-doc;7.12.4-2.fc8;noarch;linva",
				"The vips documentation package.");
	pk_backend_job_package (job, PK_INFO_ENUM_AVAILABLE,
				"vips-doc;7.12.4-2.fc8;noarch;linva",
				"The vips documentation package.");
	pk_backend_job_finished (job);
}

static void
pk_test_backend_func_immediate_false (PkBackendJob *job,
				      GVariant *params,
				      gpointer user_data)
{
	pk_backend_job_finished (job);
}

/**
 * pk_test_backend_package_cb:
 **/
static void
pk_test_backend_package_cb (PkBackend *backend, PkPackage *package, gpointer user_data)
{
	g_debug ("package:%s", pk_package_get_id (package));
	number_packages++;
}

static void
pk_test_backend_func (void)
{
	const gchar *text;
	gboolean ret;
	const gchar *filename;
	GError *error = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	_cleanup_object_unref_ PkBackend *backend = NULL;
	_cleanup_object_unref_ PkBackendJob *job = NULL;

	/* get an backend */
	conf = g_key_file_new ();
	backend = pk_backend_new (conf);
	g_assert (backend != NULL);

	/* create a config file */
	filename = "/tmp/dave";
	ret = g_file_set_contents (filename, "foo", -1, NULL);
	g_assert (ret);

	/* set up a watch file on a config file */
	ret = pk_backend_watch_file (backend, filename, (PkBackendFileChanged) pk_test_backend_watch_file_cb, NULL);
	g_assert (ret);

	/* change the config file */
	ret = g_file_set_contents (filename, "bar", -1, NULL);
	g_assert (ret);

	/* wait for config file change */
	_g_test_loop_run_with_timeout (5000);

	/* delete the config file */
	ret = g_unlink (filename);
	g_assert (!ret);

	/* connect */
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGE,
				  (PkBackendJobVFunc) pk_test_backend_package_cb,
				  NULL);
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  (PkBackendJobVFunc) pk_test_backend_finished_cb,
				  NULL);

	/* get eula that does not exist */
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	g_assert (!ret);

	/* accept eula */
	pk_backend_accept_eula (backend, "license_foo");

	/* get eula that does exist */
	ret = pk_backend_is_eula_valid (backend, "license_foo");
	g_assert (ret);

	/* accept eula (again) */
	pk_backend_accept_eula (backend, "license_foo");

	/* load an invalid backend */
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "invalid");
	ret = pk_backend_load (backend, &error);
	g_assert_error (error, 1, 0);
	g_assert (!ret);
	g_clear_error (&error);

	/* try to load a valid backend */
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "dummy");
	ret = pk_backend_load (backend, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* load an valid backend again */
	ret = pk_backend_load (backend, &error);
	g_assert_error (error, 1, 0);
	g_assert (!ret);
	g_clear_error (&error);

	/* get backend name */
	text = pk_backend_get_name (backend);
	g_assert_cmpstr (text, ==, "dummy");

	/* unlock an valid backend */
	ret = pk_backend_unload (backend);
	g_assert (ret);

	/* unlock an valid backend again */
	ret = pk_backend_unload (backend);
	g_assert (ret);

	/* check we are not finished */
	ret = pk_backend_job_get_is_finished (job);
	g_assert (!ret);

	/* check we have no error */
	ret = pk_backend_job_has_set_error_code (job);
	g_assert (!ret);

	/* wait for a thread to return true */
	ret = pk_backend_load (backend, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = pk_backend_job_thread_create (job,
					    pk_test_backend_func_true,
					    GINT_TO_POINTER (999),
					    NULL);
	g_assert (ret);

	/* wait for Finished */
	_g_test_loop_wait (2000);

	/* check duplicate filter */
	g_assert_cmpint (number_packages, ==, 1);

	/* reset */
	pk_backend_start_job (backend, job);
	pk_backend_reset_job (backend, job);
	pk_backend_stop_job (backend, job);
	g_object_unref (job);
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);

	/* wait for a thread to return false (straight away) */
	ret = pk_backend_job_thread_create (job,
					    pk_test_backend_func_immediate_false,
					    NULL,
					    NULL);
	g_assert (ret);

	/* wait for Finished */
	_g_test_loop_wait (10);

	pk_backend_start_job (backend, job);
	pk_backend_reset_job (backend, job);
	pk_backend_job_error_code (job, PK_ERROR_ENUM_GPG_FAILURE, "test error");

	/* wait for finished */
//	_g_test_loop_run_with_timeout (PK_BACKEND_FINISHED_ERROR_TIMEOUT + 400);

	/* get allow cancel after reset */
	pk_backend_reset_job (backend, job);
	ret = pk_backend_job_get_allow_cancel (job);
	g_assert (!ret);

	/* set allow cancel TRUE */
	pk_backend_job_set_allow_cancel (job, TRUE);

	/* set allow cancel TRUE (repeat) */
	pk_backend_job_set_allow_cancel (job, TRUE);

	/* set allow cancel FALSE */
	pk_backend_job_set_allow_cancel (job, FALSE);

	/* set allow cancel FALSE (after reset) */
	pk_backend_reset_job (backend, job);
	pk_backend_job_set_allow_cancel (job, FALSE);

	/* stop the job again */
	pk_backend_stop_job (backend, job);
}

static guint _backend_spawn_number_packages = 0;

/**
 * pk_test_backend_spawn_finished_cb:
 **/
static void
pk_test_backend_spawn_finished_cb (PkBackendJob *job,
				   PkExitEnum exit,
				   PkBackendSpawn *backend_spawn)
{
	_g_test_loop_quit ();
}

/**
 * pk_test_backend_spawn_package_cb:
 **/
static void
pk_test_backend_spawn_package_cb (PkBackend *backend, PkInfoEnum info,
				  const gchar *package_id, const gchar *summary,
				  PkBackendSpawn *backend_spawn)
{
	_backend_spawn_number_packages++;
}

static void
pk_test_backend_spawn_func (void)
{
	PkBackendSpawn *backend_spawn;
	const gchar *text;
	gboolean ret;
	gchar *uri;
	GError *error = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	_cleanup_object_unref_ PkBackend *backend = NULL;
	_cleanup_object_unref_ PkBackendJob *job = NULL;

	/* get an backend_spawn */
	conf = g_key_file_new ();
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "test_spawn");
	backend_spawn = pk_backend_spawn_new (conf);
	g_assert (backend_spawn != NULL);

	/* private copy for unref testing */
	backend = pk_backend_new (conf);
	job = pk_backend_job_new (conf);
	pk_backend_job_set_backend (job, backend);

	/* get backend name */
	text = pk_backend_spawn_get_name (backend_spawn);
	g_assert_cmpstr (text, ==, NULL);

	/* set backend name */
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	g_assert (ret);

	/* get backend name */
	text = pk_backend_spawn_get_name (backend_spawn);
	g_assert_cmpstr (text, ==, "test_spawn");

	/* needed to avoid an error */
	ret = pk_backend_load (backend, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* test pk_backend_spawn_inject_data Percentage1 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\t0", NULL);
	g_assert (ret);

	/* test pk_backend_spawn_inject_data Percentage2 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\tbrian", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data Percentage3 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\t12345", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data Percentage4 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage\t", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data Percentage5 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "percentage", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data NoPercentageUpdates */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "no-percentage-updates", NULL);
	g_assert (ret);

	/* test pk_backend_spawn_inject_data failure */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "error\tnot-present-woohoo\tdescription text", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data Status */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "status\tquery", NULL);
	g_assert (ret);

	/* test pk_backend_spawn_inject_data RequireRestart */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "requirerestart\tsystem\tgnome-power-manager;0.0.1;i386;data", NULL);
	g_assert (ret);

	/* test pk_backend_spawn_inject_data RequireRestart invalid enum */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "requirerestart\tmooville\tgnome-power-manager;0.0.1;i386;data", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data RequireRestart invalid PackageId */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "requirerestart\tsystem\tdetails about the restart", NULL);
	g_assert (!ret);

	/* test pk_backend_spawn_inject_data AllowUpdate1 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "allow-cancel\ttrue", NULL);
	g_assert (ret);

	/* test pk_backend_spawn_inject_data AllowUpdate2 */
	ret = pk_backend_spawn_inject_data (backend_spawn, job, "allow-cancel\tbrian", NULL);
	g_assert (!ret);

	/* convert proxy uri (bare) */
	uri = pk_backend_spawn_convert_uri ("username:password@server:port");
	g_assert_cmpstr (uri, ==, "http://username:password@server:port/");
	g_free (uri);

	/* convert proxy uri (full) */
	uri = pk_backend_spawn_convert_uri ("http://username:password@server:port/");
	g_assert_cmpstr (uri, ==, "http://username:password@server:port/");
	g_free (uri);

	/* convert proxy uri (partial) */
	uri = pk_backend_spawn_convert_uri ("ftp://username:password@server:port");
	g_assert_cmpstr (uri, ==, "ftp://username:password@server:port/");
	g_free (uri);

	/* test pk_backend_spawn_parse_common_out Package */
	ret = pk_backend_spawn_inject_data (backend_spawn, job,
		"package\tinstalled\tgnome-power-manager;0.0.1;i386;data\tMore useless software", NULL);
	g_assert (ret);

	/* manually unlock as we have no engine */
	ret = pk_backend_unload (backend);
	g_assert (ret);

	/* reset */
	g_object_unref (backend_spawn);

	/* new */
	backend_spawn = pk_backend_spawn_new (conf);

	/* set backend name */
	ret = pk_backend_spawn_set_name (backend_spawn, "test_spawn");
	g_assert (ret);

	/* so we can spin until we finish */
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_FINISHED,
				  (PkBackendJobVFunc) pk_test_backend_spawn_finished_cb,
				  backend_spawn);

	/* so we can count the returned packages */
	pk_backend_job_set_vfunc (job,
				  PK_BACKEND_SIGNAL_PACKAGE,
				  (PkBackendJobVFunc) pk_test_backend_spawn_package_cb,
				  backend_spawn);

	/* needed to avoid an error */
	ret = pk_backend_load (backend, NULL);

	/* test search-name.sh running */
	ret = pk_backend_spawn_helper (backend_spawn, job, "search-name.sh", "none", "bar", NULL);
	g_assert (ret);

	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* test number of packages */
	g_assert_cmpint (_backend_spawn_number_packages, ==, 2);

	/* manually unlock as we have no engine */
	ret = pk_backend_unload (backend);
	g_assert (ret);

	/* done */
	g_object_unref (backend_spawn);
}

static void
pk_test_dbus_func (void)
{
	_cleanup_object_unref_ PkDbus *dbus;

	dbus = pk_dbus_new ();
	g_assert (dbus != NULL);
}

PkSpawnExitType mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
guint stdout_count = 0;
guint finished_count = 0;

/**
 * pk_test_exit_cb:
 **/
static void
pk_test_exit_cb (PkSpawn *spawn, PkSpawnExitType exit, gpointer user_data)
{
	g_debug ("spawn exit=%i", exit);
	mexit = exit;
	finished_count++;
	_g_test_loop_quit ();
}

/**
 * pk_test_stdout_cb:
 **/
static void
pk_test_stdout_cb (PkSpawn *spawn, const gchar *line, gpointer user_data)
{
	g_debug ("stdout '%s'", line);
	stdout_count++;
}

static gboolean
cancel_cb (gpointer data)
{
	PkSpawn *spawn = PK_SPAWN(data);
	pk_spawn_kill (spawn);
	return FALSE;
}

static void
new_spawn_object (PkSpawn **pspawn)
{
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	if (*pspawn != NULL)
		g_object_unref (*pspawn);
	conf = g_key_file_new ();
	*pspawn = pk_spawn_new (conf);
	g_signal_connect (*pspawn, "exit",
			  G_CALLBACK (pk_test_exit_cb), NULL);
	g_signal_connect (*pspawn, "stdout",
			  G_CALLBACK (pk_test_stdout_cb), NULL);
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
	_cleanup_object_unref_ PkSpawn *spawn = NULL;
	_cleanup_strv_free_ gchar **argv = NULL;
	_cleanup_strv_free_ gchar **envp = NULL;

	new_spawn_object (&spawn);

	/* make sure return error for missing file */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit ("pk-spawn-test-xxx.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_error (error, 1, 0);
	g_strfreev (argv);
	g_assert (!ret);
	g_clear_error (&error);

	/* make sure finished wasn't called */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_UNKNOWN);

	/* make sure run correct helper */
	mexit = -1;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_strfreev (argv);

	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* make sure finished okay */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SUCCESS);

	/* make sure finished was called only once */
	g_assert_cmpint (finished_count, ==, 1);

	/* make sure we got the right stdout data */
	g_assert_cmpint (stdout_count, ==, 4+11);

	/* get new object */
	new_spawn_object (&spawn);

	/* make sure we set the proxy */
	mexit = -1;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-proxy.sh", " ", 0);
	envp = g_strsplit ("http_proxy=username:password@server:port "
			   "ftp_proxy=username:password@server:port", " ", 0);
	ret = pk_spawn_argv (spawn, argv, envp, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_strfreev (argv);
	g_strfreev (envp);

	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* get new object */
	new_spawn_object (&spawn);

	/* make sure run correct helper, and cancel it using SIGKILL */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_strfreev (argv);

	g_timeout_add_seconds (1, cancel_cb, spawn);
	/* wait for finished */
	_g_test_loop_run_with_timeout (5000);

	/* make sure finished in SIGKILL */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SIGKILL);

	/* get new object */
	new_spawn_object (&spawn);

	/* make sure dumb helper ignores SIGQUIT */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test.sh", " ", 0);
	g_object_set (spawn,
		      "allow-sigkill", FALSE,
		      NULL);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_strfreev (argv);

	g_timeout_add_seconds (1, cancel_cb, spawn);
	/* wait for finished */
	_g_test_loop_run_with_timeout (10000);

	/* make sure finished in SIGQUIT */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SIGQUIT);

	/* get new object */
	new_spawn_object (&spawn);

	/* make sure run correct helper, and SIGQUIT it */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test-sigquit.py", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_strfreev (argv);

	g_timeout_add (1000, cancel_cb, spawn);
	/* wait for finished */
	_g_test_loop_run_with_timeout (2000);

	/* make sure finished in SIGQUIT */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_SIGQUIT);

	/* run lots of data for profiling */
	argv = g_strsplit (TESTDATADIR "/pk-spawn-test-profiling.sh", " ", 0);
	ret = pk_spawn_argv (spawn, argv, NULL, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_strfreev (argv);

	/* get new object */
	new_spawn_object (&spawn);

	/* run the dispatcher */
	mexit = PK_SPAWN_EXIT_TYPE_UNKNOWN;
	argv = g_strsplit (TESTDATADIR "/pk-spawn-dispatcher.py\tsearch-name\tnone\tpower manager", "\t", 0);
	envp = g_strsplit ("NETWORK=TRUE LANG=C BACKGROUND=TRUE INTERACTIVE=TRUE", " ", 0);
	ret = pk_spawn_argv (spawn, argv, envp, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* wait 2+2 seconds for the dispatcher */
	_g_test_loop_wait (4000);

	/* we got a package (+finished)? */
	g_assert_cmpint (stdout_count, ==, 2);

	/* dispatcher still alive? */
	g_assert (pk_spawn_is_running (spawn));

	/* run the dispatcher with new input */
	ret = pk_spawn_argv (spawn, argv, envp, PK_SPAWN_ARGV_FLAGS_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* this may take a while */
	_g_test_loop_wait (100);

	/* we got another package (and finished) */
	g_assert_cmpint (stdout_count, ==, 4);

	/* see if pk_spawn_exit blocks (required) */
	g_idle_add (idle_cb, NULL);

	/* ask dispatcher to close */
	ret = pk_spawn_exit (spawn);
	g_assert (ret);

	/* ask dispatcher to close (again, should be closing) */
	ret = pk_spawn_exit (spawn);
	g_assert (!ret);

	/* this may take a while */
	_g_test_loop_wait (100);

	/* did dispatcher close? */
	g_assert (!pk_spawn_is_running (spawn));

	/* did we get the right exit code */
	g_assert_cmpint (mexit, ==, PK_SPAWN_EXIT_TYPE_DISPATCHER_EXIT);

	/* ask dispatcher to close (again) */
	ret = pk_spawn_exit (spawn);
	g_assert (!ret);
}

static void
pk_test_time_func (void)
{
	gboolean ret;
	guint value;
	_cleanup_object_unref_ PkTime *pktime = NULL;

	pktime = pk_time_new ();
	g_assert (pktime != NULL);

	/* get elapsed correctly at startup */
	value = pk_time_get_elapsed (pktime);
	g_assert_cmpint (value, <, 10);

	/* ignore remaining correctly */
	value = pk_time_get_remaining (pktime);
	g_assert_cmpint (value, ==, 0);

	g_usleep (1000*1000);

	/* get elapsed correctly */
	value = pk_time_get_elapsed (pktime);
	g_assert_cmpint (value, >, 900);
	g_assert_cmpint (value, <, 1100);

	/* ignore remaining correctly when not enough entries */
	value = pk_time_get_remaining (pktime);
	g_assert_cmpint (value, ==, 0);

	/* make sure we can add data */
	ret = pk_time_add_data (pktime, 10);
	g_assert (ret);

	/* make sure we can get remaining correctly */
	value = 20;
	while (value < 60) {
		pk_time_advance_clock (pktime, 2000);
		pk_time_add_data (pktime, value);
		value += 10;
	}
	value = pk_time_get_remaining (pktime);
	g_assert_cmpint (value, >, 9);
	g_assert_cmpint (value, <, 11);

	/* reset */
	g_object_unref (pktime);
	pktime = pk_time_new ();

	/* make sure we can do long times */
	value = 10;
	pk_time_add_data (pktime, 0);
	while (value < 60) {
		pk_time_advance_clock (pktime, 4*60*1000);
		pk_time_add_data (pktime, value);
		value += 10;
	}
	value = pk_time_get_remaining (pktime);
	g_assert_cmpint (value, >=, 1199);
	g_assert_cmpint (value, <=, 1201);
}

static void
pk_test_transaction_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GDBusNodeInfo *introspection;
	_cleanup_object_unref_ PkTransaction *transaction = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;

	introspection = pk_load_introspection (PK_DBUS_INTERFACE_TRANSACTION ".xml", NULL);
	g_assert (introspection != NULL);

	/* get PkTransaction object */
	conf = g_key_file_new ();
	transaction = pk_transaction_new (conf, introspection);
	g_assert (transaction != NULL);

	/* validate incorrect text */
	ret = pk_transaction_strvalidate ("richard$hughes", &error);
	g_assert_error (error, PK_TRANSACTION_ERROR, PK_TRANSACTION_ERROR_INPUT_INVALID);
	g_assert (!ret);
	g_clear_error (&error);

	/* validate correct text */
	ret = pk_transaction_strvalidate ("richardhughes", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_clear_error (&error);

	g_dbus_node_info_unref (introspection);
}

static void
pk_test_transaction_db_func (void)
{
	guint value;
	gchar *tid;
	gboolean ret;
	gdouble ms;
	GError *error = NULL;
	_cleanup_object_unref_ PkTransactionDb *db;
	_cleanup_free_ gchar *proxy_http = NULL;
	_cleanup_free_ gchar *proxy_ftp = NULL;

	/* remove the self check file */
#if PK_BUILD_LOCAL
	ret = g_file_test ("./transactions.db", G_FILE_TEST_EXISTS);
	if (ret) {
		/* remove old local database */
		value = g_unlink ("./transactions.db");
		g_assert (value == 0);
	}
#endif
	/* check we created quickly */
	g_test_timer_start ();
	db = pk_transaction_db_new ();
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ms = g_test_timer_elapsed ();
	g_assert_cmpfloat (ms, <, 1.5);
	g_object_unref (db);

	/* check we opened quickly */
	g_test_timer_start ();
	db = pk_transaction_db_new ();
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert (ret);
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
	g_assert (ret);

	/* do the deferred write */
	g_test_timer_start ();
	_g_test_loop_wait (2000);

	/* do we get the correct time */
	value = pk_transaction_db_action_time_since (db, PK_ROLE_ENUM_REFRESH_CACHE);
	g_assert_cmpint (value, >, 1);
	g_assert_cmpint (value, <=, 4);

	/* can we set the proxies */
	ret = pk_transaction_db_set_proxy (db, 500, "session1",
					   "127.0.0.1:80",
					   NULL,
					   "127.0.0.1:21",
					   NULL,
					   NULL,
					   NULL);
	g_assert (ret);

	/* can we set the proxies (overwrite) */
	ret = pk_transaction_db_set_proxy (db, 500, "session1",
					   "127.0.0.1:80",
					   NULL,
					   "127.0.0.1:21",
					   NULL,
					   NULL,
					   NULL);
	g_assert (ret);

	/* can we get the proxies (non-existant user) */
	ret = pk_transaction_db_get_proxy (db, 501, "session1",
					   &proxy_http,
					   NULL,
					   &proxy_ftp,
					   NULL,
					   NULL,
					   NULL);
	g_assert (!ret);
	g_assert_cmpstr (proxy_http, ==, NULL);
	g_assert_cmpstr (proxy_ftp, ==, NULL);

	/* can we get the proxies (non-existant session) */
	ret = pk_transaction_db_get_proxy (db, 500, "session2",
					   &proxy_http,
					   NULL,
					   &proxy_ftp,
					   NULL,
					   NULL,
					   NULL);
	g_assert (!ret);
	g_assert_cmpstr (proxy_http, ==, NULL);
	g_assert_cmpstr (proxy_ftp, ==, NULL);

	/* can we get the proxies (match) */
	ret = pk_transaction_db_get_proxy (db, 500, "session1",
					   &proxy_http,
					   NULL,
					   &proxy_ftp,
					   NULL,
					   NULL,
					   NULL);
	g_assert (ret);
	g_assert_cmpstr (proxy_http, ==, "127.0.0.1:80");
	g_assert_cmpstr (proxy_ftp, ==, "127.0.0.1:21");
}

static PkTransactionDb *db = NULL;

/**
 * pk_test_scheduler_finished_cb:
 **/
static void
pk_test_scheduler_finished_cb (PkTransaction *transaction, const gchar *exit_text, guint time, gpointer user_data)
{
	_g_test_loop_quit ();
}

/**
 * pk_test_scheduler_create_transaction:
 **/
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
	g_assert (ret);

	return tid;
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
	_cleanup_free_ gchar *tid_item1 = NULL;
	_cleanup_free_ gchar *tid_item2 = NULL;
	_cleanup_free_ gchar *tid_item3 = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	_cleanup_object_unref_ PkBackend *backend;
	_cleanup_object_unref_ PkScheduler *tlist;

	/* remove the self check file */
#if PK_BUILD_LOCAL
	ret = g_file_test ("./transactions.db", G_FILE_TEST_EXISTS);
	if (ret) {
		/* remove old local database */
		g_debug ("Removing %s", "./transactions.db");
		size = g_unlink ("./transactions.db");
		g_assert (size == 0);
	}
#endif

	db = pk_transaction_db_new ();
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* try to load a valid backend */
	conf = g_key_file_new ();
	backend = pk_backend_new (conf);
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "dummy");
	g_key_file_set_string (conf, "Daemon", "MaximumItemsToResolve", "1000");
	g_key_file_set_string (conf, "Daemon", "MaximumPackagesToProcess", "1000");
	g_key_file_set_string (conf, "Daemon", "SimultaneousTransactionsForUid", "1000");
	ret = pk_backend_load (backend, NULL);
	g_assert (ret);

	/* get a transaction list object */
	tlist = pk_scheduler_new (conf);
	g_assert (tlist != NULL);

	/* make sure we get a valid tid */
	pk_scheduler_set_backend (tlist, backend);
	tid = pk_transaction_db_generate_id (db);
	g_assert (tid != NULL);

	/* create a transaction object */
	ret = pk_scheduler_create (tlist, tid, ":org.freedesktop.PackageKit", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure we get the right object back */
	transaction = pk_scheduler_get_transaction (tlist, tid);
	g_assert (transaction != NULL);
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
	g_assert (!ret);
	g_clear_error (&error);

	/* remove without ever committing */
	ret = pk_scheduler_remove (tlist, tid);
	g_assert (ret);

	/* get size none we have in queue */
	size = pk_scheduler_get_size (tlist);
	g_assert_cmpint (size, ==, 0);

	/* get a new tid */
	g_free (tid);
	tid = pk_transaction_db_generate_id (db);

	/* create another transaction */
	ret = pk_scheduler_create (tlist, tid, ":org.freedesktop.PackageKit", &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get from db */
	transaction = pk_scheduler_get_transaction (tlist, tid);
	g_assert (transaction != NULL);
	g_signal_connect (transaction, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);

	/* this tests the run-on-commit action */
	pk_transaction_get_updates (transaction,
				    g_variant_new ("(t)",
						   pk_bitfield_value (PK_FILTER_ENUM_NONE)),
				    NULL);

	/* make sure transaction has correct flags */
	transaction = pk_scheduler_get_transaction (tlist, tid);
	g_assert_cmpint (pk_transaction_get_state (transaction), ==, PK_TRANSACTION_STATE_RUNNING);

	/* get present role */
	ret = pk_scheduler_role_present (tlist, PK_ROLE_ENUM_GET_UPDATES);
	g_assert (ret);

	/* get non-present role */
	ret = pk_scheduler_role_present (tlist, PK_ROLE_ENUM_SEARCH_NAME);
	g_assert (!ret);

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
	g_assert (!ret);

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
	g_signal_connect (transaction, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	g_signal_connect (transaction, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	g_signal_connect (transaction, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);

	/* this starts one action */
	array = g_strsplit ("dave", " ", -1);
	transaction = pk_scheduler_get_transaction (tlist, tid_item1);
	pk_transaction_make_exclusive (transaction);
	pk_transaction_search_details (transaction,
				       g_variant_new ("(t^as)",
						      pk_bitfield_value (PK_FILTER_ENUM_NONE),
						      array),
				       NULL);
	g_strfreev (array);

	/* this should be chained after the first action completes */
	array = g_strsplit ("power", " ", -1);
	transaction = pk_scheduler_get_transaction (tlist, tid_item2);
	pk_transaction_make_exclusive (transaction);
	pk_transaction_search_names (transaction,
				     g_variant_new ("(t^as)",
						    pk_bitfield_value (PK_FILTER_ENUM_NONE),
						    array),
				     NULL);
	g_strfreev (array);

	/* this starts be chained after the second action completes */
	array = g_strsplit ("paul", " ", -1);
	transaction = pk_scheduler_get_transaction (tlist, tid_item3);
	pk_transaction_make_exclusive (transaction);
	pk_transaction_search_details (transaction,
				       g_variant_new ("(t^as)",
						      pk_bitfield_value (PK_FILTER_ENUM_NONE),
						      array),
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
	_cleanup_free_ gchar *tid_item1 = NULL;
	_cleanup_free_ gchar *tid_item2 = NULL;
	_cleanup_free_ gchar *tid_item3 = NULL;
	_cleanup_free_ gchar *tid_item4 = NULL;
	_cleanup_free_ gchar *tid_item5 = NULL;
	_cleanup_keyfile_unref_ GKeyFile *conf = NULL;
	_cleanup_object_unref_ PkBackend *backend = NULL;
	_cleanup_object_unref_ PkScheduler *tlist = NULL;

	db = pk_transaction_db_new ();
	ret = pk_transaction_db_load (db, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* try to load a valid backend */
	conf = g_key_file_new ();
	g_key_file_set_string (conf, "Daemon", "MaximumItemsToResolve", "1000");
	g_key_file_set_string (conf, "Daemon", "MaximumPackagesToProcess", "1000");
	g_key_file_set_string (conf, "Daemon", "SimultaneousTransactionsForUid", "1000");
	g_key_file_set_string (conf, "Daemon", "DefaultBackend", "dummy");
	backend = pk_backend_new (conf);
	ret = pk_backend_load (backend, NULL);
	g_assert (ret);

	/* get a transaction list object */
	tlist = pk_scheduler_new (conf);
	g_assert (tlist != NULL);

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
	g_signal_connect (transaction1, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item2);
	g_signal_connect (transaction1, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item3);
	g_signal_connect (transaction1, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_signal_connect (transaction1, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item5);
	g_signal_connect (transaction1, "finished",
			  G_CALLBACK (pk_test_scheduler_finished_cb), NULL);

	/* this starts one action */
	array = g_strsplit ("dave", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item1);
	pk_transaction_search_details (transaction1,
				       g_variant_new ("(t^as)",
						      pk_bitfield_value (PK_FILTER_ENUM_NONE),
						      array),
				       NULL);
	g_strfreev (array);

	/* run a second (and exclusive!) action in parallel */
	array = g_strsplit ("libawesome;42;i386;debian", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item2);
	pk_transaction_skip_auth_checks (transaction1, TRUE);
	pk_transaction_install_packages (transaction1,
				       g_variant_new ("(t^as)",
						      pk_bitfield_value (PK_FILTER_ENUM_NONE),
						      array),
				       NULL);
	g_strfreev (array);

	/* run a third action in parallel */
	array = g_strsplit ("power", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item3);
	pk_transaction_search_names (transaction1,
				     g_variant_new ("(t^as)",
						    pk_bitfield_value (PK_FILTER_ENUM_NONE),
						    array),
				     NULL);
	g_strfreev (array);

	/* run a fourth (and exclusive!) action in parallel */
	array = g_strsplit ("foobar;1.1.0;i386;debian", " ", -1);
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	pk_transaction_skip_auth_checks (transaction1, TRUE);
	pk_transaction_install_packages (transaction1,
				       g_variant_new ("(t^as)",
						      pk_bitfield_value (PK_FILTER_ENUM_NONE),
						      array),
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
	pk_transaction_search_details (transaction1,
				       g_variant_new ("(t^as)",
						      pk_bitfield_value (PK_FILTER_ENUM_NONE),
						      array),
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

		if (i >= 100 ||
		    transaction1 == NULL ||
		    transaction2 == NULL ||
		    transaction3 == NULL) {
			g_print ("Dumping scheduler state:\n%s\n", pk_scheduler_get_state (tlist));
			g_warning ("did not reach state where all non-exclusive transactions are finished");
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
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_FINISHED);

	/* make sure transaction4 (second exclusive) is RUNNING now */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_RUNNING);

	/* wait for last exclusive transaction to complete */
	_g_test_loop_run_with_timeout (20000);

	/* make sure transaction4 (second exclusive) is now finished too */
	transaction1 = pk_scheduler_get_transaction (tlist, tid_item4);
	g_assert_cmpint (pk_transaction_get_state (transaction1), ==, PK_TRANSACTION_STATE_FINISHED);

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
#if (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 35)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	/* don't run when using make distcheck */
	if (g_strcmp0 (DEFAULT_BACKEND, "dummy") == 0)
		return 0;

#ifndef PK_BUILD_LOCAL
	g_warning ("you need to compile with --enable-local for make check support");
#endif

	/* components */
	g_test_add_func ("/packagekit/time", pk_test_time_func);
	g_test_add_func ("/packagekit/dbus", pk_test_dbus_func);
	g_test_add_func ("/packagekit/spawn", pk_test_spawn_func);
	g_test_add_func ("/packagekit/transaction", pk_test_transaction_func);
	g_test_add_func ("/packagekit/scheduler", pk_test_scheduler_func);
	g_test_add_func ("/packagekit/scheduler-parallel", pk_test_scheduler_parallel_func);
	g_test_add_func ("/packagekit/transaction-db", pk_test_transaction_db_func);

	/* backend stuff */
	g_test_add_func ("/packagekit/backend", pk_test_backend_func);
	g_test_add_func ("/packagekit/backend_spawn", pk_test_backend_spawn_func);

	return g_test_run ();
}

