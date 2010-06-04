/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_ENUM_H
#define __PK_ENUM_H

#include <glib.h>

G_BEGIN_DECLS

/* These are compatibility enums so we can break API in the future */
#define pk_sig_type_enum_from_text		pk_sig_type_enum_from_string
#define pk_sig_type_enum_to_text		pk_sig_type_enum_to_string
#define pk_distro_upgrade_enum_from_text	pk_distro_upgrade_enum_from_string
#define pk_distro_upgrade_enum_to_text		pk_distro_upgrade_enum_to_string
#define pk_provides_enum_from_text		pk_provides_enum_from_string
#define pk_provides_enum_to_text		pk_provides_enum_to_string
#define pk_info_enum_from_text			pk_info_enum_from_string
#define pk_info_enum_to_text			pk_info_enum_to_string
#define pk_exit_enum_from_text			pk_exit_enum_from_string
#define pk_exit_enum_to_text			pk_exit_enum_to_string
#define pk_network_enum_from_text		pk_network_enum_from_string
#define pk_network_enum_to_text			pk_network_enum_to_string
#define pk_status_enum_from_text		pk_status_enum_from_string
#define pk_status_enum_to_text			pk_status_enum_to_string
#define pk_role_enum_from_text			pk_role_enum_from_string
#define pk_role_enum_to_text			pk_role_enum_to_string
#define pk_error_enum_from_text			pk_error_enum_from_string
#define pk_error_enum_to_text			pk_error_enum_to_string
#define pk_restart_enum_from_text		pk_restart_enum_from_string
#define pk_restart_enum_to_text			pk_restart_enum_to_string
#define pk_message_enum_from_text		pk_message_enum_from_string
#define pk_message_enum_to_text			pk_message_enum_to_string
#define pk_group_enum_from_text			pk_group_enum_from_string
#define pk_group_enum_to_text			pk_group_enum_to_string
#define pk_update_state_enum_from_text		pk_update_state_enum_from_string
#define pk_update_state_enum_to_text		pk_update_state_enum_to_string
#define pk_filter_enum_from_text		pk_filter_enum_from_string
#define pk_filter_enum_to_text			pk_filter_enum_to_string
#define pk_license_enum_from_text		pk_license_enum_from_string
#define pk_license_enum_to_text			pk_license_enum_to_string
#define pk_media_type_enum_from_text		pk_media_type_enum_from_string
#define pk_media_type_enum_to_text		pk_media_type_enum_to_string
#define pk_authorize_type_enum_from_text	pk_authorize_type_enum_from_string
#define pk_authorize_type_enum_to_text		pk_authorize_type_enum_to_string

/**
 * PkEnumMatch:
 *
 * Matching an enumerated type to a string
 **/
typedef struct {
	guint		 value;
	const gchar	*string;
} PkEnumMatch;

/**
 * PkRoleEnum:
 *
 * What we were asked to do, this never changes for the lifetime of the
 * transaction.
 * Icons that have to represent the whole "aim" of the transaction will use
 * these constants
 **/
typedef enum {
	PK_ROLE_ENUM_UNKNOWN,
	PK_ROLE_ENUM_CANCEL,
	PK_ROLE_ENUM_GET_DEPENDS,
	PK_ROLE_ENUM_GET_DETAILS,
	PK_ROLE_ENUM_GET_FILES,
	PK_ROLE_ENUM_GET_PACKAGES,
	PK_ROLE_ENUM_GET_REPO_LIST,
	PK_ROLE_ENUM_GET_REQUIRES,
	PK_ROLE_ENUM_GET_UPDATE_DETAIL,
	PK_ROLE_ENUM_GET_UPDATES,
	PK_ROLE_ENUM_INSTALL_FILES,
	PK_ROLE_ENUM_INSTALL_PACKAGES,
	PK_ROLE_ENUM_INSTALL_SIGNATURE,
	PK_ROLE_ENUM_REFRESH_CACHE,
	PK_ROLE_ENUM_REMOVE_PACKAGES,
	PK_ROLE_ENUM_REPO_ENABLE,
	PK_ROLE_ENUM_REPO_SET_DATA,
	PK_ROLE_ENUM_RESOLVE,
	PK_ROLE_ENUM_ROLLBACK,
	PK_ROLE_ENUM_SEARCH_DETAILS,
	PK_ROLE_ENUM_SEARCH_FILE,
	PK_ROLE_ENUM_SEARCH_GROUP,
	PK_ROLE_ENUM_SEARCH_NAME,
	PK_ROLE_ENUM_UPDATE_PACKAGES,
	PK_ROLE_ENUM_UPDATE_SYSTEM,
	PK_ROLE_ENUM_WHAT_PROVIDES,
	PK_ROLE_ENUM_ACCEPT_EULA,
	PK_ROLE_ENUM_DOWNLOAD_PACKAGES,
	PK_ROLE_ENUM_GET_DISTRO_UPGRADES,
	PK_ROLE_ENUM_GET_CATEGORIES,
	PK_ROLE_ENUM_GET_OLD_TRANSACTIONS,
	PK_ROLE_ENUM_SIMULATE_INSTALL_FILES,
	PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES,
	PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES,
	PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES,
	PK_ROLE_ENUM_LAST
} PkRoleEnum;

