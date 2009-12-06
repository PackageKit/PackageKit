#include "transactiontest.h"

using namespace PackageKit;

TransactionTest::TransactionTest(QObject* parent) : QObject(parent) 
{
	currentPackage = NULL;
	connect (PackageKit::Client::instance(), SIGNAL(error(PackageKit::Client::DaemonError)), this, SLOT(error(PackageKit::Client::DaemonError)));
}

TransactionTest::~TransactionTest()
{
}

void TransactionTest::searchName()
{
	success = FALSE;
	Transaction* t = PackageKit::Client::instance()->searchName("vim");
	qDebug() << "searchName";
	QEventLoop el;
	connect(t, SIGNAL(package(PackageKit::Package*)), this, SLOT(searchName_cb(PackageKit::Package*)));
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("searchName", success);
}

void TransactionTest::searchDesktop()
{
	success = FALSE;
	Package* p = PackageKit::Client::instance()->searchFromDesktopFile("/usr/share/applications/gnome-terminal.desktop");
	qDebug() << "searchDesktop";
	CPPUNIT_ASSERT_MESSAGE("searchDesktop", p);
}

void TransactionTest::resolveAndInstallAndRemove()
{
	success = FALSE;
	Client* c = Client::instance();
	Transaction* t = c->resolve("glib2");
	qDebug() << "Resolving";
	QEventLoop el;
	connect(t, SIGNAL(package(PackageKit::Package*)), this, SLOT(resolveAndInstallAndRemove_cb(PackageKit::Package*)));
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("resolve", success);

	t = c->installPackage(FALSE, currentPackage);
	CPPUNIT_ASSERT_MESSAGE("installPackages", t != NULL);
	qDebug() << "Installing";
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	el.exec();

	t = c->removePackage(currentPackage, FALSE, FALSE);
	CPPUNIT_ASSERT_MESSAGE("removePackages", t != NULL);
	qDebug() << "Removing";
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	el.exec();

	delete(currentPackage);
}

void TransactionTest::refreshCache()
{
	Transaction* t = PackageKit::Client::instance()->refreshCache(true);
	qDebug() << "Refreshing cache";
	CPPUNIT_ASSERT_MESSAGE("refreshCache", t != NULL);
	QEventLoop el;
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	el.exec();
}

void TransactionTest::getDistroUpgrades()
{
	success = FALSE;
	Transaction* t = PackageKit::Client::instance()->getDistroUpgrades();
	qDebug() << "Getting distro upgrades";
	CPPUNIT_ASSERT_MESSAGE("getDistroUpgrades", t != NULL);
	QEventLoop el;
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	connect(t, SIGNAL(distroUpgrade(PackageKit::Client::DistroUpgradeType, const QString&, const QString&)), this, SLOT(getDistroUpgrades_cb()));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("getDistroUpgrades (not fatal, only means there are no distro upgrades)", success);

}

void TransactionTest::getRepos()
{
	success = FALSE;

	Transaction* t = PackageKit::Client::instance()->getRepoList();
	CPPUNIT_ASSERT_MESSAGE("getRepoList", t != NULL);
	qDebug() << "Getting repos (non filtered)";
	QEventLoop el;
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	connect(t, SIGNAL(repoDetail(const QString&, const QString&, bool)), this, SLOT(getRepos_cb(const QString&, const QString&, bool)));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("getRepoList", success);

	success = FALSE;
	t = PackageKit::Client::instance()->getRepoList(PackageKit::Client::FilterNotDevelopment);
	CPPUNIT_ASSERT_MESSAGE("getRepoList (filtered)", t != NULL);
	qDebug() << "Getting repos (filtered)";
	connect(t, SIGNAL(finished(PackageKit::Transaction::ExitStatus, uint)), &el, SLOT(quit()));
	connect(t, SIGNAL(repoDetail(const QString&, const QString&, bool)), this, SLOT(getRepos_cb(const QString&, const QString&, bool)));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("getRepoList (filtered)", success);
}

void TransactionTest::searchName_cb(Package* p)
{
	delete(p);
	success = TRUE;
}

void TransactionTest::resolveAndInstallAndRemove_cb(Package* p)
{
	currentPackage = p;
	success = TRUE;
}

void TransactionTest::getDistroUpgrades_cb()
{
	success = TRUE;
}

void TransactionTest::getRepos_cb(const QString& repoName, const QString& repoDetail, bool enabled)
{
	qDebug() << "Repository" << repoName << " (" << repoDetail << ") is" << (enabled ? "enabled" : "disabled");
	success = TRUE;
}

void TransactionTest::error (PackageKit::Client::DaemonError e)
{
	qDebug() << "Aieeeeee, daemon error!" << e;
}

CPPUNIT_TEST_SUITE_REGISTRATION(TransactionTest);

#include "transactiontest.moc"

