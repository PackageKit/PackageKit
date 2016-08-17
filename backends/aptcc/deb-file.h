/* deb-file.h
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

#ifndef DEB_FILE_H
#define DEB_FILE_H

#include <apt-pkg/debfile.h>

using std::string;

class DebFile
{
    //     typedef int user_tag_reference;
public:
    DebFile(const string &filename);
    virtual ~DebFile();
    bool isValid() const;

    string packageName() const;
    string sourcePackage() const;
    string version() const;
    string architecture() const;
    string summary() const;
    string description() const;
    string conflicts() const;
    std::vector<std::string> files() const;

    // THIS should be moved to AptIntf class
    bool check();
    string errorMsg() const;

private:
    debDebFile::MemControlExtract *m_extractor;
    pkgTagSection m_controlData;
    string m_errorMsg;
    std::vector<std::string> m_files;
    bool m_isValid = false;
};

#endif
