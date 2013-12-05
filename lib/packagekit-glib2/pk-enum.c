/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2010 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:pk-enum
 * @short_description: Functions for converting strings to enum and vice-versa
 *
 * This file contains functions to convert to and from enumerated types.
 */

#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <packagekit-glib2/pk-common.h>
#include <packagekit-glib2/pk-enum.h>

static const PkEnumMatch enum_exit[] = {
	{PK_EXIT_ENUM_UNKNOWN,			"unknown"},	/* fall though value */
	{PK_EXIT_ENUM_SUCCESS,			"success"},
	{PK_EXIT_ENUM_FAILED,			"failed"},
	{PK_EXIT_ENUM_CANCELLED,		"cancelled"},
	{PK_EXIT_ENUM_KEY_REQUIRED,		"key-required"},
	{PK_EXIT_ENUM_EULA_REQUIRED,		"eula-required"},
	{PK_EXIT_ENUM_MEDIA_CHANGE_REQUIRED,	"media-change-required"},
	{PK_EXIT_ENUM_KILLED,			"killed"},
	{PK_EXIT_ENUM_NEED_UNTRUSTED,		"need-untrusted"},
	{PK_EXIT_ENUM_CANCELLED_PRIORITY,	"cancelled-priority"},
	{PK_EXIT_ENUM_SKIP_TRANSACTION,		"skip-transaction"},
	{PK_EXIT_ENUM_REPAIR_REQUIRED,		"repair-required"},
	{0, NULL}
};

static const PkEnumMatch enum_status[] = {
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
	{PK_STATUS_ENUM_TEST_COMMIT,		"test-commit"},
	{PK_STATUS_ENUM_COMMIT,			"commit"},
	{PK_STATUS_ENUM_REQUEST,		"request"},
	{PK_STATUS_ENUM_FINISHED,		"finished"},
	{PK_STATUS_ENUM_CANCEL,			"cancel"},
	{PK_STATUS_ENUM_DOWNLOAD_REPOSITORY,	"download-repository"},
	{PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST,	"download-packagelist"},
	{PK_STATUS_ENUM_DOWNLOAD_FILELIST,	"download-filelist"},
	{PK_STATUS_ENUM_DOWNLOAD_CHANGELOG,	"download-changelog"},
	{PK_STATUS_ENUM_DOWNLOAD_GROUP,		"download-group"},
	{PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO,	"download-updateinfo"},
	{PK_STATUS_ENUM_REPACKAGING,		"repackaging"},
	{PK_STATUS_ENUM_LOADING_CACHE,		"loading-cache"},
	{PK_STATUS_ENUM_SCAN_APPLICATIONS,	"scan-applications"},
	{PK_STATUS_ENUM_GENERATE_PACKAGE_LIST,	"generate-package-list"},
	{PK_STATUS_ENUM_WAITING_FOR_LOCK,	"waiting-for-lock"},
	{PK_STATUS_ENUM_WAITING_FOR_AUTH,	"waiting-for-auth"},
	{PK_STATUS_ENUM_SCAN_PROCESS_LIST,	"scan-process-list"},
	{PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES,	"check-executable-files"},
	{PK_STATUS_ENUM_CHECK_LIBRARIES,	"check-libraries"},
	{PK_STATUS_ENUM_COPY_FILES,		"copy-files"},
	{0, NULL}
};

