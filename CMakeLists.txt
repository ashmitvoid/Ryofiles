cmake_minimum_required(VERSION 3.25)

project(ryofiles VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    Concurrent
)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(ryofiles
    src/main.cpp
    src/ryoku/RyokuIntegration.hpp
    src/ryoku/RyokuIntegration.cpp
    src/fs/DirectoryModel.hpp
    src/fs/DirectoryModel.cpp
    src/navigation/DirectorySession.hpp
    src/navigation/DirectorySession.cpp
    src/navigation/TabManager.hpp
    src/navigation/TabManager.cpp
    src/operations/OperationManager.hpp
    src/operations/OperationManager.cpp
    src/trash/TrashManager.hpp
    src/trash/TrashManager.cpp
    src/integrations/ClipboardController.hpp
    src/integrations/ClipboardController.cpp
    src/integrations/DesktopIntegration.hpp
    src/integrations/DesktopIntegration.cpp
)

qt_add_qml_module(ryofiles
    URI Ryofiles
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/RyokuRail.qml
        qml/components/TabStrip.qml
        qml/components/FileListView.qml
        qml/components/FileGridView.qml
        qml/components/OperationDrawer.qml
        qml/components/ConflictSheet.qml
        qml/components/RenameSheet.qml
        qml/components/TrashView.qml
        qml/components/FileContextMenu.qml
        qml/components/OpenWithSheet.qml
        qml/components/PropertiesSheet.qml
)

target_include_directories(ryofiles PRIVATE src)

target_link_libraries(ryofiles PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
)

option(RYOFILES_BUILD_TESTS "Build Ryofiles test targets" OFF)

if(RYOFILES_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()

    qt_add_executable(ryofiles_operation_tests
        tests/OperationManagerTest.cpp
        src/operations/OperationManager.hpp
        src/operations/OperationManager.cpp
    )

    target_include_directories(ryofiles_operation_tests PRIVATE src)

    target_link_libraries(ryofiles_operation_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME operation-manager COMMAND ryofiles_operation_tests)

    qt_add_executable(ryofiles_trash_tests
        tests/TrashManagerTest.cpp
        src/trash/TrashManager.hpp
        src/trash/TrashManager.cpp
    )

    target_include_directories(ryofiles_trash_tests PRIVATE src)

    target_link_libraries(ryofiles_trash_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME trash-manager COMMAND ryofiles_trash_tests)
endif()

install(TARGETS ryofiles
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES assets/ryofiles.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
