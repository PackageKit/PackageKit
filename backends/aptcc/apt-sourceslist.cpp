/* apt-sourceslist.cpp - access the sources.list file
 *
 * Copyright (c) 1999 Patrick Cole <z@amused.net>
 *           (c) 2002 Synaptic development team
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
#include <algorithm>
#include <fstream>

#include "apt-utils.h"

#include "config.h"

SourcesList::~SourcesList()
{
    for (list<SourceRecord *>::iterator it = SourceRecords.begin();
         it != SourceRecords.end(); ++it) {
        delete *it;
    }

    for (list<VendorRecord *>::iterator it = VendorRecords.begin();
         it != VendorRecords.end(); ++it) {
        delete *it;
    }
}

SourcesList::SourceRecord *SourcesList::AddSourceNode(SourceRecord &rec)
{
    SourceRecord *newrec = new SourceRecord;
    *newrec = rec;
    SourceRecords.push_back(newrec);

    return newrec;
}

bool SourcesList::ReadSourcePart(string listpath)
{
    //cout << "SourcesList::ReadSourcePart() "<< listpath  << endl;
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
        while (isspace(*p)) {
            p++;
        }

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
#ifndef HAVE_RPM
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
#endif

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

        // Only look at files ending in .list to skip .rpmnew etc files
        if (strcmp(Ent->d_name + strlen(Ent->d_name) - 5, ".list") != 0) {
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
    for (vector<string>::const_iterator I = List.begin(); I != List.end(); ++I) {
        if (ReadSourcePart(*I) == false) {
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
#ifdef HAVE_RPM
    rec.Type = Rpm;
#else
    rec.Type = Deb;
#endif
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

bool SourcesList::UpdateSources()
{
    list<string> filenames;
    for (list<SourceRecord *>::iterator it = SourceRecords.begin();
         it != SourceRecords.end(); ++it) {
        if ((*it)->SourceFile == "") {
            continue;
        }
        filenames.push_front((*it)->SourceFile);
    }
    filenames.sort();
    filenames.unique();

    for (list<string>::iterator fi = filenames.begin();
         fi != filenames.end(); fi++) {
        ofstream ofs((*fi).c_str(), ios::out);
        if (!ofs != 0) {
            return false;
        }

        for (list<SourceRecord *>::iterator it = SourceRecords.begin();
             it != SourceRecords.end(); it++) {
            if ((*fi) != (*it)->SourceFile) {
                continue;
            }

            string S;
            if (((*it)->Type & Comment) != 0) {
                S = (*it)->Comment;
            } else if ((*it)->URI.empty() || (*it)->Dist.empty()) {
                continue;
            } else {
                if (((*it)->Type & Disabled) != 0)
                    S = "# ";

                S += (*it)->GetType() + " ";

                if ((*it)->VendorID.empty() == false)
                    S += "[" + (*it)->VendorID + "] ";

                S += (*it)->URI + " ";
                S += (*it)->Dist + " ";

                for (unsigned int J = 0; J < (*it)->NumSections; ++J) {
                    S += (*it)->Sections[J] + " ";
                }
            }
            ofs << S << endl;
        }
        ofs.close();
    }
    return true;
}

bool SourcesList::SourceRecord::SetType(string S)
{
    if (S == "deb") {
        Type |= Deb;
    } else if (S == "deb-src") {
        Type |= DebSrc;
    } else if (S == "rpm") {
        Type |= Rpm;
    } else if (S == "rpm-src") {
        Type |= RpmSrc;
    } else if (S == "rpm-dir") {
        Type |= RpmDir;
    } else if (S == "rpm-src-dir") {
        Type |= RpmSrcDir;
    } else if (S == "repomd") {
        Type |= Repomd;
    } else if (S == "repomd-src") {
        Type |= RepomdSrc;
    } else {
        return false;
    }

    //cout << S << " settype " << (Type | Repomd) << endl;
    return true;
}

string SourcesList::SourceRecord::GetType()
{
    if ((Type & Deb) != 0) {
        return "deb";
    } else if ((Type & DebSrc) != 0) {
        return "deb-src";
    } else if ((Type & Rpm) != 0) {
        return "rpm";
    } else if ((Type & RpmSrc) != 0) {
        return "rpm-src";
    } else if ((Type & RpmDir) != 0) {
        return "rpm-dir";
    } else if ((Type & RpmSrcDir) != 0) {
        return "rpm-src-dir";
    } else if ((Type & Repomd) != 0) {
        return "repomd";
    } else if ((Type & RepomdSrc) != 0) {
        return "repomd-src";
    }

    //cout << "type " << (Type & Repomd) << endl;
    return "unknown";
}

bool SourcesList::SourceRecord::SetURI(string S)
{
    if (S.empty() == true) {
        return false;
    }
    if (S.find(':') == string::npos) {
        return false;
    }

    S = SubstVar(S, "$(ARCH)", _config->Find("APT::Architecture"));
    S = SubstVar(S, "$(VERSION)", _config->Find("APT::DistroVersion"));
    URI = S;

    // append a / to the end if one is not already there
    if (URI[URI.size() - 1] != '/') {
        URI += '/';
    }

    return true;
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
    if (starts_with(URI, "cdrom")) {
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
    URI = rhs.URI;
    Dist = rhs.Dist;
    Sections = new string[rhs.NumSections];
    for (unsigned int I = 0; I < rhs.NumSections; ++I) {
        Sections[I] = rhs.Sections[I];
    }
    NumSections = rhs.NumSections;
    Comment = rhs.Comment;
    SourceFile = rhs.SourceFile;

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

    for (list<VendorRecord *>::const_iterator I = VendorRecords.begin();
         I != VendorRecords.end(); ++I) {
        delete *I;
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

    for (list<VendorRecord *>::iterator it = VendorRecords.begin();
         it != VendorRecords.end(); ++it) {
        ofs << "simple-key \"" << (*it)->VendorID << "\" {" << endl;
        ofs << "\tFingerPrint \"" << (*it)->FingerPrint << "\";" << endl;
        ofs << "\tName \"" << (*it)->Description << "\";" << endl;
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
    if ((rec.Type & SourcesList::Rpm) != 0) {
        os << "Rpm";
    }
    if ((rec.Type & SourcesList::RpmSrc) != 0) {
        os << "RpmSrc";
    }
    if ((rec.Type & SourcesList::RpmDir) != 0) {
        os << "RpmDir";
    }
    if ((rec.Type & SourcesList::RpmSrcDir) != 0) {
        os << "RpmSrcDir";
    }
    if ((rec.Type & SourcesList::Repomd) != 0) {
        os << "Repomd";
    }
    if ((rec.Type & SourcesList::RepomdSrc) != 0) {
        os << "RepomdSrc";
    }
    os << endl;
    os << "SourceFile: " << rec.SourceFile << endl;
    os << "VendorID: " << rec.VendorID << endl;
    os << "URI: " << rec.URI << endl;
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

ostream &operator<<(ostream &os, const SourcesList::VendorRecord &rec)
{
    os << "VendorID: " << rec.VendorID << endl;
    os << "FingerPrint: " << rec.FingerPrint << endl;
    os << "Description: " << rec.Description << endl;
    return os;
}
