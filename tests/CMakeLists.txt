cmake_minimum_required(VERSION 3.24)
project(espbase_tests CXX C)

# Enforce modern C++
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(espbase_tests
    espbase/esp_result_test.cpp
)

target_include_directories(espbase_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../emulated/esp-idf/include
)

target_link_libraries(espbase_tests PRIVATE
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(espbase_tests)
