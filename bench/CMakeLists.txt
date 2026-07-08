if(LIQUIBOOK_BUILD_BENCHMARKS)
    add_executable(liquibook_bench_parser bench_parser.cpp)
    target_link_libraries(liquibook_bench_parser PRIVATE liquibook_itch benchmark::benchmark)
    liquibook_apply_warnings(liquibook_bench_parser)
    # Deliberately no liquibook_apply_sanitizers() here -- ASan/UBSan overhead would make the
    # reported throughput numbers meaningless.
endif()
