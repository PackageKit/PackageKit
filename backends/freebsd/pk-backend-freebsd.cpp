/*
 * Copyright (C) Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *               Author: Gleb Popov <arrowd@FreeBSD.org>
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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/capsicum.h>
#include <pwd.h>

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#include <pk-backend.h>
#include <pk-backend-job.h>

#include <pkg.h>

#include <functional>
#include <memory>
#include <vector>

typedef struct {
    gboolean has_signature;
    gboolean repo_enabled_devel;
    gboolean repo_enabled_fedora;
    gboolean repo_enabled_livna;
    gboolean repo_enabled_local;
    gboolean updated_gtkhtml;
    gboolean updated_kernel;
    gboolean updated_powertop;
    gboolean use_blocked;
    gboolean use_distro_upgrade;
    gboolean use_eula;
    gboolean use_gpg;
    gboolean use_media;
    gboolean use_trusted;
    gchar **package_ids;
    gchar **values;
    PkBitfield filters;
    gboolean fake_db_locked;
} PkBackendFreeBSDPrivate;

typedef struct {
    guint progress_percentage;
    GSocket *socket;
    guint socket_listen_id;
    GCancellable *cancellable;
    gulong signal_timeout;
} PkBackendFreeBSDJobData;

static PkBackendFreeBSDPrivate *priv;

static int
event_callback(void *data, struct pkg_event *ev);

template<typename T>
using deleted_unique_ptr = std::unique_ptr<T,std::function<void(T*)>>;

template<typename Func>
static void withRepo(pkgdb_lock_t lockType, PkBackendJob* job, Func f)
{
    g_assert(!pkg_initialized());

    // TODO: Use this to report progress from various jobs
    pkg_event_register(&event_callback, nullptr);

    if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK) {
        g_error("pkg_ini failure");
        return;
    }
    // can't pass nullptr here, unique_ptr won't call the deleter
    auto libpkgDeleter = deleted_unique_ptr<void>(reinterpret_cast<void*>(0xDEADC0DE), [](void* p) { pkg_shutdown(); });

    // TODO: call pkgdb_access here?

    struct pkgdb* db = NULL;
    if (pkgdb_open (&db, PKGDB_MAYBE_REMOTE) != EPKG_OK) {
        g_error("pkgdb_open failed");
        return;
    }
    auto dbDeleter = deleted_unique_ptr<struct pkgdb>(db, [](struct pkgdb* db) {pkgdb_close (db); });

    if (pkgdb_obtain_lock(db, lockType) != EPKG_OK) {
        g_error("Cannot get a read lock on a database, it is locked by another process");
        return;
    }
    if (lockType != PKGDB_LOCK_READONLY)
        pk_backend_job_set_locked (job, TRUE);
    auto lockDeleter = deleted_unique_ptr<struct pkgdb>(db, [job, lockType](struct pkgdb* db) {
        pkgdb_release_lock (db, lockType);
        if (lockType != PKGDB_LOCK_READONLY)
            pk_backend_job_set_locked (job, FALSE);
    });

    f (db);
}

// stolen from pkg/src/event.c
// required as some libpkg internals calls out to pkg_emit_event and expect
// a handler ("event_callback" in this case) to call the callback, which does the
// actual work.
#include <xstring.h>
#define HAVE_CAPSICUM
static xstring *msg_buf = NULL;
static xstring *messages = NULL;
static xstring *conflicts = NULL;
static bool progress_debit = false;

static int
event_sandboxed_call(pkg_sandbox_cb func, int fd, void *ud);
static int
event_sandboxed_get_string(pkg_sandbox_cb func, char **result, int64_t *len,
        void *ud);
static void drop_privileges(void);

static int
event_callback(void *data, struct pkg_event *ev)
{
    struct pkg *pkg = NULL, *pkg_new, *pkg_old;
    struct pkg_event_conflict *cur_conflict;
    const char *reponame;

    if (msg_buf == NULL) {
        msg_buf = xstring_new();
    }

    switch(ev->type) {
    case PKG_EVENT_ERRNO:
        g_warning("libpkg: %s(%s): %s", ev->e_errno.func, ev->e_errno.arg,
            strerror(ev->e_errno.no));
        break;
    case PKG_EVENT_ERROR:
        g_warning("libpkg: %s", ev->e_pkg_error.msg);
        break;
    case PKG_EVENT_NOTICE:
        //g_warning("libpkg: %s\n", ev->e_pkg_notice.msg);
        break;
    case PKG_EVENT_DEVELOPER_MODE:
        g_warning("libpkg: DEVELOPER_MODE: %s", ev->e_pkg_error.msg);
        break;
    case PKG_EVENT_UPDATE_ADD:
        if (!isatty(STDOUT_FILENO))
            break;
        g_warning("libpkg: Pushing new entries %d/%d", ev->e_upd_add.done, ev->e_upd_add.total);
        break;
    case PKG_EVENT_UPDATE_REMOVE:
        if (!isatty(STDOUT_FILENO))
            break;
        g_warning("libpkg: Removing entries %d/%d", ev->e_upd_remove.done, ev->e_upd_remove.total);
        break;
    case PKG_EVENT_FETCH_BEGIN:
        break;
    case PKG_EVENT_FETCH_FINISHED:
        progress_debit = false;
        break;
    case PKG_EVENT_INSTALL_BEGIN:
        break;
    case PKG_EVENT_INSTALL_FINISHED:
        break;
    case PKG_EVENT_EXTRACT_BEGIN:
        break;
    case PKG_EVENT_EXTRACT_FINISHED:
        break;
    case PKG_EVENT_ADD_DEPS_BEGIN:
        break;
    case PKG_EVENT_ADD_DEPS_FINISHED:
        break;
    case PKG_EVENT_INTEGRITYCHECK_BEGIN:
        g_warning("libpkg: Checking integrity...");
        break;
    case PKG_EVENT_INTEGRITYCHECK_FINISHED:
        g_warning("libpkg:  done (%d conflicting)", ev->e_integrity_finished.conflicting);
        if (conflicts != NULL) {
            fflush(conflicts->fp);
            g_warning("libpkg: %s", conflicts->buf);
            xstring_free(conflicts);
            conflicts = NULL;
        }
        break;
    case PKG_EVENT_INTEGRITYCHECK_CONFLICT:
        g_warning("libpkg: Conflict found on path %s between %s and ",
            ev->e_integrity_conflict.pkg_path,
            ev->e_integrity_conflict.pkg_uid);
        cur_conflict = ev->e_integrity_conflict.conflicts;
        while (cur_conflict) {
            if (cur_conflict->next)
                g_warning("libpkg: %s, ", cur_conflict->uid);
            else
                g_warning("libpkg: %s", cur_conflict->uid);

            cur_conflict = cur_conflict->next;
        }
        break;
    case PKG_EVENT_DEINSTALL_BEGIN:
        pkg = ev->e_install_begin.pkg;
        pkg_fprintf(msg_buf->fp, "Deinstalling %n-%v...\n", pkg, pkg);
        fflush(msg_buf->fp);
        g_warning("libpkg: %s", msg_buf->buf);
        break;
    case PKG_EVENT_DEINSTALL_FINISHED:
        break;
    case PKG_EVENT_DELETE_FILES_BEGIN:
        break;
    case PKG_EVENT_DELETE_FILES_FINISHED:
        break;
    case PKG_EVENT_UPGRADE_BEGIN:
        pkg_new = ev->e_upgrade_begin.n;
        pkg_old = ev->e_upgrade_begin.o;

        switch (pkg_version_change_between(pkg_new, pkg_old)) {
        case PKG_DOWNGRADE:
            pkg_fprintf(msg_buf->fp, "Downgrading %n from %v to %v...\n",
                pkg_new, pkg_old, pkg_new);
            break;
        case PKG_REINSTALL:
            pkg_fprintf(msg_buf->fp, "Reinstalling %n-%v...\n",
            pkg_old, pkg_old);
            break;
        case PKG_UPGRADE:
            pkg_fprintf(msg_buf->fp, "Upgrading %n from %v to %v...\n",
                pkg_new, pkg_old, pkg_new);
            break;
        }
        fflush(msg_buf->fp);
        g_warning("libpkg: %s", msg_buf->buf);
        break;
    case PKG_EVENT_UPGRADE_FINISHED:
        break;
    case PKG_EVENT_LOCKED:
        pkg = ev->e_locked.pkg;
        // TODO:
        //pkg_printf("\n%n-%v is locked and may not be modified\n", pkg, pkg);
        break;
    case PKG_EVENT_REQUIRED:
        pkg = ev->e_required.pkg;
        // TODO:
        //pkg_printf("\n%n-%v is required by: %r%{%rn-%rv%| %}", pkg, pkg, pkg);
        break;
    case PKG_EVENT_ALREADY_INSTALLED:
        pkg = ev->e_already_installed.pkg;
        //TODO:
        //pkg_printf("the most recent version of %n-%v is already installed\n",
        //		pkg, pkg);
        break;
    case PKG_EVENT_NOT_FOUND:
        g_warning("libpkg: Package '%s' was not found in "
            "the repositories", ev->e_not_found.pkg_name);
        break;
    case PKG_EVENT_MISSING_DEP:
        g_warning("libpkg: Missing dependency '%s'",
            pkg_dep_name(ev->e_missing_dep.dep));
        break;
    case PKG_EVENT_NOREMOTEDB:
        g_warning("libpkg: Unable to open remote database \"%s\". ",
            ev->e_remotedb.repo);
        break;
    case PKG_EVENT_NOLOCALDB:
        g_warning("libpkg: Local package database nonexistent!");
        break;
    case PKG_EVENT_NEWPKGVERSION:
        g_warning("libpkg: New version of pkg detected; it needs to be "
            "installed first.");
        break;
    case PKG_EVENT_FILE_MISMATCH:
        pkg = ev->e_file_mismatch.pkg;
        // TODO:
        //pkg_fprintf(stderr, "%n-%v: checksum mismatch for %Fn\n", pkg,
        //    pkg, ev->e_file_mismatch.file);
        break;
    case PKG_EVENT_FILE_MISSING:
        pkg = ev->e_file_missing.pkg;
        // TODO:
        // pkg_fprintf(stderr, "%n-%v: missing file %Fn\n", pkg, pkg,
        //     ev->e_file_missing.file);
        break;
    case PKG_EVENT_PLUGIN_ERRNO:
        g_warning("libpkg: %s: %s(%s): %s",
            pkg_plugin_get(ev->e_plugin_errno.plugin, PKG_PLUGIN_NAME),
            ev->e_plugin_errno.func, ev->e_plugin_errno.arg,
            strerror(ev->e_plugin_errno.no));
        break;
    case PKG_EVENT_PLUGIN_ERROR:
        g_warning("libpkg: %s: %s",
            pkg_plugin_get(ev->e_plugin_error.plugin, PKG_PLUGIN_NAME),
            ev->e_plugin_error.msg);
        break;
    case PKG_EVENT_PLUGIN_INFO:
        g_warning("libpkg: %s: %s\n",
            pkg_plugin_get(ev->e_plugin_info.plugin, PKG_PLUGIN_NAME),
            ev->e_plugin_info.msg);
        break;
    case PKG_EVENT_INCREMENTAL_UPDATE:
        g_warning("libpkg: %s repository update completed. %d packages processed.\n",
            ev->e_incremental_update.reponame,
            ev->e_incremental_update.processed);
        break;
    case PKG_EVENT_DEBUG:
        // fprintf(stderr, "DBG(%d)[%d]> %s\n", ev->e_debug.level,
        // 	(int)getpid(), ev->e_debug.msg);
        break;
    case PKG_EVENT_QUERY_YESNO:
        g_error("libpkg: Asking for yes/no");
        // return ( ev->e_query_yesno.deft ?
        // 	query_yesno(true, ev->e_query_yesno.msg, "[Y/n]") :
        // 	query_yesno(false, ev->e_query_yesno.msg, "[y/N]") );
        break;
    case PKG_EVENT_QUERY_SELECT:
        g_error("libpkg: Query select");
        // return query_select(ev->e_query_select.msg, ev->e_query_select.items,
        // 	ev->e_query_select.ncnt, ev->e_query_select.deft);
        break;
    case PKG_EVENT_SANDBOX_CALL:
        return ( event_sandboxed_call(ev->e_sandbox_call.call,
                ev->e_sandbox_call.fd,
                ev->e_sandbox_call.userdata) );
        break;
    case PKG_EVENT_SANDBOX_GET_STRING:
        return ( event_sandboxed_get_string(ev->e_sandbox_call_str.call,
                ev->e_sandbox_call_str.result,
                ev->e_sandbox_call_str.len,
                ev->e_sandbox_call_str.userdata) );
        break;
    case PKG_EVENT_PROGRESS_START:
        //progressbar_start(ev->e_progress_start.msg);
        break;
    case PKG_EVENT_PROGRESS_TICK:
        // progressbar_tick(ev->e_progress_tick.current,
        //     ev->e_progress_tick.total);
        break;
    case PKG_EVENT_BACKUP:
        fprintf(msg_buf->fp, "Backing up");
        break;
    case PKG_EVENT_RESTORE:
        fprintf(msg_buf->fp, "Restoring");
        break;
    case PKG_EVENT_NEW_ACTION:
        break;
    case PKG_EVENT_MESSAGE:
        if (messages == NULL)
            messages = xstring_new();
        fprintf(messages->fp, "%s", ev->e_pkg_message.msg);
        break;
    case PKG_EVENT_CLEANUP_CALLBACK_REGISTER:
        // if (!signal_handler_installed) {
        // 	signal(SIGINT, cleanup_handler);
        // 	signal_handler_installed = true;
        // }
        // evtmp = malloc(sizeof(struct cleanup));
        // evtmp->cb = ev->e_cleanup_callback.cleanup_cb;
        // evtmp->data = ev->e_cleanup_callback.data;
        // tll_push_back(cleanup_list, evtmp);
        break;
    case PKG_EVENT_CLEANUP_CALLBACK_UNREGISTER:
        // if (!signal_handler_installed)
        // 	break;
        // tll_foreach(cleanup_list, it) {
        // 	evtmp = it->item;
        // 	if (evtmp->cb == ev->e_cleanup_callback.cleanup_cb &&
        // 	    evtmp->data == ev->e_cleanup_callback.data) {
        // 		tll_remove(cleanup_list, it);
        // 		break;
        // 	}
        // }
        break;
    case PKG_EVENT_CONFLICTS:
        if (conflicts == NULL) {
            conflicts = xstring_new();
        }
        pkg_fprintf(conflicts->fp, "  - %n-%v",
            ev->e_conflicts.p1, ev->e_conflicts.p1);
        if (pkg_repos_total_count() > 1) {
            pkg_get_string(ev->e_conflicts.p1, PKG_REPONAME, reponame);
            fprintf(conflicts->fp, " [%s]",
                reponame == NULL ? "installed" : reponame);
        }
        pkg_fprintf(conflicts->fp, " conflicts with %n-%v",
            ev->e_conflicts.p2, ev->e_conflicts.p2);
        if (pkg_repos_total_count() > 1) {
            pkg_get_string(ev->e_conflicts.p2, PKG_REPONAME, reponame);
            fprintf(conflicts->fp, " [%s]",
                reponame == NULL ? "installed" : reponame);
        }
        fprintf(conflicts->fp, " on %s\n",
            ev->e_conflicts.path);
        break;
    case PKG_EVENT_TRIGGER:
        if (ev->e_trigger.cleanup)
            g_warning("libpkg: ==> Cleaning up trigger: %s\n", ev->e_trigger.name);
        else
            g_warning("libpkg: ==> Running trigger: %s\n", ev->e_trigger.name);
        break;
    default:
        break;
    }

    return 0;
}

static int
event_sandboxed_call(pkg_sandbox_cb func, int fd, void *ud)
{
    pid_t pid;
    int status, ret;
    struct rlimit rl_zero;

    ret = -1;
    pid = fork();

    switch(pid) {
    case -1:
        g_warning("libpkg: fork failed");
        return (EPKG_FATAL);
        break;
    case 0:
        break;
    default:
        /* Parent process */
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) {
                g_warning("libpkg: Sandboxed process pid=%d", (int)pid);
                ret = -1;
                break;
            }
        }

        if (WIFEXITED(status)) {
            ret = WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            /* Process got some terminating signal, hence stop the loop */
            g_warning("libpkg: Sandboxed process pid=%d terminated abnormally by signal: %d\n",
                    (int)pid, WTERMSIG(status));
            ret = -1;
        }
        return (ret);
    }

    rl_zero.rlim_cur = rl_zero.rlim_max = 0;
    if (setrlimit(RLIMIT_NPROC, &rl_zero) == -1)
        g_error("libpkg: Unable to setrlimit(RLIMIT_NPROC)");

    /* Here comes child process */
