/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-enum
 * @short_description: Functions for converting strings to enums and vice-versa
 *
 * This file contains functions to convert to and from enumerated types.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "pk-debug.h"
#include "pk-common.h"
#include "pk-enum.h"

static PkEnumMatch enum_exit[] = {
	{PK_EXIT_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_EXIT_ENUM_SUCCESS,			"success"},
	{PK_EXIT_ENUM_FAILED,			"failed"},
	{PK_EXIT_ENUM_CANCELLED,		"cancelled"},
	{PK_EXIT_ENUM_KEY_REQUIRED,		"key-required"},
	{PK_EXIT_ENUM_KILLED,			"killed"},
	{0, NULL}
};

static PkEnumMatch enum_status[] = {
	{PK_STATUS_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_STATUS_ENUM_WAIT,			"wait"},
	{PK_STATUS_ENUM_SETUP,			"setup"},
	{PK_STATUS_ENUM_RUNNING,		"running"},
	{PK_STATUS_ENUM_QUERY,			"query"},
	{PK_STATUS_ENUM_INFO,			"info"},
	{PK_STATUS_ENUM_REFRESH_CACHE,		"refresh-cache"},
	{PK_STATUS_ENUM_REMOVE,			"remove"},
	{PK_STATUS_ENUM_DOWNLOAD,		"download"},
	{PK_STATUS_ENUM_INSTALL,		"install"},
	{PK_STATUS_ENUM_UPDATE,			"update"},
	{PK_STATUS_ENUM_CLEANUP,		"cleanup"},
	{PK_STATUS_ENUM_OBSOLETE,		"obsolete"},
	{PK_STATUS_ENUM_DEP_RESOLVE,		"dep-resolve"},
	{PK_STATUS_ENUM_SIG_CHECK,		"sig-check"},
	{PK_STATUS_ENUM_ROLLBACK,		"rollback"},
	{PK_STATUS_ENUM_TEST_COMMIT,		"test-commit"},
	{PK_STATUS_ENUM_COMMIT,			"commit"},
	{PK_STATUS_ENUM_REQUEST,		"request"},
	{PK_STATUS_ENUM_FINISHED,		"finished"},
	{PK_STATUS_ENUM_CANCEL,			"cancel"},
	{0, NULL}
};

static PkEnumMatch enum_role[] = {
	{PK_ROLE_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_ROLE_ENUM_CANCEL,			"cancel"},
	{PK_ROLE_ENUM_GET_DEPENDS,		"get-depends"},
	{PK_ROLE_ENUM_GET_DESCRIPTION,		"get-description"},
	{PK_ROLE_ENUM_GET_FILES,		"get-files"},
	{PK_ROLE_ENUM_GET_PACKAGES,		"get-packages"},
	{PK_ROLE_ENUM_GET_REPO_LIST,		"get-repo-list"},
	{PK_ROLE_ENUM_GET_REQUIRES,		"get-requires"},
	{PK_ROLE_ENUM_GET_UPDATE_DETAIL,	"get-update-detail"},
	{PK_ROLE_ENUM_GET_UPDATES,		"get-updates"},
	{PK_ROLE_ENUM_INSTALL_FILE,		"install-file"},
	{PK_ROLE_ENUM_INSTALL_PACKAGE,		"install-package"},
	{PK_ROLE_ENUM_INSTALL_SIGNATURE,	"install-signature"},
	{PK_ROLE_ENUM_REFRESH_CACHE,		"refresh-cache"},
	{PK_ROLE_ENUM_REMOVE_PACKAGE,		"remove-package"},
	{PK_ROLE_ENUM_REPO_ENABLE,		"repo-enable"},
	{PK_ROLE_ENUM_REPO_SET_DATA,		"repo-set-data"},
	{PK_ROLE_ENUM_RESOLVE,			"resolve"},
	{PK_ROLE_ENUM_ROLLBACK,			"rollback"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,		"search-details"},
	{PK_ROLE_ENUM_SEARCH_FILE,		"search-file"},
	{PK_ROLE_ENUM_SEARCH_GROUP,		"search-group"},
	{PK_ROLE_ENUM_SEARCH_NAME,		"search-name"},
	{PK_ROLE_ENUM_SERVICE_PACK,		"service-pack"},
	{PK_ROLE_ENUM_UPDATE_PACKAGES,		"update-package"},
	{PK_ROLE_ENUM_UPDATE_SYSTEM,		"update-system"},
	{PK_ROLE_ENUM_WHAT_PROVIDES,		"what-provides"},
	{0, NULL}
};

