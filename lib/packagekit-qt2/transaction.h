/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2010-2011 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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
#include <QtDBus/QDBusObjectPath>

#include "package.h"
#include "packagedetails.h"
#include "packageupdatedetails.h"

namespace PackageKit {

class Signature;
class Eula;
/**
* \class Transaction transaction.h Transaction
* \author Adrien Bustany \e <madcat@mymadcat.com>
* \author Daniel Nicoletti \e <dantti85-pk@yahoo.com.br>
*
* \brief A transaction represents an occurring action in PackageKit
*
* A Transaction is created whenever you do an asynchronous action (for example a Search, Install...).
* This class allows you to monitor and control the flow of the action.
*
* The Transaction will be automatically deleted as soon as it emits the finished() signal.
*
* You cannot create a Transaction directly, use the functions in the Daemon class instead.
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
    Q_ENUMS(Filter)
    Q_ENUMS(Message)
    Q_ENUMS(Status)
    Q_ENUMS(MediaType)
    Q_ENUMS(Provides)
    Q_ENUMS(DistroUpgrade)
    Q_ENUMS(TransactionFlag)
public:
    /**
     * Describes an error at the daemon level (for example, PackageKit crashes or is unreachable)
     *
     * \sa Transaction::error
     */
    typedef enum {
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
    } InternalError;

    /**
     * Describes the role of the transaction
     */
    enum Role {
        RoleUnknown            = 1 << 0,
        RoleCancel             = 1 << 1,
        RoleGetDepends         = 1 << 2,
        RoleGetDetails         = 1 << 3,
        RoleGetFiles           = 1 << 4,
        RoleGetPackages        = 1 << 5,
        RoleGetRepoList        = 1 << 6,
        RoleGetRequires        = 1 << 7,
        RoleGetUpdateDetail    = 1 << 8,
        RoleGetUpdates         = 1 << 9,
        RoleInstallFiles       = 1 << 10,
        RoleInstallPackages    = 1 << 11,
        RoleInstallSignature   = 1 << 12,
        RoleRefreshCache       = 1 << 13,
        RoleRemovePackages     = 1 << 14,
        RoleRepoEnable         = 1 << 15,
        RoleRepoSetData        = 1 << 16,
        RoleResolve            = 1 << 17,
        RoleSearchDetails      = 1 << 18,
        RoleSearchFile         = 1 << 19,
        RoleSearchGroup        = 1 << 20,
        RoleSearchName         = 1 << 21,
        RoleUpdatePackages     = 1 << 22,
        RoleUpdateSystem       = 1 << 23,
        RoleWhatProvides       = 1 << 24,
        RoleAcceptEula         = 1 << 25,
        RoleDownloadPackages   = 1 << 26,
        RoleGetDistroUpgrades  = 1 << 27,
        RoleGetCategories      = 1 << 28,
        RoleGetOldTransactions = 1 << 29,
        RoleUpgradeSystem      = 1 << 30, // Since 0.6.11
        RoleRepairSystem       = 1 << 31 // Since 0.7.2
    };
    Q_DECLARE_FLAGS(Roles, Role)

    /**
     * Describes the different types of error
     */
    typedef enum {
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
    } Error;

    /**
     * Describes how the transaction finished
     * \sa Transaction::finished()
     */
    typedef enum {
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
    } Exit;

    /**
     * Describes the different package filters
     */
    typedef enum {
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
    } Filter;
    Q_DECLARE_FLAGS(Filters, Filter)

    /**
     * Describes a message's type
     */
    typedef enum {
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
    } Message;

    /**
     * Describes the current state of the transaction
     */
    typedef enum {
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
    } Status;

    /**
     * Describes what kind of media is required
     */
    typedef enum {
        MediaTypeUnknown,
        MediaTypeCd,
        MediaTypeDvd,
        MediaTypeDisc
    } MediaType;

    /**
     * Enum used to describe a "provides" request
     * \sa whatProvides
     */
    typedef enum {
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
    } Provides;

    /**
     * Describes an distro upgrade state
     */
    typedef enum {
        DistroUpgradeUnknown,
        DistroUpgradeStable,
        DistroUpgradeUnstable
    } DistroUpgrade;

