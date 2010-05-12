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
#include "enum.h"

namespace PackageKit {

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
class ClientPrivate;
class Client : public QObject
{

	Q_OBJECT
	Q_ENUMS(DaemonError)

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
	 * Returns all the actions supported by the current backend
	 */
	Enum::Roles actions() const;

	/**
	 * The backend name, e.g. "yum".
	 */
	QString backendName() const;

	/**
	 * The backend description, e.g. "Yellow Dog Update Modifier".
	 */
	QString backendDescription() const;

	/**
	 * The backend author, e.g. "Joe Bloggs <joe@blogs.com>"
	 */
	QString backendAuthor() const;

	/**
	 * Returns the filters supported by the current backend
	 */
	Enum::Filters filters() const;

	/**
	 * Returns the groups supported by the current backend
	 */
	Enum::Groups groups() const;

	/**
	 * Set when the backend is locked and native tools would fail.
	 */
	bool locked() const;

	/**
	 * Returns a list containing the MIME types supported by the current backend
	 */
	QStringList mimeTypes() const;

	/**
	 * Returns the current network state
	 */
	Enum::Network networkState() const;

	/**
	 * The distribution identifier in the
	 * distro;version;arch form,
	 * e.g. "debian;squeeze/sid;x86_64".
	 */
	QString distroId() const;

	/**
	 * Allows a client to find out if it would be allowed to authorize an action.
	 * The action ID, e.g. org.freedesktop.packagekit.system-network-proxy-configure
	 * specified in \p actionId
	 * Returm might be either yes, no or interactive.
	 */
	Enum::Authorize canAuthorize(const QString &actionId) const;

	/**
	 * Returns the time (in seconds) since the specified \p action
	 */
	uint getTimeSinceAction(Enum::Role action) const;

	/**
	 * Returns the list of current transactions
	 */
	QList<Transaction*> getTransactions();

	/**
	 * \brief Sets a global hints for all the transactions to be created
	 *
	 * This method allows the calling session to set transaction \p hints for
	 * the package manager which can change as the transaction runs.
	 *
	 * This method can be sent before the transaction has been run
	 * (by using Client::setHints) or whilst it is running
	 * (by using Transaction::setHints).
	 * There is no limit to the number of times this
	 * method can be sent, although some backends may only use the values
	 * that were set before the transaction was started.
	 *
	 * The \p hints can be filled with entries like these
	 * ('locale=en_GB.utf8','idle=true','interactive=false').
	 *
	 * \sa Transaction::setHints
	 */
	void setHints(const QString& hints);
	void setHints(const QStringList& hints);

	/**
	 * Sets a proxy to be used for all the network operations
	 */
	bool setProxy(const QString& http_proxy, const QString& ftp_proxy);

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

	/**
	 * Describes a package signature
	 * \li \c package is a pointer to the signed package
	 * \li \c repoId is the id of the software repository containing the package
	 * \li \c keyUrl, \c keyId, \c keyFingerprint and \c keyTimestamp describe the key
	 * \li \c type is the signature type
	 */
	typedef struct {
		QSharedPointer<Package> package;
		QString repoId;
		QString keyUrl;
		QString keyUserid;
		QString keyId;
		QString keyFingerprint;
		QString keyTimestamp;
		Enum::SigType type;
	} SignatureInfo;

	/**
	 * Describes an EULA
	 * \li \c id is the EULA identifier
	 * \li \c package is the package for which an EULA is required
	 * \li \c vendorName is the vendor name
	 * \li \c licenseAgreement is the EULA text
	 */
	typedef struct {
		QString id;
		QSharedPointer<Package> package;
		QString vendorName;
		QString licenseAgreement;
	} EulaInfo;

	/**
	 * Describes an error at the daemon level (for example, PackageKit crashes or is unreachable)
	 *
	 * \sa Client::error
	 * \sa Transaction::error
	 */
	typedef enum {
		NoError = 0,
		UnkownError,
		ErrorFailed,
		ErrorFailedAuth,
		ErrorNoTid,
		ErrorAlreadyTid,
		ErrorRoleUnkown,
		ErrorCannotStartDaemon,
		ErrorInvalidInput,
		ErrorInvalidFile,
		ErrorFunctionNotSupported,
		ErrorDaemonUnreachable,
		/* this always has to be at the end of the list */
		LastDaemonError
	} DaemonError;

