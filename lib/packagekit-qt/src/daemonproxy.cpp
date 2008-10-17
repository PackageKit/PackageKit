#include "daemonproxy.h"

/*
 * Implementation of interface class DaemonProxy
 */

using namespace PackageKit;

DaemonProxy::DaemonProxy(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent)
    : QDBusAbstractInterface(service, path, staticInterfaceName(), connection, parent)
{
}

DaemonProxy::~DaemonProxy()
{
}

#include "daemonproxy.moc"
