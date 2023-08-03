// stolen from pkg/src/event.c
// required as some libpkg internals calls out to pkg_emit_event and expect
// a handler ("event_callback" in this case) to call the callback, which does the
// actual work.

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/capsicum.h>
#include <pwd.h>
#include <unistd.h>

#include <glib.h>

#include <pkg.h>
#include <xstring.h>
#define HAVE_CAPSICUM

#include "stolen.h"

static int event_sandboxed_call(pkg_sandbox_cb func, int fd, void *ud);
static int event_sandboxed_get_string(pkg_sandbox_cb func, char **result, int64_t *len, void *ud);
static void drop_privileges(void);

static xstring *msg_buf = NULL;
static xstring *messages = NULL;
static xstring *conflicts = NULL;
static bool progress_debit = false;

int event_callback(void *data, struct pkg_event *ev)
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

static
int event_sandboxed_call(pkg_sandbox_cb func, int fd, void *ud)
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

static
int event_sandboxed_get_string(pkg_sandbox_cb func, char **result, int64_t *len,
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
		buf = malloc(BUFSIZ);
		if (buf == NULL) {
			g_warning("libpkg: malloc failed");
			return (EPKG_FATAL);
		}
		allocated_len = BUFSIZ;
		do {
			if (off >= allocated_len) {
				allocated_len *= 2;
				buf = realloc(buf, allocated_len);
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

static
void drop_privileges(void)
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
