/*
 * Copyright (c) 2009-2018 Daniel Nicoletti <dantti12@gmail.com>
 *               2012-2026 Matthias Klumpp <matthias@tenstral.net>
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

#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <sys/types.h>
#include <time.h>

#include <apt-pkg/install-progress.h>
#include <pk-backend.h>
#include "pkg-list.h"

class AptJob;

/**
 * Turns dpkg's --status-fd stream into package and size-weighted overall progress.
 * APT's progress stream supplies errors, conffile prompts and a percentage fallback.
 */
class DpkgProgress
{
public:
    DpkgProgress(AptJob &job, pid_t childPid, const PkgList &packages);

    /**
     * Handle everything currently readable from the status fds.
     * @param aptFd the read end of APT's progress fd
     * @param dpkgFd the read end of dpkg's own status fd
     * @param writeFd the pty of the child, used to answer conffile prompts
     * @param errorEmitted set to true if an error was reported to the job
     */
    void update(int aptFd, int dpkgFd, int writeFd, bool *errorEmitted = nullptr);

private:
    void handleAptLine(const std::string &line, int writeFd, bool *errorEmitted);
    void handleDpkgLine(const std::string &line);
    void readLines(int fd, std::string &buffer, const std::function<void(const std::string &)> &handler);
    void emitProgress(const pkgCache::VerIterator &ver, PkStatusEnum status, unsigned int percentage);
    void startHooks();

    struct PackageProgress {
        double share;
        bool removing;
        unsigned int percentage = 0;
    };
    std::map<unsigned int, PackageProgress> m_packages;
    std::set<unsigned int> m_started;
    std::set<unsigned int> m_unpacking;
    bool m_runningHooks = false;

    AptJob &m_job;
    pid_t m_childPid;

    time_t m_lastTermAction;
    std::string m_aptBuffer;
    std::string m_dpkgBuffer;
    // the dpkg processing stage and package we are currently at
    std::string m_stage;
    std::string m_lastPackage;
    bool m_startCounting;
    // when the internal terminal timesout after no activity
    int m_terminalTimeout;
};

/** Relay the start of APT's post-invoke hooks from the child to the job thread. */
class AptInstallProgress : public APT::Progress::PackageManagerProgressFd
{
public:
    explicit AptInstallProgress(int fd)
        : PackageManagerProgressFd(fd)
    {
    }
    void Stop() override;
};
