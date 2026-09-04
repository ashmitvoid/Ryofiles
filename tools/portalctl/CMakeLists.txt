cmake_minimum_required(VERSION 3.25)

project(ryofiles-portalctl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)

add_executable(ryofiles-portalctl
    main.cpp
    ../../src/portal/PortalRoutingManager.hpp
    ../../src/portal/PortalRoutingManager.cpp
)

target_include_directories(ryofiles-portalctl PRIVATE ../../src)
target_link_libraries(ryofiles-portalctl PRIVATE Qt6::Core)

include(GNUInstallDirs)
install(TARGETS ryofiles-portalctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

option(RYOFILES_PORTALCTL_BUILD_TESTS "Build portal routing helper tests" OFF)
if(RYOFILES_PORTALCTL_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(ryofiles_portal_routing_tests
        ../../tests/PortalRoutingManagerTest.cpp
        ../../src/portal/PortalRoutingManager.hpp
        ../../src/portal/PortalRoutingManager.cpp
    )
    target_include_directories(ryofiles_portal_routing_tests PRIVATE ../../src)
    target_link_libraries(ryofiles_portal_routing_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME portal-routing-manager COMMAND ryofiles_portal_routing_tests)
endif()
