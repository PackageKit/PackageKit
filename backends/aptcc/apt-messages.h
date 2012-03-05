/* apt-messages.h
 *
 *  Copyright (c) 2004 Daniel Burrows
 *  Copyright (c) 2009-2011 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
 *                2011-2012 Matthias Klumpp <matthias@tenstral.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 */

#ifndef APT_MESSAGES_H
#define APT_MESSAGES_H

#include <pk-backend.h>
#include <apt-pkg/cachefile.h>

/** \file apt-messages.h
 */

/**
 * Call the Packagekit error dialog
 */
bool show_errors(PkBackend *backend,
		PkErrorEnum errorCode = PK_ERROR_ENUM_UNKNOWN);

/**
 * Call the Packagekit message dialog
 */
bool show_warnings(PkBackend *backend,
		PkMessageEnum message = PK_MESSAGE_ENUM_UNKNOWN);

/** Shows a list of all broken packages together with their
 *  dependencies.  Similar to and based on the equivalent routine in
 *  apt-get.
 */
void show_broken(PkBackend *backend, pkgCacheFile &cache, bool Now);

#endif // AAPT_BACKEND_MESSAGES_H
