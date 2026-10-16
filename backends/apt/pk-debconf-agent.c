/*
 * Copyright (C) 2019-2020 Julian Andres Klode <julian.klode@canonical.com>
 * Copyright (C) 2022-2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"
#include <stdio.h>
#include <gio/gunixconnection.h>
#include <gio/gunixsocketaddress.h>
#include <pk-client.h>
#include <pk-client-helper-private.h>
#include <systemd/sd-daemon.h>

/* systemd runs one instance of this program client connection (Accept=yes).
 * The client hands us the listening socket of its (debconf) transaction, created
 * by the daemon, and we answer the debconf questions on it for as long as the
 * transaction lives */

static GMainLoop *main_loop = NULL;
static PkClientHelper *helper = NULL;
static gchar *transaction_path = NULL;

static gboolean
exit_loop (gpointer user_data)
{
    g_debug ("Checking for active connections");

    /* the daemon removes the socket when the transaction is destroyed */
    if (!pk_client_helper_is_active (helper) && !g_file_test (transaction_path, G_FILE_TEST_EXISTS)) {
        g_message ("Transaction is gone, exiting");
        g_main_loop_quit (main_loop);
    }
    return TRUE;
}

int
main (void)
{
    gint fd;
    g_auto(GStrv) argv = NULL;
    g_auto(GStrv) envp = NULL;
    g_autoptr(GSocket) client = NULL;
    g_autoptr(GSocketConnection) conn = NULL;
    g_autoptr(GSocket) socket = NULL;
    g_autoptr(GSocketAddress) address = NULL;
    g_autoptr(GError) error = NULL;

    main_loop = g_main_loop_new (NULL, FALSE);
    if (!pk_client_helper_create_argv_envp (&argv, &envp)) {
        g_error ("No debconf frontend available.");
        return 1;
    }

    if (sd_listen_fds (0) != 1) {
        g_error ("No or too many file descriptors received.");
        return 1;
    }
    client = g_socket_new_from_fd (SD_LISTEN_FDS_START + 0, &error);
    if (client == NULL) {
        g_error ("%s", error->message);
        return 1;
    }
    conn = g_socket_connection_factory_create_connection (client);
    fd = g_unix_connection_receive_fd (G_UNIX_CONNECTION (conn), NULL, &error);
    if (fd < 0) {
        g_error ("No transaction socket received: %s", error->message);
        return 1;
    }
    socket = g_socket_new_from_fd (fd, &error);
    if (socket != NULL)
        address = g_socket_get_local_address (socket, &error);
    if (address == NULL || !G_IS_UNIX_SOCKET_ADDRESS (address)) {
        g_error ("Invalid transaction socket: %s", error != NULL ? error->message : "not a Unix socket");
        return 1;
    }
    transaction_path = g_strdup (g_unix_socket_address_get_path (G_UNIX_SOCKET_ADDRESS (address)));

    helper = pk_client_helper_new ();
    if (!pk_client_helper_start (helper, socket, argv, envp, &error)) {
        g_error ("%s", error->message);
        return 1;
    }
    g_debug ("answering questions for %s", transaction_path);

    g_timeout_add_seconds (60, exit_loop, NULL);

    g_main_loop_run (main_loop);

    g_object_unref (helper);
    g_free (transaction_path);
    g_main_loop_unref (main_loop);

    return 0;
}
