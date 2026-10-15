/*
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2004 Michael Vogt <mvo@debian.org>
 *               2009-2018 Daniel Nicoletti <dantti12@gmail.com>
 *               2012-2026 Matthias Klumpp <matthias@tenstral.net>
 *               2016 Harald Sitter <sitter@kde.org>
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

#include "dpkg-progress.h"

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "apt-job.h"
#include "apt-utils.h"

using std::string;

DpkgProgress::DpkgProgress(AptJob &job, pid_t childPid)
    : m_job(job),
      m_childPid(childPid),
      m_lastTermAction(time(nullptr)),
      m_lastSubProgress(0),
      m_startCounting(false),
      m_terminalTimeout(120)
{
}

void DpkgProgress::handleStatusLine(const std::string &line, int writeFd, bool *errorEmitted)
{
    g_auto(GStrv) split = g_strsplit(line.c_str(), ":", 5);
    const gchar *status = g_strstrip(split[0]);
    const gchar *pkg = g_strstrip(split[1]);
    const gchar *percent = g_strstrip(split[2]);
    const std::string str = g_strstrip(split[3]);

    // major problem here, we got unexpected input. should _never_ happen
    if (pkg == nullptr && status == nullptr)
        return;

    // emit finished here if we have a kast package, as at this point dpkg
    // should be done with the previous package
    if (!m_lastPackage.empty() && m_lastPackage.compare(pkg) != 0) {
        const pkgCache::VerIterator &ver = m_job.findTransactionPackage(m_lastPackage);
        if (!ver.end()) {
            m_job.emitPackageProgress(ver, PK_STATUS_ENUM_FINISHED, 100);
        }
        m_lastSubProgress = 0;
    }

    // first check for errors and conf-file prompts
    if (strstr(status, "pmerror") != nullptr) {
        // error from dpkg
        pk_backend_job_error_code(
            m_job.pkJob(),
            PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL,
            "Error while installing package: %s",
            str.c_str());
        if (errorEmitted != nullptr)
            *errorEmitted = true;
    } else if (strstr(status, "pmconffile") != nullptr) {
        // conffile-request from dpkg, needs to be parsed different
        size_t i = 0;
        string orig_file, new_file;

        // go to first ' and read until the end
        for (; i < str.size() && str[i] != '\''; i++)
            /* nothing */
            ;
        if (i >= str.size()) {
            g_warning("malformed pmconffile status line: %s", str.c_str());
            return;
        }
        i++;
        for (; i < str.size() && str[i] != '\''; i++)
            orig_file.append(1, str[i]);
        if (i >= str.size()) {
            g_warning("malformed pmconffile status line: %s", str.c_str());
            return;
        }
        i++;

        // same for second ' and read until the end
        for (; i < str.size() && str[i] != '\''; i++)
            /* nothing */
            ;
        if (i >= str.size()) {
            g_warning("malformed pmconffile status line: %s", str.c_str());
            return;
        }
        i++;
        for (; i < str.size() && str[i] != '\''; i++)
            new_file.append(1, str[i]);
        i++;

        g_auto(GStrv) envp = nullptr;
        g_auto(GStrv) argv = (gchar **)g_malloc(5 * sizeof(gchar *));
        argv[0] = g_build_filename(DATADIR, "packagekit", "helpers", "apt", "pkconffile", nullptr);
        argv[1] = g_strdup(m_lastPackage.c_str());
        argv[2] = g_strdup(orig_file.c_str());
        argv[3] = g_strdup(new_file.c_str());
        argv[4] = nullptr;

        const gchar *socket = pk_backend_job_get_frontend_socket(m_job.pkJob());
        if (pk_backend_job_get_interactive(m_job.pkJob()) && (socket != nullptr)) {
            envp = (gchar **)g_malloc(3 * sizeof(gchar *));
            envp[0] = g_strdup("DEBIAN_FRONTEND=passthrough");
            envp[1] = g_strdup_printf("DEBCONF_PIPE=%s", socket);
            envp[2] = nullptr;
        } else {
            // we don't have a socket set or are non-interactive. Use the noninteractive frontend.
            envp = (gchar **)g_malloc(2 * sizeof(gchar *));
            envp[0] = g_strdup("DEBIAN_FRONTEND=noninteractive");
            envp[1] = nullptr;
        }

        gboolean ret;
        gint exitStatus;
        g_autoptr(GError) error = nullptr;
        ret = g_spawn_sync(
            nullptr, // working dir
            argv,    // argv
            envp,    // envp
            G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
            nullptr, // child_setup
            nullptr, // user_data
            nullptr, // standard_output
            nullptr, // standard_error
            &exitStatus,
            &error);

        int exit_code = WEXITSTATUS(exitStatus);
        g_debug("Conf-file helper %s exited with %d (spawn ret: %d)", argv[0], exit_code, ret);

        if (exit_code == 10) {
            // 1 means the user wants the package config
            if (write(writeFd, "Y\n", 2) != 2) {
                // TODO we need a DPKG patch to use debconf
                g_debug("Failed to write");
            }
        } else if (exit_code == 20) {
            // 2 means the user wants to keep the current config
            if (write(writeFd, "N\n", 2) != 2) {
                // TODO we need a DPKG patch to use debconf
                g_debug("Failed to write");
            }
        } else {
            // either the user didn't choose an option or the front end failed.
            // Fall back to keep the current config file.
            if (write(writeFd, "N\n", 2) != 2) {
                // TODO we need a DPKG patch to use debconf
                g_debug("Failed to write");
            }
        }
    } else if (strstr(status, "pmstatus") != nullptr) {
        // INSTALL & UPDATE
        // - Running dpkg
        // loops ALL
        // -  0 Installing pkg (sometimes this is skiped)
        // - 25 Preparing pkg
        // - 50 Unpacking pkg
        // - 75 Preparing to configure pkg
        //   ** Some pkgs have
        //   - Running post-installation
        //   - Running dpkg
        // reloops all
        // -   0 Configuring pkg
        // - +25 Configuring pkg (SOMETIMES)
        // - 100 Installed pkg
        // after all
        // - Running post-installation

        // REMOVE
        // - Running dpkg
        // loops
        // - 25  Removing pkg
        // - 50  Preparing for removal of pkg
        // - 75  Removing pkg
        // - 100 Removed pkg
        // after all
        // - Running post-installation

        // Let's start parsing the status:
        if (starts_with(str, "Preparing to configure")) {
            // Preparing to Install/configure
            // The next item might be Configuring so better it be 100
            m_lastSubProgress = 100;
            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_PREPARING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_SETUP, 75);
            }
        } else if (starts_with(str, "Preparing for removal")) {
            // Preparing to Install/configure
            m_lastSubProgress = 50;
            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_REMOVING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_SETUP, m_lastSubProgress);
            }
        } else if (starts_with(str, "Preparing")) {
            // Preparing to Install/configure
            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_PREPARING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_SETUP, 25);
            }
        } else if (starts_with(str, "Unpacking")) {
            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_INSTALLING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_INSTALL, 50);
            }
        } else if (starts_with(str, "Configuring")) {
            // Installing Package
            if (m_lastSubProgress >= 100 && !m_lastPackage.empty()) {
                const pkgCache::VerIterator &ver = m_job.findTransactionPackage(m_lastPackage);
                if (!ver.end()) {
                    m_job.emitPackageProgress(ver, PK_STATUS_ENUM_FINISHED, 100);
                }
                m_lastSubProgress = 0;
            }

            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_INSTALLING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_INSTALL, m_lastSubProgress);
            }
            m_lastSubProgress += 25;
        } else if (starts_with(str, "Running dpkg")) {
        } else if (starts_with(str, "Running")) {
            pk_backend_job_set_status(m_job.pkJob(), PK_STATUS_ENUM_COMMIT);
        } else if (starts_with(str, "Installing")) {
            // FINISH the last package
            if (!m_lastPackage.empty()) {
                const pkgCache::VerIterator &ver = m_job.findTransactionPackage(m_lastPackage);
                if (!ver.end()) {
                    m_job.emitPackageProgress(ver, PK_STATUS_ENUM_FINISHED, 100);
                }
            }
            m_lastSubProgress = 0;
            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_INSTALLING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_INSTALL, m_lastSubProgress);
            }
        } else if (starts_with(str, "Removing")) {
            if (m_lastSubProgress >= 100 && !m_lastPackage.empty()) {
                const pkgCache::VerIterator &ver = m_job.findTransactionPackage(m_lastPackage);
                if (!ver.end()) {
                    m_job.emitPackageProgress(ver, PK_STATUS_ENUM_FINISHED, 100);
                }
            }
            m_lastSubProgress += 25;

            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackage(ver, PK_INFO_ENUM_REMOVING);
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_REMOVE, m_lastSubProgress);
            }
        } else if (starts_with(str, "Installed") || starts_with(str, "Removed")) {
            m_lastSubProgress = 100;
            const pkgCache::VerIterator &ver = m_job.findTransactionPackage(pkg);
            if (!ver.end()) {
                m_job.emitPackageProgress(ver, PK_STATUS_ENUM_FINISHED, 100);
            }
        } else {
            g_warning("Unmaped dpkg status value: %s", line.c_str());
        }

        if (!starts_with(str, "Running")) {
            m_lastPackage = pkg;
        }
        m_startCounting = true;
    } else {
        m_startCounting = true;
    }

    int val = atoi(percent);
    pk_backend_job_set_percentage(m_job.pkJob(), val);
}

void DpkgProgress::update(int fd, int writeFd, bool *errorEmitted)
{
    char buf[2];
    std::string line;

    while (1) {
        // This algorithm should be improved (it's the same as the rpm one ;)
        int len = read(fd, buf, 1);

        // nothing was read
        if (len < 1)
            break;

        // update the time we last saw some action
        m_lastTermAction = time(nullptr);

        if (buf[0] == '\n') {
            if (m_job.cancelled())
                kill(m_childPid, SIGTERM);
            handleStatusLine(line, writeFd, errorEmitted);
            line.clear();
        } else {
            line.push_back(buf[0]);
        }
    }

    time_t now = time(nullptr);

    if (!m_startCounting) {
        usleep(100000);
        // wait until we get the first message from apt
        m_lastTermAction = now;
    }

    if ((now - m_lastTermAction) > m_terminalTimeout) {
        // get some debug info
        g_warning(
            "no statusfd changes/content updates in terminal for %i"
            " seconds",
            m_terminalTimeout);
        m_lastTermAction = time(nullptr);
    }

    // sleep for a while to not obsess over it
    usleep(5000);
}
