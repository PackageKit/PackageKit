/* apt-messages.cpp
 *
 * Copyright (c) 2004 Daniel Burrows
 * Copyright (c) 2009-2011 Daniel Nicoletti <dantti12@gmail.com>
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

#include <sstream>
#include <apt-pkg/error.h>

#include "apt-utils.h"

using namespace std;

void show_errors(PkBackendJob *job, PkErrorEnum errorCode, bool errModify)
{
    stringstream errors;
    int errorCount = 0;

    string Err;
    while (_error->empty() == false) {
        bool Type = _error->PopMessage(Err);

        g_warning("%s", Err.c_str());

        // Ugly workaround to demote the "repo not found" error message to a simple message
        if ((errModify) && (Err.find("404  Not Found") != string::npos)) {
            // TODO this should emit the regular
            // PK_ERROR_ENUM_CANNOT_FETCH_SOURCES but do not fail the
            // last-time-update
            //! messages << "E: " << Err << endl;
            continue;
        }

        if (Type == true) {
            errors << "E: " << Err << endl;
            errorCount++;
        } else {
            errors << "W: " << Err << endl;
        }
    }

    if (errorCount > 0) {
        pk_backend_job_error_code(job,
                                  errorCode,
                                  "%s",
                                  utf8(errors.str().c_str()));
    }
}
