include ${top_srcdir}/moc.mk

.NOTPARALLEL:

NULL =

INCLUDES = \
	$(QTCORE_CFLAGS)					\
	$(QTDBUS_CFLAGS)					\
	$(QTSQL_CFLAGS)						\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

METASOURCES = AUTO

MOCFILES =								\
	client.moc							\
	clientprivate.moc					\
	daemonproxy.moc						\
	transactionproxy.moc				\
	transaction.moc						\
	transactionprivate.moc				\
	enum.moc							\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit-qt.la					\
	$(NULL)

libpackagekit_qt_includedir = $(includedir)/PackageKit/packagekit-qt

libpackagekit_qt_include_HEADERS =				\
	QPackageKit						\
	client.h						\
	transaction.h						\
	package.h						\
	bitfield.h						\
	enum.h							\
	$(NULL)

libpackagekit_qt_la_SOURCES =					\
	QPackageKit						\
	common.h						\
	client.h						\
	client.cpp						\
	clientprivate.h						\
	clientprivate.cpp					\
	daemonproxy.h						\
	daemonproxy.cpp						\
	transaction.h						\
	transaction.cpp						\
	transactionprivate.h					\
	transactionprivate.cpp					\
	transactionproxy.h					\
	transactionproxy.cpp					\
	package.h						\
	package.cpp						\
	util.h							\
	util.cpp						\
	bitfield.h						\
	bitfield.cpp						\
	enum.h							\
	enum.cpp						\
	dbus_proxies.stamp					\
	$(MOCFILES)						\
	$(NULL)

libpackagekit_qt_la_LIBADD =					\
	$(QTCORE_LIBS)						\
	$(QTDBUS_LIBS)						\
	$(QTSQL_LIBS)						\
	$(NULL)

libpackagekit_qt_la_CFLAGS =					\
	$(WARNINGFLAGS_CPP)					\
	$(NULL)

libpackagekit_qt_la_LDFLAGS =					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
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
	$(NULL)

MAINTAINERCLEANFILES =						\
	dbus_proxies.stamp						\
	transactionproxy.h						\
	transactionproxy.cpp					\
	daemonproxy.h							\
	daemonproxy.cpp							\
	$(NULL)

BUILT_SOURCES=								\
	dbus_proxies.stamp						\
	$(MOCFILES)								\
	$(NULL)

dbus_proxies.stamp: ${top_srcdir}/src/org.freedesktop.PackageKit.xml ${top_srcdir}/src/org.freedesktop.PackageKit.Transaction.xml
	qdbusxml2cpp -c DaemonProxy -p daemonproxy -m -N ${top_srcdir}/src/org.freedesktop.PackageKit.xml
	qdbusxml2cpp -c TransactionProxy -p transactionproxy -m -N ${top_srcdir}/src/org.freedesktop.PackageKit.Transaction.xml
	touch $@


-include $(top_srcdir)/git.mk
