/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
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

/*
 * pk-backend-poldek: the PackageKit backend for poldek, the PLD Linux
 * package manager, as a helper process speaking the spawned backend
 * protocol (docs/extra/backend-spawn-protocol.md).
 *
 * The main thread runs one job at a time and is the only writer of the
 * protocol socket. A reader thread parses the daemon's requests: it acts on
 * cancel itself, so a cancel reaches a job that is busy inside libpoldek,
 * and queues everything else for the main thread. SIGTERM is turned into a
 * cancel plus a request to exit through a self-pipe the reader thread polls.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <jansson.h>
#include <pk-enum.h>

#include "ctx.h"
#include "filters.h"
#include "groups.h"
#include "job.h"
#include "local.h"
#include "protocol.h"
#include "query.h"
#include "repos.h"
#include "transaction.h"
#include "updates.h"

typedef void (*PkPoldekRoleFunc) (PkPoldekCtx *pc, PkPoldekJob *job, json_t *req);

typedef struct
{
	PkRoleEnum role;
	PkPoldekRoleFunc func;
	PkPoldekNeed need;
} PkPoldekRole;

/* terminated by an entry without func */
static const PkPoldekRole pk_poldek_roles[] = {
	/* clang-format off */
	{ PK_ROLE_ENUM_DOWNLOAD_PACKAGES, pk_poldek_download_packages, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_DEPENDENCIES, pk_poldek_get_dependencies, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_DEPENDENTS, pk_poldek_get_dependents, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_DETAILS, pk_poldek_get_details, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_DETAILS_LOCAL, pk_poldek_get_details_local, PK_POLDEK_NEED_NOTHING },
	{ PK_ROLE_ENUM_GET_FILES, pk_poldek_get_files, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_FILES_LOCAL, pk_poldek_get_files_local, PK_POLDEK_NEED_NOTHING },
	{ PK_ROLE_ENUM_GET_PACKAGES, pk_poldek_get_packages, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_REPO_LIST, pk_poldek_get_repo_list, PK_POLDEK_NEED_SOURCES },
	{ PK_ROLE_ENUM_GET_UPDATE_DETAIL, pk_poldek_get_update_detail, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_GET_UPDATES, pk_poldek_get_updates, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_INSTALL_FILES, pk_poldek_install_files, PK_POLDEK_NEED_NOTHING },
	{ PK_ROLE_ENUM_INSTALL_PACKAGES, pk_poldek_install_packages, PK_POLDEK_NEED_PACKAGES },
	/* also when the indexes in the cache cannot be loaded: it gets new ones */
	{ PK_ROLE_ENUM_REFRESH_CACHE, pk_poldek_refresh_cache, PK_POLDEK_NEED_SOURCES },
	{ PK_ROLE_ENUM_REMOVE_PACKAGES, pk_poldek_remove_packages, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_RESOLVE, pk_poldek_resolve, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_SEARCH_DETAILS, pk_poldek_search_details, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_SEARCH_FILE, pk_poldek_search_file, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_SEARCH_GROUP, pk_poldek_search_group, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_SEARCH_NAME, pk_poldek_search_name, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_UPDATE_PACKAGES, pk_poldek_update_packages, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_UPGRADE_SYSTEM, pk_poldek_upgrade_system, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_WHAT_PROVIDES, pk_poldek_what_provides, PK_POLDEK_NEED_PACKAGES },
	{ PK_ROLE_ENUM_UNKNOWN, NULL, PK_POLDEK_NEED_NOTHING },
	/* clang-format on */
};

typedef enum {
	PK_POLDEK_INPUT_REQUEST,
	PK_POLDEK_INPUT_HANGUP,
	PK_POLDEK_INPUT_INVALID,
	PK_POLDEK_INPUT_TERMINATE,
} PkPoldekInputKind;

typedef struct
{
	PkPoldekInputKind kind;
	json_t *msg;
	PkPoldekJob *job;
} PkPoldekInput;

static GAsyncQueue *pk_poldek_queue = NULL;
static PkPoldekCtx *pk_poldek_ctx = NULL;