	/**
	 * Returns the last daemon error that was caught
	 */
	DaemonError getLastError() const;

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
		QSharedPointer<Package> package;
		QList<QSharedPointer<Package> > updates;
		QList<QSharedPointer<Package> > obsoletes;
		QString vendorUrl;
		QString bugzillaUrl;
		QString cveUrl;
		Enum::Restart restart;
		QString updateText;
		QString changelog;
		Enum::UpdateState state;
		QDateTime issued;
		QDateTime updated;
	} UpdateInfo;

	/**
	 * Returns the major version number.
	 */
	uint versionMajor() const;

	/**
	 * The minor version number.
	 */
	uint versionMinor() const;

	/**
	 * The micro version number.
	 */
	uint versionMicro() const;

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
	Transaction* downloadPackages(const QList<QSharedPointer<Package> >& packages);

	/**
	 * This is a convenience function
	 */
	Transaction* downloadPackages(QSharedPointer<Package> package);

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
	Transaction* getDepends(const QList<QSharedPointer<Package> >& packages, Enum::Filters filters, bool recursive);
	Transaction* getDepends(QSharedPointer<Package> package, Enum::Filters filters , bool recursive);

	/**
	 * Gets more details about the given \p packages
	 *
	 * \sa Transaction::details
	 */
	Transaction* getDetails(const QList<QSharedPointer<Package> >& packages);
	Transaction* getDetails(QSharedPointer<Package> package);

	/**
	 * Gets the files contained in the given \p packages
	 *
	 * \sa Transaction::files
	 */
	Transaction* getFiles(const QList<QSharedPointer<Package> >& packages);
	Transaction* getFiles(QSharedPointer<Package> packages);

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
	Transaction* getPackages(Enum::Filters filters = Enum::NoFilter);

	/**
	 * Gets the list of software repositories matching the given \p filters
	 */
	Transaction* getRepoList(Enum::Filters filter = Enum::NoFilter);

	/**
	 * \brief Searches for the packages requiring the given \p packages
	 *
	 * The search can be limited using the \p filters parameter. The recursive flag is used to tell
	 * if the package manager should also search for the package requiring the resulting packages.
	 */
	Transaction* getRequires(const QList<QSharedPointer<Package> >& packages, Enum::Filters filters, bool recursive);
	Transaction* getRequires(QSharedPointer<Package> package, Enum::Filters filters, bool recursive);

	/**
	 * Retrieves more details about the update for the given \p packages
	 */
	Transaction* getUpdateDetail(const QList<QSharedPointer<Package> >& packages);
	Transaction* getUpdateDetail(QSharedPointer<Package> package);

	/**
	 * \p Gets the available updates
	 *
	 * The \p filters parameters can be used to restrict the updates returned
	 */
	Transaction* getUpdates(Enum::Filters filters = Enum::NoFilter);

	/**
	 * Retrieves the available distribution upgrades
	 */
	Transaction* getDistroUpgrades();

	/**
	 * \brief Installs the local packages \p files
	 *
	 * \p only_trusted indicate if the packages are signed by a trusted authority
	 */
	Transaction* installFiles(const QStringList& files, bool only_trusted);
	Transaction* installFiles(const QString& file, bool only_trusted);

	/**
	 * Install the given \p packages
	 *
	 * \p only_trusted indicates if we should allow installation of untrusted packages (requires a different authorization)
	 */
	Transaction* installPackages(bool only_trusted, const QList<QSharedPointer<Package> >& packages);
	Transaction* installPackages(bool only_trusted, QSharedPointer<Package> p);

	/**
	 * \brief Installs a signature
	 *
	 * \p type, \p key_id and \p p generally come from the Transaction::repoSignatureRequired
	 */
	Transaction* installSignature(Enum::SigType type, const QString& key_id, QSharedPointer<Package> p);

	/**
	 * Refreshes the package manager's cache
	 */
	Transaction* refreshCache(bool force);

	/**
	 * \brief Removes the given \p packages
	 *
	 * \p allow_deps if the package manager has the right to remove other packages which depend on the
	 * packages to be removed. \p autoremove tells the package manager to remove all the package which
	 * won't be needed anymore after the packages are uninstalled.
	 */
	Transaction* removePackages(const QList<QSharedPointer<Package> >& packages, bool allow_deps, bool autoremove);
	Transaction* removePackages(QSharedPointer<Package> p, bool allow_deps, bool autoremove);

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
	Transaction* resolve(const QStringList& packageNames, Enum::Filters filters = Enum::NoFilter);
	Transaction* resolve(const QString& packageName, Enum::Filters filters = Enum::NoFilter);

	/**
	 * Rolls back the given \p transactions
	 */
	Transaction* rollback(Transaction* oldtrans);