static PkEnumMatch enum_error[] = {
	{PK_ERROR_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_ERROR_ENUM_OOM,			"out-of-memory"},
	{PK_ERROR_ENUM_NO_CACHE,		"no-cache"},
	{PK_ERROR_ENUM_NO_NETWORK,		"no-network"},
	{PK_ERROR_ENUM_NOT_SUPPORTED,		"not-supported"},
	{PK_ERROR_ENUM_INTERNAL_ERROR,		"internal-error"},
	{PK_ERROR_ENUM_GPG_FAILURE,		"gpg-failure"},
	{PK_ERROR_ENUM_FILTER_INVALID,		"filter-invalid"},
	{PK_ERROR_ENUM_PACKAGE_ID_INVALID,	"package-id-invalid"},
	{PK_ERROR_ENUM_TRANSACTION_ERROR,	"transaction-error"},
	{PK_ERROR_ENUM_TRANSACTION_CANCELLED,	"transaction-cancelled"},
	{PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,	"package-not-installed"},
	{PK_ERROR_ENUM_PACKAGE_NOT_FOUND,	"package-not-found"},
	{PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,	"package-already-installed"},
	{PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,	"package-download-failed"},
	{PK_ERROR_ENUM_GROUP_NOT_FOUND,		"group-not-found"},
	{PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,	"dep-resolution-failed"},
	{PK_ERROR_ENUM_CREATE_THREAD_FAILED,	"create-thread-failed"},
	{PK_ERROR_ENUM_REPO_NOT_FOUND,		"repo-not-found"},
	{PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,	"cannot-remove-system-package"},
	{PK_ERROR_ENUM_PROCESS_KILL,		"process-kill"},
        {PK_ERROR_ENUM_FAILED_INITIALIZATION,   "failed-initialization"},
        {PK_ERROR_ENUM_FAILED_FINALISE,         "failed-finalise"},
	{PK_ERROR_ENUM_FAILED_CONFIG_PARSING,	"failed-config-parsing"},
	{PK_ERROR_ENUM_CANNOT_CANCEL,		"cannot-cancel"},
	{PK_ERROR_ENUM_CANNOT_GET_LOCK,         "cannot-get-lock"},
	{PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE,   "no-packages-to-update"},
	{PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG,        "cannot-write-repo-config"},
	{PK_ERROR_LOCAL_INSTALL_FAILED,         "local-install-failed"},
	{PK_ERROR_BAD_GPG_SIGNATURE,            "bad-gpg-signature"},
	{PK_ERROR_ENUM_CANNOT_INSTALL_SOURCE_PACKAGE,       "cannot-install-source-package"},
	{0, NULL}
};

static PkEnumMatch enum_restart[] = {
	{PK_RESTART_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_RESTART_ENUM_NONE,			"none"},
	{PK_RESTART_ENUM_SYSTEM,		"system"},
	{PK_RESTART_ENUM_SESSION,		"session"},
	{PK_RESTART_ENUM_APPLICATION,		"application"},
	{0, NULL}
};

static PkEnumMatch enum_message[] = {
	{PK_MESSAGE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_MESSAGE_ENUM_NOTICE,		"notice"},
	{PK_MESSAGE_ENUM_WARNING,		"warning"},
	{PK_MESSAGE_ENUM_DAEMON,		"daemon"},
	{0, NULL}
};

static PkEnumMatch enum_filter[] = {
	{PK_FILTER_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_FILTER_ENUM_NONE,			"none"},
	{PK_FILTER_ENUM_INSTALLED,		"installed"},
	{PK_FILTER_ENUM_NOT_INSTALLED,		"~installed"},
	{PK_FILTER_ENUM_DEVELOPMENT,		"devel"},
	{PK_FILTER_ENUM_NOT_DEVELOPMENT,	"~devel"},
	{PK_FILTER_ENUM_GUI,			"gui"},
	{PK_FILTER_ENUM_NOT_GUI,		"~gui"},
	{PK_FILTER_ENUM_FREE,			"free"},
	{PK_FILTER_ENUM_NOT_FREE,		"~free"},
	{PK_FILTER_ENUM_VISIBLE,		"visible"},
	{PK_FILTER_ENUM_NOT_VISIBLE,		"~visible"},
	{PK_FILTER_ENUM_SUPPORTED,		"supported"},
	{PK_FILTER_ENUM_NOT_SUPPORTED,		"~supported"},
	{PK_FILTER_ENUM_BASENAME,		"basename"},
	{PK_FILTER_ENUM_NOT_BASENAME,		"~basename"},
	{PK_FILTER_ENUM_NEWEST,			"newest"},
	{PK_FILTER_ENUM_NOT_NEWEST,		"~newest"},
	{0, NULL}
};