#ifdef HAVE_CAPSICUM
#ifndef PKG_COVERAGE
    if (cap_enter() < 0 && errno != ENOSYS) {
        g_error("libpkg: cap_enter() failed");
        _exit(EXIT_FAILURE);
    }
#endif
#endif

    ret = func(fd, ud);

    _exit(ret);
}

static int
event_sandboxed_get_string(pkg_sandbox_cb func, char **result, int64_t *len,
        void *ud)
{
    pid_t pid;
    struct rlimit rl_zero;
    int	status, ret = EPKG_OK;
    int pair[2], r, allocated_len = 0, off = 0;
    char *buf = NULL;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1) {
        g_warning("libpkg: socketpair failed");
        return (EPKG_FATAL);
    }

    pid = fork();

    switch(pid) {
    case -1:
        g_warning("libpkg: fork failed");
        return (EPKG_FATAL);
        break;
    case 0:
        break;
    default:
        /* Parent process */
        close(pair[0]);
        /*
            * We use blocking IO here as if the child is terminated we would have
            * EINTR here
            */
        buf = reinterpret_cast<char*>(malloc(BUFSIZ));
        if (buf == NULL) {
            g_warning("libpkg: malloc failed");
            return (EPKG_FATAL);
        }
        allocated_len = BUFSIZ;
        do {
            if (off >= allocated_len) {
                allocated_len *= 2;
                buf = reinterpret_cast<char*>(realloc(buf, allocated_len));
                if (buf == NULL) {
                    g_warning("libpkg: realloc failed");
                    return (EPKG_FATAL);
                }
            }

            r = read(pair[1], buf + off, allocated_len - off);
            if (r == -1 && errno != EINTR) {
                free(buf);
                g_warning("libpkg: read failed");
                return (EPKG_FATAL);
            }
            else if (r > 0) {
                off += r;
            }
        } while (r > 0);

        /* Fill the result buffer */
        *len = off;
        *result = buf;
        if (*result == NULL) {
            g_warning("libpkg: malloc failed");
            kill(pid, SIGTERM);
            ret = EPKG_FATAL;
        }
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) {
                g_warning("libpkg: Sandboxed process pid=%d", (int)pid);
                ret = -1;
                break;
            }
        }

        if (WIFEXITED(status)) {
            ret = WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            /* Process got some terminating signal, hence stop the loop */
            g_warning("libpkg: Sandboxed process pid=%d terminated abnormally by signal: %d\n",
                    (int)pid, WTERMSIG(status));
            ret = -1;
        }
        return (ret);
    }

    /* Here comes child process */
    close(pair[1]);

    drop_privileges();

    rl_zero.rlim_cur = rl_zero.rlim_max = 0;
    if (setrlimit(RLIMIT_NPROC, &rl_zero) == -1)
        g_error("libpkg: Unable to setrlimit(RLIMIT_NPROC)");

