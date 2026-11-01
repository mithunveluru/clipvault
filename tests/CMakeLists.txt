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

# suites added after the first release build with warnings as errors
set(CLIPVAULT_STRICT_TESTS
    test_migration
    test_repository_fields
    test_settings
    test_secret_detector
    test_ephemeral_store
)

foreach(test IN LISTS CLIPVAULT_STRICT_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE clipvault_lib Qt6::Test)
    target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_definitions(${test} PRIVATE CLIPVAULT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# release gate that needs strace and xvfb and is skipped nowhere in ci
find_program(STRACE strace)
find_program(XVFB_RUN xvfb-run)
find_program(DBUS_RUN_SESSION dbus-run-session)
if(STRACE AND XVFB_RUN AND DBUS_RUN_SESSION)
    add_test(NAME no_network COMMAND ${PROJECT_SOURCE_DIR}/tools/check_no_network.sh ${PROJECT_BINARY_DIR})
elseif(DEFINED ENV{CI})
    message(FATAL_ERROR "strace xvfb-run and dbus-run-session are required for the no network gate")
endif()
