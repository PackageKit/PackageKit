/*
 * This file is part of the QPackageKit project
 * Copyright (C) 2008 Adrien Bustany <madcat@mymadcat.com>
 * Copyright (C) 2010 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <QtCore>
#include "enum.h"
#include "client.h"

namespace PackageKit {

class ClientPrivate;
class Package;

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
 * You cannot create a Transaction directly, use the functions in the Client class instead.
 *
 * \sa Client
 */
class TransactionPrivate;
class Transaction : public QObject
{
    Q_OBJECT
public:
    /**
     * Create a transaction object with transaction id \p tid
     * \note The if \p tid is a NULL string then it will automatically
     * asks PackageKit for a tid
     *
     * The transaction object \b cannot be reused
     * (i.e. simulateInstallPackages then installPackages)
     *
     * \warning after creating the transaction object be sure
     * to verify if it doesn't have any \sa error()
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
	Client::DaemonError error() const;

	/**
	 * Indicates weither you can cancel the transaction or not
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
	bool callerActive() const;

	/**
	 * Returns the last package processed by the transaction
	 *
	 * This is mostly used when getting an already existing Transaction, to
	 * display a more complete summary of the transaction.
	 *
	 * \return the last package processed by the transaction
	 * \sa transactionListChanged
	 * \sa getTransactionList
	 */
	QSharedPointer<Package> lastPackage() const;

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
	Enum::Role role() const;

	/**
	 * \brief Tells the underlying package manager to use the given \p hints
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
	 * \sa Client::setHints
	 */
	void setHints(const QString &hints);
	void setHints(const QStringList &hints);

	/**
	 * Returns the current state of the transaction
	 * \return a Transaction::Status value describing the status of the transaction
	 */
	Enum::Status status() const;

	/**
	 * Returns the date at which the transaction was created
	 * \return a QDateTime object containing the date at which the transaction was created
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	QDateTime timespec() const;

	/**
	 * Returns weither the trasaction succeded or not
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
     * The EULA is identified by the EulaInfo structure \p info
     *
     * \note You need to restart the transaction which triggered the EULA manually
     *
     * \sa eulaRequired
     */
    void acceptEula(const Client::EulaInfo &info);

    /**
     * Download the given \p packages to a temp dir, if \p storeInCache is true
     * the download will be stored in the package manager cache
     */
    void downloadPackages(const QList<QSharedPointer<Package> > &packages, bool storeInCache = false);

    /**
     * This is a convenience function
     */
    void downloadPackages(const QSharedPointer<Package> &package, bool storeInCache = false);

    /**
     * Returns the collection categories
     *
     * \sa category
     */
    void getCategories();

    /**
     * \brief Gets the list of dependencies for the given \p packages
     *
     * You can use the \p filters to limit the results to certain packages. The
     * \p recursive flag indicates if the package manager should also fetch the
     * dependencies's dependencies.
     *
     * \note This method emits \sa package()
     */
    void getDepends(const QList<QSharedPointer<Package> > &packages, Enum::Filters filters, bool recursive);
    void getDepends(const QSharedPointer<Package> &package, Enum::Filters filters , bool recursive);

    /**
     * Gets more details about the given \p packages
     *
     * \sa Transaction::details
     * \note This method emits \sa details()
     */
    void getDetails(const QList<QSharedPointer<Package> > &packages);
    void getDetails(const QSharedPointer<Package> &package);

    /**
     * Gets the files contained in the given \p packages
     *
     * \note This method emits \sa files()
     */
    void getFiles(const QList<QSharedPointer<Package> > &packages);
    void getFiles(const QSharedPointer<Package> &packages);

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
    void getPackages(Enum::Filters filters = Enum::NoFilter);

    /**
     * Gets the list of software repositories matching the given \p filters
     *
     * \note This method emits \sa repository()
     */
    void getRepoList(Enum::Filters filter = Enum::NoFilter);

    /**
     * \brief Searches for the packages requiring the given \p packages
     *
     * The search can be limited using the \p filters parameter. The recursive flag is used to tell
     * if the package manager should also search for the package requiring the resulting packages.
     *
     * \note This method emits \sa package()
     */
    void getRequires(const QList<QSharedPointer<Package> > &packages, Enum::Filters filters, bool recursive);
    void getRequires(const QSharedPointer<Package> &package, Enum::Filters filters, bool recursive);

    /**
     * Retrieves more details about the update for the given \p packages
     *
     * \note This method emits \sa updateDetail()
     */
    void getUpdateDetail(const QList<QSharedPointer<Package> > &packages);
    void getUpdateDetail(const QSharedPointer<Package> &package);

    /**
     * \p Gets the available updates
     *
     * The \p filters parameters can be used to restrict the updates returned
     *
     * \note This method emits \sa package()
     */
    void getUpdates(Enum::Filters filters = Enum::NoFilter);

