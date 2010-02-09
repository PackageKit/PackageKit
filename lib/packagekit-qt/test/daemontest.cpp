#include "daemontest.h"

DaemonTest::DaemonTest(QObject* parent) : QObject(parent)
{
}

DaemonTest::~DaemonTest()
{
}

void DaemonTest::getActions()
{
	PackageKit::Enum::Roles act = PackageKit::Client::instance()->actions();
	CPPUNIT_ASSERT(act & PackageKit::Enum::RoleInstallPackages); // Not really a test, but if *that* fails, then things are going badly :)
}

void DaemonTest::getBackendDetail()
{
	QString backendName = PackageKit::Client::instance()->backendName();
	CPPUNIT_ASSERT(!backendName.isNull());
}

void DaemonTest::getFilters()
{
	PackageKit::Enum::Filters f = PackageKit::Client::instance()->filters();
	CPPUNIT_ASSERT(f & PackageKit::Enum::FilterInstalled); // Not really a test, but if *that* fails, then things are going badly :)
}

void DaemonTest::getGroups()
{
	PackageKit::Enum::Groups g = PackageKit::Client::instance()->groups();
	CPPUNIT_ASSERT(g.size() != 1);
}

void DaemonTest::getTimeSinceAction()
{
	PackageKit::Client::instance()->getTimeSinceAction(PackageKit::Enum::RoleInstallPackages);
}

CPPUNIT_TEST_SUITE_REGISTRATION(DaemonTest);

#include "daemontest.moc"

