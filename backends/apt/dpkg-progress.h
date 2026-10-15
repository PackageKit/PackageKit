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

#include <string>
#include <sys/types.h>
#include <time.h>

class AptJob;

/**
 * Turns the dpkg --status-fd stream of dpkg into PackageKit package progress,
 * and answers its conffile prompts.
 */
class DpkgProgress
{
public:
    DpkgProgress(AptJob &job, pid_t childPid);

    /**
     * Handle everything currently readable from the status fd.
     * @param readFd the read end of dpkg's status fd
     * @param writeFd the pty of the child, used to answer conffile prompts
     * @param errorEmitted set to true if an error was reported to the job
     */
    void update(int readFd, int writeFd, bool *errorEmitted = nullptr);

private:
    void handleStatusLine(const std::string &line, int writeFd, bool *errorEmitted);

    AptJob &m_job;
    pid_t m_childPid;

    time_t m_lastTermAction;
    std::string m_lastPackage;
    uint m_lastSubProgress;
    bool m_startCounting;
    // when the internal terminal timesout after no activity
    int m_terminalTimeout;
};
