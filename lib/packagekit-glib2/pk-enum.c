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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include "egg-debug.h"
#include "egg-string.h"

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
	{PK_STATUS_ENUM_ROLLBACK,		"rollback"},
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
	{PK_ROLE_ENUM_ROLLBACK,				"rollback"},
	{PK_ROLE_ENUM_SEARCH_DETAILS,			"search-details"},
	{PK_ROLE_ENUM_SEARCH_FILE,			"search-file"},
	{PK_ROLE_ENUM_SEARCH_GROUP,			"search-group"},
	{PK_ROLE_ENUM_SEARCH_NAME,			"search-name"},
	{PK_ROLE_ENUM_UPDATE_PACKAGES,			"update-packages"},
	{PK_ROLE_ENUM_UPDATE_SYSTEM,			"update-system"},
	{PK_ROLE_ENUM_WHAT_PROVIDES,			"what-provides"},
	{PK_ROLE_ENUM_ACCEPT_EULA,			"accept-eula"},
	{PK_ROLE_ENUM_DOWNLOAD_PACKAGES,		"download-packages"},
	{PK_ROLE_ENUM_GET_DISTRO_UPGRADES,		"get-distro-upgrades"},
	{PK_ROLE_ENUM_GET_CATEGORIES,			"get-categories"},
	{PK_ROLE_ENUM_GET_OLD_TRANSACTIONS,		"get-old-transactions"},
	{PK_ROLE_ENUM_SIMULATE_INSTALL_FILES,		"simulate-install-files"},
	{PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES,	"simulate-install-packages"},
	{PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES,		"simulate-remove-packages"},
	{PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES,		"simulate-update-packages"},
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

