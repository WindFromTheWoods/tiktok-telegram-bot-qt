cmake_minimum_required(VERSION 3.24)

project(TikTokTelegramBot VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Core Network Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

set(TIKTOK_BOT_SOURCES
    src/Logging.cpp
    src/Logging.h
    src/app/BotController.cpp
    src/app/BotController.h
    src/config/AppConfig.cpp
    src/config/AppConfig.h
    src/config/CredentialStore.cpp
    src/config/CredentialStore.h
    src/telegram/TelegramApi.cpp
    src/telegram/TelegramApi.h
    src/telegram/TelegramBot.cpp
    src/telegram/TelegramBot.h
    src/telegram/TelegramTypes.h
    src/tiktok/TikTokDownloader.cpp
    src/tiktok/TikTokDownloader.h
    src/tiktok/TikTokUrlValidator.cpp
    src/tiktok/TikTokUrlValidator.h
)

qt_add_library(tiktok_bot_core STATIC ${TIKTOK_BOT_SOURCES})
target_compile_features(tiktok_bot_core PUBLIC cxx_std_23)
target_include_directories(tiktok_bot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tiktok_bot_core PUBLIC Qt6::Core Qt6::Network)

if(WIN32)
    target_link_libraries(tiktok_bot_core PRIVATE Advapi32)
endif()

qt_add_executable(TikTokTelegramBot
    WIN32
    src/main.cpp
    src/ui/SettingsController.cpp
    src/ui/SettingsController.h
)
target_compile_features(TikTokTelegramBot PRIVATE cxx_std_23)
target_link_libraries(TikTokTelegramBot
    PRIVATE
        tiktok_bot_core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)

qt_add_qml_module(TikTokTelegramBot
    URI TikTokBot.Ui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

function(tiktok_bot_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

tiktok_bot_enable_warnings(tiktok_bot_core)
tiktok_bot_enable_warnings(TikTokTelegramBot)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS TikTokTelegramBot RUNTIME DESTINATION bin)

qt_generate_deploy_qml_app_script(
    TARGET TikTokTelegramBot
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
