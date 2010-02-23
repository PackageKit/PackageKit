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
		CREATE_NEW_TRANSACTION \
		QDBusPendingReply<> r = t->d->p->blurb;        \
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

Client::Client(QObject* parent) : QObject(parent)
{
	d = new ClientPrivate(this);

	d->daemon = new DaemonProxy(PK_NAME, PK_PATH, QDBusConnection::systemBus(), this);
	d->locale = QString();

	d->error = NoError;

	connect(d->daemon, SIGNAL(Changed()), this, SIGNAL(changed()));
	connect(d->daemon, SIGNAL(Locked(bool)), this, SIGNAL(locked(bool)));
	connect(d->daemon, SIGNAL(NetworkStateChanged(const QString&)), d, SLOT(networkStateChanged(const QString&)));
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
	delete d;
}

Client::Actions Client::actions() const
{
	QStringList actions = d->daemon->roles().split(";");

	Actions flags;
	foreach(const QString& action, actions) {
		flags |= (Action) Util::enumFromString<Client>(action, "Action", "Action");
	}
	return flags;
}

Client::Actions Client::getActions() const
{
	return actions();
}

Client::BackendDetail Client::getBackendDetail() const
{
	BackendDetail detail;
	detail.name = backendName();
	detail.author = backendAuthor();
	return detail;
}

QString Client::backendName() const
{
	return d->daemon->backendName();
}

QString Client::backendDescription() const
{
	return d->daemon->backendDescription();
}

QString Client::backendAuthor() const
{
	return d->daemon->backendAuthor();
}

Client::Filters Client::filters() const
{
	QStringList filters = d->daemon->filters().split(";");

	// Adapt a slight difference in the enum
	if(filters.contains("none")) {
		filters[filters.indexOf("none")] = "no-filter";
	}

	Filters flags;
	foreach(const QString& filter, filters) {
		flags |= (Filter) Util::enumFromString<Client>(filter, "Filter", "Filter");
	}
	return flags;
}

Client::Filters Client::getFilters() const
{
	return filters();
}

Client::Groups Client::groups() const
{
	QStringList groups = d->daemon->groups().split(";");

	Groups flags;
	foreach(const QString& group, groups) {
		flags.insert((Group) Util::enumFromString<Client>(group, "Group", "Group"));
	}
	return flags;
}

Client::Groups Client::getGroups() const
{
	return groups();
}

bool Client::locked() const
{
	return d->daemon->locked();
}

QStringList Client::mimeTypes() const
{
	return d->daemon->mimeTypes().split(";");
}

QStringList Client::getMimeTypes() const
{
	return mimeTypes();
}

Client::NetworkState Client::networkState() const
{
	QString state = d->daemon->networkState();
	return (NetworkState) Util::enumFromString<Client>(state, "NetworkState", "Network");
}

Client::NetworkState Client::getNetworkState() const
{
	return networkState();
}

QString Client::distroId() const
{
	return d->daemon->distroId();
}

uint Client::getTimeSinceAction(Action action) const
{
	QString pkName = Util::enumToString<Client>(action, "Action", "Action");
	return d->daemon->GetTimeSinceAction(pkName);
}

QList<Transaction*> Client::getTransactions()
{
	QStringList tids = d->daemon->GetTransactionList();
	QList<Transaction*> transactions;
	foreach(const QString& tid, tids) {
		transactions.append(new Transaction(tid, this));
	}

	return transactions;
}

void Client::setLocale(const QString& locale)
{
	d->locale = locale;
}

void Client::setHints(const QStringList& hints)
{
	d->hints = hints;
}

void Client::setHints(const QString& hints)
{
	d->hints = QStringList() << hints;
}

bool Client::setProxy(const QString& http_proxy, const QString& ftp_proxy)
{
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
	d->daemon->StateHasChanged(reason);
}

void Client::suggestDaemonQuit()
{
	d->daemon->SuggestDaemonQuit();
}

Client::DaemonError Client::getLastError() const
{
	return d->error;
}

uint Client::versionMajor() const
{
	return d->daemon->versionMajor();
}

uint Client::versionMinor() const
{
	return d->daemon->versionMinor();
}

uint Client::versionMicro() const
{
	return d->daemon->versionMicro();
}

