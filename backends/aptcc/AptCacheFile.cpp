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

#include "apt-utils.h"
#include "OpPackageKitProgress.h"

#include <apt-pkg/algorithms.h>
#include <sstream>
#include <cstdio>

AptCacheFile::AptCacheFile(PkBackend *backend) :
    m_packageRecords(0),
    m_backend(backend)
{
}

AptCacheFile::~AptCacheFile()
{
    Close();
}

bool AptCacheFile::Open(bool withLock)
{
    OpPackageKitProgress progress(m_backend);
    return pkgCacheFile::Open(&progress, withLock);
}

void AptCacheFile::Close()
{
    delete m_packageRecords;

    m_packageRecords = 0;

    pkgCacheFile::Close();

    // Discard all errors to avoid a future failure when opening
    // the package cache
    _error->Discard();
}

bool AptCacheFile::BuildCaches(bool withLock)
{
    OpPackageKitProgress progress(m_backend);
    return pkgCacheFile::BuildCaches(&progress, withLock);
}

bool AptCacheFile::CheckDeps(bool FixBroken)
{
    if (_error->PendingError() == true) {
        return false;
    }

    // Check that the system is OK
    if (DCache->DelCount() != 0 || DCache->InstCount() != 0) {
        return _error->Error("Internal error, non-zero counts");
    }

    // Apply corrections for half-installed packages
    if (pkgApplyStatus(*DCache) == false) {
        return _error->Error("Unable to apply corrections for half-installed packages");;
    }

    // Nothing is broken or we don't want to try fixing it
    if (DCache->BrokenCount() == 0 || FixBroken == false) {
        return true;
    }

    // Attempt to fix broken things
    if (pkgFixBroken(*DCache) == false || DCache->BrokenCount() != 0) {
        // We failed to fix the cache
        ShowBroken(true);

        return _error->Error("Unable to correct dependencies");
    }
    if (pkgMinimizeUpgrade(*DCache) == false) {
        return _error->Error("Unable to minimize the upgrade set");
    }

    // Fixing the cache is DONE no errors were found
    return true;
}

bool AptCacheFile::DistUpgrade()
{
    return pkgDistUpgrade(*this);
}

