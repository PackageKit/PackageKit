#ifndef TRANSACTIONTEST_H
#define TRANSACTIONTEST_H

#include <QObject>
#include <cppunit/TestFixture.h>
#include <cppunit/TestSuite.h>
#include <cppunit/extensions/HelperMacros.h>
#include "QPackageKit"

class TransactionTest : public QObject, public CppUnit::TestFixture
{
	Q_OBJECT

	CPPUNIT_TEST_SUITE(TransactionTest);
	CPPUNIT_TEST(searchName);
	CPPUNIT_TEST(searchDesktop);
	CPPUNIT_TEST(resolveAndInstallAndRemove);
	CPPUNIT_TEST(refreshCache);
	CPPUNIT_TEST(getDistroUpgrades);
	CPPUNIT_TEST(getRepos);
	CPPUNIT_TEST_SUITE_END();

public:
	TransactionTest(QObject* parent = 0);
	~TransactionTest();

	void searchName();
	void searchDesktop();
	void resolveAndInstallAndRemove();
	void refreshCache();
	void getDistroUpgrades();
	void getRepos();

public slots:
	void searchName_cb(QSharedPointer<PackageKit::Package> p);
	void resolveAndInstallAndRemove_cb(QSharedPointer<PackageKit::Package> p);
	void getDistroUpgrades_cb();
	void getRepos_cb(const QString& repoName, const QString& repoDetail, bool enabled);

	void error(PackageKit::Client::DaemonError e);

private:
	bool success;
	QSharedPointer<PackageKit::Package> currentPackage;

};

#endif
