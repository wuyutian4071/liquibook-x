# Header-only: MatchingEngine is a class template over EventHandler, so its definition
# can't be split into a .cpp without explicit instantiation (which would defeat the point of
# a compile-time-resolved, not-std::function callback). This target exists only to carry
# include paths and dependencies to consumers/tests, not to compile anything of its own.
add_library(liquibook_engine INTERFACE)
target_include_directories(liquibook_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(liquibook_engine INTERFACE liquibook_book)

if(LIQUIBOOK_BUILD_TESTS)
    add_executable(liquibook_engine_tests
        tests/test_matching_engine.cpp
    )
    target_link_libraries(liquibook_engine_tests PRIVATE liquibook_engine GTest::gtest_main)
    liquibook_apply_warnings(liquibook_engine_tests)
    liquibook_apply_sanitizers(liquibook_engine_tests)

    include(GoogleTest)
    gtest_discover_tests(liquibook_engine_tests)
endif()
