/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2010-2012 Daniel Nicoletti <dantti12@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef PACKAGEKIT_TRANSACTION_H
#define PACKAGEKIT_TRANSACTION_H

#include <QtCore/QObject>
#include <QtCore/QDateTime>
#include <QtDBus/QDBusObjectPath>

#include "bitfield.h"

namespace PackageKit {

/**
* \class Transaction transaction.h Transaction
* \author Adrien Bustany \e <madcat@mymadcat.com>
* \author Daniel Nicoletti \e <dantti12@gmail.com>
*
* \brief A transaction represents an occurring action in PackageKit
*
* A Transaction is created whenever you do an asynchronous action (for example a Search, Install...).
* This class allows you to monitor and control the flow of the action.
*
* You should delete the transaction after finished() is emitted,
* or use the reset() method to reuse it
*
* \sa Daemon
*/
class TransactionPrivate;
class Transaction : public QObject
{
    Q_OBJECT
    Q_ENUMS(InternalsError)
    Q_ENUMS(Role)
    Q_ENUMS(Error)
    Q_ENUMS(Exit)
    Q_ENUMS(Message)
    Q_ENUMS(Status)
    Q_ENUMS(MediaType)
    Q_ENUMS(Provides)
    Q_ENUMS(DistroUpgrade)
    Q_ENUMS(TransactionFlag)
    Q_ENUMS(Restart)
    Q_ENUMS(UpdateState)
    Q_ENUMS(Group)
    Q_ENUMS(Info)
    Q_ENUMS(SigType)
    Q_FLAGS(TransactionFlag TransactionFlags)
    Q_FLAGS(Filter Filters)
    Q_PROPERTY(bool allowCancel READ allowCancel NOTIFY changed)
    Q_PROPERTY(bool isCallerActive READ isCallerActive NOTIFY changed)
    Q_PROPERTY(QString lastPackage READ lastPackage NOTIFY changed)
    Q_PROPERTY(uint percentage READ percentage NOTIFY changed)
    Q_PROPERTY(uint elapsedTime READ elapsedTime NOTIFY changed)
    Q_PROPERTY(uint remainingTime READ remainingTime NOTIFY changed)
    Q_PROPERTY(uint speed READ speed NOTIFY changed)
    Q_PROPERTY(qulonglong downloadSizeRemaining READ downloadSizeRemaining)
    Q_PROPERTY(Role role READ role NOTIFY changed)
    Q_PROPERTY(Status status READ status NOTIFY changed)
public:
    /**
     * Describes an error at the daemon level (for example, PackageKit crashes or is unreachable)
     *
     * \sa Transaction::error
     */
    enum InternalError {
        InternalErrorNone = 0,
        InternalErrorUnkown,
        InternalErrorFailed,
        InternalErrorFailedAuth,
        InternalErrorNoTid,
        InternalErrorAlreadyTid,
        InternalErrorRoleUnkown,
        InternalErrorCannotStartDaemon,
        InternalErrorInvalidInput,
        InternalErrorInvalidFile,
        InternalErrorFunctionNotSupported,
        InternalErrorDaemonUnreachable
    };

    /**
     * Describes the role of the transaction
     */
    typedef enum {
        RoleUnknown,
        RoleCancel,
        RoleGetDepends,
        RoleGetDetails,
        RoleGetFiles,
        RoleGetPackages,
        RoleGetRepoList,
        RoleGetRequires,
        RoleGetUpdateDetail,
        RoleGetUpdates,
        RoleInstallFiles,
        RoleInstallPackages,
        RoleInstallSignature,
        RoleRefreshCache,
        RoleRemovePackages,
        RoleRepoEnable,
        RoleRepoSetData,
        RoleResolve,
        RoleSearchDetails,
        RoleSearchFile,
        RoleSearchGroup,
        RoleSearchName,
        RoleUpdatePackages,
        RoleWhatProvides,
        RoleAcceptEula,
        RoleDownloadPackages,
        RoleGetDistroUpgrades,
        RoleGetCategories,
        RoleGetOldTransactions,
        RoleUpgradeSystem, // Since 0.6.11
        RoleRepairSystem   // Since 0.7.2
    } Role;
    typedef Bitfield Roles;

