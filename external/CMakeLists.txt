include(ExternalProject)
find_package(Git REQUIRED)
# Example usage:
#
#     ExternalHeaderOnly_Add("Catch"
#         "https://github.com/catchorg/Catch2.git" "origin/master" "single_include/catch2")
#
# Use with:
#     target_link_libraries(unittests Catch)
# This will add the INCLUDE_FOLDER_PATH to the `unittests` target.

macro(ExternalHeaderOnly_Add LIBNAME REPOSITORY GIT_TAG INCLUDE_FOLDER_PATH)
    ExternalProject_Add(
        ${LIBNAME}_download
        PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}
        GIT_REPOSITORY ${REPOSITORY}
        # For shallow git clone (without downloading whole history)
        # GIT_SHALLOW 1
        # For point at certain tag
        GIT_TAG origin/${GIT_TAG}
        #disables auto update on every build
        UPDATE_DISCONNECTED 1
        #disable following
        CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_DIR "" INSTALL_COMMAND ""
        )
    # special target
    add_custom_target(${LIBNAME}_update
        COMMENT "Updated ${LIBNAME}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}/src/${LIBNAME}_download
        COMMAND ${GIT_EXECUTABLE} fetch --recurse-submodules
        COMMAND ${GIT_EXECUTABLE} reset --hard origin/${GIT_TAG}
        COMMAND ${GIT_EXECUTABLE} submodule update --init --force --recursive --remote --merge
        DEPENDS ${LIBNAME}_download)

    set(${LIBNAME}_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}/src/${LIBNAME}_download/)
    add_library(${LIBNAME} INTERFACE)
    add_dependencies(${LIBNAME} ${LIBNAME}_download)
    # add_dependencies(update ${LIBNAME}_update)
    target_include_directories(${LIBNAME} SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}/src/${LIBNAME}_download/${INCLUDE_FOLDER_PATH})
endmacro()

# This command will clone git repo during cmake setup phase, also adds
# ${LIBNAME}_update target into general update target.
# Example usage:
#
#   ExternalDownloadNowGit(cpr https://github.com/finkandreas/cpr.git origin/master)
#   add_subdirectory(${cpr_SOURCE_DIR})
macro(ExternalDownloadNowGit LIBNAME REPOSITORY GIT_TAG)

    set(${LIBNAME}_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}/src/${LIBNAME}_download/)

    # clone repository if not done
    if(IS_DIRECTORY ${${LIBNAME}_SOURCE_DIR})
        message(STATUS "Already downloaded: ${REPOSITORY}")
    else()
        message(STATUS "Clonning: ${REPOSITORY}")
        execute_process(
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMAND ${GIT_EXECUTABLE} clone --recursive ${REPOSITORY} ${LIBNAME}/src/${LIBNAME}_download
            )
        # switch to target TAG and update submodules
        execute_process(
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}/src/${LIBNAME}_download
            COMMAND ${GIT_EXECUTABLE} reset --hard origin/${GIT_TAG}
            COMMAND ${GIT_EXECUTABLE} submodule update --init --force --recursive --remote --merge
            )
    endif()

    # special update target
    add_custom_target(${LIBNAME}_update
        COMMENT "Updated ${LIBNAME}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${LIBNAME}/src/${LIBNAME}_download
        COMMAND ${GIT_EXECUTABLE} fetch --recurse-submodules
        COMMAND ${GIT_EXECUTABLE} reset --hard origin/${GIT_TAG}
        COMMAND ${GIT_EXECUTABLE} submodule update --init --force --recursive --remote --merge)
    # Add this as dependency to the general update target
    add_dependencies(update ${LIBNAME}_update)
endmacro()

#------------------------------------------------------------------------------
# Catch - Unit test header only library

# target_link_libraries (exampleApp PRIVATE catch)
# #include <catch.hpp>
ExternalHeaderOnly_Add(Catch
    "https://github.com/catchorg/Catch2.git" "origin/master" "single_include/catch2")

#------------------------------------------------------------------------------
# ExternalHeaderOnly_Add(json
#     "https://github.com/nlohmann/json.git" "origin/master" "single_include")

#------------------------------------------------------------------------------
# target_link_libraries (exampleApp PRIVATE spdlog)
ExternalHeaderOnly_Add(spdlog
    "https://github.com/gabime/spdlog.git" "v1.x" "include")

#------------------------------------------------------------------------------
# ExternalDownloadNowGit(fmt "https://github.com/fmtlib/fmt.git" "origin/master")
# add_library(fmt STATIC ${fmt_SOURCE_DIR}/src/format.cc)
# target_include_directories(fmt SYSTEM PUBLIC ${fmt_SOURCE_DIR}/include)
# # target_compile_definitions(spdlog INTERFACE -DSPDLOG_FMT_EXTERNAL) #if using SPDLOG

#-----------------------------------------------------------------------------
# TOML Lib
ExternalHeaderOnly_Add(cpptoml
    "https://github.com/skystrife/cpptoml.git" "origin/master" "include")

# Tracy
# TODO Need to find a way to neatly switch to tags. Have to manually set tag
ExternalHeaderOnly_Add(tracy
	"https://bitbucket.org/wolfpld/tracy.git" "master" "")
