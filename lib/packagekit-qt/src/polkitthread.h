#ifndef POLKITHREAD_H
#define POLKITHREAD_H

#include <QtCore>

namespace PackageKit {

class PolkitThread : public QThread
{
	Q_OBJECT
public:
	void run();
	bool allowed();	
	bool finished();
	PolkitThread(const QString &action );
private:
	bool _allowed;
	bool _finished;
	QString _action;
};

} // End namespace PackageKit

#endif