    /**
     * Describes the different types of error
     */
    enum Error {
        ErrorUnknown,
        ErrorOom,
        ErrorNoNetwork,
        ErrorNotSupported,
        ErrorInternalError,
        ErrorGpgFailure,
        ErrorPackageIdInvalid,
        ErrorPackageNotInstalled,
        ErrorPackageNotFound,
        ErrorPackageAlreadyInstalled,
        ErrorPackageDownloadFailed,
        ErrorGroupNotFound,
        ErrorGroupListInvalid,
        ErrorDepResolutionFailed,
        ErrorFilterInvalid,
        ErrorCreateThreadFailed,
        ErrorTransactionError,
        ErrorTransactionCancelled,
        ErrorNoCache,
        ErrorRepoNotFound,
        ErrorCannotRemoveSystemPackage,
        ErrorProcessKill,
        ErrorFailedInitialization,
        ErrorFailedFinalise,
        ErrorFailedConfigParsing,
        ErrorCannotCancel,
        ErrorCannotGetLock,
        ErrorNoPackagesToUpdate,
        ErrorCannotWriteRepoConfig,
        ErrorLocalInstallFailed,
        ErrorBadGpgSignature,
        ErrorMissingGpgSignature,
        ErrorCannotInstallSourcePackage,
        ErrorRepoConfigurationError,
        ErrorNoLicenseAgreement,
        ErrorFileConflicts,
        ErrorPackageConflicts,
        ErrorRepoNotAvailable,
        ErrorInvalidPackageFile,
        ErrorPackageInstallBlocked,
        ErrorPackageCorrupt,
        ErrorAllPackagesAlreadyInstalled,
        ErrorFileNotFound,
        ErrorNoMoreMirrorsToTry,
        ErrorNoDistroUpgradeData,
        ErrorIncompatibleArchitecture,
        ErrorNoSpaceOnDevice,
        ErrorMediaChangeRequired,
        ErrorNotAuthorized,
        ErrorUpdateNotFound,
        ErrorCannotInstallRepoUnsigned,
        ErrorCannotUpdateRepoUnsigned,
        ErrorCannotGetFilelist,
        ErrorCannotGetRequires,
        ErrorCannotDisableRepository,
        ErrorRestrictedDownload,
        ErrorPackageFailedToConfigure,
        ErrorPackageFailedToBuild,
        ErrorPackageFailedToInstall,
        ErrorPackageFailedToRemove,
        ErrorUpdateFailedDueToRunningProcess,
        ErrorPackageDatabaseChanged,
        ErrorProvideTypeNotSupported,
        ErrorInstallRootInvalid,
        ErrorCannotFetchSources,
        ErrorCancelledPriority,
        ErrorUnfinishedTransaction,
        ErrorLockRequired
    };

    /**
     * Describes how the transaction finished
     * \sa Transaction::finished()
     */
    enum Exit {
        ExitUnknown,
        ExitSuccess,
        ExitFailed,
        ExitCancelled,
        ExitKeyRequired,
        ExitEulaRequired,
        ExitKilled, /* when we forced the cancel, but had to sigkill */
        ExitMediaChangeRequired,
        ExitNeedUntrusted,
        ExitCancelledPriority,
        ExitRepairRequired
    };

    /**
     * Describes the different package filters
     */
    enum Filter {
        FilterUnknown        = 0x0000001,
        FilterNone           = 0x0000002,
        FilterInstalled      = 0x0000004,
        FilterNotInstalled   = 0x0000008,
        FilterDevel          = 0x0000010,
        FilterNotDevel       = 0x0000020,
        FilterGui            = 0x0000040,
        FilterNotGui         = 0x0000080,
        FilterFree           = 0x0000100,
        FilterNotFree        = 0x0000200,
        FilterVisible        = 0x0000400,
        FilterNotVisible     = 0x0000800,
        FilterSupported      = 0x0001000,
        FilterNotSupported   = 0x0002000,
        FilterBasename       = 0x0004000,
        FilterNotBasename    = 0x0008000,
        FilterNewest         = 0x0010000,
        FilterNotNewest      = 0x0020000,
        FilterArch           = 0x0040000,
        FilterNotArch        = 0x0080000,
        FilterSource         = 0x0100000,
        FilterNotSource      = 0x0200000,
        FilterCollections    = 0x0400000,
        FilterNotCollections = 0x0800000,
        FilterApplication    = 0x1000000,
        FilterNotApplication = 0x2000000,
        /* this always has to be at the end of the list */
        FilterLast           = 0x4000000
    };
    Q_DECLARE_FLAGS(Filters, Filter)

    /**
     * Describes a message's type
     */
    enum Message {
        MessageUnknown,
        MessageBrokenMirror,
        MessageConnectionRefused,
        MessageParameterInvalid,
        MessagePriorityInvalid,
        MessageBackendError,
        MessageDaemonError,
        MessageCacheBeingRebuilt,
        MessageUntrustedPackage,
        MessageNewerPackageExists,
        MessageCouldNotFindPackage,
        MessageConfigFilesChanged,
        MessagePackageAlreadyInstalled,
        MessageAutoremoveIgnored,
        MessageRepoMetadataDownloadFailed,
        MessageRepoForDevelopersOnly,
        MessageOtherUpdatesHeldBack
    };