/**
 * PkStatusEnum:
 *
 * What status we are now; this can change for each transaction giving a
 * status of what sort of thing is happening
 * Icons that change to represent the current status of the transaction will
 * use these constants
 * If you add to these, make sure you add filenames in gpk-watch.c also
 *
 * A typical transaction will do:
 * - schedule task
 *	WAIT
 * - run task
 *	SETUP
 * - wait for lock
 *	RUNNING
 *
 * This means that backends should run pk_backend_set_status (backend, PK_STATUS_ENUM_RUNNING)
 * when they are ready to start running the transaction and after a lock has been got.
 **/
typedef enum {
	PK_STATUS_ENUM_UNKNOWN,
	PK_STATUS_ENUM_WAIT,
	PK_STATUS_ENUM_SETUP,
	PK_STATUS_ENUM_RUNNING,
	PK_STATUS_ENUM_QUERY,
	PK_STATUS_ENUM_INFO,
	PK_STATUS_ENUM_REMOVE,
	PK_STATUS_ENUM_REFRESH_CACHE,
	PK_STATUS_ENUM_DOWNLOAD,
	PK_STATUS_ENUM_INSTALL,
	PK_STATUS_ENUM_UPDATE,
	PK_STATUS_ENUM_CLEANUP,
	PK_STATUS_ENUM_OBSOLETE,
	PK_STATUS_ENUM_DEP_RESOLVE,
	PK_STATUS_ENUM_SIG_CHECK,
	PK_STATUS_ENUM_ROLLBACK,
	PK_STATUS_ENUM_TEST_COMMIT,
	PK_STATUS_ENUM_COMMIT,
	PK_STATUS_ENUM_REQUEST,
	PK_STATUS_ENUM_FINISHED,
	PK_STATUS_ENUM_CANCEL,
	PK_STATUS_ENUM_DOWNLOAD_REPOSITORY,
	PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST,
	PK_STATUS_ENUM_DOWNLOAD_FILELIST,
	PK_STATUS_ENUM_DOWNLOAD_CHANGELOG,
	PK_STATUS_ENUM_DOWNLOAD_GROUP,
	PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO,
	PK_STATUS_ENUM_REPACKAGING,
	PK_STATUS_ENUM_LOADING_CACHE,
	PK_STATUS_ENUM_SCAN_APPLICATIONS,
	PK_STATUS_ENUM_GENERATE_PACKAGE_LIST,
	PK_STATUS_ENUM_WAITING_FOR_LOCK,
	PK_STATUS_ENUM_WAITING_FOR_AUTH,
	PK_STATUS_ENUM_SCAN_PROCESS_LIST,
	PK_STATUS_ENUM_CHECK_EXECUTABLE_FILES,
	PK_STATUS_ENUM_CHECK_LIBRARIES,
	PK_STATUS_ENUM_COPY_FILES,
	PK_STATUS_ENUM_LAST
} PkStatusEnum;

/**
 * PkExitEnum:
 *
 * How the backend exited
 **/
typedef enum {
	PK_EXIT_ENUM_UNKNOWN,
	PK_EXIT_ENUM_SUCCESS,
	PK_EXIT_ENUM_FAILED,
	PK_EXIT_ENUM_CANCELLED,
	PK_EXIT_ENUM_KEY_REQUIRED,
	PK_EXIT_ENUM_EULA_REQUIRED,
	PK_EXIT_ENUM_KILLED, /* when we forced the cancel, but had to SIGKILL */
	PK_EXIT_ENUM_MEDIA_CHANGE_REQUIRED,
	PK_EXIT_ENUM_NEED_UNTRUSTED,
	PK_EXIT_ENUM_LAST
} PkExitEnum;

/**
 * PkNetworkEnum:
 **/
typedef enum {
	PK_NETWORK_ENUM_UNKNOWN,
	PK_NETWORK_ENUM_OFFLINE,
	PK_NETWORK_ENUM_ONLINE,
	PK_NETWORK_ENUM_WIRED,
	PK_NETWORK_ENUM_WIFI,
	PK_NETWORK_ENUM_MOBILE,
	PK_NETWORK_ENUM_LAST
} PkNetworkEnum;

/**
 * PkFilterEnum:
 *
 * The filter types
 **/
