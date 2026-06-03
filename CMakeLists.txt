cmake_minimum_required(VERSION 3.25)
project(video2vec VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(BUILD_CLI "Build CLI tools" ON)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /permissive-)
    if(WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
endif()

if(ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

if(ENABLE_TSAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    pkg_check_modules(SPDLOG spdlog)
    if(NOT SPDLOG_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.14.1
        )
        FetchContent_MakeAvailable(spdlog)
    endif()
endif()

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    pkg_check_modules(NLOHMANN_JSON nlohmann_json)
    if(NOT NLOHMANN_JSON_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    pkg_check_modules(YAML_CPP yaml-cpp)
    if(NOT YAML_CPP_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0.8.0
        )
        FetchContent_MakeAvailable(yaml-cpp)
    endif()
endif()

find_package(cxxopts QUIET)
if(NOT cxxopts_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
        GIT_TAG v3.2.0
    )
    FetchContent_MakeAvailable(cxxopts)
endif()

pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale
    libswresample
)

pkg_check_modules(VIPS vips)
if(NOT VIPS_FOUND)
    message(STATUS "libvips not found, vision module will be limited")
endif()

find_package(onnxruntime QUIET)
find_package(faiss QUIET)
find_package(FlatBuffers QUIET)
find_package(Tesseract QUIET)

set(VIDEO2VEC_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(VIDEO2VEC_PRIVATE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(VIDEO2VEC_MODULES core ffmpeg sync windowing asr ocr vision embedding flatbuffers index query)

foreach(mod ${VIDEO2VEC_MODULES})
    add_subdirectory(src/${mod})
endforeach()

if(BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

install(DIRECTORY include/video2vec DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/video2vecConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/video2vecConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/video2vecConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/video2vec
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/video2vecConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/video2vecConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/video2vec
)
# Note: install(EXPORT) disabled when using FetchContent for dependencies.
# Re-enable once all dependencies are found via find_package instead of FetchContent.
# install(EXPORT video2vecTargets
#     FILE video2vecTargets.cmake
#     NAMESPACE video2vec::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/video2vec
# )

message(STATUS "")
message(STATUS "video2vec ${PROJECT_VERSION} configuration:")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  CLI: ${BUILD_CLI}")
message(STATUS "")
