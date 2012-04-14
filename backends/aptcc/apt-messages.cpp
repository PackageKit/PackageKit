/* apt-messages.cpp
 *
 * Copyright (c) 2004 Daniel Burrows
 * Copyright (c) 2009-2011 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
 *               2011-2012 Matthias Klumpp <matthias@tenstral.net>
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

#include "apt-messages.h"

#include <string>
#include <sstream>
#include <cstdio>

using namespace std;

bool show_errors(PkBackend *backend, PkErrorEnum errorCode)
{
    stringstream errors;

    string Err;
    while (_error->empty() == false) {
        bool Type = _error->PopMessage(Err);
        if (Type == true) {
            errors << "E: " << Err << endl;
        } else {
            errors << "W: " << Err << endl;
        }
    }

    if (!errors.str().empty()) {
        pk_backend_error_code(backend, errorCode, errors.str().c_str());
    }
}

bool show_warnings(PkBackend *backend, PkMessageEnum message)
{
    stringstream warnings;

    string Err;
    while (_error->empty() == false) {
        bool Type = _error->PopMessage(Err);
        if (Type == true) {
            warnings << "E: " << Err << endl;
        } else {
            warnings << "W: " << Err << endl;
        }
    }

    if (!warnings.str().empty()) {
        pk_backend_message(backend, message, warnings.str().c_str());
    }
}

/** Shows broken dependencies for a single package */
void show_broken(PkBackend *backend, pkgCacheFile &Cache, bool Now)
{
    stringstream out;

    out << "The following packages have unmet dependencies:" << endl;
    for (pkgCache::PkgIterator I = Cache->PkgBegin(); ! I.end(); ++I) {
        if (Now == true) {
            if (Cache[I].NowBroken() == false) {
                continue;
            }
        } else {
            if (Cache[I].InstBroken() == false){
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
            Ver = Cache[I].InstVerIter(Cache);
        }

        if (Ver.end() == true) {
            out << endl;
            continue;
        }

        for (pkgCache::DepIterator D = Ver.DependsList(); D.end() == false;) {
            // Compute a single dependency element (glob or)
            pkgCache::DepIterator Start;
            pkgCache::DepIterator End;
            D.GlobOr(Start,End); // advances D

            if (Cache->IsImportantDep(End) == false){
                continue;
            }

            if (Now == true) {
                if ((Cache[End] & pkgDepCache::DepGNow) == pkgDepCache::DepGNow){
                    continue;
                }
            } else {
                if ((Cache[End] & pkgDepCache::DepGInstall) == pkgDepCache::DepGInstall) {
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
                    pkgCache::VerIterator Ver = Cache[Targ].InstVerIter(Cache);
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
                        if (Cache[Targ].CandidateVerIter(Cache).end() == true) {
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
                out << endl;

                if (Start == End){
                    break;
                }
                Start++;
            }
        }
    }
    pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, out.str().c_str());
}