/* test only: the reader thread confirms each cancel here */
static int pk_poldek_test_sync_fd = -1;
static int pk_poldek_term_pipe[2] = { -1, -1 };

/*
 * The job of the last run request the reader thread queued. The main thread
 * clears it when the job ends, so the reader thread can cancel a job that
 * is still waiting in the queue, and a late cancel finds nothing.
 */
static GMutex pk_poldek_current_lock;
static PkPoldekJob *pk_poldek_current = NULL;

static void
pk_poldek_push (PkPoldekInputKind kind, json_t *msg, PkPoldekJob *job)
{
	PkPoldekInput *input = g_new0 (PkPoldekInput, 1);

	input->kind = kind;
	input->msg = msg;
	input->job = job;
	g_async_queue_push (pk_poldek_queue, input);
}

static void
pk_poldek_cancel_current (const gchar *job_id)
{
	g_mutex_lock (&pk_poldek_current_lock);
	if (pk_poldek_current != NULL &&
	    (job_id == NULL || g_strcmp0 (pk_poldek_current->id, job_id) == 0)) {
		pk_poldek_job_cancel (pk_poldek_current);
		/* a job still in the queue clears this when it starts, and sends it again */
		pk_poldek_ctx_interrupt ();
	}
	g_mutex_unlock (&pk_poldek_current_lock);
}

static void
pk_poldek_test_ack_cancel (const gchar *job_id)
{
	g_autofree gchar *line = g_strdup_printf ("cancel-ack %s\n", job_id);
	ssize_t n G_GNUC_UNUSED = write (pk_poldek_test_sync_fd, line, strlen (line));
}

/* Returns FALSE if @line is not a valid request; the helper has to exit then. */
static gboolean
pk_poldek_handle_line (const gchar *line)
{
	g_autoptr(GError) error = NULL;
	PkPoldekJob *job;
	const gchar *op;
	const gchar *job_id;
	json_t *msg;

	msg = pk_poldek_protocol_parse_request (line, &error);
	if (msg == NULL) {
		g_printerr ("poldek: invalid request from the daemon: %s\n", error->message);
		return FALSE;
	}
	op = json_string_value (json_object_get (msg, "op"));
	job_id = json_string_value (json_object_get (msg, "job"));

	if (g_strcmp0 (op, "cancel") == 0) {
		if (job_id != NULL)
			pk_poldek_cancel_current (job_id);
		if (job_id != NULL && pk_poldek_test_sync_fd >= 0)
			pk_poldek_test_ack_cancel (job_id);
		json_decref (msg);
		return TRUE;
	}
	if (g_strcmp0 (op, "run") != 0) {
		pk_poldek_push (PK_POLDEK_INPUT_REQUEST, msg, NULL);
		return TRUE;
	}
	if (job_id == NULL || !json_is_string (json_object_get (msg, "role"))) {
		g_printerr ("poldek: invalid request from the daemon: run without job or role\n");
		json_decref (msg);
		return FALSE;
	}
	job = pk_poldek_job_new (job_id);
	g_mutex_lock (&pk_poldek_current_lock);
	pk_poldek_current = job;
	g_mutex_unlock (&pk_poldek_current_lock);
	pk_poldek_push (PK_POLDEK_INPUT_REQUEST, msg, job);
	return TRUE;
}