typedef enum {
	PK_FILTER_ENUM_UNKNOWN,
	PK_FILTER_ENUM_NONE,
	PK_FILTER_ENUM_INSTALLED,
	PK_FILTER_ENUM_NOT_INSTALLED,
	PK_FILTER_ENUM_DEVELOPMENT,
	PK_FILTER_ENUM_NOT_DEVELOPMENT,
	PK_FILTER_ENUM_GUI,
	PK_FILTER_ENUM_NOT_GUI,
	PK_FILTER_ENUM_FREE,
	PK_FILTER_ENUM_NOT_FREE,
	PK_FILTER_ENUM_VISIBLE,
	PK_FILTER_ENUM_NOT_VISIBLE,
	PK_FILTER_ENUM_SUPPORTED,
	PK_FILTER_ENUM_NOT_SUPPORTED,
	PK_FILTER_ENUM_BASENAME,
	PK_FILTER_ENUM_NOT_BASENAME,
	PK_FILTER_ENUM_NEWEST,
	PK_FILTER_ENUM_NOT_NEWEST,
	PK_FILTER_ENUM_ARCH,
	PK_FILTER_ENUM_NOT_ARCH,
	PK_FILTER_ENUM_SOURCE,
	PK_FILTER_ENUM_NOT_SOURCE,
	PK_FILTER_ENUM_COLLECTIONS,
	PK_FILTER_ENUM_NOT_COLLECTIONS,
	PK_FILTER_ENUM_APPLICATION,
	PK_FILTER_ENUM_NOT_APPLICATION,
	PK_FILTER_ENUM_LAST,
} PkFilterEnum;

/**
 * PkRestartEnum:
 *
 * What restart we need to after a transaction, ordered by severity
 **/
typedef enum {
	PK_RESTART_ENUM_UNKNOWN,
	PK_RESTART_ENUM_NONE,
	PK_RESTART_ENUM_APPLICATION,
	PK_RESTART_ENUM_SESSION,
	PK_RESTART_ENUM_SYSTEM,
	PK_RESTART_ENUM_SECURITY_SESSION,	/* a library that is being used by this package has been updated for security */
	PK_RESTART_ENUM_SECURITY_SYSTEM,
	PK_RESTART_ENUM_LAST
} PkRestartEnum;

/**
 * PkMessageEnum:
 *
 * What message type we need to show
 **/
typedef enum {
	PK_MESSAGE_ENUM_UNKNOWN,
	PK_MESSAGE_ENUM_BROKEN_MIRROR,
	PK_MESSAGE_ENUM_CONNECTION_REFUSED,
	PK_MESSAGE_ENUM_PARAMETER_INVALID,
	PK_MESSAGE_ENUM_PRIORITY_INVALID,
	PK_MESSAGE_ENUM_BACKEND_ERROR,
	PK_MESSAGE_ENUM_DAEMON_ERROR,
	PK_MESSAGE_ENUM_CACHE_BEING_REBUILT,
	PK_MESSAGE_ENUM_UNTRUSTED_PACKAGE,
	PK_MESSAGE_ENUM_NEWER_PACKAGE_EXISTS,
	PK_MESSAGE_ENUM_COULD_NOT_FIND_PACKAGE,
	PK_MESSAGE_ENUM_CONFIG_FILES_CHANGED,
	PK_MESSAGE_ENUM_PACKAGE_ALREADY_INSTALLED,
	PK_MESSAGE_ENUM_AUTOREMOVE_IGNORED,
	PK_MESSAGE_ENUM_REPO_METADATA_DOWNLOAD_FAILED,
	PK_MESSAGE_ENUM_REPO_FOR_DEVELOPERS_ONLY,
	PK_MESSAGE_ENUM_OTHER_UPDATES_HELD_BACK,
	PK_MESSAGE_ENUM_LAST
} PkMessageEnum;

/**
 * PkErrorEnum:
 *
 * The error type
 **/
