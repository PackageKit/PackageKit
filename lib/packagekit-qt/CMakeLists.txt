cmake_minimum_required(VERSION 2.4.8)

project(packagekit-qt)

set (LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )

find_package(Qt4 REQUIRED)
add_definitions(${QT_DEFINITIONS})

add_subdirectory(src)
add_subdirectory(modules)

if(BUILD_TESTS)
	add_subdirectory(test)
endif(BUILD_TESTS)