#ifdef HAVE_CAPSICUM
#ifndef PKG_COVERAGE
    if (cap_enter() < 0 && errno != ENOSYS) {
        g_warning("libpkg: cap_enter() failed");
        return (EPKG_FATAL);
    }
#endif
#endif

    ret = func(pair[0], ud);

    close(pair[0]);

    _exit(ret);
}

static void drop_privileges(void)
{
    struct passwd *nobody;

    if (geteuid() == 0) {
        nobody = getpwnam("nobody");
        if (nobody == NULL)
            g_error("libpkg: Unable to drop privileges: no 'nobody' user");
        setgroups(1, &nobody->pw_gid);
        /* setgid also sets egid and setuid also sets euid */
        if (setgid(nobody->pw_gid) == -1)
            g_error("libpkg: Unable to setgid");
        if (setuid(nobody->pw_uid) == -1)
            g_error("libpkg: Unable to setuid");
    }
}

static void
pk_freebsd_search(PkBackendJob *job, PkBitfield filters, gchar **values);

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
    /* create private area */
    priv = g_new0 (PkBackendFreeBSDPrivate, 1);
    priv->repo_enabled_fedora = TRUE;
    priv->repo_enabled_devel = TRUE;
    priv->repo_enabled_livna = TRUE;
    priv->use_trusted = TRUE;
}