typedef enum {
	PK_ERROR_ENUM_UNKNOWN,
	PK_ERROR_ENUM_OOM,
	PK_ERROR_ENUM_NO_NETWORK,
	PK_ERROR_ENUM_NOT_SUPPORTED,
	PK_ERROR_ENUM_INTERNAL_ERROR,
	PK_ERROR_ENUM_GPG_FAILURE,
	PK_ERROR_ENUM_PACKAGE_ID_INVALID,
	PK_ERROR_ENUM_PACKAGE_NOT_INSTALLED,
	PK_ERROR_ENUM_PACKAGE_NOT_FOUND,
	PK_ERROR_ENUM_PACKAGE_ALREADY_INSTALLED,
	PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED,
	PK_ERROR_ENUM_GROUP_NOT_FOUND,
	PK_ERROR_ENUM_GROUP_LIST_INVALID,
	PK_ERROR_ENUM_DEP_RESOLUTION_FAILED,
	PK_ERROR_ENUM_FILTER_INVALID,
	PK_ERROR_ENUM_CREATE_THREAD_FAILED,
	PK_ERROR_ENUM_TRANSACTION_ERROR,
	PK_ERROR_ENUM_TRANSACTION_CANCELLED,
	PK_ERROR_ENUM_NO_CACHE,
	PK_ERROR_ENUM_REPO_NOT_FOUND,
	PK_ERROR_ENUM_CANNOT_REMOVE_SYSTEM_PACKAGE,
	PK_ERROR_ENUM_PROCESS_KILL,
	PK_ERROR_ENUM_FAILED_INITIALIZATION,
	PK_ERROR_ENUM_FAILED_FINALISE,
	PK_ERROR_ENUM_FAILED_CONFIG_PARSING,
	PK_ERROR_ENUM_CANNOT_CANCEL,
	PK_ERROR_ENUM_CANNOT_GET_LOCK,
	PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE,
	PK_ERROR_ENUM_CANNOT_WRITE_REPO_CONFIG,
	PK_ERROR_ENUM_LOCAL_INSTALL_FAILED,
	PK_ERROR_ENUM_BAD_GPG_SIGNATURE,
	PK_ERROR_ENUM_MISSING_GPG_SIGNATURE,
	PK_ERROR_ENUM_CANNOT_INSTALL_SOURCE_PACKAGE,
	PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR,
	PK_ERROR_ENUM_NO_LICENSE_AGREEMENT,
	PK_ERROR_ENUM_FILE_CONFLICTS,
	PK_ERROR_ENUM_PACKAGE_CONFLICTS,
	PK_ERROR_ENUM_REPO_NOT_AVAILABLE,
	PK_ERROR_ENUM_INVALID_PACKAGE_FILE,
	PK_ERROR_ENUM_PACKAGE_INSTALL_BLOCKED,
	PK_ERROR_ENUM_PACKAGE_CORRUPT,
	PK_ERROR_ENUM_ALL_PACKAGES_ALREADY_INSTALLED,
	PK_ERROR_ENUM_FILE_NOT_FOUND,
	PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY,
	PK_ERROR_ENUM_NO_DISTRO_UPGRADE_DATA,
	PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE,
	PK_ERROR_ENUM_NO_SPACE_ON_DEVICE,
	PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED,
	PK_ERROR_ENUM_NOT_AUTHORIZED,
	PK_ERROR_ENUM_UPDATE_NOT_FOUND,
	PK_ERROR_ENUM_CANNOT_INSTALL_REPO_UNSIGNED,
	PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED,
	PK_ERROR_ENUM_CANNOT_GET_FILELIST,
	PK_ERROR_ENUM_CANNOT_GET_REQUIRES,
	PK_ERROR_ENUM_CANNOT_DISABLE_REPOSITORY,
	PK_ERROR_ENUM_RESTRICTED_DOWNLOAD,
	PK_ERROR_ENUM_PACKAGE_FAILED_TO_CONFIGURE,
	PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD,
	PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL,
	PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE,
	PK_ERROR_ENUM_UPDATE_FAILED_DUE_TO_RUNNING_PROCESS,
	PK_ERROR_ENUM_PACKAGE_DATABASE_CHANGED,
	PK_ERROR_ENUM_PROVIDE_TYPE_NOT_SUPPORTED,
	PK_ERROR_ENUM_INSTALL_ROOT_INVALID,
	PK_ERROR_ENUM_LAST
} PkErrorEnum;

/**
 * PkGroupEnum:
 *
 * The group type
 **/
typedef enum {
	PK_GROUP_ENUM_UNKNOWN,
	PK_GROUP_ENUM_ACCESSIBILITY,
	PK_GROUP_ENUM_ACCESSORIES,
	PK_GROUP_ENUM_ADMIN_TOOLS,
	PK_GROUP_ENUM_COMMUNICATION,
	PK_GROUP_ENUM_DESKTOP_GNOME,
	PK_GROUP_ENUM_DESKTOP_KDE,
	PK_GROUP_ENUM_DESKTOP_OTHER,
	PK_GROUP_ENUM_DESKTOP_XFCE,
	PK_GROUP_ENUM_EDUCATION,
	PK_GROUP_ENUM_FONTS,
	PK_GROUP_ENUM_GAMES,
	PK_GROUP_ENUM_GRAPHICS,
	PK_GROUP_ENUM_INTERNET,
	PK_GROUP_ENUM_LEGACY,
	PK_GROUP_ENUM_LOCALIZATION,
	PK_GROUP_ENUM_MAPS,
	PK_GROUP_ENUM_MULTIMEDIA,
	PK_GROUP_ENUM_NETWORK,
	PK_GROUP_ENUM_OFFICE,
	PK_GROUP_ENUM_OTHER,
	PK_GROUP_ENUM_POWER_MANAGEMENT,
	PK_GROUP_ENUM_PROGRAMMING,
	PK_GROUP_ENUM_PUBLISHING,
	PK_GROUP_ENUM_REPOS,
	PK_GROUP_ENUM_SECURITY,
	PK_GROUP_ENUM_SERVERS,
	PK_GROUP_ENUM_SYSTEM,
	PK_GROUP_ENUM_VIRTUALIZATION,
	PK_GROUP_ENUM_SCIENCE,
	PK_GROUP_ENUM_DOCUMENTATION,
	PK_GROUP_ENUM_ELECTRONICS,
	PK_GROUP_ENUM_COLLECTIONS,
	PK_GROUP_ENUM_VENDOR,
	PK_GROUP_ENUM_NEWEST,
	PK_GROUP_ENUM_LAST
} PkGroupEnum;

