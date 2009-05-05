#include "daemontest.h"

DaemonTest::DaemonTest(QObject* parent) : QObject(parent)
{
}

DaemonTest::~DaemonTest()
{
}

void DaemonTest::getActions()
{
	PackageKit::Client::Actions act = PackageKit::Client::instance()->getActions();
	CPPUNIT_ASSERT(act & PackageKit::Client::ActionInstallPackages); // Not really a test, but if *that* fails, then things are going badly :)
}

void DaemonTest::getBackendDetail()
{
	PackageKit::Client::BackendDetail d = PackageKit::Client::instance()->getBackendDetail();
	CPPUNIT_ASSERT(!d.name.isNull());
}

void DaemonTest::getFilters()
{
	PackageKit::Client::Filters f = PackageKit::Client::instance()->getFilters();
	CPPUNIT_ASSERT(f & PackageKit::Client::FilterInstalled); // Not really a test, but if *that* fails, then things are going badly :)
}

void DaemonTest::getGroups()
{
	PackageKit::Client::Groups g = PackageKit::Client::instance()->getGroups();
	CPPUNIT_ASSERT(g.size() != 1);
}

void DaemonTest::getTimeSinceAction()
{
	PackageKit::Client::instance()->getTimeSinceAction(PackageKit::Client::ActionInstallPackages);
}

CPPUNIT_TEST_SUITE_REGISTRATION(DaemonTest);

#include "daemontest.moc"