void
pk_backend_destroy (PkBackend *backend)
{
    g_free (priv);
}

PkBitfield
pk_backend_get_groups (PkBackend *backend)
{
    return pk_bitfield_from_enums (PK_GROUP_ENUM_ACCESSIBILITY, //accessibility
        PK_GROUP_ENUM_COMMUNICATION, //comms
        PK_GROUP_ENUM_DESKTOP_GNOME, //gnome-* ports
        PK_GROUP_ENUM_DESKTOP_KDE, //plasma5-* ports
        PK_GROUP_ENUM_DESKTOP_OTHER, //budgie, enlightenment, etc.
        PK_GROUP_ENUM_DESKTOP_XFCE, //xfce-* ports
        PK_GROUP_ENUM_EDUCATION, //edu
        PK_GROUP_ENUM_FONTS, //x11-fonts
        PK_GROUP_ENUM_GAMES, //games
        PK_GROUP_ENUM_GRAPHICS, //graphics
        PK_GROUP_ENUM_INTERNET, //www
        PK_GROUP_ENUM_NETWORK, //net
        PK_GROUP_ENUM_PROGRAMMING, //devel
        PK_GROUP_ENUM_MULTIMEDIA, //multimedia
        PK_GROUP_ENUM_SECURITY, //security
        PK_GROUP_ENUM_SYSTEM, //sysutils
        PK_GROUP_ENUM_SCIENCE, //science
        -1);
}

