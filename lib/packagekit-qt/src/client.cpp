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

#include <QtSql>

#include "client.h"
#include "clientprivate.h"

#include "common.h"
#include "daemonproxy.h"
#include "transaction.h"
#include "transactionprivate.h"
#include "transactionproxy.h"
#include "package.h"
#include "util.h"

#define CREATE_NEW_TRANSACTION                      \
		Transaction* t = d->createNewTransaction(); \
		if (t->tid ().isEmpty ()) {                                   \
			setLastError (ErrorDaemonUnreachable);  \
			setTransactionError (t, ErrorDaemonUnreachable); \
			return t;                            \
		}                                           \

#define CHECK_TRANSACTION                                          \
		if (r.isError ()) {                                       \
			setTransactionError (t, daemonErrorFromDBusReply (r)); \
		}                                                          \

#define RUN_TRANSACTION(blurb) \
		Q_D(Client);   \
		CREATE_NEW_TRANSACTION \
		QDBusPendingReply<> r = t->d_ptr->p->blurb;        \
		r.waitForFinished (); \
		CHECK_TRANSACTION      \
		return t;              \

#define PK_DESKTOP_DEFAULT_DATABASE		LOCALSTATEDIR "/lib/PackageKit/desktop-files.db"

using namespace PackageKit;

Client* Client::m_instance = 0;

template<class T> Client::DaemonError daemonErrorFromDBusReply (QDBusPendingReply<T> e) {
	return Util::errorFromString (e.error ().name ());
}

Client* Client::instance()
{
	if(!m_instance)
		m_instance = new Client(qApp);

	return m_instance;
}

Client::Client(QObject* parent) : QObject(parent), d_ptr(new ClientPrivate(this))
{
	Q_D(Client);
	d->daemon = new ::DaemonProxy(PK_NAME, PK_PATH, QDBusConnection::systemBus(), this);

	d->error = NoError;

	connect(d->daemon, SIGNAL(Changed()), this, SIGNAL(changed()));
	connect(d->daemon, SIGNAL(RepoListChanged()), this, SIGNAL(repoListChanged()));
	connect(d->daemon, SIGNAL(RestartSchedule()), this, SIGNAL(restartScheduled()));
	connect(d->daemon, SIGNAL(TransactionListChanged(const QStringList&)), d, SLOT(transactionListChanged(const QStringList&)));
	connect(d->daemon, SIGNAL(UpdatesChanged()), this, SIGNAL(updatesChanged()));

	// Set up database for desktop files
	QSqlDatabase db;
	db = QSqlDatabase::addDatabase("QSQLITE");
	db.setDatabaseName (PK_DESKTOP_DEFAULT_DATABASE);
	if (!db.open()) {
		qDebug() << "Failed to initialize the desktop files database";
	}

	// Set up a watch on DBus so that we know if the daemon disappears
	connect (QDBusConnection::systemBus ().interface (), SIGNAL(serviceOwnerChanged (const QString&, const QString&, const QString&)), d, SLOT (serviceOwnerChanged (const QString&, const QString&, const QString&)));
}

Client::~Client()
{
}

Enum::Roles Client::actions() const
{
	Q_D(const Client);
	QStringList roles = d->daemon->roles().split(";");

	Enum::Roles flags;
	foreach(const QString& role, roles) {
		flags |= (Enum::Role) Util::enumFromString<Enum>(role, "Role", "Role");
	}
	return flags;
}

QString Client::backendName() const
{
	Q_D(const Client);
	return d->daemon->backendName();
}

QString Client::backendDescription() const
{
	Q_D(const Client);
	return d->daemon->backendDescription();
}

QString Client::backendAuthor() const
{
	Q_D(const Client);
	return d->daemon->backendAuthor();
}

Enum::Filters Client::filters() const
{
	Q_D(const Client);
	QStringList filters = d->daemon->filters().split(";");

	// Adapt a slight difference in the enum
	if(filters.contains("none")) {
		filters[filters.indexOf("none")] = "no-filter";
	}

	Enum::Filters flags;
	foreach(const QString& filter, filters) {
		flags |= (Enum::Filter) Util::enumFromString<Enum>(filter, "Filter", "Filter");
	}
	return flags;
}