static PkEnumMatch enum_group[] = {
	{PK_GROUP_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_GROUP_ENUM_ACCESSIBILITY,		"accessibility"},
	{PK_GROUP_ENUM_ACCESSORIES,		"accessories"},
	{PK_GROUP_ENUM_EDUCATION,		"education"},
	{PK_GROUP_ENUM_GAMES,			"games"},
	{PK_GROUP_ENUM_GRAPHICS,		"graphics"},
	{PK_GROUP_ENUM_INTERNET,		"internet"},
	{PK_GROUP_ENUM_OFFICE,			"office"},
	{PK_GROUP_ENUM_OTHER,			"other"},
	{PK_GROUP_ENUM_PROGRAMMING,		"programming"},
	{PK_GROUP_ENUM_MULTIMEDIA,		"multimedia"},
	{PK_GROUP_ENUM_SYSTEM,			"system"},
	{PK_GROUP_ENUM_DESKTOP_GNOME,		"desktop-gnome"},
	{PK_GROUP_ENUM_DESKTOP_KDE,		"desktop-kde"},
	{PK_GROUP_ENUM_DESKTOP_XFCE,		"desktop-xfce"},
	{PK_GROUP_ENUM_DESKTOP_OTHER,		"desktop-other"},
	{PK_GROUP_ENUM_PUBLISHING,		"publishing"},
	{PK_GROUP_ENUM_SERVERS,			"servers"},
	{PK_GROUP_ENUM_FONTS,			"fonts"},
	{PK_GROUP_ENUM_ADMIN_TOOLS,		"admin-tools"},
	{PK_GROUP_ENUM_LEGACY,			"legacy"},
	{PK_GROUP_ENUM_LOCALIZATION,		"localization"},
	{PK_GROUP_ENUM_VIRTUALIZATION,		"virtualization"},
	{PK_GROUP_ENUM_POWER_MANAGEMENT,	"power-management"},
	{PK_GROUP_ENUM_SECURITY,		"security"},
	{PK_GROUP_ENUM_COMMUNICATION,		"communication"},
	{PK_GROUP_ENUM_NETWORK,			"network"},
	{PK_GROUP_ENUM_MAPS,			"maps"},
	{PK_GROUP_ENUM_REPOS,			"repos"},
	{0, NULL}
};

static PkEnumMatch enum_freq[] = {
	{PK_FREQ_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_FREQ_ENUM_HOURLY,			"hourly"},
	{PK_FREQ_ENUM_DAILY,			"daily"},
	{PK_FREQ_ENUM_WEEKLY,			"weekly"},
	{PK_FREQ_ENUM_NEVER,			"never"},
	{0, NULL}
};

static PkEnumMatch enum_update[] = {
	{PK_UPDATE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_UPDATE_ENUM_ALL,			"all"},
	{PK_UPDATE_ENUM_SECURITY,		"security"},
	{PK_UPDATE_ENUM_NONE,			"none"},
	{0, NULL}
};

static PkEnumMatch enum_info[] = {
	{PK_INFO_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_INFO_ENUM_INSTALLED,		"installed"},
	{PK_INFO_ENUM_AVAILABLE,		"available"},
	{PK_INFO_ENUM_LOW,			"low"},
	{PK_INFO_ENUM_NORMAL,			"normal"},
	{PK_INFO_ENUM_IMPORTANT,		"important"},
	{PK_INFO_ENUM_SECURITY,			"security"},
	{PK_INFO_ENUM_BUGFIX,			"bugfix"},
	{PK_INFO_ENUM_ENHANCEMENT,		"enhancement"},
	{PK_INFO_ENUM_BLOCKED,			"blocked"},
	{PK_INFO_ENUM_DOWNLOADING,		"downloading"},
	{PK_INFO_ENUM_UPDATING,			"updating"},
	{PK_INFO_ENUM_INSTALLING,		"installing"},
	{PK_INFO_ENUM_REMOVING,			"removing"},
	{PK_INFO_ENUM_CLEANUP,			"cleanup"},
	{PK_INFO_ENUM_OBSOLETING,		"obsoleting"},
	{0, NULL}
};

static PkEnumMatch enum_sig_type[] = {
	{PK_SIGTYPE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_SIGTYPE_ENUM_GPG,			"gpg"},
	{0, NULL}
};

static PkEnumMatch enum_provides[] = {
	{PK_PROVIDES_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_PROVIDES_ENUM_ANY,			"any"},
	{PK_PROVIDES_ENUM_MODALIAS,		"modalias"},
	{PK_PROVIDES_ENUM_CODEC,		"codec"},
	{0, NULL}
};

