set(CLIPVAULT_TESTS
    test_classifier
    test_repository
    test_history_service
    test_search_service
    test_clipboard_monitor
)

foreach(test IN LISTS CLIPVAULT_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE clipvault_lib Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# suites added after 1.0 build with warnings as errors
set(CLIPVAULT_STRICT_TESTS
    test_migration
    test_repository_fields
    test_settings
)

foreach(test IN LISTS CLIPVAULT_STRICT_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE clipvault_lib Qt6::Test)
    target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_definitions(${test} PRIVATE CLIPVAULT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${test} COMMAND ${test})
endforeach()