/**
 * PkUpdateStateEnum:
 *
 * What state the update is in
 **/
typedef enum {
	PK_UPDATE_STATE_ENUM_UNKNOWN,
	PK_UPDATE_STATE_ENUM_STABLE,
	PK_UPDATE_STATE_ENUM_UNSTABLE,
	PK_UPDATE_STATE_ENUM_TESTING,
	PK_UPDATE_STATE_ENUM_LAST
} PkUpdateStateEnum;

/**
 * PkInfoEnum:
 *
 * The enumerated types used in Package() - these have to refer to a specific
 * package action, rather than a general state
 **/
typedef enum {
	PK_INFO_ENUM_UNKNOWN,
	PK_INFO_ENUM_INSTALLED,
	PK_INFO_ENUM_AVAILABLE,
	PK_INFO_ENUM_LOW,
	PK_INFO_ENUM_ENHANCEMENT,
	PK_INFO_ENUM_NORMAL,
	PK_INFO_ENUM_BUGFIX,
	PK_INFO_ENUM_IMPORTANT,
	PK_INFO_ENUM_SECURITY,
	PK_INFO_ENUM_BLOCKED,
	PK_INFO_ENUM_DOWNLOADING,
	PK_INFO_ENUM_UPDATING,
	PK_INFO_ENUM_INSTALLING,
	PK_INFO_ENUM_REMOVING,
	PK_INFO_ENUM_CLEANUP,
	PK_INFO_ENUM_OBSOLETING,
	PK_INFO_ENUM_COLLECTION_INSTALLED,
	PK_INFO_ENUM_COLLECTION_AVAILABLE,
	PK_INFO_ENUM_FINISHED,
	PK_INFO_ENUM_REINSTALLING,
	PK_INFO_ENUM_DOWNGRADING,
	PK_INFO_ENUM_PREPARING,
	PK_INFO_ENUM_DECOMPRESSING,
	PK_INFO_ENUM_LAST
} PkInfoEnum;

/**
 * PkDistroUpgradeEnum:
 *
 * The distro upgrade status
 **/
typedef enum {
	PK_DISTRO_UPGRADE_ENUM_UNKNOWN,
	PK_DISTRO_UPGRADE_ENUM_STABLE,
	PK_DISTRO_UPGRADE_ENUM_UNSTABLE,
	PK_DISTRO_UPGRADE_ENUM_LAST
} PkDistroUpgradeEnum;

/**
 * PkSigTypeEnum:
 *
 * The signature type type
 **/
typedef enum {
	PK_SIGTYPE_ENUM_UNKNOWN,
	PK_SIGTYPE_ENUM_GPG,
	PK_SIGTYPE_ENUM_LAST
} PkSigTypeEnum;

/**
 * PkProvidesEnum:
 *
 * The signature type type
 **/
typedef enum {
	PK_PROVIDES_ENUM_UNKNOWN,
	PK_PROVIDES_ENUM_ANY,
	PK_PROVIDES_ENUM_MODALIAS,
	PK_PROVIDES_ENUM_CODEC,
	PK_PROVIDES_ENUM_MIMETYPE,
	PK_PROVIDES_ENUM_FONT,
	PK_PROVIDES_ENUM_HARDWARE_DRIVER,
	PK_PROVIDES_ENUM_POSTSCRIPT_DRIVER,
	PK_PROVIDES_ENUM_LAST
} PkProvidesEnum;