    /**
     * Retrieves the available distribution upgrades
     *
     * \note This method emits \sa distroUpgrade()
     */
    void getDistroUpgrades();

    /**
     * \brief Installs the local packages \p files
     *
     * \p only_trusted indicate if the packages are signed by a trusted authority
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void installFiles(const QStringList &files, bool only_trusted);
    void installFiles(const QString &file, bool only_trusted);

    /**
     * Install the given \p packages
     *
     * \p only_trusted indicates if we should allow installation of untrusted packages (requires a different authorization)
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void installPackages(bool only_trusted, const QList<QSharedPointer<Package> > &packages);
    void installPackages(bool only_trusted, const QSharedPointer<Package> &package);

    /**
     * \brief Installs a signature
     *
     * \p type, \p keyId and \p package generally come from the Transaction::repoSignatureRequired
     */
    void installSignature(Enum::SigType type, const QString &keyId, const QSharedPointer<Package> &package);

    /**
     * Refreshes the package manager's cache
     *
     * \note This method emits \sa changed()
     */
    void refreshCache(bool force);

    /**
     * \brief Removes the given \p packages
     *
     * \p allow_deps if the package manager has the right to remove other packages which depend on the
     * packages to be removed. \p autoremove tells the package manager to remove all the package which
     * won't be needed anymore after the packages are uninstalled.
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void removePackages(const QList<QSharedPointer<Package> >  &packages, bool allow_deps, bool autoremove);
    void removePackages(const QSharedPointer<Package> &package, bool allow_deps, bool autoremove);

    /**
     * Activates or disables a repository
     */
    void repoEnable(const QString &repo_id, bool enable);

    /**
     * Sets a repository's parameter
     */
    void repoSetData(const QString &repo_id, const QString& parameter, const QString &value);

    /**
     * \brief Tries to create a Package object from the package's name
     *
     * The \p filters can be used to restrict the search
     *
     * \note This method emits \sa package()
     */
    void resolve(const QStringList &packageNames, Enum::Filters filters = Enum::NoFilter);
    void resolve(const QString &packageName, Enum::Filters filters = Enum::NoFilter);

    /**
     * \brief Search in the packages files
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchFiles(const QStringList &search, Enum::Filters filters = Enum::NoFilter);
    void searchFiles(const QString &search, Enum::Filters filters = Enum::NoFilter);

    /**
     * \brief Search in the packages details
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchDetails(const QStringList &search, Enum::Filters filters = Enum::NoFilter);
    void searchDetails(const QString &search, Enum::Filters filters = Enum::NoFilter);

    /**
     * \brief Lists all the packages in the given \p group
     *
     * \p groups is the name of the group that you want, when searching for
     * categories prefix it with '@'
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchGroups(const QStringList &groups, Enum::Filters filters = Enum::NoFilter);
    void searchGroups(const QString &group, Enum::Filters filters = Enum::NoFilter);

    /**
     * \brief Lists all the packages in the given \p group
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchGroups(Enum::Groups group, Enum::Filters filters = Enum::NoFilter);
    void searchGroups(Enum::Group group, Enum::Filters filters = Enum::NoFilter);

    /**
     * \brief Search in the packages names
     *
     * \p filters can be used to restrict the returned packages
     *
     * \note This method emits \sa package()
     */
    void searchNames(const QStringList &search, Enum::Filters filters = Enum::NoFilter);
    void searchNames(const QString &search, Enum::Filters filters = Enum::NoFilter);

    /**
     * \brief Simulates an installation of \p files.
     *
     * You should call this method before installing \p files
     * \note: This method might emit \sa package()
     *   with INSTALLING, REMOVING, UPDATING,
     *        REINSTALLING or OBSOLETING status.
     */
    void simulateInstallFiles(const QStringList &files);
    void simulateInstallFiles(const QString &file);

    /**
     * \brief Simulates an installation of \p packages.
     *
     * You should call this method before installing \p packages
     * \note: This method might emit \sa package()
     *   with INSTALLING, REMOVING, UPDATING,
     *        REINSTALLING or OBSOLETING status.
     */
    void simulateInstallPackages(const QList<QSharedPointer<Package> > &packages);
    void simulateInstallPackages(const QSharedPointer<Package> &package);

    /**
     * \brief Simulates a removal of \p packages.
     *
     * You should call this method before removing \p packages
     * \note: This method might emit \sa package()
     *   with INSTALLING, REMOVING, UPDATING,
     *        REINSTALLING or OBSOLETING status.
     */
    void simulateRemovePackages(const QList<QSharedPointer<Package> > &packages, bool autoremove);
    void simulateRemovePackages(const QSharedPointer<Package> &package, bool autoremove);

