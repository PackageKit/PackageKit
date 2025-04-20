/* apt-sourceslist.cpp - read & write APT repository sources
 *
 * Copyright (c) 1999 Patrick Cole <z@amused.net>
 *           (c) 2002 Synaptic development team
 *           (c) 2016 Daniel Nicoletti <dantti12@gmail.com>
 *           (c) 2018-2025 Matthias Klumpp <matthias@tenstral.net>
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

#include "apt-sourceslist.h"

#include <sys/stat.h>
#include <dirent.h>

#include <apt-pkg/configuration.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <fcntl.h>

#include "deb822.h"
#include "apt-utils.h"

#include "config.h"

static std::vector<std::string> FindMultiValue(pkgTagSection &Tags, char const *const Field)
{
   auto values = Tags.FindS(Field);
   // we ignore duplicate spaces by removing empty values
   std::replace_if(values.begin(), values.end(), isspace_ascii, ' ');
   auto vect = VectorizeString(values, ' ');
   vect.erase(std::remove_if(vect.begin(), vect.end(), [](std::string const &s) { return s.empty(); }), vect.end());
   return vect;
}

SourcesList::~SourcesList()
{
    for (SourceRecord *sr : SourceRecords) {
        delete sr;
    }

    for (VendorRecord *vr : VendorRecords) {
        delete vr;
    }
}

SourcesList::SourceRecord *SourcesList::AddSourceNode(SourceRecord &rec)
{
    SourceRecord *newrec = new SourceRecord;
    *newrec = rec;
    SourceRecords.push_back(newrec);

    return newrec;
}

bool SourcesList::OpenConfigurationFileFd(std::string const &File, FileFd &Fd) /*{{{*/
{
   int const fd = open(File.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY);
   if (fd == -1)
      return _error->WarningE("open", "Unable to read %s", File.c_str());
   APT::Configuration::Compressor none(".", "", "", nullptr, nullptr, 0);
   if (Fd.OpenDescriptor(fd, FileFd::ReadOnly, none, true) == false)
      return false;
   Fd.SetFileName(File);
   return true;
}

bool SourcesList::ParseDeb822Stanza(const char *Type,
                              pkgTagSection &Tags,
                              unsigned int const stanzaIdx,
                              FileFd &Fd)
{
    string Enabled = Tags.FindS("Enabled");

    // now create one item per suite/section
    auto const list_uris = FindMultiValue(Tags, "URIs");
    auto const list_comp = FindMultiValue(Tags, "Components");
    auto list_suite = FindMultiValue(Tags, "Suites");

    {
        auto const nativeArch = _config->Find("APT::Architecture");
        std::transform(list_suite.begin(), list_suite.end(), list_suite.begin(),
                       [&](std::string const &suite) { return SubstVar(suite, "$(ARCH)", nativeArch); });
    }

    if (list_uris.empty())
        return _error->Error("Malformed entry %u in %s file %s (%s)", stanzaIdx, "sources", Fd.Name().c_str(), "URI");

    if (list_suite.empty())
        return _error->Error("Malformed entry %u in %s file %s (%s)", stanzaIdx, "sources", Fd.Name().c_str(), "Suite");

    for (auto const &S : list_suite) {
        SourceRecord rec = SourceRecord ();
        rec.Deb822StanzaIdx = stanzaIdx;

        if (!rec.SetURIs(list_uris))
            return _error->Error("Malformed entry %u in %s file %s (%s)", stanzaIdx, "sources", Fd.Name().c_str(), "URI parse");

        if (S.empty() == false && S[S.size() - 1] == '/') {
            if (list_comp.empty() == false)
                return _error->Error("Malformed entry %u in %s file %s (%s)", stanzaIdx, "sources", Fd.Name().c_str(), "absolute Suite Component");

            rec.SourceFile = Fd.Name();
            if (!rec.SetType(Type)) {
                _error->Error("Unknown type %s", Type);
                return false;
            }

            if (Enabled.empty() == false && StringToBool(Enabled) == false)
                rec.Type |= SourcesList::Disabled;

            rec.Dist = S;
            rec.NumSections = 0;
            rec.Sections = nullptr;
            AddSourceNode(rec);
         } else {
            if (list_comp.empty())
                return _error->Error("Malformed entry %u in %s file %s (%s)", stanzaIdx, "sources", Fd.Name().c_str(), "Component");

            rec.SourceFile = Fd.Name();
            if (!rec.SetType(Type)) {
                _error->Error("Unknown type %s", Type);
                return false;
            }
            if (Enabled.empty() == false && StringToBool(Enabled) == false)
                rec.Type |= SourcesList::Disabled;

            rec.Dist = S;
            rec.NumSections = list_comp.size();
            rec.Sections = new string[rec.NumSections];
            std::copy(list_comp.begin(), list_comp.end(), rec.Sections);
            AddSourceNode(rec);
        }
    }

    return true;
}