static PkEnumMatch enum_free_licenses[] = {
	{PK_LICENSE_ENUM_UNKNOWN,              "unknown"},	/* fall though value */
	{PK_LICENSE_ENUM_GLIDE,                "Glide"},
	{PK_LICENSE_ENUM_AFL,                  "AFL"},
	{PK_LICENSE_ENUM_AMPAS_BSD,            "AMPAS BSD"},
	{PK_LICENSE_ENUM_AMAZON_DSL,           "ADSL"},
	{PK_LICENSE_ENUM_ADOBE,                "Adobe"},
	{PK_LICENSE_ENUM_AGPLV1,               "AGPLv1"},
	{PK_LICENSE_ENUM_AGPLV3,               "AGPLv3"},
	{PK_LICENSE_ENUM_ASL_1_DOT_0,          "ASL 1.0"},
	{PK_LICENSE_ENUM_ASL_1_DOT_1,          "ASL 1.1"},
	{PK_LICENSE_ENUM_ASL_2_DOT_0,          "ASL 2.0"},
	{PK_LICENSE_ENUM_APSL_2_DOT_0,         "APSL 2.0"},
	{PK_LICENSE_ENUM_ARTISTIC_CLARIFIED,   "Artistic clarified"},
	{PK_LICENSE_ENUM_ARTISTIC_2_DOT_0,     "Artistic 2.0"},
	{PK_LICENSE_ENUM_ARL,                  "ARL"},
	{PK_LICENSE_ENUM_BITTORRENT,           "BitTorrent"},
	{PK_LICENSE_ENUM_BOOST,                "Boost"},
	{PK_LICENSE_ENUM_BSD_WITH_ADVERTISING, "BSD with advertising"},
	{PK_LICENSE_ENUM_BSD,                  "BSD"},
	{PK_LICENSE_ENUM_CECILL,               "CeCILL"},
	{PK_LICENSE_ENUM_CDDL,                 "CDDL"},
	{PK_LICENSE_ENUM_CPL,                  "CPL"},
	{PK_LICENSE_ENUM_CONDOR,               "Condor"},
	{PK_LICENSE_ENUM_COPYRIGHT_ONLY,       "Copyright only"},
	{PK_LICENSE_ENUM_CRYPTIX,              "Cryptix"},
	{PK_LICENSE_ENUM_CRYSTAL_STACKER,      "Crystal Stacker"},
	{PK_LICENSE_ENUM_DOC,                  "DOC"},
	{PK_LICENSE_ENUM_WTFPL,                "WTFPL"},
	{PK_LICENSE_ENUM_EPL,                  "EPL"},
	{PK_LICENSE_ENUM_ECOS,                 "eCos"},
	{PK_LICENSE_ENUM_EFL_2_DOT_0,          "EFL 2.0"},
	{PK_LICENSE_ENUM_EU_DATAGRID,          "EU Datagrid"},
	{PK_LICENSE_ENUM_LGPLV2_WITH_EXCEPTIONS, "LGPLv2 with exceptions"},
	{PK_LICENSE_ENUM_FTL,                  "FTL"},
	{PK_LICENSE_ENUM_GIFTWARE,             "Giftware"},
	{PK_LICENSE_ENUM_GPLV2,                "GPLv2"},
	{PK_LICENSE_ENUM_GPLV2_WITH_EXCEPTIONS, "GPLv2 with exceptions"},
	{PK_LICENSE_ENUM_GPLV2_PLUS_WITH_EXCEPTIONS, "GPLv2+ with exceptions"},
	{PK_LICENSE_ENUM_GPLV3,                "GPLv3"},
	{PK_LICENSE_ENUM_GPLV3_WITH_EXCEPTIONS, "GPLv3 with exceptions"},
	{PK_LICENSE_ENUM_GPLV3_PLUS_WITH_EXCEPTIONS, "GPLv3+ with exceptions"},
	{PK_LICENSE_ENUM_LGPLV2,               "LGPLv2"},
	{PK_LICENSE_ENUM_LGPLV3,               "LGPLv3"},
	{PK_LICENSE_ENUM_GNUPLOT,              "gnuplot"},
	{PK_LICENSE_ENUM_IBM,                  "IBM"},
	{PK_LICENSE_ENUM_IMATIX,               "iMatix"},
	{PK_LICENSE_ENUM_IMAGEMAGICK,          "ImageMagick"},
	{PK_LICENSE_ENUM_IMLIB2,               "Imlib2"},
	{PK_LICENSE_ENUM_IJG,                  "IJG"},
	{PK_LICENSE_ENUM_INTEL_ACPI,           "Intel ACPI"},
	{PK_LICENSE_ENUM_INTERBASE,            "Interbase"},
	{PK_LICENSE_ENUM_ISC,                  "ISC"},
	{PK_LICENSE_ENUM_JABBER,               "Jabber"},
	{PK_LICENSE_ENUM_JASPER,               "JasPer"},
	{PK_LICENSE_ENUM_LPPL,                 "LPPL"},
	{PK_LICENSE_ENUM_LIBTIFF,              "libtiff"},
	{PK_LICENSE_ENUM_LPL,                  "LPL"},
	{PK_LICENSE_ENUM_MECAB_IPADIC,         "mecab-ipadic"},
	{PK_LICENSE_ENUM_MIT,                  "MIT"},
	{PK_LICENSE_ENUM_MIT_WITH_ADVERTISING, "MIT with advertising"},
	{PK_LICENSE_ENUM_MPLV1_DOT_0,          "MPLv1.0"},
	{PK_LICENSE_ENUM_MPLV1_DOT_1,          "MPLv1.1"},
	{PK_LICENSE_ENUM_NCSA,                 "NCSA"},
	{PK_LICENSE_ENUM_NGPL,                 "NGPL"},
	{PK_LICENSE_ENUM_NOSL,                 "NOSL"},
	{PK_LICENSE_ENUM_NETCDF,               "NetCDF"},
	{PK_LICENSE_ENUM_NETSCAPE,             "Netscape"},
	{PK_LICENSE_ENUM_NOKIA,                "Nokia"},
	{PK_LICENSE_ENUM_OPENLDAP,             "OpenLDAP"},
	{PK_LICENSE_ENUM_OPENPBS,              "OpenPBS"},
	{PK_LICENSE_ENUM_OSL_1_DOT_0,          "OSL 1.0"},
	{PK_LICENSE_ENUM_OSL_1_DOT_1,          "OSL 1.1"},
	{PK_LICENSE_ENUM_OSL_2_DOT_0,          "OSL 2.0"},
	{PK_LICENSE_ENUM_OSL_3_DOT_0,          "OSL 3.0"},
	{PK_LICENSE_ENUM_OPENSSL,              "OpenSSL"},
	{PK_LICENSE_ENUM_OREILLY,              "OReilly"},
	{PK_LICENSE_ENUM_PHORUM,               "Phorum"},
	{PK_LICENSE_ENUM_PHP,                  "PHP"},
	{PK_LICENSE_ENUM_PUBLIC_DOMAIN,        "Public Domain"},
	{PK_LICENSE_ENUM_PYTHON,               "Python"},
	{PK_LICENSE_ENUM_QPL,                  "QPL"},
	{PK_LICENSE_ENUM_RPSL,                 "RPSL"},
	{PK_LICENSE_ENUM_RUBY,                 "Ruby"},
	{PK_LICENSE_ENUM_SENDMAIL,             "Sendmail"},
	{PK_LICENSE_ENUM_SLEEPYCAT,            "Sleepycat"},
	{PK_LICENSE_ENUM_SLIB,                 "SLIB"},
	{PK_LICENSE_ENUM_SISSL,                "SISSL"},
	{PK_LICENSE_ENUM_SPL,                  "SPL"},
	{PK_LICENSE_ENUM_TCL,                  "TCL"},
	{PK_LICENSE_ENUM_UCD,                  "UCD"},
	{PK_LICENSE_ENUM_VIM,                  "Vim"},
	{PK_LICENSE_ENUM_VNLSL,                "VNLSL"},
	{PK_LICENSE_ENUM_VSL,                  "VSL"},
	{PK_LICENSE_ENUM_W3C,                  "W3C"},
	{PK_LICENSE_ENUM_WXWIDGETS,            "wxWidgets"},
	{PK_LICENSE_ENUM_XINETD,               "xinetd"},
	{PK_LICENSE_ENUM_ZEND,                 "Zend"},
	{PK_LICENSE_ENUM_ZPLV1_DOT_0,          "ZPLv1.0"},
	{PK_LICENSE_ENUM_ZPLV2_DOT_0,          "ZPLv2.0"},
	{PK_LICENSE_ENUM_ZPLV2_DOT_1,          "ZPLv2.1"},
	{PK_LICENSE_ENUM_ZLIB,                 "zlib"},
	{PK_LICENSE_ENUM_ZLIB_WITH_ACK,        "zlib with acknowledgement"},
	{PK_LICENSE_ENUM_CDL,                  "CDL"},
	{PK_LICENSE_ENUM_FBSDDL,               "FBSDDL"},
	{PK_LICENSE_ENUM_GFDL,                 "GFDL"},
	{PK_LICENSE_ENUM_IEEE,                 "IEEE"},
	{PK_LICENSE_ENUM_OFSFDL,               "OFSFDL"},
	{PK_LICENSE_ENUM_OPEN_PUBLICATION,     "Open Publication"},
	{PK_LICENSE_ENUM_CC_BY,                "CC-BY"},
	{PK_LICENSE_ENUM_CC_BY_SA,             "CC-BY-SA"},
	{PK_LICENSE_ENUM_CC_BY_ND,             "CC-BY-ND"},
	{PK_LICENSE_ENUM_DSL,                  "DSL"},
	{PK_LICENSE_ENUM_FREE_ART,             "Free Art"},
	{PK_LICENSE_ENUM_OFL,                  "OFL"},
	{PK_LICENSE_ENUM_UTOPIA,               "Utopia"},
	{PK_LICENSE_ENUM_ARPHIC,               "Arphic"},
	{PK_LICENSE_ENUM_BAEKMUK,              "Baekmuk"},
	{PK_LICENSE_ENUM_BITSTREAM_VERA,       "Bitstream Vera"},
	{PK_LICENSE_ENUM_LUCIDA,               "Lucida"},
	{PK_LICENSE_ENUM_MPLUS,                "mplus"},
	{PK_LICENSE_ENUM_STIX,                 "STIX"},
	{PK_LICENSE_ENUM_XANO,                 "XANO"},
	{0, NULL}
};

