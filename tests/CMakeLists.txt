# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# Standalone test project; the component itself is built with qmake and does
# not include this. See README.md for how to configure it.

cmake_minimum_required(VERSION 3.16)
project(qt-features-webos-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui InputSupportPrivate)

# googletest: the build host has none, so it is built from source
# (GTEST_SOURCE_DIR) unless the sysroot has an installed copy.
set(GTEST_SOURCE_DIR "" CACHE PATH "googletest source tree to build instead of find_package(GTest)")
if(GTEST_SOURCE_DIR)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GTEST_SOURCE_DIR} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
    set(GTEST_TARGETS GTest::gtest)
else()
    find_package(GTest REQUIRED)
    set(GTEST_TARGETS GTest::gtest)
endif()

# The tool under test, built as the recipe builds it.
add_executable(generate_qmap ${SRC_ROOT}/tools/generate_qmap/generate_qmap.cpp)
target_link_libraries(generate_qmap PRIVATE Qt6::Core)

# Qt's own copy of the default keymap, for the drift check.
get_filename_component(_qt_prefix "${Qt6InputSupportPrivate_DIR}/../../.." ABSOLUTE)
find_file(QT_DEFAULTMAP_HEADER qevdevkeyboard_defaultmap_p.h
          PATHS "${_qt_prefix}/include/QtInputSupport/${Qt6_VERSION}/QtInputSupport/private"
          NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)

add_executable(qt-features-webos-tests
    unit/qmapfile.cpp
    unit/test_generate_qmap.cpp
    unit/test_keymap_tables.cpp
    unit/test_evdev_handler.cpp
    unit/test_key_extension.cpp
    unit/test_main.cpp)
target_include_directories(qt-features-webos-tests PRIVATE
    ${SRC_ROOT}/tools/generate_qmap ${SRC_ROOT}/include unit)
target_compile_definitions(qt-features-webos-tests PRIVATE
    GENERATE_QMAP="$<TARGET_FILE:generate_qmap>"
    QT_DEFAULTMAP_HEADER="${QT_DEFAULTMAP_HEADER}"
    SRC_ROOT="${SRC_ROOT}")
target_link_libraries(qt-features-webos-tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::InputSupportPrivate ${GTEST_TARGETS})
add_dependencies(qt-features-webos-tests generate_qmap)

enable_testing()
add_test(NAME unit COMMAND qt-features-webos-tests)
set_tests_properties(unit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME prf COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/prf/run-prf-tests.sh)