PkBitfield
pk_backend_get_filters (PkBackend *backend)
{
    return pk_bitfield_from_enums (PK_FILTER_ENUM_INSTALLED,
        PK_FILTER_ENUM_NOT_INSTALLED,
        -1);
}

gchar **
pk_backend_get_mime_types (PkBackend *backend)
{
    const gchar *mime_types[] = {
                "application/x-xar",
                NULL };
    return g_strdupv ((gchar **) mime_types);
}

void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
    g_error("pk_backend_cancel not implemented yet");
}

void
pk_backend_depends_on (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    g_error("pk_backend_depends_on not implemented yet");
}

void
pk_backend_get_details_local (PkBackend *backend, PkBackendJob *job, gchar **files)
{
    g_error("pk_backend_get_details_local not implemented yet");
}

void
pk_backend_get_files_local (PkBackend *backend, PkBackendJob *job, gchar **_files)
{
    g_error("pk_backend_get_files_local not implemented yet");
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    g_error("pk_backend_get_details not implemented yet");
}

// TODO: This requires pkgbase support
// void
// pk_backend_get_distro_upgrades (PkBackend *backend, PkBackendJob *job)
// {
// }

void
pk_backend_get_files (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    g_error("pk_backend_get_files not implemented yet");
}

void
pk_backend_required_by (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
    g_error("pk_backend_required_by not implemented yet");
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
    g_error("pk_backend_get_update_detail not implemented yet");
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    PkBackendFreeBSDJobData *job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
    pk_backend_job_set_percentage (job, PK_BACKEND_PERCENTAGE_INVALID);
    /* check network state */
    if (!pk_backend_is_online (backend)) {
        pk_backend_job_error_code (job, PK_ERROR_ENUM_NO_NETWORK, "Cannot check when offline");
        pk_backend_job_finished (job);
        return;
    }

    g_error("pk_backend_get_updates not implemented yet");
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
    g_error("pk_backend_install_packages not implemented yet");
}

