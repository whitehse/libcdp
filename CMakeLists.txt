cmake_minimum_required(VERSION 3.14)
project(libcdp VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Strict warnings
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Library
add_library(cdp STATIC src/cdp.c)
target_include_directories(cdp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------- Tests ----------
enable_testing()

# Smoke test
add_executable(cdp_smoke tests/cdp_smoke.c)
target_link_libraries(cdp_smoke PRIVATE cdp)
add_test(NAME cdp_smoke COMMAND cdp_smoke)

# Dialectic test (client <-> target round-trip)
add_executable(cdp_dialectic tests/cdp_dialectic.c)
target_link_libraries(cdp_dialectic PRIVATE cdp)
add_test(NAME cdp_dialectic COMMAND cdp_dialectic)

# Error handling test
add_executable(cdp_errors tests/cdp_errors.c)
target_link_libraries(cdp_errors PRIVATE cdp)
add_test(NAME cdp_errors COMMAND cdp_errors)

# ---------- Example ----------
add_executable(cdp_page_example examples/cdp_page_example.c)
target_link_libraries(cdp_page_example PRIVATE cdp)

# ---------- Fuzz (optional) ----------
option(CDP_FUZZ "Build fuzz harnesses (requires clang)" OFF)
if(CDP_FUZZ)
    add_executable(cdp_fuzz_feed fuzz/cdp_fuzz_feed.c)
    target_link_libraries(cdp_fuzz_feed PRIVATE cdp)
    target_compile_options(cdp_fuzz_feed PRIVATE -fsanitize=fuzzer,address)
    target_link_options(cdp_fuzz_feed PRIVATE -fsanitize=fuzzer,address)
endif()

# ---------- Valgrind (optional) ----------
find_program(VALGRIND valgrind)
if(VALGRIND)
    add_custom_target(valgrind_smoke
        COMMAND ${VALGRIND} --leak-check=full --error-exitcode=1
                $<TARGET_FILE:cdp_smoke>
        DEPENDS cdp_smoke
        COMMENT "Running cdp_smoke under Valgrind")

    add_custom_target(valgrind_dialectic
        COMMAND ${VALGRIND} --leak-check=full --error-exitcode=1
                $<TARGET_FILE:cdp_dialectic>
        DEPENDS cdp_dialectic
        COMMENT "Running cdp_dialectic under Valgrind")

    add_custom_target(valgrind_errors
        COMMAND ${VALGRIND} --leak-check=full --error-exitcode=1
                $<TARGET_FILE:cdp_errors>
        DEPENDS cdp_errors
        COMMENT "Running cdp_errors under Valgrind")

    add_custom_target(valgrind DEPENDS
        valgrind_smoke valgrind_dialectic valgrind_errors)
endif()

# ---------- Install ----------
include(GNUInstallDirs)
install(TARGETS cdp ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/cdp.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
