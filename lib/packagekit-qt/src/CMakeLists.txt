project(packagekit-qt)


include(FindPkgConfig)
pkg_check_modules(POLKIT polkit-dbus)

if(POLKIT_FOUND)
	set(USE_SECURITY_POLKIT 1)
else(POLKIT_FOUND)
	set(USE_SECURITY_POLKIT 0)
endif(POLKIT_FOUND)
configure_file(config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

include_directories(
	${QT_INCLUDES}
	${POLKIT_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/src
)

set( lib_SRCS
	client.cpp
	clientprivate.cpp
	daemonproxy.cpp
	transaction.cpp
	transactionprivate.cpp
	transactionproxy.cpp
	package.cpp
	polkitclient.cpp
	util.cpp
)


QT4_AUTOMOC(${lib_SRCS})

add_library(packagekit-qt SHARED ${lib_SRCS})
set_target_properties( packagekit-qt PROPERTIES VERSION 4.2 SOVERSION 0 )

target_link_libraries(packagekit-qt
  ${QT_QTCORE_LIBRARY}
  ${QT_QTDBUS_LIBRARY}
  ${QT_QTSQL_LIBRARY}
  polkit-dbus
  dbus-1
)

install(TARGETS
	packagekit-qt
	DESTINATION
	lib${LIB_SUFFIX}
)

install(FILES
	client.h
	transaction.h
	package.h
	QPackageKit
	DESTINATION
	include/PackageKit/packagekit-qt)