    /**
     * Describes the type of distribution upgrade to perform
     * \sa upgradeSystem()
     */
    typedef enum {
        UpgradeKindUnknown,
        UpgradeKindMinimal,
        UpgradeKindDefault,
        UpgradeKindComplete
    } UpgradeKind;

    /**
     * Describes the type of distribution upgrade to perform
     * \sa upgradeSystem()
     */
    typedef enum {
        TransactionFlagNone,        // Since: 0.8.1
        TransactionFlagOnlyTrusted, // Since: 0.8.1
        TransactionFlagSimulate,    // Since: 0.8.1
        TransactionFlagOnlyDownload // Since: 0.8.1
    } TransactionFlag;
    Q_DECLARE_FLAGS(TransactionFlags, TransactionFlag)

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
    Package lastPackage() const;

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
    Q_PROPERTY(qulonglong DownloadSizeRemaining READ downloadSizeRemaining)
    qulonglong downloadSizeRemaining() const;

    /**
     * Returns information describing the transaction
     * like InstallPackages, SearchName or GetUpdates
     * \return the current role of the transaction
     */
    Transaction::Role role() const;

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
    void setHints(const QStringList &hints);

    /**
     * Convenience function to set this transaction \p hints
     * \sa getDetails(const QStringList &hints)
     */
    void setHints(const QString &hints);

    /**
     * Returns the current state of the transaction
     * \return a Transaction::Status value describing the status of the transaction
     */
    Status status() const;

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
    void acceptEula(const QString &eulaId);

    /**
     * Download the given \p packages to a temp dir, if \p storeInCache is true
     * the download will be stored in the package manager cache
     */
    void downloadPackages(const PackageList &packages, bool storeInCache = false);

    /**
     * This is a convenience function to download this \p package
     * \sa downloadPackages(const PackageList &packages, bool storeInCache = false)
     */
    void downloadPackage(const Package &package, bool storeInCache = false);

    /**
     * Returns the collection categories
     *
     * \sa category
     */
    void getCategories();

    /**
     * \brief Gets the list of dependencies for the given \p packages
     *
     * You can use the \p filters to limit the results to certain packages.
     * The \p recursive flag indicates if the package manager should also
     * fetch the dependencies's dependencies.
     *
     * \note This method emits \sa package()
     */
    void getDepends(const PackageList &packages, Filters filters, bool recursive = false);

    /**
     * Convenience function to get the dependencies of this \p package
     * \sa getDetails(const PackageList &packages, Filters filters, bool recursive = false)
     */
    void getDepends(const Package &package, Filters filters , bool recursive = false);

    /**
     * Gets more details about the given \p packages
     *
     * \sa Transaction::details
     * \note This method emits \sa package()
     * with details set
     */
    void getDetails(const PackageList &packages);

    /**
     * Convenience function to get the details about this \p package
     * \sa getDetails(const PackageList &packages)
     */
    void getDetails(const Package &package);

    /**
     * Gets the files contained in the given \p packages
     *
     * \note This method emits \sa files()
     */
    void getFiles(const PackageList &packages);

    /**
     * Convenience function to get the files contained in this \p package
     * \sa getRequires(const PackageList &packages)
     */
    void getFiles(const Package &packages);

    /**
     * \brief Gets the last \p number finished transactions
     *
     * \note You must delete these transactions yourself
     * \note This method emits \sa transaction()
     */
    void getOldTransactions(uint number);

    /**
     * Gets all the packages matching the given \p filters
     *
     * \note This method emits \sa package()
     */
    void getPackages(Filters filters = FilterNone);

    /**
     * Gets the list of software repositories matching the given \p filters
     *
     * \note This method emits \sa repository()
     */
    void getRepoList(Filters filter = FilterNone);

    /**
     * \brief Searches for the packages requiring the given \p packages
     *
     * The search can be limited using the \p filters parameter.
     * The \p recursive flag is used to tell if the package manager should
     * also search for the package requiring the resulting packages.
     *
     * \note This method emits \sa package()
     */
    void getRequires(const PackageList &packages, Filters filters, bool recursive = false);

    /**
     * Convenience function to get packages requiring this package
     * \sa getRequires(const PackageList &packages, Filters filters, bool recursive = false)
     */
    void getRequires(const Package &package, Filters filters, bool recursive = false);