static const PkEnumMatch enum_message[] = {
	{PK_MESSAGE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_MESSAGE_ENUM_BROKEN_MIRROR,		"broken-mirror"},
	{PK_MESSAGE_ENUM_CONNECTION_REFUSED,	"connection-refused"},
	{PK_MESSAGE_ENUM_PARAMETER_INVALID,	"parameter-invalid"},
	{PK_MESSAGE_ENUM_PRIORITY_INVALID,	"priority-invalid"},
	{PK_MESSAGE_ENUM_BACKEND_ERROR,		"backend-error"},
	{PK_MESSAGE_ENUM_DAEMON_ERROR,		"daemon-error"},
	{PK_MESSAGE_ENUM_CACHE_BEING_REBUILT,	"cache-being-rebuilt"},
	{PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE,	"untrusted-package"},
	{PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS,	"newer-package-exists"},
	{PK_MESSAGE_ENUM_COULD_NOT_FIND_PACKAGE,	"could-not-find-package"},
	{PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED,	"config-files-changed"},
	{PK_MESSAGE_ENUM_PACKAGE_ALREADY_INSTALLED, "package-already-installed"},
	{PK_MESSAGE_ENUM_AUTOREMOVE_IGNORED, "autoremove-ignored"},
	{PK_MESSAGE_ENUM_REPO_METADATA_DOWNLOAD_FAILED, "repo-metadata-download-failed"},
	{PK_MESSAGE_ENUM_REPO_FOR_DEVELOPERS_ONLY, "repo-for-developers-only"},
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

/* DO NOT ADD ENTRIES MANUALLY... Use pk-refresh-licenses in tools */
static const PkEnumMatch enum_free_licenses[] = {
	{PK_LICENSE_ENUM_UNKNOWN,		"unknown"},	/* fall though value */
	{PK_LICENSE_ENUM_AAL,			"AAL"},
	{PK_LICENSE_ENUM_ADOBE,			"Adobe"},
	{PK_LICENSE_ENUM_ADSL,			"ADSL"},
	{PK_LICENSE_ENUM_AFL,			"AFL"},
	{PK_LICENSE_ENUM_AGPLV1,		"AGPLv1"},
	{PK_LICENSE_ENUM_AMDPLPA,		"AMDPLPA"},
	{PK_LICENSE_ENUM_AMPAS_BSD,		"AMPAS BSD"},
	{PK_LICENSE_ENUM_APSL_2_DOT_0,		"APSL 2.0"},
	{PK_LICENSE_ENUM_ARL,			"ARL"},
	{PK_LICENSE_ENUM_ARPHIC,		"Arphic"},
	{PK_LICENSE_ENUM_ARTISTIC_2_DOT_0,	"Artistic 2.0"},
	{PK_LICENSE_ENUM_ARTISTIC_CLARIFIED,	"Artistic clarified"},
	{PK_LICENSE_ENUM_ASL_1_DOT_0,		"ASL 1.0"},
	{PK_LICENSE_ENUM_ASL_1_DOT_1,		"ASL 1.1"},
	{PK_LICENSE_ENUM_ASL_2_DOT_0,		"ASL 2.0"},
	{PK_LICENSE_ENUM_BAEKMUK,		"Baekmuk"},
	{PK_LICENSE_ENUM_BITTORRENT,		"BitTorrent"},
	{PK_LICENSE_ENUM_BOOST,			"Boost"},
	{PK_LICENSE_ENUM_BSD,			"BSD"},
	{PK_LICENSE_ENUM_BSD_PROTECTION,	"BSD Protection"},
	{PK_LICENSE_ENUM_BSD_WITH_ADVERTISING,	"BSD with advertising"},
	{PK_LICENSE_ENUM_CATOSL,		"CATOSL"},
	{PK_LICENSE_ENUM_CC0,			"CC0"},
	{PK_LICENSE_ENUM_CC_BY,			"CC-BY"},
	{PK_LICENSE_ENUM_CC_BY_SA,		"CC-BY-SA"},
	{PK_LICENSE_ENUM_CDDL,			"CDDL"},
	{PK_LICENSE_ENUM_CDL,			"CDL"},
	{PK_LICENSE_ENUM_CECILL,		"CeCILL"},
	{PK_LICENSE_ENUM_CECILL_B,		"CeCILL-B"},
	{PK_LICENSE_ENUM_CECILL_C,		"CeCILL-C"},
	{PK_LICENSE_ENUM_CNRI,			"CNRI"},
	{PK_LICENSE_ENUM_CONDOR,		"Condor"},
	{PK_LICENSE_ENUM_COPYRIGHT_ONLY,	"Copyright only"},
	{PK_LICENSE_ENUM_CPAL,			"CPAL"},
	{PK_LICENSE_ENUM_CPL,			"CPL"},
	{PK_LICENSE_ENUM_CRYSTAL_STACKER,	"Crystal Stacker"},
	{PK_LICENSE_ENUM_DOC,			"DOC"},
	{PK_LICENSE_ENUM_DSL,			"DSL"},
	{PK_LICENSE_ENUM_DVIPDFM,		"dvipdfm"},
	{PK_LICENSE_ENUM_ECL_1_DOT_0,		"ECL 1.0"},
	{PK_LICENSE_ENUM_ECL_2_DOT_0,		"ECL 2.0"},
	{PK_LICENSE_ENUM_ECOS,			"eCos"},
	{PK_LICENSE_ENUM_EFL_2_DOT_0,		"EFL 2.0"},
	{PK_LICENSE_ENUM_ENTESSA,		"Entessa"},
	{PK_LICENSE_ENUM_EPL,			"EPL"},
	{PK_LICENSE_ENUM_ERPL,			"ERPL"},
	{PK_LICENSE_ENUM_EUPL_1_DOT_1,		"EUPL 1.1"},
	{PK_LICENSE_ENUM_EUROSYM,		"Eurosym"},
	{PK_LICENSE_ENUM_EU_DATAGRID,		"EU Datagrid"},
	{PK_LICENSE_ENUM_FAIR,			"Fair"},
	{PK_LICENSE_ENUM_FBSDDL,		"FBSDDL"},
	{PK_LICENSE_ENUM_FREE_ART,		"Free Art"},
	{PK_LICENSE_ENUM_FTL,			"FTL"},
	{PK_LICENSE_ENUM_GEOGRATIS,		"GeoGratis"},
	{PK_LICENSE_ENUM_GFDL,			"GFDL"},
	{PK_LICENSE_ENUM_GIFTWARE,		"Giftware"},
	{PK_LICENSE_ENUM_GL2PS,			"GL2PS"},
	{PK_LICENSE_ENUM_GLIDE,			"Glide"},
	{PK_LICENSE_ENUM_GNUPLOT,		"gnuplot"},
	{PK_LICENSE_ENUM_GPLV1,			"GPLv1"},
	{PK_LICENSE_ENUM_GPLV2,			"GPLv2"},
	{PK_LICENSE_ENUM_GPLV2_OR_ARTISTIC,	"GPLv2 or Artistic"},
	{PK_LICENSE_ENUM_GPLV2_PLUS,		"GPLv2+"},
	{PK_LICENSE_ENUM_GPLV2_PLUS_OR_ARTISTIC, "GPLv2+ or Artistic"},
	{PK_LICENSE_ENUM_GPLV2_PLUS_WITH_EXCEPTIONS, "GPLv2+ with exceptions"},
	{PK_LICENSE_ENUM_GPLV2_WITH_EXCEPTIONS,	"GPLv2 with exceptions"},
	{PK_LICENSE_ENUM_GPLV3,			"GPLv3"},
	{PK_LICENSE_ENUM_GPLV3_PLUS,		"GPLv3+"},
	{PK_LICENSE_ENUM_GPLV3_PLUS_WITH_EXCEPTIONS, "GPLv3+ with exceptions"},
	{PK_LICENSE_ENUM_GPLV3_WITH_EXCEPTIONS,	"GPLv3 with exceptions"},
	{PK_LICENSE_ENUM_GPL_PLUS,		"GPL+"},
	{PK_LICENSE_ENUM_GPL_PLUS_OR_ARTISTIC,	"GPL+ or Artistic"},
	{PK_LICENSE_ENUM_GPL_PLUS_WITH_EXCEPTIONS, "GPL+ with exceptions"},
	{PK_LICENSE_ENUM_IBM,			"IBM"},
	{PK_LICENSE_ENUM_IEEE,			"IEEE"},
	{PK_LICENSE_ENUM_IJG,			"IJG"},
	{PK_LICENSE_ENUM_IMAGEMAGICK,		"ImageMagick"},
	{PK_LICENSE_ENUM_IMATIX,		"iMatix"},
	{PK_LICENSE_ENUM_IMLIB2,		"Imlib2"},
	{PK_LICENSE_ENUM_INTEL_ACPI,		"Intel ACPI"},
	{PK_LICENSE_ENUM_INTERBASE,		"Interbase"},
	{PK_LICENSE_ENUM_IPA,			"IPA"},
	{PK_LICENSE_ENUM_ISC,			"ISC"},
	{PK_LICENSE_ENUM_JABBER,		"Jabber"},
	{PK_LICENSE_ENUM_JASPER,		"JasPer"},
	{PK_LICENSE_ENUM_JPYTHON,		"JPython"},
	{PK_LICENSE_ENUM_KNUTH,			"Knuth"},
	{PK_LICENSE_ENUM_LBNL_BSD,		"LBNL BSD"},
	{PK_LICENSE_ENUM_LGPLV2,		"LGPLv2"},
	{PK_LICENSE_ENUM_LGPLV2_PLUS,		"LGPLv2+"},
	{PK_LICENSE_ENUM_LGPLV2_PLUS_OR_ARTISTIC, "LGPLv2+ or Artistic"},
	{PK_LICENSE_ENUM_LGPLV2_PLUS_WITH_EXCEPTIONS, "LGPLv2+ with exceptions"},
	{PK_LICENSE_ENUM_LGPLV2_WITH_EXCEPTIONS, "LGPLv2 with exceptions"},
	{PK_LICENSE_ENUM_LGPLV3,		"LGPLv3"},
	{PK_LICENSE_ENUM_LGPLV3_PLUS,		"LGPLv3+"},
	{PK_LICENSE_ENUM_LGPLV3_PLUS_WITH_EXCEPTIONS, "LGPLv3+ with exceptions"},
	{PK_LICENSE_ENUM_LGPLV3_WITH_EXCEPTIONS, "LGPLv3 with exceptions"},
	{PK_LICENSE_ENUM_LIBERATION,		"Liberation"},
	{PK_LICENSE_ENUM_LIBTIFF,		"libtiff"},
	{PK_LICENSE_ENUM_LLGPL,			"LLGPL"},
	{PK_LICENSE_ENUM_LOGICA,		"Logica"},
	{PK_LICENSE_ENUM_LPL,			"LPL"},
	{PK_LICENSE_ENUM_LPPL,			"LPPL"},
	{PK_LICENSE_ENUM_MECAB_IPADIC,		"mecab-ipadic"},
	{PK_LICENSE_ENUM_MIROS,			"MirOS"},
	{PK_LICENSE_ENUM_MIT,			"MIT"},
	{PK_LICENSE_ENUM_MIT_WITH_ADVERTISING,	"MIT with advertising"},
	{PK_LICENSE_ENUM_MOD_MACRO,		"mod_macro"},
	{PK_LICENSE_ENUM_MOTOSOTO,		"Motosoto"},
	{PK_LICENSE_ENUM_MPLUS,			"mplus"},
	{PK_LICENSE_ENUM_MPLV1_DOT_0,		"MPLv1.0"},
	{PK_LICENSE_ENUM_MPLV1_DOT_1,		"MPLv1.1"},
	{PK_LICENSE_ENUM_MS_PL,			"MS-PL"},
	{PK_LICENSE_ENUM_NAUMEN,		"Naumen"},
	{PK_LICENSE_ENUM_NCSA,			"NCSA"},
	{PK_LICENSE_ENUM_NETCDF,		"NetCDF"},
	{PK_LICENSE_ENUM_NETSCAPE,		"Netscape"},
	{PK_LICENSE_ENUM_NEWMAT,		"Newmat"},
	{PK_LICENSE_ENUM_NGPL,			"NGPL"},
	{PK_LICENSE_ENUM_NOKIA,			"Nokia"},
	{PK_LICENSE_ENUM_NOSL,			"NOSL"},
	{PK_LICENSE_ENUM_NOWEB,			"Noweb"},
	{PK_LICENSE_ENUM_OAL,			"OAL"},
	{PK_LICENSE_ENUM_OFL,			"OFL"},
	{PK_LICENSE_ENUM_OFSFDL,		"OFSFDL"},
	{PK_LICENSE_ENUM_OPENLDAP,		"OpenLDAP"},
	{PK_LICENSE_ENUM_OPENPBS,		"OpenPBS"},
	{PK_LICENSE_ENUM_OPENSSL,		"OpenSSL"},
	{PK_LICENSE_ENUM_OREILLY,		"OReilly"},
	{PK_LICENSE_ENUM_OSL_1_DOT_0,		"OSL 1.0"},
	{PK_LICENSE_ENUM_OSL_1_DOT_1,		"OSL 1.1"},
	{PK_LICENSE_ENUM_OSL_2_DOT_0,		"OSL 2.0"},
	{PK_LICENSE_ENUM_OSL_2_DOT_1,		"OSL 2.1"},
	{PK_LICENSE_ENUM_OSL_3_DOT_0,		"OSL 3.0"},
	{PK_LICENSE_ENUM_PHORUM,		"Phorum"},
	{PK_LICENSE_ENUM_PHP,			"PHP"},
	{PK_LICENSE_ENUM_PLEXUS,		"Plexus"},
	{PK_LICENSE_ENUM_PSUTILS,		"psutils"},
	{PK_LICENSE_ENUM_PTFL,			"PTFL"},
	{PK_LICENSE_ENUM_PUBLIC_DOMAIN,		"Public Domain"},
	{PK_LICENSE_ENUM_PUBLIC_USE,		"Public Use"},
	{PK_LICENSE_ENUM_PYTHON,		"Python"},
	{PK_LICENSE_ENUM_QHULL,			"Qhull"},
	{PK_LICENSE_ENUM_QPL,			"QPL"},
	{PK_LICENSE_ENUM_RDISC,			"Rdisc"},
	{PK_LICENSE_ENUM_RICEBSD,		"RiceBSD"},
	{PK_LICENSE_ENUM_RPSL,			"RPSL"},
	{PK_LICENSE_ENUM_RUBY,			"Ruby"},
	{PK_LICENSE_ENUM_SAXPATH,		"Saxpath"},
	{PK_LICENSE_ENUM_SCEA,			"SCEA"},
	{PK_LICENSE_ENUM_SCRIP,			"SCRIP"},
	{PK_LICENSE_ENUM_SENDMAIL,		"Sendmail"},
	{PK_LICENSE_ENUM_SISSL,			"SISSL"},
	{PK_LICENSE_ENUM_SLEEPYCAT,		"Sleepycat"},
	{PK_LICENSE_ENUM_SLIB,			"SLIB"},
	{PK_LICENSE_ENUM_SNIA,			"SNIA"},
	{PK_LICENSE_ENUM_SPL,			"SPL"},
	{PK_LICENSE_ENUM_STIX,			"STIX"},
	{PK_LICENSE_ENUM_TCL,			"TCL"},
	{PK_LICENSE_ENUM_TMATE,			"TMate"},
	{PK_LICENSE_ENUM_TOSL,			"TOSL"},
	{PK_LICENSE_ENUM_TPL,			"TPL"},
	{PK_LICENSE_ENUM_UCD,			"UCD"},
	{PK_LICENSE_ENUM_VIM,			"Vim"},
	{PK_LICENSE_ENUM_VNLSL,			"VNLSL"},
	{PK_LICENSE_ENUM_VOSTROM,		"VOSTROM"},
	{PK_LICENSE_ENUM_VSL,			"VSL"},
	{PK_LICENSE_ENUM_W3C,			"W3C"},
	{PK_LICENSE_ENUM_WADALAB,		"Wadalab"},
	{PK_LICENSE_ENUM_WEBMIN,		"Webmin"},
	{PK_LICENSE_ENUM_WTFPL,			"WTFPL"},
	{PK_LICENSE_ENUM_WXWIDGETS,		"wxWidgets"},
	{PK_LICENSE_ENUM_XANO,			"XANO"},
	{PK_LICENSE_ENUM_XEROX,			"Xerox"},
	{PK_LICENSE_ENUM_XINETD,		"xinetd"},
	{PK_LICENSE_ENUM_XSKAT,			"XSkat"},
	{PK_LICENSE_ENUM_YPLV1_DOT_1,		"YPLv1.1"},
	{PK_LICENSE_ENUM_ZEND,			"Zend"},
	{PK_LICENSE_ENUM_ZLIB,			"zlib"},
	{PK_LICENSE_ENUM_ZLIB_WITH_ACKNOWLEDGEMENT, "zlib with acknowledgement"},
	{PK_LICENSE_ENUM_ZPLV1_DOT_0,		"ZPLv1.0"},
	{PK_LICENSE_ENUM_ZPLV2_DOT_0,		"ZPLv2.0"},
	{PK_LICENSE_ENUM_ZPLV2_DOT_1,		"ZPLv2.1"},
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
 * pk_distro_upgrade_enum_from_text:
 * @upgrade: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_DISTRO_UPGRADE_ENUM_STABLE
 */
PkDistroUpgradeEnum
pk_distro_upgrade_enum_from_text (const gchar *upgrade)
{
	return pk_enum_find_value (enum_upgrade, upgrade);
}

/**
 * pk_distro_upgrade_enum_to_text:
 * @upgrade: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "stable"
 **/
const gchar *
pk_distro_upgrade_enum_to_text (PkDistroUpgradeEnum upgrade)
{
	return pk_enum_find_string (enum_upgrade, upgrade);
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
pk_exit_enum_from_text (const gchar *exit_text)
{
	return pk_enum_find_value (enum_exit, exit_text);
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
pk_exit_enum_to_text (PkExitEnum exit_enum)
{
	return pk_enum_find_string (enum_exit, exit_enum);
}

/**
 * pk_network_enum_from_text:
 * @network: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_SIGTYPE_ENUM_GPG
 */
PkNetworkEnum
pk_network_enum_from_text (const gchar *network)
{
	return pk_enum_find_value (enum_network, network);
}

/**
 * pk_network_enum_to_text:
 * @network: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "available"
 **/
const gchar *
pk_network_enum_to_text (PkNetworkEnum network)
{
	return pk_enum_find_string (enum_network, network);
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
PkErrorEnum
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
pk_error_enum_to_text (PkErrorEnum code)
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
 * pk_update_state_enum_from_text:
 * @update_state: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. %PK_UPDATE_STATE_ENUM_STABLE
 **/
PkUpdateStateEnum
pk_update_state_enum_from_text (const gchar *update_state)
{
	return pk_enum_find_value (enum_update_state, update_state);
}

/**
 * pk_update_state_enum_to_text:
 * @update_state: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "testing"
 **/
const gchar *
pk_update_state_enum_to_text (PkUpdateStateEnum update_state)
{
	return pk_enum_find_string (enum_update_state, update_state);
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

/**
 * pk_media_type_enum_from_text:
 * @code: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_MEDIA_TYPE_ENUM_CD
 **/
PkMediaTypeEnum
pk_media_type_enum_from_text (const gchar *media_type)
{
	return pk_enum_find_value (enum_media_type, media_type);
}

/**
 * pk_media_type_enum_to_text:
 * @code: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "dvd"
 **/
const gchar *
pk_media_type_enum_to_text (PkMediaTypeEnum media_type)
{
	return pk_enum_find_string (enum_media_type, media_type);
}

/**
 * pk_authorize_type_enum_from_text:
 * @code: Text describing the enumerated type
 *
 * Converts a text enumerated type to its unsigned integer representation
 *
 * Return value: the enumerated constant value, e.g. PK_AUTHORIZE_ENUM_YES
 **/
PkAuthorizeEnum
pk_authorize_type_enum_from_text (const gchar *authorize_type)
{
	return pk_enum_find_value (enum_authorize_type, authorize_type);
}

/**
 * pk_authorize_type_enum_to_text:
 * @code: The enumerated type value
 *
 * Converts a enumerated type to its text representation
 *
 * Return value: the enumerated constant value, e.g. "yes"
 **/
const gchar *
pk_authorize_type_enum_to_text (PkAuthorizeEnum authorize_type)
{
	return pk_enum_find_string (enum_authorize_type, authorize_type);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_enum_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	const gchar *string;
	PkRoleEnum role_value;
	guint i;

	if (!egg_test_start (test, "PkEnum"))
		return;

	/************************************************************/
	egg_test_title (test, "find role_value");
	role_value = pk_enum_find_value (enum_role, "search-file");
	if (role_value == PK_ROLE_ENUM_SEARCH_FILE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "find string");
	string = pk_enum_find_string (enum_role, PK_ROLE_ENUM_SEARCH_FILE);
	if (g_strcmp0 (string, "search-file") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "find value");
	role_value = pk_role_enum_from_text ("search-file");
	if (role_value == PK_ROLE_ENUM_SEARCH_FILE)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "find string");
	string = pk_role_enum_to_text (PK_ROLE_ENUM_SEARCH_FILE);
	if (g_strcmp0 (string, "search-file") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the role bitfield");
	for (i=1; i<PK_ROLE_ENUM_LAST; i++) {
		string = pk_role_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the status bitfield");
	for (i=1; i<PK_STATUS_ENUM_LAST; i++) {
		string = pk_status_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the exit bitfield");
	for (i=0; i<PK_EXIT_ENUM_LAST; i++) {
		string = pk_exit_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the filter bitfield");
	for (i=0; i<PK_FILTER_ENUM_LAST; i++) {
		string = pk_filter_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the restart bitfield");
	for (i=0; i<PK_RESTART_ENUM_LAST; i++) {
		string = pk_restart_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the error_code bitfield");
	for (i=0; i<PK_ERROR_ENUM_LAST; i++) {
		string = pk_error_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the group bitfield");
	for (i=1; i<PK_GROUP_ENUM_LAST; i++) {
		string = pk_group_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the info bitfield");
	for (i=1; i<PK_INFO_ENUM_LAST; i++) {
		string = pk_info_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the sig_type bitfield");
	for (i=0; i<PK_SIGTYPE_ENUM_LAST; i++) {
		string = pk_sig_type_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the upgrade bitfield");
	for (i=0; i<PK_DISTRO_UPGRADE_ENUM_LAST; i++) {
		string = pk_distro_upgrade_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the license bitfield");
	for (i=0; i<PK_LICENSE_ENUM_LAST; i++) {
		string = pk_license_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "check we convert all the media type bitfield");
	for (i=0; i<PK_MEDIA_TYPE_ENUM_LAST; i++) {
		string = pk_media_type_enum_to_text (i);
		if (string == NULL) {
			egg_test_failed (test, "failed to get %i", i);
			break;
		}
	}
	egg_test_success (test, NULL);

	egg_test_end (test);
}
#endif