static const PkEnumMatch enum_role[] = {
	{PK_ROLE_ENUM_UNKNOWN,				"unknown"},	/* fall though value */
	{PK_ROLE_ENUM_CANCEL,				"cancel"},
	{PK_ROLE_ENUM_GET_DEPENDS,			"get-depends"},
	{PK_ROLE_ENUM_GET_DETAILS,			"get-details"},
	{PK_ROLE_ENUM_GET_FILES,			"get-files"},
	{PK_ROLE_ENUM_GET_PACKAGES,			"get-packages"},
	{PK_ROLE_ENUM_GET_REPO_LIST,			"get-repo-list"},
	{PK_ROLE_ENUM_GET_REQUIRES,			"get-requires"},
	{PK_ROLE_ENUM_GET_UPDATE_DETAIL,		"get-update-detail"},
	{PK_ROLE_ENUM_GET_UPDATES,			"get-updates"},
	{PK_ROLE_ENUM_INSTALL_FILES,			"install-files"},
	{PK_ROLE_ENUM_INSTALL_PACKAGES,			"install-packages"},
	{PK_ROLE_ENUM_INSTALL_SIGNATURE,		"install-signature"},
	{PK_ROLE_ENUM_REFRESH_CACHE,			"refresh-cache"},
	{PK_ROLE_ENUM_REMOVE_PACKAGES,			"remove-packages"},
	{PK_ROLE_ENUM_REPO_ENABLE,			"repo-enable"},
	{PK_ROLE_ENUM_REPO_SET_DATA,			"repo-set-data"},
	{PK_ROLE_ENUM_RESOLVE,				"resolve"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,			"search-details"},
	{PK_ROLE_ENUM_SEARCH_FILE,			"search-file"},
	{PK_ROLE_ENUM_SEARCH_GROUP,			"search-group"},
	{PK_ROLE_ENUM_SEARCH_NAME,			"search-name"},
	{PK_ROLE_ENUM_UPDATE_PACKAGES,			"update-packages"},
	{PK_ROLE_ENUM_WHAT_PROVIDES,			"what-provides"},
	{PK_ROLE_ENUM_ACCEPT_EULA,			"accept-eula"},
	{PK_ROLE_ENUM_DOWNLOAD_PACKAGES,		"download-packages"},
	{PK_ROLE_ENUM_GET_DISTRO_UPGRADES,		"get-distro-upgrades"},
	{PK_ROLE_ENUM_GET_CATEGORIES,			"get-categories"},
	{PK_ROLE_ENUM_GET_OLD_TRANSACTIONS,		"get-old-transactions"},
	{PK_ROLE_ENUM_REPAIR_SYSTEM,			"repair-system"},
	{0, NULL}
};

static const PkEnumMatch enum_error[] = {
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
	{PK_ERROR_ENUM_GROUP_LIST_INVALID,	"group-list-invalid"},
	{PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,	"dep-resolution-failed"},
	{PK_ERROR_ENUM_CREATE_THREAD_FAILED,	"create-thread-failed"},
	{PK_ERROR_ENUM_REPO_NOT_FOUND,		"repo-not-found"},
	{PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,	"cannot-remove-system-package"},
	{PK_ERROR_ENUM_PROCESS_KILL,		"process-kill"},
	{PK_ERROR_ENUM_FAILED_INITIALIZATION,	"failed-initialization"},
	{PK_ERROR_ENUM_FAILED_FINALISE,		"failed-finalise"},
	{PK_ERROR_ENUM_FAILED_CONFIG_PARSING,	"failed-config-parsing"},
	{PK_ERROR_ENUM_CANNOT_CANCEL,		"cannot-cancel"},
	{PK_ERROR_ENUM_CANNOT_GET_LOCK,		"cannot-get-lock"},
	{PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE,	"no-packages-to-update"},
	{PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG, "cannot-write-repo-config"},
	{PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,	"local-install-failed"},
	{PK_ERROR_ENUM_BAD_GPG_SIGNATURE,	"bad-gpg-signature"},
	{PK_ERROR_ENUM_MISSING_GPG_SIGNATURE,	"missing-gpg-signature"},
	{PK_ERROR_ENUM_CANNOT_INSTALL_SOURCE_PACKAGE,	"cannot-install-source-package"},
	{PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,	"repo-configuration-error"},
	{PK_ERROR_ENUM_NO_LICENSE_AGREEMENT,	"no-license-agreement"},
	{PK_ERROR_ENUM_FILE_CONFLICTS,		"file-conflicts"},
	{PK_ERROR_ENUM_PACKAGE_CONFLICTS,	"package-conflicts"},
	{PK_ERROR_ENUM_REPO_NOT_AVAILABLE,	"repo-not-available"},
	{PK_ERROR_ENUM_INVALID_PACKAGE_FILE,	"invalid-package-file"},
	{PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED, "package-install-blocked"},
	{PK_ERROR_ENUM_PACKAGE_CORRUPT,		"package-corrupt"},
	{PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED, "all-packages-already-installed"},
	{PK_ERROR_ENUM_FILE_NOT_FOUND,		"file-not-found"},
	{PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY,	"no-more-mirrors-to-try"},
	{PK_ERROR_ENUM_NO_DISTRO_UPGRADE_DATA,	"no-distro-upgrade-data"},
	{PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE,	"incompatible-architecture"},
	{PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,	"no-space-on-device"},
	{PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED,	"media-change-required"},
	{PK_ERROR_ENUM_NOT_AUTHORIZED,		"not-authorized"},
	{PK_ERROR_ENUM_UPDATE_NOT_FOUND,	"update-not-found"},
	{PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,	"cannot-install-repo-unsigned"},
	{PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED,	"cannot-update-repo-unsigned"},
	{PK_ERROR_ENUM_CANNOT_GET_FILELIST, "cannot-get-filelist"},
	{PK_ERROR_ENUM_CANNOT_GET_REQUIRES, "cannot-get-requires"},
	{PK_ERROR_ENUM_CANNOT_DISABLE_REPOSITORY, "cannot-disable-repository"},
	{PK_ERROR_ENUM_RESTRICTED_DOWNLOAD, "restricted-download"},
	{PK_ERROR_ENUM_PACKAGE_FAILED_TO_CONFIGURE, "package-failed-to-configure"},
	{PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD, "package-failed-to-build"},
	{PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL, "package-failed-to-install"},
	{PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE, "package-failed-to-remove"},
	{PK_ERROR_ENUM_UPDATE_FAILED_DUE_TO_RUNNING_PROCESS, "failed-due-to-running-process"},
	{PK_ERROR_ENUM_PACKAGE_DATABASE_CHANGED, "package-database-changed"},
	{PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED, "provide-type-not-supported"},
	{PK_ERROR_ENUM_INSTALL_ROOT_INVALID,	"install-root-invalid"},
	{PK_ERROR_ENUM_CANNOT_FETCH_SOURCES,	"cannot-fetch-sources"},
	{PK_ERROR_ENUM_CANCELLED_PRIORITY,	"cancelled-priority"},
	{PK_ERROR_ENUM_UNFINISHED_TRANSACTION,	"unfinished-transaction"},
	{PK_ERROR_ENUM_LOCK_REQUIRED,		"lock-required"},
	{0, NULL}
};