/**
 * pk_enum_find_value:
 * @table: A #PkEnumMatch enum table of values
 * @string: the string constant to search for, e.g. "desktop-gnome"
 *
 * Search for a string value in a table of constants.
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 */
guint
pk_enum_find_value (PkEnumMatch *table, const gchar *string)
{
	guint i;
	const gchar *string_tmp;

	/* return the first entry on non-found or error */
	if (string == NULL) {
		return table[0].value;
	}
	for (i=0;;i++) {
		string_tmp = table[i].string;
		if (string_tmp == NULL) {
			break;
		}
		/* keep strcmp for speed */
		if (strcmp (string, string_tmp) == 0) {
			return table[i].value;
		}
	}
	return table[0].value;
}

/**
 * pk_enum_find_string:
 * @table: A #PkEnumMatch enum table of values
 * @value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Search for a enum value in a table of constants.
 *
 * Return value: the string constant, e.g. "desktop-gnome"
 */
const gchar *
pk_enum_find_string (PkEnumMatch *table, guint value)
{
	guint i;
	guint tmp;
	const gchar *string_tmp;

	for (i=0;;i++) {
		string_tmp = table[i].string;
		if (string_tmp == NULL) {
			break;
		}
		tmp = table[i].value;
		if (tmp == value) {
			return table[i].string;
		}
	}
	return table[0].string;
}

