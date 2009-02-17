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

#include "client.h"
#include "clientprivate.h"

#include "common.h"
#include "daemonproxy.h"
#include "polkitclient.h"
#include "transaction.h"
#include "transactionprivate.h"
#include "transactionproxy.h"
#include "util.h"

using namespace PackageKit;

Client* Client::m_instance = 0;

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

	connect(d->daemon, SIGNAL(Locked(bool)), this, SIGNAL(locked(bool)));
	connect(d->daemon, SIGNAL(NetworkStateChanged(const QString&)), d, SLOT(networkStateChanged(const QString&)));
	connect(d->daemon, SIGNAL(RepoListChanged()), this, SIGNAL(repoListChanged()));
	connect(d->daemon, SIGNAL(RestartSchedule()), this, SIGNAL(restartScheduled()));
	connect(d->daemon, SIGNAL(TransactionListChanged(const QStringList&)), d, SLOT(transactionListChanged(const QStringList&)));
}

Client::~Client()
{
}

Client::Actions Client::getActions()
{
	QStringList actions = d->daemon->GetActions().value().split(";");
	Actions flags;
	int value;
	foreach(const QString& action, actions) {
		value = Util::enumFromString<Client>(action, "Action", "Action");
		flags.insert((Action)value);
	}
	return flags;
}

Client::BackendDetail Client::getBackendDetail()
{
	BackendDetail detail;
	QString name, author;
	name = d->daemon->GetBackendDetail(author);
	detail.name = name;
	detail.author = author;
	return detail;
}

Client::Filters Client::getFilters()
{
	QStringList filters = d->daemon->GetFilters().value().split(";");

	// Adapt a slight difference in the enum
	if(filters.contains("none")) {
		filters[filters.indexOf("none")] = "no-filter";
	}

	Filters flags;
	int value;
	foreach(const QString& filter, filters) {
		value = Util::enumFromString<Client>(filter, "Filter", "Filter");
		flags.insert((Filter)value);
	}
	return flags;
}

Client::Groups Client::getGroups()
{
	QStringList groups = d->daemon->GetGroups().value().split(";");

	Groups flags;
	int value;
	foreach(const QString& group, groups) {
		value = Util::enumFromString<Client>(group, "Group");
		flags.insert((Group)value);
	}
	return flags;
}

QStringList Client::getMimeTypes()
{
	return d->daemon->GetMimeTypes().value().split(";");
}

Client::NetworkState Client::getNetworkState()
{
	QString state = d->daemon->GetNetworkState();
	int value = Util::enumFromString<Client>(state, "NetworkState");
	return (NetworkState)value;
}

uint Client::getTimeSinceAction(Action action)
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

void Client::setProxy(const QString& http_proxy, const QString& ftp_proxy)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_NETWORK_PROXY_CONFIGURE)) {
		emit authError(AUTH_SYSTEM_NETWORK_PROXY_CONFIGURE);
		return;
	}

	d->daemon->SetProxy(http_proxy, ftp_proxy);
}

void Client::stateHasChanged(const QString& reason)
{
	d->daemon->StateHasChanged(reason);
}

void Client::suggestDaemonQuit()
{
	d->daemon->SuggestDaemonQuit();
}

Client::DaemonError Client::getLastError ()
{
	return d->lastError;
}

////// Transaction functions

Transaction* Client::acceptEula(EulaInfo info)
{
	if(!PolkitClient::instance()->getAuth(AUTH_PACKAGE_EULA_ACCEPT)) {
		emit authError(AUTH_PACKAGE_EULA_ACCEPT);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->AcceptEula(info.id);

	return t;
}

Transaction* Client::downloadPackages(const QList<Package*>& packages)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->DownloadPackages(Util::packageListToPids(packages));

	return t;
}

Transaction* Client::downloadPackage(Package* package)
{
	return downloadPackages(QList<Package*>() << package);
}

Transaction* Client::getDepends(const QList<Package*>& packages, Filters filters, bool recursive)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}


	t->d->p->GetDepends(Util::filtersToString(filters), Util::packageListToPids(packages), recursive);

	return t;
}

Transaction* Client::getDepends(Package* package, Filters filters, bool recursive)
{
	return getDepends(QList<Package*>() << package, filters, recursive);
}