static const PkEnumMatch enum_restart[] = {
	{PK_RESTART_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_RESTART_ENUM_NONE,			"none"},
	{PK_RESTART_ENUM_SYSTEM,		"system"},
	{PK_RESTART_ENUM_SESSION,		"session"},
	{PK_RESTART_ENUM_APPLICATION,		"application"},
	{PK_RESTART_ENUM_SECURITY_SYSTEM,	"security-system"},
	{PK_RESTART_ENUM_SECURITY_SESSION,	"security-session"},
	{0, NULL}
};

static const PkEnumMatch enum_filter[] = {
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
	{PK_FILTER_ENUM_ARCH,			"arch"},
	{PK_FILTER_ENUM_NOT_ARCH,		"~arch"},
	{PK_FILTER_ENUM_SOURCE,			"source"},
	{PK_FILTER_ENUM_NOT_SOURCE,		"~source"},
	{PK_FILTER_ENUM_COLLECTIONS,		"collections"},
	{PK_FILTER_ENUM_NOT_COLLECTIONS,	"~collections"},
	{PK_FILTER_ENUM_APPLICATION,		"application"},
	{PK_FILTER_ENUM_NOT_APPLICATION,	"~application"},
	{PK_FILTER_ENUM_DOWNLOADED,		"downloaded"},
	{PK_FILTER_ENUM_NOT_DOWNLOADED,		"~downloaded"},
	{0, NULL}
};

static const PkEnumMatch enum_group[] = {
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
	{PK_GROUP_ENUM_SCIENCE,			"science"},
	{PK_GROUP_ENUM_DOCUMENTATION,		"documentation"},
	{PK_GROUP_ENUM_ELECTRONICS,		"electronics"},
	{PK_GROUP_ENUM_COLLECTIONS,		"collections"},
	{PK_GROUP_ENUM_VENDOR,			"vendor"},
	{PK_GROUP_ENUM_NEWEST,			"newest"},
	{0, NULL}
};

