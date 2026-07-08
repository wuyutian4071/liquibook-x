# Header-only: run_itch_pipeline is a plain inline function, but the module exists at the
# same layering level as engine/ -- a thin composition of itch/ + containers/ + book/, not
# folded backwards into containers/tests/ (which would invert containers/'s foundational-
# module dependency direction).
add_library(liquibook_pipeline INTERFACE)
target_include_directories(liquibook_pipeline INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(liquibook_pipeline INTERFACE liquibook_itch liquibook_book liquibook_containers Threads::Threads)

if(LIQUIBOOK_BUILD_TESTS)
    add_executable(liquibook_pipeline_tests
        tests/test_itch_pipeline.cpp
    )
    target_link_libraries(liquibook_pipeline_tests PRIVATE liquibook_pipeline GTest::gtest_main)
    liquibook_apply_warnings(liquibook_pipeline_tests)
    liquibook_apply_sanitizers(liquibook_pipeline_tests)

    include(GoogleTest)
    gtest_discover_tests(liquibook_pipeline_tests)
endif()
