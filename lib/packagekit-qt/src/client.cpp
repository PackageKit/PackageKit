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

#include <QtSql>

#include "client.h"
#include "clientprivate.h"

#include "common.h"
#include "daemonproxy.h"
#include "transaction.h"
#include "transactionprivate.h"
#include "package.h"
#include "util.h"

#define CREATE_NEW_TRANSACTION                             \
		Transaction* t = new Transaction(QString(), this); \
		if (t->tid().isEmpty()) {                          \
			setLastError(ErrorDaemonUnreachable);          \
			return t;                                      \
		}                                                  \

#define RUN_TRANSACTION(blurb) \
		Q_D(Client);           \
		CREATE_NEW_TRANSACTION \
		t->setHints(d->hints); \
		t->blurb;              \
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

QString Client::getTid() const
{
    Q_D(const Client);
    return d->daemon->GetTid();
}

uint Client::getTimeSinceAction(Enum::Role role) const
{
	Q_D(const Client);
	QString roleName = Util::enumToString<Enum>(role, "Role", "Role");
	return d->daemon->GetTimeSinceAction(roleName);
}

QStringList Client::getTransactionList() const
{
    Q_D(const Client);
    return d->daemon->GetTransactionList();
}

QList<Transaction*> Client::getTransactionObjectList(QObject *parent)
{
    Q_D(Client);
    return d->transactions(getTransactionList(), parent);
}


QList<Transaction*> Client::getTransactions()
{
    return getTransactionObjectList(this);
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
	QDBusPendingReply<> r = d->daemon->SetProxy(http_proxy, NULL, ftp_proxy, NULL, NULL, NULL);
	r.waitForFinished ();
	if (r.isError ()) {
		setLastError (daemonErrorFromDBusReply (r));
		return false;
	} else {
		return true;
	}
}

bool Client::setProxy(const QString& http_proxy, const QString& https_proxy, const QString& ftp_proxy, const QString& socks_proxy, const QString& no_proxy, const QString& pac)
{
	Q_D(Client);
	QDBusPendingReply<> r = d->daemon->SetProxy(http_proxy, https_proxy, ftp_proxy, socks_proxy, no_proxy, pac);
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
	RUN_TRANSACTION(acceptEula(info))
}

Transaction* Client::downloadPackages(const QList<QSharedPointer<Package> > &packages, bool storeInCache)
{
    RUN_TRANSACTION(downloadPackages(packages, storeInCache))
}

Transaction* Client::downloadPackages(QSharedPointer<Package> package, bool storeInCache)
{
    return downloadPackages(QList<QSharedPointer<Package> >() << package, storeInCache);
}

Transaction* Client::getDepends(const QList<QSharedPointer<Package> >& packages, Enum::Filters filters, bool recursive)
{
	RUN_TRANSACTION(getDepends(packages, filters, recursive))
}

Transaction* Client::getDepends(QSharedPointer<Package> package, Enum::Filters filters, bool recursive)
{
	return getDepends(QList<QSharedPointer<Package> >() << package, filters, recursive);
}

Transaction* Client::getDetails(const QList<QSharedPointer<Package> > &packages)
{
    RUN_TRANSACTION(getDetails(packages))
}