bool SourcesList::ReadSourceDeb822(string listpath)
{
    FileFd Fd;
    if (OpenConfigurationFileFd(listpath, Fd) == false)
        return false;

    pkgTagFile Sources(&Fd, pkgTagFile::SUPPORT_COMMENTS);
    if (Fd.IsOpen() == false || Fd.Failed())
        return _error->Error("Malformed stanza %u in source list %s (type)", 0, listpath.c_str());

    // read step by step
    pkgTagSection Tags;
    for (guint i = 0; Sources.Step(Tags); i++) {
        if(Tags.Exists("Types") == false)
            return _error->Error("Malformed stanza %u in source list %s (type)", i, listpath.c_str());

        for (auto const &type : FindMultiValue(Tags, "Types")) {
            if (!ParseDeb822Stanza(type.c_str(), Tags, i, Fd))
                return false;
        }
    }

    return true;
}

bool SourcesList::ReadSourceLegacy(string listpath)
{
    char buf[512];
    const char *p;
    ifstream ifs(listpath.c_str(), ios::in);
    bool record_ok = true;

    // cannot open file
    if (!ifs != 0) {
        return _error->Error("Can't read %s", listpath.c_str());
    }

    while (ifs.eof() == false) {
        p = buf;
        SourceRecord rec;
        string Type;
        string Section;
        string VURI;

        ifs.getline(buf, sizeof(buf));

        rec.SourceFile = listpath;
        while (isspace(*p))
            p++;

        if (*p == '#') {
            rec.Type = Disabled;
            p++;
            while (isspace(*p))
                p++;
        }

        if (*p == '\r' || *p == '\n' || *p == 0) {
            rec.Type = Comment;
            rec.Comment = p;

            AddSourceNode(rec);
            continue;
        }

        bool Failed = true;
        if (ParseQuoteWord(p, Type) == true &&
                rec.SetType(Type) == true && ParseQuoteWord(p, VURI) == true) {
            if (VURI[0] == '[') {
                rec.VendorID = VURI.substr(1, VURI.length() - 2);
                if (ParseQuoteWord(p, VURI) == true && rec.SetURI(VURI) == true)
                    Failed = false;
            } else if (rec.SetURI(VURI) == true) {
                Failed = false;
            }
            if (Failed == false && ParseQuoteWord(p, rec.Dist) == false)
                Failed = true;
        }

        if (Failed == true) {
            if (rec.Type == Disabled) {
                // treat as a comment field
                rec.Type = Comment;
                rec.Comment = buf;
            } else {
                // syntax error on line
                rec.Type = Comment;
                string s = "#" + string(buf);
                rec.Comment = s;
                record_ok = false;
                //return _error->Error("Syntax error in line %s", buf);
            }
        }

        // check for absolute dist
        if (rec.Dist.empty() == false && rec.Dist[rec.Dist.size() - 1] == '/') {
            // make sure there's no section
            if (ParseQuoteWord(p, Section) == true)
                return _error->Error("Syntax error in line %s", buf);

            rec.Dist = SubstVar(rec.Dist, "$(ARCH)",
                                _config->Find("APT::Architecture"));

            AddSourceNode(rec);
            continue;
        }

        const char *tmp = p;
        rec.NumSections = 0;
        while (ParseQuoteWord(p, Section) == true)
            rec.NumSections++;
        if (rec.NumSections > 0) {
            p = tmp;
            rec.Sections = new string[rec.NumSections];
            rec.NumSections = 0;
            while (ParseQuoteWord(p, Section) == true) {
                // comments inside the record are preserved
                if (Section[0] == '#') {
                    SourceRecord rec;
                    string s = Section + string(p);
                    rec.Type = Comment;
                    rec.Comment = s;
                    rec.SourceFile = listpath;
                    AddSourceNode(rec);
                    break;
                } else {
                    rec.Sections[rec.NumSections++] = Section;
                }
            }
        }
        AddSourceNode(rec);
    }

    ifs.close();
    return record_ok;
}

bool SourcesList::ReadSourcePart(string listpath)
{
    if (g_str_has_suffix (listpath.c_str(), ".sources")) {
        return ReadSourceDeb822(listpath);
    } else {
        return ReadSourceLegacy(listpath);
    }
}

