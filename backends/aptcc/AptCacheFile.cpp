/*
 * Copyright (c) 2012 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2012 Matthias Klumpp <matthias@tenstral.net>
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
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "AptCacheFile.h"

AptCacheFile::AptCacheFile() :
    m_packageRecords(0)
{
}

AptCacheFile::~AptCacheFile()
{
    if (m_packageRecords) {
        delete m_packageRecords;
    }
}

bool AptCacheFile::open(bool withLock)
{
    return Open(NULL, withLock);
}

bool AptCacheFile::buildCaches(bool withLock)
{
    return BuildCaches(NULL, withLock);
}

void AptCacheFile::buildPkgRecords()
{
    if (m_packageRecords) {
        return;
    }

    // Create the text record parser
    m_packageRecords = new pkgRecords(*this->GetPkgCache());
}


