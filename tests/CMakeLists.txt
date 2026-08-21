find_package(Qt6 6.11.2 REQUIRED COMPONENTS Test)

function(tiktok_bot_register_test name target)
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
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