bool SourcesList::ReadSourceDir(string Dir)
{
    //cout << "SourcesList::ReadSourceDir() " << Dir  << endl;

    DIR *D = opendir(Dir.c_str());
    if (D == 0) {
        return _error->Errno("opendir", "Unable to read %s", Dir.c_str());
    }

    vector<string> List;
    for (struct dirent * Ent = readdir(D); Ent != 0; Ent = readdir(D)) {
        if (Ent->d_name[0] == '.') {
            continue;
        }

        // Skip bad file names ala run-parts
        const char *C = Ent->d_name;
        for (; *C != 0; C++) {
            if (isalpha(*C) == 0 && isdigit(*C) == 0
                    && *C != '_' && *C != '-' && *C != '.') {
                break;
            }
        }
        if (*C != 0) {
            continue;
        }

        // Only look at files ending in .list and .sources, skip .dpkg-new/.bak/.save etc.
        if (!g_str_has_suffix (Ent->d_name, ".list") &&
            !g_str_has_suffix (Ent->d_name, ".sources")) {
            continue;
        }

        // Make sure it is a file and not something else
        string File = flCombine(Dir, Ent->d_name);
        struct stat St;
        if (stat(File.c_str(), &St) != 0 || S_ISREG(St.st_mode) == 0) {
            continue;
        }
        List.push_back(File);

    }
    closedir(D);

    sort(List.begin(), List.end());

    // Read the files
    for (const string &sourcePart : List) {
        if (ReadSourcePart(sourcePart) == false) {
            return false;
        }
    }
    return true;
}


bool SourcesList::ReadSources()
{
    //cout << "SourcesList::ReadSources() " << endl;

    bool Res = true;

    string Parts = _config->FindDir("Dir::Etc::sourceparts");
    if (FileExists(Parts) == true) {
        Res &= ReadSourceDir(Parts);
    }

    string Main = _config->FindFile("Dir::Etc::sourcelist");
    if (FileExists(Main) == true) {
        Res &= ReadSourcePart(Main);
    }

    return Res;
}

SourcesList::SourceRecord *SourcesList::AddEmptySource()
{
    SourceRecord rec;
    rec.Type = Deb;
    rec.VendorID = "";
    rec.SourceFile = _config->FindFile("Dir::Etc::sourcelist");
    rec.Dist = "";
    rec.NumSections = 0;
    return AddSourceNode(rec);
}

SourcesList::SourceRecord *SourcesList::AddSource(RecType Type,
                                                  string VendorID, string URI,
                                                  string Dist,
                                                  string *Sections,
                                                  unsigned short count,
                                                  string SourceFile)
{
    SourceRecord rec;
    rec.Type = Type;
    rec.VendorID = VendorID;
    rec.SourceFile = SourceFile;

    if (rec.SetURI(URI) == false) {
        return NULL;
    }
    rec.Dist = Dist;
    rec.NumSections = count;
    rec.Sections = new string[count];
    for (unsigned int i = 0; i < count; ++i) {
        rec.Sections[i] = Sections[i];
    }

    return AddSourceNode(rec);
}

void SourcesList::RemoveSource(SourceRecord *&rec)
{
    SourceRecords.remove(rec);
    delete rec;
    rec = 0;
}

void SourcesList::SwapSources( SourceRecord *&rec_one, SourceRecord *&rec_two )
{
    list<SourceRecord *>::iterator rec_p;
    list<SourceRecord *>::iterator rec_n;

    rec_p = find( SourceRecords.begin(), SourceRecords.end(), rec_one );
    rec_n = find( SourceRecords.begin(), SourceRecords.end(), rec_two );

    SourceRecords.insert( rec_p, rec_two );
    SourceRecords.erase( rec_n );
}

