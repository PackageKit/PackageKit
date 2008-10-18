include ${top_srcdir}/moc.mk

NULL =

INCLUDES = \
	$(QT_CFLAGS)						\
	$(QTCORE_CFLAGS)					\
	$(QTDBUS_CFLAGS)					\
	$(POLKIT_CFLAGS)					\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

METASOURCES = AUTO

lib_LTLIBRARIES =						\
	libpackagekit-qt.la					\
	$(NULL)

libpackagekit_qt_includedir = $(includedir)/PackageKit/packagekit-glib

libpackagekit_qt_include_HEADERS =				\
	QPackageKit						\
	client.h						\
	transaction.h						\
	package.h						\
	$(NULL)

$(libpackagekit_qt_la_OBJECTS) :				\
	client.moc						\
	clientprivate.moc					\
	daemonproxy.moc						\
	package.moc						\
	polkitclient.moc					\
	transaction.moc						\
	transactionprivate.moc					\
	transactionproxy.moc					\
	$(NULL)

libpackagekit_qt_la_SOURCES =					\
	client.cpp						\
	clientprivate.cpp					\
	daemonproxy.cpp						\
	transaction.cpp						\
	transactionprivate.cpp					\
	transactionproxy.cpp					\
	package.cpp						\
	polkitclient.cpp					\
	util.cpp						\
	$(NULL)

libpackagekit_qt_la_LIBADD =					\
	$(QTCORE_LIBS)						\
	$(QTDBUS_LIBS)						\
	$(POLKIT_LIBS)						\
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
	util.h							\
	util.cpp						\
	polkitclient.cpp					\
	client.h						\
	CMakeLists.txt						\
	transactionprivate.h					\
	transactionprivate.cpp					\
	common.h						\
	package.h						\
	clientprivate.h						\
	package.cpp						\
	daemonproxy.cpp						\
	client.cpp						\
	transaction.h						\
	transactionproxy.h					\
	transaction.cpp						\
	transactionproxy.cpp					\
	QPackageKit						\
	daemonproxy.h						\
	polkitclient.h						\
	clientprivate.cpp					\
	$(NULL)

