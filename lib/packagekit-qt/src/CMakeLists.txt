cmake_minimum_required(VERSION 2.4.8)

project(packagekit-qt)

if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
	SET(LIB_SUFFIX "64")
ELSE(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
	SET(LIB_SUFFIX "")
ENDIF(${CMAKE_SIZEOF_VOID_P} EQUAL 8)

find_package(Qt4 REQUIRED)
add_definitions(${QT_DEFINITIONS})

include(FindPkgConfig)
pkg_check_modules(POLKIT polkit-dbus)

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
set_target_properties( packagekit-qt PROPERTIES VERSION 2.0 SOVERSION 2 )

target_link_libraries(packagekit-qt
  ${QT_QTCORE_LIBRARY}
  ${QT_QTDBUS_LIBRARY}
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
	include/packagekit-qt)

