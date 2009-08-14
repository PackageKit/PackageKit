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

#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <QtCore>
#include "package.h"
#include "client.h"

namespace PackageKit {

class Client;
class ClientPrivate;
class Package;
class TransactionPrivate;

/**
 * \class Transaction transaction.h Transaction
 * \author Adrien Bustany <madcat@mymadcat.com>
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
class Transaction : public QObject
{
	Q_OBJECT
	Q_ENUMS(TransactionError)
	Q_ENUMS(Status)
	Q_ENUMS(ExitStatus)
	Q_ENUMS(MediaType)

public:
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
	 */
	Client::DaemonError error () const;

	/**
	 * Indicates weither you can cancel the transaction or not
	 *
	 * \return true if you can cancel the transaction, false else
	 */
	bool allowCancel();

	/**
	 * Indicates weither the transaction caller is active or not
	 *
	 * The caller can be inactive if it has quitted before the transaction finished.
	 *
	 * \return true if the caller is active, false else
	 */
	bool callerActive();

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
	Package* lastPackage();

	/**
	 * Gathers all the information about a transaction's progress in one struct
	 * \li \c percentage is the global percentage (for example, 40\% when having installed 4 packages out of 10)
	 * \li \c subpercentage is the percentage of the current subtask (for example, 60\% of the 4th package installation)
	 * \li \c elapsed is the number of seconds elapsed since the beginning of the transaction
	 * \li \c remaining is the estimated number of seconds remaining until the transaction finishes
	 */
	typedef struct {
		uint percentage;
		uint subpercentage;
		uint elapsed;
		uint remaining;
	} ProgressInfo;
	/**
	 * Returns the current transaction's progress
	 * \return a ProgressInfo struct describing the transaction's progress
	 */
	ProgressInfo progress();

	/**
	 * The RoleInfo is a describing the current state of a transaction
	 * \li \c action describes the operation carried on by the transaction
	 * \li \c terms are the terms (for example search terms) used when creating the transaction
	 */
	typedef struct {
		Client::Action action;
		QStringList terms;
	} RoleInfo;
	/**
	 * Returns information describing the transaction
	 * \return a RoleInfo struct describing the current transaction
	 */
	RoleInfo role();

	/**
	 * \brief Tells the underlying package manager to use the given \p locale
	 *
	 * It's recommanded to call Client::setLocale, which will in turn call setLocale
	 * on every created transaction.
	 *
	 * \sa Client::setLocale
	 */
	void setLocale(const QString& locale);

	/**
	 * Describes the current state of the transaction
	 */
	typedef enum {
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
		UnknownStatus
	} Status;
	/**
	 * Returns the current state of the transaction
	 * \return a Transaction::Status value describing the status of the transaction
	 */
	Status status();

	/**
	 * Returns the date at which the transaction was created
	 * \return a QDateTime object containing the date at which the transaction was created
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	QDateTime timespec();

	/**
	 * Returns weither the trasaction succeded or not
	 * \return true if the transaction succeeded, false else
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	bool succeeded();

	/**
	 * Returns the time the transaction took to finish
	 * \return the number of milliseconds the transaction took to finish
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	uint duration();

	/**
	 * Returns some data set by the backend to pass additionnal information
	 * \return a string set by the backend
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	QString data();

	/**
	 * Returns the UID of the calling process
	 * \return the uid of the calling process
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	uint uid();

	/**
	 * Returns the command line for the calling process
	 * \return a string of the command line for the calling process
	 * \note This function only returns a real value for old transactions returned by getOldTransactions
	 */
	QString cmdline();

	/**
	 * Describes how the transaction finished
	 * \sa finished()
	 */
	typedef enum {
		ExitSuccess,
		ExitFailed,
		ExitCancelled,
		ExitKeyRequired,
		ExitEulaRequired,
		ExitKilled, /* when we forced the cancel, but had to sigkill */
		ExitMediaChangeRequired,
		UnknownExitStatus
	} ExitStatus;

