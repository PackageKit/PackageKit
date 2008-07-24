/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is PackageKit plugin code.
 *
 * The Initial Developer of the Original Code is
 * Red Hat, Inc.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <config.h>
#include "util.h"

struct PkpExecuteCommandAsyncHandle {
    PkpExecuteCommandCallback callback;
    void *callback_data;

    GError *error;
    int status;
    GString *output;

    guint io_watch;
    guint child_watch;
    
    gboolean exited;
    gboolean closed;
};
    
static void
pkp_execute_command_async_free(PkpExecuteCommandAsyncHandle *handle)
{
    if (handle->io_watch)
        g_source_remove(handle->io_watch);
    if (handle->child_watch)
        g_source_remove(handle->child_watch);
    
    if (handle->error)
        g_error_free(handle->error);
    
    g_string_free(handle->output, TRUE);
    g_free(handle);
}

static void
pkp_execute_command_async_finish(PkpExecuteCommandAsyncHandle *handle)
{
    handle->callback(handle->error, handle->status, handle->output->str, handle->callback_data);

    pkp_execute_command_async_free(handle);
}
     
static void
pkp_execute_async_child_watch(GPid     pid,
			      int      status,
			      gpointer data)
{
    PkpExecuteCommandAsyncHandle *handle = (PkpExecuteCommandAsyncHandle *)data;

    handle->exited = TRUE;
    handle->child_watch = 0;
    handle->status = status;

    if (handle->exited && handle->closed)
        pkp_execute_command_async_finish(handle);
}
    
static gboolean
pkp_execute_async_io_watch(GIOChannel   *source,
			   GIOCondition  condition,
			   gpointer      data)
{
    PkpExecuteCommandAsyncHandle *handle = (PkpExecuteCommandAsyncHandle *)data;
    GIOStatus status;
    char buf[1024];
    gsize bytes_read;

    handle->io_watch = 0;
    
    status = g_io_channel_read_chars(source, buf, sizeof(buf), &bytes_read, &handle->error);
    switch (status) {
    case G_IO_STATUS_ERROR:
        g_io_channel_close(source);
        handle->closed = TRUE;
        if (handle->exited && handle->closed)
            pkp_execute_command_async_finish(handle);
        return FALSE;
    case G_IO_STATUS_NORMAL:
        g_string_append_len(handle->output, buf, bytes_read);
        break;
    case G_IO_STATUS_EOF:
        g_io_channel_close(source);
        handle->closed = TRUE;
        if (handle->exited && handle->closed)
            pkp_execute_command_async_finish(handle);
        
        return FALSE;
    case G_IO_STATUS_AGAIN:
        /* Should not be reached */
        break;
    }

    return TRUE;
}

PkpExecuteCommandAsyncHandle *
pkp_execute_command_async(char                     **argv,
			  PkpExecuteCommandCallback  callback,
			  void                      *callback_data)
{
    PkpExecuteCommandAsyncHandle *handle;
    GPid child_pid;
    int out_fd;
    GIOChannel *channel;
    const char *locale_encoding;

    handle = g_new(PkpExecuteCommandAsyncHandle, 1);
    
    handle->callback = callback;
    handle->callback_data = callback_data;
    
    handle->error = NULL;
    handle->status = -1;
    handle->output = g_string_new(NULL);

    handle->closed = FALSE;
    handle->exited = FALSE;
    
    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
                                  (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
                                  NULL, NULL,
                                  &child_pid,
                                  NULL, &out_fd, NULL,
                                  &handle->error)) {
        pkp_execute_command_async_finish(handle);
        return NULL;
    }

    channel = g_io_channel_unix_new(out_fd);
    
    g_get_charset(&locale_encoding);
    g_io_channel_set_encoding(channel, locale_encoding, NULL);

    handle->io_watch = g_io_add_watch(channel,
                                      (GIOCondition)(G_IO_IN | G_IO_HUP),
                                      pkp_execute_async_io_watch,
                                      handle);
    g_io_channel_unref(channel);

    handle->child_watch = g_child_watch_add(child_pid,
                                            pkp_execute_async_child_watch,
                                            handle);

    return handle;
}

void
pkp_execute_command_async_cancel (PkpExecuteCommandAsyncHandle *handle)
{
    pkp_execute_command_async_free(handle);
}