void
pk_backend_install_signature (PkBackend *backend, PkBackendJob *job, PkSigTypeEnum type,
                              const gchar *key_id, const gchar *package_id)
{
    g_error("pk_backend_install_signature not implemented yet");
}

void
pk_backend_install_files (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **full_paths)
{
    g_error("pk_backend_install_files not implemented yet");
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);

    pkg_event_register(&event_callback, nullptr);

    g_assert(!pkg_initialized());
    if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK)
    {
        g_error("pkg_ini failure");
        return;
    }
    // can't pass nullptr here, unique_ptr won't call the deleter
    auto libpkgDeleter = deleted_unique_ptr<void>(reinterpret_cast<void*>(0xDEADC0DE), [](void* p) { pkg_shutdown(); });

    int ret = pkgdb_access(PKGDB_MODE_WRITE|PKGDB_MODE_CREATE,
                PKGDB_DB_REPO);
    if (ret == EPKG_ENOACCESS) {
        g_error("Insufficient privileges to update the repository catalogue.");
    } else if (ret != EPKG_OK)
        g_error("Error");

    if (pkgdb_access(PKGDB_MODE_READ|PKGDB_MODE_WRITE|PKGDB_MODE_CREATE, PKGDB_DB_REPO) == EPKG_ENOACCESS) {
        g_error("Can't access package DB");
    }

    if (pkg_repos_total_count() == 0) {
        g_warning("No active remote repositories configured");
    }

    // TODO: basic progress reporting

    struct pkg_repo *r = NULL;
    while (pkg_repos(&r) == EPKG_OK) {
        if (!pkg_repo_enabled(r))
                continue;
        pkg_update (r, force);
    }

    pk_backend_job_finished (job);
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    guint size = g_strv_length (packages);
    std::vector<gchar*> names;
    names.reserve(size);
    for (guint i = 0; i < size; i++) {
        gchar* pkg = packages[i];

        if (pk_package_id_check(pkg) == false) {
            // if pkgid isn't a valid package ID, treat it as regexp suitable for pk_freebsd_search
            names.push_back(pkg);
            // TODO: maybe search by glob there?
        }
        else {
            // TODO: deduplicate this with similar code in pk_packend_download()
            gchar** package_id_parts = pk_package_id_split (pkg);
            gchar* package_namever = g_strconcat(package_id_parts[PK_PACKAGE_ID_NAME], "-", package_id_parts[PK_PACKAGE_ID_VERSION], NULL);
            names.push_back(package_namever);
            g_strfreev(package_id_parts);
            // TODO: we leak package_namever here
        }
    }
    pk_freebsd_search (job, filters, names.data());
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job,
                            PkBitfield transaction_flags,
                            gchar **package_ids,
                            gboolean allow_deps,
                            gboolean autoremove)
{
    g_error("pk_backend_remove_packages not implemented yet");
}