	/**
	 * Describes what kind of media is required
	 */
	typedef enum {
		MediaCd,
		MediaDvd,
		MediaDisc,
		UnknownMediaType
	} MediaType;

public Q_SLOTS:
	/**
	 * Cancels the transaction
	 */
	void cancel();

Q_SIGNALS:
	/**
	 * The transaction has changed it's "cancellability"
	 */
	void allowCancelChanged(bool allow);

	/**
	 * The transaction's caller activity changed
	 */
	void callerActiveChanged(bool isActive);

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
	 * \sa Client::getDetails
	 */
	void details(PackageKit::Package* package);

	/**
	 * Sent when the transaction has been destroyed and is
	 * no longer available for use.
	 */
	void destroy ();

	/**
	 * Emitted when a distribution upgrade is available
	 * \sa Client::getDistroUpgrades
	 */
	void distroUpgrade(PackageKit::Client::DistroUpgradeType type, const QString& name, const QString& description);

	/**
	 * Emitted when an error occurs
	 */
	void errorCode(PackageKit::Client::ErrorType error, const QString& details);

	/**
	 * Emitted when an EULA agreement prevents the transaction from running
	 * \note You will need to relaunch the transaction after accepting the EULA
	 * \sa Client::acceptEula
	 */
	void eulaRequired(PackageKit::Client::EulaInfo info);

	/**
	 * Emitted when a different media is required in order to fetch packages
	 * which prevents the transaction from running
	 * \note You will need to relaunch the transaction after changing the media
	 * \sa Transaction::MediaType
	 */
	void mediaChangeRequired(PackageKit::Transaction::MediaType type, const QString& id, const QString& text);

	/**
	 * Sends the \p filenames contained in package \p p
	 * \sa Client::getFiles
	 */
	void files(PackageKit::Package* p, const QStringList& filenames);

	/**
	 * Emitted when the transaction finishes
	 *
	 * \p status describes the exit status, \p runtime is the number of seconds it took to complete the transaction
	 */
	void finished(PackageKit::Transaction::ExitStatus status, uint runtime);

	/**
	 * Conveys a message sent from the backend
	 *
	 * \p type is the type of the \p message
	 */
	void message(PackageKit::Client::MessageType type, const QString& message);

	/**
	 * Emitted when the transaction sends a new package
	 */
	void package(PackageKit::Package* p);

	/**
	 * Emitted when the progress of the transaction has changed
	 */
	void progressChanged(PackageKit::Transaction::ProgressInfo info);

	/**
	 * Sends some additional details about a software repository
	 * \sa Client::getRepoList
	 */
	void repoDetail(const QString& repoId, const QString& description, bool enabled);

	/**
	 * Emitted when the user has to validate a repository's signature
	 */
	void repoSignatureRequired(PackageKit::Client::SignatureInfo info);

	/**
	 * Indicates that a restart is required
	 * \p package is the package who triggered the restart signal
	 */
	void requireRestart(PackageKit::Client::RestartType type, Package* p);

	/**
	 * Emitted when the transaction's status has changed
	 */
	void statusChanged(PackageKit::Transaction::Status s);

	/**
	 * Sends an old transaction
	 * \sa Client::getOldTransactions
	 */
	void transaction(PackageKit::Transaction* t);

	/**
	 * Sends additionnal details about an update
	 * \sa Client::getUpdateDetail
	 */
	void updateDetail(PackageKit::Client::UpdateInfo info);

	/**
	 * \internal
	 * Used to clean the transaction pool in Client
	 */
	void destroyed(const QString& tid);

private:
	friend class Client;
	friend class ClientPrivate;
	Transaction(const QString& tid, Client* parent);
	Transaction(const QString& tid, const QString& timespec, bool succeeded, const QString& role, uint duration, const QString& data, uint uid, const QString& cmdline, Client* parent);

	friend class TransactionPrivate;
	TransactionPrivate* d;

};

} // End namespace PackageKit

#endif