static const PkEnumMatch enum_update_state[] = {
	{PK_UPDATE_STATE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_UPDATE_STATE_ENUM_TESTING,		"testing"},
	{PK_UPDATE_STATE_ENUM_UNSTABLE,		"unstable"},
	{PK_UPDATE_STATE_ENUM_STABLE,		"stable"},
	{0, NULL}
};

static const PkEnumMatch enum_info[] = {
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
	{PK_INFO_ENUM_COLLECTION_INSTALLED,	"collection-installed"},
	{PK_INFO_ENUM_COLLECTION_AVAILABLE,	"collection-available"},
	{PK_INFO_ENUM_FINISHED,			"finished"},
	{PK_INFO_ENUM_REINSTALLING,		"reinstalling"},
	{PK_INFO_ENUM_DOWNGRADING,		"downgrading"},
	{PK_INFO_ENUM_PREPARING,		"preparing"},
	{PK_INFO_ENUM_DECOMPRESSING,		"decompressing"},
	{PK_INFO_ENUM_UNTRUSTED,			"untrusted"},
	{PK_INFO_ENUM_TRUSTED,				"trusted"},
	{0, NULL}
};

static const PkEnumMatch enum_sig_type[] = {
	{PK_SIGTYPE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_SIGTYPE_ENUM_GPG,			"gpg"},
	{0, NULL}
};

static const PkEnumMatch enum_upgrade[] = {
	{PK_DISTRO_UPGRADE_ENUM_UNKNOWN,	"unknown"},	/* fall though value */
	{PK_DISTRO_UPGRADE_ENUM_STABLE,		"stable"},
	{PK_DISTRO_UPGRADE_ENUM_UNSTABLE,		"unstable"},
	{0, NULL}
};

static const PkEnumMatch enum_provides[] = {
	{PK_PROVIDES_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_PROVIDES_ENUM_ANY,			"any"},
	{PK_PROVIDES_ENUM_MODALIAS,		"modalias"},
	{PK_PROVIDES_ENUM_CODEC,		"codec"},
	{PK_PROVIDES_ENUM_MIMETYPE,		"mimetype"},
	{PK_PROVIDES_ENUM_HARDWARE_DRIVER,	"driver"},
	{PK_PROVIDES_ENUM_FONT,			"font"},
	{PK_PROVIDES_ENUM_POSTSCRIPT_DRIVER,	"postscript-driver"},
	{PK_PROVIDES_ENUM_PLASMA_SERVICE,	"plasma-service"},
	{PK_PROVIDES_ENUM_SHARED_LIB,		"shared-library"},
	{PK_PROVIDES_ENUM_PYTHON,		"python-module"},
	{PK_PROVIDES_ENUM_LANGUAGE_SUPPORT,     "language-support"},
	{0, NULL}
};

static const PkEnumMatch enum_network[] = {
	{PK_NETWORK_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_NETWORK_ENUM_OFFLINE,		"offline"},
	{PK_NETWORK_ENUM_ONLINE,		"online"},
	{PK_NETWORK_ENUM_WIRED,			"wired"},
	{PK_NETWORK_ENUM_WIFI,			"wifi"},
	{PK_NETWORK_ENUM_MOBILE,		"mobile"},
	{0, NULL}
};

static const PkEnumMatch enum_media_type[] = {
	{PK_MEDIA_TYPE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_MEDIA_TYPE_ENUM_CD,			"cd"},
	{PK_MEDIA_TYPE_ENUM_DVD,		"dvd"},
	{PK_MEDIA_TYPE_ENUM_DISC,		"disc"},
	{0, NULL}
};

static const PkEnumMatch enum_authorize_type[] = {
	{PK_AUTHORIZE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_AUTHORIZE_ENUM_YES,			"yes"},
	{PK_AUTHORIZE_ENUM_NO,			"no"},
	{PK_AUTHORIZE_ENUM_INTERACTIVE,		"interactive"},
	{0, NULL}
};

static const PkEnumMatch enum_upgrade_kind[] = {
	{PK_UPGRADE_KIND_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_UPGRADE_KIND_ENUM_MINIMAL,		"minimal"},
	{PK_UPGRADE_KIND_ENUM_DEFAULT,		"default"},
	{PK_UPGRADE_KIND_ENUM_COMPLETE,		"complete"},
	{0, NULL}
};