/**
 * pk_sig_type_enum_from_text:
 * @sig_type: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 */
PkSigTypeEnum
pk_sig_type_enum_from_text (const gchar *sig_type)
{
	return pk_enum_find_value (enum_sig_type, sig_type);
}

/**
 * pk_sig_type_enum_to_text:
 * @sig_type: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_sig_type_enum_to_text (PkSigTypeEnum sig_type)
{
	return pk_enum_find_string (enum_sig_type, sig_type);
}

/**
 * pk_provides_enum_from_text:
 * @provides: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_PROVIDES_ENUM_MODALIAS
 */
PkProvidesEnum
pk_provides_enum_from_text (const gchar *provides)
{
	return pk_enum_find_value (enum_provides, provides);
}

/**
 * pk_provides_enum_to_text:
 * @provides: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "codec"
 **/
const gchar *
pk_provides_enum_to_text (PkProvidesEnum provides)
{
	return pk_enum_find_string (enum_provides, provides);
}

/**
 * pk_info_enum_from_text:
 * @info: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 */
PkInfoEnum
pk_info_enum_from_text (const gchar *info)
{
	return pk_enum_find_value (enum_info, info);
}

/**
 * pk_info_enum_to_text:
 * @info: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_info_enum_to_text (PkInfoEnum info)
{
	return pk_enum_find_string (enum_info, info);
}

/**
 * pk_exit_enum_from_text:
 * @exit: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 */
PkExitEnum
pk_exit_enum_from_text (const gchar *exit)
{
	return pk_enum_find_value (enum_exit, exit);
}

