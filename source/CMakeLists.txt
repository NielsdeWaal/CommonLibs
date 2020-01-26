# Adds convenience methods, see cmake/cleanCppExtensions.cmake
include(cleanCppExtensions)

# Creates options to turn on sanitizers, see cmake/sanitizers.cmake
include(sanitizers)

# Enable helpfull warnings and C++14 for all files
if(MSVC)
    add_compile_options(/std:c++14 /W3 /WX )
else()
	add_compile_options(-std=c++17 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wunused -Wpedantic)
endif()

# configure version.cpp.in with selected version
configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/version.cpp"
  )
# If you want to have date and time of the build, your targets has to depend on this.
# This will force recompilation of version.o and thus forcing gcc to update __DATE__ macro.
add_custom_target(versionFileTouchForRebuild
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")


#------------------------------------------------------------------------------
# Libraries dependencies
find_package(Threads REQUIRED) #threading
find_library(libUring REQUIRED) #liburing

#------------------------------------------------------------------------------
# By using macro to add common dependencies you can avoid repetition when you have
# multiple binaries.
# Use with `addCommonDependencies(CommonLibs)`
# macro(addCommonDependencies TARGET)
#     add_dependencies(${TARGET} versionFileTouchForRebuild) # We want precise time of build in version
#     target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
#     target_link_libraries (${TARGET} PRIVATE ${CMAKE_THREAD_LIBS_INIT})
#     target_compile_definitions(${TARGET} PRIVATE -DSOME_DEFINE)
# endmacro()

#-------------------------------------------------------------------------------
# CommonLibs executable setup

add_executable(CommonLibs
	${CMAKE_CURRENT_BINARY_DIR}/version.cpp
	EventLoop/EventLoop.cpp
	EventLoop/EventLoop.h
	Common/StreamSocket.h
	Common/TLSSocket.h
	Common/UDPSocket.h
	Common/Websocket.h
	MQTT/MQTTPacket.h
	MQTT/MQTTClient.h
	MQTT/MQTTBroker.h
	Statwriter/StatWriter.h
	Statwriter/StatWriter.cpp
	Example.h
	main.cpp)
add_dependencies(CommonLibs versionFileTouchForRebuild) # We want precise time of build in version
target_include_directories(CommonLibs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} EventLoop Common)
target_link_libraries(CommonLibs PRIVATE Threads::Threads)
target_link_libraries(CommonLibs PRIVATE spdlog)
target_link_libraries(CommonLibs PRIVATE cpptoml)
target_link_libraries(CommonLibs PRIVATE json)
target_link_libraries(CommonLibs PRIVATE uring)

install(TARGETS CommonLibs
    BUNDLE DESTINATION "."
    RUNTIME DESTINATION bin)

# Using macro from cmake/cleanCpppExtensions.cpp included on the top
# Provides CommonLibs-run and CommonLibs-dbg
addRunAndDebugTargets(CommonLibs)

# short convenience target from cmake/cleanCpppExtensions.cpp
add_custom_target(run DEPENDS CommonLibs-run)

# Manually specified convenience target for running the binary, we use macro
# add_custom_target(CommonLibs-run
#     WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}/bin
#     USES_TERMINAL
#     COMMAND ./CommonLibs
#     DEPENDS Install_)

# And this would add -DBAR_ENABLE_FEATURE or equivalent to build command
# target_compile_definitions(CommonLibs PRIVATE BAR_ENABLE_FEATURE)

# Add compile time definition based on option
# option(OPTION_NAME "description for end user" OFF)
# if(OPTION_NAME)
#     # you can do anything here, eg. setup new optional executable
#     target_compile_definitions(CommonLibs PRIVATE DEFINE_NAME)
# endif()

# Add building new shared library
# add_library(exampleLibrary SHARED ${LIBRARY_SOURCES})
# target_link_libraries (exampleLibrary ${CMAKE_THREAD_LIBS_INIT})
# set_property(TARGET exampleLibrary PROPERTY CXX_STANDARD 14) # we want C++14
#set(EVENTLOOP_SOURCES ./EventLoop/*.cpp ./EventLoop/*.h)
#add_library(EventLoop SHARED ${EVENTLOOP_SOURCES})
#target_link_libraries(EventLoop ${CMAKE_THREAD_LIBS_INIT})
#set_property(TARGET EventLoop PROPERTY CXX_STANDARD 14) # we want C++14
#
#set(COMMON_SOURCES ./Common/*.cpp ./Common/*.h)
#add_library(Common SHARED ${COMMON_SOURCES})
#target_link_libraries(Common ${CMAKE_THREAD_LIBS_INIT})
#set_property(TARGET Common PROPERTY CXX_STANDARD 14) # we want C++14

# How to include and link OpenGL and GLUT libraries
# find_package(OpenGL REQUIRED)
# find_package(GLUT REQUIRED)
# target_include_directories(CommonLibs SYSTEM ${OPENGL_INCLUDE_DIRS} ${GLUT_INCLUDE_DIRS} )
# target_link_libraries(CommonLibs ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} )

# How to use some other standard packages
# find_package(ZLIB REQUIRED) 
# target_link_libraries (CommonLibs PRIVATE ZLIB::ZLIB)
# 
find_package(OpenSSL REQUIRED)
target_link_libraries (CommonLibs PRIVATE OpenSSL::SSL OpenSSL::Crypto)
#
# find_package(CURL REQUIRED)
# target_link_libraries (CommonLibs PRIVATE CURL::libcurl)

#------------------------------------------------------------------------------
# Unit tests

add_subdirectory(unittest)