Transaction* Client::getDetails(QSharedPointer<Package> package)
{
	return getDetails(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getFiles(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(getFiles(packages))
}

Transaction* Client::getFiles(QSharedPointer<Package> package)
{
	return getFiles(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getOldTransactions(uint number)
{
	RUN_TRANSACTION(getOldTransactions(number))
}

Transaction* Client::getPackages(Enum::Filters filters)
{
	RUN_TRANSACTION(getPackages(filters))
}

Transaction* Client::getRepoList(Enum::Filters filters)
{
	RUN_TRANSACTION(getRepoList(filters))
}

Transaction* Client::getRequires(const QList<QSharedPointer<Package> >& packages, Enum::Filters filters, bool recursive)
{
	RUN_TRANSACTION(getRequires(packages, filters, recursive))
}

Transaction* Client::getRequires(QSharedPointer<Package> package, Enum::Filters filters, bool recursive)
{
	return getRequires(QList<QSharedPointer<Package> >() << package, filters, recursive);
}

Transaction* Client::getUpdateDetail(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(getUpdateDetail(packages))
}

Transaction* Client::getUpdateDetail(QSharedPointer<Package> package)
{
	return getUpdateDetail(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::getUpdates(Enum::Filters filters)
{
	RUN_TRANSACTION(getUpdates(filters))
}

Transaction* Client::getDistroUpgrades()
{
	RUN_TRANSACTION(getDistroUpgrades())
}

Transaction* Client::installFiles(const QStringList& files, bool only_trusted)
{
	RUN_TRANSACTION(installFiles(files, only_trusted))
}

Transaction* Client::installFiles(const QString& file, bool only_trusted)
{
	return installFiles(QStringList() << file, only_trusted);
}

Transaction* Client::installPackages(bool only_trusted, const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(installPackages(only_trusted, packages))
}

Transaction* Client::installPackages(bool only_trusted, QSharedPointer<Package> p)
{
	return installPackages(only_trusted, QList<QSharedPointer<Package> >() << p);
}

Transaction* Client::installSignature(Enum::SigType type, const QString& key_id, QSharedPointer<Package> p)
{
	RUN_TRANSACTION(installSignature(type, key_id, p))
}

Transaction* Client::refreshCache(bool force)
{
	RUN_TRANSACTION(refreshCache(force))
}

Transaction* Client::removePackages(const QList<QSharedPointer<Package> >& packages, bool allow_deps, bool autoremove)
{
	RUN_TRANSACTION(removePackages(packages, allow_deps, autoremove))
}

Transaction* Client::removePackages(QSharedPointer<Package> p, bool allow_deps, bool autoremove)
{
	return removePackages(QList<QSharedPointer<Package> >() << p, allow_deps, autoremove);
}

Transaction* Client::repoEnable(const QString& repo_id, bool enable)
{
	RUN_TRANSACTION(repoEnable(repo_id, enable))
}

Transaction* Client::repoSetData(const QString& repo_id, const QString &parameter, const QString& value)
{
	RUN_TRANSACTION(repoSetData(repo_id, parameter, value))
}

Transaction* Client::resolve(const QStringList& packageNames, Enum::Filters filters)
{
	RUN_TRANSACTION(resolve(packageNames, filters))
}

Transaction* Client::resolve(const QString& packageName, Enum::Filters filters)
{
	return resolve(QStringList() << packageName, filters);
}

Transaction* Client::rollback(Transaction* oldtrans)
{
    qWarning("NOT IMPLEMENTED");
    return 0;
}

Transaction* Client::searchFiles(const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(searchFiles(search, filters))
}

Transaction* Client::searchFiles(const QString& search, Enum::Filters filters)
{
	return searchFiles(QStringList() << search, filters);
}

Transaction* Client::searchDetails(const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(searchDetails(search, filters))
}

Transaction* Client::searchDetails(const QString& search, Enum::Filters filters)
{
	return searchDetails(QStringList() << search, filters);
}

Transaction* Client::searchGroups(Enum::Groups groups, Enum::Filters filters)
{
	RUN_TRANSACTION(searchGroups(groups, filters))
}

Transaction* Client::searchGroups(Enum::Group group, Enum::Filters filters)
{
	return searchGroups(Enum::Groups() << group, filters);
}

Transaction* Client::searchNames(const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(searchNames(search, filters))
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
	RUN_TRANSACTION(simulateInstallFiles(files))
}

Transaction* Client::simulateInstallFiles(const QString& file)
{
	return simulateInstallFiles(QStringList() << file);
}

Transaction* Client::simulateInstallPackages(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(simulateInstallPackages(packages))
}

Transaction* Client::simulateInstallPackages(QSharedPointer<Package> package)
{
	return simulateInstallPackages(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::simulateRemovePackages(const QList<QSharedPointer<Package> >& packages, bool autoremove)
{
	RUN_TRANSACTION(simulateRemovePackages(packages, autoremove))
}

Transaction* Client::simulateRemovePackages(QSharedPointer<Package> package, bool autoremove)
{
	return simulateRemovePackages(QList<QSharedPointer<Package> >() << package, autoremove);
}

Transaction* Client::simulateUpdatePackages(const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(simulateUpdatePackages(packages))
}

Transaction* Client::simulateUpdatePackages(QSharedPointer<Package> package)
{
	return simulateUpdatePackages(QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::updatePackages(bool only_trusted, const QList<QSharedPointer<Package> >& packages)
{
	RUN_TRANSACTION(updatePackages(only_trusted, packages))
}

Transaction* Client::updatePackages(bool only_trusted, QSharedPointer<Package> package)
{
	return updatePackages(only_trusted, QList<QSharedPointer<Package> >() << package);
}

Transaction* Client::updateSystem(bool only_trusted)
{
	RUN_TRANSACTION(updateSystem(only_trusted))
}

Transaction* Client::whatProvides(Enum::Provides type, const QStringList& search, Enum::Filters filters)
{
	RUN_TRANSACTION(whatProvides(type, search, filters))
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

void Client::destroyTransaction(const QString &tid)
{
	Q_D(Client);
	d->destroyTransaction(tid);
}

#include "client.moc"