    /**
     * \brief Simulates an update of \p packages.
     *
     * You should call this method before updating \p packages
     * \note: This method might emit \sa package()
     *   with INSTALLING, REMOVING, UPDATING,
     *        REINSTALLING or OBSOLETING status.
     */
    void simulateUpdatePackages(const QList<QSharedPointer<Package> > &packages);
    void simulateUpdatePackages(const QSharedPointer<Package> &package);

    /**
     * Update the given \p packages
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void updatePackages(bool only_trusted, const QList<QSharedPointer<Package> > &packages);
    void updatePackages(bool only_trusted, const QSharedPointer<Package> &package);

    /**
     * Updates the whole system
     *
     * \p only_trusted indicates if this transaction is only allowed to install trusted packages
     *
     * \note This method emits \sa package()
     * and \sa changed()
     */
    void updateSystem(bool only_trusted);

    /**
     * Searchs for a package providing a file/a mimetype
     *
     * \note This method emits \sa package()
     */
    void whatProvides(Enum::Provides type, const QStringList &search, Enum::Filters filters = Enum::NoFilter);
    void whatProvides(Enum::Provides type, const QString &search, Enum::Filters filters = Enum::NoFilter);


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
	 * \li \p parent_id is the id of the parent category. A blank parent means a root category
	 * \li \p cat_id is the id of the category
	 * \li \p name is the category's name. This name is localized.
	 * \li \p summary is the category's summary. It is localized.
	 * \li \p icon is the icon identifier eg. server-cfg. If unknown, it is set to icon-missing.
	 */
	void category(const QString &parent_id, const QString &cat_id, const QString &name, const QString &summary, const QString &icon);

	/**
	 * Sends additional details about the \p package
	 * \sa getDetails()
	 */
	void details(const QSharedPointer<PackageKit::Package> &package);

	/**
	 * Sent when the transaction has been destroyed and is
	 * no longer available for use.
	 * \warning the object will get deleted after this method is called
	 */
	void destroy ();

	/**
	 * Emitted when a distribution upgrade is available
	 * \sa getDistroUpgrades()
	 */
	void distroUpgrade(PackageKit::Enum::DistroUpgrade type, const QString& name, const QString& description);

	/**
	 * Emitted when an error occurs
	 */
	void errorCode(PackageKit::Enum::Error error, const QString& details);

	/**
	 * Emitted when an EULA agreement prevents the transaction from running
	 * \note You will need to relaunch the transaction after accepting the EULA
	 * \sa acceptEula()
	 */
	void eulaRequired(const PackageKit::Client::EulaInfo &info);

	/**
	 * Emitted when a different media is required in order to fetch packages
	 * which prevents the transaction from running
	 * \note You will need to relaunch the transaction after changing the media
	 * \sa Transaction::MediaType
	 */
	void mediaChangeRequired(PackageKit::Enum::MediaType type, const QString& id, const QString& text);

	/**
	 * Sends the \p filenames contained in package \p package
	 * \sa Client::getFiles
	 */
	void files(const QSharedPointer<PackageKit::Package> &package, const QStringList &filenames);

	/**
	 * Emitted when the transaction finishes
	 *
	 * \p status describes the exit status, \p runtime is the number of seconds it took to complete the transaction
	 */
	void finished(PackageKit::Enum::Exit status, uint runtime);

	/**
	 * Conveys a message sent from the backend
	 *
	 * \p type is the type of the \p message
	 */
	void message(PackageKit::Enum::Message type, const QString &message);

	/**
	 * Emitted when the transaction sends a new package
	 */
	void package(const QSharedPointer<PackageKit::Package> &package);

	/**
	 * Sends some additional details about a software repository
	 * \sa getRepoList()
	 */
	void repoDetail(const QString& repoId, const QString& description, bool enabled);

	/**
	 * Emitted when the user has to validate a repository's signature
	 */
	void repoSignatureRequired(const PackageKit::Client::SignatureInfo &info);

	/**
	 * Indicates that a restart is required
	 * \p package is the package who triggered the restart signal
	 */
	void requireRestart(PackageKit::Enum::Restart type, const QSharedPointer<PackageKit::Package> &package);

	/**
	 * Sends an old transaction
	 * \sa Client::getOldTransactions
	 */
	void transaction(PackageKit::Transaction *transaction);

	/**
	 * Sends additionnal details about an update
	 * \sa Client::getUpdateDetail
	 */
	void updateDetail(const PackageKit::Client::UpdateInfo &info);

protected:
	TransactionPrivate * const d_ptr;

private:
	Q_DECLARE_PRIVATE(Transaction);
	friend class ClientPrivate;
	Transaction(const QString &tid,
                const QString &timespec,
                bool succeeded,
                const QString &role,
                uint duration,
                const QString &data,
                uint uid,
                const QString &cmdline,
                QObject *parent);
};

} // End namespace PackageKit

#endif