void
pk_backend_search_details (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_files (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_groups (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search(job, filters, values);
}

void
pk_backend_search_names (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_freebsd_search (job, filters, values);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
    g_error("pk_backend_update_packages not implemented yet");
}

void
pk_backend_get_repo_list (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    struct pkg_repo* repo = NULL;
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    g_assert(!pkg_initialized());
    if (pkg_ini(NULL, NULL, PKG_INIT_FLAG_USE_IPV4) != EPKG_OK) {
        g_error("pkg_ini failure");
        goto exit;
    }

    while (pkg_repos(&repo) == EPKG_OK) {
        const gchar* id = pkg_repo_name (repo);
        const gchar* descr = pkg_repo_url (repo);
        gboolean enabled = pkg_repo_enabled (repo);

        pk_backend_job_repo_detail (job, id, descr, enabled);
    }

    pkg_shutdown();
exit:
    pk_backend_job_finished (job);
}

void
pk_backend_repo_set_data (PkBackend *backend, PkBackendJob *job, const gchar *rid, const gchar *parameter, const gchar *value)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    g_warning ("REPO '%s' PARAMETER '%s' TO '%s'", rid, parameter, value);
    pk_backend_job_finished (job);
    g_error("pk_backend_repo_set_data not implemented yet");
}

void
pk_backend_what_provides (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **values)
{
    PkBackendFreeBSDJobData *job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));
    priv->values = values;
    //job_data->signal_timeout = g_timeout_add (200, pk_backend_what_provides_timeout, job);
    priv->filters = filters;
    pk_backend_job_set_status (job, PK_STATUS_ENUM_REQUEST);
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_percentage (job, 0);
    g_error("pk_backend_what_provides not implemented yet");
}

void
pk_backend_get_packages (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
    gchar* values[2] = { (gchar*)(".*"), NULL };
    pk_freebsd_search (job, filters, values);
}

void
pk_backend_download_packages (PkBackend *backend, PkBackendJob *job, gchar **package_ids, const gchar *directory)
{
    pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);

    withRepo(PKGDB_LOCK_READONLY, job, [package_ids, directory, job](struct pkgdb* db) {
        struct pkg_jobs *jobs = NULL;
        pkg_flags pkg_flags = PKG_FLAG_NONE;
        const gchar *cache_dir = "/var/cache/pkg"; // TODO: query this from libpkg
        const gchar *package_id;
        gchar** package_id_parts;
        gchar* package_namever;
        uint i, size;
        // This is required to convince libpkg to download into an arbitrary directory
        if (directory != NULL)
            pkg_flags = PKG_FLAG_FETCH_MIRROR;

        size = g_strv_length (package_ids);
        for (i = 0; i < size; i++) {
            bool need_break = 0;
            const gchar* file, *filename;
            gchar* files[] = {NULL, NULL};

            if (pkg_jobs_new(&jobs, PKG_JOBS_FETCH, db) != EPKG_OK) {
                g_error("pkg_jobs_new failed");
                return;
            }

            // TODO: set reponame when libpkg start reporting it
            // if (reponame != NULL && pkg_jobs_set_repository(jobs, reponame) != EPKG_OK)
            // 	goto cleanup;

            if (directory != NULL && pkg_jobs_set_destdir(jobs, directory) != EPKG_OK) {
                g_error("pkg_jobs_set_destdir failed for %s", directory);
                goto exit4;
            }

            pkg_jobs_set_flags(jobs, pkg_flags);

            package_id = package_ids[i];
            package_id_parts = pk_package_id_split (package_id);
            package_namever = g_strconcat(package_id_parts[PK_PACKAGE_ID_NAME], "-", package_id_parts[PK_PACKAGE_ID_VERSION], NULL);

            if (pkg_jobs_add(jobs, MATCH_GLOB, &package_namever, 1) != EPKG_OK) {
                g_error("pkg_jobs_add failed for %s", package_id);
                need_break = 1;
                goto exit4;
            }

            if (pkg_jobs_solve(jobs) != EPKG_OK) {
                g_error("pkg_jobs_solve failed");
                need_break = 1;
                goto exit4;
            }

            if (pkg_jobs_count(jobs) == 0)
                goto exit4;

            if (pkg_jobs_apply(jobs) != EPKG_OK) {
                g_error("pkg_jobs_apply failed");
                need_break = 1;
                goto exit4;
            }

            filename = g_strconcat(package_namever, ".pkg", NULL);
            file = directory == NULL
                ? g_build_path("/", cache_dir, filename, NULL)
                : g_build_path("/", directory, "All", filename, NULL);

            files[0] = (gchar*)file;
            pk_backend_job_files(job, package_id, files);

            pkg_jobs_free(jobs);

            g_free((gchar*)filename);
            g_free((gchar*)file);
    exit4:
            g_free(package_namever);
            g_strfreev(package_id_parts);
            if (need_break)
                break;
        }
    });

    pk_backend_job_finished (job);
}

