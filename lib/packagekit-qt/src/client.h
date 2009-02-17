/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
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

#ifndef CLIENT_H
#define CLIENT_H

#include <QtCore>

namespace PackageKit {

class ClientPrivate;
class Package;
class Transaction;

/**
 * \class Client client.h Client
 * \author Adrien Bustany <madcat@mymadcat.com>
 *
 * \brief Base class used to interact with the PackageKit daemon
 *
 * This class holds all the functions enabling the user to interact with the PackageKit daemon.
 * The user should always use this class to initiate transactions.
 *
 * All the function returning a pointer to a Transaction work in an asynchronous way. The returned
 * object can be used to alter the operation's execution, or monitor it's state. The Transaction
 * object will be automatically deleted after it emits the finished() signal.
 *
 * \note This class is a singleton, its constructor is private. Call Client::instance() to get
 * an instance of the Client object
 */
class Client : public QObject
{

	Q_OBJECT
	Q_ENUMS(Action)
	Q_ENUMS(Filter)
	Q_ENUMS(Group)
	Q_ENUMS(NetworkState)
	Q_ENUMS(SignatureType)
	Q_ENUMS(ErrorType)
	Q_ENUMS(RestartType)
	Q_ENUMS(UpgradeType)

public:
	/**
	 * \brief Returns an instance of the Client
	 *
	 * The Client class is a singleton, you can call this method several times,
	 * a single Client object will exist.
	 */
	static Client* instance();

	/**
	 * Destructor
	 */
	~Client();

	// Daemon functions

	/**
	 * Lists all the available actions
	 * \sa getActions
	 */
	typedef enum {
		ActionCancel,
		ActionGetDepends,
		ActionGetDetails,
		ActionGetFiles,
		ActionGetPackages,
		ActionGetRepoList,
		ActionGetRequires,
		ActionGetUpdateDetail,
		ActionGetUpdates,
		ActionInstallFiles,
		ActionInstallPackages,
		ActionInstallSignature,
		ActionRefreshCache,
		ActionRemovePackages,
		ActionRepoEnable,
		ActionRepoSetData,
		ActionResolve,
		ActionRollback,
		ActionSearchDetails,
		ActionSearchFile,
		ActionSearchGroup,
		ActionSearchName,
		ActionServicePack,
		ActionUpdatePackages,
		ActionUpdateSystem,
		ActionWhatProvides,
		ActionAcceptEula,
		ActionDownloadPackages,
		ActionGetDistroUpgrades,
		ActionGetCategories,
		UnkownAction = -1
	} Action;
	typedef QSet<Action> Actions;

	/**
	 * Returns all the actions supported by the current backend
	 */
	Actions getActions();

	/**
	 * Holds a backend's detail
	 * \li \c name is the name of the backend
	 * \li \c author is the name of the person who wrote the backend
	 */
	typedef struct {
		QString name;
		QString author;
	} BackendDetail;

	/**
	 * Gets the current backend's details
	 * \return a BackendDetail struct holding the backend's details. You have to free this structure.
	 */
	BackendDetail getBackendDetail();

	/**
	 * Describes the different filters
	 */
	typedef enum {
		NoFilter,
		FilterInstalled,
		FilterNotInstalled,
		FilterDevelopment,
		FilterNotDevelopment,
		FilterGui,
		FilterNotGui,
		FilterFree,
		FilterNotFree,
		FilterVisible,
		FilterNotVisible,
		FilterSupported,
		FilterNotSupported,
		FilterBasename,
		FilterNotBasename,
		FilterNewest,
		FilterNotNewest,
		FilterArch,
		FilterNotArch,
		FilterSource,
		FilterNotSource,
		FilterCollections,
		FilterNotCollections,
		UnknownFilter = -1
	} Filter;
	typedef QSet<Filter> Filters;

	/**
	 * Returns the filters supported by the current backend
	 */
	Filters getFilters();

	/**
	 * Describes the different groups
	 */
	typedef enum {
		Accessibility,
		Accessories,
		AdminTools,
		Communication,
		DesktopGnome,
		DesktopKde,
		DesktopOther,
		DesktopXfce,
		Education,
		Fonts,
		Games,
		Graphics,
		Internet,
		Legacy,
		Localization,
		Maps,
		Multimedia,
		Network,
		Office,
		Other,
		PowerManagement,
		Programming,
		Publishing,
		Repos,
		Security,
		Servers,
		System,
		Virtualization,
		Science,
		Documentation,
		Electronics,
		Collections,
		Vendor,
		Newest,
		UnknownGroup = -1
	} Group;
	typedef QSet<Group> Groups;

	/**
	 * Returns the groups supported by the current backend
	 */
	Groups getGroups();

