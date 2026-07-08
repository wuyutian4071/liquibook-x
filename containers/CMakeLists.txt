add_library(liquibook_containers INTERFACE)
target_include_directories(liquibook_containers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if(LIQUIBOOK_BUILD_TESTS)
    add_executable(liquibook_containers_tests
        tests/test_object_pool.cpp
        tests/test_intrusive_list.cpp
        tests/test_hash_map.cpp
        tests/test_spsc_ring_buffer.cpp
        ${CMAKE_SOURCE_DIR}/testing/allocation_guard.cpp
    )
    target_include_directories(liquibook_containers_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(liquibook_containers_tests PRIVATE
        liquibook_containers
        GTest::gtest_main
        Threads::Threads
    )
    liquibook_apply_warnings(liquibook_containers_tests)
    liquibook_apply_sanitizers(liquibook_containers_tests)

    include(GoogleTest)
    gtest_discover_tests(liquibook_containers_tests)
endif()