bool SourcesList::UpdateSourceLegacy(const string &filename)
{
    if (std::filesystem::path(filename).extension().string() != ".list") {
        g_warning("Tried to update APT source file '%s' as legacy file, but filename has wrong extension.",
            filename.c_str());
        return false;
    }

    ofstream ofs(filename.c_str(), ios::out);
    if (!ofs != 0) {
        return false;
    }

    for (SourceRecord *sr : SourceRecords) {
        if (filename != sr->SourceFile) {
            continue;
        }

        string S;
        if ((sr->Type & Comment) != 0) {
            S = sr->Comment;
        } else if (sr->PrimaryURI.empty() || sr->Dist.empty()) {
            continue;
        } else {
            if ((sr->Type & Disabled) != 0)
                S = "# ";

            S += sr->GetType() + " ";

            if (sr->VendorID.empty() == false)
                S += "[" + sr->VendorID + "] ";

            S += sr->PrimaryURI + " ";
            S += sr->Dist + " ";

            for (unsigned int J = 0; J < sr->NumSections; ++J) {
                S += sr->Sections[J] + " ";
            }
        }

        // remove extra linebreak from S, if it has any
        if (!S.empty() && S[S.size() - 1] == '\n')
            S.pop_back();
        ofs << S << "\n";
    }
    ofs.close();

    return true;
}

bool SourcesList::UpdateSourceDeb822(const std::string &filename)
{
    if (std::filesystem::path(filename).extension().string() != ".sources") {
        g_warning("Tried to update APT source file '%s' in Deb822 format, but filename has wrong extension.",
            filename.c_str());
        return false;
    }

    Deb822File sf;
    if (!sf.load(filename)) {
        g_warning("Failed to load Deb822 file '%s': %s", filename.c_str(), sf.lastError().c_str());
        return false;
    }

    std::set<uint> rmPendingStanzas;
    for (SourceRecord *sr : SourceRecords) {
        if (filename != sr->SourceFile)
            continue;

        // comments shouldn't exist for Deb822 files in this data structure,
        // we parse them differently.
        if ((sr->Type & Comment) != 0)
            continue;

        if (sr->PrimaryURI.empty() || sr->Dist.empty())
            continue;

        std::string components;
        for (unsigned int J = 0; J < sr->NumSections; ++J)
            components += sr->Sections[J] + " ";
        if (!components.empty())
            components.pop_back();

        std::string uris;
        for (const auto &uri : sr->URIs)
            uris += uri + " ";
        if (!uris.empty())
            uris.pop_back();

        const auto type = sr->GetType();

        if (sf.getFieldValue(sr->Deb822StanzaIdx, "Types") != type ||
            sf.getFieldValue(sr->Deb822StanzaIdx, "URIs") != uris ||
            sf.getFieldValue(sr->Deb822StanzaIdx, "Components") != components ||
            sf.getFieldValue(sr->Deb822StanzaIdx, "Suites") != sr->Dist) {
            // The new Deb822 sources do not fit well on the existing data model and concept of
            // what a "source" is, so we rewrite the file to make it match a "one stanza per source"
            // scheme like what existed in legacy files.
            // FIXME: In the long run, we should reconsider what a repository source is and adjust the internal
            // data model, as rewriting the file like this is a really ugly hack.

            // mark the old stanza for deletion and create a new one that we will edit
            rmPendingStanzas.insert(sr->Deb822StanzaIdx);
            sr->Deb822StanzaIdx = sf.duplicateStanza(sr->Deb822StanzaIdx);
        }

        sf.updateField(sr->Deb822StanzaIdx, "Types", type);
        sf.updateField(sr->Deb822StanzaIdx, "URIs", uris);
        sf.updateField(sr->Deb822StanzaIdx, "Suites", sr->Dist);
        sf.updateField(sr->Deb822StanzaIdx, "Components", components);

        if ((sr->Type & Disabled) != 0)
            sf.updateField(sr->Deb822StanzaIdx, "Enabled", "no");
        else
            sf.deleteField(sr->Deb822StanzaIdx, "Enabled");
    }

    // delete any stanzas marked for removal, in descending order to avoid index shifting
    std::vector<uint> sortedRmStanzas(rmPendingStanzas.begin(), rmPendingStanzas.end());
    std::sort(sortedRmStanzas.rbegin(), sortedRmStanzas.rend());
    for (uint rmIdx : sortedRmStanzas)
        sf.deleteStanza(rmIdx);

    if (!sf.save(filename)) {
        g_warning("Failed to save Deb822 file '%s': %s", filename.c_str(), sf.lastError().c_str());
        return false;
    }

    // remove all records from this file
    for (auto it = SourceRecords.begin(); it != SourceRecords.end();) {
        if ((*it)->SourceFile == filename) {
            delete *it;
            it = SourceRecords.erase(it);
        } else {
            ++it;
        }
    }

    // reload the updated data
    return ReadSourceDeb822(filename);
}

