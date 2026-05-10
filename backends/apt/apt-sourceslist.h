/* apt-sourceslist.h - access the sources.list file
 *
 * Copyright (c) 1999 Patrick Cole <z@amused.net>
 *           (c) 2002 Synaptic development team
 *           (c) 2018-2026 Matthias Klumpp <matthias@tenstral.net>
 *
 * Author: Patrick Cole <z@amused.net>
 *         Michael Vogt <mvo@debian.org>
 *         Gustavo Niemeyer <niemeyer@conectiva.com>
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

#ifndef APT_SOURCESLIST_H
#define APT_SOURCESLIST_H

#include <apt-pkg/fileutl.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/metaindex.h>

#include <string>
#include <list>

class SourcesList
{
public:
    enum RecType {
        Deb = 1 << 0,
        DebSrc = 1 << 1,
        Disabled = 1 << 2,
        Comment = 1 << 3,
    };

    struct SourceRecord {
        unsigned int Type;
        std::string VendorID;
        std::string PrimaryURI;
        std::vector<std::string> URIs;
        std::string Dist;
        std::string *Sections;
        unsigned short NumSections;
        std::string Comment;

        std::string SourceFile;
        uint Deb822StanzaIdx;

        std::string joinedSections(const std::string &separator = " ") const;
        std::string niceName();
        std::string repoId();
        bool hasSection(const char *component) const;

        bool SetType(std::string);
        std::string GetType() const;
        bool SetURI(std::string);
        bool SetURIs(const std::vector<std::string> &newURIs);

        SourceRecord()
            : Type(0),
              Sections(0),
              NumSections(0),
              Deb822StanzaIdx(0)
        {
        }
        ~SourceRecord()
        {
            if (Sections) {
                delete[] Sections;
            }
        }
        SourceRecord &operator=(const SourceRecord &);
    };

    struct VendorRecord {
        std::string VendorID;
        std::string FingerPrint;
        std::string Description;
    };

    std::list<SourceRecord *> SourceRecords;
    std::list<VendorRecord *> VendorRecords;

private:
    SourceRecord *AddSourceNode(const SourceRecord &);
    VendorRecord *AddVendorNode(VendorRecord &);
    static bool OpenConfigurationFileFd(std::string const &File, FileFd &Fd);
    bool ParseDeb822Stanza(const char *Type, pkgTagSection &Tags, unsigned int const stanzaIdx, FileFd &Fd);
    bool UpdateSourceLegacy(const std::string &filename);
    bool UpdateSourceDeb822(const std::string &filename);

public:
    SourceRecord *AddSource(
        RecType Type,
        std::string VendorID,
        std::string URI,
        std::string Dist,
        std::string *Sections,
        unsigned short count,
        std::string SourceFile);
    SourceRecord *AddEmptySource();
    void RemoveSource(SourceRecord *&);
    void SwapSources(SourceRecord *&, SourceRecord *&);
    bool ReadSourceDeb822(const std::string &listpath);
    bool ReadSourceLegacy(const std::string &listpath);
    bool ReadSourcePart(std::string listpath);
    bool ReadSourceDir(std::string Dir);
    bool ReadSources();
    bool UpdateSources();

    VendorRecord *AddVendor(std::string VendorID, std::string FingerPrint, std::string Description);
    void RemoveVendor(VendorRecord *&);
    bool ReadVendors();
    bool UpdateVendors();

    SourcesList() {}
    ~SourcesList();
};

typedef std::list<SourcesList::SourceRecord *>::iterator SourcesListIter;

std::ostream &operator<<(std::ostream &, const SourcesList::SourceRecord &);
std::ostream &operator<<(std::ostream &os, const SourcesList::VendorRecord &rec);

#endif