typedef enum {
	PK_LICENSE_ENUM_UNKNOWN,
	PK_LICENSE_ENUM_AAL,
	PK_LICENSE_ENUM_ADOBE,
	PK_LICENSE_ENUM_ADSL,
	PK_LICENSE_ENUM_AFL,
	PK_LICENSE_ENUM_AGPLV1,
	PK_LICENSE_ENUM_AMDPLPA,
	PK_LICENSE_ENUM_AMPAS_BSD,
	PK_LICENSE_ENUM_APSL_2_DOT_0,
	PK_LICENSE_ENUM_ARL,
	PK_LICENSE_ENUM_ARPHIC,
	PK_LICENSE_ENUM_ARTISTIC_2_DOT_0,
	PK_LICENSE_ENUM_ARTISTIC_CLARIFIED,
	PK_LICENSE_ENUM_ASL_1_DOT_0,
	PK_LICENSE_ENUM_ASL_1_DOT_1,
	PK_LICENSE_ENUM_ASL_2_DOT_0,
	PK_LICENSE_ENUM_BAEKMUK,
	PK_LICENSE_ENUM_BITTORRENT,
	PK_LICENSE_ENUM_BOOST,
	PK_LICENSE_ENUM_BSD,
	PK_LICENSE_ENUM_BSD_PROTECTION,
	PK_LICENSE_ENUM_BSD_WITH_ADVERTISING,
	PK_LICENSE_ENUM_CATOSL,
	PK_LICENSE_ENUM_CC0,
	PK_LICENSE_ENUM_CC_BY,
	PK_LICENSE_ENUM_CC_BY_SA,
	PK_LICENSE_ENUM_CDDL,
	PK_LICENSE_ENUM_CDL,
	PK_LICENSE_ENUM_CECILL,
	PK_LICENSE_ENUM_CECILL_B,
	PK_LICENSE_ENUM_CECILL_C,
	PK_LICENSE_ENUM_CNRI,
	PK_LICENSE_ENUM_CONDOR,
	PK_LICENSE_ENUM_COPYRIGHT_ONLY,
	PK_LICENSE_ENUM_CPAL,
	PK_LICENSE_ENUM_CPL,
	PK_LICENSE_ENUM_CRYSTAL_STACKER,
	PK_LICENSE_ENUM_DOC,
	PK_LICENSE_ENUM_DSL,
	PK_LICENSE_ENUM_DVIPDFM,
	PK_LICENSE_ENUM_ECL_1_DOT_0,
	PK_LICENSE_ENUM_ECL_2_DOT_0,
	PK_LICENSE_ENUM_ECOS,
	PK_LICENSE_ENUM_EFL_2_DOT_0,
	PK_LICENSE_ENUM_ENTESSA,
	PK_LICENSE_ENUM_EPL,
	PK_LICENSE_ENUM_ERPL,
	PK_LICENSE_ENUM_EUPL_1_DOT_1,
	PK_LICENSE_ENUM_EUROSYM,
	PK_LICENSE_ENUM_EU_DATAGRID,
	PK_LICENSE_ENUM_FAIR,
	PK_LICENSE_ENUM_FBSDDL,
	PK_LICENSE_ENUM_FREE_ART,
	PK_LICENSE_ENUM_FTL,
	PK_LICENSE_ENUM_GEOGRATIS,
	PK_LICENSE_ENUM_GFDL,
	PK_LICENSE_ENUM_GIFTWARE,
	PK_LICENSE_ENUM_GL2PS,
	PK_LICENSE_ENUM_GLIDE,
	PK_LICENSE_ENUM_GNUPLOT,
	PK_LICENSE_ENUM_GPLV1,
	PK_LICENSE_ENUM_GPLV2,
	PK_LICENSE_ENUM_GPLV2_OR_ARTISTIC,
	PK_LICENSE_ENUM_GPLV2_PLUS,
	PK_LICENSE_ENUM_GPLV2_PLUS_OR_ARTISTIC,
	PK_LICENSE_ENUM_GPLV2_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPLV2_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPLV3,
	PK_LICENSE_ENUM_GPLV3_PLUS,
	PK_LICENSE_ENUM_GPLV3_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPLV3_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPL_PLUS,
	PK_LICENSE_ENUM_GPL_PLUS_OR_ARTISTIC,
	PK_LICENSE_ENUM_GPL_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_IBM,
	PK_LICENSE_ENUM_IEEE,
	PK_LICENSE_ENUM_IJG,
	PK_LICENSE_ENUM_IMAGEMAGICK,
	PK_LICENSE_ENUM_IMATIX,
	PK_LICENSE_ENUM_IMLIB2,
	PK_LICENSE_ENUM_INTEL_ACPI,
	PK_LICENSE_ENUM_INTERBASE,
	PK_LICENSE_ENUM_IPA,
	PK_LICENSE_ENUM_ISC,
	PK_LICENSE_ENUM_JABBER,
	PK_LICENSE_ENUM_JASPER,
	PK_LICENSE_ENUM_JPYTHON,
	PK_LICENSE_ENUM_KNUTH,
	PK_LICENSE_ENUM_LBNL_BSD,
	PK_LICENSE_ENUM_LGPLV2,
	PK_LICENSE_ENUM_LGPLV2_PLUS,
	PK_LICENSE_ENUM_LGPLV2_PLUS_OR_ARTISTIC,
	PK_LICENSE_ENUM_LGPLV2_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_LGPLV2_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_LGPLV3,
	PK_LICENSE_ENUM_LGPLV3_PLUS,
	PK_LICENSE_ENUM_LGPLV3_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_LGPLV3_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_LIBERATION,
	PK_LICENSE_ENUM_LIBTIFF,
	PK_LICENSE_ENUM_LLGPL,
	PK_LICENSE_ENUM_LOGICA,
	PK_LICENSE_ENUM_LPL,
	PK_LICENSE_ENUM_LPPL,
	PK_LICENSE_ENUM_MECAB_IPADIC,
	PK_LICENSE_ENUM_MIROS,
	PK_LICENSE_ENUM_MIT,
	PK_LICENSE_ENUM_MIT_WITH_ADVERTISING,
	PK_LICENSE_ENUM_MOD_MACRO,
	PK_LICENSE_ENUM_MOTOSOTO,
	PK_LICENSE_ENUM_MPLUS,
	PK_LICENSE_ENUM_MPLV1_DOT_0,
	PK_LICENSE_ENUM_MPLV1_DOT_1,
	PK_LICENSE_ENUM_MS_PL,
	PK_LICENSE_ENUM_NAUMEN,
	PK_LICENSE_ENUM_NCSA,
	PK_LICENSE_ENUM_NETCDF,
	PK_LICENSE_ENUM_NETSCAPE,
	PK_LICENSE_ENUM_NEWMAT,
	PK_LICENSE_ENUM_NGPL,
	PK_LICENSE_ENUM_NOKIA,
	PK_LICENSE_ENUM_NOSL,
	PK_LICENSE_ENUM_NOWEB,
	PK_LICENSE_ENUM_OAL,
	PK_LICENSE_ENUM_OFL,
	PK_LICENSE_ENUM_OFSFDL,
	PK_LICENSE_ENUM_OPENLDAP,
	PK_LICENSE_ENUM_OPENPBS,
	PK_LICENSE_ENUM_OPENSSL,
	PK_LICENSE_ENUM_OREILLY,
	PK_LICENSE_ENUM_OSL_1_DOT_0,
	PK_LICENSE_ENUM_OSL_1_DOT_1,
	PK_LICENSE_ENUM_OSL_2_DOT_0,
	PK_LICENSE_ENUM_OSL_2_DOT_1,
	PK_LICENSE_ENUM_OSL_3_DOT_0,
	PK_LICENSE_ENUM_PHORUM,
	PK_LICENSE_ENUM_PHP,
	PK_LICENSE_ENUM_PLEXUS,
	PK_LICENSE_ENUM_PSUTILS,
	PK_LICENSE_ENUM_PTFL,
	PK_LICENSE_ENUM_PUBLIC_DOMAIN,
	PK_LICENSE_ENUM_PUBLIC_USE,
	PK_LICENSE_ENUM_PYTHON,
	PK_LICENSE_ENUM_QHULL,
	PK_LICENSE_ENUM_QPL,
	PK_LICENSE_ENUM_RDISC,
	PK_LICENSE_ENUM_RICEBSD,
	PK_LICENSE_ENUM_RPSL,
	PK_LICENSE_ENUM_RUBY,
	PK_LICENSE_ENUM_SAXPATH,
	PK_LICENSE_ENUM_SCEA,
	PK_LICENSE_ENUM_SCRIP,
	PK_LICENSE_ENUM_SENDMAIL,
	PK_LICENSE_ENUM_SISSL,
	PK_LICENSE_ENUM_SLEEPYCAT,
	PK_LICENSE_ENUM_SLIB,
	PK_LICENSE_ENUM_SNIA,
	PK_LICENSE_ENUM_SPL,
	PK_LICENSE_ENUM_STIX,
	PK_LICENSE_ENUM_TCL,
	PK_LICENSE_ENUM_TMATE,
	PK_LICENSE_ENUM_TOSL,
	PK_LICENSE_ENUM_TPL,
	PK_LICENSE_ENUM_UCD,
	PK_LICENSE_ENUM_VIM,
	PK_LICENSE_ENUM_VNLSL,
	PK_LICENSE_ENUM_VOSTROM,
	PK_LICENSE_ENUM_VSL,
	PK_LICENSE_ENUM_W3C,
	PK_LICENSE_ENUM_WADALAB,
	PK_LICENSE_ENUM_WEBMIN,
	PK_LICENSE_ENUM_WTFPL,
	PK_LICENSE_ENUM_WXWIDGETS,
	PK_LICENSE_ENUM_XANO,
	PK_LICENSE_ENUM_XEROX,
	PK_LICENSE_ENUM_XINETD,
	PK_LICENSE_ENUM_XSKAT,
	PK_LICENSE_ENUM_YPLV1_DOT_1,
	PK_LICENSE_ENUM_ZEND,
	PK_LICENSE_ENUM_ZLIB,
	PK_LICENSE_ENUM_ZLIB_WITH_ACKNOWLEDGEMENT,
	PK_LICENSE_ENUM_ZPLV1_DOT_0,
	PK_LICENSE_ENUM_ZPLV2_DOT_0,
	PK_LICENSE_ENUM_ZPLV2_DOT_1,
	PK_LICENSE_ENUM_LAST
} PkLicenseEnum;