	/**
	 * Returns a list containing the MIME types supported by the current backend
	 */
	QStringList getMimeTypes();

	/**
	 * Describes the current network state
	 */
	typedef enum {
    	Offline,
    	Online,
    	Mobile,
    	Wifi,
		Wired,
    	UnknownNetworkState = -1
	} NetworkState;

	/**
	 * Returns the current network state
	 */
	NetworkState getNetworkState();

	/**
	 * Returns the time (in seconds) since the specified \p action
	 */
	uint getTimeSinceAction(Action action);

	/**
	 * Returns the list of current transactions
	 */
	QList<Transaction*> getTransactions();

	/**
	 * Sets a global locale for all the transactions to be created
	 */
	void setLocale(const QString& locale);

	/**
	 * Sets a proxy to be used for all the network operations
	 */
	void setProxy(const QString& http_proxy, const QString& ftp_proxy);

	/**
	 * \brief Tells the daemon that the system state has changed, to make it reload its cache
	 *
	 * \p reason can be resume or posttrans
	 */
	void stateHasChanged(const QString& reason);

	/**
	 * Asks PackageKit to quit, for example to let a native package manager operate
	 */
	void suggestDaemonQuit();

	// Other enums
	/**
	 * Describes a signature type
	 */
	typedef enum {
		Gpg,
		UnknownSignatureType = -1
	} SignatureType;

	/**
	 * Describes a package signature
	 * \li \c package is a pointer to the signed package
	 * \li \c repoId is the id of the software repository containing the package
	 * \li \c keyUrl, \c keyId, \c keyFingerprint and \c keyTimestamp describe the key
	 * \li \c type is the signature type
	 */
	typedef struct {
		Package* package;
		QString repoId;
		QString keyUrl;
		QString keyUserid;
		QString keyId;
		QString keyFingerprint;
		QString keyTimestamp;
		SignatureType type;
	} SignatureInfo;

	/**
	 * Enum used to describe a "provides" request
	 * \sa whatProvides
	 */
	typedef enum {
		ProvidesAny,
		ProvidesModalias,
		ProvidesCodec,
		ProvidesMimetype,
		ProvidesFont,
		UnknownProvidesType = -1
	} ProvidesType;

	/**
	 * Lists the different types of error
	 */
	typedef enum {
		Oom,
		NoNetwork,
		NotSupported,
		InternalError,
		GpgFailure,
		PackageIdInvalid,
		PackageNotInstalled,
		PackageNotFound,
		PackageAlreadyInstalled,
		PackageDownloadFailed,
		GroupNotFound,
		GroupListInvalid,
		DepResolutionFailed,
		FilterInvalid,
		CreateThreadFailed,
		TransactionError,
		TransactionCancelled,
		NoCache,
		RepoNotFound,
		CannotRemoveSystemPackage,
		ProcessKill,
		FailedInitialization,
		FailedFinalise,
		FailedConfigParsing,
		CannotCancel,
		CannotGetLock,
		NoPackagesToUpdate,
		CannotWriteRepoConfig,
		LocalInstallFailed,
		BadGpgSignature,
		MissingGpgSignature,
		CannotInstallSourcePackage,
		RepoConfigurationError,
		NoLicenseAgreement,
		FileConflicts,
		PackageConflicts,
		RepoNotAvailable,
		InvalidPackageFile,
		PackageInstallBlocked,
		PackageCorrupt,
		AllPackagesAlreadyInstalled,
		FileNotFound,
		NoMoreMirrorsToTry,
		UnknownErrorType = -1
	} ErrorType;

	/**
	 * Describes a message's type
	 */
	typedef enum {
		Warning,
		Notice,
		Daemon,
		UnknownMessageType = -1
	} MessageType;

	/**
	 * Describes an EULA
	 * \li \c id is the EULA identifier
	 * \li \c package is the package for which an EULA is required
	 * \li \c vendorName is the vendor name
	 * \li \c licenseAgreement is the EULA text
	 */
	typedef struct {
		QString id;
		Package* package;
		QString vendorName;
		QString licenseAgreement;
	} EulaInfo;

	/**
	 * Describes a restart type
	 */
	typedef enum {
		RestartNone,
		RestartApplication,
		RestartSession,
		RestartSystem,
		UnknownRestartType = -1
	} RestartType;

	/**
	 * Describes an update's state
	 */
	typedef enum {
		UpgradeStable,
		UpgradeUnstable,
		UpgradeTesting,
		UnknownUpgradeType = -1
	} UpgradeType;

	/**
	 * Describes an error at the daemon level (for example, PackageKit crashes or is unreachable)
	 */
	typedef enum {
		DaemonUnreachable,
		UnkownDaemonError = -1
	} DaemonError;
	/**
	 * Returns the last daemon error that was caught
	 */
	DaemonError getLastError();