bool SourcesList::UpdateSources()
{
    list<string> filenames;
    for (SourceRecord *sr : SourceRecords) {
        if (sr->SourceFile == "") {
            continue;
        }
        filenames.push_front(sr->SourceFile);
    }
    filenames.sort();
    filenames.unique();

    for (const string &filename : filenames) {
        const auto fileExt = std::filesystem::path(filename).extension().string();
        if (fileExt == ".sources") {
            if (!UpdateSourceDeb822(filename))
                return false;

        } else if (fileExt == ".list") {
            if (!UpdateSourceLegacy(filename))
                return false;

        } else {
            g_warning("Tried to update APT source file '%s', but could not determine file type.",
                filename.c_str());
        }
    }

    return true;
}

bool SourcesList::SourceRecord::SetType(string S)
{
    if (S == "deb") {
        Type |= Deb;
    } else if (S == "deb-src") {
        Type |= DebSrc;
    } else {
        return false;
    }

    return true;
}

string SourcesList::SourceRecord::GetType()
{
    if ((Type & Deb) != 0) {
        return "deb";
    } else if ((Type & DebSrc) != 0) {
        return "deb-src";
    }

    return "unknown";
}

static bool FixupURI(string &URI)
{
    if (URI.empty() == true)
        return false;

    if (URI.find(':') == string::npos)
        return false;

    URI = ::URI{SubstVar(URI, "$(ARCH)", _config->Find("APT::Architecture"))};

    // Make sure that the URI is / postfixed
    if (URI.back() != '/')
        URI.push_back('/');

    return true;
}

bool SourcesList::SourceRecord::SetURI(string S)
{
    PrimaryURI = S;
    return ::FixupURI(PrimaryURI);
}

bool SourcesList::SourceRecord::SetURIs(const std::vector<std::string> &newURIs)
{
    bool ret = true;
    URIs = newURIs;
    for (auto &uri : URIs) {
        if (!::FixupURI(uri))
            ret = false;
    }

    if (!URIs.empty())
        PrimaryURI = URIs[0];

    return ret;
}

string SourcesList::SourceRecord::joinedSections()
{
    string ret;
    for (unsigned int i = 0; i < NumSections; ++i) {
        ret += Sections[i];
        if (i + 1 < NumSections) {
            ret += " ";
        }
    }
    return ret;
}

string SourcesList::SourceRecord::niceName()
{
    string ret;
    if (starts_with(PrimaryURI, "cdrom")) {
        ret = "Disc ";
    }

    // Make distribution camel case
    std::locale loc;
    string dist = Dist;
    dist[0] = std::toupper(dist[0], loc);

    // Replace - or / by by a space
    std::size_t found = dist.find_first_of("-/");
    while (found != std::string::npos) {
        dist[found] = ' ';
        found = dist.find_first_of("-/", found + 1);
    }
    ret += dist;

    // Append sections: main contrib non-free
    if (NumSections) {
        ret += " (" + joinedSections() + ")";
    }

    if(Type & SourcesList::DebSrc) {
        ret += " Sources";
    }

    std::string uri_info;
    size_t schema_pos = PrimaryURI.find("://");
    if (schema_pos == std::string::npos) {
        uri_info = PrimaryURI;
    } else {
        uri_info = PrimaryURI.substr(schema_pos + 3);
        if (uri_info.back() == '/')
            uri_info.pop_back();
    }

    if (g_pattern_match_simple ("*.debian.org/*", uri_info.c_str()))
        return "Debian " + ret;
    if (g_pattern_match_simple ("*.ubuntu.com/*", uri_info.c_str()))
        return "Ubuntu " + ret;
    if (g_pattern_match_simple ("*.pureos.net/*", uri_info.c_str()))
        return "PureOS " + ret;

    return uri_info + " - " + ret;
}

string SourcesList::SourceRecord::repoId()
{
    string ret;
    ret = SourceFile;
    ret += ":" + GetType();
    ret += VendorID + " ";
    ret += PrimaryURI + " ";
    ret += Dist + " ";
    ret += joinedSections();
    return ret;
}

bool SourcesList::SourceRecord::hasSection(const char *component)
{
    for (unsigned int i = 0; i < NumSections; ++i) {
        if (Sections[i].compare(component) == 0) {
            return true;
        }
    }
    return false;
}

SourcesList::SourceRecord &SourcesList::SourceRecord::operator=(const SourceRecord &rhs)
{
    // Needed for a proper deep copy of the record; uses the string operator= to properly copy the strings
    Type = rhs.Type;
    VendorID = rhs.VendorID;
    PrimaryURI = rhs.PrimaryURI;
    URIs = rhs.URIs;
    Dist = rhs.Dist;
    Sections = new string[rhs.NumSections];
    for (unsigned int I = 0; I < rhs.NumSections; ++I) {
        Sections[I] = rhs.Sections[I];
    }
    NumSections = rhs.NumSections;
    Comment = rhs.Comment;
    SourceFile = rhs.SourceFile;
    Deb822StanzaIdx = rhs.Deb822StanzaIdx;

    return *this;
}

