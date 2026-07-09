if(LIQUIBOOK_BUILD_BENCHMARKS)
    add_executable(liquibook_bench_parser bench_parser.cpp)
    target_link_libraries(liquibook_bench_parser PRIVATE liquibook_itch benchmark::benchmark)
    liquibook_apply_warnings(liquibook_bench_parser)
    # Deliberately no liquibook_apply_sanitizers() here -- ASan/UBSan overhead would make the
    # reported throughput numbers meaningless. Same reasoning applies to every target below.

    add_executable(liquibook_bench_order_book_latency bench_order_book_latency.cpp)
    target_link_libraries(liquibook_bench_order_book_latency PRIVATE liquibook_book benchmark::benchmark)
    liquibook_apply_warnings(liquibook_bench_order_book_latency)

    add_executable(liquibook_bench_matching_engine_latency bench_matching_engine_latency.cpp)
    target_link_libraries(liquibook_bench_matching_engine_latency PRIVATE liquibook_engine benchmark::benchmark)
    liquibook_apply_warnings(liquibook_bench_matching_engine_latency)

    add_executable(liquibook_bench_ring_buffer_latency bench_ring_buffer_latency.cpp)
    target_link_libraries(liquibook_bench_ring_buffer_latency PRIVATE liquibook_containers Threads::Threads benchmark::benchmark)
    liquibook_apply_warnings(liquibook_bench_ring_buffer_latency)
endif()