/**
 * pk_exit_enum_to_text:
 * @exit: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_exit_enum_to_text (PkExitEnum exit)
{
	return pk_enum_find_string (enum_exit, exit);
}

/**
 * pk_status_enum_from_text:
 * @status: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkStatusEnum
pk_status_enum_from_text (const gchar *status)
{
	return pk_enum_find_value (enum_status, status);
}

/**
 * pk_status_enum_to_text:
 * @status: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_status_enum_to_text (PkStatusEnum status)
{
	return pk_enum_find_string (enum_status, status);
}

/**
 * pk_role_enum_from_text:
 * @role: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkRoleEnum
pk_role_enum_from_text (const gchar *role)
{
	return pk_enum_find_value (enum_role, role);
}

/**
 * pk_role_enum_to_text:
 * @role: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_role_enum_to_text (PkRoleEnum role)
{
	return pk_enum_find_string (enum_role, role);
}

/**
 * pk_error_enum_from_text:
 * @code: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkErrorCodeEnum
pk_error_enum_from_text (const gchar *code)
{
	return pk_enum_find_value (enum_error, code);
}

/**
 * pk_error_enum_to_text:
 * @code: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_error_enum_to_text (PkErrorCodeEnum code)
{
	return pk_enum_find_string (enum_error, code);
}

/**
 * pk_restart_enum_from_text:
 * @restart: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkRestartEnum
pk_restart_enum_from_text (const gchar *restart)
{
	return pk_enum_find_value (enum_restart, restart);
}

/**
 * pk_restart_enum_to_text:
 * @restart: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_restart_enum_to_text (PkRestartEnum restart)
{
	return pk_enum_find_string (enum_restart, restart);
}

/**
 * pk_message_enum_from_text:
 * @message: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkMessageEnum
pk_message_enum_from_text (const gchar *message)
{
	return pk_enum_find_value (enum_message, message);
}

/**
 * pk_message_enum_to_text:
 * @message: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_message_enum_to_text (PkMessageEnum message)
{
	return pk_enum_find_string (enum_message, message);
}

/**
 * pk_group_enum_from_text:
 * @group: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkGroupEnum
pk_group_enum_from_text (const gchar *group)
{
	return pk_enum_find_value (enum_group, group);
}

/**
 * pk_group_enum_to_text:
 * @group: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_group_enum_to_text (PkGroupEnum group)
{
	return pk_enum_find_string (enum_group, group);
}

/**
 * pk_freq_enum_from_text:
 * @freq: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkFreqEnum
pk_freq_enum_from_text (const gchar *freq)
{
	return pk_enum_find_value (enum_freq, freq);
}

/**
 * pk_freq_enum_to_text:
 * @freq: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_freq_enum_to_text (PkFreqEnum freq)
{
	return pk_enum_find_string (enum_freq, freq);
}

/**
 * pk_update_enum_from_text:
 * @update: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkUpdateEnum
pk_update_enum_from_text (const gchar *update)
{
	return pk_enum_find_value (enum_update, update);
}

/**
 * pk_update_enum_to_text:
 * @update: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_update_enum_to_text (PkUpdateEnum update)
{
	return pk_enum_find_string (enum_update, update);
}

/**
 * pk_filter_enum_from_text:
 * @filter: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkFilterEnum
pk_filter_enum_from_text (const gchar *filter)
{
	return pk_enum_find_value (enum_filter, filter);
}

/**
 * pk_filter_enum_to_text:
 * @filter: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_filter_enum_to_text (PkFilterEnum filter)
{
	return pk_enum_find_string (enum_filter, filter);
}

/**
 * pk_filter_enums_to_text:
 * @filters: The enumerated type values
 *
 * Converts a enumerated type bitfield to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available;~gui"
 **/
gchar *
pk_filter_enums_to_text (PkFilterEnum filters)
{
	GString *string;
	guint i;

	/* shortcut */
	if (filters == PK_FILTER_ENUM_NONE) {
		return g_strdup (pk_filter_enum_to_text (filters));
	}

	string = g_string_new ("");
	for (i=1; i<PK_FILTER_ENUM_UNKNOWN; i*=2) {
		if ((filters & i) == 0) {
			continue;
		}
		g_string_append_printf (string, "%s;", pk_filter_enum_to_text (i));
	}
	/* do we have a 'none' filter? \n */
	if (string->len == 0) {
		pk_warning ("not valid!");
		g_string_append (string, pk_filter_enum_to_text (PK_FILTER_ENUM_NONE));
	} else {
		/* remove last \n */
		g_string_set_size (string, string->len - 1);
	}
	return g_string_free (string, FALSE);
}

/**
 * pk_license_enum_from_text:
 * @license: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 **/
PkLicenseEnum
pk_license_enum_from_text (const gchar *license)
{
	return pk_enum_find_value (enum_free_licenses, license);
}