    /**
     * Describes the current state of the transaction
     */
    enum Status {
        StatusUnknown,
        StatusWait,
        StatusSetup,
        StatusRunning,
        StatusQuery,
        StatusInfo,
        StatusRemove,
        StatusRefreshCache,
        StatusDownload,
        StatusInstall,
        StatusUpdate,
        StatusCleanup,
        StatusObsolete,
        StatusDepResolve,
        StatusSigCheck,
        StatusTestCommit,
        StatusCommit,
        StatusRequest,
        StatusFinished,
        StatusCancel,
        StatusDownloadRepository,
        StatusDownloadPackagelist,
        StatusDownloadFilelist,
        StatusDownloadChangelog,
        StatusDownloadGroup,
        StatusDownloadUpdateinfo,
        StatusRepackaging,
        StatusLoadingCache,
        StatusScanApplications,
        StatusGeneratePackageList,
        StatusWaitingForLock,
        StatusWaitingForAuth,
        StatusScanProcessList,
        StatusCheckExecutableFiles,
        StatusCheckLibraries,
        StatusCopyFiles
    };

    /**
     * Describes what kind of media is required
     */
    enum MediaType {
        MediaTypeUnknown,
        MediaTypeCd,
        MediaTypeDvd,
        MediaTypeDisc
    };

    /**
     * Enum used to describe a "provides" request
     * \sa whatProvides
     */
    enum Provides {
        ProvidesUnknown,
        ProvidesAny,
        ProvidesModalias,
        ProvidesCodec,
        ProvidesMimetype,
        ProvidesFont,
        ProvidesHardwareDriver,
        ProvidesPostscriptDriver,
        ProvidesPlasmaService,
        ProvidesSharedLib,
        ProvidesPythonModule,
        ProvidesLanguageSupport
    };

    /**
     * Describes an distro upgrade state
     */
    enum DistroUpgrade {
        DistroUpgradeUnknown,
        DistroUpgradeStable,
        DistroUpgradeUnstable
    };

    /**
     * Describes the type of distribution upgrade to perform
     * \sa upgradeSystem()
     */
    enum UpgradeKind {
        UpgradeKindUnknown,
        UpgradeKindMinimal,
        UpgradeKindDefault,
        UpgradeKindComplete
    };

    /**
     * Describes the type of distribution upgrade to perform
     * \sa upgradeSystem()
     */
    enum TransactionFlag {
        TransactionFlagNone         = 1 << 0, // Since: 0.8.1
        TransactionFlagOnlyTrusted  = 1 << 1, // Since: 0.8.1
        TransactionFlagSimulate     = 1 << 2, // Since: 0.8.1
        TransactionFlagOnlyDownload = 1 << 3  // Since: 0.8.1
    };
    Q_DECLARE_FLAGS(TransactionFlags, TransactionFlag)

    /**
     * Describes a restart type
     */
    enum Restart {
        RestartUnknown,
        RestartNone,
        RestartApplication,
        RestartSession,
        RestartSystem,
        RestartSecuritySession, /* a library that is being used by this package has been updated for security */
        RestartSecuritySystem
    };

    /**
     * Describes an update's state
     */
    enum UpdateState {
        UpdateStateUnknown,
        UpdateStateStable,
        UpdateStateUnstable,
        UpdateStateTesting
    };

    /**
     * Describes the different package groups
     */
    enum Group {
        GroupUnknown,
        GroupAccessibility,
        GroupAccessories,
        GroupAdminTools,
        GroupCommunication,
        GroupDesktopGnome,
        GroupDesktopKde,
        GroupDesktopOther,
        GroupDesktopXfce,
        GroupEducation,
        GroupFonts,
        GroupGames,
        GroupGraphics,
        GroupInternet,
        GroupLegacy,
        GroupLocalization,
        GroupMaps,
        GroupMultimedia,
        GroupNetwork,
        GroupOffice,
        GroupOther,
        GroupPowerManagement,
        GroupProgramming,
        GroupPublishing,
        GroupRepos,
        GroupSecurity,
        GroupServers,
        GroupSystem,
        GroupVirtualization,
        GroupScience,
        GroupDocumentation,
        GroupElectronics,
        GroupCollections,
        GroupVendor,
        GroupNewest
    };
    typedef Bitfield Groups;

