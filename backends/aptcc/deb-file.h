/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include <string>
#include <apt-pkg/debfile.h>

class DebFile
{
//     typedef int user_tag_reference;
public:
    DebFile(const std::string &filename);
    bool isValid() const;

    std::string name() const;
    std::string version() const;
    std::string architecture() const;
    std::string conflicts() const;
    
    // THIS should be moved to AptCC class
    bool check();
    std::string errorMsg() const;

private:
    debDebFile::MemControlExtract *m_extractor;
    pkgTagSection m_controlData;
    std::string m_errorMsg;
    bool m_isValid;
};

#endif
