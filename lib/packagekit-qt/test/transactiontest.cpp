#include "transactiontest.h"

using namespace PackageKit;

TransactionTest::TransactionTest(QObject* parent) : QObject(parent) 
{
	currentPackage = QSharedPointer<Package> (NULL);
	connect (PackageKit::Client::instance(), SIGNAL(error(PackageKit::Client::DaemonError)), this, SLOT(error(PackageKit::Client::DaemonError)));
}

TransactionTest::~TransactionTest()
{
}

void TransactionTest::searchName()
{
	success = FALSE;
	Transaction* t = PackageKit::Client::instance()->searchNames("vim");
	qDebug() << "searchName";
	QEventLoop el;
	connect(t, SIGNAL(package(QSharedPointer<PackageKit::Package>)), this, SLOT(searchName_cb(QSharedPointer<PackageKit::Package>)));
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("searchName", success);
}

void TransactionTest::searchDesktop()
{
	success = FALSE;
	QSharedPointer<Package> p = PackageKit::Client::instance()->searchFromDesktopFile("/usr/share/applications/gnome-terminal.desktop");
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
	connect(t, SIGNAL(package(QSharedPointer<PackageKit::Package>)), this, SLOT(resolveAndInstallAndRemove_cb(QSharedPointer<PackageKit::Package>)));
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("resolve", success);

	t = c->installPackages(FALSE, currentPackage);
	CPPUNIT_ASSERT_MESSAGE("installPackages", t != NULL);
	qDebug() << "Installing";
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	el.exec();

	t = c->removePackages(currentPackage, FALSE, FALSE);
	CPPUNIT_ASSERT_MESSAGE("removePackages", t != NULL);
	qDebug() << "Removing";
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	el.exec();
}

void TransactionTest::refreshCache()
{
	Transaction* t = PackageKit::Client::instance()->refreshCache(true);
	qDebug() << "Refreshing cache";
	CPPUNIT_ASSERT_MESSAGE("refreshCache", t != NULL);
	QEventLoop el;
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	el.exec();
}

void TransactionTest::getDistroUpgrades()
{
	success = FALSE;
	Transaction* t = PackageKit::Client::instance()->getDistroUpgrades();
	qDebug() << "Getting distro upgrades";
	CPPUNIT_ASSERT_MESSAGE("getDistroUpgrades", t != NULL);
	QEventLoop el;
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	connect(t, SIGNAL(distroUpgrade(PackageKit::Enum::DistroUpgrade, const QString&, const QString&)), this, SLOT(getDistroUpgrades_cb()));
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
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	connect(t, SIGNAL(repoDetail(const QString&, const QString&, bool)), this, SLOT(getRepos_cb(const QString&, const QString&, bool)));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("getRepoList", success);

	success = FALSE;
	t = PackageKit::Client::instance()->getRepoList(PackageKit::Enum::FilterNotDevelopment);
	CPPUNIT_ASSERT_MESSAGE("getRepoList (filtered)", t != NULL);
	qDebug() << "Getting repos (filtered)";
	connect(t, SIGNAL(finished(PackageKit::Enum::Exit, uint)), &el, SLOT(quit()));
	connect(t, SIGNAL(repoDetail(const QString&, const QString&, bool)), this, SLOT(getRepos_cb(const QString&, const QString&, bool)));
	el.exec();
	CPPUNIT_ASSERT_MESSAGE("getRepoList (filtered)", success);
}

void TransactionTest::searchName_cb(QSharedPointer<Package> p)
{
	qDebug() << "Emitted package: " << p->name ();
	success = TRUE;
}

void TransactionTest::resolveAndInstallAndRemove_cb(QSharedPointer<Package> p)
{
	qDebug () << "Emitted package: " << p->name ();
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

