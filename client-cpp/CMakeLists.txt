cmake_minimum_required(VERSION 3.21)

project(FnDesktopPlus VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network WebEngineWidgets WebEngineCore WebChannel)

set(APP_SOURCES
    src/main.cpp

    src/core/config.cpp
    src/core/api_client.cpp
    src/core/secure_vault.cpp
    src/core/app_logger.cpp

    src/ui/icons.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    src/ui/title_bar.cpp
    src/ui/appearance_dialog.cpp
    src/ui/login_dialog.cpp
    src/ui/main_window.cpp
    src/ui/wizard_dialog.cpp
    src/ui/app_install_dialog.cpp
    src/ui/ssh_session.cpp
    src/ui/ssh/ssh_bridge.cpp
    src/ui/ssh/ssh_workspace.cpp
    src/ui/ssh/pty_runner.cpp
    src/ui/ssh/sftp_panel.cpp
    src/ui/ssh/ssh_keys_dialog.cpp

    src/ui/pages/admin_page.cpp
    src/ui/pages/browser_page.cpp
    src/ui/pages/dashboard_page.cpp
    src/ui/pages/files_page.cpp
    src/ui/pages/docker_page.cpp
    src/ui/pages/users_page.cpp
    src/ui/pages/audit_page.cpp
    src/ui/pages/ssh_page.cpp
    src/ui/pages/settings_page.cpp

    resources/resources.qrc
)

set(APP_HEADERS
    src/core/config.h
    src/core/api_client.h
    src/core/secure_vault.h
    src/core/app_logger.h

    src/ui/icons.h
    src/ui/theme.h
    src/ui/widgets.h
    src/ui/title_bar.h
    src/ui/appearance_dialog.h
    src/ui/login_dialog.h
    src/ui/main_window.h
    src/ui/wizard_dialog.h
    src/ui/app_install_dialog.h
    src/ui/ssh_session.h
    src/ui/ssh/ssh_bridge.h
    src/ui/ssh/ssh_workspace.h
    src/ui/ssh/pty_runner.h
    src/ui/ssh/sftp_panel.h
    src/ui/ssh/ssh_keys_dialog.h

    src/ui/pages/admin_page.h
    src/ui/pages/browser_page.h
    src/ui/pages/dashboard_page.h
    src/ui/pages/files_page.h
    src/ui/pages/docker_page.h
    src/ui/pages/users_page.h
    src/ui/pages/audit_page.h
    src/ui/pages/ssh_page.h
    src/ui/pages/settings_page.h
)

add_executable(FnDesktopPlus ${APP_SOURCES} ${APP_HEADERS})

set_target_properties(FnDesktopPlus PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "FnDesktopPlus"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(WIN32)
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RC}" "IDI_ICON1 ICON DISCARDABLE \"${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico\"\n")
    target_sources(FnDesktopPlus PRIVATE "${APP_ICON_RC}")
endif()

target_include_directories(FnDesktopPlus PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(FnDesktopPlus PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
    Qt6::WebChannel
)

if(MSVC)
    target_compile_options(FnDesktopPlus PRIVATE /utf-8 /W3 /permissive-)
    target_compile_definitions(FnDesktopPlus PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
    target_link_libraries(FnDesktopPlus PRIVATE ncrypt crypt32)
endif()

install(TARGETS FnDesktopPlus RUNTIME DESTINATION .)
