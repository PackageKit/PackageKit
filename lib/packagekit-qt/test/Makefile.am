include ${top_srcdir}/moc.mk

NULL =

INCLUDES =						\
	$(QTCORE_CFLAGS)				\
	$(QTGUI_CFLAGS)					\
	$(QTDBUS_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	$(CPPUNIT_CFLAGS)				\
	-I$(top_srcdir)/lib/packagekit-qt/src		\
	$(NULL)

METASOURCES = AUTO

if EGG_BUILD_TESTS

PK_QT_LIBS =						\
	$(top_builddir)/lib/packagekit-qt/src/libpackagekit-qt.la	\
	$(NULL)

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	daemontest.h					\
	daemontest.cpp					\
	transactiontest.h				\
	transactiontest.cpp				\
	main.cpp					\
	$(NULL)

$(pk_self_test_OBJECTS) :				\
	transactiontest.moc				\
	daemontest.moc					\
	$(NULL)

pk_self_test_LDADD =					\
	$(QTCORE_LIBS)					\
	$(QTGUI_LIBS)					\
	$(QTDBUS_LIBS)					\
	$(POLKIT_LIBS)					\
	$(CPPUNIT_LIBS)					\
	$(PK_QT_LIBS)					\
	$(NULL)

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = pk-self-test
endif

clean-local:
	rm -f *~
	rm -f *.out

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)


-include $(top_srcdir)/git.mk
