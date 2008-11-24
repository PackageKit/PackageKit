#ifdef USE_SECURITY_POLKIT
#include <polkit-dbus/polkit-dbus.h>
#endif

#include "polkitthread.h"

using namespace PackageKit;

PolkitThread::PolkitThread(const QString &action)
: _allowed( false )
, _finished( false )
{
	_action = action;
}


bool PolkitThread::allowed()
{
	return _allowed;
}

bool PolkitThread::finished()
{
	return _finished;
}


#ifdef USE_SECURITY_POLKIT
void PolkitThread::run()
{
	DBusError e;
	dbus_error_init(&e);

	if(polkit_check_auth(QCoreApplication::applicationPid(), _action.toAscii().data(), NULL))
	{
		_allowed = true;
		_finished = true;
		exit();
		return;
	}

	_allowed = polkit_auth_obtain(_action.toAscii().data(), 0, QCoreApplication::applicationPid(), &e);
	if(!_allowed) {
		qDebug() << "Authentification error :" << e.name << ":" << e.message;
	}
	_finished = true;
	exit();
}
#else
void PolkitThread::run()
{
	_allowed = true;
	_finished = true;
	exit();
}
#endif

#include "polkitthread.moc"
