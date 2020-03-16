cmake_minimum_required (VERSION 3.6)
project(CommonLibs)

if(COMMONLIBS_TRACE)
	add_definitions(-DCOMMONLIBS_TRACE=1)
endif()

# External resources/repositories are downloaded here
add_subdirectory(external)

# Testing
enable_testing()
add_subdirectory(test)

# Source code
add_subdirectory(source)

