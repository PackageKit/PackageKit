#include "transactionproxy.h"

/*
 * Implementation of interface class TransactionProxy
 */

using namespace PackageKit;

TransactionProxy::TransactionProxy(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent)
    : QDBusAbstractInterface(service, path, staticInterfaceName(), connection, parent)
{
}

TransactionProxy::~TransactionProxy()
{
}


#include "transactionproxy.moc"