/**
 * PkMediaTypeEnum:
 *
 * The media type
 **/
typedef enum {
	PK_MEDIA_TYPE_ENUM_UNKNOWN,
	PK_MEDIA_TYPE_ENUM_CD,
	PK_MEDIA_TYPE_ENUM_DVD,
	PK_MEDIA_TYPE_ENUM_DISC,
	PK_MEDIA_TYPE_ENUM_LAST
} PkMediaTypeEnum;

/**
 * PkAuthorizeEnum:
 *
 * The authorization result
 **/
typedef enum {
	PK_AUTHORIZE_ENUM_UNKNOWN,
	PK_AUTHORIZE_ENUM_YES,
	PK_AUTHORIZE_ENUM_NO,
	PK_AUTHORIZE_ENUM_INTERACTIVE,
	PK_AUTHORIZE_ENUM_LAST
} PkAuthorizeEnum;

/* general */
void		 pk_enum_test				(gpointer	 user_data);
guint		 pk_enum_find_value			(const PkEnumMatch *table,
							 const gchar	*string)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*pk_enum_find_string			(const PkEnumMatch *table,
							 guint		 value)
							 G_GNUC_WARN_UNUSED_RESULT;

PkSigTypeEnum	 pk_sig_type_enum_from_string		(const gchar	*sig_type);
const gchar	*pk_sig_type_enum_to_string		(PkSigTypeEnum	 sig_type);