    /**
     * Describes the state of a package
     */
    enum Info {
        InfoUnknown,
        InfoInstalled,
        InfoAvailable,
        InfoLow,
        InfoEnhancement,
        InfoNormal,
        InfoBugfix,
        InfoImportant,
        InfoSecurity,
        InfoBlocked,
        InfoDownloading,
        InfoUpdating,
        InfoInstalling,
        InfoRemoving,
        InfoCleanup,
        InfoObsoleting,
        InfoCollectionInstalled,
        InfoCollectionAvailable,
        InfoFinished,
        InfoReinstalling,
        InfoDowngrading,
        InfoPreparing,
        InfoDecompressing,
        InfoUntrusted,
        InfoTrusted
    };

    /**
     * Describes a signature type
     */
    enum SigType {
        SigTypeUnknown,
        SigTypeGpg
    };

    /**
     * Create a transaction object with a new transaction id
     *
     * The transaction object \b cannot be reused
     * (i.e. removePackages then installPackages)
     *
     * \warning after creating the transaction object be sure
     * to verify if it doesn't have any error()
     */
    Transaction(QObject *parent = 0);

    /**
     * Create a transaction object with transaction id \p tid
     * \note The if \p tid is a NULL string then it will automatically
     * asks PackageKit for a tid
     *
     * The transaction object \b cannot be reused
     * (i.e. removePackages then installPackages)
     *
     * \warning after creating the transaction object be sure
     * to verify if it doesn't have any error()
     */
    Transaction(const QDBusObjectPath &tid, QObject *parent = 0);

    /**
     * Destructor
     */
    ~Transaction();

    /**
     * \brief Returns the TID of the Transaction
     *
     * The TID (Transaction ID) uniquely identifies the transaction.
     *
     * \return the TID of the current transaction
     */
    QDBusObjectPath tid() const;

    /**
     * \brief Returns the error status of the Transaction
     *
     * \return A value from TransactionError describing the state of the transaction
     * or 0 in case of not having an error
     */
    Transaction::InternalError error() const;

    /**
     * Indicates whether you can cancel the transaction or not
     * i.e. the backend forbids cancelling the transaction while
     * it's installing packages
     *
     * \return true if you are able cancel the transaction, false else
     */
    bool allowCancel() const;

    /**
     * Indicates weither the transaction caller is active or not
     *
     * The caller can be inactive if it has quitted before the transaction finished.
     *
     * \return true if the caller is active, false else
     */
    bool isCallerActive() const;

    /**
     * Returns the last package processed by the transaction
     *
     * This is mostly used when getting an already existing Transaction, to
     * display a more complete summary of the transaction.
     *
     * \return the last package processed by the transaction
     */
    QString lastPackage() const;

    /**
     * The percentage complete of the whole transaction.
     * \return percentage, or 101 if not known.
     */
    uint percentage() const;

    /**
     * The amount of time elapsed during the transaction in seconds.
     * \return time in seconds.
     */
    uint elapsedTime() const;

    /**
     * The estimated time remaining of the transaction in seconds, or 0 if not known.
     * \return time in seconds, or 0 if not known.
     */
    uint remainingTime() const;

    /**
     * Returns the estimated speed of the transaction (copying, downloading, etc.)
     * \return speed bits per second, or 0 if not known.
     */
    uint speed() const;
    
    /**
     * Returns the number of bytes remaining to download
     * \return bytes to download, or 0 if nothing is left to download.
     */
    qulonglong downloadSizeRemaining() const;

    /**
     * Returns information describing the transaction
     * like InstallPackages, SearchName or GetUpdates
     * \return the current role of the transaction
     */
    Transaction::Role role() const;

    /**
     * Returns the current state of the transaction
     * \return a Transaction::Status value describing the status of the transaction
     */
    Status status() const;

    /**
     * \brief Tells the underlying package manager to use the given \p hints
     *
     * This method allows the calling session to set transaction \p hints for
     * the package manager which can change as the transaction runs.
     *
     * This method can be sent before the transaction has been run
     * (by using Daemon::setHints) or whilst it is running
     * (by using Transaction::setHints).
     * There is no limit to the number of times this
     * method can be sent, although some backends may only use the values
     * that were set before the transaction was started.
     *
     * The \p hints can be filled with entries like these
     * ('locale=en_GB.utf8','idle=true','interactive=false').
     *
     * \sa Daemon::setHints
     */
    Q_INVOKABLE void setHints(const QStringList &hints);

    /**
     * Convenience function to set this transaction \p hints
     * \sa getDetails(const QStringList &hints)
     */
    Q_INVOKABLE void setHints(const QString &hints);

    /**
     * Reset the transaction for reuse
     */
    Q_INVOKABLE void reset();

