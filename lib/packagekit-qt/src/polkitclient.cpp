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
#include "config.h"

#include "polkitclient.h"

using namespace PackageKit;

PolkitClient* PolkitClient::m_instance = NULL;
PolkitClient* PolkitClient::instance()
{
	if(!m_instance)
		m_instance = new PolkitClient(qApp);
	return m_instance;
}

PolkitClient::PolkitClient(QObject *parent) : QObject(parent) {
}

#if 0

#ifdef USE_SECURITY_POLKIT
bool PolkitClient::getAuth(const QString &action) {
	DBusError e;
	dbus_error_init(&e);

	if(polkit_check_auth(QCoreApplication::applicationPid(), action.toAscii().data(), NULL))
		return true;

	bool auth = polkit_auth_obtain(action.toAscii().data(), 0, QCoreApplication::applicationPid(), &e);
	if(!auth) {
		qDebug() << "Authentification error :" << e.name << ":" << e.message;
	}

	return auth;
}
#else
bool PolkitClient::getAuth(const QString &action) {
	qDebug() << "Not configured with old PolicyKit support";
	return false;
}
#endif
#endif

#include "polkitclient.moc"

