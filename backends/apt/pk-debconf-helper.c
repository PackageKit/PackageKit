#include "config.h"
#include <stdio.h>
#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-client-helper.h>
#include <systemd/sd-daemon.h>

static GMainLoop *main_loop = NULL;
static PkClientHelper *helper = NULL;

static gboolean exit_loop (gpointer user_data)
{
	g_debug ("Checking for active connections");

	if (!pk_client_helper_is_active (helper)) {
		g_message ("No active connections, exiting");
		g_main_loop_quit (main_loop);
	}
	return TRUE;
}

int main (void)
{
	g_auto(GStrv) argv = NULL;
	g_auto(GStrv) envp = NULL;
	g_autoptr(GSocket) socket = NULL;
	g_autoptr(GError) error = NULL;
	int fd = -1;

	main_loop = g_main_loop_new (NULL, FALSE);
	pk_client_create_helper_argv_envp (&argv, &envp);

	if (sd_listen_fds (0) != 1) {
		g_error ("No or too many file descriptors received.");
		exit (1);
	}

	fd = SD_LISTEN_FDS_START + 0;
	socket = g_socket_new_from_fd (fd, &error);

	if (error != NULL) {
		g_error ("%s", error->message);
		return 1;
	}

	helper = pk_client_helper_new ();
	if (!pk_client_helper_start_with_socket (helper, socket, argv, envp, &error)) {
		g_error ("%s", error->message);
		return 1;
	}

	g_timeout_add_seconds (60, exit_loop, NULL);

	g_main_loop_run (main_loop);

	g_object_unref (helper);
	g_main_loop_unref (main_loop);

	return 0;
}