Enum::Groups Client::groups() const
{
	Q_D(const Client);
	QStringList groups = d->daemon->groups().split(";");

	Enum::Groups flags;
	foreach(const QString& group, groups) {
		flags.insert((Enum::Group) Util::enumFromString<Enum>(group, "Group", "Group"));
	}
	return flags;
}

bool Client::locked() const
{
	Q_D(const Client);
	return d->daemon->locked();
}

QStringList Client::mimeTypes() const
{
	Q_D(const Client);
	return d->daemon->mimeTypes().split(";");
}

Enum::Network Client::networkState() const
{
	Q_D(const Client);
	QString state = d->daemon->networkState();
	return (Enum::Network) Util::enumFromString<Enum>(state, "Network", "Network");
}

QString Client::distroId() const
{
	Q_D(const Client);
	return d->daemon->distroId();
}

Enum::Authorize Client::canAuthorize(const QString &actionId) const
{
	Q_D(const Client);
	QString result = d->daemon->CanAuthorize(actionId);
	return (Enum::Authorize) Util::enumFromString<Enum>(result, "Authorize", "Authorize");;
}

uint Client::getTimeSinceAction(Enum::Role role) const
{
	Q_D(const Client);
	QString roleName = Util::enumToString<Enum>(role, "Role", "Role");
	return d->daemon->GetTimeSinceAction(roleName);
}

QList<Transaction*> Client::getTransactions()
{
	Q_D(Client);
	QStringList tids = d->daemon->GetTransactionList();

	return d->transactions(tids);
}

void Client::setHints(const QStringList& hints)
{
	Q_D(Client);
	d->hints = hints;
}

void Client::setHints(const QString& hints)
{
	Q_D(Client);
	d->hints = QStringList() << hints;
}

bool Client::setProxy(const QString& http_proxy, const QString& ftp_proxy)
{
	Q_D(Client);
	QDBusPendingReply<> r = d->daemon->SetProxy(http_proxy, ftp_proxy);
	r.waitForFinished ();
	if (r.isError ()) {
		setLastError (daemonErrorFromDBusReply (r));
		return false;
	} else {
		return true;
	}
}

void Client::stateHasChanged(const QString& reason)
{
	Q_D(Client);
	d->daemon->StateHasChanged(reason);
}

void Client::suggestDaemonQuit()
{
	Q_D(Client);
	d->daemon->SuggestDaemonQuit();
}

Client::DaemonError Client::getLastError() const
{
	Q_D(const Client);
	return d->error;
}

uint Client::versionMajor() const
{
	Q_D(const Client);
	return d->daemon->versionMajor();
}

uint Client::versionMinor() const
{
	Q_D(const Client);
	return d->daemon->versionMinor();
}

uint Client::versionMicro() const
{
	Q_D(const Client);
	return d->daemon->versionMicro();
}

////// Transaction functions

Transaction* Client::acceptEula(EulaInfo info)
{
	RUN_TRANSACTION(AcceptEula(info.id))
}

Transaction* Client::downloadPackages(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(DownloadPackages(Util::packageListToPids(packages)))
}

