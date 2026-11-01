cmake_minimum_required(VERSION 3.21)

project(ClipVault
    VERSION 1.0.0
    DESCRIPTION "Local-first clipboard history"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CLIPVAULT_SEMANTIC "Build local semantic search, needs ONNXRUNTIME_ROOT" OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Concurrent DBus Gui Widgets Sql Network Test)
qt_standard_project_setup()

add_library(clipvault_lib STATIC
    src/core/ClipboardType.cpp
    src/core/Classifier.cpp
    src/core/Logging.cpp
    src/core/SecretDetector.cpp
    src/core/SecureZero.cpp
    src/storage/Database.cpp
    src/storage/ClipboardRepository.cpp
    src/storage/SettingsRepository.cpp
    src/services/SettingsService.cpp
    src/services/HistoryService.cpp
    src/services/SearchService.cpp
    src/services/EphemeralStore.cpp
    src/clipboard/ClipboardMonitor.cpp
    src/platform/Autostart.cpp
    src/platform/SingleInstance.cpp
    src/platform/GnomeGlobalHotkey.cpp
    src/platform/GlobalHotkeyFactory.cpp
    src/platform/FocusTrackerFactory.cpp
    src/platform/SourceLocation.cpp
    src/ui/ClipboardListModel.cpp
    src/ui/ClipboardItemDelegate.cpp
    src/ui/SettingsDialog.cpp
    src/ui/MainWindow.cpp
    src/app/TrayIcon.cpp
    src/app/ApplicationController.cpp

    # Headers are listed so AUTOMOC sees the Q_OBJECT classes that have no
    # identically named source file (GlobalHotkey.h in particular).
    src/core/ClipboardItem.h
    src/core/ClipboardType.h
    src/core/Classifier.h
    src/core/Logging.h
    src/core/Provenance.h
    src/core/SearchFilter.h
    src/core/SecretDetector.h
    src/core/SecureZero.h
    src/storage/Database.h
    src/storage/ClipboardRepository.h
    src/storage/SettingsRepository.h
    src/services/SettingsService.h
    src/services/HistoryService.h
    src/services/SearchService.h
    src/services/EphemeralStore.h
    src/clipboard/ClipboardMonitor.h
    src/platform/Autostart.h
    src/platform/SingleInstance.h
    src/platform/GlobalHotkey.h
    src/platform/GnomeGlobalHotkey.h
    src/platform/FocusTracker.h
    src/platform/SourceLocation.h
    src/ui/ClipboardListModel.h
    src/ui/ClipboardItemDelegate.h
    src/ui/SettingsDialog.h
    src/ui/MainWindow.h
    src/app/TrayIcon.h
    src/app/ApplicationController.h
)

target_include_directories(clipvault_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clipvault_lib PUBLIC
    Qt6::Core Qt6::Concurrent Qt6::DBus Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network)

# sources added after the first release also build with warnings as errors
set(CLIPVAULT_STRICT_SOURCES
    src/core/SecretDetector.cpp
    src/core/SecureZero.cpp
    src/platform/FocusTrackerFactory.cpp
    src/platform/SourceLocation.cpp
    src/services/EphemeralStore.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(clipvault_lib PRIVATE -Wall -Wextra -Wpedantic)
    if(CLIPVAULT_STRICT_SOURCES)
        set_source_files_properties(${CLIPVAULT_STRICT_SOURCES} PROPERTIES COMPILE_OPTIONS -Werror)
    endif()
endif()

if(CLIPVAULT_SEMANTIC)
    target_compile_definitions(clipvault_lib PUBLIC CLIPVAULT_SEMANTIC=1)
endif()

qt_add_executable(clipvault src/main.cpp)
target_link_libraries(clipvault PRIVATE clipvault_lib)

add_executable(clipvault_bench tools/bench.cpp)
target_link_libraries(clipvault_bench PRIVATE clipvault_lib)

include(GNUInstallDirs)
install(TARGETS clipvault RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/clipvault.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/clipvault.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

enable_testing()
add_subdirectory(tests)
