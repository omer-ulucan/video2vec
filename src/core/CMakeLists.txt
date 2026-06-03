add_library(video2vec-core
    logger.cpp
    thread_pool.cpp
    cancellation.cpp
    config.cpp
    metrics.cpp
    memory.cpp
)

add_library(video2vec::core ALIAS video2vec-core)

# Resolve third-party dependencies without linking to CMake targets
# that cannot be exported (e.g. FetchContent-generated targets).
set(CORE_PRIVATE_LIBS)
set(CORE_PRIVATE_INCLUDES ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})

if(TARGET spdlog)
    get_target_property(_spdlog_inc spdlog INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _spdlog_inc)
        get_target_property(_spdlog_inc spdlog INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    endif()
    list(APPEND CORE_PRIVATE_LIBS $<TARGET_FILE:spdlog>)
    if(_spdlog_inc)
        list(APPEND CORE_PRIVATE_INCLUDES ${_spdlog_inc})
    endif()
elseif(SPDLOG_FOUND)
    list(APPEND CORE_PRIVATE_LIBS ${SPDLOG_LIBRARIES})
    list(APPEND CORE_PRIVATE_INCLUDES ${SPDLOG_INCLUDE_DIRS})
endif()

if(TARGET yaml-cpp)
    get_target_property(_yaml_inc yaml-cpp INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _yaml_inc)
        get_target_property(_yaml_inc yaml-cpp INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
    endif()
    list(APPEND CORE_PRIVATE_LIBS $<TARGET_FILE:yaml-cpp>)
    if(_yaml_inc)
        list(APPEND CORE_PRIVATE_INCLUDES ${_yaml_inc})
    endif()
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
