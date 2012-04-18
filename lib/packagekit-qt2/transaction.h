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

#include "bitfield.h"
#include "package.h"

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
public:
    /**
     * Describes an error at the daemon level (for example, PackageKit crashes or is unreachable)
     *
     * \sa Transaction::error
     */
    typedef enum {
        NoInternalError = 0,
        UnkownInternalError,
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
    typedef enum {
        UnknownRole,
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
        RoleRollback,
        RoleSearchDetails,
        RoleSearchFile,
        RoleSearchGroup,
        RoleSearchName,
        RoleUpdatePackages,
        RoleUpdateSystem,
        RoleWhatProvides,
        RoleAcceptEula,
        RoleDownloadPackages,
        RoleGetDistroUpgrades,
        RoleGetCategories,
        RoleGetOldTransactions,
        RoleSimulateInstallFiles,
        RoleSimulateInstallPackages,
        RoleSimulateRemovePackages,
        RoleSimulateUpdatePackages,
        RoleUpgradeSystem // Since 0.6.11
    } Role;
    typedef Bitfield Roles;

    /**
     * Describes the different types of error
     */
    typedef enum {
        UnknownError,
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
        ErrorUnfinishedTransaction
    } Error;

    /**
     * Describes how the transaction finished
     * \sa Transaction::finished()
     */
    typedef enum {
        UnknownExit,
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
        UnknownFilter        = 0x0000001,
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
        UnknownMessage,
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
        UnknownStatus,
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
        StatusRollback,
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
        UnknownMediaType,
        MediaTypeCd,
        MediaTypeDvd,
        MediaTypeDisc
    } MediaType;

    /**
     * Enum used to describe a "provides" request
     * \sa whatProvides
     */
    typedef enum {
        UnknownProvides,
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
        UnknownDistroUpgrade,
        DistroUpgradeStable,
        DistroUpgradeUnstable
    } DistroUpgrade;

    /**
     * Describes the type of distribution upgrade to perform
     * \sa upgradeSystem()
     */
    typedef enum {
        UnknownUpgradeKind,
        UpgradeKindMinimal,
        upgradeKindDefault,
        upgradeKindComplete
    } UpgradeKind;

    /**
     * Create a transaction object with a new transaction id
     *
     * The transaction object \b cannot be reused
     * (i.e. simulateInstallPackages then installPackages)
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
     * (i.e. simulateInstallPackages then installPackages)
     *
     * \warning after creating the transaction object be sure
     * to verify if it doesn't have any error()
     */
    Transaction(const QString &tid, QObject *parent = 0);

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
    QString tid() const;

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
     * The percentage complete of the individual task, for example, downloading.
     * \return percentage, or 101 if not known.
     */
    uint subpercentage() const;

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
    void downloadPackages(const QList<Package> &packages, bool storeInCache = false);

    /**
     * This is a convenience function to download this \p package
     * \sa downloadPackages(const QList<Package> &packages, bool storeInCache = false)
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
    void getDepends(const QList<Package> &packages, Filters filters, bool recursive = false);

    /**
     * Convenience function to get the dependencies of this \p package
     * \sa getDetails(const QList<Package> &packages, Filters filters, bool recursive = false)
     */
    void getDepends(const Package &package, Filters filters , bool recursive = false);

    /**
     * Gets more details about the given \p packages
     *
     * \sa Transaction::details
     * \note This method emits \sa package()
     * with details set
     */
    void getDetails(const QList<Package> &packages);

    /**
     * Convenience function to get the details about this \p package
     * \sa getDetails(const QList<Package> &packages)
     */
    void getDetails(const Package &package);

    /**
     * Gets the files contained in the given \p packages
     *
     * \note This method emits \sa files()
     */
    void getFiles(const QList<Package> &packages);

    /**
     * Convenience function to get the files contained in this \p package
     * \sa getRequires(const QList<Package> &packages)
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
    void getRequires(const QList<Package> &packages, Filters filters, bool recursive = false);

    /**
     * Convenience function to get packages requiring this package
     * \sa getRequires(const QList<Package> &packages, Filters filters, bool recursive = false)
     */
    void getRequires(const Package &package, Filters filters, bool recursive = false);

    /**
     * Retrieves more details about the update for the given \p packages
     *
     * \note This method emits \sa updateDetail()
     */
    void getUpdatesDetails(const QList<Package> &packages);

    /**
     * Convenience function to get update details
     * \sa getUpdateDetail(const QList<Package> &packages)
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
    void installFiles(const QStringList &files, bool onlyTrusted = true);

    /**
     * Convenience function to install a file
     * \sa installFiles(const QStringList &files, bool onlyTrusted = true)
     */
    void installFile(const QString &file, bool onlyTrusted = true);

    /**
     * Install the given \p packages
     *
     * \p only_trusted indicates if we should allow installation of untrusted packages (requires a different authorization)
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void installPackages(const QList<Package> &packages, bool onlyTrusted = true);

    /**
     * Convenience function to install a package
     * \sa installPackages(const QList<Package> &packages, bool onlyTrusted = true)
     */
    void installPackage(const Package &package, bool onlyTrusted = true);

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
    void removePackages(const QList<Package>  &packages, bool allowDeps = false, bool autoRemove = false);

    /**
     * Convenience function to remove a package
     * \sa removePackages(const QList<Package>  &packages, bool allowDeps = false, bool autoRemove = false)
     */
    void removePackage(const Package &package, bool allowDeps = false, bool autoRemove = false);

    /**
     * Repairs a broken system
     * \sa simulateRepairSystem();
     */
    void repairSystem(bool onlyTrusted = true);

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
    void searchGroups(Package::Groups group, Filters filters = FilterNone);

    /**
     * Convenience function to search by group
     * \sa searchGroups(Package::Groups group, Filters filters = FilterNone)
     */
    void searchGroup(Package::Group group, Filters filters = FilterNone);

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
     * \sa searchNames(const QStringList &search, Filters filters = FilterNone)
     */
    void searchNames(const QString &search, Filters filters = FilterNone);

    /**
     * \brief Simulates an installation of \p files.
     *
     * You should call this method before installing \p files
     * \note: This method typically emits package() and errorCode()
     * with INSTALLING, REMOVING, UPDATING, DOWNGRADING,
     * REINSTALLING, OBSOLETING or UNTRUSTED status.
     * The later is used to present the user untrusted packages
     * that are about to be installed.
     */
    void simulateInstallFiles(const QStringList &files);

    /**
     * Convenience function to simulate the install of a file
     * \sa simulateInstallFiles(const QStringList &files)
     */
    void simulateInstallFile(const QString &file);

    /**
     * \brief Simulates an installation of \p packages.
     *
     * You should call this method before installing \p packages
     * \note: This method typically emits package() and errorCode()
     * with INSTALLING, REMOVING, UPDATING, DOWNGRADING,
     * REINSTALLING, OBSOLETING or UNTRUSTED status.
     * The later is used to present the user untrusted packages
     * that are about to be installed.
     */
    void simulateInstallPackages(const QList<Package> &packages);

    /**
     * Convenience function to simulate the install of a package
     * \sa simulateInstallPackages(const QList<Package> &packages)
     */
    void simulateInstallPackage(const Package &package);

    /**
     * \brief Simulates a removal of \p packages.
     *
     * You should call this method before removing \p packages
     * \note: This method typically emits package() and errorCode()
     * with INSTALLING, REMOVING, UPDATING, DOWNGRADING,
     * REINSTALLING, OBSOLETING or UNTRUSTED status.
     * The later is used to present the user untrusted packages
     * that are about to be installed.
     */
    void simulateRemovePackages(const QList<Package> &packages, bool autoRemove = false);

    /**
     * Convenience function to simulate the removal of a package
     * \sa simulateRemovePackages(const QList<Package> &packages, bool autoRemove = false)
     */
    void simulateRemovePackage(const Package &package, bool autoRemove = false);

    /**
     * \brief Simulates an update of \p packages.
     *
     * You should call this method before updating \p packages
     * \note: This method typically emits package() and errorCode()
     * with INSTALLING, REMOVING, UPDATING, DOWNGRADING,
     * REINSTALLING, OBSOLETING or UNTRUSTED status.
     * The later is used to present the user untrusted packages
     * that are about to be installed.
     */
    void simulateUpdatePackages(const QList<Package> &packages);

    /**
     * Convenience function to simulate the update of a package
     * \sa simulateUpdatePackages(const QList<Package> &packages)
     */
    void simulateUpdatePackage(const Package &package);

    /**
     * Tries to fix a broken system
     * \note this function will emit packages that describe the actions
     * the backend will take
     * \sa repairSystem(bool);
     */
    void simulateRepairSystem();

    /**
     * Update the given \p packages
     *
     * \p onlyTrusted indicates if this transaction is only allowed to install trusted packages
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void updatePackages(const QList<Package> &packages, bool onlyTrusted = true);

    /**
     * Convenience function to update a package
     * \sa updatePackages(const QList<Package> &packages, bool onlyTrusted = true)
     */
    void updatePackage(const Package &package, bool onlyTrusted = true);

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
    void requireRestart(PackageKit::Package::Restart type, const PackageKit::Package &package);

    /**
     * Sends an old transaction
     * \sa getOldTransactions()
     */
    void transaction(PackageKit::Transaction *transaction);

protected:
    TransactionPrivate * const d_ptr;

private:
    void init(const QString &tid = QString());
    Transaction(const QString &tid,
                const QString &timespec,
                bool succeeded,
                const QString &role,
                uint duration,
                const QString &data,
                uint uid,
                const QString &cmdline,
                QObject *parent);
    Q_DECLARE_PRIVATE(Transaction);
    Q_DISABLE_COPY(Transaction)
    Q_PRIVATE_SLOT(d_ptr, void details(const QString& pid, const QString& license, const QString& group, const QString& detail, const QString& url, qulonglong size));
    Q_PRIVATE_SLOT(d_ptr, void distroUpgrade(const QString& type, const QString& name, const QString& description));
    Q_PRIVATE_SLOT(d_ptr, void errorCode(const QString& error, const QString& details));
    Q_PRIVATE_SLOT(d_ptr, void eulaRequired(const QString& eulaId, const QString& pid, const QString& vendor, const QString& licenseAgreement));
    Q_PRIVATE_SLOT(d_ptr, void mediaChangeRequired(const QString& mediaType, const QString& mediaId, const QString& mediaText));
    Q_PRIVATE_SLOT(d_ptr, void files(const QString& pid, const QString& filenames));
    Q_PRIVATE_SLOT(d_ptr, void finished(const QString& exitCode, uint runtime));
    Q_PRIVATE_SLOT(d_ptr, void message(const QString& type, const QString& message));
    Q_PRIVATE_SLOT(d_ptr, void package(const QString& info, const QString& pid, const QString& summary));
    Q_PRIVATE_SLOT(d_ptr, void repoSignatureRequired(const QString& pid, const QString& repoName, const QString& keyUrl, const QString &keyUserid, const QString& keyId, const QString& keyFingerprint, const QString& keyTimestamp, const QString& type));
    Q_PRIVATE_SLOT(d_ptr, void requireRestart(const QString& type, const QString& pid));
    Q_PRIVATE_SLOT(d_ptr, void transaction(const QString& oldTid, const QString& timespec, bool succeeded, const QString& role, uint duration, const QString& data, uint uid, const QString& cmdline));
    Q_PRIVATE_SLOT(d_ptr, void updateDetail(const QString& pid, const QString& updates, const QString& obsoletes, const QString& vendorUrl, const QString& bugzillaUrl, const QString& cveUrl, const QString& restart, const QString& updateText, const QString& changelog, const QString& state, const QString& issued, const QString& updated));
    Q_PRIVATE_SLOT(d_ptr, void destroy());
    Q_PRIVATE_SLOT(d_ptr, void daemonQuit());
};
Q_DECLARE_OPERATORS_FOR_FLAGS(Transaction::Filters)

} // End namespace PackageKit

#endif
