add_library(liquibook_itch STATIC
    decode.cpp
    file_reader.cpp
    synth.cpp
)
target_include_directories(liquibook_itch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
liquibook_apply_warnings(liquibook_itch)
liquibook_apply_sanitizers(liquibook_itch)

if(LIQUIBOOK_BUILD_TESTS)
    add_executable(liquibook_itch_tests
        tests/test_byteorder.cpp
        tests/test_decode.cpp
        tests/test_file_reader.cpp
        tests/test_synth_roundtrip.cpp
    )
    target_link_libraries(liquibook_itch_tests PRIVATE liquibook_itch GTest::gtest_main)
    liquibook_apply_warnings(liquibook_itch_tests)
    liquibook_apply_sanitizers(liquibook_itch_tests)

    include(GoogleTest)
    gtest_discover_tests(liquibook_itch_tests)
endif()
