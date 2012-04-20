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

#include "apt-utils.h"

#include <string>
#include <sstream>
#include <cstdio>

using namespace std;

bool show_errors(PkBackend *backend, PkErrorEnum errorCode, bool errModify)
{
    stringstream errors;
    stringstream messages;

    PkMessageEnum messageCode = PK_MESSAGE_ENUM_UNKNOWN;
    if (errorCode == PK_ERROR_ENUM_CANNOT_FETCH_SOURCES)
        messageCode = PK_MESSAGE_ENUM_REPO_METADATA_DOWNLOAD_FAILED;

    string Err;
    while (_error->empty() == false) {
        bool Type = _error->PopMessage(Err);

        // Ugly workaround to demote the "repo not found" error message to a simple message
        if ((errModify) && (Err.find("404  Not Found") != string::npos)) {
            messages << "E: " << Err << endl;
        } else {
            if (Type == true) {
                errors << "E: " << Err << endl;
            } else {
                errors << "W: " << Err << endl;
            }
        }
    }

    if (!errors.str().empty()) {
        pk_backend_error_code(backend, errorCode, utf8(errors.str().c_str()));
    }

    if ((errModify) && (!messages.str().empty())) {
        pk_backend_message(backend, messageCode, utf8(messages.str().c_str()));
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
        pk_backend_message(backend, message, utf8(warnings.str().c_str()));
    }
}
