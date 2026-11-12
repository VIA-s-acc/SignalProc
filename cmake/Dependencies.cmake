include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.16.1 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(cli11 GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git GIT_TAG v2.7.2 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 12.2.0 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(nanobench GIT_REPOSITORY https://github.com/martinus/nanobench.git GIT_TAG v4.6.0 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(rapidcheck GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
                     GIT_TAG 2c3c4365aca21ef4e612768fdc95b1ce8b39a651 SYSTEM)
FetchContent_Declare(pocketfft GIT_REPOSITORY https://github.com/mreineck/pocketfft.git
                     GIT_TAG 4d865cba24359b05b37da4e75aee24498c3b78e7 SOURCE_SUBDIR _none)
FetchContent_Declare(dr_libs GIT_REPOSITORY https://github.com/mackron/dr_libs.git
                     GIT_TAG wav-0.14.6 GIT_SHALLOW TRUE SOURCE_SUBDIR _none)
FetchContent_Declare(ftxui GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git GIT_TAG v7.0.3 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(tomlplusplus GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git GIT_TAG v3.4.0 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(multiprecision GIT_REPOSITORY https://github.com/boostorg/multiprecision.git
                     GIT_TAG Boost_1_92_0 GIT_SHALLOW TRUE SOURCE_SUBDIR _none)

set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git
                     GIT_TAG 0.11.26 GIT_SHALLOW TRUE SOURCE_SUBDIR _none)
FetchContent_Declare(eigen GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git GIT_TAG 5.0.1 GIT_SHALLOW TRUE SYSTEM)
FetchContent_Declare(taskflow GIT_REPOSITORY https://github.com/taskflow/taskflow.git GIT_TAG v4.1.0 GIT_SHALLOW TRUE SOURCE_SUBDIR _none)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(CLI11_PRECOMPILED OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(catch2 cli11 json fmt nanobench rapidcheck pocketfft dr_libs ftxui tomlplusplus multiprecision eigen miniaudio taskflow)

add_library(signals_taskflow INTERFACE)
target_include_directories(signals_taskflow SYSTEM INTERFACE ${taskflow_SOURCE_DIR})

add_library(signals_multiprecision INTERFACE)
target_include_directories(signals_multiprecision SYSTEM INTERFACE ${multiprecision_SOURCE_DIR}/include)
target_compile_definitions(signals_multiprecision INTERFACE BOOST_MP_STANDALONE)

add_library(signals_pocketfft INTERFACE)
target_include_directories(signals_pocketfft SYSTEM INTERFACE ${pocketfft_SOURCE_DIR})

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_library(signals_dr_libs STATIC ${PROJECT_SOURCE_DIR}/src/io/dr_wav_impl.cpp ${PROJECT_SOURCE_DIR}/src/io/dr_flac_impl.cpp ${PROJECT_SOURCE_DIR}/src/io/dr_mp3_impl.cpp)
target_include_directories(signals_dr_libs SYSTEM PUBLIC ${dr_libs_SOURCE_DIR})
