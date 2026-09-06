cmake_minimum_required(VERSION 3.25)

project(ryofiles-preview-foundation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)

qt_add_executable(ryofiles-preview-helper
    ../../src/preview/PreviewProtocol.hpp
    ../../src/preview/MediaProbe.hpp
    ../../src/preview/MediaProbe.cpp
    ../../src/preview/preview_helper_main.cpp
)
target_include_directories(ryofiles-preview-helper PRIVATE ../../src)
target_link_libraries(ryofiles-preview-helper PRIVATE
    Qt6::Core
    Qt6::Gui
    PkgConfig::POPPLER_QT6
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
)

option(RYOFILES_PREVIEW_BUILD_TESTS "Build preview foundation tests" ON)
if(RYOFILES_PREVIEW_BUILD_TESTS)
    enable_testing()

    qt_add_executable(ryofiles_preview_scheduler_tests
        ../../tests/PreviewSchedulerTest.cpp
        ../../src/preview/PreviewProtocol.hpp
        ../../src/preview/PreviewScheduler.hpp
        ../../src/preview/PreviewScheduler.cpp
    )
    target_include_directories(ryofiles_preview_scheduler_tests PRIVATE ../../src)
    target_link_libraries(ryofiles_preview_scheduler_tests PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(ryofiles_preview_scheduler_tests ryofiles-preview-helper)

    add_test(NAME preview-scheduler COMMAND ryofiles_preview_scheduler_tests)

    qt_add_executable(ryofiles_media_preview_tests
        ../../tests/MediaProbeTest.cpp
        ../../src/preview/PreviewProtocol.hpp
        ../../src/preview/PreviewScheduler.hpp
        ../../src/preview/PreviewScheduler.cpp
    )
    target_include_directories(ryofiles_media_preview_tests PRIVATE ../../src)
    target_link_libraries(ryofiles_media_preview_tests PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(ryofiles_media_preview_tests ryofiles-preview-helper)

    add_test(NAME media-preview COMMAND ryofiles_media_preview_tests)
endif()

install(TARGETS ryofiles-preview-helper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
