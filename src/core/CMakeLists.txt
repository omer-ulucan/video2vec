add_library(video2vec-core
    logger.cpp
    thread_pool.cpp
    cancellation.cpp
    config.cpp
    metrics.cpp
    memory.cpp
)

add_library(video2vec::core ALIAS video2vec-core)

# Resolve third-party dependencies.  For static libraries we extract the
# file path so install(EXPORT) does not leak FetchContent targets.  For
# system shared libraries we link to the CMake target directly so
# transitive dependencies (e.g. spdlog -> fmt) are preserved.
set(CORE_PRIVATE_LIBS)
set(CORE_PRIVATE_INCLUDES ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})

macro(_link_third_party_target target_name)
    get_target_property(_type ${target_name} TYPE)
    get_target_property(_inc ${target_name} INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _inc)
        get_target_property(_inc ${target_name} INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    endif()
    if(_type STREQUAL "STATIC_LIBRARY")
        list(APPEND CORE_PRIVATE_LIBS $<TARGET_FILE:${target_name}>)
    else()
        list(APPEND CORE_PRIVATE_LIBS ${target_name})
    endif()
    if(_inc)
        list(APPEND CORE_PRIVATE_INCLUDES ${_inc})
    endif()
endmacro()

if(TARGET spdlog::spdlog)
    _link_third_party_target(spdlog::spdlog)
elseif(TARGET spdlog)
    _link_third_party_target(spdlog)
elseif(SPDLOG_FOUND)
    list(APPEND CORE_PRIVATE_LIBS ${SPDLOG_LIBRARIES})
    list(APPEND CORE_PRIVATE_INCLUDES ${SPDLOG_INCLUDE_DIRS})
endif()

if(TARGET yaml-cpp)
    _link_third_party_target(yaml-cpp)
elseif(YAML_CPP_FOUND)
    list(APPEND CORE_PRIVATE_LIBS ${YAML_CPP_LIBRARIES})
    list(APPEND CORE_PRIVATE_INCLUDES ${YAML_CPP_INCLUDE_DIRS})
endif()

if(TARGET nlohmann_json::nlohmann_json)
    get_target_property(_json_inc nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _json_inc)
        get_target_property(_json_inc nlohmann_json::nlohmann_json INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    endif()
    if(_json_inc)
        list(APPEND CORE_PRIVATE_INCLUDES ${_json_inc})
    endif()
elseif(NLOHMANN_JSON_FOUND)
    list(APPEND CORE_PRIVATE_INCLUDES ${NLOHMANN_JSON_INCLUDE_DIRS})
endif()

target_include_directories(video2vec-core
    PUBLIC
        $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CORE_PRIVATE_INCLUDES}
)

target_link_libraries(video2vec-core
    PUBLIC Threads::Threads
    PRIVATE ${CORE_PRIVATE_LIBS}
)

set_target_properties(video2vec-core PROPERTIES
    OUTPUT_NAME video2vec-core
    EXPORT_NAME core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS video2vec-core
    EXPORT video2vecTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
