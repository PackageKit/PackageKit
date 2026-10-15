/* deb-file.cpp
 *
 * Copyright (c) 2011-2016 Daniel Nicoletti <dantti12@gmail.com>
 *               2012 Matthias Klumpp <matthias@tenstral.net>
 *
 * Licensed under the GNU General Public License Version 2
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

#include "deb-file.h"

#include <glib.h>
#include <apt-pkg/init.h>
#include <apt-pkg/configuration.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/debfile.h>
#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>

class GetFilesStream : public pkgDirStream
{
public:
    std::vector<std::pair<std::string, PkFileKindEnum>> files;

    virtual bool DoItem(Item &Itm, int &Fd) override
    {
        Fd = -1;
        PkFileKindEnum kind = PK_FILE_KIND_ENUM_UNKNOWN;
        switch (Itm.Type) {
        case Item::File:
        case Item::HardLink:
            kind = PK_FILE_KIND_ENUM_REGULAR;
            break;
        case Item::SymbolicLink:
            kind = PK_FILE_KIND_ENUM_SYMLINK;
            break;
        case Item::Directory:
            kind = PK_FILE_KIND_ENUM_DIRECTORY;
            break;
        default:
            break;
        }
        files.emplace_back(Itm.Name, kind);
        return true;
    }
};

/* The pending libapt error, if any, as a ": message" suffix. */
static std::string aptErrorSuffix()
{
    std::string msg;
    if (!_error->PopMessage(msg))
        return "";
    _error->Discard();
    return ": " + msg;
}

std::expected<DebFile, std::string> DebFile::open(const std::string &filename)
{
    FileFd in(filename, FileFd::ReadOnly);
    if (!in.IsOpen())
        return std::unexpected("Failed to open " + filename + aptErrorSuffix());
    debDebFile deb(in);

    debDebFile::MemControlExtract extractor("control");
    if (!extractor.Read(deb))
        return std::unexpected("Failed to read the control data of " + filename + aptErrorSuffix());

    pkgTagSection control;
    if (!control.Scan(extractor.Control, extractor.Length + 2))
        return std::unexpected("Failed to parse the control data of " + filename + aptErrorSuffix());

    GetFilesStream stream;
    if (!deb.ExtractArchive(stream))
        return std::unexpected("Failed to read the file list of " + filename + aptErrorSuffix());

    DebFile result;
    result.m_packageName = control.FindS("Package");
    result.m_version = control.FindS("Version");
    result.m_architecture = control.FindS("Architecture");
    result.m_files = std::move(stream.files);
    return result;
}
