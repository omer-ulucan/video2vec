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

# Build fetched third-party dependencies as static libraries so they
# are embedded into our shared libraries and do not leak as runtime
# dependencies that would break install(EXPORT).
set(_VIDEO2VEC_BUILD_SHARED_LIBS_SAVE ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

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

set(BUILD_SHARED_LIBS ${_VIDEO2VEC_BUILD_SHARED_LIBS_SAVE} CACHE BOOL "" FORCE)

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

# --- Local prebuilt dependencies ---
set(VIDEO2VEC_LOCAL_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps" CACHE PATH "Local prebuilt dependencies directory")

# whisper.cpp
set(_WHISPER_LIB "")
if(EXISTS "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/src/libwhisper.a")
    set(_WHISPER_LIB "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/src/libwhisper.a")
elseif(EXISTS "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/src/libwhisper.so")
    set(_WHISPER_LIB "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/src/libwhisper.so")
endif()
if(_WHISPER_LIB)
    add_library(whisper STATIC IMPORTED)
    set_target_properties(whisper PROPERTIES
        IMPORTED_LOCATION "${_WHISPER_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-src/include;${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-src/ggml/include"
    )
    if(EXISTS "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/ggml/src/libggml.a")
        set_property(TARGET whisper APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/ggml/src/libggml.a")
    elseif(EXISTS "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/ggml/src/libggml.so")
        set_property(TARGET whisper APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${VIDEO2VEC_LOCAL_DEPS_DIR}/whisper-build/ggml/src/libggml.so")
    endif()
    if(OpenMP_C_FOUND OR OpenMP_CXX_FOUND)
        find_package(OpenMP)
        if(OpenMP_FOUND)
            set_property(TARGET whisper APPEND PROPERTY INTERFACE_LINK_LIBRARIES OpenMP::OpenMP_CXX)
        endif()
    endif()
    message(STATUS "Using local whisper.cpp from ${VIDEO2VEC_LOCAL_DEPS_DIR} (${_WHISPER_LIB})")
endif()

# ONNX Runtime
if(EXISTS "${VIDEO2VEC_LOCAL_DEPS_DIR}/onnxruntime-linux-x64-1.18.0")
    set(onnxruntime_ROOT "${VIDEO2VEC_LOCAL_DEPS_DIR}/onnxruntime-linux-x64-1.18.0" CACHE PATH "" FORCE)
    find_library(ONNXRUNTIME_LIB onnxruntime PATHS "${VIDEO2VEC_LOCAL_DEPS_DIR}/onnxruntime-linux-x64-1.18.0/lib" NO_DEFAULT_PATH)
    if(ONNXRUNTIME_LIB)
        add_library(onnxruntime::onnxruntime SHARED IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${VIDEO2VEC_LOCAL_DEPS_DIR}/onnxruntime-linux-x64-1.18.0/include"
        )
        message(STATUS "Using local ONNX Runtime from ${VIDEO2VEC_LOCAL_DEPS_DIR}/onnxruntime-linux-x64-1.18.0")
    endif()
endif()

# Tesseract (headers from source, library from system)
if(EXISTS "${VIDEO2VEC_LOCAL_DEPS_DIR}/tesseract-5.5.2/src/api")
    find_library(_TESSERACT_LIB tesseract NAMES tesseract tesseract.so.5 tesseract.so.5.5 PATH_SUFFIXES lib lib64 x86_64-linux-gnu)
    if(_TESSERACT_LIB)
        add_library(Tesseract::libtesseract SHARED IMPORTED)
        set_target_properties(Tesseract::libtesseract PROPERTIES
            IMPORTED_LOCATION "${_TESSERACT_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${VIDEO2VEC_LOCAL_DEPS_DIR}/tesseract-5.5.2/include;${VIDEO2VEC_LOCAL_DEPS_DIR}/tesseract-5.5.2/src/api;${VIDEO2VEC_LOCAL_DEPS_DIR}/tesseract-5.5.2/src/ccutil;${VIDEO2VEC_LOCAL_DEPS_DIR}/leptonica-1.87.0;${VIDEO2VEC_LOCAL_DEPS_DIR}/leptonica-build/src"
        )
        message(STATUS "Using system libtesseract with local headers: ${_TESSERACT_LIB}")
    else()
        message(WARNING "Tesseract headers downloaded but system library not found. Install libtesseract-dev (Ubuntu) or tesseract-devel (Fedora).")
    endif()
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
install(EXPORT video2vecTargets
    FILE video2vecTargets.cmake
    NAMESPACE video2vec::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/video2vec
)

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