void
pk_backend_upgrade_system (PkBackend *backend,
                           PkBackendJob *job,
                           PkBitfield transaction_flags,
                           const gchar *distro_id,
                           PkUpgradeKindEnum upgrade_kind)
{
    pk_backend_job_finished (job);
    g_error("pk_backend_upgrade_system not implemented yet");
}

void
pk_backend_repair_system (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags)
{
    pk_backend_job_finished (job);
    g_error("pk_backend_repair_system not implemented yet");
}

void
pk_backend_start_job (PkBackend *backend, PkBackendJob *job)
{
    PkBackendFreeBSDJobData *job_data;

    /* create private state for this job */
    job_data = g_new0 (PkBackendFreeBSDJobData, 1);
    job_data->progress_percentage = 0;
    job_data->cancellable = g_cancellable_new ();

    /* you can use pk_backend_job_error_code() here too */
    pk_backend_job_set_user_data (job, job_data);
}

void
pk_backend_stop_job (PkBackend *backend, PkBackendJob *job)
{
    PkBackendFreeBSDJobData *job_data = reinterpret_cast<PkBackendFreeBSDJobData*> (pk_backend_job_get_user_data (job));

    /* you *cannot* use pk_backend_job_error_code() here,
     * unless pk_backend_get_is_error_set() returns FALSE, and
     * even then it's probably just best to clean up silently */

    /* you cannot do pk_backend_job_finished() here as well as this is
     * needed to fire the job_stop() vfunc */
    g_object_unref (job_data->cancellable);

    /* destroy state for this job */
    g_free (job_data);
}

gboolean
pk_backend_supports_parallelization (PkBackend *backend)
{
    return FALSE;
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
    return "FreeBSD";
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
    return "Gleb Popov <arrowd@FreeBSD.org>";
}

static void
pk_freebsd_search(PkBackendJob *job, PkBitfield filters, gchar **values)
{
    pk_backend_job_set_allow_cancel (job, TRUE);
    pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

    //g_error(pk_filter_bitfield_to_string(filters));

    // TODO: what can we possible get in filters?
    // We ignore ~installed as there is no support in libpkg
    if (! (filters == 0 || pk_bitfield_contain(filters, PK_FILTER_ENUM_NOT_INSTALLED))) {
        g_error(pk_filter_bitfield_to_string(filters));
    }

    withRepo(PKGDB_LOCK_READONLY, job, [values, job](struct pkgdb* db) {
        struct pkgdb_it* it = NULL;
        struct pkg *pkg = NULL;
        pkgdb_field searched_field = FIELD_NAMEVER;

        switch (pk_backend_job_get_role (job))
        {
        case PK_ROLE_ENUM_SEARCH_DETAILS:
            // TODO: can we search both?
            searched_field = FIELD_COMMENT;
            break;
        case PK_ROLE_ENUM_SEARCH_GROUP:
            // TODO: this is the best approximation for non-existing FIELD_CATEGORIES
            searched_field = FIELD_ORIGIN;
            break;
        case PK_ROLE_ENUM_SEARCH_FILE:
            // TODO: we don't support searching for packages that provide given file
            return;
        default: break;
        }
        // TODO: take filters and all values into account
        it = pkgdb_repo_search (db, values[0], MATCH_REGEX, searched_field, FIELD_NAMEVER, NULL);

        while (pkgdb_it_next (it, &pkg, PKG_LOAD_BASIC) == EPKG_OK) {
            gchar* pk_id;
            struct pkg_el* name_el, *version_el,
                // TODO: arch or abi?
                *arch_el, *reponame_el, *comment_el;

            // TODO: libpkg reports this incorrectly
            PkInfoEnum pk_type = pkg_type (pkg) == PKG_INSTALLED
                                ? PK_INFO_ENUM_INSTALLED
                                : PK_INFO_ENUM_AVAILABLE;

            name_el = pkg_get_element(pkg, PKG_NAME);
            version_el = pkg_get_element(pkg, PKG_VERSION);
            arch_el = pkg_get_element(pkg, (pkg_attr)XXX_PKG_ARCH);
            reponame_el = pkg_get_element(pkg, PKG_REPONAME);
            pk_id = pk_package_id_build(name_el->string, version_el->string,
                                        arch_el->string, reponame_el->string);

            comment_el = pkg_get_element(pkg, PKG_COMMENT);
            pk_backend_job_package (job, pk_type, pk_id, comment_el->string);

            g_free(pk_id);

            if (pk_backend_job_is_cancelled (job))
                break;
        }
    });

    pk_backend_job_finished(job);
}
