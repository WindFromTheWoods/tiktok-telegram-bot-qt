cmake_minimum_required(VERSION 3.24)

project(TikTokTelegramBot VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_DOCUMENTATION "Build the Doxygen API reference" OFF)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Core Network Sql Widgets Qml Quick QuickControls2 Concurrent
                                                  LinguistTools)

qt_standard_project_setup(REQUIRES 6.8)

set(TIKTOK_BOT_SOURCES
    src/Logging.cpp
    src/Logging.h
    src/app/BotController.cpp
    src/app/BotController.h
    src/app/AutostartManager.cpp
    src/app/AutostartManager.h
    src/app/ScheduleCalculator.cpp
    src/app/ScheduleCalculator.h
    src/app/UpdateManager.cpp
    src/app/UpdateManager.h
    src/app/CacheManager.cpp
    src/app/CacheManager.h
    src/app/SingleInstance.cpp
    src/app/SingleInstance.h
    src/app/DiagnosticsService.cpp
    src/app/DiagnosticsService.h
    src/config/AppConfig.cpp
    src/config/AppConfig.h
    src/config/CredentialStore.cpp
    src/config/CredentialStore.h
    src/storage/AppDatabase.cpp
    src/storage/AppDatabase.h
    src/telegram/TelegramApi.cpp
    src/telegram/TelegramApi.h
    src/telegram/TelegramBot.cpp
    src/telegram/TelegramBot.h
    src/telegram/TelegramTypes.h
    src/telegram/TdJsonClient.cpp
    src/telegram/TdJsonClient.h
    src/telegram/TdPublisher.cpp
    src/telegram/TdPublisher.h
    src/tiktok/TikTokDownloader.cpp
    src/tiktok/TikTokDownloader.h
    src/tiktok/TikTokUrlValidator.cpp
    src/tiktok/TikTokUrlValidator.h
)

qt_add_library(tiktok_bot_core STATIC ${TIKTOK_BOT_SOURCES})
target_compile_features(tiktok_bot_core PUBLIC cxx_std_23)
target_include_directories(tiktok_bot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(tiktok_bot_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Concurrent)

if(WIN32)
    target_link_libraries(tiktok_bot_core PRIVATE Advapi32)
endif()

qt_add_executable(TikTokTelegramBot
    WIN32
    src/main.cpp
    src/ui/SettingsController.cpp
    src/ui/SettingsController.h
    src/ui/SettingsCatalog.cpp
    src/ui/TrayController.cpp
    src/ui/TrayController.h
)
target_compile_features(TikTokTelegramBot PRIVATE cxx_std_23)
target_link_libraries(TikTokTelegramBot
    PRIVATE
        tiktok_bot_core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
)

set(TIKTOK_BOT_QML_FILES
    qml/AppButton.qml
    qml/CatalogPage.qml
    qml/DashboardPage.qml
    qml/LogsPage.qml
    qml/Main.qml
    qml/MetricCard.qml
    qml/SchedulePage.qml
    qml/SettingsPage.qml
    qml/VideoCard.qml
    qml/BatchAddDialog.qml
    qml/VideoEditorDialog.qml
    qml/PublicationCalendar.qml
    qml/ProductivitySettings.qml
)

foreach(qml_file IN LISTS TIKTOK_BOT_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_qml_module(TikTokTelegramBot
    URI TikTokBot.Ui
    VERSION 1.0
    QML_FILES
        ${TIKTOK_BOT_QML_FILES}
)

qt_add_translations(TikTokTelegramBot
    TS_FILES
        translations/TikTokTelegramBot_ru_RU.ts
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

set(TIKTOK_BOT_FORMAT_FILES ${TIKTOK_BOT_SOURCES}
    src/main.cpp
    src/ui/SettingsController.cpp
    src/ui/SettingsController.h
    src/ui/TrayController.cpp
    src/ui/TrayController.h
    tests/FakeTdJson.cpp
    tests/TestAppConfig.cpp
    tests/TestAppDatabase.cpp
    tests/TestAppDatabaseValidation.cpp
    tests/TestScheduleCalculator.cpp
    tests/TestTdJsonClient.cpp
    tests/TestTdPublisher.cpp
    tests/TestTelegramApiResponse.cpp
    tests/TestTikTokUrlValidator.cpp
    src/ui/SettingsCatalog.cpp
    tests/TestCacheManager.cpp
    tests/TestSingleInstance.cpp
    tests/TestDurableWorkflow.cpp
    tests/TestDesktopWorkflow.cpp
)

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${TIKTOK_BOT_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting C++ sources with the project Beningo profile"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${TIKTOK_BOT_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking the project Beningo formatting profile"
        VERBATIM
    )
endif()

if(BUILD_DOCUMENTATION)
    find_package(Doxygen REQUIRED)
    configure_file(Doxyfile.in Doxyfile @ONLY)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen API documentation"
        VERBATIM
    )
endif()

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
