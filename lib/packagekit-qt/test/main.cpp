#include <QApplication>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h> 

#include "QPackageKit"

int main(int argc, char **argv)
{
	// Check that we are using the dummy backend for testing (I'm sometimes absent minded)
	PackageKit::Client::BackendDetail d = PackageKit::Client::instance()->getBackendDetail();
	if(d.name != "dummy") {
		qFatal("Please use the dummy backend for testing");
	}

	CppUnit::TextUi::TestRunner runner;
	CppUnit::TestFactoryRegistry &registry = CppUnit::TestFactoryRegistry::getRegistry();
	runner.addTest(registry.makeTest());
	QCoreApplication app(argc, argv);
	runner.run();
	return 0;
}