	/**
	 * Describes a software update
	 * \li \c package is the package which triggered the update
	 * \li \c updates are the packages to be updated
	 * \li \c obsoletes lists the packages which will be obsoleted by this update
	 * \li \c vendorUrl, \c bugzillaUrl and \c cveUrl are links to webpages describing the update
	 * \li \c restart indicates if a restart will be required after this update
	 * \li \c updateText describes the update
	 * \li \c changelog holds the changelog
	 * \li \c state is the category of the update, eg. stable or testing
	 * \li \c issued and \c updated indicate the dates at which the update was issued and updated
	 */
	typedef struct {
		Package* package;
		QList<Package*> updates;
		QList<Package*> obsoletes;
		QString vendorUrl;
		QString bugzillaUrl;
		QString cveUrl;
		RestartType restart;
		QString updateText;
		QString changelog;
		UpgradeType state;
		QDateTime issued;
		QDateTime updated;
	} UpdateInfo;

	// Transaction functions

	/**
	 * \brief Accepts an EULA
	 *
	 * The EULA is identified by the EulaInfo structure \p info
	 *
	 * \note You need to restart the transaction which triggered the EULA manually
	 *
	 * \sa Transaction::eulaRequired
	 */
	Transaction* acceptEula(EulaInfo info);

	/**
	 * Download the given \p packages to a temp dir
	 */
	Transaction* downloadPackages(const QList<Package*>& packages);

	/**
	 * This is a convenience function
	 */
	Transaction* downloadPackage(Package* package);

	/**
	 * Returns the collection categories
	 *
	 * \sa Transaction::category
	 */
	Transaction* getCategories();

	/**
	 * \brief Gets the list of dependencies for the given \p packages
	 *
	 * You can use the \p filters to limit the results to certain packages. The
	 * \p recursive flag indicates if the package manager should also fetch the
	 * dependencies's dependencies.
	 *
	 * \sa Transaction::package
	 *
	 */
	Transaction* getDepends(const QList<Package*>& packages, Filters filters = Filters() << NoFilter, bool recursive = true);
	Transaction* getDepends(Package* package, Filters filters = Filters() << NoFilter, bool recursive = true);
	Transaction* getDepends(const QList<Package*>& packages, Filter filter, bool recursive = true);
	Transaction* getDepends(Package* package, Filter filter, bool recursive = true);

	/**
	 * Gets more details about the given \p packages
	 *
	 * \sa Transaction::details
	 */
	Transaction* getDetails(const QList<Package*>& packages);
	Transaction* getDetails(Package* package);

	/**
	 * Gets the files contained in the given \p packages
	 *
	 * \sa Transaction::files
	 */
	Transaction* getFiles(const QList<Package*>& packages);
	Transaction* getFiles(Package* packages);

	/**
	 * \brief Gets the last \p number finished transactions
	 *
	 * \note You must delete these transactions yourself
	 */
	Transaction* getOldTransactions(uint number);

	/**
	 * Gets all the packages matching the given \p filters
	 *
	 * \sa Transaction::package
	 */
	Transaction* getPackages(Filters filters = Filters() << NoFilter);
	Transaction* getPackages(Filter filter);

	/**
	 * Gets the list of software repositories matching the given \p filters
	 */
	Transaction* getRepoList(Filters filter = Filters() << NoFilter);
	Transaction* getRepoList(Filter filter);

	/**
	 * \brief Searches for the packages requiring the given \p packages
	 *
	 * The search can be limited using the \p filters parameter. The recursive flag is used to tell
	 * if the package manager should also search for the package requiring the resulting packages.
	 */
	Transaction* getRequires(const QList<Package*>& packages, Filters filters = Filters() << NoFilter, bool recursive = true);
	Transaction* getRequires(Package* package, Filters filters = Filters() << NoFilter, bool recursive = true);
	Transaction* getRequires(const QList<Package*>& packages, Filter filter, bool recursive = true);
	Transaction* getRequires(Package* package, Filter filter, bool recursive = true);

	/**
	 * Retrieves more details about the update for the given \p packages
	 */
	Transaction* getUpdateDetail(const QList<Package*>& packages);
	Transaction* getUpdateDetail(Package* package);

	/**
	 * \p Gets the available updates
	 *
	 * The \p filters parameters can be used to restrict the updates returned
	 */
	Transaction* getUpdates(Filters filters = Filters() << NoFilter);
	Transaction* getUpdates(Filter filter);

	/**
	 * Retrieves the available distribution upgrades
	 */
	Transaction* getDistroUpgrades();