SourcesList::VendorRecord *SourcesList::AddVendorNode(VendorRecord &rec)
{
    VendorRecord *newrec = new VendorRecord;
    *newrec = rec;
    VendorRecords.push_back(newrec);

    return newrec;
}

bool SourcesList::ReadVendors()
{
    Configuration Cnf;

    string CnfFile = _config->FindFile("Dir::Etc::vendorlist");
    if (FileExists(CnfFile) == true) {
        if (ReadConfigFile(Cnf, CnfFile, true) == false) {
            return false;
        }
    }

    for (VendorRecord *vr : VendorRecords) {
        delete vr;
    }
    VendorRecords.clear();

    // Process 'simple-key' type sections
    const Configuration::Item *Top = Cnf.Tree("simple-key");
    for (Top = (Top == 0 ? 0 : Top->Child); Top != 0; Top = Top->Next) {
        Configuration Block(Top);
        VendorRecord Vendor;

        Vendor.VendorID = Top->Tag;
        Vendor.FingerPrint = Block.Find("Fingerprint");
        Vendor.Description = Block.Find("Name");

        char *buffer = new char[Vendor.FingerPrint.length() + 1];
        char *p = buffer;;
        for (string::const_iterator I = Vendor.FingerPrint.begin();
             I != Vendor.FingerPrint.end(); ++I) {
            if (*I != ' ' && *I != '\t') {
                *p++ = *I;
            }
        }
        *p = 0;
        Vendor.FingerPrint = buffer;
        delete[]buffer;

        if (Vendor.FingerPrint.empty() == true ||
                Vendor.Description.empty() == true) {
            _error->Error("Vendor block %s is invalid",
                          Vendor.VendorID.c_str());
            continue;
        }

        AddVendorNode(Vendor);
    }

    return !_error->PendingError();
}

SourcesList::VendorRecord *SourcesList::AddVendor(string VendorID,
                                                  string FingerPrint,
                                                  string Description)
{
    VendorRecord rec;
    rec.VendorID = VendorID;
    rec.FingerPrint = FingerPrint;
    rec.Description = Description;
    return AddVendorNode(rec);
}

bool SourcesList::UpdateVendors()
{
    ofstream ofs(_config->FindFile("Dir::Etc::vendorlist").c_str(), ios::out);
    if (!ofs != 0) {
        return false;
    }

    for (VendorRecord *vr : VendorRecords) {
        ofs << "simple-key \"" << vr->VendorID << "\" {" << endl;
        ofs << "\tFingerPrint \"" << vr->FingerPrint << "\";" << endl;
        ofs << "\tName \"" << vr->Description << "\";" << endl;
        ofs << "}" << endl;
    }

    ofs.close();
    return true;
}


void SourcesList::RemoveVendor(VendorRecord *&rec)
{
    VendorRecords.remove(rec);
    delete rec;
    rec = 0;
}

ostream &operator<<(ostream &os, const SourcesList::SourceRecord &rec)
{
    os << "Type: ";
    if ((rec.Type & SourcesList::Comment) != 0) {
        os << "Comment ";
    }
    if ((rec.Type & SourcesList::Disabled) != 0) {
        os << "Disabled ";
    }
    if ((rec.Type & SourcesList::Deb) != 0) {
        os << "Deb";
    }
    if ((rec.Type & SourcesList::DebSrc) != 0) {
        os << "DebSrc";
    }
    os << endl;
    os << "SourceFile: " << rec.SourceFile << endl;
    os << "VendorID: " << rec.VendorID << endl;
    os << "URI: " << rec.PrimaryURI << endl;
    os << "Dist: " << rec.Dist << endl;
    os << "Section(s):" << endl;
#if 0
    for (unsigned int J = 0; J < rec.NumSections; ++J) {
        cout << "\t" << rec.Sections[J] << endl;
    }
#endif
    os << endl;
    return os;
}

ostream &operator <<(ostream &os, const SourcesList::VendorRecord &rec)
{
    os << "VendorID: " << rec.VendorID << endl;
    os << "FingerPrint: " << rec.FingerPrint << endl;
    os << "Description: " << rec.Description << endl;
    return os;
}
