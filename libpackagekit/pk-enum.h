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

#ifndef __PK_ENUM_H
#define __PK_ENUM_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

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
	PK_ROLE_ENUM_CANCEL,
	PK_ROLE_ENUM_RESOLVE,
	PK_ROLE_ENUM_ROLLBACK,
	PK_ROLE_ENUM_GET_DEPENDS,
	PK_ROLE_ENUM_GET_UPDATE_DETAIL,
	PK_ROLE_ENUM_GET_DESCRIPTION,
	PK_ROLE_ENUM_GET_FILES,
	PK_ROLE_ENUM_GET_REQUIRES,
	PK_ROLE_ENUM_GET_UPDATES,
	PK_ROLE_ENUM_SEARCH_DETAILS,
	PK_ROLE_ENUM_SEARCH_FILE,
	PK_ROLE_ENUM_SEARCH_GROUP,
	PK_ROLE_ENUM_SEARCH_NAME,
	PK_ROLE_ENUM_REFRESH_CACHE,
	PK_ROLE_ENUM_UPDATE_SYSTEM,
	PK_ROLE_ENUM_REMOVE_PACKAGE,
	PK_ROLE_ENUM_INSTALL_PACKAGE,
	PK_ROLE_ENUM_INSTALL_FILE,
	PK_ROLE_ENUM_UPDATE_PACKAGE,
	PK_ROLE_ENUM_GET_REPO_LIST,
	PK_ROLE_ENUM_REPO_ENABLE,
	PK_ROLE_ENUM_REPO_SET_DATA,
	PK_ROLE_ENUM_SERVICE_PACK,
	PK_ROLE_ENUM_WHAT_PROVIDES,
	PK_ROLE_ENUM_UNKNOWN
} PkRoleEnum;

/**
 * PkStatusEnum:
 *
 * What status we are now; this can change for each transaction giving a
 * status of what sort of thing is happening
 * Icons that change to represent the current status of the transaction will
 * use these constants
 * If you add to these, make sure you add filenames in pk-watch also
 **/
typedef enum {
	PK_STATUS_ENUM_SETUP,
	PK_STATUS_ENUM_WAIT,
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
	PK_STATUS_ENUM_ROLLBACK,
	PK_STATUS_ENUM_COMMIT,
	PK_STATUS_ENUM_REQUEST,
	PK_STATUS_ENUM_FINISHED,
	PK_STATUS_ENUM_CANCEL,
	PK_STATUS_ENUM_UNKNOWN
} PkStatusEnum;

/**
 * PkExitEnum:
 *
 * How the backend exited
 **/
typedef enum {
	PK_EXIT_ENUM_SUCCESS,
	PK_EXIT_ENUM_FAILED,
	PK_EXIT_ENUM_QUIT,
	PK_EXIT_ENUM_KILL,
	PK_EXIT_ENUM_UNKNOWN
} PkExitEnum;

/**
 * PkFilterEnum:
 *
 * The filter types
 **/
typedef enum {
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
	PK_FILTER_ENUM_UNKNOWN
} PkFilterEnum;

/**
 * PkRestartEnum:
 *
 * What restart we need to after a transaction
 **/
typedef enum {
	PK_RESTART_ENUM_NONE,
	PK_RESTART_ENUM_APPLICATION,
	PK_RESTART_ENUM_SESSION,
	PK_RESTART_ENUM_SYSTEM,
	PK_RESTART_ENUM_UNKNOWN
} PkRestartEnum;

/**
 * PkMessageEnum:
 *
 * What message type we need to show
 **/
typedef enum {
	PK_MESSAGE_ENUM_NOTICE,
	PK_MESSAGE_ENUM_WARNING,
	PK_MESSAGE_ENUM_DAEMON,
	PK_MESSAGE_ENUM_UNKNOWN
} PkMessageEnum;

/**
 * PkErrorCodeEnum:
 *
 * The error type
 **/
typedef enum {
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
	PK_ERROR_ENUM_UNKNOWN
} PkErrorCodeEnum;

/**
 * PkGroupEnum:
 *
 * The group type
 **/
