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
#include "apt-cache-file.h"
#include "apt-utils.h"

using std::string;

DpkgProgress::DpkgProgress(AptJob &job, pid_t childPid, const PkgList &packages)
    : m_job(job),
      m_childPid(childPid),
      m_lastTermAction(time(nullptr)),
      m_startCounting(false),
      m_terminalTimeout(120)
{
    for (const auto &package : packages) {
        const auto pkg = package.ver.ParentPkg();
        const bool removing = (*job.aptCacheFile())[pkg].Delete();
        m_packages.emplace(pkg->ID, PackageProgress{static_cast<double>(package.ver->InstalledSize), removing});
    }

    double installSize = 0;
    unsigned int installCount = 0;
    for (const auto &[id, package] : m_packages) {
        if (!package.removing) {
            installSize += package.share;
            installCount++;
        }
    }
    for (auto &[id, package] : m_packages) {
        if (package.removing || installSize <= 0)
            package.share = 1.0 / m_packages.size();
        else
            package.share = (static_cast<double>(installCount) / m_packages.size()) * package.share / installSize;
    }
}

void AptInstallProgress::Stop()
{
    PackageManagerProgressFd::Stop();
    WriteToStatusFd("pkpostinvoke:::Running post-installation hooks\n");
}

void DpkgProgress::startHooks()
{
    m_runningHooks = true;
    pk_backend_job_set_status(m_job.pkJob(), PK_STATUS_ENUM_RUN_HOOK);
    pk_backend_job_set_percentage(m_job.pkJob(), PK_BACKEND_PERCENTAGE_INVALID);
}

void DpkgProgress::emitProgress(const pkgCache::VerIterator &ver, PkStatusEnum status, unsigned int percentage)
{
    auto it = m_packages.find(ver.ParentPkg()->ID);
    if (it != m_packages.end()) {
        // dpkg can repeat earlier states while deconfiguring an old version.
        percentage = std::max(percentage, it->second.percentage);
        it->second.percentage = percentage;
    }
    m_job.emitPackageProgress(ver, status, percentage);
    if (it == m_packages.end() || m_runningHooks)
        return;

    double done = 0;
    for (const auto &[id, package] : m_packages)
        done += package.share * package.percentage;
    pk_backend_job_set_percentage(m_job.pkJob(), static_cast<unsigned int>(std::min(done + 1e-6, 100.0)));
}

void DpkgProgress::handleAptLine(const std::string &line, int writeFd, bool *errorEmitted)
{
    // pmstatus:<pkg>:<percent>:<translated message>; dpkg supplies package progress.
    g_auto(GStrv) split = g_strsplit(line.c_str(), ":", 4);
    if (g_strv_length(split) < 4)
        return;
    const gchar *status = g_strstrip(split[0]);
    const gchar *percent = g_strstrip(split[2]);
    const std::string str = g_strstrip(split[3]);

    if (strcmp(status, "pkpostinvoke") == 0) {
        startHooks();
        return;
    } else if (strstr(status, "pmerror") != nullptr) {
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
    }
    m_startCounting = true;

    if (m_packages.empty() && !m_runningHooks)
        pk_backend_job_set_percentage(m_job.pkJob(), atoi(percent));
}

