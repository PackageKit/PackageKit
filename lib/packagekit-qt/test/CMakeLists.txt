project(pktest)

include(FindPkgConfig)
pkg_check_modules(POLKIT polkit-dbus)

include_directories(
	${QT_INCLUDES}
	${POLKIT_INCLUDE_DIRS}
	${packagekit-qt_SOURCE_DIR}
	${CMAKE_BINARY_DIR}/test
)

link_directories(
	${packagekit-qt_BUILD_DIR}/src
)

set( test_SRCS
	daemontest.cpp
	transactiontest.cpp
	main.cpp
)

QT4_AUTOMOC(${test_SRCS})

add_executable(pktest ${test_SRCS})

target_link_libraries(pktest
  ${QT_QTCORE_LIBRARY}
  ${QT_QTDBUS_LIBRARY}
  polkit-dbus
  dbus-1
  cppunit
  packagekit-qt
)

