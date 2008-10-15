cmake_minimum_required(VERSION 2.4.8)

project(packagekit-qt)

if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
	SET(LIB_SUFFIX "64")
ELSE(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
	SET(LIB_SUFFIX "")
ENDIF(${CMAKE_SIZEOF_VOID_P} EQUAL 8)

add_subdirectory(src)
if(BUILD_TESTS)
	add_subdirectory(test)
endif(BUILD_TESTS)
add_subdirectory(modules)