static const PkEnumMatch enum_transaction_flag[] = {
	{PK_TRANSACTION_FLAG_ENUM_NONE,		"none"},	/* fall though value */
	{PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED,	"only-trusted"},
	{PK_TRANSACTION_FLAG_ENUM_SIMULATE,	"simulate"},
	{PK_TRANSACTION_FLAG_ENUM_ONLY_DOWNLOAD, "only-download"},
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
pk_enum_find_value (const PkEnumMatch *table, const gchar *string)
{
	guint i;
	const gchar *string_tmp;

	/* return the first entry on non-found or error */
	if (string == NULL) {
		return table[0].value;
	}
	for (i=0;;i++) {
		string_tmp = table[i].string;
		if (string_tmp == NULL)
			break;
		/* keep strcmp for speed */
		if (strcmp (string, string_tmp) == 0)
			return table[i].value;
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
pk_enum_find_string (const PkEnumMatch *table, guint value)
{
	guint i;
	guint tmp;
	const gchar *string_tmp;

	for (i=0;;i++) {
		string_tmp = table[i].string;
		if (string_tmp == NULL)
			break;
		tmp = table[i].value;
		if (tmp == value)
			return table[i].string;
	}
	return table[0].string;
}

/**
 * pk_sig_type_enum_from_string:
 * @sig_type: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 */
PkSigTypeEnum
pk_sig_type_enum_from_string (const gchar *sig_type)
{
	return pk_enum_find_value (enum_sig_type, sig_type);
}

/**
 * pk_sig_type_enum_to_string:
 * @sig_type: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_sig_type_enum_to_string (PkSigTypeEnum sig_type)
{
	return pk_enum_find_string (enum_sig_type, sig_type);
}

/**
 * pk_distro_upgrade_enum_from_string:
 * @upgrade: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_DISTRO_UPGRADE_ENUM_STABLE
 *
 * Since: 0.5.0
 **/
PkDistroUpgradeEnum
pk_distro_upgrade_enum_from_string (const gchar *upgrade)
{
	return pk_enum_find_value (enum_upgrade, upgrade);
}

/**
 * pk_distro_upgrade_enum_to_string:
 * @upgrade: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "stable"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_distro_upgrade_enum_to_string (PkDistroUpgradeEnum upgrade)
{
	return pk_enum_find_string (enum_upgrade, upgrade);
}

/**
 * pk_provides_enum_from_string:
 * @provides: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_PROVIDES_ENUM_MODALIAS
 *
 * Since: 0.5.0
 **/
PkProvidesEnum
pk_provides_enum_from_string (const gchar *provides)
{
	return pk_enum_find_value (enum_provides, provides);
}

/**
 * pk_provides_enum_to_string:
 * @provides: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "codec"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_provides_enum_to_string (PkProvidesEnum provides)
{
	return pk_enum_find_string (enum_provides, provides);
}

/**
 * pk_info_enum_from_string:
 * @info: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkInfoEnum
pk_info_enum_from_string (const gchar *info)
{
	return pk_enum_find_value (enum_info, info);
}

/**
 * pk_info_enum_to_string:
 * @info: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_info_enum_to_string (PkInfoEnum info)
{
	return pk_enum_find_string (enum_info, info);
}

/**
 * pk_exit_enum_from_string:
 * @exit: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkExitEnum
pk_exit_enum_from_string (const gchar *exit_text)
{
	return pk_enum_find_value (enum_exit, exit_text);
}

/**
 * pk_exit_enum_to_string:
 * @exit: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_exit_enum_to_string (PkExitEnum exit_enum)
{
	return pk_enum_find_string (enum_exit, exit_enum);
}

/**
 * pk_network_enum_from_string:
 * @network: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkNetworkEnum
pk_network_enum_from_string (const gchar *network)
{
	return pk_enum_find_value (enum_network, network);
}

/**
 * pk_network_enum_to_string:
 * @network: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_network_enum_to_string (PkNetworkEnum network)
{
	return pk_enum_find_string (enum_network, network);
}

/**
 * pk_status_enum_from_string:
 * @status: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkStatusEnum
pk_status_enum_from_string (const gchar *status)
{
	return pk_enum_find_value (enum_status, status);
}

/**
 * pk_status_enum_to_string:
 * @status: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_status_enum_to_string (PkStatusEnum status)
{
	return pk_enum_find_string (enum_status, status);
}

/**
 * pk_role_enum_from_string:
 * @role: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkRoleEnum
pk_role_enum_from_string (const gchar *role)
{
	return pk_enum_find_value (enum_role, role);
}

/**
 * pk_role_enum_to_string:
 * @role: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_role_enum_to_string (PkRoleEnum role)
{
	return pk_enum_find_string (enum_role, role);
}

/**
 * pk_error_enum_from_string:
 * @code: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkErrorEnum
pk_error_enum_from_string (const gchar *code)
{
	return pk_enum_find_value (enum_error, code);
}

/**
 * pk_error_enum_to_string:
 * @code: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_error_enum_to_string (PkErrorEnum code)
{
	return pk_enum_find_string (enum_error, code);
}

/**
 * pk_restart_enum_from_string:
 * @restart: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkRestartEnum
pk_restart_enum_from_string (const gchar *restart)
{
	return pk_enum_find_value (enum_restart, restart);
}

/**
 * pk_restart_enum_to_string:
 * @restart: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_restart_enum_to_string (PkRestartEnum restart)
{
	return pk_enum_find_string (enum_restart, restart);
}

/**
 * pk_group_enum_from_string:
 * @group: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkGroupEnum
pk_group_enum_from_string (const gchar *group)
{
	return pk_enum_find_value (enum_group, group);
}

/**
 * pk_group_enum_to_string:
 * @group: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_group_enum_to_string (PkGroupEnum group)
{
	return pk_enum_find_string (enum_group, group);
}

/**
 * pk_update_state_enum_from_string:
 * @update_state: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. %PK_UPDATE_STATE_ENUM_STABLE
 *
 * Since: 0.5.0
 **/
PkUpdateStateEnum
pk_update_state_enum_from_string (const gchar *update_state)
{
	return pk_enum_find_value (enum_update_state, update_state);
}

/**
 * pk_update_state_enum_to_string:
 * @update_state: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "testing"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_update_state_enum_to_string (PkUpdateStateEnum update_state)
{
	return pk_enum_find_string (enum_update_state, update_state);
}

/**
 * pk_filter_enum_from_string:
 * @filter: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 *
 * Since: 0.5.0
 **/
PkFilterEnum
pk_filter_enum_from_string (const gchar *filter)
{
	return pk_enum_find_value (enum_filter, filter);
}

/**
 * pk_filter_enum_to_string:
 * @filter: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_filter_enum_to_string (PkFilterEnum filter)
{
	return pk_enum_find_string (enum_filter, filter);
}

/**
 * pk_media_type_enum_from_string:
 * @media_type: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_MEDIA_TYPE_ENUM_CD
 *
 * Since: 0.5.0
 **/
PkMediaTypeEnum
pk_media_type_enum_from_string (const gchar *media_type)
{
	return pk_enum_find_value (enum_media_type, media_type);
}

/**
 * pk_media_type_enum_to_string:
 * @media_type: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "dvd"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_media_type_enum_to_string (PkMediaTypeEnum media_type)
{
	return pk_enum_find_string (enum_media_type, media_type);
}

/**
 * pk_authorize_type_enum_from_string:
 * @authorize_type: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. %PK_AUTHORIZE_ENUM_YES
 *
 * Since: 0.5.0
 **/
PkAuthorizeEnum
pk_authorize_type_enum_from_string (const gchar *authorize_type)
{
	return pk_enum_find_value (enum_authorize_type, authorize_type);
}

/**
 * pk_authorize_type_enum_to_string:
 * @authorize_type: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "yes"
 *
 * Since: 0.5.0
 **/
const gchar *
pk_authorize_type_enum_to_string (PkAuthorizeEnum authorize_type)
{
	return pk_enum_find_string (enum_authorize_type, authorize_type);
}

/**
 * pk_upgrade_kind_enum_from_string:
 * @upgrade_kind: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. %PK_UPGRADE_KIND_ENUM_MINIMAL
 *
 * Since: 0.6.11
 **/
PkUpgradeKindEnum
pk_upgrade_kind_enum_from_string (const gchar *upgrade_kind)
{
	return pk_enum_find_value (enum_upgrade_kind, upgrade_kind);
}

/**
 * pk_upgrade_kind_enum_to_string:
 * @upgrade_kind: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "minimal"
 *
 * Since: 0.6.11
 **/
const gchar *
pk_upgrade_kind_enum_to_string (PkUpgradeKindEnum upgrade_kind)
{
	return pk_enum_find_string (enum_upgrade_kind, upgrade_kind);
}

/**
 * pk_transaction_flag_enum_from_string:
 * @transaction_flag: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. %PK_TRANSACTION_FLAG_ENUM_SIMULATE
 *
 * Since: 0.8.1
 **/
PkTransactionFlagEnum
pk_transaction_flag_enum_from_string (const gchar *transaction_flag)
{
	return pk_enum_find_value (enum_transaction_flag, transaction_flag);
}

/**
 * pk_transaction_flag_enum_to_string:
 * @transaction_flag: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "simulate"
 *
 * Since: 0.8.1
 **/
const gchar *
pk_transaction_flag_enum_to_string (PkTransactionFlagEnum transaction_flag)
{
	return pk_enum_find_string (enum_transaction_flag, transaction_flag);
}

/**
 * pk_info_enum_to_localised_text:
 * @info: The enumerated type value
 *
 * Converts a enumerated type to its localized description
 *
 * Return Value: the translated text
 *
 * Since: 0.7.2
 **/
static const gchar *
pk_info_enum_to_localised_text (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_LOW:
		/* TRANSLATORS: The type of update */
		text = _("Trivial");
		break;
	case PK_INFO_ENUM_NORMAL:
		/* TRANSLATORS: The type of update */
		text = dgettext("PackageKit", "Normal");
		break;
	case PK_INFO_ENUM_IMPORTANT:
		/* TRANSLATORS: The type of update */
		text = dgettext("PackageKit", "Important");
		break;
	case PK_INFO_ENUM_SECURITY:
		/* TRANSLATORS: The type of update */
		text = dgettext("PackageKit", "Security");
		break;
	case PK_INFO_ENUM_BUGFIX:
		/* TRANSLATORS: The type of update */
		text = dgettext("PackageKit", "Bug fix ");
		break;
	case PK_INFO_ENUM_ENHANCEMENT:
		/* TRANSLATORS: The type of update */
		text = dgettext("PackageKit", "Enhancement");
		break;
	case PK_INFO_ENUM_BLOCKED:
		/* TRANSLATORS: The type of update */
		text = dgettext("PackageKit", "Blocked");
		break;
	case PK_INFO_ENUM_INSTALLED:
	case PK_INFO_ENUM_COLLECTION_INSTALLED:
		/* TRANSLATORS: The state of a package */
		text = dgettext("PackageKit", "Installed");
		break;
	case PK_INFO_ENUM_AVAILABLE:
	case PK_INFO_ENUM_COLLECTION_AVAILABLE:
		/* TRANSLATORS: The state of a package, i.e. not installed */
		text = dgettext("PackageKit", "Available");
		break;
	default:
		g_warning ("info unrecognised: %s", pk_info_enum_to_string (info));
	}
	return text;
}

/**
 * pk_info_enum_to_localised_present:
 * @info: The enumerated type value
 *
 * Converts a enumerated type to its localized description
 *
 * Return Value: the translated text
 *
 * Since: 0.7.2
 **/
const gchar *
pk_info_enum_to_localised_present (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_DOWNLOADING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Downloading");
		break;
	case PK_INFO_ENUM_UPDATING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Updating");
		break;
	case PK_INFO_ENUM_INSTALLING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Installing");
		break;
	case PK_INFO_ENUM_REMOVING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Removing");
		break;
	case PK_INFO_ENUM_CLEANUP:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Cleaning up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Obsoleting");
		break;
	case PK_INFO_ENUM_REINSTALLING:
		/* TRANSLATORS: The action of the package, in present tense */
		text = dgettext("PackageKit", "Reinstalling");
		break;
	default:
		text = pk_info_enum_to_localised_text (info);
	}
	return text;
}

/**
 * pk_info_enum_to_localised_past:
 * @info: The enumerated type value
 *
 * Converts a enumerated type to its localized description
 *
 * Return Value: the translated text
 *
 * Since: 0.7.2
 **/
const gchar *
pk_info_enum_to_localised_past (PkInfoEnum info)
{
	const gchar *text = NULL;
	switch (info) {
	case PK_INFO_ENUM_DOWNLOADING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Downloaded");
		break;
	case PK_INFO_ENUM_UPDATING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Updated");
		break;
	case PK_INFO_ENUM_INSTALLING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Installed");
		break;
	case PK_INFO_ENUM_REMOVING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Removed");
		break;
	case PK_INFO_ENUM_CLEANUP:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Cleaned up");
		break;
	case PK_INFO_ENUM_OBSOLETING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Obsoleted");
		break;
	case PK_INFO_ENUM_REINSTALLING:
		/* TRANSLATORS: The action of the package, in past tense */
		text = dgettext("PackageKit", "Reinstalled");
		break;
	default:
		text = pk_info_enum_to_localised_text (info);
	}
	return text;
}

/**
 * pk_role_enum_to_localised_present:
 * @role: The enumerated type value
 *
 * Converts a enumerated type to its localized description
 *
 * Return Value: the translated text
 *
 * Since: 0.7.2
 **/
const gchar *
pk_role_enum_to_localised_present (PkRoleEnum role)
{
	const gchar *text = NULL;
	switch (role) {
	case PK_ROLE_ENUM_UNKNOWN:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Unknown role type");
		break;
	case PK_ROLE_ENUM_GET_DEPENDS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting dependencies");
		break;
	case PK_ROLE_ENUM_GET_UPDATE_DETAIL:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting update details");
		break;
	case PK_ROLE_ENUM_GET_DETAILS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting details");
		break;
	case PK_ROLE_ENUM_GET_REQUIRES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting requires");
		break;
	case PK_ROLE_ENUM_GET_UPDATES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting updates");
		break;
	case PK_ROLE_ENUM_SEARCH_DETAILS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Searching by details");
		break;
	case PK_ROLE_ENUM_SEARCH_FILE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Searching by file");
		break;
	case PK_ROLE_ENUM_SEARCH_GROUP:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Searching groups");
		break;
	case PK_ROLE_ENUM_SEARCH_NAME:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Searching by name");
		break;
	case PK_ROLE_ENUM_REMOVE_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Removing");
		break;
	case PK_ROLE_ENUM_INSTALL_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Installing");
		break;
	case PK_ROLE_ENUM_INSTALL_FILES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Installing files");
		break;
	case PK_ROLE_ENUM_REFRESH_CACHE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Refreshing cache");
		break;
	case PK_ROLE_ENUM_UPDATE_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Updating packages");
		break;
	case PK_ROLE_ENUM_CANCEL:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Canceling");
		break;
	case PK_ROLE_ENUM_GET_REPO_LIST:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting repositories");
		break;
	case PK_ROLE_ENUM_REPO_ENABLE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Enabling repository");
		break;
	case PK_ROLE_ENUM_REPO_SET_DATA:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Setting data");
		break;
	case PK_ROLE_ENUM_RESOLVE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Resolving");
		break;
	case PK_ROLE_ENUM_GET_FILES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting file list");
		break;
	case PK_ROLE_ENUM_WHAT_PROVIDES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting provides");
		break;
	case PK_ROLE_ENUM_INSTALL_SIGNATURE:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Installing signature");
		break;
	case PK_ROLE_ENUM_GET_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting packages");
		break;
	case PK_ROLE_ENUM_ACCEPT_EULA:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Accepting EULA");
		break;
	case PK_ROLE_ENUM_DOWNLOAD_PACKAGES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Downloading packages");
		break;
	case PK_ROLE_ENUM_GET_DISTRO_UPGRADES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting upgrades");
		break;
	case PK_ROLE_ENUM_GET_CATEGORIES:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting categories");
		break;
	case PK_ROLE_ENUM_GET_OLD_TRANSACTIONS:
		/* TRANSLATORS: The role of the transaction, in present tense */
		text = dgettext("PackageKit", "Getting transactions");
		break;
	default:
		g_warning ("role unrecognised: %s", pk_role_enum_to_string (role));
	}
	return text;
}
