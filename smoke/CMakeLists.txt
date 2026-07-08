add_library(liquibook_smoke STATIC version.cpp)
target_include_directories(liquibook_smoke PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
liquibook_apply_warnings(liquibook_smoke)
liquibook_apply_sanitizers(liquibook_smoke)

if(LIQUIBOOK_BUILD_TESTS)
    add_executable(liquibook_smoke_tests test_version.cpp)
    target_link_libraries(liquibook_smoke_tests PRIVATE liquibook_smoke GTest::gtest_main)
    liquibook_apply_warnings(liquibook_smoke_tests)
    liquibook_apply_sanitizers(liquibook_smoke_tests)

    include(GoogleTest)
    gtest_discover_tests(liquibook_smoke_tests)
endif()
