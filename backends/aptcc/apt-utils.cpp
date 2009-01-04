/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 200 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
 *
 * Licensed under the GNU General Public License Version 2
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "apt-utils.h"
#include "apt.h"

#include <apt-pkg/pkgrecords.h>
#include <string.h>

// used to emit packages it collects all the needed info
void emit_package (PkBackend *backend, pkgRecords *records, PkBitfield filters,
		   const pkgCache::PkgIterator &pkg,
		   const pkgCache::VerIterator &ver)
{
	PkInfoEnum state;
	if (pkg->CurrentState == pkgCache::State::Installed) {
		state = PK_INFO_ENUM_INSTALLED;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
			return;
	} else {
		state = PK_INFO_ENUM_AVAILABLE;
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
			return;
	}

	if (filters != 0) {
		std::string str = ver.Section();
		std::string section, repo_section;

		size_t found;
		found = str.find_last_of("/");
		section = str.substr(found + 1);
		repo_section = str.substr(0, found);

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
			// if ver.end() means unknow
			// strcmp will be true when it's different than devel
			std::string pkgName = pkg.Name();
			if (pkgName.compare(pkgName.size() - 4, 4, "-dev") &&
			    pkgName.compare(pkgName.size() - 4, 4, "-dbg") &&
			    section.compare("devel") &&
			    section.compare("libdevel"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
			std::string pkgName = pkg.Name();
			if (!pkgName.compare(pkgName.size() - 4, 4, "-dev") ||
			    !pkgName.compare(pkgName.size() - 4, 4, "-dbg") ||
			    !section.compare("devel") ||
			    !section.compare("libdevel"))
				return;
		}

		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
			// if ver.end() means unknow
			// strcmp will be true when it's different than x11
			if (section.compare("x11") && section.compare("gnome") &&
			    section.compare("kde") && section.compare("graphics"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
			if (!section.compare("x11") || !section.compare("gnome") ||
			    !section.compare("kde") || !section.compare("graphics"))
				return;
		}

		// TODO add Ubuntu handling
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_FREE)) {
			if (!repo_section.compare("contrib") || !repo_section.compare("non-free"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_FREE)) {
			if (repo_section.compare("contrib") && repo_section.compare("non-free"))
				return;
		}

		// TODO test this one..
		if (pk_bitfield_contain (filters, PK_FILTER_ENUM_COLLECTIONS)) {
			if (!repo_section.compare("metapackages"))
				return;
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_COLLECTIONS)) {
			if (repo_section.compare("metapackages"))
				return;
		}
		
	}
	pkgCache::VerFileIterator vf = ver.FileList();

	gchar *package_id;
	package_id = pk_package_id_build ( pkg.Name(),
					ver.VerStr(),
					ver.Arch() ? ver.Arch() : "N/A", // _("N/A")
					vf.File().Archive() ? vf.File().Archive() : "<NULL>");//  _("<NULL>")
	pk_backend_package (backend, state, package_id, get_short_description(ver, records).c_str() );
}