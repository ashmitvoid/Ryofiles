cmake_minimum_required(VERSION 3.25)

project(ryofiles VERSION 0.3.0 LANGUAGES CXX)

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
    DBus
)
find_package(LibArchive REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(ryofiles
    src/main.cpp
    src/archive/ArchivePathGuard.hpp
    src/archive/ArchivePathGuard.cpp
    src/archive/ArchiveExtractor.hpp
    src/archive/ArchiveExtractor.cpp
    src/archive/ArchiveCreator.hpp
    src/archive/ArchiveCreator.cpp
    src/ryoku/RyokuIntegration.hpp
    src/ryoku/RyokuIntegration.cpp
    src/fs/DirectoryModel.hpp
    src/fs/DirectoryModel.cpp
    src/search/DeepSearchModel.hpp
    src/search/DeepSearchModel.cpp
    src/storage/DriveModel.hpp
    src/storage/DriveModel.cpp
    src/locations/LocationSpec.hpp
    src/locations/LocationSpec.cpp
    src/locations/NetworkConnectionController.hpp
    src/locations/NetworkConnectionController.cpp
    src/locations/NetworkDisconnectController.hpp
    src/locations/NetworkDisconnectController.cpp
    src/locations/NetworkLocationModel.hpp
    src/locations/NetworkLocationModel.cpp
    src/locations/RemoteDirectoryModel.hpp
    src/locations/RemoteDirectoryModel.cpp
    src/locations/SessionFileModel.hpp
    src/locations/SessionFileModel.cpp
    src/git/GitStatusController.hpp
    src/git/GitStatusController.cpp
    src/git/GitActionController.hpp
    src/git/GitActionController.cpp
    src/navigation/DirectorySession.hpp
    src/navigation/DirectorySession.cpp
    src/navigation/TabManager.hpp
    src/navigation/TabManager.cpp
    src/operations/OperationManager.hpp
    src/operations/OperationManager.cpp
    src/operations/RemoteOperationManager.hpp
    src/operations/RemoteOperationManager.cpp
    src/picker/PickerController.hpp
    src/picker/PortalPickerContext.hpp
    src/portal/PortalPickerRequest.hpp
    src/portal/PortalPickerRequest.cpp
    src/portal/FileChooserPortal.hpp
    src/portal/FileChooserPortal.cpp
    src/trash/TrashManager.hpp
    src/trash/TrashManager.cpp
    src/integrations/ClipboardController.hpp
    src/integrations/ClipboardController.cpp
    src/integrations/DesktopIntegration.hpp
    src/integrations/DesktopIntegration.cpp
    src/integrations/NetworkMountRecoveryRegistry.hpp
    src/thumbnails/ThumbnailStore.hpp
    src/thumbnails/ThumbnailStore.cpp
    src/thumbnails/ThumbnailProvider.hpp
    src/thumbnails/ThumbnailProvider.cpp
    src/thumbnails/ThumbnailController.hpp
    src/thumbnails/ThumbnailController.cpp
    src/preview/TextPreviewStore.hpp
    src/preview/TextPreviewStore.cpp
    src/preview/TextPreviewLoader.hpp
    src/preview/TextPreviewLoader.cpp
)

qt_add_qml_module(ryofiles
    URI Ryofiles
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Picker.qml
        qml/components/RyokuRail.qml
        qml/components/NetworkConnectSheet.qml
        qml/components/TabStrip.qml
        qml/components/FilePane.qml
        qml/components/SplitPaneContainer.qml
        qml/components/FileListView.qml
        qml/components/FileGridView.qml
        qml/components/FolderFilterBar.qml
        qml/components/DeepSearchPanel.qml
        qml/components/CommandPalette.qml
        qml/components/GitStatusBadge.qml
        qml/components/GitBranchChip.qml
        qml/components/GitAwarenessOverlay.qml
        qml/components/GitDiffPanel.qml
        qml/components/OperationDrawer.qml
        qml/components/ConflictSheet.qml
        qml/components/RenameSheet.qml
        qml/components/ArchiveCreateSheet.qml
        qml/components/TrashView.qml
        qml/components/FileContextMenu.qml
        qml/components/RemoteFileContextMenu.qml
        qml/components/RemotePaneActions.qml
        qml/components/OpenWithSheet.qml
        qml/components/PropertiesSheet.qml
        qml/components/PreviewPanel.qml
        qml/components/PortalPickerOptions.qml
)

target_include_directories(ryofiles PRIVATE src)

target_link_libraries(ryofiles PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
    Qt6::DBus
    LibArchive::LibArchive
    PkgConfig::GIO
)

option(RYOFILES_BUILD_TESTS "Build Ryofiles test targets" OFF)

if(RYOFILES_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()

    qt_add_executable(ryofiles_operation_tests
        tests/OperationManagerTest.cpp
        src/operations/OperationManager.hpp
        src/operations/OperationManager.cpp
        src/archive/ArchiveCreator.hpp
        src/archive/ArchiveCreator.cpp
        src/archive/ArchiveExtractor.hpp
        src/archive/ArchiveExtractor.cpp
        src/archive/ArchivePathGuard.hpp
        src/archive/ArchivePathGuard.cpp
    )

    target_include_directories(ryofiles_operation_tests PRIVATE src)

    target_link_libraries(ryofiles_operation_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
        LibArchive::LibArchive
    )

    add_test(NAME operation-manager COMMAND ryofiles_operation_tests)

    qt_add_executable(ryofiles_archive_operation_tests
        tests/ArchiveOperationManagerTest.cpp
        src/operations/OperationManager.hpp
        src/operations/OperationManager.cpp
        src/archive/ArchiveCreator.hpp
        src/archive/ArchiveCreator.cpp
        src/archive/ArchiveExtractor.hpp
        src/archive/ArchiveExtractor.cpp
        src/archive/ArchivePathGuard.hpp
        src/archive/ArchivePathGuard.cpp
    )

    target_include_directories(ryofiles_archive_operation_tests PRIVATE src)

    target_link_libraries(ryofiles_archive_operation_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
        LibArchive::LibArchive
    )

    add_test(NAME archive-operation-manager COMMAND ryofiles_archive_operation_tests)

    qt_add_executable(ryofiles_remote_operation_tests
        tests/RemoteOperationManagerTest.cpp
        src/operations/RemoteOperationManager.hpp
        src/operations/RemoteOperationManager.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
    )

    target_include_directories(ryofiles_remote_operation_tests PRIVATE src)

    target_link_libraries(ryofiles_remote_operation_tests PRIVATE
        Qt6::Core
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME remote-operation COMMAND ryofiles_remote_operation_tests)

    qt_add_executable(ryofiles_clipboard_tests
        tests/ClipboardControllerTest.cpp
        src/integrations/ClipboardController.hpp
        src/integrations/ClipboardController.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
    )

    target_include_directories(ryofiles_clipboard_tests PRIVATE src)

    target_link_libraries(ryofiles_clipboard_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
    )

    add_test(NAME clipboard-controller COMMAND ryofiles_clipboard_tests)

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

    qt_add_executable(ryofiles_desktop_integration_tests
        tests/DesktopIntegrationTest.cpp
        src/integrations/DesktopIntegration.hpp
        src/integrations/DesktopIntegration.cpp
    )

    target_include_directories(ryofiles_desktop_integration_tests PRIVATE src)

    target_link_libraries(ryofiles_desktop_integration_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME desktop-integration COMMAND ryofiles_desktop_integration_tests)

    qt_add_executable(ryofiles_thumbnail_tests
        tests/ThumbnailStoreTest.cpp
        src/thumbnails/ThumbnailStore.hpp
        src/thumbnails/ThumbnailStore.cpp
    )

    target_include_directories(ryofiles_thumbnail_tests PRIVATE src)

    target_link_libraries(ryofiles_thumbnail_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Test
    )

    add_test(NAME thumbnail-store COMMAND ryofiles_thumbnail_tests)

    qt_add_executable(ryofiles_text_preview_tests
        tests/TextPreviewStoreTest.cpp
        src/preview/TextPreviewStore.hpp
        src/preview/TextPreviewStore.cpp
    )

    target_include_directories(ryofiles_text_preview_tests PRIVATE src)

    target_link_libraries(ryofiles_text_preview_tests PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME text-preview-store COMMAND ryofiles_text_preview_tests)

    qt_add_executable(ryofiles_directory_filter_tests
        tests/DirectoryModelFilterTest.cpp
        src/fs/DirectoryModel.hpp
        src/fs/DirectoryModel.cpp
    )

    target_include_directories(ryofiles_directory_filter_tests PRIVATE src)

    target_link_libraries(ryofiles_directory_filter_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME directory-filter COMMAND ryofiles_directory_filter_tests)

    qt_add_executable(ryofiles_deep_search_tests
        tests/DeepSearchModelTest.cpp
        src/search/DeepSearchModel.hpp
        src/search/DeepSearchModel.cpp
    )

    target_include_directories(ryofiles_deep_search_tests PRIVATE src)

    target_link_libraries(ryofiles_deep_search_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME deep-search COMMAND ryofiles_deep_search_tests)

    qt_add_executable(ryofiles_git_status_tests
        tests/GitStatusControllerTest.cpp
        src/git/GitStatusController.hpp
        src/git/GitStatusController.cpp
    )

    target_include_directories(ryofiles_git_status_tests PRIVATE src)

    target_link_libraries(ryofiles_git_status_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME git-status COMMAND ryofiles_git_status_tests)

    qt_add_executable(ryofiles_git_action_tests
        tests/GitActionControllerTest.cpp
        src/git/GitActionController.hpp
        src/git/GitActionController.cpp
    )

    target_include_directories(ryofiles_git_action_tests PRIVATE src)

    target_link_libraries(ryofiles_git_action_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
    )

    add_test(NAME git-actions COMMAND ryofiles_git_action_tests)

    qt_add_executable(ryofiles_tab_split_tests
        tests/TabManagerSplitTest.cpp
        src/navigation/TabManager.hpp
        src/navigation/TabManager.cpp
        src/navigation/DirectorySession.hpp
        src/navigation/DirectorySession.cpp
        src/fs/DirectoryModel.hpp
        src/fs/DirectoryModel.cpp
        src/search/DeepSearchModel.hpp
        src/search/DeepSearchModel.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/RemoteDirectoryModel.hpp
        src/locations/RemoteDirectoryModel.cpp
        src/locations/SessionFileModel.hpp
        src/locations/SessionFileModel.cpp
    )

    target_include_directories(ryofiles_tab_split_tests PRIVATE src)

    target_link_libraries(ryofiles_tab_split_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME tab-split COMMAND ryofiles_tab_split_tests)

    qt_add_executable(ryofiles_network_mount_recovery_tests
        tests/NetworkMountRecoveryTest.cpp
        src/navigation/TabManager.hpp
        src/navigation/TabManager.cpp
        src/navigation/DirectorySession.hpp
        src/navigation/DirectorySession.cpp
        src/fs/DirectoryModel.hpp
        src/fs/DirectoryModel.cpp
        src/search/DeepSearchModel.hpp
        src/search/DeepSearchModel.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/RemoteDirectoryModel.hpp
        src/locations/RemoteDirectoryModel.cpp
        src/locations/SessionFileModel.hpp
        src/locations/SessionFileModel.cpp
        src/integrations/NetworkMountRecoveryRegistry.hpp
    )

    target_include_directories(ryofiles_network_mount_recovery_tests PRIVATE src)

    target_link_libraries(ryofiles_network_mount_recovery_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME network-mount-recovery COMMAND ryofiles_network_mount_recovery_tests)

    qt_add_executable(ryofiles_directory_session_location_tests
        tests/DirectorySessionLocationTest.cpp
        src/navigation/DirectorySession.hpp
        src/navigation/DirectorySession.cpp
        src/fs/DirectoryModel.hpp
        src/fs/DirectoryModel.cpp
        src/search/DeepSearchModel.hpp
        src/search/DeepSearchModel.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/RemoteDirectoryModel.hpp
        src/locations/RemoteDirectoryModel.cpp
        src/locations/SessionFileModel.hpp
        src/locations/SessionFileModel.cpp
    )

    target_include_directories(ryofiles_directory_session_location_tests PRIVATE src)

    target_link_libraries(ryofiles_directory_session_location_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME directory-session-location COMMAND ryofiles_directory_session_location_tests)

    qt_add_executable(ryofiles_location_spec_tests
        tests/LocationSpecTest.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
    )

    target_include_directories(ryofiles_location_spec_tests PRIVATE src)

    target_link_libraries(ryofiles_location_spec_tests PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME location-spec COMMAND ryofiles_location_spec_tests)

    qt_add_executable(ryofiles_network_connection_tests
        tests/NetworkConnectionControllerTest.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/NetworkConnectionController.hpp
        src/locations/NetworkConnectionController.cpp
    )

    target_include_directories(ryofiles_network_connection_tests PRIVATE src)

    target_link_libraries(ryofiles_network_connection_tests PRIVATE
        Qt6::Core
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME network-connection COMMAND ryofiles_network_connection_tests)

    qt_add_executable(ryofiles_network_disconnect_tests
        tests/NetworkDisconnectControllerTest.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/NetworkDisconnectController.hpp
        src/locations/NetworkDisconnectController.cpp
    )

    target_include_directories(ryofiles_network_disconnect_tests PRIVATE src)

    target_link_libraries(ryofiles_network_disconnect_tests PRIVATE
        Qt6::Core
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME network-disconnect COMMAND ryofiles_network_disconnect_tests)

    qt_add_executable(ryofiles_remote_directory_tests
        tests/RemoteDirectoryModelTest.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/RemoteDirectoryModel.hpp
        src/locations/RemoteDirectoryModel.cpp
    )

    target_include_directories(ryofiles_remote_directory_tests PRIVATE src)

    target_link_libraries(ryofiles_remote_directory_tests PRIVATE
        Qt6::Core
        Qt6::Concurrent
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME remote-directory COMMAND ryofiles_remote_directory_tests)

    qt_add_executable(ryofiles_session_file_model_tests
        tests/SessionFileModelTest.cpp
        src/fs/DirectoryModel.hpp
        src/fs/DirectoryModel.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/RemoteDirectoryModel.hpp
        src/locations/RemoteDirectoryModel.cpp
        src/locations/SessionFileModel.hpp
        src/locations/SessionFileModel.cpp
    )

    target_include_directories(ryofiles_session_file_model_tests PRIVATE src)

    target_link_libraries(ryofiles_session_file_model_tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME session-file-model COMMAND ryofiles_session_file_model_tests)

    qt_add_executable(ryofiles_network_location_tests
        tests/NetworkLocationModelTest.cpp
        src/locations/LocationSpec.hpp
        src/locations/LocationSpec.cpp
        src/locations/NetworkLocationModel.hpp
        src/locations/NetworkLocationModel.cpp
    )

    target_include_directories(ryofiles_network_location_tests PRIVATE src)

    target_link_libraries(ryofiles_network_location_tests PRIVATE
        Qt6::Core
        Qt6::Test
        PkgConfig::GIO
    )

    add_test(NAME network-locations COMMAND ryofiles_network_location_tests)

    qt_add_executable(ryofiles_drive_tests
        tests/DriveModelTest.cpp
        src/storage/DriveModel.hpp
        src/storage/DriveModel.cpp
    )

    target_include_directories(ryofiles_drive_tests PRIVATE src)

    target_link_libraries(ryofiles_drive_tests PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Test
    )

    add_test(NAME drive-model COMMAND ryofiles_drive_tests)

    qt_add_executable(ryofiles_portal_picker_tests
        tests/PortalPickerRequestTest.cpp
        src/portal/PortalPickerRequest.hpp
        src/portal/PortalPickerRequest.cpp
    )

    target_include_directories(ryofiles_portal_picker_tests PRIVATE src)

    target_link_libraries(ryofiles_portal_picker_tests PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Test
    )

    add_test(NAME portal-picker-request COMMAND ryofiles_portal_picker_tests)

    qt_add_executable(ryofiles_portal_context_tests
        tests/PortalPickerContextTest.cpp
        src/picker/PortalPickerContext.hpp
    )

    target_include_directories(ryofiles_portal_context_tests PRIVATE src)

    target_link_libraries(ryofiles_portal_context_tests PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME portal-picker-context COMMAND ryofiles_portal_context_tests)

    qt_add_executable(ryofiles_archive_path_tests
        tests/ArchivePathGuardTest.cpp
        src/archive/ArchivePathGuard.hpp
        src/archive/ArchivePathGuard.cpp
    )

    target_include_directories(ryofiles_archive_path_tests PRIVATE src)

    target_link_libraries(ryofiles_archive_path_tests PRIVATE
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME archive-path-guard COMMAND ryofiles_archive_path_tests)

    qt_add_executable(ryofiles_archive_extractor_tests
        tests/ArchiveExtractorTest.cpp
        src/archive/ArchiveExtractor.hpp
        src/archive/ArchiveExtractor.cpp
        src/archive/ArchivePathGuard.hpp
        src/archive/ArchivePathGuard.cpp
    )

    target_include_directories(ryofiles_archive_extractor_tests PRIVATE src)

    target_link_libraries(ryofiles_archive_extractor_tests PRIVATE
        Qt6::Core
        Qt6::Test
        LibArchive::LibArchive
    )

    add_test(NAME archive-extractor COMMAND ryofiles_archive_extractor_tests)

    qt_add_executable(ryofiles_archive_creator_tests
        tests/ArchiveCreatorTest.cpp
        src/archive/ArchiveCreator.hpp
        src/archive/ArchiveCreator.cpp
        src/archive/ArchiveExtractor.hpp
        src/archive/ArchiveExtractor.cpp
        src/archive/ArchivePathGuard.hpp
        src/archive/ArchivePathGuard.cpp
    )

    target_include_directories(ryofiles_archive_creator_tests PRIVATE src)

    target_link_libraries(ryofiles_archive_creator_tests PRIVATE
        Qt6::Core
        Qt6::Test
        LibArchive::LibArchive
    )

    add_test(NAME archive-creator COMMAND ryofiles_archive_creator_tests)
endif()

install(TARGETS ryofiles
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES assets/ryofiles.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