Transaction* Client::getDepends(const QList<Package*>& packages, Filter filter, bool recursive)
{
	return getDepends(packages, Filters() << filter, recursive);
}

Transaction* Client::getDepends(Package* package, Filter filter, bool recursive)
{
	return getDepends(QList<Package*>() << package, filter, recursive);
}

Transaction* Client::getDetails(const QList<Package*>& packages)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	foreach(Package* p, packages) {
		t->d->packageMap.insert(p->id(), p);
	}

	t->d->p->GetDetails(Util::packageListToPids(packages));

	return t;
}

Transaction* Client::getDetails(Package* package)
{
	return getDetails(QList<Package*>() << package);
}

Transaction* Client::getFiles(const QList<Package*>& packages)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetFiles(Util::packageListToPids(packages));

	return t;
}

Transaction* Client::getFiles(Package* package)
{
	return getFiles(QList<Package*>() << package);
}

Transaction* Client::getOldTransactions(uint number)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetOldTransactions(number);

	return t;
}

Transaction* Client::getPackages(Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetPackages(Util::filtersToString(filters));

	return t;
}

Transaction* Client::getPackages(Filter filter)
{
	return getPackages(Filters() << filter);
}

Transaction* Client::getRepoList(Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetRepoList(Util::filtersToString(filters));

	return t;
}

Transaction* Client::getRepoList(Filter filter)
{
	return getRepoList(Filters() << filter);
}

Transaction* Client::getRequires(const QList<Package*>& packages, Filters filters, bool recursive)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetRequires(Util::filtersToString(filters), Util::packageListToPids(packages), recursive);

	return t;
}

Transaction* Client::getRequires(Package* package, Filters filters, bool recursive)
{
	return getRequires(QList<Package*>() << package, filters, recursive);
}

Transaction* Client::getRequires(const QList<Package*>& packages, Filter filter, bool recursive)
{
	return getRequires(packages, Filters() << filter, recursive);
}

Transaction* Client::getRequires(Package* package, Filter filter, bool recursive)
{
	return getRequires(QList<Package*>() << package, filter, recursive);
}

Transaction* Client::getUpdateDetail(const QList<Package*>& packages)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetUpdateDetail(Util::packageListToPids(packages));

	return t;
}

Transaction* Client::getUpdateDetail(Package* package)
{
	return getUpdateDetail(QList<Package*>() << package);
}

Transaction* Client::getUpdates(Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetUpdates(Util::filtersToString(filters));

	return t;
}

Transaction* Client::getUpdates(Filter filter)
{
	return getUpdates(Filters() << filter);
}

Transaction* Client::getDistroUpgrades()
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->GetDistroUpgrades();

	return t;
}

Transaction* Client::installFiles(const QStringList& files, bool trusted)
{
	QString polkitAction = trusted ? AUTH_PACKAGE_INSTALL : AUTH_PACKAGE_INSTALL_UNTRUSTED;
	if(!PolkitClient::instance()->getAuth(polkitAction)) {
		emit authError(polkitAction);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->InstallFiles(trusted, files);

	return t;
}

Transaction* Client::installFile(const QString& file, bool trusted)
{
	return installFiles(QStringList() << file, trusted);
}

Transaction* Client::installPackages(const QList<Package*>& packages)
{
	if(!PolkitClient::instance()->getAuth(AUTH_PACKAGE_INSTALL)) {
		emit authError(AUTH_PACKAGE_INSTALL);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->InstallPackages(Util::packageListToPids(packages));

	return t;
}

Transaction* Client::installPackage(Package* p)
{
	return installPackages(QList<Package*>() << p);
}

Transaction* Client::installSignature(SignatureType type, const QString& key_id, Package* p)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_TRUST_SIGNING_KEY)) {
		emit authError(AUTH_SYSTEM_TRUST_SIGNING_KEY);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->InstallSignature(Util::enumToString<Client>(type, "SignatureType"), key_id, p->id());

	return t;
}

Transaction* Client::refreshCache(bool force)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_SOURCES_REFRESH)) {
		emit authError(AUTH_SYSTEM_SOURCES_REFRESH);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->RefreshCache(force);

	return t;
}

