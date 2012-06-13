include ${top_srcdir}/moc.mk

SUBDIRS = modules

.NOTPARALLEL:

NULL =

INCLUDES = \
	$(QTCORE_CFLAGS)					\
	$(QTDBUS_CFLAGS)					\
	$(QTSQL_CFLAGS)						\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

METASOURCES = AUTO

MOCFILES =							\
	signature.moc						\
	daemon.moc						\
	daemonproxy.moc						\
	transactionproxy.moc					\
	transaction.moc						\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit-qt2.la					\
	$(NULL)

libpackagekit_qt2_includedir = $(includedir)/PackageKit/packagekit-qt2

libpackagekit_qt2_include_HEADERS =				\
	Signature						\
	signature.h						\
	Eula							\
	eula.h							\
	Transaction						\
	Daemon							\
	daemon.h						\
	transaction.h						\
	Package							\
	package.h						\
	PackageDetails						\
	packagedetails.h					\
	PackageUpdateDetails					\
	packageupdatedetails.h					\
	$(NULL)

libpackagekit_qt2_la_SOURCES =					\
	signature.h						\
	signature.cpp						\
	eula.h							\
	eula.cpp						\
	package.h						\
	package.cpp						\
	packagedetails.h					\
	packagedetails.cpp					\
	packageupdatedetails.h					\
	packageupdatedetails.cpp				\
	transactionproxy.h					\
	transactionproxy.cpp					\
	transactionprivate.h					\
	transactionprivate.cpp					\
	transaction.h						\
	transaction.cpp						\
	daemonprivate.h						\
	daemonprivate.cpp					\
	daemonproxy.h						\
	daemonproxy.cpp						\
	daemon.h						\
	daemon.cpp						\
	common.h						\
	dbus_proxies.stamp					\
	$(NULL)

libpackagekit_qt2_la_LIBADD =					\
	$(QTCORE_LIBS)						\
	$(QTDBUS_LIBS)						\
	$(QTSQL_LIBS)						\
	$(NULL)

libpackagekit_qt2_la_CFLAGS =					\
	$(WARNINGFLAGS_CPP)					\
	$(NULL)

libpackagekit_qt2_la_LDFLAGS =					\
	-version-info $(LT_QT_CURRENT):$(LT_QT_REVISION):$(LT_QT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)

EXTRA_DIST =							\
	dbus_proxies.stamp					\
	daemonproxy.h						\
	daemonproxy.cpp						\
	transactionproxy.h					\
	transactionproxy.cpp					\
	packagekit-qt2.pc.in					\
	Doxyfile
	$(NULL)

MAINTAINERCLEANFILES =						\
	dbus_proxies.stamp					\
	transactionproxy.h					\
	transactionproxy.cpp					\
	daemonproxy.h						\
	daemonproxy.cpp						\
	$(NULL)

BUILT_SOURCES=							\
	dbus_proxies.stamp					\
	$(MOCFILES)						\
	$(NULL)

dbus_proxies.stamp: ${top_srcdir}/src/org.freedesktop.PackageKit.xml ${top_srcdir}/src/org.freedesktop.PackageKit.Transaction.xml
	qdbusxml2cpp -c DaemonProxy -p daemonproxy -m -N ${top_srcdir}/src/org.freedesktop.PackageKit.xml
	qdbusxml2cpp -c TransactionProxy -p transactionproxy -m -N ${top_srcdir}/src/org.freedesktop.PackageKit.Transaction.xml
	touch $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 					\
	packagekit-qt2.pc				\
	$(NULL)

-include $(top_srcdir)/git.mk