typedef enum {
	PK_GROUP_ENUM_ACCESSIBILITY,
	PK_GROUP_ENUM_ACCESSORIES,
	PK_GROUP_ENUM_EDUCATION,
	PK_GROUP_ENUM_GAMES,
	PK_GROUP_ENUM_GRAPHICS,
	PK_GROUP_ENUM_INTERNET,
	PK_GROUP_ENUM_OFFICE,
	PK_GROUP_ENUM_OTHER,
	PK_GROUP_ENUM_PROGRAMMING,
	PK_GROUP_ENUM_MULTIMEDIA,
	PK_GROUP_ENUM_SYSTEM,
	PK_GROUP_ENUM_DESKTOP_GNOME,
	PK_GROUP_ENUM_DESKTOP_KDE,
	PK_GROUP_ENUM_DESKTOP_XFCE,
	PK_GROUP_ENUM_DESKTOP_OTHER,
	PK_GROUP_ENUM_PUBLISHING,
	PK_GROUP_ENUM_SERVERS,
	PK_GROUP_ENUM_FONTS,
	PK_GROUP_ENUM_ADMIN_TOOLS,
	PK_GROUP_ENUM_LEGACY,
	PK_GROUP_ENUM_LOCALIZATION,
	PK_GROUP_ENUM_VIRTUALIZATION,
	PK_GROUP_ENUM_SECURITY,
	PK_GROUP_ENUM_POWER_MANAGEMENT,
	PK_GROUP_ENUM_COMMUNICATION,
	PK_GROUP_ENUM_NETWORK,
	PK_GROUP_ENUM_MAPS,
	PK_GROUP_ENUM_REPOS,
	PK_GROUP_ENUM_UNKNOWN
} PkGroupEnum;

/**
 * PkFreqEnum:
 *
 * The frequency type
 **/
typedef enum {
	PK_FREQ_ENUM_HOURLY,
	PK_FREQ_ENUM_DAILY,
	PK_FREQ_ENUM_WEEKLY,
	PK_FREQ_ENUM_NEVER,
	PK_FREQ_ENUM_UNKNOWN
} PkFreqEnum;

/**
 * PkUpdateEnum:
 *
 * The update type
 **/
typedef enum {
	PK_UPDATE_ENUM_ALL,
	PK_UPDATE_ENUM_SECURITY,
	PK_UPDATE_ENUM_NONE,
	PK_UPDATE_ENUM_UNKNOWN
} PkUpdateEnum;

/**
 * PkInfoEnum:
 *
 * The enumerated types used in Package() - these have to refer to a specific
 * package action, rather than a general state
 **/
typedef enum {
	PK_INFO_ENUM_INSTALLED,
	PK_INFO_ENUM_AVAILABLE,
	PK_INFO_ENUM_LOW,
	PK_INFO_ENUM_NORMAL,
	PK_INFO_ENUM_IMPORTANT,
	PK_INFO_ENUM_SECURITY,
	PK_INFO_ENUM_BUGFIX,
	PK_INFO_ENUM_ENHANCEMENT,
	PK_INFO_ENUM_BLOCKED,
	PK_INFO_ENUM_DOWNLOADING,
	PK_INFO_ENUM_UPDATING,
	PK_INFO_ENUM_INSTALLING,
	PK_INFO_ENUM_REMOVING,
	PK_INFO_ENUM_CLEANUP,
	PK_INFO_ENUM_OBSOLETING,
	PK_INFO_ENUM_UNKNOWN
} PkInfoEnum;

/**
 * PkSigTypeEnum:
 *
 * The signature type type
 **/
typedef enum {
	PK_SIGTYPE_ENUM_GPG,
	PK_SIGTYPE_ENUM_UNKNOWN
} PkSigTypeEnum;

/**
 * PkProvidesEnum:
 *
 * The signature type type
 **/
typedef enum {
	PK_PROVIDES_ENUM_MODALIAS,
	PK_PROVIDES_ENUM_CODEC,
	PK_PROVIDES_ENUM_UNKNOWN
} PkProvidesEnum;

