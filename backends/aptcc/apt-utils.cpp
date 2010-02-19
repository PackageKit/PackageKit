/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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
#include <iostream>

static int descrBufferSize = 4096;
static char *descrBuffer = new char[descrBufferSize];

static char *debParser(string descr);

string get_default_short_description(const pkgCache::VerIterator &ver,
				    pkgRecords *records)
{
	if(ver.end() || ver.FileList().end() || records == NULL) {
		return string();
	}

	pkgCache::VerFileIterator vf = ver.FileList();

	if (vf.end()) {
		return string();
	} else {
		return records->Lookup(vf).ShortDesc();
	}
}

string get_short_description(const pkgCache::VerIterator &ver,
			    pkgRecords *records)
{
	if (ver.end() || ver.FileList().end() || records == NULL) {
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
		return records->Lookup(df).ShortDesc();
	}
}

string get_long_description(const pkgCache::VerIterator &ver,
				pkgRecords *records)
{
	if (ver.end() || ver.FileList().end() || records == NULL) {
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
		return records->Lookup(df).LongDesc();
	}
}

string get_long_description_parsed(const pkgCache::VerIterator &ver,
				pkgRecords *records)
{
	return debParser(get_long_description(ver, records));
}

string get_default_long_description(const pkgCache::VerIterator &ver,
				    pkgRecords *records)
{
	if(ver.end() || ver.FileList().end() || records == NULL) {
		return string();
	}

	pkgCache::VerFileIterator vf = ver.FileList();

	if (vf.end()) {
		return string();
	} else {
		return records->Lookup(vf).LongDesc();
	}
}

static char *debParser(string descr)
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
	strcpy(descrBuffer, descr.c_str());
	return descrBuffer;
}

PkGroupEnum
get_enum_group (string group)
{
	if (group.compare ("admin") == 0) {
		return PK_GROUP_ENUM_ADMIN_TOOLS;
	} else if (group.compare ("base") == 0) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.compare ("comm") == 0) {
		return PK_GROUP_ENUM_COMMUNICATION;
	} else if (group.compare ("devel") == 0) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.compare ("doc") == 0) {
		return PK_GROUP_ENUM_DOCUMENTATION;
	} else if (group.compare ("editors") == 0) {
		return PK_GROUP_ENUM_PUBLISHING;
	} else if (group.compare ("electronics") == 0) {
		return PK_GROUP_ENUM_ELECTRONICS;
	} else if (group.compare ("embedded") == 0) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.compare ("games") == 0) {
		return PK_GROUP_ENUM_GAMES;
	} else if (group.compare ("gnome") == 0) {
		return PK_GROUP_ENUM_DESKTOP_GNOME;
	} else if (group.compare ("graphics") == 0) {
		return PK_GROUP_ENUM_GRAPHICS;
	} else if (group.compare ("hamradio") == 0) {
		return PK_GROUP_ENUM_COMMUNICATION;
	} else if (group.compare ("interpreters") == 0) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.compare ("kde") == 0) {
		return PK_GROUP_ENUM_DESKTOP_KDE;
	} else if (group.compare ("libdevel") == 0) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.compare ("libs") == 0) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.compare ("mail") == 0) {
		return PK_GROUP_ENUM_INTERNET;
	} else if (group.compare ("math") == 0) {
		return PK_GROUP_ENUM_SCIENCE;
	} else if (group.compare ("misc") == 0) {
		return PK_GROUP_ENUM_OTHER;
	} else if (group.compare ("net") == 0) {
		return PK_GROUP_ENUM_NETWORK;
	} else if (group.compare ("news") == 0) {
		return PK_GROUP_ENUM_INTERNET;
	} else if (group.compare ("oldlibs") == 0) {
		return PK_GROUP_ENUM_LEGACY;
	} else if (group.compare ("otherosfs") == 0) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.compare ("perl") == 0) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.compare ("python") == 0) {
		return PK_GROUP_ENUM_PROGRAMMING;
	} else if (group.compare ("science") == 0) {
		return PK_GROUP_ENUM_SCIENCE;
	} else if (group.compare ("shells") == 0) {
		return PK_GROUP_ENUM_SYSTEM;
	} else if (group.compare ("sound") == 0) {
		return PK_GROUP_ENUM_MULTIMEDIA;
	} else if (group.compare ("tex") == 0) {
		return PK_GROUP_ENUM_PUBLISHING;
	} else if (group.compare ("text") == 0) {
		return PK_GROUP_ENUM_PUBLISHING;
	} else if (group.compare ("utils") == 0) {
		return PK_GROUP_ENUM_ACCESSORIES;
	} else if (group.compare ("web") == 0) {
		return PK_GROUP_ENUM_INTERNET;
	} else if (group.compare ("x11") == 0) {
		return PK_GROUP_ENUM_DESKTOP_OTHER;
	} else if (group.compare ("alien") == 0) {
		return PK_GROUP_ENUM_UNKNOWN;//FIXME alien is an unknown group?
	} else if (group.compare ("translations") == 0) {
		return PK_GROUP_ENUM_LOCALIZATION;
	} else if (group.compare ("metapackages") == 0) {
		return PK_GROUP_ENUM_COLLECTIONS;
	} else {
		return PK_GROUP_ENUM_UNKNOWN;
	}
}

bool contains(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> > packages,
	    const pkgCache::PkgIterator pkg)
{
	for(vector<pair<pkgCache::PkgIterator, pkgCache::VerIterator> >::iterator it = packages.begin();
	    it != packages.end(); ++it)
	{
		if (it->first == pkg) {
			return true;
		}
	}
	return false;
}

bool ends_with (const string &str, const char *end)
{
	size_t endSize = strlen(end);
	return str.size() >= endSize && (memcmp(str.data() + str.size() - endSize, end, endSize) == 0);
}

bool starts_with (const string &str, const char *start)
{
	size_t startSize = strlen(start);
	return str.size() >= startSize && (strncmp(str.data(), start, startSize) == 0);
}

