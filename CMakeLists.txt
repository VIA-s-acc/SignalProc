cmake_minimum_required(VERSION 3.28)
project(signals VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SIGNALS_SANITIZE "Build own targets with AddressSanitizer and UBSan" OFF)
option(SIGNALS_WERROR "Treat warnings in own targets as errors" OFF)
option(SIGNALS_BUILD_TESTS "Build the test suite" ON)

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src/core)
add_subdirectory(app)

if(SIGNALS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