typedef enum {
	PK_LICENSE_ENUM_GLIDE,
	PK_LICENSE_ENUM_AFL,
	PK_LICENSE_ENUM_AMPAS_BSD,
	PK_LICENSE_ENUM_ADOBE,
	PK_LICENSE_ENUM_AGPLV1,
	PK_LICENSE_ENUM_AGPLV3,
	PK_LICENSE_ENUM_ASL_1_DOT_0,
	PK_LICENSE_ENUM_ASL_1_DOT_1,
	PK_LICENSE_ENUM_ASL_2_DOT_0,
	PK_LICENSE_ENUM_APSL_2_DOT_0,
	PK_LICENSE_ENUM_ARTISTIC_CLARIFIED,
	PK_LICENSE_ENUM_ARTISTIC_2_DOT_0,
	PK_LICENSE_ENUM_ARL,
	PK_LICENSE_ENUM_BITTORRENT,
	PK_LICENSE_ENUM_BOOST,
	PK_LICENSE_ENUM_BSD_WITH_ADVERTISING,
	PK_LICENSE_ENUM_BSD,
	PK_LICENSE_ENUM_CECILL,
	PK_LICENSE_ENUM_CDDL,
	PK_LICENSE_ENUM_CPL,
	PK_LICENSE_ENUM_CONDOR,
	PK_LICENSE_ENUM_COPYRIGHT_ONLY,
	PK_LICENSE_ENUM_CRYPTIX,
	PK_LICENSE_ENUM_CRYSTAL_STACKER,
	PK_LICENSE_ENUM_WTFPL,
	PK_LICENSE_ENUM_EPL,
	PK_LICENSE_ENUM_ECOS,
	PK_LICENSE_ENUM_EFL_2_DOT_0,
	PK_LICENSE_ENUM_EU_DATAGRID,
	PK_LICENSE_ENUM_LGPLV2_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_FTL,
	PK_LICENSE_ENUM_GIFTWARE,
	PK_LICENSE_ENUM_GPLV2,
	PK_LICENSE_ENUM_GPLV2_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPLV2_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPLV3,
	PK_LICENSE_ENUM_GPLV3_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_GPLV3_PLUS_WITH_EXCEPTIONS,
	PK_LICENSE_ENUM_LGPLV2,
	PK_LICENSE_ENUM_LGPLV3,
	PK_LICENSE_ENUM_GNUPLOT,
	PK_LICENSE_ENUM_IBM,
	PK_LICENSE_ENUM_IMATIX,
	PK_LICENSE_ENUM_IMAGEMAGICK,
	PK_LICENSE_ENUM_IMLIB2,
	PK_LICENSE_ENUM_IJG,
	PK_LICENSE_ENUM_INTEL_ACPI,
	PK_LICENSE_ENUM_INTERBASE,
	PK_LICENSE_ENUM_ISC,
	PK_LICENSE_ENUM_JABBER,
	PK_LICENSE_ENUM_JASPER,
	PK_LICENSE_ENUM_LPPL,
	PK_LICENSE_ENUM_LIBTIFF,
	PK_LICENSE_ENUM_LPL,
	PK_LICENSE_ENUM_MECAB_IPADIC,
	PK_LICENSE_ENUM_MIT,
	PK_LICENSE_ENUM_MPLV1_DOT_0,
	PK_LICENSE_ENUM_MPLV1_DOT_1,
	PK_LICENSE_ENUM_NCSA,
	PK_LICENSE_ENUM_NGPL,
	PK_LICENSE_ENUM_NOSL,
	PK_LICENSE_ENUM_NETSCAPE,
	PK_LICENSE_ENUM_NOKIA,
	PK_LICENSE_ENUM_OPENLDAP,
	PK_LICENSE_ENUM_OPENPBS,
	PK_LICENSE_ENUM_OSL_1_DOT_0,
	PK_LICENSE_ENUM_OSL_1_DOT_1,
	PK_LICENSE_ENUM_OSL_2_DOT_0,
	PK_LICENSE_ENUM_OSL_3_DOT_0,
	PK_LICENSE_ENUM_OPENSSL,
	PK_LICENSE_ENUM_OREILLY,
	PK_LICENSE_ENUM_PHORUM,
	PK_LICENSE_ENUM_PHP,
	PK_LICENSE_ENUM_PUBLIC_DOMAIN,
	PK_LICENSE_ENUM_PYTHON,
	PK_LICENSE_ENUM_QPL,
	PK_LICENSE_ENUM_RPSL,
	PK_LICENSE_ENUM_RUBY,
	PK_LICENSE_ENUM_SLEEPYCAT,
	PK_LICENSE_ENUM_SLIB,
	PK_LICENSE_ENUM_SISSL,
	PK_LICENSE_ENUM_SPL,
	PK_LICENSE_ENUM_TCL,
	PK_LICENSE_ENUM_UCD,
	PK_LICENSE_ENUM_VIM,
	PK_LICENSE_ENUM_VNLSL,
	PK_LICENSE_ENUM_VSL,
	PK_LICENSE_ENUM_W3C,
	PK_LICENSE_ENUM_WXWIDGETS,
	PK_LICENSE_ENUM_XINETD,
	PK_LICENSE_ENUM_ZEND,
	PK_LICENSE_ENUM_ZPLV1_DOT_0,
	PK_LICENSE_ENUM_ZPLV2_DOT_0,
	PK_LICENSE_ENUM_ZPLV2_DOT_1,
	PK_LICENSE_ENUM_ZLIB,
	PK_LICENSE_ENUM_CDL,
	PK_LICENSE_ENUM_FBSDDL,
	PK_LICENSE_ENUM_GFDL,
	PK_LICENSE_ENUM_IEEE,
	PK_LICENSE_ENUM_OFSFDL,
	PK_LICENSE_ENUM_OPEN_PUBLICATION,
	PK_LICENSE_ENUM_CC_BY,
	PK_LICENSE_ENUM_CC_BY_SA,
	PK_LICENSE_ENUM_CC_BY_ND,
	PK_LICENSE_ENUM_DSL,
	PK_LICENSE_ENUM_FREE_ART,
	PK_LICENSE_ENUM_OFL,
	PK_LICENSE_ENUM_UTOPIA,
	PK_LICENSE_ENUM_ARPHIC,
	PK_LICENSE_ENUM_BAEKMUK,
	PK_LICENSE_ENUM_BITSTREAM_VERA,
	PK_LICENSE_ENUM_LUCIDA,
	PK_LICENSE_ENUM_MPLUS,
	PK_LICENSE_ENUM_STIX,
	PK_LICENSE_ENUM_XANO,
	PK_LICENSE_ENUM_UNKNOWN
} PkLicenseEnum;