	/**
	 * \brief Installs the local packages \p files
	 *
	 * \trusted indicate if the packages are signed by a trusted authority
	 */
	Transaction* installFiles(const QStringList& files, bool trusted);
	Transaction* installFile(const QString& file, bool trusted);

	/**
	 * Install the given \p packages
	 */
	Transaction* installPackages(const QList<Package*>& packages);
	Transaction* installPackage(Package* p);

	/**
	 * \brief Installs a signature
	 *
	 * \p type, \p key_id and \p p generally come from the Transaction::repoSignatureRequired
	 */
	Transaction* installSignature(SignatureType type, const QString& key_id, Package* p);

	/**
	 * Refreshes the package manager's cache
	 */
	Transaction* refreshCache(bool force);

	/**
	 * \brief Removes the given \p packages
	 *
	 * \p allow_deps if the package manager has the right to remove other packages which depend on the
	 * pacakges to be removed. \p autoremove tells the package manager to remove all the package which
	 * won't be needed anymore after the packages are uninstalled.
	 */
	Transaction* removePackages(const QList<Package*>& packages, bool allow_deps = false, bool autoremove = false);
	Transaction* removePackage(Package* p, bool allow_deps = false, bool autoremove = false);

	/**
	 * Activates or disables a repository
	 */
	Transaction* repoEnable(const QString& repo_id, bool enable);

	/**
	 * Sets a repository's parameter
	 */
	Transaction* repoSetData(const QString& repo_id, const QString& parameter, const QString& value);

	/**
	 * \brief Tries to create a Package object from the package's name
	 *
	 * The \p filters can be used to restrict the search
	 */
	Transaction* resolve(const QStringList& packageNames, Filters filters = Filters() << NoFilter);
	Transaction* resolve(Package* package, Filters filters = Filters() << NoFilter);
	Transaction* resolve(const QStringList& packageNames, Filter filter);
	Transaction* resolve(const QString& packageName, Filters filters = Filters() << NoFilter);
	Transaction* resolve(const QString& packageName, Filter filter);

	/**
	 * Rolls back the given \p transactions
	 */
	Transaction* rollback(Transaction* oldtrans);

        /**
         * \brief Search in the packages files
         *
         * \p filters can be used to restrict the returned packages
         */
        Transaction* searchFile(const QString& search, Filters filters = Filters() << NoFilter);
        Transaction* searchFile(const QString& search, Filter filter);

	/**
	 * \brief Search in the packages details
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchDetails(const QString& search, Filters filters = Filters() << NoFilter);
        Transaction* searchDetails(const QString& search, Filter filter);

	/**
	 * \brief Lists all the packages in the given \p group
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchGroup(Client::Group group, Filters filters = Filters() << NoFilter);
	Transaction* searchGroup(Client::Group group, Filter filter);

	/**
	 * \brief Search in the packages names
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchName(const QString& search, Filters filters = Filters() << NoFilter);
	Transaction* searchName(const QString& search, Filter filter);

	/**
	 * Update the given \p packages
	 */
	Transaction* updatePackages(const QList<Package*>& packages);
	Transaction* updatePackage(Package* package);

	/**
	 * Updates the whole system
	 */
	Transaction* updateSystem();

	/**
	 * Searchs for a package providing a file/a mimetype
	 */
	Transaction* whatProvides(ProvidesType type, const QString& search, Filters filters = Filters() << NoFilter);
	Transaction* whatProvides(ProvidesType type, const QString& search, Filter filter);

Q_SIGNALS:
	/**
	 * \brief Emitted when PolicyKit doesn't grant the necessary permissions to the user
	 *
	 * \p action is the PolicyKit name of the action
	 */
	void authError(const QString& action);

	/**
	 * Emitted when the PackageKit daemon is not reachable anymore
	 */
	void daemonError(PackageKit::Client::DaemonError e);

	/**
	 * Emitted when the daemon's locked state changes
	 */
	void locked(bool locked);

	/**
	 * Emitted when the network state changes
	 */
	void networkStateChanged(PackageKit::Client::NetworkState state);

	/**
	 * Emitted when the list of repositories changes
	 */
	void repoListChanged();

	/**
	 * Emmitted when a restart is scheduled
	 */
	void restartScheduled();

	/**
	 * \brief Emitted when the current transactions list changes.
	 *
	 * \note This is mostly useful for monitoring the daemon's state.
	 */
	void transactionListChanged(const QList<PackageKit::Transaction*>&);

	/**
	 * Emitted when new updates are available
	 */
	void updatesChanged();

private:
	Client(QObject* parent = 0);
	static Client* m_instance;
	friend class ClientPrivate;
	ClientPrivate* d;
};

} // End namespace PackageKit

#endif
