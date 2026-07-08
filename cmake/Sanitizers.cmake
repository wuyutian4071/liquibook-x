# Sanitizer setup, shared by every target via liquibook_apply_sanitizers().
#
# ENABLE_ASAN and ENABLE_UBSAN may combine (both are Debug-time correctness tools with
# tolerable overhead together). ENABLE_TSAN is mutually exclusive with both -- ASan/TSan
# use incompatible shadow-memory schemes and cannot be linked into the same binary.
# TSan has nothing meaningful to test until the concurrent SPSC ring buffer lands (M6),
# but the plumbing exists now so enabling it later is a one-line CI change, not new CMake.

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_TSAN AND (ENABLE_ASAN OR ENABLE_UBSAN))
    message(FATAL_ERROR "ENABLE_TSAN cannot be combined with ENABLE_ASAN/ENABLE_UBSAN")
endif()

function(liquibook_apply_sanitizers target)
    set(_sanitizers "")
    if(ENABLE_ASAN)
        list(APPEND _sanitizers "address")
    endif()
    if(ENABLE_UBSAN)
        list(APPEND _sanitizers "undefined")
    endif()
    if(ENABLE_TSAN)
        list(APPEND _sanitizers "thread")
    endif()

    if(_sanitizers)
        list(JOIN _sanitizers "," _sanitizers_csv)
        target_compile_options(${target} PRIVATE
            -fsanitize=${_sanitizers_csv}
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=${_sanitizers_csv})
    endif()
endfunction()