Transaction* Client::removePackages(const QList<Package*>& packages, bool allow_deps, bool autoremove)
{
	if(!PolkitClient::instance()->getAuth(AUTH_PACKAGE_REMOVE)) {
		emit authError(AUTH_PACKAGE_REMOVE);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->RemovePackages(Util::packageListToPids(packages), allow_deps, autoremove);

	return t;
}

Transaction* Client::removePackage(Package* p, bool allow_deps, bool autoremove)
{
	return removePackages(QList<Package*>() << p, allow_deps, autoremove);
}

Transaction* Client::repoEnable(const QString& repo_id, bool enable)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_SOURCES_CONFIGURE)) {
		emit authError(AUTH_SYSTEM_SOURCES_CONFIGURE);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->RepoEnable(repo_id, enable);

	return t;
}

Transaction* Client::repoSetData(const QString& repo_id, const QString& parameter, const QString& value)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_SOURCES_CONFIGURE)) {
		emit authError(AUTH_SYSTEM_SOURCES_CONFIGURE);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->RepoSetData(repo_id, parameter, value);

	return t;
}

Transaction* Client::resolve(const QStringList& packageNames, Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->Resolve(Util::filtersToString(filters), packageNames);

	return t;
}

Transaction* Client::resolve(const QStringList& packageNames, Filter filter)
{
	return resolve(packageNames, Filters() << filter);
}

Transaction* Client::resolve(const QString& packageName, Filters filters)
{
	return resolve(QStringList() << packageName, filters);
}

Transaction* Client::resolve(const QString& packageName, Filter filter)
{
	return resolve(packageName, Filters() << filter);
}

Transaction* Client::rollback(Transaction* oldtrans)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_ROLLBACK)) {
		emit authError(AUTH_SYSTEM_ROLLBACK);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->Rollback(oldtrans->tid());

	return t;
}

Transaction* Client::searchFile(const QString& search, Filters filters)
{
        Transaction* t = d->createNewTransaction();
		if (!t) {
			emit daemonError(DaemonUnreachable);
			return NULL;
		}

        t->d->p->SearchFile(Util::filtersToString(filters), search);

        return t;
}

Transaction* Client::searchFile(const QString& search, Filter filter)
{
        return Client::searchFile(search, Filters() << filter);
}

Transaction* Client::searchDetails(const QString& search, Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->SearchDetails(Util::filtersToString(filters), search);

	return t;
}

Transaction* Client::searchDetails(const QString& search, Filter filter)
{
        return Client::searchDetails(search, Filters() << filter);
}

Transaction* Client::searchGroup(Client::Group group, Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->SearchGroup(Util::filtersToString(filters), Util::enumToString<Client>(group, "Group"));

	return t;
}

Transaction* Client::searchGroup(Client::Group group, Filter filter)
{
	return searchGroup(group, Filters() << filter);
}

Transaction* Client::searchName(const QString& search, Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->SearchName(Util::filtersToString(filters), search);

	return t;
}

Transaction* Client::searchName(const QString& search, Filter filter)
{
	return Client::searchName(search, Filters() << filter);
}

Transaction* Client::updatePackages(const QList<Package*>& packages)
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_UPDATE)) {
		emit authError(AUTH_SYSTEM_UPDATE);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->UpdatePackages(Util::packageListToPids(packages));

	return t;
}

Transaction* Client::updatePackage(Package* package)
{
	return updatePackages(QList<Package*>() << package);
}

Transaction* Client::updateSystem()
{
	if(!PolkitClient::instance()->getAuth(AUTH_SYSTEM_UPDATE)) {
		emit authError(AUTH_SYSTEM_UPDATE);
		return NULL;
	}

	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		d->lastError = DaemonUnreachable;
		return NULL;
	}

	t->d->p->UpdateSystem();

	return t;
}

Transaction* Client::whatProvides(ProvidesType type, const QString& search, Filters filters)
{
	Transaction* t = d->createNewTransaction();
	if (!t) {
		emit daemonError(DaemonUnreachable);
		return NULL;
	}

	t->d->p->WhatProvides(Util::filtersToString(filters), Util::enumToString<Client>(type, "ProvidesType", "Provides"), search);

	return t;
}

Transaction* Client::whatProvides(ProvidesType type, const QString& search, Filter filter)
{
	return whatProvides(type, search, Filters() << filter);
}

#include "client.moc"

