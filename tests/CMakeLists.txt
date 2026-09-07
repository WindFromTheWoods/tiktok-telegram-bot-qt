find_package(Qt6 6.11.2 REQUIRED COMPONENTS Test)

function(tiktok_bot_register_test name target)
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_PLUGIN_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    )
endfunction()

qt_add_executable(TestAppConfig TestAppConfig.cpp)
target_compile_features(TestAppConfig PRIVATE cxx_std_23)
target_link_libraries(TestAppConfig PRIVATE tiktok_bot_core Qt6::Test)
tiktok_bot_enable_warnings(TestAppConfig)
tiktok_bot_register_test(AppConfig TestAppConfig)

qt_add_executable(TestTikTokUrlValidator TestTikTokUrlValidator.cpp)
target_compile_features(TestTikTokUrlValidator PRIVATE cxx_std_23)
target_link_libraries(TestTikTokUrlValidator PRIVATE tiktok_bot_core Qt6::Test)
tiktok_bot_enable_warnings(TestTikTokUrlValidator)
tiktok_bot_register_test(TikTokUrlValidator TestTikTokUrlValidator)

qt_add_executable(TestTelegramApiResponse TestTelegramApiResponse.cpp)
target_compile_features(TestTelegramApiResponse PRIVATE cxx_std_23)
target_link_libraries(TestTelegramApiResponse PRIVATE tiktok_bot_core Qt6::Test)
tiktok_bot_enable_warnings(TestTelegramApiResponse)
tiktok_bot_register_test(TelegramApiResponse TestTelegramApiResponse)

qt_add_executable(TestScheduleCalculator TestScheduleCalculator.cpp)
target_compile_features(TestScheduleCalculator PRIVATE cxx_std_23)
target_link_libraries(TestScheduleCalculator PRIVATE tiktok_bot_core Qt6::Test)
tiktok_bot_enable_warnings(TestScheduleCalculator)
tiktok_bot_register_test(ScheduleCalculator TestScheduleCalculator)

qt_add_executable(TestAppDatabase TestAppDatabase.cpp)
target_compile_features(TestAppDatabase PRIVATE cxx_std_23)
target_link_libraries(TestAppDatabase PRIVATE tiktok_bot_core Qt6::Test)
tiktok_bot_enable_warnings(TestAppDatabase)
tiktok_bot_register_test(AppDatabase TestAppDatabase)

qt_add_executable(TestAppDatabaseValidation TestAppDatabaseValidation.cpp)
target_compile_features(TestAppDatabaseValidation PRIVATE cxx_std_23)
target_link_libraries(TestAppDatabaseValidation PRIVATE tiktok_bot_core Qt6::Test)
tiktok_bot_enable_warnings(TestAppDatabaseValidation)
tiktok_bot_register_test(AppDatabaseValidation TestAppDatabaseValidation)

qt_add_library(FakeTdJson SHARED FakeTdJson.cpp)
target_compile_features(FakeTdJson PRIVATE cxx_std_23)
target_link_libraries(FakeTdJson PRIVATE Qt6::Core)
set_target_properties(FakeTdJson PROPERTIES OUTPUT_NAME tdjson_test)

qt_add_executable(TestTdJsonClient TestTdJsonClient.cpp)
target_compile_features(TestTdJsonClient PRIVATE cxx_std_23)
target_link_libraries(TestTdJsonClient PRIVATE tiktok_bot_core Qt6::Test)
target_compile_definitions(TestTdJsonClient PRIVATE
    TEST_TDJSON_PATH="$<TARGET_FILE:FakeTdJson>"
)
add_dependencies(TestTdJsonClient FakeTdJson)
tiktok_bot_enable_warnings(TestTdJsonClient)
tiktok_bot_register_test(TdJsonClient TestTdJsonClient)

qt_add_executable(TestTdPublisher TestTdPublisher.cpp)
target_compile_features(TestTdPublisher PRIVATE cxx_std_23)
target_link_libraries(TestTdPublisher PRIVATE tiktok_bot_core Qt6::Test)
target_compile_definitions(TestTdPublisher PRIVATE
    TEST_TDJSON_PATH="$<TARGET_FILE:FakeTdJson>"
)
add_dependencies(TestTdPublisher FakeTdJson)
tiktok_bot_enable_warnings(TestTdPublisher)
tiktok_bot_register_test(TdPublisher TestTdPublisher)

foreach(suite IN ITEMS CacheManager SingleInstance DurableWorkflow)
    qt_add_executable(Test${suite} Test${suite}.cpp)
    target_link_libraries(Test${suite} PRIVATE tiktok_bot_core Qt6::Test)
    tiktok_bot_enable_warnings(Test${suite})
    tiktok_bot_register_test(${suite} Test${suite})
endforeach()

qt_add_executable(TestDesktopWorkflow TestDesktopWorkflow.cpp
    ../src/ui/SettingsController.cpp ../src/ui/SettingsController.h ../src/ui/SettingsCatalog.cpp)
target_link_libraries(TestDesktopWorkflow PRIVATE tiktok_bot_core Qt6::Test Qt6::Gui)
tiktok_bot_enable_warnings(TestDesktopWorkflow)
tiktok_bot_register_test(DesktopWorkflow TestDesktopWorkflow)