static gpointer
pk_poldek_reader_thread (gpointer user_data)
{
	int fd = GPOINTER_TO_INT (user_data);
	PkPoldekLineReader reader;
	gboolean terminating = FALSE;

	pk_poldek_line_reader_init (&reader);
	for (;;) {
		struct pollfd fds[] = {
			{ .fd = fd, .events = POLLIN },
			{ .fd = pk_poldek_term_pipe[0], .events = POLLIN },
		};
		gchar buf[65536];
		gchar *line;
		gboolean too_long;
		ssize_t n;

		if (poll (fds, G_N_ELEMENTS (fds), -1) < 0) {
			if (errno == EINTR)
				continue;
			g_printerr ("poldek: poll failed: %s\n", g_strerror (errno));
			pk_poldek_push (PK_POLDEK_INPUT_HANGUP, NULL, NULL);
			break;
		}
		if (fds[1].revents != 0) {
			while (read (pk_poldek_term_pipe[0], buf, sizeof (buf)) > 0)
				;
			pk_poldek_cancel_current (NULL);
			if (pk_poldek_test_sync_fd >= 0)
				pk_poldek_test_ack_cancel ("sigterm");
			if (!terminating)
				pk_poldek_push (PK_POLDEK_INPUT_TERMINATE, NULL, NULL);
			terminating = TRUE;
		}
		if (fds[0].revents == 0)
			continue;

		n = read (fd, buf, sizeof (buf));
		if (n < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (n <= 0) {
			if (n < 0)
				g_printerr ("poldek: cannot read from the daemon: %s\n",
					    g_strerror (errno));
			pk_poldek_push (PK_POLDEK_INPUT_HANGUP, NULL, NULL);
			break;
		}
		pk_poldek_line_reader_feed (&reader, buf, n);
		while ((line = pk_poldek_line_reader_next (&reader, &too_long)) != NULL) {
			gboolean ok = pk_poldek_handle_line (line);

			g_free (line);
			if (!ok)
				goto invalid;
		}
		if (too_long) {
			g_printerr ("poldek: request from the daemon exceeds %d bytes\n",
				    PK_POLDEK_PROTOCOL_MAX_LINE);
			goto invalid;
		}
	}
	pk_poldek_line_reader_clear (&reader);
	return NULL;
invalid:
	pk_poldek_push (PK_POLDEK_INPUT_INVALID, NULL, NULL);
	pk_poldek_line_reader_clear (&reader);
	return NULL;
}

static void
pk_poldek_sigterm_cb (int sig)
{
	int saved_errno = errno;
	ssize_t n G_GNUC_UNUSED = write (pk_poldek_term_pipe[1], "", 1);

	errno = saved_errno;
}

static json_t *
pk_poldek_bitfield_to_json (PkBitfield bits, gint last, const gchar *(*to_string) (gint value))
{
	json_t *array = json_array ();

	for (gint i = 0; i < last; i++) {
		if (pk_bitfield_contain (bits, i))
			json_array_append_new (array, json_string (to_string (i)));
	}
	return array;
}

static const gchar *
pk_poldek_filter_name (gint value)
{
	return pk_filter_enum_to_string (value);
}

static const gchar *
pk_poldek_group_name (gint value)
{
	return pk_group_enum_to_string (value);
}

static void
pk_poldek_send_hello (void)
{
	json_t *roles = json_array ();
	json_t *msg;

	for (guint i = 0; pk_poldek_roles[i].func != NULL; i++)
		json_array_append_new (
		    roles,
		    json_string (pk_role_enum_to_string (pk_poldek_roles[i].role)));
	msg = json_pack ("{s:s, s:i, s:s, s:s, s:s, s:o, s:o, s:o, s:[s], s:b}",
			 "ev",
			 "hello",
			 "protocol",
			 PK_POLDEK_PROTOCOL_VERSION,
			 "name",
			 "poldek",
			 "description",
			 "poldek - PLD Linux package manager",
			 "author",
			 "Arkadiusz Miśkiewicz <arekm@maven.pl>",
			 "roles",
			 roles,
			 "filters",
			 pk_poldek_bitfield_to_json (pk_poldek_filters_supported (),
						     PK_FILTER_ENUM_LAST,
						     pk_poldek_filter_name),
			 "groups",
			 pk_poldek_bitfield_to_json (pk_poldek_groups_used (),
						     PK_GROUP_ENUM_LAST,
						     pk_poldek_group_name),
			 "mime_types",
			 "application/x-rpm",
			 "parallel",
			 0);
	pk_poldek_protocol_send (msg);
	json_decref (msg);
}

/*
 * The client's proxies, where vfile looks for them, as the Python backend
 * library does, and its language, which poldek loads the descriptions in
 * when it reads the repositories; messages stay untranslated with LC_ALL=C.
 * A change sets poldek up again. vfile keeps the first proxy it found until
 * the helper exits, though.
 */
static void
pk_poldek_export_ctx (json_t *req)
{
	static gchar *exported = NULL;
	json_t *ctx = json_object_get (req, "ctx");
	const gchar *locale = json_string_value (json_object_get (ctx, "locale"));
	const struct
	{
		const gchar *key;
		const gchar *var;
		const gchar *scheme;
	} map[] = {
		/* clang-format off */
		{ "http",	"http_proxy",	"http://" },
		{ "https",	"https_proxy",	"http://" },
		{ "ftp",	"ftp_proxy",	"http://" },
		{ "socks",	"all_proxy",	"socks://" },
		{ "no_proxy",	"no_proxy",	NULL },
		/* clang-format on */
	};
	json_t *proxy = json_object_get (ctx, "proxy");
	g_autoptr(GString) all = g_string_new (NULL);

	if (locale == NULL || locale[0] == '\0')
		locale = "C";
	g_setenv ("LANGUAGE", locale, TRUE);
	g_string_append_printf (all, "LANGUAGE=%s;", locale);

	for (guint i = 0; i < G_N_ELEMENTS (map); i++) {
		const gchar *value = json_string_value (json_object_get (proxy, map[i].key));
		g_autofree gchar *url = NULL;

		if (value == NULL || value[0] == '\0') {
			g_unsetenv (map[i].var);
			continue;
		}
		if (map[i].scheme != NULL && strstr (value, "://") == NULL)
			url = g_strconcat (map[i].scheme, value, NULL);
		else
			url = g_strdup (value);
		g_setenv (map[i].var, url, TRUE);
		g_string_append_printf (all, "%s=%s;", map[i].var, url);
	}
	if (exported != NULL && g_strcmp0 (exported, all->str) != 0)
		pk_poldek_ctx_invalidate (pk_poldek_ctx);
	g_free (exported);
	exported = g_strdup (all->str);
}

static const PkPoldekRole *
pk_poldek_lookup_role (PkRoleEnum role)
{
	for (guint i = 0; pk_poldek_roles[i].func != NULL; i++) {
		if (pk_poldek_roles[i].role == role)
			return &pk_poldek_roles[i];
	}
	return NULL;
}

static void
pk_poldek_run (PkPoldekJob *job, json_t *req)
{
	const gchar *role_name = json_string_value (json_object_get (req, "role"));
	const PkPoldekRole *role = pk_poldek_lookup_role (pk_role_enum_from_string (role_name));
	GError *error = NULL;

	if (role == NULL) {
		pk_poldek_job_error (job,
				     PK_ERROR_ENUM_NOT_SUPPORTED,
				     "role '%s' is not supported",
				     role_name);
	} else if (pk_poldek_job_is_cancelled (job)) {
		pk_poldek_job_error (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
	} else {
		pk_poldek_export_ctx (req);
		pk_poldek_ctx_reset_interrupt ();
		/* cancelled between the check and the reset */
		if (pk_poldek_job_is_cancelled (job))
			pk_poldek_ctx_interrupt ();
		pk_poldek_job_emit (
		    job,
		    "status",
		    json_pack ("{s:s}", "status", pk_status_enum_to_string (PK_STATUS_ENUM_QUERY)));
		if (role->need != PK_POLDEK_NEED_NOTHING &&
		    !pk_poldek_ctx_prepare (pk_poldek_ctx, role->need, &error))
			pk_poldek_job_take_error (job, error);
		else if (pk_poldek_job_is_cancelled (job))
			pk_poldek_job_error (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
		else
			role->func (pk_poldek_ctx, job, req);
	}
	pk_poldek_job_finish (job);

	g_mutex_lock (&pk_poldek_current_lock);
	if (pk_poldek_current == job)
		pk_poldek_current = NULL;
	g_mutex_unlock (&pk_poldek_current_lock);
	pk_poldek_job_free (job);
}

/* Returns the exit status once the helper should exit, -1 to go on. */
static int
pk_poldek_dispatch (PkPoldekInput *input)
{
	const gchar *op;
	const gchar *job_id;

	switch (input->kind) {
	case PK_POLDEK_INPUT_HANGUP:
	case PK_POLDEK_INPUT_TERMINATE:
		return EXIT_SUCCESS;
	case PK_POLDEK_INPUT_INVALID:
		return EXIT_FAILURE;
	case PK_POLDEK_INPUT_REQUEST:
		break;
	}

	op = json_string_value (json_object_get (input->msg, "op"));
	job_id = json_string_value (json_object_get (input->msg, "job"));
	if (input->job != NULL) {
		pk_poldek_run (input->job, input->msg);
	} else if (g_strcmp0 (op, "hello") == 0) {
		pk_poldek_send_hello ();
	} else if (g_strcmp0 (op, "exit") == 0) {
		return EXIT_SUCCESS;
	} else if (job_id != NULL) {
		g_autoptr(PkPoldekJob) job = pk_poldek_job_new (job_id);

		pk_poldek_job_error (job, PK_ERROR_ENUM_NOT_SUPPORTED, "unknown request '%s'", op);
		pk_poldek_job_finish (job);
	}
	return -1;
}

int
main (int argc, char *argv[])
{
	struct sigaction sa = { .sa_handler = pk_poldek_sigterm_cb, .sa_flags = SA_RESTART };
	const gchar *fd_env = g_getenv ("PK_BACKEND_PROTOCOL_FD");
	g_autofree gchar *config = NULL;
	g_autofree gchar *root = NULL;
	/* for tests and by hand; the daemon runs the helper without arguments */
	/* clang-format off */
	const GOptionEntry options[] = {
		{ "config", 0, 0, G_OPTION_ARG_FILENAME, &config, "poldek configuration", "FILE" },
		{ "root", 0, 0, G_OPTION_ARG_FILENAME, &root, "Root directory to manage", "DIR" },
		{ "test-sync-fd", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_INT, &pk_poldek_test_sync_fd,
		  "Confirm each cancel on this file descriptor", "FD" },
		{ NULL },
	};
	/* clang-format on */
	g_autoptr(GOptionContext) option_context = g_option_context_new (NULL);
	g_autoptr(GError) error = NULL;
	GThread *reader;
	int fd;
	int rc = -1;

	g_option_context_add_main_entries (option_context, options, NULL);
	if (!g_option_context_parse (option_context, &argc, &argv, &error)) {
		g_printerr ("poldek: %s\n", error->message);
		return EXIT_FAILURE;
	}

	/* poldek's and rpm's messages are matched in English */
	g_setenv ("LC_ALL", "C", TRUE);
	fd = fd_env != NULL ? atoi (fd_env) : 3;
	if (fcntl (fd, F_GETFD) < 0) {
		g_printerr ("poldek: protocol fd %d is not open: %s\n", fd, g_strerror (errno));
		return EXIT_FAILURE;
	}
	/* rpm runs as a child: an inherited socket would keep the daemon's end open */
	fcntl (fd, F_SETFD, FD_CLOEXEC);
	if (pipe2 (pk_poldek_term_pipe, O_CLOEXEC | O_NONBLOCK) < 0) {
		g_printerr ("poldek: cannot create a pipe: %s\n", g_strerror (errno));
		return EXIT_FAILURE;
	}
	sigemptyset (&sa.sa_mask);
	sigaction (SIGTERM, &sa, NULL);

	pk_poldek_protocol_set_fd (fd);
	pk_poldek_ctx = pk_poldek_ctx_new (config, root);
	pk_poldek_queue = g_async_queue_new ();
	reader = g_thread_new ("protocol-reader", pk_poldek_reader_thread, GINT_TO_POINTER (fd));
	g_thread_unref (reader);

	while (rc < 0) {
		PkPoldekInput *input = g_async_queue_pop (pk_poldek_queue);

		rc = pk_poldek_dispatch (input);
		if (input->msg != NULL)
			json_decref (input->msg);
		g_free (input);
	}
	pk_poldek_ctx_free (pk_poldek_ctx);
	return rc;
}