PkInfoEnum	 pk_info_enum_from_string			(const gchar	*info);
const gchar	*pk_info_enum_to_string			(PkInfoEnum	 info);

PkUpdateStateEnum  pk_update_state_enum_from_string	(const gchar	*update_state);
const gchar	*pk_update_state_enum_to_string		(PkUpdateStateEnum update_state);

PkExitEnum	 pk_exit_enum_from_string			(const gchar	*exit);
const gchar	*pk_exit_enum_to_string			(PkExitEnum	 exit);

PkNetworkEnum	 pk_network_enum_from_string		(const gchar	*network);
const gchar	*pk_network_enum_to_string		(PkNetworkEnum	 network);

PkStatusEnum	 pk_status_enum_from_string		(const gchar	*status);
const gchar	*pk_status_enum_to_string			(PkStatusEnum	 status);

PkRoleEnum	 pk_role_enum_from_string			(const gchar	*role);
const gchar	*pk_role_enum_to_string			(PkRoleEnum	 role);

PkErrorEnum	 pk_error_enum_from_string		(const gchar	*code);
const gchar	*pk_error_enum_to_string			(PkErrorEnum code);

PkRestartEnum	 pk_restart_enum_from_string		(const gchar	*restart);
const gchar	*pk_restart_enum_to_string		(PkRestartEnum	 restart);

PkMessageEnum	 pk_message_enum_from_string		(const gchar	*message);
const gchar	*pk_message_enum_to_string		(PkMessageEnum	 message);

PkGroupEnum	 pk_group_enum_from_string		(const gchar	*group);
const gchar	*pk_group_enum_to_string			(PkGroupEnum	 group);

PkFilterEnum	 pk_filter_enum_from_string		(const gchar	*filter);
const gchar	*pk_filter_enum_to_string			(PkFilterEnum	 filter);

PkProvidesEnum	 pk_provides_enum_from_string		(const gchar	*provides);
const gchar	*pk_provides_enum_to_string		(PkProvidesEnum	 provides);

PkLicenseEnum	 pk_license_enum_from_string		(const gchar	*license);
const gchar	*pk_license_enum_to_string		(PkLicenseEnum	 license);

PkDistroUpgradeEnum pk_distro_upgrade_enum_from_string	(const gchar	*upgrade);
const gchar	*pk_distro_upgrade_enum_to_string		(PkDistroUpgradeEnum upgrade);

PkMediaTypeEnum  pk_media_type_enum_from_string		(const gchar	*media_type);
const gchar	*pk_media_type_enum_to_string		(PkMediaTypeEnum media_type);

PkAuthorizeEnum  pk_authorize_type_enum_from_string	(const gchar	*auth_type);
const gchar	*pk_authorize_type_enum_to_string		(PkAuthorizeEnum auth_type);

G_END_DECLS

#endif /* __PK_ENUM_H */
