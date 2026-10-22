# Can also be built separately for fast protocol/session integration tests.
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(TexpressoClient LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_AUTOMOC ON)
    include(GNUInstallDirs)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Concurrent Test)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Concurrent Test)
    option(TEXSTUDIO_TEXPRESSO_TESTS "Build live-preview tests" ON)
endif()
add_library(texpresso-client STATIC
    texpressoprotocol.cpp texpressoprotocol.h
    texpressoprocess.cpp texpressoprocess.h
    texpressopreview.cpp texpressopreview.h
    texpressosession.cpp texpressosession.h
    texpressomanager.cpp texpressomanager.h
    texpressowidget.cpp texpressowidget.h)
target_include_directories(texpresso-client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(texpresso-client PUBLIC Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent)
if(NOT APPLE)
    if(NOT CMAKE_INSTALL_LIBEXECDIR)
        set(CMAKE_INSTALL_LIBEXECDIR libexec)
    endif()
    file(RELATIVE_PATH _txp_install_relative "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBEXECDIR}/texstudio/texpresso")
    target_compile_definitions(texpresso-client PRIVATE TEXPRESSO_INSTALL_RELATIVE="${_txp_install_relative}")
endif()
option(TEXSTUDIO_TEXPRESSO_TESTS "Build live-preview tests" OFF)
if(TEXSTUDIO_TEXPRESSO_TESTS)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(texpresso-client-tests tests/client_test.cpp)
    target_link_libraries(texpresso-client-tests PRIVATE texpresso-client Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME texpresso-client COMMAND texpresso-client-tests)
    set_tests_properties(texpresso-client PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
