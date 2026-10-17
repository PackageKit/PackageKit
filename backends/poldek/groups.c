/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2012 Marcin Banasiak <megabajt@pld-linux.org>
 * Copyright (C) 2026 Arkadiusz Miśkiewicz <arekm@maven.pl>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * PLD Linux groups (the Group tag, see rpm.groups in rpm-build-tools) and
 * the PackageKit groups they are shown as.
 */

#include <string.h>

#include "groups.h"

typedef struct
{
	const gchar *rpm;
	PkGroupEnum group;
} PkPoldekGroupMap;

/* matched on whole path components; the longest match wins */
static const PkPoldekGroupMap pk_poldek_group_map[] = {
	{ "Applications", PK_GROUP_ENUM_UTILITIES },
	{ "Applications/Communications", PK_GROUP_ENUM_COMMUNICATION },
	{ "Applications/Databases", PK_GROUP_ENUM_DATABASES },
	{ "Applications/Dictionaries", PK_GROUP_ENUM_OFFICE },
	{ "Applications/Editors", PK_GROUP_ENUM_EDITORS },
	{ "Applications/Emulators", PK_GROUP_ENUM_VIRTUALIZATION },
	{ "Applications/Engineering", PK_GROUP_ENUM_ELECTRONICS },
	{ "Applications/Games", PK_GROUP_ENUM_GAMES },
	{ "Applications/Graphics", PK_GROUP_ENUM_GRAPHICS },
	{ "Applications/Mail", PK_GROUP_ENUM_INTERNET },
	{ "Applications/Math", PK_GROUP_ENUM_SCIENCE },
	{ "Applications/Multimedia", PK_GROUP_ENUM_MULTIMEDIA },
	{ "Applications/Networking", PK_GROUP_ENUM_NETWORK },
	{ "Applications/News", PK_GROUP_ENUM_INTERNET },
	{ "Applications/Printing", PK_GROUP_ENUM_PUBLISHING },
	{ "Applications/Publishing", PK_GROUP_ENUM_PUBLISHING },
	{ "Applications/Science", PK_GROUP_ENUM_SCIENCE },
	{ "Applications/Shells", PK_GROUP_ENUM_SHELLS },
	{ "Applications/Sound", PK_GROUP_ENUM_MULTIMEDIA },
	{ "Applications/Spreadsheets", PK_GROUP_ENUM_OFFICE },
	{ "Applications/System", PK_GROUP_ENUM_ADMIN },
	{ "Applications/Text", PK_GROUP_ENUM_PUBLISHING },
	{ "Applications/WWW", PK_GROUP_ENUM_INTERNET },
	{ "Base", PK_GROUP_ENUM_SYSTEM },
	{ "Base/Authentication and Authorization", PK_GROUP_ENUM_SECURITY },
	{ "Daemons", PK_GROUP_ENUM_SERVERS },
	{ "Development", PK_GROUP_ENUM_PROGRAMMING },
	{ "Development/Debug", PK_GROUP_ENUM_DEBUG },
	{ "Development/Languages/Java", PK_GROUP_ENUM_LANG_JAVA },
	{ "Development/Languages/PHP", PK_GROUP_ENUM_LANG_PHP },
	{ "Development/Languages/Perl", PK_GROUP_ENUM_LANG_PERL },
	{ "Development/Languages/Python", PK_GROUP_ENUM_LANG_PYTHON },
	{ "Development/Languages/Scheme", PK_GROUP_ENUM_LANG_LISP },
	{ "Development/Libraries", PK_GROUP_ENUM_DEVEL },
	{ "Documentation", PK_GROUP_ENUM_DOCUMENTATION },
	{ "Fonts", PK_GROUP_ENUM_FONTS },
	{ "I18n", PK_GROUP_ENUM_LOCALIZATION },
	{ "Libraries", PK_GROUP_ENUM_LIBRARIES },
	{ "Libraries/Java", PK_GROUP_ENUM_LANG_JAVA },
	{ "Libraries/Python", PK_GROUP_ENUM_LANG_PYTHON },
	{ "Networking", PK_GROUP_ENUM_NETWORK },
	{ "Networking/Admin", PK_GROUP_ENUM_ADMIN },
	{ "Networking/Daemons", PK_GROUP_ENUM_SERVERS },
	{ "Themes", PK_GROUP_ENUM_DESKTOP_OTHER },
	{ "X11", PK_GROUP_ENUM_DESKTOP_OTHER },
	{ "X11/Amusements", PK_GROUP_ENUM_GAMES },
	{ "X11/Applications", PK_GROUP_ENUM_UTILITIES },
	{ "X11/Applications/Accessibility", PK_GROUP_ENUM_ACCESSIBILITY },
	{ "X11/Applications/Editors", PK_GROUP_ENUM_EDITORS },
	{ "X11/Applications/Games", PK_GROUP_ENUM_GAMES },
	{ "X11/Applications/Graphics", PK_GROUP_ENUM_GRAPHICS },
	{ "X11/Applications/Mail", PK_GROUP_ENUM_INTERNET },
	{ "X11/Applications/Multimedia", PK_GROUP_ENUM_MULTIMEDIA },
	{ "X11/Applications/Networking", PK_GROUP_ENUM_INTERNET },
	{ "X11/Applications/Publishing", PK_GROUP_ENUM_PUBLISHING },
	{ "X11/Applications/Science", PK_GROUP_ENUM_SCIENCE },
	{ "X11/Applications/Sound", PK_GROUP_ENUM_MULTIMEDIA },
	{ "X11/Development/Libraries", PK_GROUP_ENUM_DEVEL },
	{ "X11/Development/Tools", PK_GROUP_ENUM_PROGRAMMING },
	{ "X11/Libraries", PK_GROUP_ENUM_LIBRARIES },
};

/* Never PK_GROUP_ENUM_UNKNOWN: the daemon rejects it in details. */
PkGroupEnum
pk_poldek_group_from_rpm (const gchar *group)
{
	PkGroupEnum best = PK_GROUP_ENUM_OTHER;
	gsize best_len = 0;

	if (group == NULL)
		return best;
	for (guint i = 0; i < G_N_ELEMENTS (pk_poldek_group_map); i++) {
		const gchar *prefix = pk_poldek_group_map[i].rpm;
		gsize len = strlen (prefix);

		if (len <= best_len || strncmp (group, prefix, len) != 0)
			continue;
		if (group[len] != '\0' && group[len] != '/')
			continue;
		best = pk_poldek_group_map[i].group;
		best_len = len;
	}
	return best;
}

PkBitfield
pk_poldek_groups_used (void)
{
	PkBitfield groups = pk_bitfield_value (PK_GROUP_ENUM_OTHER);

	for (guint i = 0; i < G_N_ELEMENTS (pk_poldek_group_map); i++)
		pk_bitfield_add (groups, pk_poldek_group_map[i].group);
	return groups;
}