    /**
     * Returns the date at which the transaction was created
     * \return a QDateTime object containing the date at which the transaction was created
     * \note This function only returns a real value for old transactions returned by getOldTransactions
     */
    QDateTime timespec() const;

    /**
     * Returns weither the transaction succeded or not
     * \return true if the transaction succeeded, false else
     * \note This function only returns a real value for old transactions returned by getOldTransactions
     */
    bool succeeded() const;

    /**
     * Returns the time the transaction took to finish
     * \return the number of milliseconds the transaction took to finish
     * \note This function only returns a real value for old transactions returned by getOldTransactions
     */
    uint duration() const;

    /**
     * Returns some data set by the backend to pass additionnal information
     * \return a string set by the backend
     * \note This function only returns a real value for old transactions returned by getOldTransactions
     */
    QString data() const;

    /**
     * Returns the UID of the calling process
     * \return the uid of the calling process
     * \note This function only returns a real value for old transactions returned by getOldTransactions
     */
    uint uid() const;

    /**
     * Returns the command line for the calling process
     * \return a string of the command line for the calling process
     * \note This function only returns a real value for old transactions returned by getOldTransactions
     */
    QString cmdline() const;

    /**
     * \brief Accepts an EULA
     *
     * The EULA is identified by the \sa Eula structure \p info
     *
     * \note You need to manually restart the transaction which triggered the EULA.
     * \sa eulaRequired()
     */
    void acceptEula(const QString &eulaID);

    /**
     * Download the given \p packages to a temp dir, if \p storeInCache is true
     * the download will be stored in the package manager cache
     */
    void downloadPackages(const QStringList &packageIDs, bool storeInCache = false);

    /**
     * This is a convenience function to download this \p package
     * \sa downloadPackages(const QStringList &packageIDs, bool storeInCache = false)
     */
    Q_INVOKABLE void downloadPackage(const QString &packageID, bool storeInCache = false);

    /**
     * Returns the collection categories
     *
     * \sa category
     */
    Q_INVOKABLE void getCategories();

    /**
     * \brief Gets the list of dependencies for the given \p packages
     *
     * You can use the \p filters to limit the results to certain packages.
     * The \p recursive flag indicates if the package manager should also
     * fetch the dependencies's dependencies.
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void getDepends(const QStringList &packageIDs, Filters filters, bool recursive = false);

    /**
     * Convenience function to get the dependencies of this \p package
     * \sa getDetails(const QStringList &packageIDs, Filters filters, bool recursive = false)
     */
    Q_INVOKABLE void getDepends(const QString &packageID, Filters filters , bool recursive = false);

    /**
     * Gets more details about the given \p packages
     *
     * \sa Transaction::details
     * \note This method emits \sa package()
     * with details set
     */
    Q_INVOKABLE void getDetails(const QStringList &packageIDs);

    /**
     * Convenience function to get the details about this \p package
     * \sa getDetails(const QStringList &packageIDs)
     */
    Q_INVOKABLE void getDetails(const QString &packageID);

    /**
     * Gets the files contained in the given \p packages
     *
     * \note This method emits \sa files()
     */
    Q_INVOKABLE void getFiles(const QStringList &packageIDs);

    /**
     * Convenience function to get the files contained in this \p package
     * \sa getRequires(const QStringList &packageIDs)
     */
    Q_INVOKABLE void getFiles(const QString &packageIDs);

    /**
     * \brief Gets the last \p number finished transactions
     *
     * \note You must delete these transactions yourself
     * \note This method emits \sa transaction()
     */
    Q_INVOKABLE void getOldTransactions(uint number);

    /**
     * Gets all the packages matching the given \p filters
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void getPackages(Filters filters = FilterNone);

    /**
     * Gets the list of software repositories matching the given \p filters
     *
     * \note This method emits \sa repository()
     */
    Q_INVOKABLE void getRepoList(Filters filter = FilterNone);

    /**
     * \brief Searches for the packages requiring the given \p packages
     *
     * The search can be limited using the \p filters parameter.
     * The \p recursive flag is used to tell if the package manager should
     * also search for the package requiring the resulting packages.
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void getRequires(const QStringList &packageIDs, Filters filters, bool recursive = false);

    /**
     * Convenience function to get packages requiring this package
     * \sa getRequires(const QStringList &packageIDs, Filters filters, bool recursive = false)
     */
    Q_INVOKABLE void getRequires(const QString &packageID, Filters filters, bool recursive = false);

    /**
     * Retrieves more details about the update for the given \p packageIDs
     *
     * \note This method emits \sa updateDetail()
     */
    Q_INVOKABLE void getUpdatesDetails(const QStringList &packageIDs);

