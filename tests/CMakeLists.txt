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