Transaction* Client::downloadPackages(QSharedPointer<Package> package)
{
	return downloadPackages(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getDepends(const QList<QSharedPointer<Package> >& packages, Enum::Filters filters, bool recursive)
{
	RUN_TRANSACTION(GetDepends(Util::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

Transaction* Client::getDepends(QSharedPointer<Package> package, Enum::Filters filters, bool recursive)
{
	return getDepends(QList<QSharedPointer<Package> >() << package, filters, recursive);
}

Transaction* Client::getDetails(const QList<QSharedPointer<Package> >& packages)
{
	Q_D(Client);
	CREATE_NEW_TRANSACTION

	foreach(QSharedPointer<Package> p, packages) {
		t->d_ptr->packageMap.insert(p->id(), p);
	}

	QDBusPendingReply<> r = t->d_ptr->p->GetDetails(Util::packageListToPids(packages));
	r.waitForFinished ();

	CHECK_TRANSACTION

	return t;
}

Transaction* Client::getDetails(QSharedPointer<Package> package)
{
	return getDetails(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getFiles(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(GetFiles(Util::packageListToPids(packages)))
}

Transaction* Client::getFiles(QSharedPointer<Package> package)
{
	return getFiles(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getOldTransactions(uint number)
{
	RUN_TRANSACTION(GetOldTransactions(number))
}

Transaction* Client::getPackages(Enum::Filters filters)
{
	RUN_TRANSACTION(GetPackages(Util::filtersToString(filters)))
}

Transaction* Client::getRepoList(Enum::Filters filters)
{
	RUN_TRANSACTION(GetRepoList(Util::filtersToString(filters)))
}

Transaction* Client::getRequires(const QList<QSharedPointer<Package> >& packages, Enum::Filters filters, bool recursive)
{
	RUN_TRANSACTION(GetRequires(Util::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

Transaction* Client::getRequires(QSharedPointer<Package> package, Enum::Filters filters, bool recursive)
{
	return getRequires(QList<QSharedPointer<Package> >() << package, filters, recursive);
}

Transaction* Client::getUpdateDetail(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(GetUpdateDetail(Util::packageListToPids(packages)))
}

Transaction* Client::getUpdateDetail(QSharedPointer<Package> package)
{
	return getUpdateDetail(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getUpdates(Enum::Filters filters)
{
	RUN_TRANSACTION(GetUpdates(Util::filtersToString(filters)))
}

Transaction* Client::getDistroUpgrades()
{
	RUN_TRANSACTION(GetDistroUpgrades())
}

Transaction* Client::installFiles(const QStringList& files, bool only_trusted)
{
	RUN_TRANSACTION(InstallFiles(only_trusted, files))
}

Transaction* Client::installFiles(const QString& file, bool only_trusted)
{
	return installFiles(QStringList() << file, only_trusted);
}

Transaction* Client::installPackages(bool only_trusted, const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(InstallPackages(only_trusted, Util::packageListToPids(packages)))
}

Transaction* Client::installPackages(bool only_trusted, QSharedPointer<Package> p)
{
	return installPackages(only_trusted, QList<QSharedPointer<Package> >() << p);
}

Transaction* Client::installSignature(Enum::SigType type, const QString& key_id, QSharedPointer<Package> p)
{
	RUN_TRANSACTION(InstallSignature(Util::enumToString<Enum>(type, "SigType", "Signature"), key_id, p->id()))
}

Transaction* Client::refreshCache(bool force)
{
	RUN_TRANSACTION(RefreshCache(force))
}

Transaction* Client::removePackages(const QList<QSharedPointer<Package> >& packages, bool allow_deps, bool autoremove)
{
	RUN_TRANSACTION(RemovePackages(Util::packageListToPids(packages), allow_deps, autoremove))
}

Transaction* Client::removePackages(QSharedPointer<Package> p, bool allow_deps, bool autoremove)
{
	return removePackages(QList<QSharedPointer<Package> >() << p, allow_deps, autoremove);
}

Transaction* Client::repoEnable(const QString& repo_id, bool enable)
{
	RUN_TRANSACTION(RepoEnable(repo_id, enable))
}

Transaction* Client::repoSetData(const QString& repo_id, const QString& parameter, const QString& value)
{
	RUN_TRANSACTION(RepoSetData(repo_id, parameter, value))
}

Transaction* Client::resolve(const QStringList& packageNames, Enum::Filters filters)
{
	RUN_TRANSACTION(Resolve(Util::filtersToString(filters), packageNames))
}

Transaction* Client::resolve(const QString& packageName, Enum::Filters filters)
{
	return resolve(QStringList() << packageName, filters);
}

Transaction* Client::rollback(Transaction* oldtrans)
{
	RUN_TRANSACTION(Rollback(oldtrans->tid()))
}

Transaction* Client::searchFiles(const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(SearchFiles(Util::filtersToString(filters), search))
}

Transaction* Client::searchFiles(const QString& search, Enum::Filters filters)
{
	return searchFiles(QStringList() << search, filters);
}

Transaction* Client::searchDetails(const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(SearchDetails(Util::filtersToString(filters), search))
}

Transaction* Client::searchDetails(const QString& search, Enum::Filters filters)
{
	return searchDetails(QStringList() << search, filters);
}

Transaction* Client::searchGroups(Enum::Groups groups, Enum::Filters filters)
{
	QStringList groupsSL;
	foreach (const Enum::Group group, groups) {
		groupsSL << Util::enumToString<Enum>(group, "Group", "Group");
	}

	RUN_TRANSACTION(SearchGroups(Util::filtersToString(filters), groupsSL))
}

Transaction* Client::searchGroups(Enum::Group group, Enum::Filters filters)
{
	return searchGroups(Enum::Groups() << group, filters);
}

Transaction* Client::searchNames(const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(SearchNames(Util::filtersToString(filters), search))
}

Transaction* Client::searchNames(const QString& search, Enum::Filters filters)
{
	return searchNames(QStringList() << search, filters);
}

QSharedPointer<Package> Client::searchFromDesktopFile(const QString& path)
{
	QSqlDatabase db = QSqlDatabase::database();
	if (!db.isOpen()) {
		qDebug() << "Desktop files database is not open";
		return QSharedPointer<Package> (NULL);
	}

	QSqlQuery q(db);
	q.prepare("SELECT package FROM cache WHERE filename = :path");
	q.bindValue(":path", path);
	if(!q.exec()) {
		qDebug() << "Error while running query " << q.executedQuery();
		return QSharedPointer<Package> (NULL);
	}

	if (!q.next()) return QSharedPointer<Package> (NULL); // Return NULL if no results

	return QSharedPointer<Package> (new Package(q.value(0).toString()));

}

Transaction* Client::simulateInstallFiles(const QStringList& files)
{
	RUN_TRANSACTION(SimulateInstallFiles(files))
}

Transaction* Client::simulateInstallFiles(const QString& file)
{
	return simulateInstallFiles(QStringList() << file);
}

Transaction* Client::simulateInstallPackages(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(SimulateInstallPackages(Util::packageListToPids(packages)))
}

Transaction* Client::simulateInstallPackages(QSharedPointer<Package> package)
{
	return simulateInstallPackages(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::simulateRemovePackages(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(SimulateRemovePackages(Util::packageListToPids(packages)))
}

Transaction* Client::simulateRemovePackages(QSharedPointer<Package> package)
{
	return simulateRemovePackages(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::simulateUpdatePackages(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(SimulateUpdatePackages(Util::packageListToPids(packages)))
}

Transaction* Client::simulateUpdatePackages(QSharedPointer<Package> package)
{
	return simulateUpdatePackages(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::updatePackages(bool only_trusted, const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(UpdatePackages(only_trusted, Util::packageListToPids(packages)))
}

Transaction* Client::updatePackages(bool only_trusted, QSharedPointer<Package> package)
{
	return updatePackages(only_trusted, QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::updateSystem(bool only_trusted)
{
	RUN_TRANSACTION(UpdateSystem(only_trusted))
}

Transaction* Client::whatProvides(Enum::Provides type, const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(WhatProvides(Util::filtersToString(filters), Util::enumToString<Enum>(type, "Provides", "Provides"), search))
}

Transaction* Client::whatProvides(Enum::Provides type, const QString& search, Enum::Filters filters)
{
	return whatProvides(type, QStringList() << search, filters);
}

void Client::setLastError (DaemonError e)
{
	Q_D(Client);
	d->error = e;
	emit error (e);
}

void Client::setTransactionError (Transaction* t, DaemonError e)
{
	t->d_ptr->error = e;
}

void Client::destroyTransaction(const QString &tid)
{
	Q_D(Client);
	d->removeTransactionFromPool(tid);
}

#include "client.moc"