    /**
     * Convenience function to get update details
     * \sa getUpdateDetail(const QStringList &packageIDs)
     */
    Q_INVOKABLE void getUpdateDetail(const QString &packageID);

    /**
     * \p Gets the available updates
     *
     * The \p filters parameters can be used to restrict the updates returned
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void getUpdates(Filters filters = FilterNone);

    /**
     * Retrieves the available distribution upgrades
     *
     * \note This method emits \sa distroUpgrade()
     */
    Q_INVOKABLE void getDistroUpgrades();

    /**
     * \brief Installs the local packages \p files
     *
     * \p onlyTrusted indicate if the packages are signed by a trusted authority
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    Q_INVOKABLE void installFiles(const QStringList &files, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to install a file
     * \sa installFiles(const QStringList &files, TransactionFlags flags)
     */
    Q_INVOKABLE void installFile(const QString &file, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Install the given \p packages
     *
     * \p only_trusted indicates if we should allow installation of untrusted packages (requires a different authorization)
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    Q_INVOKABLE void installPackages(const QStringList &packageIDs, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to install a package
     * \sa installPackages(const QStringList &packageIDs, TransactionFlags flags)
     */
    Q_INVOKABLE void installPackage(const QString &packageID, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * \brief Installs a signature
     *
     * \p type, \p keyId and \p package generally come from the Transaction::repoSignatureRequired
     */
    Q_INVOKABLE void installSignature(SigType type, const QString &keyID, const QString &packageID);

    /**
     * Refreshes the package manager's cache
     *
     * \note This method emits \sa changed()
     */
    Q_INVOKABLE void refreshCache(bool force);

    /**
     * \brief Removes the given \p packages
     *
     * \p allowDeps if the package manager has the right to remove other packages which depend on the
     * packages to be removed. \p autoRemove tells the package manager to remove all the package which
     * won't be needed anymore after the packages are uninstalled.
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    Q_INVOKABLE void removePackages(const QStringList &packageIDs, bool allowDeps = false, bool autoRemove = false, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to remove a package
     * \sa removePackages(const PackageList  &packages, bool allowDeps = false, bool autoRemove = false, TransactionFlags flags)
     */
    Q_INVOKABLE void removePackage(const QString &packageID, bool allowDeps = false, bool autoRemove = false, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Repairs a broken system
     */
    Q_INVOKABLE void repairSystem(TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Activates or disables a repository
     */
    Q_INVOKABLE void repoEnable(const QString &repoId, bool enable = true);

    /**
     * Sets a repository's parameter
     */
    Q_INVOKABLE void repoSetData(const QString &repoId, const QString &parameter, const QString &value);

    /**
     * \brief Tries to create a Package object from the package's name
     *
     * The \p filters can be used to restrict the search
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void resolve(const QStringList &packageNames, Filters filters = FilterNone);

    /**
     * Convenience function to remove a package name
     * \sa resolve(const QStringList &packageNames, Filters filters = FilterNone)
     */
    Q_INVOKABLE void resolve(const QString &packageName, Filters filters = FilterNone);

    /**
     * \brief Search in the packages files
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void searchFiles(const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search for a file
     * \sa searchFiles(const QStringList &search, Filters filters = FilterNone)
     */
    Q_INVOKABLE void searchFiles(const QString &search, Filters filters = FilterNone);

    /**
     * \brief Search in the packages details
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void searchDetails(const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search by details
     * \sa searchDetails(const QStringList &search, Filters filters = FilterNone)
     */
    Q_INVOKABLE void searchDetails(const QString &search, Filters filters = FilterNone);

    /**
     * \brief Lists all the packages in the given \p group
     *
     * \p groups is the name of the group that you want, when searching for
     * categories prefix it with '@'
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void searchGroups(const QStringList &groups, Filters filters = FilterNone);

    /**
     * Convenience function to search by group string
     * \sa searchGroups(const QStringList &groups, Filters filters = FilterNone)
     */
    Q_INVOKABLE void searchGroup(const QString &group, Filters filters = FilterNone);
    
    /**
     * Convenience function to search by group enum
     * \sa searchGroups(const QStringList &groups, Filters filters = FilterNone)
     */
    Q_INVOKABLE void searchGroup(Group group, Filters filters = FilterNone);

    /**
     * \brief Lists all the packages in the given \p group
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void searchGroups(Groups group, Filters filters = FilterNone);

    /**
     * \brief Search in the packages names
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void searchNames(const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search by names
     * \sa searchNames(const QStringList &search, Filters filters)
     */
    Q_INVOKABLE void searchNames(const QString &search, Filters filters = FilterNone);

    /**
     * Update the given \p packages
     *
     * \p onlyTrusted indicates if this transaction is only allowed to install trusted packages
     * \note This method emits \sa package()
     * and \sa changed()
     */
    Q_INVOKABLE void updatePackages(const QStringList &packageIDs, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to update a package
     * \sa updatePackages(const QStringList &packageIDs, TransactionFlags flags)
     */
    Q_INVOKABLE void updatePackage(const QString &packageID, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Updates the whole system
     *
     * This method perfoms a distribution upgrade to the
     * specified version.
     *
     * The \p type of upgrade, e.g. minimal, default or complete.
     * Minimal upgrades will download the smallest amount of data
     * before launching a installer.
     * The default is to download enough data to launch a full
     * graphical installer, but a complete upgrade will be
     * required if there is no internet access during install time.
     *
     * \note This method typically emits
     * \li changed()
     * \li error()
     * \li package()
     */
    Q_INVOKABLE void upgradeSystem(const QString &distroId, UpgradeKind kind);

    /**
     * Searchs for a package providing a file/a mimetype
     *
     * \note This method emits \sa package()
     */
    Q_INVOKABLE void whatProvides(Provides type, const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search for what provides
     * \sa whatProvides(Provides type, const QStringList &search, Filters filters = FilterNone)
     */
    Q_INVOKABLE void whatProvides(Provides type, const QString &search, Filters filters = FilterNone);

    /**
     * Cancels the transaction
     */
    Q_INVOKABLE void cancel();

    /**
     * Returns the package name from the \p packageID
     */
    static QString packageName(const QString &packageID);

    /**
     * Returns the package version from the \p packageID
     */
    static QString packageVersion(const QString &packageID);

    /**
     * Returns the package arch from the \p packageID
     */
    static QString packageArch(const QString &packageID);

    /**
     * Returns the package data from the \p packageID
     */
    static QString packageData(const QString &packageID);

    /**
     * Returns the package icon from the \p packageID
     */
    static QString packageIcon(const QString &packageID);

Q_SIGNALS:
    /**
     * The transaction has changed one of it's properties
     */
    void changed();

    /**
     * \brief Sends a category
     *
     * \li \p parentId is the id of the parent category. A blank parent means a root category
     * \li \p categoryId is the id of the category
     * \li \p name is the category's name. This name is localized.
     * \li \p summary is the category's summary. It is localized.
     * \li \p icon is the icon identifier eg. server-cfg. If unknown, it is set to icon-missing.
     *
     * \sa getCategories()
     */
    void category(const QString &parentId, const QString &categoryId, const QString &name, const QString &summary, const QString &icon);

    /**
     * Emitted when a distribution upgrade is available
     * \sa getDistroUpgrades()
     */
    void distroUpgrade(PackageKit::Transaction::DistroUpgrade type, const QString &name, const QString &description);

    /**
     * Emitted when an error occurs
     */
    void errorCode(PackageKit::Transaction::Error error, const QString &details);

    /**
     * Emitted when an EULA agreement prevents the transaction from running
     * \li \c eulaId is the EULA identifier
     * \li \c package is the package for which an EULA is required
     * \li \c vendorName is the vendor name
     * \li \c licenseAgreement is the EULA text
     *
     * \note You will need to relaunch the transaction after accepting the EULA
     * \sa acceptEula()
     */
    void eulaRequired(const QString &eulaID, const QString &packageID, const QString &vendor, const QString &licenseAgreement);

    /**
     * Emitted when a different media is required in order to fetch packages
     * which prevents the transaction from running
     * \note You will need to relaunch the transaction after changing the media
     * \sa Transaction::MediaType
     */
    void mediaChangeRequired(PackageKit::Transaction::MediaType type, const QString &id, const QString &text);

    /**
     * Sends the \p item current progress \p percentage
     * Currently only a package id is emitted
     */
    void itemProgress(const QString &itemID, PackageKit::Transaction::Status status, uint percentage);

    /**
     * Sends the \p filenames contained in package \p package
     * \sa getFiles()
     */
    void files(const QString &packageID, const QStringList &filenames);

    /**
     * Emitted when the transaction finishes
     *
     * \p status describes the exit status, \p runtime is the number of seconds it took to complete the transaction
     */
    void finished(PackageKit::Transaction::Exit status, uint runtime);

    /**
     * Conveys a message sent from the backend
     *
     * \p type is the type of the \p message
     */
    void message(PackageKit::Transaction::Message type, const QString &message);

    /**
     * Emitted when the transaction sends a new package
     */
    void package(PackageKit::Transaction::Info info, const QString &packageID, const QString &summary);

    /**
     * Emitted when the transaction sends details of a package
     */
    void details(const QString &packageID,
                 const QString &license,
                 PackageKit::Transaction::Group group,
                 const QString &detail,
                 const QString &url,
                 qulonglong size);

    /**
     * Emitted when the transaction sends details of an update
     */
    void updateDetail(const QString &packageID,
                      const QStringList &updates,
                      const QStringList &obsoletes,
                      const QStringList &vendorUrls,
                      const QStringList &bugzillaUrls,
                      const QStringList &cveUrls,
                      PackageKit::Transaction::Restart restart,
                      const QString &updateText,
                      const QString &changelog,
                      PackageKit::Transaction::UpdateState state,
                      const QDateTime &issued,
                      const QDateTime &updated);

    /**
      * Sends some additional details about a software repository
      * \sa getRepoList()
      */
    void repoDetail(const QString &repoId, const QString &description, bool enabled);

    /**
     * Emitted when the user has to validate a repository's signature
     * \sa installSignature()
     */
    void repoSignatureRequired(const QString &packageID,
                               const QString &repoName,
                               const QString &keyUrl,
                               const QString &keyUserid,
                               const QString &keyId,
                               const QString &keyFingerprint,
                               const QString &keyTimestamp,
                               PackageKit::Transaction::SigType type);

    /**
     * Indicates that a restart is required
     * \p package is the package who triggered the restart signal
     */
    void requireRestart(PackageKit::Transaction::Restart type, const QString &packageID);

    /**
     * Sends an old transaction
     * \sa getOldTransactions()
     */
    void transaction(PackageKit::Transaction *transaction);

protected:
    static Transaction::InternalError parseError(const QString &errorName);

    TransactionPrivate * const d_ptr;

private:
    friend class Daemon;
    bool init(const QDBusObjectPath &tid = QDBusObjectPath());
    Transaction(const QDBusObjectPath &tid,
                const QString &timespec,
                bool succeeded,
                Role role,
                uint duration,
                const QString &data,
                uint uid,
                const QString &cmdline,
                QObject *parent);
    Q_DECLARE_PRIVATE(Transaction);
    Q_DISABLE_COPY(Transaction)
    Q_PRIVATE_SLOT(d_ptr, void Details(const QString &pid, const QString &license, uint group, const QString &detail, const QString &url, qulonglong size));
    Q_PRIVATE_SLOT(d_ptr, void distroUpgrade(uint type, const QString &name, const QString &description));
    Q_PRIVATE_SLOT(d_ptr, void errorCode(uint error, const QString &details));
    Q_PRIVATE_SLOT(d_ptr, void mediaChangeRequired(uint mediaType, const QString &mediaId, const QString &mediaText));
    Q_PRIVATE_SLOT(d_ptr, void files(const QString &pid, const QStringList &filenames));
    Q_PRIVATE_SLOT(d_ptr, void finished(uint exitCode, uint runtime));
    Q_PRIVATE_SLOT(d_ptr, void message(uint type, const QString &message));
    Q_PRIVATE_SLOT(d_ptr, void Package(uint info, const QString &pid, const QString &summary));
    Q_PRIVATE_SLOT(d_ptr, void ItemProgress(const QString &itemID, uint status, uint percentage));
    Q_PRIVATE_SLOT(d_ptr, void RepoSignatureRequired(const QString &pid, const QString &repoName, const QString &keyUrl, const QString &keyUserid, const QString &keyId, const QString &keyFingerprint, const QString &keyTimestamp, uint type));
    Q_PRIVATE_SLOT(d_ptr, void requireRestart(uint type, const QString &pid));
    Q_PRIVATE_SLOT(d_ptr, void transaction(const QDBusObjectPath &oldTid, const QString &timespec, bool succeeded, uint role, uint duration, const QString &data, uint uid, const QString &cmdline));
    Q_PRIVATE_SLOT(d_ptr, void UpdateDetail(const QString &package_id, const QStringList &updates, const QStringList &obsoletes, const QStringList &vendor_urls, const QStringList &bugzilla_urls, const QStringList &cve_urls, uint restart, const QString &update_text, const QString &changelog, uint state, const QString &issued, const QString &updated));
    Q_PRIVATE_SLOT(d_ptr, void destroy());
    Q_PRIVATE_SLOT(d_ptr, void daemonQuit());
};
Q_DECLARE_OPERATORS_FOR_FLAGS(Transaction::Filters)
Q_DECLARE_OPERATORS_FOR_FLAGS(Transaction::TransactionFlags)

} // End namespace PackageKit

Q_DECLARE_METATYPE(PackageKit::Transaction::Info)

#endif
