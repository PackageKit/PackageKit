/* deb-file.cpp
 *
 * Copyright (c) 2011-2016 Daniel Nicoletti <dantti12@gmail.com>
 *               2012 Matthias Klumpp <matthias@tenstral.net>
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

#include "deb-file.h"

#include <glib.h>
#include <apt-pkg/init.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/fileutl.h>
#include <iostream>

class GetFilesStream : public pkgDirStream
{
public:
    std::vector<std::string> files;

    virtual bool DoItem(Item &Itm,int &Fd) override {
        Fd = -1;
        files.push_back(Itm.Name);
        return true;
    }
};

DebFile::DebFile(const string &filename)
{
    FileFd in(filename, FileFd::ReadOnly);
    debDebFile deb(in);

    // Extract control data
    m_extractor = new debDebFile::MemControlExtract("control");
    if (!m_extractor->Read(deb)) {
        return;
    }

    if(!m_controlData.Scan(m_extractor->Control,m_extractor->Length+2)) {
        g_warning("DebFile: Scan failed.");
        return;
    }

    GetFilesStream stream;
    if (deb.ExtractArchive(stream) == false) {
        return;
    }
    m_files = stream.files;

    m_isValid = true;
}

DebFile::~DebFile()
{
    delete m_extractor;
}

std::vector<std::string> DebFile::files() const
{
    return m_files;
}

bool DebFile::isValid() const
{
    return m_isValid;
}

string DebFile::packageName() const
{
    return m_controlData.FindS("Package");
}

string DebFile::sourcePackage() const
{
    return m_controlData.FindS("Source");
}

string DebFile::version() const
{
    return m_controlData.FindS("Version");
}

string DebFile::architecture() const
{
    return m_controlData.FindS("Architecture");
}

string DebFile::conflicts() const
{
    return m_controlData.FindS("Conflicts");
}

string DebFile::summary() const
{
    string longDesc = description ();
    longDesc.resize (longDesc.find_first_of ("\n"));

    return longDesc;
}

string DebFile::description() const
{
    return m_controlData.FindS("Description");
}

bool DebFile::check()
{
    // check arch
    if (architecture().empty()) {
        m_errorMsg = "No Architecture field in the package";
        return false;
    }

    std::cout << architecture() << std::endl;
    if (architecture().compare("all") != 0 &&
            architecture().compare(_config->Find("APT::Architecture")) != 0) {
        m_errorMsg = "Wrong architecture ";
        m_errorMsg.append(architecture());
        return false;
    }
    //     if not "Architecture" in self._sections:
    //         self._dbg(1, "ERROR: no architecture field")
    //         self._failure_string = _("No Architecture field in the package")
    //         return False
    //     arch = self._sections["Architecture"]
    //     if  arch != "all" and arch != apt_pkg.config.find("APT::Architecture"):
    //         self._dbg(1, "ERROR: Wrong architecture dude!")
    //         self._failure_string = _("Wrong architecture '%s'") % arch
    //         return False
    //
    //     // check version
    //     if self.compare_to_version_in_cache() == self.VERSION_OUTDATED:
    //         if self._cache[self.pkgname].installed:
    //             // the deb is older than the installed
    //             self._failure_string = _("A later version is already installed")
    //             return False
    //
    //     // FIXME: this sort of error handling sux
    //     self._failure_string = ""
    //
    //     // check conflicts
    //     if not self.check_conflicts():
    //         return False
    //
    //     // check if installing it would break anything on the
    //     // current system
    //     if not self.check_breaks_existing_packages():
    //         return False
    //
    //     // try to satisfy the dependencies
    //     if not self._satisfy_depends(self.depends):
    //         return False
    //
    //     // check for conflicts again (this time with the packages that are
    //     // makeed for install)
    //     if not self.check_conflicts():
    //         return False
    //
    //     if self._cache._depcache.broken_count > 0:
    //         self._failure_string = _("Failed to satisfy all dependencies "
    //                                     "(broken cache)")
    //         // clean the cache again
    //         self._cache.clear()
    //         return False
    return true;
}

string DebFile::errorMsg() const
{
    return m_errorMsg;
}