////// Transaction functions

Transaction* Client::acceptEula(EulaInfo info)
{
	RUN_TRANSACTION(AcceptEula(info.id))
}

Transaction* Client::downloadPackages(const QList<Package*>& packages)
{
	RUN_TRANSACTION(DownloadPackages(Util::packageListToPids(packages)))
}

Transaction* Client::downloadPackage(Package* package)
{
	return downloadPackages(QList<Package*>() << package);
}

Transaction* Client::getDepends(const QList<Package*>& packages, Filters filters, bool recursive)
{
	RUN_TRANSACTION(GetDepends(Util::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

Transaction* Client::getDepends(Package* package, Filters filters, bool recursive)
{
	return getDepends(QList<Package*>() << package, filters, recursive);
}

Transaction* Client::getDetails(const QList<Package*>& packages)
{
	CREATE_NEW_TRANSACTION

	foreach(Package* p, packages) {
		t->d->packageMap.insert(p->id(), p);
	}

	QDBusPendingReply<> r = t->d->p->GetDetails(Util::packageListToPids(packages));
	r.waitForFinished ();

	CHECK_TRANSACTION

	return t;
}

Transaction* Client::getDetails(Package* package)
{
	return getDetails(QList<Package*>() << package);
}

Transaction* Client::getFiles(const QList<Package*>& packages)
{
	RUN_TRANSACTION(GetFiles(Util::packageListToPids(packages)))
}

Transaction* Client::getFiles(Package* package)
{
	return getFiles(QList<Package*>() << package);
}

Transaction* Client::getOldTransactions(uint number)
{
	RUN_TRANSACTION(GetOldTransactions(number))
}

Transaction* Client::getPackages(Filters filters)
{
	RUN_TRANSACTION(GetPackages(Util::filtersToString(filters)))
}

Transaction* Client::getRepoList(Filters filters)
{
	RUN_TRANSACTION(GetRepoList(Util::filtersToString(filters)))
}

Transaction* Client::getRequires(const QList<Package*>& packages, Filters filters, bool recursive)
{
	RUN_TRANSACTION(GetRequires(Util::filtersToString(filters), Util::packageListToPids(packages), recursive))
}

Transaction* Client::getRequires(Package* package, Filters filters, bool recursive)
{
	return getRequires(QList<Package*>() << package, filters, recursive);
}

Transaction* Client::getUpdateDetail(const QList<Package*>& packages)
{
	RUN_TRANSACTION(GetUpdateDetail(Util::packageListToPids(packages)))
}

Transaction* Client::getUpdateDetail(Package* package)
{
	return getUpdateDetail(QList<Package*>() << package);
}

Transaction* Client::getUpdates(Filters filters)
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

Transaction* Client::installFile(const QString& file, bool only_trusted)
{
	return installFiles(QStringList() << file, only_trusted);
}

Transaction* Client::installPackages(bool only_trusted, const QList<Package*>& packages)
{
	RUN_TRANSACTION(InstallPackages(only_trusted, Util::packageListToPids(packages)))
}

Transaction* Client::installPackage(bool only_trusted, Package* p)
{
	return installPackages(only_trusted, QList<Package*>() << p);
}

Transaction* Client::installSignature(SignatureType type, const QString& key_id, Package* p)
{
	RUN_TRANSACTION(InstallSignature(Util::enumToString<Client>(type, "SignatureType", "Signature"), key_id, p->id()))
}

Transaction* Client::refreshCache(bool force)
{
	RUN_TRANSACTION(RefreshCache(force))
}

Transaction* Client::removePackages(const QList<Package*>& packages, bool allow_deps, bool autoremove)
{
	RUN_TRANSACTION(RemovePackages(Util::packageListToPids(packages), allow_deps, autoremove))
}

Transaction* Client::removePackage(Package* p, bool allow_deps, bool autoremove)
{
	return removePackages(QList<Package*>() << p, allow_deps, autoremove);
}

Transaction* Client::repoEnable(const QString& repo_id, bool enable)
{
	RUN_TRANSACTION(RepoEnable(repo_id, enable))
}

Transaction* Client::repoSetData(const QString& repo_id, const QString& parameter, const QString& value)
{
	RUN_TRANSACTION(RepoSetData(repo_id, parameter, value))
}

Transaction* Client::resolve(const QStringList& packageNames, Filters filters)
{
	RUN_TRANSACTION(Resolve(Util::filtersToString(filters), packageNames))
}

Transaction* Client::resolve(const QString& packageName, Filters filters)
{
	return resolve(QStringList() << packageName, filters);
}

Transaction* Client::rollback(Transaction* oldtrans)
{
	RUN_TRANSACTION(Rollback(oldtrans->tid()))
}

Transaction* Client::searchFile(const QString& search, Filters filters)
{
	RUN_TRANSACTION(SearchFile(Util::filtersToString(filters), search))
}

Transaction* Client::searchFile(const QStringList& search, Filters filters)
{
	return searchFile(search.join("&"), filters);
}

Transaction* Client::searchDetails(const QString& search, Filters filters)
{
	RUN_TRANSACTION(SearchDetails(Util::filtersToString(filters), search))
}

Transaction* Client::searchGroup(Client::Group group, Filters filters)
{
	RUN_TRANSACTION(SearchGroup(Util::filtersToString(filters), Util::enumToString<Client>(group, "Group", "Group")))
}

Transaction* Client::searchName(const QString& search, Filters filters)
{
	RUN_TRANSACTION(SearchName(Util::filtersToString(filters), search))
}

Package* Client::searchFromDesktopFile(const QString& path)
{
	QSqlDatabase db = QSqlDatabase::database();
	if (!db.isOpen()) {
		qDebug() << "Desktop files database is not open";
		return NULL;
	}

	QSqlQuery q(db);
	q.prepare("SELECT package FROM cache WHERE filename = :path");
	q.bindValue(":path", path);
	if(!q.exec()) {
		qDebug() << "Error while running query " << q.executedQuery();
		return NULL;
	}

	if (!q.next()) return NULL; // Return NULL if no results

	return new Package(q.value(0).toString());

}

Transaction* Client::simulateInstallFiles(const QStringList& files)
{
	RUN_TRANSACTION(SimulateInstallFiles(files))
}

Transaction* Client::simulateInstallFile(const QString& file)
{
	return simulateInstallFiles(QStringList() << file);
}

Transaction* Client::simulateInstallPackages(const QList<Package*>& packages)
{
	RUN_TRANSACTION(SimulateInstallPackages(Util::packageListToPids(packages)))
}

Transaction* Client::simulateInstallPackage(Package* package)
{
	return simulateInstallPackages(QList<Package*>() << package);
}

Transaction* Client::simulateRemovePackages(const QList<Package*>& packages)
{
	RUN_TRANSACTION(SimulateRemovePackages(Util::packageListToPids(packages)))
}

Transaction* Client::simulateRemovePackage(Package* package)
{
	return simulateRemovePackages(QList<Package*>() << package);
}

Transaction* Client::simulateUpdatePackages(const QList<Package*>& packages)
{
	RUN_TRANSACTION(SimulateUpdatePackages(Util::packageListToPids(packages)))
}

Transaction* Client::simulateUpdatePackage(Package* package)
{
	return simulateUpdatePackages(QList<Package*>() << package);
}

Transaction* Client::updatePackages(bool only_trusted, const QList<Package*>& packages)
{
	RUN_TRANSACTION(UpdatePackages(only_trusted, Util::packageListToPids(packages)))
}

Transaction* Client::updatePackage(bool only_trusted, Package* package)
{
	return updatePackages(only_trusted, QList<Package*>() << package);
}

Transaction* Client::updateSystem(bool only_trusted)
{
	RUN_TRANSACTION(UpdateSystem(only_trusted))
}

Transaction* Client::whatProvides(ProvidesType type, const QString& search, Filters filters)
{
	RUN_TRANSACTION(WhatProvides(Util::filtersToString(filters), Util::enumToString<Client>(type, "ProvidesType", "Provides"), search))
}

Transaction* Client::whatProvides(ProvidesType type, const QStringList& search, Filters filters)
{
	return whatProvides(type, search.join("&"), filters);
}

void Client::setLastError (DaemonError e)
{
	d->error = e;
	emit error (e);
}

void Client::setTransactionError (Transaction* t, DaemonError e)
{
	t->d->error = e;
}

#include "client.moc"