/**
 * pk_license_enum_to_text:
 * @license: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_license_enum_to_text (PkLicenseEnum license)
{
	return pk_enum_find_string (enum_free_licenses, license);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef PK_BUILD_TESTS
#include <libselftest.h>

void
libst_enum (LibSelfTest *test)
{
	const gchar *string;
	PkRoleEnum value;
	guint i;
	gchar *text;
	PkFilterEnum filter;

	if (libst_start (test, "PkEnum", CLASS_AUTO) == FALSE) {
		return;
	}

	/************************************************************/
	libst_title (test, "find value");
	value = pk_enum_find_value (enum_role, "search-file");
	if (PK_ROLE_ENUM_SEARCH_FILE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "find string");
	string = pk_enum_find_string (enum_role, PK_ROLE_ENUM_SEARCH_FILE);
	if (pk_strequal (string, "search-file")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "find value");
	value = pk_role_enum_from_text ("search-file");
	if (PK_ROLE_ENUM_SEARCH_FILE) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "find string");
	string = pk_role_enum_to_text (PK_ROLE_ENUM_SEARCH_FILE);
	if (pk_strequal (string, "search-file")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, NULL);
	}

	/************************************************************/
	libst_title (test, "check we convert all the role enums");
	for (i=0; i<=PK_ROLE_ENUM_UNKNOWN; i++) {
		string = pk_role_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the status enums");
	for (i=0; i<=PK_STATUS_ENUM_UNKNOWN; i++) {
		string = pk_status_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the exit enums");
	for (i=0; i<=PK_EXIT_ENUM_UNKNOWN; i++) {
		string = pk_exit_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the filter enums");
	for (i=0; i<=PK_FILTER_ENUM_UNKNOWN; i++) {
		string = pk_filter_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the restart enums");
	for (i=0; i<=PK_RESTART_ENUM_UNKNOWN; i++) {
		string = pk_restart_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the error_code enums");
	for (i=0; i<=PK_ERROR_ENUM_UNKNOWN; i++) {
		string = pk_error_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the group enums");
	for (i=0; i<=PK_GROUP_ENUM_UNKNOWN; i++) {
		string = pk_group_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the freq enums");
	for (i=0; i<=PK_FREQ_ENUM_UNKNOWN; i++) {
		string = pk_freq_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the update enums");
	for (i=0; i<=PK_UPDATE_ENUM_UNKNOWN; i++) {
		string = pk_update_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the info enums");
	for (i=0; i<=PK_INFO_ENUM_UNKNOWN; i++) {
		string = pk_info_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the sig_type enums");
	for (i=0; i<=PK_SIGTYPE_ENUM_UNKNOWN; i++) {
		string = pk_sig_type_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we convert all the license enums");
	for (i=0; i<=PK_LICENSE_ENUM_UNKNOWN; i++) {
		string = pk_license_enum_to_text (i);
		if (string == NULL) {
			libst_failed (test, "failed to get %i", i);
			break;
		}
	}
	libst_success (test, NULL);

	/************************************************************/
	libst_title (test, "check we can convert filter enums to text (none)");
	text = pk_filter_enums_to_text (PK_FILTER_ENUM_NONE);
	if (pk_strequal (text, "none")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can convert filter enums to text (single)");
	text = pk_filter_enums_to_text (PK_FILTER_ENUM_NOT_DEVELOPMENT);
	if (pk_strequal (text, "~devel")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can convert filter enums to text (plural)");
	text = pk_filter_enums_to_text (PK_FILTER_ENUM_NOT_DEVELOPMENT | PK_FILTER_ENUM_GUI | PK_FILTER_ENUM_NEWEST);
	if (pk_strequal (text, "~devel;gui;newest")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can add / remove enums");
	filter = PK_FILTER_ENUM_NOT_DEVELOPMENT | PK_FILTER_ENUM_GUI | PK_FILTER_ENUM_NEWEST;
	pk_enums_add (filter, PK_FILTER_ENUM_NOT_FREE);
	pk_enums_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_enums_to_text (filter);
	if (pk_strequal (text, "gui;~free;newest")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	/************************************************************/
	libst_title (test, "check we can add / remove enums to nothing");
	filter = PK_FILTER_ENUM_NOT_DEVELOPMENT;
	pk_enums_remove (filter, PK_FILTER_ENUM_NOT_DEVELOPMENT);
	text = pk_filter_enums_to_text (filter);
	if (pk_strequal (text, "none")) {
		libst_success (test, NULL);
	} else {
		libst_failed (test, "text was %s", text);
	}
	g_free (text);

	libst_end (test);
}
#endif

