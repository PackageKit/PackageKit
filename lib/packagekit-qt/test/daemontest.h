#ifndef DAEMONTEST_H
#define DAEMONTEST_H

#include <QObject>
#include <cppunit/TestFixture.h>
#include <cppunit/TestSuite.h>
#include <cppunit/extensions/HelperMacros.h>
#include "QPackageKit"

class DaemonTest : public QObject, public CppUnit::TestFixture
{
	Q_OBJECT

	CPPUNIT_TEST_SUITE(DaemonTest);
	CPPUNIT_TEST(getBackendDetail);
	CPPUNIT_TEST(getActions);
	CPPUNIT_TEST(getFilters);
	CPPUNIT_TEST(getGroups);
	CPPUNIT_TEST(getTimeSinceAction);
	CPPUNIT_TEST_SUITE_END();

public:
	DaemonTest(QObject* parent = 0);
	~DaemonTest();

	void getActions();
	void getBackendDetail();
	void getFilters();
	void getGroups();
	void getTimeSinceAction();
};

#endif
