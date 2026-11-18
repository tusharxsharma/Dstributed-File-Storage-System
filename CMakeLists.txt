cmake_minimum_required(VERSION 3.20)

project(distributed_file_storage
    VERSION 0.1.0
    DESCRIPTION "A C++20 distributed file storage system"
    LANGUAGES CXX
)

option(DFS_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(DFS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(dfs_core
    src/core/chunk_store.cpp
    src/core/file_transfer.cpp
    src/common/checksum.cpp
)
target_include_directories(dfs_core PUBLIC include)
target_link_libraries(dfs_core PUBLIC OpenSSL::Crypto)

if(DFS_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(dfs_core PRIVATE /W4 /permissive-)
    else()
        target_compile_options(dfs_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

if(DFS_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(dfs_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(dfs_core PUBLIC -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(dfs_tests
        tests/unit/checksum_test.cpp
        tests/unit/chunk_store_test.cpp
        tests/unit/file_transfer_test.cpp
    )
    target_link_libraries(dfs_tests PRIVATE dfs_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(dfs_tests)
endif()
