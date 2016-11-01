/* apt-cache-file.cpp
 * 
 * Copyright (c) 2012 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2012 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (c) 2016 Harald Sitter <sitter@kde.org>
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
#include "apt-cache-file.h"

#include <sstream>
#include <cstdio>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/progress.h>
#include <apt-pkg/upgrade.h>

#include "apt-utils.h"
#include "apt-messages.h"

using namespace APT;

AptCacheFile::AptCacheFile(PkBackendJob *job) :
    m_packageRecords(0),
    m_job(job)
{
}

AptCacheFile::~AptCacheFile()
{
    Close();
}

bool AptCacheFile::Open(bool withLock)
{
    OpPackageKitProgress progress(m_job);
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
    OpPackageKitProgress progress(m_job);
    return pkgCacheFile::BuildCaches(&progress, withLock);
}

bool AptCacheFile::CheckDeps(bool AllowBroken)
{
    PkRoleEnum role = pk_backend_job_get_role(m_job);
    bool FixBroken = (role == PK_ROLE_ENUM_REPAIR_SYSTEM);

    if (_error->PendingError() == true) {
        return false;
    }

    // Check that the system is OK
    if (DCache->DelCount() != 0 || DCache->InstCount() != 0) {
        _error->Error("Internal error, non-zero counts");
        show_errors(m_job, PK_ERROR_ENUM_INTERNAL_ERROR);
        return false;
    }

    // Apply corrections for half-installed packages
    if (pkgApplyStatus(*DCache) == false) {
        _error->Error("Unable to apply corrections for half-installed packages");;
        show_errors(m_job, PK_ERROR_ENUM_INTERNAL_ERROR);
        return false;
    }

    // Nothing is broken or we don't want to try fixing it
    if (DCache->BrokenCount() == 0 || AllowBroken == true) {
        return true;
    }

    // Attempt to fix broken things
    if (pkgFixBroken(*DCache) == false || DCache->BrokenCount() != 0) {
        // We failed to fix the cache
        ShowBroken(true, PK_ERROR_ENUM_UNFINISHED_TRANSACTION);

        g_warning("Unable to correct dependencies");
        return false;
    }

    if (pkgMinimizeUpgrade(*DCache) == false) {
        g_warning("Unable to minimize the upgrade set");
        show_errors(m_job, PK_ERROR_ENUM_INTERNAL_ERROR);
        return false;
    }

    // Fixing the cache is DONE no errors were found
    return true;
}

bool AptCacheFile::DistUpgrade()
{
    OpPackageKitProgress progress(m_job);
    return Upgrade::Upgrade(*this, Upgrade::ALLOW_EVERYTHING, &progress);
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
    pk_backend_job_error_code(m_job,
                              error,
                              "%s",
                              utf8(out.str().c_str()));
}

void AptCacheFile::buildPkgRecords()
{
    if (m_packageRecords) {
        return;
    }

    // Create the text record parser
    m_packageRecords = new pkgRecords(*this);
}

bool AptCacheFile::doAutomaticRemove()
{
    pkgDepCache::ActionGroup group(*this);

    // look over the cache to see what can be removed
    for (pkgCache::PkgIterator Pkg = (*this)->PkgBegin(); ! Pkg.end(); ++Pkg) {
        if ((*this)[Pkg].Garbage) {
            if (Pkg.CurrentVer() != 0 &&
                    Pkg->CurrentState != pkgCache::State::ConfigFiles) {
                // TODO, packagekit could provide a way to purge
                (*this)->MarkDelete(Pkg, false);
            } else {
                (*this)->MarkKeep(Pkg, false, false);
            }
        }
    }

    // Now see if we destroyed anything
    if ((*this)->BrokenCount() != 0) {
        cout << "Hmm, seems like the AutoRemover destroyed something which really\n"
                "shouldn't happen. Please file a bug report against apt." << endl;
        // TODO call show_broken
        //       ShowBroken(c1out,cache,false);
        return _error->Error("Internal Error, AutoRemover broke stuff");
    }

    return true;
}

bool AptCacheFile::isRemovingEssentialPackages()
{
    string List;
    bool *Added = new bool[(*this)->Head().PackageCount];
    for (unsigned int I = 0; I != (*this)->Head().PackageCount; ++I) {
        Added[I] = false;
    }

    for (pkgCache::PkgIterator I = (*this)->PkgBegin(); ! I.end(); ++I) {
        if ((I->Flags & pkgCache::Flag::Essential) != pkgCache::Flag::Essential &&
                (I->Flags & pkgCache::Flag::Important) != pkgCache::Flag::Important) {
            continue;
        }

        if ((*this)[I].Delete() == true) {
            if (Added[I->ID] == false) {
                Added[I->ID] = true;
                List += string(I.Name()) + " ";
            }
        }

        if (I->CurrentVer == 0) {
            continue;
        }

        // Print out any essential package depenendents that are to be removed
        for (pkgCache::DepIterator D = I.CurrentVer().DependsList(); D.end() == false; ++D) {
            // Skip everything but depends
            if (D->Type != pkgCache::Dep::PreDepends &&
                    D->Type != pkgCache::Dep::Depends){
                continue;
            }

            pkgCache::PkgIterator P = D.SmartTargetPkg();
            if ((*this)[P].Delete() == true)
            {
                if (Added[P->ID] == true){
                    continue;
                }
                Added[P->ID] = true;

                char S[300];
                snprintf(S, sizeof(S), "%s (due to %s) ", P.Name(), I.Name());
                List += S;
            }
        }
    }

    delete [] Added;
    if (!List.empty()) {
        pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
                                  "WARNING: You are trying to remove the following essential packages: %s",
                                  List.c_str());
        return true;
    }

    return false;
}

pkgCache::VerIterator AptCacheFile::resolvePkgID(const gchar *packageId)
{
    gchar **parts;
    pkgCache::PkgIterator pkg;

    parts = pk_package_id_split(packageId);
    pkg = (*this)->FindPkg(parts[PK_PACKAGE_ID_NAME], parts[PK_PACKAGE_ID_ARCH]);

    // Ignore packages that could not be found or that exist only due to dependencies.
    if (pkg.end() || (pkg.VersionList().end() && pkg.ProvidesList().end())) {
        g_strfreev(parts);
        return pkgCache::VerIterator();
    }

    const pkgCache::VerIterator &ver = findVer(pkg);
    // check to see if the provided package isn't virtual too
    if (ver.end() == false &&
            strcmp(ver.VerStr(), parts[PK_PACKAGE_ID_VERSION]) == 0) {
        g_strfreev(parts);
        return ver;
    }

    const pkgCache::VerIterator &candidateVer = findCandidateVer(pkg);
    // check to see if the provided package isn't virtual too
    if (candidateVer.end() == false &&
            strcmp(candidateVer.VerStr(), parts[PK_PACKAGE_ID_VERSION]) == 0) {
        g_strfreev(parts);
        return candidateVer;
    }

    g_strfreev (parts);

    return ver;
}

pkgCache::VerIterator AptCacheFile::findVer(const pkgCache::PkgIterator &pkg)
{
    // if the package is installed return the current version
    if (!pkg.CurrentVer().end()) {
        return pkg.CurrentVer();
    }

    // Else get the candidate version iterator
    const pkgCache::VerIterator &candidateVer = findCandidateVer(pkg);
    if (!candidateVer.end()) {
        return candidateVer;
    }

    // return the version list as a last resource
    return pkg.VersionList();
}

pkgCache::VerIterator AptCacheFile::findCandidateVer(const pkgCache::PkgIterator &pkg)
{
    // get the candidate version iterator
    return (*this)[pkg].CandidateVerIter(*this);
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

bool AptCacheFile::tryToInstall(pkgProblemResolver &Fix,
                                const pkgCache::VerIterator &ver,
                                bool BrokenFix,
                                bool autoInst,
                                bool preserveAuto)
{
    pkgCache::PkgIterator Pkg = ver.ParentPkg();

    // Check if there is something at all to install
    GetDepCache()->SetCandidateVersion(ver);
    pkgDepCache::StateCache &State = (*this)[Pkg];

    if (State.CandidateVer == 0) {
        pk_backend_job_error_code(m_job,
                                  PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
                                  "Package %s is virtual and has no installation candidate",
                                  Pkg.Name());
        return false;
    }

    // On updates we want to always preserve the autoflag as updates are usually
    // non-indicative of whether or not the user explicitly wants this package to be
    // installed or simply wants it to be updated.
    const bool fromUser = preserveAuto ? !(State.Flags & pkgCache::Flag::Auto) : true;
    // FIXME: this is ignoring the return value. OTOH the return value means little to us
    //   since we run markinstall twice, once without autoinst and once with.
    //   We probably should change the return value behavior and have the callee decide whether to
    //   error out or call us again with autoinst. This however is further complicated by us
    //   having protected, so we'd have to lift protection before this?
    GetDepCache()->MarkInstall(Pkg, autoInst, 0, fromUser);
    // Protect against further resolver changes.
    Fix.Clear(Pkg);
    Fix.Protect(Pkg);

    return true;
}

void AptCacheFile::tryToRemove(pkgProblemResolver &Fix,
                               const pkgCache::VerIterator &ver)
{
    pkgCache::PkgIterator Pkg = ver.ParentPkg();

    // The package is not installed
    if (Pkg->CurrentVer == 0) {
        Fix.Clear(Pkg);
        Fix.Protect(Pkg);
        Fix.Remove(Pkg);

        return;
    }

    Fix.Clear(Pkg);
    Fix.Protect(Pkg);
    Fix.Remove(Pkg);
    // TODO this is false since PackageKit can't
    // tell it want's o purge
    GetDepCache()->MarkDelete(Pkg, false);
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

OpPackageKitProgress::OpPackageKitProgress(PkBackendJob *job) :
    m_job(job)
{
    // Set PackageKit status
    pk_backend_job_set_status(m_job, PK_STATUS_ENUM_LOADING_CACHE);
}

OpPackageKitProgress::~OpPackageKitProgress()
{
    Done();
}

void OpPackageKitProgress::Done()
{
    pk_backend_job_set_percentage(m_job, 100);
}

void OpPackageKitProgress::Update()
{
    if (CheckChange() == false) {
        // No change has happened skip
        return;
    }

    // Set the new percent
    pk_backend_job_set_percentage(m_job, static_cast<unsigned int>(Percent));
}