/* general */
guint		 pk_enum_find_value			(PkEnumMatch	*table,
							 const gchar	*string)
							 G_GNUC_WARN_UNUSED_RESULT;
const gchar	*pk_enum_find_string			(PkEnumMatch	*table,
							 guint		 value)
							 G_GNUC_WARN_UNUSED_RESULT;

PkSigTypeEnum    pk_sig_type_enum_from_text             (const gchar    *sig_type);
const gchar     *pk_sig_type_enum_to_text               (PkSigTypeEnum   sig_type);

PkInfoEnum	 pk_info_enum_from_text			(const gchar	*info);
const gchar	*pk_info_enum_to_text			(PkInfoEnum	 info);

PkUpdateEnum	 pk_update_enum_from_text		(const gchar	*update);
const gchar	*pk_update_enum_to_text			(PkUpdateEnum	 update);

PkFreqEnum	 pk_freq_enum_from_text			(const gchar	*freq);
const gchar	*pk_freq_enum_to_text			(PkFreqEnum	 freq);

PkExitEnum	 pk_exit_enum_from_text			(const gchar	*exit);
const gchar	*pk_exit_enum_to_text			(PkExitEnum	 exit);

PkStatusEnum	 pk_status_enum_from_text		(const gchar	*status);
const gchar	*pk_status_enum_to_text			(PkStatusEnum	 status);

PkRoleEnum	 pk_role_enum_from_text			(const gchar	*role);
const gchar	*pk_role_enum_to_text			(PkRoleEnum	 role);

PkErrorCodeEnum	 pk_error_enum_from_text		(const gchar	*code);
const gchar	*pk_error_enum_to_text			(PkErrorCodeEnum code);

PkRestartEnum	 pk_restart_enum_from_text		(const gchar	*restart);
const gchar	*pk_restart_enum_to_text		(PkRestartEnum	 restart);

PkMessageEnum	 pk_message_enum_from_text		(const gchar	*message);
const gchar	*pk_message_enum_to_text		(PkMessageEnum	 message);

PkGroupEnum	 pk_group_enum_from_text		(const gchar	*group);
const gchar	*pk_group_enum_to_text			(PkGroupEnum	 group);

PkFilterEnum	 pk_filter_enum_from_text		(const gchar	*filter);
const gchar	*pk_filter_enum_to_text			(PkFilterEnum	 filter);

PkProvidesEnum	 pk_provides_enum_from_text		(const gchar	*provides);
const gchar	*pk_provides_enum_to_text		(PkProvidesEnum	 provides);

PkLicenseEnum	 pk_license_enum_from_text		(const gchar	*license);
const gchar	*pk_license_enum_to_text		(PkLicenseEnum	 license);

G_END_DECLS

#endif /* __PK_ENUM_H */