	/**
	 * \brief Search in the packages files
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchFiles(const QStringList& search, Enum::Filters filters = Enum::NoFilter);
	Transaction* searchFiles(const QString& search, Enum::Filters filters = Enum::NoFilter);

	/**
	 * \brief Search in the packages details
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchDetails(const QStringList& search, Enum::Filters filters = Enum::NoFilter);
	Transaction* searchDetails(const QString& search, Enum::Filters filters = Enum::NoFilter);

	/**
	 * \brief Lists all the packages in the given \p group
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchGroups(Enum::Groups group, Enum::Filters filters = Enum::NoFilter);
	Transaction* searchGroups(Enum::Group group, Enum::Filters filters = Enum::NoFilter);

	/**
	 * \brief Search in the packages names
	 *
	 * \p filters can be used to restrict the returned packages
	 */
	Transaction* searchNames(const QStringList& search, Enum::Filters filters = Enum::NoFilter);
	Transaction* searchNames(const QString& search, Enum::Filters filters = Enum::NoFilter);

	/**
	 * \brief Tries to find a package name from a desktop file
	 *
	 * This function looks into /var/lib/PackageKit/desktop-files.db and searches for the associated package name.
	 *
	 * \p path the path to the desktop file (as shipped by the package)
	 * \return The associated package, or NULL if there's no result
	 */
	QSharedPointer<Package> searchFromDesktopFile(const QString& path);

	/**
	 * \brief Simulates an installation of \p files.
	 *
	 * You should call this method before installing \p files
	 * \note: This method might emit packages with INSTALLING, REMOVING, UPDATING,
	 *        REINSTALLING or OBSOLETING status.
	 */
	Transaction* simulateInstallFiles(const QStringList& files);
	Transaction* simulateInstallFiles(const QString& file);

	/**
	 * \brief Simulates an installation of \p packages.
	 *
	 * You should call this method before installing \p packages
	 * \note: This method might emit packages with INSTALLING, REMOVING, UPDATING,
	 *        REINSTALLING or OBSOLETING status.
	 */
	Transaction* simulateInstallPackages(const QList<QSharedPointer<Package> >& packages);
	Transaction* simulateInstallPackages(QSharedPointer<Package> package);

	/**
	 * \brief Simulates a removal of \p packages.
	 *
	 * You should call this method before removing \p packages
	 * \note: This method might emit packages with INSTALLING, REMOVING, UPDATING,
	 *        REINSTALLING or OBSOLETING status.
	 */
	Transaction* simulateRemovePackages(const QList<QSharedPointer<Package> >& packages, bool autoremove);
	Transaction* simulateRemovePackages(QSharedPointer<Package> package, bool autoremove);

	/**
	 * \brief Simulates an update of \p packages.
	 *
	 * You should call this method before updating \p packages
	 * \note: This method might emit packages with INSTALLING, REMOVING, UPDATING,
	 *        REINSTALLING or OBSOLETING status.
	 */
	Transaction* simulateUpdatePackages(const QList<QSharedPointer<Package> >& packages);
	Transaction* simulateUpdatePackages(QSharedPointer<Package> package);

	/**
	 * Update the given \p packages
	 */
	Transaction* updatePackages(bool only_trusted, const QList<QSharedPointer<Package> >& packages);
	Transaction* updatePackages(bool only_trusted, QSharedPointer<Package> package);

	/**
	 * Updates the whole system
	 *
	 * \p only_trusted indicates if this transaction is only allowed to install trusted packages
	 */
	Transaction* updateSystem(bool only_trusted);

	/**
	 * Searchs for a package providing a file/a mimetype
	 */
	Transaction* whatProvides(Enum::Provides type, const QStringList& search, Enum::Filters filters = Enum::NoFilter);
	Transaction* whatProvides(Enum::Provides type, const QString& search, Enum::Filters filters = Enum::NoFilter);

Q_SIGNALS:
	/**
	 * This signal is emitted when a property on the interface changes.
	 */
	void changed();

	/**
	 * Emitted when the PackageKit daemon sends an error
	 */
	void error(PackageKit::Client::DaemonError e);

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

protected:
	ClientPrivate * const d_ptr;

private:
	Q_DECLARE_PRIVATE(Client);
	Client(QObject* parent = 0);
	static Client* m_instance;
	friend class TransactionPrivate;


	void setLastError (DaemonError e);
	void setTransactionError (Transaction* t, DaemonError e);

	void destroyTransaction(const QString &tid);
};

} // End namespace PackageKit

#endif