    /**
     * Retrieves more details about the update for the given \p packages
     *
     * \note This method emits \sa updateDetail()
     */
    void getUpdatesDetails(const PackageList &packages);

    /**
     * Convenience function to get update details
     * \sa getUpdateDetail(const PackageList &packages)
     */
    void getUpdateDetail(const Package &package);

    /**
     * \p Gets the available updates
     *
     * The \p filters parameters can be used to restrict the updates returned
     *
     * \note This method emits \sa package()
     */
    void getUpdates(Filters filters = FilterNone);

    /**
     * Retrieves the available distribution upgrades
     *
     * \note This method emits \sa distroUpgrade()
     */
    void getDistroUpgrades();

    /**
     * \brief Installs the local packages \p files
     *
     * \p onlyTrusted indicate if the packages are signed by a trusted authority
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void installFiles(const QStringList &files, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to install a file
     * \sa installFiles(const QStringList &files, TransactionFlags flags)
     */
    void installFile(const QString &file, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Install the given \p packages
     *
     * \p only_trusted indicates if we should allow installation of untrusted packages (requires a different authorization)
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void installPackages(const PackageList &packages, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to install a package
     * \sa installPackages(const PackageList &packages, TransactionFlags flags)
     */
    void installPackage(const Package &package, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * \brief Installs a signature
     *
     * \p type, \p keyId and \p package generally come from the Transaction::repoSignatureRequired
     */
    void installSignature(const Signature &signature);

    /**
     * Refreshes the package manager's cache
     *
     * \note This method emits \sa changed()
     */
    void refreshCache(bool force);

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
    void removePackages(const PackageList  &packages, bool allowDeps = false, bool autoRemove = false, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to remove a package
     * \sa removePackages(const PackageList  &packages, bool allowDeps = false, bool autoRemove = false, TransactionFlags flags)
     */
    void removePackage(const Package &package, bool allowDeps = false, bool autoRemove = false, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Repairs a broken system
     */
    void repairSystem(TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Activates or disables a repository
     */
    void repoEnable(const QString &repoId, bool enable = true);

    /**
     * Sets a repository's parameter
     */
    void repoSetData(const QString &repoId, const QString &parameter, const QString &value);

    /**
     * \brief Tries to create a Package object from the package's name
     *
     * The \p filters can be used to restrict the search
     *
     * \note This method emits \sa package()
     */
    void resolve(const QStringList &packageNames, Filters filters = FilterNone);

    /**
     * Convenience function to remove a package name
     * \sa resolve(const QStringList &packageNames, Filters filters = FilterNone)
     */
    void resolve(const QString &packageName, Filters filters = FilterNone);

    /**
     * \brief Search in the packages files
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchFiles(const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search for a file
     * \sa searchFiles(const QStringList &search, Filters filters = FilterNone)
     */
    void searchFiles(const QString &search, Filters filters = FilterNone);

    /**
     * \brief Search in the packages details
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchDetails(const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search by details
     * \sa searchDetails(const QStringList &search, Filters filters = FilterNone)
     */
    void searchDetails(const QString &search, Filters filters = FilterNone);

    /**
     * \brief Lists all the packages in the given \p group
     *
     * \p groups is the name of the group that you want, when searching for
     * categories prefix it with '@'
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchGroups(const QStringList &groups, Filters filters = FilterNone);

    /**
     * Convenience function to search by group string
     * \sa searchGroups(const QStringList &groups, Filters filters = FilterNone)
     */
    void searchGroup(const QString &group, Filters filters = FilterNone);

    /**
     * \brief Lists all the packages in the given \p group
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchGroups(PackageDetails::Groups group, Filters filters = FilterNone);

    /**
     * \brief Search in the packages names
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchNames(const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search by names
     * \sa searchNames(const QStringList &search, Filters filters)
     */
    void searchNames(const QString &search, Filters filters = FilterNone);

    /**
     * Update the given \p packages
     *
     * \p onlyTrusted indicates if this transaction is only allowed to install trusted packages
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void updatePackages(const PackageList &packages, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Convenience function to update a package
     * \sa updatePackages(const PackageList &packages, TransactionFlags flags)
     */
    void updatePackage(const Package &package, TransactionFlags flags = TransactionFlagOnlyTrusted);

    /**
     * Updates the whole system
     *
     * \p onlyTrusted indicates if this transaction is only allowed to install trusted packages
     *
     * \note This method typically emits
     * \li package()
     * \li changed()
     */
    void updateSystem(bool onlyTrusted = true);

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
    void upgradeSystem(const QString &distroId, UpgradeKind kind);

    /**
     * Searchs for a package providing a file/a mimetype
     *
     * \note This method emits \sa package()
     */
    void whatProvides(Provides type, const QStringList &search, Filters filters = FilterNone);

    /**
     * Convenience function to search for what provides
     * \sa whatProvides(Provides type, const QStringList &search, Filters filters = FilterNone)
     */
    void whatProvides(Provides type, const QString &search, Filters filters = FilterNone);

public Q_SLOTS:
    /**
     * Cancels the transaction
     */
    void cancel();

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
    void eulaRequired(const PackageKit::Eula &eula);

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
    void ItemProgress(const QString &id, uint percentage);

    /**
     * Sends the \p filenames contained in package \p package
     * \sa getFiles()
     */
    void files(const PackageKit::Package &package, const QStringList &filenames);

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
    void package(const PackageKit::Package &package);

    /**
     * Emitted when the transaction sends a new package
     */
    void packageDetails(const PackageKit::PackageDetails &package);

    /**
     * Emitted when the transaction sends a new package
     */
    void packageUpdateDetails(const PackageKit::PackageUpdateDetails &package);

    /**
      * Sends some additional details about a software repository
      * \sa getRepoList()
      */
    void repoDetail(const QString& repoId, const QString& description, bool enabled);

    /**
     * Emitted when the user has to validate a repository's signature
     * \sa installSignature()
     */
    void repoSignatureRequired(const PackageKit::Signature &info);

    /**
     * Indicates that a restart is required
     * \p package is the package who triggered the restart signal
     */
    void requireRestart(PackageKit::PackageUpdateDetails::Restart type, const PackageKit::Package &package);

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
    void init(const QDBusObjectPath &tid = QDBusObjectPath());
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
    Q_PRIVATE_SLOT(d_ptr, void details(const QString &pid, const QString &license, uint group, const QString &detail, const QString &url, qulonglong size));
    Q_PRIVATE_SLOT(d_ptr, void distroUpgrade(uint type, const QString &name, const QString &description));
    Q_PRIVATE_SLOT(d_ptr, void errorCode(uint error, const QString &details));
    Q_PRIVATE_SLOT(d_ptr, void eulaRequired(const QString &eulaId, const QString &pid, const QString &vendor, const QString &licenseAgreement));
    Q_PRIVATE_SLOT(d_ptr, void mediaChangeRequired(uint mediaType, const QString &mediaId, const QString &mediaText));
    Q_PRIVATE_SLOT(d_ptr, void files(const QString &pid, const QStringList &filenames));
    Q_PRIVATE_SLOT(d_ptr, void finished(uint exitCode, uint runtime));
    Q_PRIVATE_SLOT(d_ptr, void message(uint type, const QString &message));
    Q_PRIVATE_SLOT(d_ptr, void package(uint info, const QString &pid, const QString &summary));
    Q_PRIVATE_SLOT(d_ptr, void repoSignatureRequired(const QString &pid, const QString &repoName, const QString &keyUrl, const QString &keyUserid, const QString &keyId, const QString &keyFingerprint, const QString &keyTimestamp, uint type));
    Q_PRIVATE_SLOT(d_ptr, void requireRestart(uint type, const QString &pid));
    Q_PRIVATE_SLOT(d_ptr, void transaction(const QDBusObjectPath &oldTid, const QString &timespec, bool succeeded, uint role, uint duration, const QString &data, uint uid, const QString &cmdline));
    Q_PRIVATE_SLOT(d_ptr, void updateDetail(const QString &package_id, const QStringList &updates, const QStringList &obsoletes, const QStringList &vendor_urls, const QStringList &bugzilla_urls, const QStringList &cve_urls, uint restart, const QString &update_text, const QString &changelog, uint state, const QString &issued, const QString &updated));
    Q_PRIVATE_SLOT(d_ptr, void destroy());
    Q_PRIVATE_SLOT(d_ptr, void daemonQuit());
};
Q_DECLARE_OPERATORS_FOR_FLAGS(Transaction::Filters)

} // End namespace PackageKit

#endif
