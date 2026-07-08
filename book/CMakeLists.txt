add_library(liquibook_book STATIC
    order_book.cpp
)
target_include_directories(liquibook_book PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(liquibook_book PUBLIC liquibook_itch liquibook_containers)
liquibook_apply_warnings(liquibook_book)
liquibook_apply_sanitizers(liquibook_book)

if(LIQUIBOOK_BUILD_TESTS)
    add_executable(liquibook_book_tests
        tests/test_order_book.cpp
    )
    target_link_libraries(liquibook_book_tests PRIVATE liquibook_book GTest::gtest_main)
    liquibook_apply_warnings(liquibook_book_tests)
    liquibook_apply_sanitizers(liquibook_book_tests)

    include(GoogleTest)
    gtest_discover_tests(liquibook_book_tests)
endif()
