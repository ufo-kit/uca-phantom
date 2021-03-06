cmake_minimum_required(VERSION 2.6)

if (POLICY CMP0053)
cmake_policy(SET CMP0053 OLD)
endif ()

if (POLICY CMP0054)
    cmake_policy(SET CMP0054 OLD)
endif ()

project(ucaphantom C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(PkgConfig)

include(FindPackageHandleStandardArgs)
include(PkgConfigVars)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4 -maes")

add_definitions("-std=c99 -O2 -Wall -Wextra -fPIC")
add_definitions(-DG_LOG_DOMAIN="Uca-Phantom")

pkg_check_modules(GIO gio-2.0>=2.22 REQUIRED)
pkg_check_modules(UCA libuca>=2.1.0 REQUIRED)
pkg_check_variable(libuca plugindir)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${UCA_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS})

link_directories(
    ${UCA_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS})

add_library(ucaphantom SHARED uca-phantom-camera.c)

target_link_libraries(ucaphantom
    ${UCA_LIBRARIES}
    ${GIO_LIBRARIES})
    
target_link_libraries(ucaphantom m)

set_property(TARGET ucaphantom PROPERTY C_STANDARD 99)

install(TARGETS ucaphantom
        LIBRARY DESTINATION ${LIBUCA_PLUGINDIR}
        RUNTIME DESTINATION ${LIBUCA_PLUGINDIR})
