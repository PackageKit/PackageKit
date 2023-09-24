// stolen from https://github.com/freebsd/pkg/tree/master/src/event.c
// required as some libpkg internals calls out to pkg_emit_event and expect
// a handler ("event_callback" in this case) to call the callback, which does the
// actual work.
// this file will go away at some point

/*-
 * Copyright (c) 2011-2014 Baptiste Daroussin <bapt@FreeBSD.org>
 * Copyright (c) 2011-2012 Julien Laffaye <jlaffaye@FreeBSD.org>
 * Copyright (c) 2011 Will Andrews <will@FreeBSD.org>
 * Copyright (c) 2011-2012 Marin Atanasov Nikolov <dnaeon@gmail.com>
 * Copyright (c) 2014 Vsevolod Stakhov <vsevolod@FreeBSD.org>
 * Copyright (c) 2015 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#include "stolen.h"

static xstring *msg_buf = NULL;
static xstring *messages = NULL;
static xstring *conflicts = NULL;

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
	case PKG_EVENT_INCREMENTAL_UPDATE:
		g_message("libpkg: %s repository update completed. %d packages processed.\n",
			ev->e_incremental_update.reponame,
			ev->e_incremental_update.processed);
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
		return ( pkg_handle_sandboxed_call(ev->e_sandbox_call.call,
				ev->e_sandbox_call.fd,
				ev->e_sandbox_call.userdata) );
		break;
	case PKG_EVENT_SANDBOX_GET_STRING:
		return ( pkg_handle_sandboxed_get_string(ev->e_sandbox_call_str.call,
				ev->e_sandbox_call_str.result,
				ev->e_sandbox_call_str.len,
				ev->e_sandbox_call_str.userdata) );
		break;
	case PKG_EVENT_BACKUP:
		g_message("libpkg: Backing up");
		break;
	case PKG_EVENT_RESTORE:
		g_message("libpkg: Restoring");
		break;
	case PKG_EVENT_MESSAGE:
		if (messages == NULL)
			messages = xstring_new();
		fprintf(messages->fp, "%s", ev->e_pkg_message.msg);
		break;
	case PKG_EVENT_CONFLICTS:
		if (conflicts == NULL) {
			conflicts = xstring_new();
		}
		pkg_fprintf(conflicts->fp, "  - %n-%v",
		    ev->e_conflicts.p1, ev->e_conflicts.p1);
		if (pkg_repos_total_count() > 1) {
			pkg_get(ev->e_conflicts.p1, PKG_ATTR_REPONAME, &reponame);
			fprintf(conflicts->fp, " [%s]",
			    reponame == NULL ? "installed" : reponame);
		}
		pkg_fprintf(conflicts->fp, " conflicts with %n-%v",
		    ev->e_conflicts.p2, ev->e_conflicts.p2);
		if (pkg_repos_total_count() > 1) {
			pkg_get(ev->e_conflicts.p2, PKG_ATTR_REPONAME, &reponame);
			fprintf(conflicts->fp, " [%s]",
			    reponame == NULL ? "installed" : reponame);
		}
		fprintf(conflicts->fp, " on %s\n",
		    ev->e_conflicts.path);
		break;
	case PKG_EVENT_TRIGGER:
		if (ev->e_trigger.cleanup)
			g_message("libpkg: ==> Cleaning up trigger: %s\n", ev->e_trigger.name);
		else
			g_message("libpkg: ==> Running trigger: %s\n", ev->e_trigger.name);
		break;
	default:
		break;
	}

	return 0;
}