void DpkgProgress::handleDpkgLine(const std::string &line)
{
    // "processing: <stage>: <pkg>" or "status: <pkg>: <state>", see dpkg(1) --status-fd.
    // Package names may be arch-qualified, so only split at ": ".
    g_auto(GStrv) split = g_strsplit(line.c_str(), ": ", 3);
    if (g_strv_length(split) < 3)
        return;
    const std::string kind = g_strstrip(split[0]);
    const std::string field1 = g_strstrip(split[1]);
    const std::string field2 = g_strstrip(split[2]);

    if (kind == "processing") {
        m_stage = field1;
        m_lastPackage = field2;
        if (m_stage == "trigproc") {
            startHooks();
            return;
        }

        m_runningHooks = false;

        const pkgCache::VerIterator &ver = m_job.findTransactionPackage(m_lastPackage);
        if (ver.end())
            return;
        m_started.insert(ver.ParentPkg()->ID);
        if (m_stage == "install" || m_stage == "upgrade") {
            m_unpacking.erase(ver.ParentPkg()->ID);
            m_job.emitPackage(ver, PK_INFO_ENUM_PREPARING);
            emitProgress(ver, PK_STATUS_ENUM_SETUP, 0);
        } else if (m_stage == "remove" || m_stage == "purge" || m_stage == "disappear") {
            m_job.emitPackage(ver, PK_INFO_ENUM_REMOVING);
            emitProgress(ver, PK_STATUS_ENUM_REMOVE, 0);
        } else if (m_stage == "configure") {
            const auto &state = (*m_job.aptCacheFile())[ver.ParentPkg()];
            m_job.emitPackage(
                ver,
                state.Upgrade() || state.Downgrade() ? PK_INFO_ENUM_UPDATING : PK_INFO_ENUM_INSTALLING);
        }
        return;
    }

    // errors and conffile prompts are handled via APT's pmerror and pmconffile
    if (kind != "status")
        return;
    const pkgCache::VerIterator &ver = m_job.findTransactionPackage(field1);
    if (ver.end())
        return;
    // dpkg also reports the initial state before it starts processing a package.
    if (!m_started.contains(ver.ParentPkg()->ID))
        return;
    const std::string &state = field2;
    const bool removing = m_stage == "remove" || m_stage == "purge" || m_stage == "disappear";

    if (state == "half-installed") {
        if (removing) {
            emitProgress(ver, PK_STATUS_ENUM_REMOVE, 50);
        } else {
            m_unpacking.insert(ver.ParentPkg()->ID);
            m_job.emitPackage(ver, m_stage == "upgrade" ? PK_INFO_ENUM_UPDATING : PK_INFO_ENUM_INSTALLING);
            emitProgress(ver, PK_STATUS_ENUM_INSTALL, 25);
        }
    } else if (state == "unpacked" && !removing && m_unpacking.contains(ver.ParentPkg()->ID)) {
        emitProgress(ver, PK_STATUS_ENUM_INSTALL, 50);
    } else if (state == "half-configured") {
        if (removing)
            emitProgress(ver, PK_STATUS_ENUM_REMOVE, 25);
        else if (m_stage == "configure")
            emitProgress(ver, PK_STATUS_ENUM_INSTALL, 75);
    } else if (
        state == "installed" || state == "triggers-pending" || state == "triggers-awaited" || state == "config-files"
        || state == "not-installed") {
        if (state == "config-files" && removing && (*m_job.aptCacheFile())[ver.ParentPkg()].Purge())
            emitProgress(ver, PK_STATUS_ENUM_REMOVE, 75);
        else
            emitProgress(ver, PK_STATUS_ENUM_FINISHED, 100);
    }
}

void DpkgProgress::readLines(int fd, std::string &buffer, const std::function<void(const std::string &)> &handler)
{
    char buf[1024];
    while (true) {
        const ssize_t len = read(fd, buf, sizeof(buf));
        if (len < 1)
            break;

        // update the time we last saw some action
        m_lastTermAction = time(nullptr);

        buffer.append(buf, len);
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            if (m_job.cancelled())
                kill(m_childPid, SIGTERM);
            handler(buffer.substr(0, pos));
            buffer.erase(0, pos + 1);
        }
    }
}

void DpkgProgress::update(int aptFd, int dpkgFd, int writeFd, bool *errorEmitted)
{
    // read dpkg first, so we know the current package before APT forwards a conffile prompt for it
    readLines(dpkgFd, m_dpkgBuffer, [this](const std::string &line) {
        handleDpkgLine(line);
    });
    readLines(aptFd, m_aptBuffer, [this, writeFd, errorEmitted](const std::string &line) {
        handleAptLine(line, writeFd, errorEmitted);
    });

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