void AptCacheFile::ShowBroken(bool Now, PkErrorEnum error)
{
    std::stringstream out;

    out << "The following packages have unmet dependencies:" << std::endl;
    for (pkgCache::PkgIterator I = (*this)->PkgBegin(); ! I.end(); ++I) {
        if (Now == true) {
            if ((*this)[I].NowBroken() == false) {
                continue;
            }
        } else {
            if ((*this)[I].InstBroken() == false){
                continue;
            }
        }

        // Print out each package and the failed dependencies
        out << "  " <<  I.Name() << ":";
        unsigned Indent = strlen(I.Name()) + 3;
        bool First = true;
        pkgCache::VerIterator Ver;

        if (Now == true) {
            Ver = I.CurrentVer();
        } else {
            Ver = (*this)[I].InstVerIter(*this);
        }

        if (Ver.end() == true) {
            out << std::endl;
            continue;
        }

        for (pkgCache::DepIterator D = Ver.DependsList(); D.end() == false;) {
            // Compute a single dependency element (glob or)
            pkgCache::DepIterator Start;
            pkgCache::DepIterator End;
            D.GlobOr(Start,End); // advances D

            if ((*this)->IsImportantDep(End) == false){
                continue;
            }

            if (Now == true) {
                if (((*this)[End] & pkgDepCache::DepGNow) == pkgDepCache::DepGNow){
                    continue;
                }
            } else {
                if (((*this)[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall) {
                    continue;
                }
            }

            bool FirstOr = true;
            while (1) {
                if (First == false){
                    for (unsigned J = 0; J != Indent; J++) {
                        out << ' ';
                    }
                }
                First = false;

                if (FirstOr == false) {
                    for (unsigned J = 0; J != strlen(End.DepType()) + 3; J++) {
                        out << ' ';
                    }
                } else {
                    out << ' ' << End.DepType() << ": ";
                }
                FirstOr = false;

                out << Start.TargetPkg().Name();

                // Show a quick summary of the version requirements
                if (Start.TargetVer() != 0) {
                    out << " (" << Start.CompType() << " " << Start.TargetVer() << ")";
                }

                /* Show a summary of the target package if possible. In the case
                of virtual packages we show nothing */
                pkgCache::PkgIterator Targ = Start.TargetPkg();
                if (Targ->ProvidesList == 0) {
                    out << ' ';
                    pkgCache::VerIterator Ver = (*this)[Targ].InstVerIter(*this);
                    if (Now == true) {
                        Ver = Targ.CurrentVer();
                    }

                    if (Ver.end() == false)
                    {
                        char buffer[1024];
                        if (Now == true) {
                            sprintf(buffer, "but %s is installed", Ver.VerStr());
                        } else {
                            sprintf(buffer, "but %s is to be installed", Ver.VerStr());
                        }

                        out << buffer;
                    } else {
                        if ((*this)[Targ].CandidateVerIter(*this).end() == true) {
                            if (Targ->ProvidesList == 0) {
                                out << "but it is not installable";
                            } else {
                                out << "but it is a virtual package";
                            }
                        } else {
                            if (Now) {
                                out << "but it is not installed";
                            } else {
                                out << "but it is not going to be installed";
                            }
                        }
                    }
                }

                if (Start != End) {
                    out << " or";
                }
                out << std::endl;

                if (Start == End){
                    break;
                }
                Start++;
            }
        }
    }
    pk_backend_error_code(m_backend, error, utf8(out.str().c_str()));
}

void AptCacheFile::buildPkgRecords()
{
    if (m_packageRecords) {
        return;
    }

    // Create the text record parser
    m_packageRecords = new pkgRecords(*this);
}

pkgCache::VerIterator AptCacheFile::findCandidateVer(const pkgCache::PkgIterator &pkg)
{
    // get the candidate version iterator
    return (*this)[pkg].CandidateVerIter(*this);
}

std::string AptCacheFile::getDefaultShortDescription(const pkgCache::VerIterator &ver)
{
    if (ver.end() || ver.FileList().end() || GetPkgRecords() == 0) {
        return string();
    }

    pkgCache::VerFileIterator vf = ver.FileList();
    return m_packageRecords->Lookup(vf).ShortDesc();
}

std::string AptCacheFile::getShortDescription(const pkgCache::VerIterator &ver)
{
    if (ver.end() || ver.FileList().end() || GetPkgRecords() == 0) {
        return string();
    }

    pkgCache::DescIterator d = ver.TranslatedDescription();
    if (d.end()) {
        return string();
    }

    pkgCache::DescFileIterator df = d.FileList();
    if (df.end()) {
        return string();
    } else {
        return m_packageRecords->Lookup(df).ShortDesc();
    }
}

std::string AptCacheFile::getDefaultLongDescription(const pkgCache::VerIterator &ver)
{
    if(ver.end() || ver.FileList().end() || GetPkgRecords() == 0) {
        return string();
    }

    pkgCache::VerFileIterator vf = ver.FileList();

    if (vf.end()) {
        return string();
    } else {
        return m_packageRecords->Lookup(vf).LongDesc();
    }
}

std::string AptCacheFile::getLongDescription(const pkgCache::VerIterator &ver)
{
    if (ver.end() || ver.FileList().end() || GetPkgRecords() == 0) {
        return string();
    }

    pkgCache::DescIterator d = ver.TranslatedDescription();
    if (d.end()) {
        return string();
    }

    pkgCache::DescFileIterator df = d.FileList();
    if (df.end()) {
        return string();
    } else {
        return m_packageRecords->Lookup(df).LongDesc();
    }
}

std::string AptCacheFile::getLongDescriptionParsed(const pkgCache::VerIterator &ver)
{
    return debParser(getLongDescription(ver));
}

std::string AptCacheFile::debParser(std::string descr)
{
    // Policy page on package descriptions
    // http://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Description
    unsigned int i;
    string::size_type nlpos=0;

    nlpos = descr.find('\n');
    // delete first line
    if (nlpos != string::npos) {
        descr.erase(0, nlpos + 2);        // del "\n " too
    }

    // avoid replacing '\n' for a ' ' after a '.\n' is found
    bool removedFullStop = false;
    while (nlpos < descr.length()) {
        // find the new line position
        nlpos = descr.find('\n', nlpos);
        if (nlpos == string::npos) {
            // if it could not find the new line
            // get out of the loop
            break;
        }

        i = nlpos;
        // erase the char after '\n' which is always " "
        descr.erase(++i, 1);

        // remove lines likes this: " .", making it a \n
        if (descr[i] == '.') {
            descr.erase(i, 1);
            nlpos = i;
            // don't permit the next round to replace a '\n' to a ' '
            removedFullStop = true;
            continue;
        } else if (descr[i] != ' ' && removedFullStop == false) {
            // it's not a line to be verbatim displayed
            // So it's a paragraph let's replace '\n' with a ' '
            // replace new line with " "
            descr.replace(nlpos, 1, " ");
        }

        removedFullStop = false;
        nlpos++;
    }

    return descr;
}
