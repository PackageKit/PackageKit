/* deb-file.h
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

#ifndef DEB_FILE_H
#define DEB_FILE_H

#include <expected>
#include <string>
#include <utility>
#include <vector>

#include <pk-enum.h>

/**
 * The control data and file list of a local .deb file.
 */
class DebFile
{
public:
    /**
     * Read the control data and file list of the .deb at filename.
     * @returns the file's data, or a message saying why it could not be read.
     */
    static std::expected<DebFile, std::string> open(const std::string &filename);

    const std::string &packageName() const
    {
        return m_packageName;
    }
    const std::string &version() const
    {
        return m_version;
    }
    const std::string &architecture() const
    {
        return m_architecture;
    }
    /// Paths of the archive entries and what they are.
    const std::vector<std::pair<std::string, PkFileKindEnum>> &files() const
    {
        return m_files;
    }

private:
    DebFile() = default;

    std::string m_packageName;
    std::string m_version;
    std::string m_architecture;
    std::vector<std::pair<std::string, PkFileKindEnum>> m_files;
};

#endif
